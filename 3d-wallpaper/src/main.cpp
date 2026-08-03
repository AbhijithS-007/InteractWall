#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <wincodec.h>
#include <cmath>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shell32.lib")

#include "GLBLoader.h"
#include "SceneLoader.h"
#include "TextureLoader.h"

using namespace DirectX;

// ---------------------------------------------------------------
// Globals
// ---------------------------------------------------------------
static HWND g_hwnd = nullptr;
static HWND g_shellDefView = nullptr;
static ID3D11Device*            g_device        = nullptr;
static ID3D11DeviceContext*     g_context       = nullptr;
static ID3D11DeviceContext*     g_deferredContext = nullptr;
static IDXGISwapChain1*         g_swapChain     = nullptr;
static ID3D11RenderTargetView*  g_rtv           = nullptr;
static ID3D11Texture2D*         g_msaaTex       = nullptr;
static ID3D11RenderTargetView*  g_msaaRtv       = nullptr;
static ID3D11DepthStencilView*  g_dsv           = nullptr;

const int QUERY_BUFFER_COUNT = 3;
ID3D11Query* g_queryDisjoint[QUERY_BUFFER_COUNT] = {nullptr};
ID3D11Query* g_queryStart[QUERY_BUFFER_COUNT] = {nullptr};
ID3D11Query* g_queryEnd[QUERY_BUFFER_COUNT] = {nullptr};
int g_queryFrame = 0;

static ID3D11VertexShader*      g_vertexShader  = nullptr;
static ID3D11PixelShader*       g_pixelShader   = nullptr;
static ID3D11InputLayout*       g_inputLayout   = nullptr;
static ID3D11Buffer*            g_cbTransform   = nullptr;
static ID3D11Buffer*            g_cbMaterial    = nullptr;
static ID3D11Buffer*            g_cbLight       = nullptr;
static ID3D11SamplerState*      g_sampler       = nullptr;
static ID3D11BlendState*        g_blendOpaque   = nullptr;
static ID3D11BlendState*        g_blendAlpha    = nullptr;
static ID3D11DepthStencilState* g_dssOpaque     = nullptr;
static ID3D11DepthStencilState* g_dssTransparent= nullptr;

static int  g_width  = 1280;
static int  g_height = 720;
static bool g_wallpaperMode = false;

static ID3D11VertexShader*       g_quadVS        = nullptr;
static ID3D11PixelShader*        g_quadPS        = nullptr;
static ID3D11Buffer*             g_cbQuad        = nullptr;
static ID3D11ShaderResourceView* g_bgTextureSRV  = nullptr;
static UINT                      g_bgWidth       = 0;
static UINT                      g_bgHeight      = 0;

// Scene data
static SceneData g_scene;
static bool g_sceneLoaded = false;

// Per-object runtime data (model + its scene transform)
struct SceneObjectRuntime {
    LoadedModel model;
    bool        loaded = false;
    SceneObject config;       // copy of the parsed config
};
static std::vector<SceneObjectRuntime> g_objects;

// Background Loading state
static std::atomic<bool> g_isReloading{false};
static ID3D11CommandList* g_pendingCommandList = nullptr;
static std::vector<SceneObjectRuntime> g_pendingObjects;
static SceneData g_pendingScene;
static ID3D11ShaderResourceView* g_pendingBgSRV = nullptr;
static float g_pendingBgColor[4];
static UINT g_pendingBgW = 0, g_pendingBgH = 0;
static std::mutex g_reloadMutex;
static std::atomic<bool> g_pendingReady{false};

// Mouse state
static float g_mouseX = 0.0f;
static float g_mouseY = 0.0f;

// Background color (parsed from scene JSON)
static float g_bgColor[4] = { 0.063f, 0.078f, 0.094f, 1.0f }; // #101418

// ---------------------------------------------------------------
// Constant buffer structs (must match HLSL)
// ---------------------------------------------------------------
struct CBTransform {
    XMFLOAT4X4 worldViewProj;
    XMFLOAT4X4 world;
    XMFLOAT4X4 worldInverseTranspose;
};

struct CBLightDir {
    XMFLOAT4 color;     // rgb = color, a = intensity
    XMFLOAT4 direction; // xyz = dir
};

struct CBLight {
    XMFLOAT4 ambientColor;
    CBLightDir lights[4];
    int numLights;
    int lightingEnabled;
    float exposure;
    float paddingLight;
};

struct CBQuad {
    float screenWidth;
    float screenHeight;
    float imageWidth;
    float imageHeight;
    int fitMode;
    float padding[3];
};

struct CBMaterial {
    XMFLOAT4 baseColor;
    float    metallicFactor;
    float    roughnessFactor;
    int      hasTexture;
    int      hasMetallicRoughnessMap;
    int      hasNormalMap;
    int      hasEmissiveMap;
    int      alphaMode;
    float    alphaCutoff;
    XMFLOAT3 emissiveFactor;
    int      emissiveTexCoord;
    float    clearcoatFactor;
    float    clearcoatRoughness;
    XMFLOAT2 paddingMat2;
};

// ---------------------------------------------------------------
// Console (debug builds)
// ---------------------------------------------------------------
static void InitConsole() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string logPath(exePath);
    logPath = logPath.substr(0, logPath.find_last_of("\\/")) + "\\Scene3D_log.txt";

    FILE* logFile;
    freopen_s(&logFile, logPath.c_str(), "w", stdout);
    freopen_s(&logFile, logPath.c_str(), "a", stderr);

