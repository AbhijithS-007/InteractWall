#pragma once
#include <d3d11.h>
#include <vector>
#include <string>
#include <DirectXMath.h>

using namespace DirectX;

// A single vertex as stored in our GPU buffers
struct MeshVertex {
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT2 texcoord;
    XMFLOAT4 tangent;
    XMFLOAT2 texcoord1;
};

// A sub-mesh (one draw call per primitive in the glTF)
struct SubMesh {
    ID3D11Buffer* vertexBuffer = nullptr;
    ID3D11Buffer* indexBuffer  = nullptr;
    UINT vertexCount = 0;
    UINT indexCount  = 0;
    DXGI_FORMAT indexFormat = DXGI_FORMAT_R16_UINT;

    // Material
    XMFLOAT4 baseColor = { 0.8f, 0.8f, 0.8f, 1.0f };
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    XMFLOAT3 emissiveFactor = { 0.0f, 0.0f, 0.0f };
    
    ID3D11ShaderResourceView* diffuseTextureSRV = nullptr;
    ID3D11ShaderResourceView* metallicRoughnessTextureSRV = nullptr;
    ID3D11ShaderResourceView* normalTextureSRV = nullptr;
    ID3D11ShaderResourceView* emissiveTextureSRV = nullptr;
    
    bool hasTexture = false;
    bool hasNormalMap = false;
    bool hasMetallicRoughnessMap = false;
    bool hasEmissiveMap = false;
    
    int alphaMode = 0; // 0=OPAQUE, 1=MASK, 2=BLEND
    float alphaCutoff = 0.5f;
    int emissiveTexCoord = 0;
    
    float clearcoatFactor = 0.0f;
    float clearcoatRoughness = 0.0f;
};

// A loaded 3D model consisting of one or more sub-meshes
struct LoadedModel {
    std::vector<SubMesh> submeshes;
    XMFLOAT3 boundsMin = { 0, 0, 0 };
    XMFLOAT3 boundsMax = { 0, 0, 0 };

    void Release() {
        for (auto& sm : submeshes) {
            if (sm.vertexBuffer) { sm.vertexBuffer->Release(); sm.vertexBuffer = nullptr; }
            if (sm.indexBuffer)  { sm.indexBuffer->Release();  sm.indexBuffer = nullptr; }
            if (sm.diffuseTextureSRV) { sm.diffuseTextureSRV->Release(); sm.diffuseTextureSRV = nullptr; }
            if (sm.metallicRoughnessTextureSRV) { sm.metallicRoughnessTextureSRV->Release(); sm.metallicRoughnessTextureSRV = nullptr; }
            if (sm.normalTextureSRV) { sm.normalTextureSRV->Release(); sm.normalTextureSRV = nullptr; }
            if (sm.emissiveTextureSRV) { sm.emissiveTextureSRV->Release(); sm.emissiveTextureSRV = nullptr; }
        }
        submeshes.clear();
    }
};

// Load a .glb file and create GPU buffers
bool LoadGLB(ID3D11Device* device, ID3D11DeviceContext* context, const std::string& filepath, LoadedModel& outModel);
