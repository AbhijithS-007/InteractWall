#pragma once
#include <d3d11.h>
#include <wincodec.h>
#include <string>

class WICLoader {
public:
    static bool LoadTexture(
        ID3D11Device* device,
        const std::string& filepath,
        int maxWidth,
        int maxHeight,
        ID3D11Texture2D** outTexture,
        ID3D11ShaderResourceView** outSRV);
};