#ifdef _DEBUG
    AllocConsole();
    FILE* dummy;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);
#endif
    std::cout << "[Scene3D] Log started.\n";
    std::cout.flush();
}

// ---------------------------------------------------------------
// Load compiled shader from .cso file
// ---------------------------------------------------------------
static std::vector<uint8_t> LoadShaderFile(const std::string& filename) {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string dir(exePath);
    dir = dir.substr(0, dir.find_last_of("\\/")) + "\\" + filename;

    std::ifstream file(dir, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cout << "[Scene3D] Failed to open shader: " << dir << "\n";
        return {};
    }
    size_t size = (size_t)file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(size);
    file.read((char*)data.data(), size);
    return data;
}

// ---------------------------------------------------------------
// D3D Initialization
// ---------------------------------------------------------------
static bool InitD3D(HWND hwnd) {
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    D3D_FEATURE_LEVEL featureLevel;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        featureLevels, 2, D3D11_SDK_VERSION,
        &g_device, &featureLevel, &g_context);

    if (FAILED(hr)) {
        std::cout << "[Scene3D] D3D11CreateDevice failed: 0x" << std::hex << hr << "\n";
        return false;
    }

    hr = g_device->CreateDeferredContext(0, &g_deferredContext);
    if (FAILED(hr)) {
        std::cout << "[Scene3D] CreateDeferredContext failed: 0x" << std::hex << hr << "\n";
        return false;
    }

    IDXGIDevice1* dxgiDevice = nullptr;
    g_device->QueryInterface(__uuidof(IDXGIDevice1), (void**)&dxgiDevice);
    IDXGIAdapter* adapter = nullptr;
    dxgiDevice->GetAdapter(&adapter);
    IDXGIFactory2* factory = nullptr;
    adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory);

    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.Width = g_width;
    scDesc.Height = g_height;
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = 2;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    hr = factory->CreateSwapChainForHwnd(g_device, hwnd, &scDesc, nullptr, nullptr, &g_swapChain);
    factory->Release(); adapter->Release(); dxgiDevice->Release();

    if (FAILED(hr)) {
        std::cout << "[Scene3D] CreateSwapChain failed: 0x" << std::hex << hr << "\n";
        return false;
    }

    // Create RTV
    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    g_device->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
    backBuffer->Release();

    // Create MSAA RTV
    D3D11_TEXTURE2D_DESC msaaDesc = {};
    msaaDesc.Width = g_width;
    msaaDesc.Height = g_height;
    msaaDesc.MipLevels = 1;
    msaaDesc.ArraySize = 1;
    msaaDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    msaaDesc.SampleDesc.Count = 4;
    msaaDesc.SampleDesc.Quality = 0;
    msaaDesc.Usage = D3D11_USAGE_DEFAULT;
    msaaDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
    
    g_device->CreateTexture2D(&msaaDesc, nullptr, &g_msaaTex);
    g_device->CreateRenderTargetView(g_msaaTex, nullptr, &g_msaaRtv);

    // Create depth buffer (MSAA)
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = g_width;
    depthDesc.Height = g_height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 4;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* depthTex = nullptr;
    g_device->CreateTexture2D(&depthDesc, nullptr, &depthTex);
    g_device->CreateDepthStencilView(depthTex, nullptr, &g_dsv);
    depthTex->Release();

    D3D11_QUERY_DESC qdDisjoint = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
    D3D11_QUERY_DESC qdTimestamp = { D3D11_QUERY_TIMESTAMP, 0 };
    for(int i=0; i<QUERY_BUFFER_COUNT; ++i) {
        g_device->CreateQuery(&qdDisjoint, &g_queryDisjoint[i]);
        g_device->CreateQuery(&qdTimestamp, &g_queryStart[i]);
        g_device->CreateQuery(&qdTimestamp, &g_queryEnd[i]);
    }

    return true;
}

