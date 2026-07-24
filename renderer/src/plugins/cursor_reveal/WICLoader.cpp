#include "WICLoader.h"
#include <algorithm>
#include <iostream>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

bool WICLoader::LoadTexture(
    ID3D11Device* device,
    const std::string& filepath,
    int maxWidth,
    int maxHeight,
    ID3D11Texture2D** outTexture,
    ID3D11ShaderResourceView** outSRV) 
{
    IWICImagingFactory* wicFactory = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory, (void**)&wicFactory);

    if (FAILED(hr)) return false;

    // Convert string to wstring
    std::wstring wpath(filepath.begin(), filepath.end());

    IWICBitmapDecoder* decoder = nullptr;
    hr = wicFactory->CreateDecoderFromFilename(
        wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);

    if (FAILED(hr)) {
        std::cout << "[CursorReveal] Failed to load WIC image: " << filepath << "\n";
        wicFactory->Release();
        return false;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) { decoder->Release(); wicFactory->Release(); return false; }

    UINT origWidth = 0, origHeight = 0;
    frame->GetSize(&origWidth, &origHeight);

    // Downscale if larger, never upscale
    UINT targetWidth = origWidth;
    UINT targetHeight = origHeight;
    
    if (origWidth > (UINT)maxWidth || origHeight > (UINT)maxHeight) {
        float scaleX = (float)maxWidth / (float)origWidth;
        float scaleY = (float)maxHeight / (float)origHeight;
        float scale = (scaleX < scaleY) ? scaleX : scaleY;
        targetWidth = (UINT)(origWidth * scale);
        targetHeight = (UINT)(origHeight * scale);
    }

    IWICBitmapScaler* scaler = nullptr;
    hr = wicFactory->CreateBitmapScaler(&scaler);
    if (FAILED(hr)) { frame->Release(); decoder->Release(); wicFactory->Release(); return false; }

    hr = scaler->Initialize(frame, targetWidth, targetHeight, WICBitmapInterpolationModeFant);
    if (FAILED(hr)) { scaler->Release(); frame->Release(); decoder->Release(); wicFactory->Release(); return false; }

    IWICFormatConverter* converter = nullptr;
    hr = wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr)) { scaler->Release(); frame->Release(); decoder->Release(); wicFactory->Release(); return false; }

    hr = converter->Initialize(
        scaler, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
        nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) { converter->Release(); scaler->Release(); frame->Release(); decoder->Release(); wicFactory->Release(); return false; }

    UINT stride = targetWidth * 4;
    UINT imageSize = stride * targetHeight;
    std::vector<BYTE> pixels(imageSize);

    hr = converter->CopyPixels(nullptr, stride, imageSize, pixels.data());
    if (FAILED(hr)) { converter->Release(); scaler->Release(); frame->Release(); decoder->Release(); wicFactory->Release(); return false; }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = targetWidth;
    desc.Height = targetHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixels.data();
    initData.SysMemPitch = stride;

    hr = device->CreateTexture2D(&desc, &initData, outTexture);
    if (SUCCEEDED(hr) && outSRV) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(*outTexture, &srvDesc, outSRV);
    }

    converter->Release();
    scaler->Release();
    frame->Release();
    decoder->Release();
    wicFactory->Release();

    return SUCCEEDED(hr);
}
