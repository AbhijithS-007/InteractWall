#define NOMINMAX
#define STB_IMAGE_IMPLEMENTATION
#include "IBLPreprocessor.h"
#include "stb_image.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <windows.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

// ---------------------------------------------------------------
// Load an equirectangular HDR from disk
// ---------------------------------------------------------------
bool IBLPreprocessor::LoadHDR(const std::string& path, float** outData, int* outWidth, int* outHeight) {
    stbi_set_flip_vertically_on_load(true);
    int channels;
    *outData = stbi_loadf(path.c_str(), outWidth, outHeight, &channels, 4); // Force RGBA
    if (!*outData) {
        std::cout << "[IBL] Failed to load HDR: " << path << " (" << stbi_failure_reason() << ")\n";
        return false;
    }
    std::cout << "[IBL] Loaded HDR: " << *outWidth << "x" << *outHeight << " from " << path << "\n";
    return true;
}

// ---------------------------------------------------------------
// Generate a procedural studio-style HDR environment
// Soft warm key light from upper-right, cool fill from left,
// gentle ground reflection. Mimics a neutral photo studio.
// ---------------------------------------------------------------
bool IBLPreprocessor::GenerateProceduralHDR(float** outData, int* outWidth, int* outHeight) {
    const int W = 1024;
    const int H = 512;
    *outWidth = W;
    *outHeight = H;
    *outData = (float*)malloc(W * H * 4 * sizeof(float));
    if (!*outData) return false;

    const float PI = 3.14159265359f;

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float u = (float)x / (float)W;
            float v = (float)y / (float)H;

            // Convert equirectangular to direction
            float phi = u * 2.0f * PI - PI;
            float theta = v * PI - PI * 0.5f;

            float dx = cosf(theta) * cosf(phi);
            float dy = sinf(theta);
            float dz = cosf(theta) * sinf(phi);

            // Base ambient: dark blue-gray gradient
            float upness = dy * 0.5f + 0.5f;
            float r = 0.02f + 0.06f * upness;
            float g = 0.025f + 0.07f * upness;
            float b = 0.035f + 0.09f * upness;

            // Key light: warm area light from upper-right-front
            {
                float klx = 0.5f, kly = 0.7f, klz = -0.5f;
                float len = sqrtf(klx*klx + kly*kly + klz*klz);
                klx /= len; kly /= len; klz /= len;
                float d = dx*klx + dy*kly + dz*klz;
                float intensity = powf(std::max(0.0f, d), 16.0f) * 8.0f; // Concentrated area light
                r += 1.0f * intensity;
                g += 0.92f * intensity;
                b += 0.82f * intensity;
            }

            // Fill light: cool from left
            {
                float flx = -0.8f, fly = 0.3f, flz = 0.2f;
                float len = sqrtf(flx*flx + fly*fly + flz*flz);
                flx /= len; fly /= len; flz /= len;
                float d = dx*flx + dy*fly + dz*flz;
                float intensity = powf(std::max(0.0f, d), 8.0f) * 2.5f;
                r += 0.6f * intensity;
                g += 0.7f * intensity;
                b += 0.9f * intensity;
            }

            // Rim/back light: subtle from behind
            {
                float rlx = -0.2f, rly = 0.1f, rlz = 0.9f;
                float len = sqrtf(rlx*rlx + rly*rly + rlz*rlz);
                rlx /= len; rly /= len; rlz /= len;
                float d = dx*rlx + dy*rly + dz*rlz;
                float intensity = powf(std::max(0.0f, d), 12.0f) * 3.0f;
                r += 0.8f * intensity;
                g += 0.85f * intensity;
                b += 1.0f * intensity;
            }

            // Ground plane reflection: warm floor
            if (dy < -0.1f) {
                float groundFactor = std::min(1.0f, (-dy - 0.1f) * 2.0f);
                r += 0.08f * groundFactor;
                g += 0.06f * groundFactor;
                b += 0.04f * groundFactor;
            }

            // Overhead soft panel (like a studio softbox directly above)
            {
                float d = dy; // just up direction
                float intensity = powf(std::max(0.0f, d), 4.0f) * 1.5f;
                r += 0.9f * intensity;
                g += 0.9f * intensity;
                b += 1.0f * intensity;
            }

            int idx = (y * W + x) * 4;
            (*outData)[idx + 0] = r;
            (*outData)[idx + 1] = g;
            (*outData)[idx + 2] = b;
            (*outData)[idx + 3] = 1.0f;
        }
    }

    std::cout << "[IBL] Generated procedural studio HDR environment (" << W << "x" << H << ")\n";
    return true;
}