// ---------------------------------------------------------------
// Create shaders and pipeline state
// ---------------------------------------------------------------
static bool InitPipeline() {
    auto vsData = LoadShaderFile("ModelVS.cso");
    if (vsData.empty()) return false;

    HRESULT hr = g_device->CreateVertexShader(vsData.data(), vsData.size(), nullptr, &g_vertexShader);
    if (FAILED(hr)) return false;

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(MeshVertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(MeshVertex, normal),   D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(MeshVertex, texcoord), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(MeshVertex, tangent), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(MeshVertex, texcoord1), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = g_device->CreateInputLayout(layout, 5, vsData.data(), vsData.size(), &g_inputLayout);
    if (FAILED(hr)) return false;

    auto psData = LoadShaderFile("ModelPS.cso");
    if (psData.empty()) return false;

    hr = g_device->CreatePixelShader(psData.data(), psData.size(), nullptr, &g_pixelShader);
    if (FAILED(hr)) return false;

    auto qvsData = LoadShaderFile("QuadVS.cso");
    if (!qvsData.empty()) {
        g_device->CreateVertexShader(qvsData.data(), qvsData.size(), nullptr, &g_quadVS);
    }
    auto qpsData = LoadShaderFile("QuadPS.cso");
    if (!qpsData.empty()) {
        g_device->CreatePixelShader(qpsData.data(), qpsData.size(), nullptr, &g_quadPS);
    }

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(CBTransform);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    hr = g_device->CreateBuffer(&cbDesc, nullptr, &g_cbTransform);
    if (FAILED(hr)) return false;

    cbDesc.ByteWidth = sizeof(CBQuad);
    g_device->CreateBuffer(&cbDesc, nullptr, &g_cbQuad);

    cbDesc.ByteWidth = sizeof(CBMaterial);
    hr = g_device->CreateBuffer(&cbDesc, nullptr, &g_cbMaterial);
    if (FAILED(hr)) return false;

    cbDesc.ByteWidth = sizeof(CBLight);
    hr = g_device->CreateBuffer(&cbDesc, nullptr, &g_cbLight);
    if (FAILED(hr)) return false;

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MaxAnisotropy = 16;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = g_device->CreateSamplerState(&sampDesc, &g_sampler);
    if (FAILED(hr)) return false;

    // Create Rasterizer State for Right-Handed (CCW front faces)
    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_BACK;
    rastDesc.FrontCounterClockwise = TRUE;
    rastDesc.DepthClipEnable = TRUE;
    ID3D11RasterizerState* rs = nullptr;
    hr = g_device->CreateRasterizerState(&rastDesc, &rs);
    if (SUCCEEDED(hr)) {
        g_context->RSSetState(rs);
        rs->Release();
    }

    // Create Blend States
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = g_device->CreateBlendState(&blendDesc, &g_blendAlpha);
    if (FAILED(hr)) return false;
    
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    hr = g_device->CreateBlendState(&blendDesc, &g_blendOpaque);
    if (FAILED(hr)) return false;

    // Create Depth Stencil States
    D3D11_DEPTH_STENCIL_DESC dssDesc = {};
    dssDesc.DepthEnable = TRUE;
    dssDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dssDesc.DepthFunc = D3D11_COMPARISON_LESS;
    hr = g_device->CreateDepthStencilState(&dssDesc, &g_dssOpaque);
    if (FAILED(hr)) return false;
    
    dssDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    hr = g_device->CreateDepthStencilState(&dssDesc, &g_dssTransparent);
    if (FAILED(hr)) return false;

    return true;
}

// ---------------------------------------------------------------
// Render one LoadedModel with a given world matrix
// ---------------------------------------------------------------
static void RenderModel(const LoadedModel& model, const XMMATRIX& world, const XMMATRIX& wvp) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = g_context->Map(g_cbTransform, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        CBTransform* cbT = (CBTransform*)mapped.pData;
        XMStoreFloat4x4(&cbT->worldViewProj, XMMatrixTranspose(wvp));
        XMStoreFloat4x4(&cbT->world, XMMatrixTranspose(world));
        
        // Compute World Inverse Transpose for correct normal transformation.
        // We store M^-1 here; HLSL reads it as column-major which implicitly
        // transposes it, giving us (M^-1)^T — exactly what normals need.
        XMVECTOR det;
        XMMATRIX invWorld = XMMatrixInverse(&det, world);
        XMStoreFloat4x4(&cbT->worldInverseTranspose, invWorld);

        g_context->Unmap(g_cbTransform, 0);
    }

    UINT stride = sizeof(MeshVertex);
    UINT offset = 0;

    for (size_t i = 0; i < model.submeshes.size(); ++i) {
        auto& sm = model.submeshes[i];

        hr = g_context->Map(g_cbMaterial, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            CBMaterial* cbM = (CBMaterial*)mapped.pData;
            cbM->baseColor = sm.baseColor;
            cbM->metallicFactor = sm.metallicFactor;
            cbM->roughnessFactor = sm.roughnessFactor;
            cbM->hasTexture = sm.hasTexture ? 1 : 0;
            cbM->hasMetallicRoughnessMap = sm.hasMetallicRoughnessMap ? 1 : 0;
            cbM->hasNormalMap = sm.hasNormalMap ? 1 : 0;
            cbM->hasEmissiveMap = sm.hasEmissiveMap ? 1 : 0;
            cbM->alphaMode = sm.alphaMode;
            cbM->alphaCutoff = sm.alphaCutoff;
            cbM->emissiveFactor = sm.emissiveFactor;
            cbM->emissiveTexCoord = sm.emissiveTexCoord;
            cbM->clearcoatFactor = sm.clearcoatFactor;
            cbM->clearcoatRoughness = sm.clearcoatRoughness;
            g_context->Unmap(g_cbMaterial, 0);
        }

        if (sm.alphaMode == 2) {
            g_context->OMSetDepthStencilState(g_dssTransparent, 0);
            g_context->OMSetBlendState(g_blendAlpha, nullptr, 0xFFFFFFFF);
        } else {
            g_context->OMSetDepthStencilState(g_dssOpaque, 0);
            g_context->OMSetBlendState(g_blendOpaque, nullptr, 0xFFFFFFFF);
        }

        g_context->PSSetConstantBuffers(0, 1, &g_cbMaterial);

        ID3D11ShaderResourceView* srvs[4] = { nullptr, nullptr, nullptr, nullptr };
        if (sm.hasTexture) srvs[0] = sm.diffuseTextureSRV;
        if (sm.hasMetallicRoughnessMap) srvs[1] = sm.metallicRoughnessTextureSRV;
        if (sm.hasNormalMap) srvs[2] = sm.normalTextureSRV;
        if (sm.hasEmissiveMap) srvs[3] = sm.emissiveTextureSRV;
        
        g_context->PSSetShaderResources(0, 4, srvs);

        g_context->IASetVertexBuffers(0, 1, &sm.vertexBuffer, &stride, &offset);

        if (sm.indexBuffer) {
            g_context->IASetIndexBuffer(sm.indexBuffer, sm.indexFormat, 0);
            g_context->DrawIndexed(sm.indexCount, 0, 0);
        } else {
            g_context->Draw(sm.vertexCount, 0);
        }
    }
}

