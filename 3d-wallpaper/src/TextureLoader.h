#pragma once
#include <d3d11.h>
#include <cstdint>
#include <string>

// Texture loading utilities using Windows Imaging Component (WIC)
namespace TextureLoader {

    // Load an image from memory (used by GLBLoader for embedded textures)
    // isSRGB: true for color textures (baseColor, emissive), false for data textures (normal, metallic-roughness)
    ID3D11ShaderResourceView* CreateTextureFromMemory(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const uint8_t* data,
        size_t dataSize,
        bool isSRGB = true,
        UINT* outWidth = nullptr,
        UINT* outHeight = nullptr);

    // Load an image from file (used for background images)
    ID3D11ShaderResourceView* CreateTextureFromFile(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const std::string& filepath,
        bool isSRGB = true,
        UINT* outWidth = nullptr,
        UINT* outHeight = nullptr);
}