// ---------------------------------------------------------------
// Create a D3D11 texture from equirectangular float data
// ---------------------------------------------------------------
ID3D11ShaderResourceView* IBLPreprocessor::CreateEquirectTexture(
    ID3D11Device* device, ID3D11DeviceContext* context,
    float* data, int width, int height)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data;
    initData.SysMemPitch = width * 4 * sizeof(float);

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &initData, &tex);
    if (FAILED(hr)) {
        std::cout << "[IBL] Failed to create equirect texture. HR=0x" << std::hex << hr << "\n";
        return nullptr;
    }

    ID3D11ShaderResourceView* srv = nullptr;
    hr = device->CreateShaderResourceView(tex, nullptr, &srv);
    tex->Release();
    if (FAILED(hr)) return nullptr;

    return srv;
}

// ---------------------------------------------------------------
// Load a compiled compute shader from .cso file
// ---------------------------------------------------------------
ID3D11ComputeShader* IBLPreprocessor::LoadComputeShader(ID3D11Device* device, const std::string& csoPath) {
    std::ifstream file(csoPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cout << "[IBL] Failed to open shader: " << csoPath << "\n";
        return nullptr;
    }
    size_t size = (size_t)file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(size);
    file.read((char*)data.data(), size);

    ID3D11ComputeShader* cs = nullptr;
    HRESULT hr = device->CreateComputeShader(data.data(), data.size(), nullptr, &cs);
    if (FAILED(hr)) {
        std::cout << "[IBL] Failed to create compute shader from " << csoPath << " HR=0x" << std::hex << hr << "\n";
        return nullptr;
    }
    return cs;
}