// ---------------------------------------------------------------
// Compute the world matrix for a single scene object
// ---------------------------------------------------------------
static XMMATRIX ComputeObjectWorld(const SceneObjectRuntime& obj) {
    const auto& cfg = obj.config;
    const auto& mdl = obj.model;

    // 1. Center the model at its own origin
    XMFLOAT3 center = {
        (mdl.boundsMin.x + mdl.boundsMax.x) * 0.5f,
        (mdl.boundsMin.y + mdl.boundsMax.y) * 0.5f,
        (mdl.boundsMin.z + mdl.boundsMax.z) * 0.5f,
    };
    float extentX = mdl.boundsMax.x - mdl.boundsMin.x;
    float extentY = mdl.boundsMax.y - mdl.boundsMin.y;
    float extentZ = mdl.boundsMax.z - mdl.boundsMin.z;
    float maxExtent = std::max({ extentX, extentY, extentZ });
    float normalizeScale = (maxExtent > 0.0f) ? (2.0f / maxExtent) : 1.0f;

    XMMATRIX centerAndNormalize =
        XMMatrixTranslation(-center.x, -center.y, -center.z)
        * XMMatrixScaling(normalizeScale, normalizeScale, normalizeScale);

    // 2. Apply user-defined scale
    XMMATRIX userScale = XMMatrixScaling(cfg.scale[0], cfg.scale[1], cfg.scale[2]);

    // 3. Apply base rotation (Euler XYZ intrinsic, matching Three.js)
    //    Three.js 'XYZ' intrinsic = extrinsic Z→Y→X
    //    In row-major DirectXMath: v * Rz * Ry * Rx achieves the same extrinsic order
    float baseRX = XMConvertToRadians(cfg.rotation[0]);
    float baseRY = XMConvertToRadians(cfg.rotation[1]);
    float baseRZ = XMConvertToRadians(cfg.rotation[2]);
    XMMATRIX baseRotation =
        XMMatrixRotationZ(baseRZ)
        * XMMatrixRotationY(baseRY)
        * XMMatrixRotationX(baseRX);

    // 4. Apply cursor-driven rotation (additive on top of base)
    XMMATRIX cursorRotation = XMMatrixIdentity();
    if (cfg.followMouse) {
        float normalizedX = (g_mouseX / (float)g_width) * 2.0f - 1.0f;
        float normalizedY = (g_mouseY / (float)g_height) * 2.0f - 1.0f;

        float effectiveSensitivity = g_scene.camera.sensitivity * cfg.rotationMultiplier;
        float maxOffset = g_scene.camera.maxRotationOffset;

        // Compute raw offset, then clamp to maxRotationOffset
        float yawDeg   = normalizedX * effectiveSensitivity * 60.0f;
        float pitchDeg = normalizedY * effectiveSensitivity * 40.0f;
        yawDeg   = std::clamp(yawDeg,   -maxOffset, maxOffset);
        pitchDeg = std::clamp(pitchDeg, -maxOffset, maxOffset);

        cursorRotation =
            XMMatrixRotationX(XMConvertToRadians(pitchDeg))
            * XMMatrixRotationY(XMConvertToRadians(yawDeg));
    }

    // 5. Apply user-defined position (pure RH now)
    XMMATRIX userTranslation = XMMatrixTranslation(
        cfg.position[0], cfg.position[1], cfg.position[2]);

    // Chain: center → normalize → userScale → baseRot → cursorRot → translate
    return centerAndNormalize * userScale * baseRotation * cursorRotation * userTranslation;
}

// ---------------------------------------------------------------
// Fullscreen detection — pause rendering when desktop is occluded
// ---------------------------------------------------------------
static bool IsFullscreenAppRunning() {
    // Method: check if the foreground window covers the entire primary monitor
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    
    // Ignore our own window and the desktop/shell windows
    if (fg == g_hwnd) return false;
    
    char className[256];
    GetClassNameA(fg, className, sizeof(className));
    
    // Ignore desktop shell classes
    if (strcmp(className, "Progman") == 0 ||
        strcmp(className, "WorkerW") == 0 ||
        strcmp(className, "Shell_TrayWnd") == 0) {
        return false;
    }
    
    RECT rc;
    if (!GetWindowRect(fg, &rc)) return false;
    
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    
    // Check if the foreground window covers the entire screen
    return (rc.left <= 0 && rc.top <= 0 && 
            rc.right >= screenW && rc.bottom >= screenH);
}

