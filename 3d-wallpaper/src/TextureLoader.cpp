#include "TextureLoader.h"
#include <iostream>
#include <wincodec.h>
#include <vector>

namespace TextureLoader {

    static ID3D11ShaderResourceView* CreateTextureFromWIC(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        IWICImagingFactory* wicFactory,
        IWICBitmapDecoder* decoder,
        bool isSRGB,
        UINT* outWidth,
        UINT* outHeight)
    {
        IWICBitmapFrameDecode* frame = nullptr;
        HRESULT hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) return nullptr;

        IWICFormatConverter* converter = nullptr;
        hr = wicFactory->CreateFormatConverter(&converter);
        if (FAILED(hr)) { frame->Release(); return nullptr; }

        hr = converter->Initialize(
            frame, GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0,
            WICBitmapPaletteTypeMedianCut);
        if (FAILED(hr)) {
            std::cout << "[TextureLoader] Failed to get WIC image size. HR=" << std::hex << hr << "\n"; std::cout.flush();
            converter->Release(); frame->Release(); return nullptr;
        }

        UINT w, h;
        converter->GetSize(&w, &h);
        std::cout << "[TextureLoader] Decoded image size: " << w << "x" << h
                  << (isSRGB ? " (sRGB)" : " (Linear)") << "\n"; std::cout.flush();

        if (outWidth) *outWidth = w;
        if (outHeight) *outHeight = h;

        std::vector<uint8_t> pixels(w * h * 4);
        hr = converter->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data());
        if (FAILED(hr)) {
            std::cout << "[TextureLoader] Failed to CopyPixels. HR=" << std::hex << hr << "\n"; std::cout.flush();
            converter->Release(); frame->Release(); return nullptr;
        }
        
        std::cout << "[TextureLoader] Successfully copied pixels to memory.\n"; std::cout.flush();

        // Choose format based on whether this is a color texture (sRGB) or data texture (linear)
        DXGI_FORMAT texFormat = isSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

        // Create a temporary texture with the top-level data (always UNORM for raw byte upload)
        D3D11_TEXTURE2D_DESC srcDesc = {};
        srcDesc.Width = w;
        srcDesc.Height = h;
        srcDesc.MipLevels = 1;
        srcDesc.ArraySize = 1;
        srcDesc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        srcDesc.SampleDesc.Count = 1;
        srcDesc.Usage = D3D11_USAGE_DEFAULT;
        srcDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = pixels.data();
        initData.SysMemPitch = w * 4;
        
        ID3D11Texture2D* srcTex = nullptr;
        hr = device->CreateTexture2D(&srcDesc, &initData, &srcTex);
        if (FAILED(hr)) {
            std::cout << "[TextureLoader] Failed to CreateTexture2D (srcTex). HR=" << std::hex << hr << "\n"; std::cout.flush();
            converter->Release(); frame->Release();
            return nullptr;
        }
        std::cout << "[TextureLoader] Created srcTex successfully.\n"; std::cout.flush();

        // Create the actual texture with mipmap support using the correct format
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = w;
        texDesc.Height = h;
        texDesc.MipLevels = 0; // Generate all mip levels
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        texDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

        ID3D11Texture2D* tex = nullptr;
        hr = device->CreateTexture2D(&texDesc, nullptr, &tex);
        if (FAILED(hr)) {
            std::cout << "[TextureLoader] Failed to CreateTexture2D (tex with MipLevels=0). HR=" << std::hex << hr << "\n"; std::cout.flush();
            srcTex->Release(); converter->Release(); frame->Release();
            return nullptr;
        }
        std::cout << "[TextureLoader] Created tex (mipmap) successfully.\n"; std::cout.flush();

        // Copy top-level texture data (UNORM -> SRGB copy is valid, same typeless group)
        context->CopySubresourceRegion(tex, 0, 0, 0, 0, srcTex, 0, nullptr);
        srcTex->Release();

        // Create SRV with the correct format
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = texFormat;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = -1; // Use all available mips

        ID3D11ShaderResourceView* srv = nullptr;
        hr = device->CreateShaderResourceView(tex, &srvDesc, &srv);
        if (FAILED(hr)) {
            std::cout << "[TextureLoader] Failed to CreateShaderResourceView. HR=" << std::hex << hr << "\n"; std::cout.flush();
        } else {
            std::cout << "[TextureLoader] CreateShaderResourceView succeeded.\n"; std::cout.flush();
            // Generate mipmaps (with SRGB format, this correctly averages in linear space)
            context->GenerateMips(srv);
            std::cout << "[TextureLoader] GenerateMips called successfully.\n"; std::cout.flush();
        }

        tex->Release();
        converter->Release();
        frame->Release();

        return srv;
    }

    ID3D11ShaderResourceView* CreateTextureFromMemory(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const uint8_t* data,
        size_t dataSize,
        bool isSRGB,
        UINT* outWidth,
        UINT* outHeight)
    {
        IWICImagingFactory* wicFactory = nullptr;
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&wicFactory));
        if (FAILED(hr)) return nullptr;

        IWICStream* stream = nullptr;
        hr = wicFactory->CreateStream(&stream);
        if (FAILED(hr)) { wicFactory->Release(); return nullptr; }
        
        stream->InitializeFromMemory(const_cast<uint8_t*>(data), (DWORD)dataSize);

        IWICBitmapDecoder* decoder = nullptr;
        hr = wicFactory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(hr)) {
            stream->Release();
            wicFactory->Release();
            return nullptr;
        }

        ID3D11ShaderResourceView* srv = CreateTextureFromWIC(device, context, wicFactory, decoder, isSRGB, outWidth, outHeight);

        decoder->Release(); stream->Release(); wicFactory->Release();
        return srv;
    }

    ID3D11ShaderResourceView* CreateTextureFromFile(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const std::string& filepath,
        bool isSRGB,
        UINT* outWidth,
        UINT* outHeight)
    {
        // Convert to wide string
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &filepath[0], (int)filepath.size(), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &filepath[0], (int)filepath.size(), &wstrTo[0], size_needed);

        IWICImagingFactory* wicFactory = nullptr;
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&wicFactory));
        if (FAILED(hr)) return nullptr;

        IWICBitmapDecoder* decoder = nullptr;
        hr = wicFactory->CreateDecoderFromFilename(
            wstrTo.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(hr)) {
            wicFactory->Release();
            return nullptr;
        }

        ID3D11ShaderResourceView* srv = CreateTextureFromWIC(device, context, wicFactory, decoder, isSRGB, outWidth, outHeight);

        decoder->Release(); wicFactory->Release();
        return srv;
    }
}