// ---------------------------------------------------------------
// Convert equirectangular to cubemap
// ---------------------------------------------------------------
bool IBLPreprocessor::ConvertEquirectToCubemap(
    ID3D11Device* device, ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* equirectSRV,
    ID3D11ShaderResourceView** outCubeSRV,
    ID3D11Texture2D** outCubeTex,
    int faceSize, const std::string& shaderDir)
{
    // Create cubemap texture (TextureCube = Texture2DArray with 6 slices)
    D3D11_TEXTURE2D_DESC cubeDesc = {};
    cubeDesc.Width = faceSize;
    cubeDesc.Height = faceSize;
    cubeDesc.MipLevels = 0; // Full mip chain
    cubeDesc.ArraySize = 6;
    cubeDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    cubeDesc.SampleDesc.Count = 1;
    cubeDesc.Usage = D3D11_USAGE_DEFAULT;
    cubeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
    cubeDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_GENERATE_MIPS;

    HRESULT hr = device->CreateTexture2D(&cubeDesc, nullptr, outCubeTex);
    if (FAILED(hr)) {
        std::cout << "[IBL] Failed to create cubemap texture. HR=0x" << std::hex << hr << "\n";
        return false;
    }

    // Create UAV for mip 0 (all 6 faces)
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
    uavDesc.Texture2DArray.MipSlice = 0;
    uavDesc.Texture2DArray.FirstArraySlice = 0;
    uavDesc.Texture2DArray.ArraySize = 6;

    ID3D11UnorderedAccessView* cubeUAV = nullptr;
    hr = device->CreateUnorderedAccessView(*outCubeTex, &uavDesc, &cubeUAV);
    if (FAILED(hr)) {
        std::cout << "[IBL] Failed to create cubemap UAV. HR=0x" << std::hex << hr << "\n";
        return false;
    }

    // Load compute shader
    ID3D11ComputeShader* cs = LoadComputeShader(device, shaderDir + "\\EquirectToCube.cso");
    if (!cs) { cubeUAV->Release(); return false; }

    // Create constant buffer
    struct CBConvert { UINT faceSize; UINT pad[3]; };
    CBConvert cbData = { (UINT)faceSize, {0,0,0} };
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(CBConvert);
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA cbInit = {};
    cbInit.pSysMem = &cbData;
    ID3D11Buffer* cb = nullptr;
    device->CreateBuffer(&cbDesc, &cbInit, &cb);

    // Create sampler for equirect sampling
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    ID3D11SamplerState* sampler = nullptr;
    device->CreateSamplerState(&sampDesc, &sampler);

    // Dispatch
    context->CSSetShader(cs, nullptr, 0);
    context->CSSetConstantBuffers(0, 1, &cb);
    context->CSSetShaderResources(0, 1, &equirectSRV);
    context->CSSetUnorderedAccessViews(0, 1, &cubeUAV, nullptr);
    context->CSSetSamplers(0, 1, &sampler);

    UINT groupsX = (faceSize + 7) / 8;
    UINT groupsY = (faceSize + 7) / 8;
    context->Dispatch(groupsX, groupsY, 6);

    // Unbind
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->CSSetShaderResources(0, 1, &nullSRV);

    // Create SRV for the cubemap
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = -1;
    hr = device->CreateShaderResourceView(*outCubeTex, &srvDesc, outCubeSRV);

    // Generate mips for the environment cubemap
    if (SUCCEEDED(hr)) {
        context->GenerateMips(*outCubeSRV);
    }

    cubeUAV->Release();
    cs->Release();
    cb->Release();
    sampler->Release();

    std::cout << "[IBL] Converted equirect to cubemap (" << faceSize << "x" << faceSize << " per face)\n";
    return SUCCEEDED(hr);
}

// ---------------------------------------------------------------
// Generate irradiance map
// ---------------------------------------------------------------
bool IBLPreprocessor::GenerateIrradianceMap(
    ID3D11Device* device, ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* cubeSRV, const std::string& shaderDir)
{
    const int SIZE = IRRADIANCE_SIZE;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = SIZE;
    desc.Height = SIZE;
    desc.MipLevels = 1;
    desc.ArraySize = 6;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    ID3D11Texture2D* irradianceTex = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &irradianceTex);
    if (FAILED(hr)) return false;

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
    uavDesc.Texture2DArray.MipSlice = 0;
    uavDesc.Texture2DArray.FirstArraySlice = 0;
    uavDesc.Texture2DArray.ArraySize = 6;

    ID3D11UnorderedAccessView* uav = nullptr;
    device->CreateUnorderedAccessView(irradianceTex, &uavDesc, &uav);

    ID3D11ComputeShader* cs = LoadComputeShader(device, shaderDir + "\\IrradianceConvolve.cso");
    if (!cs) { uav->Release(); irradianceTex->Release(); return false; }

    struct CBIrradiance { UINT irradianceSize; UINT pad[3]; };
    CBIrradiance cbData = { (UINT)SIZE, {0,0,0} };
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(CBIrradiance);
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA cbInit = {};
    cbInit.pSysMem = &cbData;
    ID3D11Buffer* cb = nullptr;
    device->CreateBuffer(&cbDesc, &cbInit, &cb);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    ID3D11SamplerState* sampler = nullptr;
    device->CreateSamplerState(&sampDesc, &sampler);

    context->CSSetShader(cs, nullptr, 0);
    context->CSSetConstantBuffers(0, 1, &cb);
    context->CSSetShaderResources(0, 1, &cubeSRV);
    context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    context->CSSetSamplers(0, 1, &sampler);

    context->Dispatch((SIZE + 7) / 8, (SIZE + 7) / 8, 6);

    // Unbind
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->CSSetShaderResources(0, 1, &nullSRV);

    // Create SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = 1;
    device->CreateShaderResourceView(irradianceTex, &srvDesc, &m_irradianceSRV);

    uav->Release();
    cs->Release();
    cb->Release();
    sampler->Release();
    irradianceTex->Release();

    std::cout << "[IBL] Generated irradiance map (" << SIZE << "x" << SIZE << " per face)\n";
    return m_irradianceSRV != nullptr;
}