// ---------------------------------------------------------------
// Main render loop
// ---------------------------------------------------------------
static void Render() {
    static int frameCount = 0;

    if (!g_context || !g_rtv || !g_dsv) return;

    LARGE_INTEGER tStart, tEnd, tFreq;
    QueryPerformanceFrequency(&tFreq);
    QueryPerformanceCounter(&tStart);

    g_context->OMSetRenderTargets(1, &g_msaaRtv, g_dsv);

    // Use background color if no image
    g_context->ClearRenderTargetView(g_msaaRtv, g_bgColor);
    g_context->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    // Set viewport
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)g_width;
    vp.Height = (float)g_height;
    vp.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &vp);

    // View and projection (Right-Handed)
    XMMATRIX view = XMMatrixLookAtRH(
        XMVectorSet(0, 0, 4.0f, 1),
        XMVectorSet(0, 0, 0, 1),
        XMVectorSet(0, 1, 0, 0)
    );
    float aspect = (float)g_width / (float)g_height;
    XMMATRIX proj = XMMatrixPerspectiveFovRH(
        XMConvertToRadians(g_scene.camera.fov), aspect, 0.01f, 100.0f);

    // Render Background Quad if we have a texture
    if (g_bgTextureSRV && g_quadVS && g_quadPS) {
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetShader(g_quadVS, nullptr, 0);
        g_context->PSSetShader(g_quadPS, nullptr, 0);

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(g_context->Map(g_cbQuad, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            CBQuad* cbQ = (CBQuad*)mapped.pData;
            cbQ->screenWidth = (float)g_width;
            cbQ->screenHeight = (float)g_height;
            cbQ->imageWidth = (float)g_bgWidth;
            cbQ->imageHeight = (float)g_bgHeight;
            
            if (g_scene.background.fit == "contain") cbQ->fitMode = 1;
            else if (g_scene.background.fit == "stretch") cbQ->fitMode = 2;
            else if (g_scene.background.fit == "tile") cbQ->fitMode = 3;
            else cbQ->fitMode = 0; // cover

            g_context->Unmap(g_cbQuad, 0);
        }
        g_context->PSSetConstantBuffers(0, 1, &g_cbQuad);
        g_context->PSSetShaderResources(0, 1, &g_bgTextureSRV);
        g_context->PSSetSamplers(0, 1, &g_sampler);
        g_context->Draw(3, 0);
        
        // Clear depth after drawing background quad so 3D models draw on top
        g_context->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }

    // Set shared pipeline state for 3D models
    g_context->IASetInputLayout(g_inputLayout);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetShader(g_vertexShader, nullptr, 0);
    g_context->PSSetShader(g_pixelShader, nullptr, 0);
    g_context->VSSetConstantBuffers(0, 1, &g_cbTransform);
    
    // Update and bind CBLight
    D3D11_MAPPED_SUBRESOURCE mappedLight;
    if (SUCCEEDED(g_context->Map(g_cbLight, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedLight))) {
        CBLight* cbL = (CBLight*)mappedLight.pData;
        cbL->ambientColor = XMFLOAT4(
            g_scene.lighting.ambientColor[0], 
            g_scene.lighting.ambientColor[1], 
            g_scene.lighting.ambientColor[2], 1.0f);
        
        int numLights = (int)g_scene.lighting.directionalLights.size();
        if (numLights > 4) numLights = 4;
        cbL->numLights = numLights;
        cbL->lightingEnabled = g_scene.rendering.lightingEnabled ? 1 : 0;
        cbL->exposure = g_scene.lighting.exposure;
        
        for (int i = 0; i < numLights; ++i) {
            const auto& sl = g_scene.lighting.directionalLights[i];
            cbL->lights[i].color = XMFLOAT4(sl.color[0], sl.color[1], sl.color[2], sl.intensity);
            
            // Normalize direction just in case
            XMVECTOR dir = XMVectorSet(sl.direction[0], sl.direction[1], sl.direction[2], 0.0f);
            dir = XMVector3Normalize(dir);
            XMStoreFloat4(&cbL->lights[i].direction, dir);
        }
        
        g_context->Unmap(g_cbLight, 0);
    }
    g_context->PSSetConstantBuffers(1, 1, &g_cbLight);

    g_context->PSSetSamplers(0, 1, &g_sampler);
    
    // Blend state is set per-mesh in the draw loop

    // Begin disjoint query
    int currentQueryIdx = g_queryFrame % QUERY_BUFFER_COUNT;
    g_context->Begin(g_queryDisjoint[currentQueryIdx]);
    g_context->End(g_queryStart[currentQueryIdx]);

    // Render each object
    for (auto& obj : g_objects) {
        if (!obj.loaded) continue;

        XMMATRIX world = ComputeObjectWorld(obj);
        XMMATRIX wvp = world * view * proj;
        RenderModel(obj.model, world, wvp);
    }

    g_context->End(g_queryEnd[currentQueryIdx]);
    g_context->End(g_queryDisjoint[currentQueryIdx]);

    if (g_queryFrame >= QUERY_BUFFER_COUNT) {
        int oldestQueryIdx = (g_queryFrame + 1) % QUERY_BUFFER_COUNT;
        
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT tsDisjoint;
        while (g_context->GetData(g_queryDisjoint[oldestQueryIdx], &tsDisjoint, sizeof(tsDisjoint), 0) == S_FALSE) {
            Sleep(0); // Should be rare to wait for 3 frames ago
        }
        
        if (!tsDisjoint.Disjoint) {
            UINT64 tsStart, tsEnd;
            g_context->GetData(g_queryStart[oldestQueryIdx], &tsStart, sizeof(UINT64), 0);
            g_context->GetData(g_queryEnd[oldestQueryIdx], &tsEnd, sizeof(UINT64), 0);
            
            double msGpu = double(tsEnd - tsStart) / double(tsDisjoint.Frequency) * 1000.0;
            
            static double totalMs = 0.0;
            static int countMs = 0;
            totalMs += msGpu;
            countMs++;
            
            if (countMs == 100) {
                std::cout << "[Scene3D] GPU Model Draw Time (100-frame avg): " << (totalMs / 100.0) << " ms\n";
                std::cout.flush();
                totalMs = 0.0;
                countMs = 0;
            }
        }
    }
    
    g_queryFrame++;

    if (frameCount == 0) {
        std::cout << "[Scene3D] First frame rendered with " << g_objects.size() << " object(s).\n";
        std::cout.flush();
    }

    // Resolve MSAA to back buffer
    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    g_context->ResolveSubresource(backBuffer, 0, g_msaaTex, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    backBuffer->Release();

    g_swapChain->Present(1, 0);

    frameCount++;
}

// ---------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_wallpaperMode && g_shellDefView) {
        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK ||
            msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP || msg == WM_RBUTTONDBLCLK ||
            msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP || msg == WM_MBUTTONDBLCLK) {
            
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ClientToScreen(hwnd, &pt);
            ScreenToClient(g_shellDefView, &pt);
            
            PostMessage(g_shellDefView, msg, wParam, MAKELPARAM(pt.x, pt.y));
        }
    }

    switch (msg) {
    case WM_DESTROY:
        std::cout << "[Scene3D] Received WM_DESTROY\n"; std::cout.flush();
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        if (g_device && g_swapChain && wParam != SIZE_MINIMIZED) {
            g_width = LOWORD(lParam);
            g_height = HIWORD(lParam);
            if (g_width > 0 && g_height > 0) {
                g_context->OMSetRenderTargets(0, nullptr, nullptr);
                if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
                if (g_msaaTex) { g_msaaTex->Release(); g_msaaTex = nullptr; }
                if (g_msaaRtv) { g_msaaRtv->Release(); g_msaaRtv = nullptr; }
                if (g_dsv) { g_dsv->Release(); g_dsv = nullptr; }

                g_swapChain->ResizeBuffers(0, g_width, g_height, DXGI_FORMAT_UNKNOWN, 0);

                ID3D11Texture2D* bb = nullptr;
                g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
                g_device->CreateRenderTargetView(bb, nullptr, &g_rtv);
                bb->Release();

                // Recreate MSAA RTV
                D3D11_TEXTURE2D_DESC msaaDesc = {};
                msaaDesc.Width = g_width; msaaDesc.Height = g_height;
                msaaDesc.MipLevels = 1; msaaDesc.ArraySize = 1;
                msaaDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                msaaDesc.SampleDesc.Count = 4;
                msaaDesc.Usage = D3D11_USAGE_DEFAULT;
                msaaDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
                g_device->CreateTexture2D(&msaaDesc, nullptr, &g_msaaTex);
                g_device->CreateRenderTargetView(g_msaaTex, nullptr, &g_msaaRtv);

                D3D11_TEXTURE2D_DESC dd = {};
                dd.Width = g_width; dd.Height = g_height;
                dd.MipLevels = 1; dd.ArraySize = 1;
                dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
                dd.SampleDesc.Count = 4;
                dd.Usage = D3D11_USAGE_DEFAULT;
                dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
                ID3D11Texture2D* dt = nullptr;
                g_device->CreateTexture2D(&dd, nullptr, &dt);
                g_device->CreateDepthStencilView(dt, nullptr, &g_dsv);
                dt->Release();
            }
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    HWND p = FindWindowEx(hwnd, nullptr, "SHELLDLL_DefView", nullptr);
    if (p != nullptr) {
        g_shellDefView = p;
        HWND* ret = (HWND*)lParam;
        *ret = FindWindowEx(nullptr, hwnd, "WorkerW", nullptr);
    }
    return TRUE;
}

// ---------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    InitConsole();

    std::cout << "=== Scene3D Viewer (Phase 2) ===\n";

    // ---- Determine scene file path ----
    std::string scenePath;

    // Check command line first
    for (int i = 1; i < __argc; ++i) {
        std::string arg = __argv[i];
        if (arg == "/wallpaper") {
            g_wallpaperMode = true;
        } else if (scenePath.empty()) {
            scenePath = arg;
        }
    }

    // If a .glb was passed directly, create a minimal in-memory scene for it
    bool directGLB = false;
    std::string directModelPath;
    if (!scenePath.empty()) {
        std::string lower = scenePath;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".glb") {
            directGLB = true;
            directModelPath = scenePath;
            scenePath.clear();
        }
    }

    // Dev fallback: look for scene.json next to exe, then in project root
    if (scenePath.empty() && !directGLB) {
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::string exeDir(exePath);
        exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));

        // Try scene.json next to exe
        std::string candidate = exeDir + "\\scene.json";
        if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
            scenePath = candidate;
        } else {
            // Try project root (../../scene.json relative to build/Release/)
            candidate = exeDir + "\\..\\..\\scene.json";
            if (GetFileAttributesA(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
                scenePath = candidate;
            }
        }
    }

    // ---- Load scene or create a default ----
    if (!scenePath.empty()) {
        std::cout << "[Scene3D] Loading scene: " << scenePath << "\n"; std::cout.flush();
        g_sceneLoaded = LoadScene(scenePath, g_scene);
    }

    if (!g_sceneLoaded && directGLB) {
        // Wrap a single .glb in a default scene
        std::cout << "[Scene3D] Direct GLB mode: " << directModelPath << "\n"; std::cout.flush();
        g_scene.name = "Direct Model";
        g_scene.camera.fov = 45.0f;
        g_scene.camera.sensitivity = 0.3f;
        g_scene.camera.maxRotationOffset = 30.0f;
        SceneObject obj;
        obj.id = "obj_direct";
        obj.modelPath = directModelPath;
        obj.followMouse = true;
        g_scene.objects.push_back(obj);
        g_sceneLoaded = true;
    }

    if (!g_sceneLoaded || g_scene.objects.empty()) {
        // Last resort: try Duck.glb in the models folder
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::string exeDir(exePath);
        exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));
        std::string fallback = exeDir + "\\..\\..\\models\\Duck.glb";
        if (GetFileAttributesA(fallback.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::cout << "[Scene3D] Fallback to Duck.glb\n"; std::cout.flush();
            g_scene.name = "Duck Fallback";
            SceneObject obj;
            obj.id = "obj_fallback";
            obj.modelPath = fallback;
            obj.followMouse = true;
            g_scene.objects.push_back(obj);
            g_sceneLoaded = true;
        }
    }

    if (!g_sceneLoaded || g_scene.objects.empty()) {
        MessageBoxA(nullptr,
            "Usage: Scene3DViewer.exe <scene.json or model.glb>\n\n"
            "Drag and drop a scene.json or .glb file onto the exe.",
            "Scene3D Viewer", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // Parse background color
    if (g_scene.background.type == "color") {
        ParseHexColor(g_scene.background.value, g_bgColor);
    }

    std::cout << "[Scene3D] Scene: \"" << g_scene.name << "\" | "
              << g_scene.objects.size() << " object(s) | bg=" << g_scene.background.value << "\n";
    std::cout.flush();

    // ---- Create window ----
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "Scene3DViewerClass";
    RegisterClassEx(&wc);

    std::string windowTitle = "Scene3D Viewer - " + g_scene.name;
    RECT rc = { 0, 0, g_width, g_height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    
    DWORD style = g_wallpaperMode ? WS_POPUP | WS_VISIBLE : WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    
    g_hwnd = CreateWindowEx(
        0, wc.lpszClassName, windowTitle.c_str(),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, nullptr);

    if (g_wallpaperMode && g_hwnd) {
        // Change style to WS_CHILD before re-parenting
        LONG_PTR currentStyle = GetWindowLongPtr(g_hwnd, GWL_STYLE);
        SetWindowLongPtr(g_hwnd, GWL_STYLE, (currentStyle & ~WS_POPUP) | WS_CHILD);
        
        // WorkerW Hack to draw behind desktop icons
        HWND progman = FindWindow("Progman", nullptr);
        SendMessageTimeout(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);
        HWND workerw = nullptr;
        EnumWindows(EnumWindowsProc, (LPARAM)&workerw);
        if (workerw) {
            SetParent(g_hwnd, workerw);
        } else {
            std::cout << "[Scene3D] Failed to find WorkerW, falling back to Progman.\n";
            SetParent(g_hwnd, progman);
        }

        // Match screen size
        int screenX = GetSystemMetrics(SM_CXSCREEN);
        int screenY = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(g_hwnd, nullptr, 0, 0, screenX, screenY, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    if (!g_hwnd) {
        std::cout << "[Scene3D] CreateWindow failed: " << GetLastError() << "\n";
        return 1;
    }

    // ---- Init D3D ----
    if (!InitD3D(g_hwnd)) {
        MessageBoxA(nullptr, "Failed to initialize Direct3D.", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!InitPipeline()) {
        MessageBoxA(nullptr, "Failed to initialize shaders.", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // ---- Load Background Texture ----
    if (g_scene.background.type == "image" && !g_scene.background.value.empty()) {
        std::cout << "[Scene3D] Loading background image: " << g_scene.background.value << "\n";
        g_bgTextureSRV = TextureLoader::CreateTextureFromFile(g_device, g_context, g_scene.background.value, false, &g_bgWidth, &g_bgHeight);
        if (!g_bgTextureSRV) {
            std::cout << "[Scene3D] Failed to load background image!\n";
        }
    }

    // ---- Load all scene objects ----
    g_objects.resize(g_scene.objects.size());
    for (size_t i = 0; i < g_scene.objects.size(); ++i) {
        g_objects[i].config = g_scene.objects[i];
        std::cout << "[Scene3D] Loading object \"" << g_scene.objects[i].id
                  << "\": " << g_scene.objects[i].modelPath << "\n";
        std::cout.flush();
        g_objects[i].loaded = LoadGLB(g_device, g_context, g_scene.objects[i].modelPath, g_objects[i].model);
        if (!g_objects[i].loaded) {
            std::cout << "[Scene3D] WARNING: Failed to load: " << g_scene.objects[i].modelPath << "\n";
        }
    }

    std::cout << "[Scene3D] All models loaded. Starting render loop.\n"; std::cout.flush();

    // ---- File Watcher State ----
    FILETIME lastSceneFileTime = {};
    if (!scenePath.empty()) {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesEx(scenePath.c_str(), GetFileExInfoStandard, &fad)) {
            lastSceneFileTime = fad.ftLastWriteTime;
        }
    }
    unsigned int frameCounter = 0;

    // ---- Message loop ----
    MSG msg = {};
    bool running = true;
    while (running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        // Hot Reload check every 60 frames
        if (!scenePath.empty() && !g_isReloading && (frameCounter++ % 60 == 0)) {
            WIN32_FILE_ATTRIBUTE_DATA fad;
            if (GetFileAttributesEx(scenePath.c_str(), GetFileExInfoStandard, &fad)) {
                if (CompareFileTime(&lastSceneFileTime, &fad.ftLastWriteTime) < 0) {
                    lastSceneFileTime = fad.ftLastWriteTime;
                    std::cout << "[Scene3D] scene.json changed! Starting background hot-reload...\n"; std::cout.flush();
                    
                    g_isReloading = true;
                    std::thread([scenePath]() {
                        SceneData tempScene;
                        ID3D11ShaderResourceView* tempBgSRV = nullptr;
                        float tempBgColor[4] = {0};
                        UINT tempBgW = 0, tempBgH = 0;
                        std::vector<SceneObjectRuntime> tempObjects;

                        if (LoadScene(scenePath, tempScene)) {
                            if (tempScene.background.type == "color") {
                                ParseHexColor(tempScene.background.value, tempBgColor);
                            } else if (tempScene.background.type == "image" && !tempScene.background.value.empty()) {
                                tempBgSRV = TextureLoader::CreateTextureFromFile(g_device, g_deferredContext, tempScene.background.value, false, &tempBgW, &tempBgH);
                            }
                            
                            tempObjects.resize(tempScene.objects.size());
                            for (size_t i = 0; i < tempScene.objects.size(); ++i) {
                                tempObjects[i].config = tempScene.objects[i];
                                tempObjects[i].loaded = LoadGLB(g_device, g_deferredContext, tempScene.objects[i].modelPath, tempObjects[i].model);
                            }
                        }

                        ID3D11CommandList* cmdList = nullptr;
                        g_deferredContext->FinishCommandList(FALSE, &cmdList);

                        std::lock_guard<std::mutex> lock(g_reloadMutex);
                        g_pendingScene = std::move(tempScene);
                        g_pendingBgSRV = tempBgSRV;
                        for(int i=0; i<4; i++) g_pendingBgColor[i] = tempBgColor[i];
                        g_pendingBgW = tempBgW;
                        g_pendingBgH = tempBgH;
                        g_pendingObjects = std::move(tempObjects);
                        g_pendingCommandList = cmdList;
                        g_pendingReady = true;

                    }).detach();
                }
            }
        }

        // Apply hot-reload if background thread finished
        if (g_pendingReady) {
            std::lock_guard<std::mutex> lock(g_reloadMutex);
            
            // Execute deferred commands (e.g. mipmap generation)
            if (g_pendingCommandList) {
                g_context->ExecuteCommandList(g_pendingCommandList, FALSE);
                g_pendingCommandList->Release();
                g_pendingCommandList = nullptr;
            }

            // Cleanup old resources
            if (g_bgTextureSRV) { g_bgTextureSRV->Release(); g_bgTextureSRV = nullptr; }
            for (auto& obj : g_objects) {
                if (obj.loaded) obj.model.Release();
            }

            // Swap in new resources
            g_scene = std::move(g_pendingScene);
            g_bgTextureSRV = g_pendingBgSRV;
            for(int i=0; i<4; i++) g_bgColor[i] = g_pendingBgColor[i];
            g_bgWidth = g_pendingBgW;
            g_bgHeight = g_pendingBgH;
            g_objects = std::move(g_pendingObjects);
            
            g_pendingBgSRV = nullptr;
            g_pendingObjects.clear();
            
            g_isReloading = false;
            g_pendingReady = false;
            std::cout << "[Scene3D] Background hot-reload applied to main thread.\n"; std::cout.flush();
        }
        POINT pt;
        if (GetCursorPos(&pt)) {
            ScreenToClient(g_hwnd, &pt);
            g_mouseX = (float)pt.x;
            g_mouseY = (float)pt.y;
        }

        // Skip rendering when a fullscreen app covers the desktop
        if (g_wallpaperMode && IsFullscreenAppRunning()) {
            // Sleep up to 500ms, but wake up instantly if a window message arrives.
            // This prevents the message pump from freezing, which breaks OS gestures.
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 500, QS_ALLINPUT);
            continue;
        }

        Render();
    }

    // ---- Cleanup ----
    for (auto& obj : g_objects) {
        if (obj.loaded) obj.model.Release();
    }
    g_objects.clear();

    if (g_bgTextureSRV)  g_bgTextureSRV->Release();
    if (g_cbQuad)        g_cbQuad->Release();
    if (g_quadPS)        g_quadPS->Release();
    if (g_quadVS)        g_quadVS->Release();
    if (g_sampler)       g_sampler->Release();
    if (g_cbMaterial)    g_cbMaterial->Release();
    if (g_cbTransform)   g_cbTransform->Release();
    if (g_inputLayout)   g_inputLayout->Release();
    if (g_pixelShader)   g_pixelShader->Release();
    if (g_vertexShader)  g_vertexShader->Release();
    if (g_dsv)           g_dsv->Release();
    if (g_rtv)           g_rtv->Release();
    if (g_swapChain)     g_swapChain->Release();
    if (g_deferredContext) g_deferredContext->Release();
    if (g_context)       g_context->Release();
    if (g_device)        g_device->Release();

    CoUninitialize();
    return 0;
}