// ---------------------------------------------------------------
// Generate prefiltered specular map with mip chain
// ---------------------------------------------------------------
bool IBLPreprocessor::GeneratePrefilterMap(
    ID3D11Device* device, ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* cubeSRV, const std::string& shaderDir)
{
    const int SIZE = PREFILTERED_SIZE;
    m_prefilteredMipCount = (int)log2((double)SIZE) + 1; // e.g. 8 for 128

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = SIZE;
    desc.Height = SIZE;
    desc.MipLevels = m_prefilteredMipCount;
    desc.ArraySize = 6;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    ID3D11Texture2D* prefilteredTex = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &prefilteredTex);
    if (FAILED(hr)) {
        std::cout << "[IBL] Failed to create prefiltered texture. HR=0x" << std::hex << hr << "\n";
        return false;
    }

    ID3D11ComputeShader* cs = LoadComputeShader(device, shaderDir + "\\PrefilterSpecular.cso");
    if (!cs) { prefilteredTex->Release(); return false; }

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    ID3D11SamplerState* sampler = nullptr;
    device->CreateSamplerState(&sampDesc, &sampler);

    // Process each mip level
    for (int mip = 0; mip < m_prefilteredMipCount; mip++) {
        int mipSize = SIZE >> mip;
        if (mipSize < 1) mipSize = 1;

        float roughness = (float)mip / (float)(m_prefilteredMipCount - 1);

        // Create UAV for this mip level
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Texture2DArray.MipSlice = mip;
        uavDesc.Texture2DArray.FirstArraySlice = 0;
        uavDesc.Texture2DArray.ArraySize = 6;

        ID3D11UnorderedAccessView* uav = nullptr;
        device->CreateUnorderedAccessView(prefilteredTex, &uavDesc, &uav);

        // Constant buffer for this mip
        struct CBPrefilter {
            UINT mipSize;
            float roughness;
            UINT totalMips;
            UINT sourceFaceSize;
        };
        CBPrefilter cbData = { (UINT)mipSize, roughness, (UINT)m_prefilteredMipCount, (UINT)SIZE };
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(CBPrefilter);
        cbDesc.Usage = D3D11_USAGE_DEFAULT;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA cbInit = {};
        cbInit.pSysMem = &cbData;
        ID3D11Buffer* cb = nullptr;
        device->CreateBuffer(&cbDesc, &cbInit, &cb);

        context->CSSetShader(cs, nullptr, 0);
        context->CSSetConstantBuffers(0, 1, &cb);
        context->CSSetShaderResources(0, 1, &cubeSRV);
        context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        context->CSSetSamplers(0, 1, &sampler);

        UINT groupsX = std::max(1, (mipSize + 7) / 8);
        UINT groupsY = std::max(1, (mipSize + 7) / 8);
        context->Dispatch(groupsX, groupsY, 6);

        // Unbind
        ID3D11UnorderedAccessView* nullUAV = nullptr;
        context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
        ID3D11ShaderResourceView* nullSRV = nullptr;
        context->CSSetShaderResources(0, 1, &nullSRV);

        uav->Release();
        cb->Release();
    }

    // Create SRV for the prefiltered cubemap
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = m_prefilteredMipCount;
    device->CreateShaderResourceView(prefilteredTex, &srvDesc, &m_prefilteredSRV);

    prefilteredTex->Release();
    cs->Release();
    sampler->Release();

    std::cout << "[IBL] Generated prefiltered specular map (" << SIZE << "x" << SIZE
              << ", " << m_prefilteredMipCount << " mip levels)\n";
    return m_prefilteredSRV != nullptr;
}

// ---------------------------------------------------------------
// Initialize from HDR file
// ---------------------------------------------------------------
bool IBLPreprocessor::Initialize(
    ID3D11Device* device, ID3D11DeviceContext* context,
    const std::string& hdrPath, const std::string& cachePath,
    const std::string& shaderDir)
{
    float* hdrData = nullptr;
    int hdrWidth, hdrHeight;

    if (!LoadHDR(hdrPath, &hdrData, &hdrWidth, &hdrHeight)) {
        std::cout << "[IBL] HDR file not found, falling back to procedural environment.\n";
        return InitializeProcedural(device, context, cachePath, shaderDir);
    }

    // Create equirectangular texture
    ID3D11ShaderResourceView* equirectSRV = CreateEquirectTexture(device, context, hdrData, hdrWidth, hdrHeight);
    stbi_image_free(hdrData);
    if (!equirectSRV) return false;

    // Convert to cubemap
    ID3D11ShaderResourceView* cubeSRV = nullptr;
    ID3D11Texture2D* cubeTex = nullptr;
    if (!ConvertEquirectToCubemap(device, context, equirectSRV, &cubeSRV, &cubeTex, 256, shaderDir)) {
        equirectSRV->Release();
        return false;
    }
    equirectSRV->Release();

    // Generate irradiance map
    if (!GenerateIrradianceMap(device, context, cubeSRV, shaderDir)) {
        cubeSRV->Release();
        cubeTex->Release();
        return false;
    }

    // Generate prefiltered specular map
    if (!GeneratePrefilterMap(device, context, cubeSRV, shaderDir)) {
        cubeSRV->Release();
        cubeTex->Release();
        return false;
    }

    cubeSRV->Release();
    cubeTex->Release();

    std::cout << "[IBL] Initialization complete.\n";
    return true;
}

// ---------------------------------------------------------------
// Initialize with procedural environment (no HDR file needed)
// ---------------------------------------------------------------
bool IBLPreprocessor::InitializeProcedural(
    ID3D11Device* device, ID3D11DeviceContext* context,
    const std::string& cachePath, const std::string& shaderDir)
{
    float* hdrData = nullptr;
    int hdrWidth, hdrHeight;

    if (!GenerateProceduralHDR(&hdrData, &hdrWidth, &hdrHeight)) {
        return false;
    }

    ID3D11ShaderResourceView* equirectSRV = CreateEquirectTexture(device, context, hdrData, hdrWidth, hdrHeight);
    free(hdrData);
    if (!equirectSRV) return false;

    // Convert to cubemap
    ID3D11ShaderResourceView* cubeSRV = nullptr;
    ID3D11Texture2D* cubeTex = nullptr;
    if (!ConvertEquirectToCubemap(device, context, equirectSRV, &cubeSRV, &cubeTex, 256, shaderDir)) {
        equirectSRV->Release();
        return false;
    }
    equirectSRV->Release();

    // Generate irradiance map
    if (!GenerateIrradianceMap(device, context, cubeSRV, shaderDir)) {
        cubeSRV->Release();
        cubeTex->Release();
        return false;
    }

    // Generate prefiltered specular map
    if (!GeneratePrefilterMap(device, context, cubeSRV, shaderDir)) {
        cubeSRV->Release();
        cubeTex->Release();
        return false;
    }

    cubeSRV->Release();
    cubeTex->Release();

    std::cout << "[IBL] Procedural initialization complete.\n";
    return true;
}

// ---------------------------------------------------------------
// Release all resources
// ---------------------------------------------------------------
void IBLPreprocessor::Release() {
    if (m_irradianceSRV) { m_irradianceSRV->Release(); m_irradianceSRV = nullptr; }
    if (m_prefilteredSRV) { m_prefilteredSRV->Release(); m_prefilteredSRV = nullptr; }
}
