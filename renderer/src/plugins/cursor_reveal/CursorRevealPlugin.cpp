#include "../interface/PluginAPI.h"
#include "WICLoader.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

static RendererContext* g_ctx = nullptr;

struct Settings {
    float brushSize = 250.0f;
    float brushHardness = 0.5f;
    float fadeSpeed = 0.02f;
    float trailLength = 1.0f;
} g_settings;

// D3D Resources
static ID3D11VertexShader* g_FullscreenVS = nullptr;
static ID3D11PixelShader*  g_CompositePS = nullptr;
static ID3D11PixelShader*  g_BrushPS = nullptr;
static ID3D11PixelShader*  g_FadePS = nullptr;

static ID3D11Texture2D*          g_MaskTexture = nullptr;
static ID3D11RenderTargetView*   g_MaskRTV = nullptr;
static ID3D11ShaderResourceView* g_MaskSRV = nullptr;

static ID3D11Texture2D*          g_TexA = nullptr;
static ID3D11ShaderResourceView* g_SRVA = nullptr;
static ID3D11Texture2D*          g_TexB = nullptr;
static ID3D11ShaderResourceView* g_SRVB = nullptr;

static ID3D11SamplerState*       g_Sampler = nullptr;
static ID3D11BlendState*         g_BlendAdditive = nullptr;
static ID3D11BlendState*         g_BlendSubtractive = nullptr;
static ID3D11BlendState*         g_BlendOpaque = nullptr;

static ID3D11Buffer* g_BrushCB = nullptr;
static ID3D11Buffer* g_FadeCB = nullptr;

__declspec(align(16))
struct BrushConstantBuffer {
    float mouseX;
    float mouseY;
    float brushSize;
    float brushHardness;
};

__declspec(align(16))
struct FadeConstantBuffer {
    float fadeAmount;
    float padding[3];
};

static BrushConstantBuffer g_brushData = { -1000.0f, -1000.0f, 250.0f, 0.5f };

// Helper to read compiled shader
std::vector<char> ReadFileContent(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) return {};
    size_t size = (size_t)file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    file.read(buffer.data(), size);
    return buffer;
}

void CR_Initialize(RendererContext* ctx) {
    g_ctx = ctx;
    std::cout << "[CursorReveal] Initializing...\n";

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir = exePath;
    exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));
    std::string pluginDir = exeDir + "\\plugins\\";

    // Load Shaders
    auto vsData = ReadFileContent(pluginDir + "FullscreenVS.cso");
    auto compData = ReadFileContent(pluginDir + "CompositePS.cso");
    auto brushData = ReadFileContent(pluginDir + "BrushPS.cso");
    auto fadeData = ReadFileContent(pluginDir + "FadePS.cso");

    if (vsData.empty() || compData.empty() || brushData.empty() || fadeData.empty()) {
        std::cout << "[CursorReveal] Failed to load shaders.\n";
        return;
    }

    g_ctx->device->CreateVertexShader(vsData.data(), vsData.size(), nullptr, &g_FullscreenVS);
    g_ctx->device->CreatePixelShader(compData.data(), compData.size(), nullptr, &g_CompositePS);
    g_ctx->device->CreatePixelShader(brushData.data(), brushData.size(), nullptr, &g_BrushPS);
    g_ctx->device->CreatePixelShader(fadeData.data(), fadeData.size(), nullptr, &g_FadePS);

    // Create Constant Buffers
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = sizeof(BrushConstantBuffer);
    g_ctx->device->CreateBuffer(&cbDesc, nullptr, &g_BrushCB);

    cbDesc.ByteWidth = sizeof(FadeConstantBuffer);
    g_ctx->device->CreateBuffer(&cbDesc, nullptr, &g_FadeCB);

    // Create Sampler
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    g_ctx->device->CreateSamplerState(&sampDesc, &g_Sampler);

    // Create Blend States
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_ctx->device->CreateBlendState(&blendDesc, &g_BlendAdditive);

    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
    g_ctx->device->CreateBlendState(&blendDesc, &g_BlendSubtractive);

    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    g_ctx->device->CreateBlendState(&blendDesc, &g_BlendOpaque);

    // Create Reveal Mask Texture (R8G8B8A8 for simplicity, though R8 would suffice)
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = g_ctx->processingWidth;
    texDesc.Height = g_ctx->processingHeight;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    
    g_ctx->device->CreateTexture2D(&texDesc, nullptr, &g_MaskTexture);
    if (g_MaskTexture) {
        g_ctx->device->CreateRenderTargetView(g_MaskTexture, nullptr, &g_MaskRTV);
        g_ctx->device->CreateShaderResourceView(g_MaskTexture, nullptr, &g_MaskSRV);
        
        // Clear mask initially
        float clearColor[4] = {0,0,0,0};
        g_ctx->context->ClearRenderTargetView(g_MaskRTV, clearColor);
    }
}

void CR_Update(float deltaTime) { }

void CR_Render() {
    if (!g_ctx || !g_ctx->context || !g_FullscreenVS) return;

    // 1. Setup Viewport to processing resolution
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)g_ctx->processingWidth;
    vp.Height = (float)g_ctx->processingHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_ctx->context->RSSetViewports(1, &vp);

    // Bind common
    g_ctx->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->context->VSSetShader(g_FullscreenVS, nullptr, 0);

    // 2. FADE PASS (Subtractive)
    g_ctx->context->OMSetRenderTargets(1, &g_MaskRTV, nullptr);
    float blendFactor[4] = {0,0,0,0};
    g_ctx->context->OMSetBlendState(g_BlendSubtractive, blendFactor, 0xffffffff);
    g_ctx->context->PSSetShader(g_FadePS, nullptr, 0);
    
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_ctx->context->Map(g_FadeCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        FadeConstantBuffer* fc = (FadeConstantBuffer*)mapped.pData;
        fc->fadeAmount = g_settings.fadeSpeed;
        g_ctx->context->Unmap(g_FadeCB, 0);
    }
    g_ctx->context->PSSetConstantBuffers(0, 1, &g_FadeCB);
    g_ctx->context->Draw(3, 0);

    // 3. BRUSH PASS (Additive)
    g_ctx->context->OMSetBlendState(g_BlendAdditive, blendFactor, 0xffffffff);
    g_ctx->context->PSSetShader(g_BrushPS, nullptr, 0);
    
    if (SUCCEEDED(g_ctx->context->Map(g_BrushCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        BrushConstantBuffer* bc = (BrushConstantBuffer*)mapped.pData;
        *bc = g_brushData;
        g_ctx->context->Unmap(g_BrushCB, 0);
    }
    g_ctx->context->PSSetConstantBuffers(0, 1, &g_BrushCB);
    g_ctx->context->Draw(3, 0);

    // 4. COMPOSITE PASS (Opaque to Screen)
    if (g_ctx->mainRenderTargetView) {
        g_ctx->context->OMSetRenderTargets(1, &g_ctx->mainRenderTargetView, nullptr);
        g_ctx->context->OMSetBlendState(g_BlendOpaque, blendFactor, 0xffffffff);
        g_ctx->context->PSSetShader(g_CompositePS, nullptr, 0);

        ID3D11ShaderResourceView* srvs[3] = { g_SRVA, g_SRVB, g_MaskSRV };
        g_ctx->context->PSSetShaderResources(0, 3, srvs);
        g_ctx->context->PSSetSamplers(0, 1, &g_Sampler);

        g_ctx->context->Draw(3, 0);

        // Unbind SRVs to prevent DXGI warnings
        ID3D11ShaderResourceView* nullSRVs[3] = {nullptr, nullptr, nullptr};
        g_ctx->context->PSSetShaderResources(0, 3, nullSRVs);
    }
}

void CR_Shutdown() {
    if (g_FullscreenVS) g_FullscreenVS->Release();
    if (g_CompositePS) g_CompositePS->Release();
    if (g_BrushPS) g_BrushPS->Release();
    if (g_FadePS) g_FadePS->Release();
    if (g_MaskTexture) g_MaskTexture->Release();
    if (g_MaskRTV) g_MaskRTV->Release();
    if (g_MaskSRV) g_MaskSRV->Release();
    if (g_TexA) g_TexA->Release();
    if (g_SRVA) g_SRVA->Release();
    if (g_TexB) g_TexB->Release();
    if (g_SRVB) g_SRVB->Release();
    if (g_Sampler) g_Sampler->Release();
    if (g_BlendAdditive) g_BlendAdditive->Release();
    if (g_BlendSubtractive) g_BlendSubtractive->Release();
    if (g_BlendOpaque) g_BlendOpaque->Release();
    if (g_BrushCB) g_BrushCB->Release();
    if (g_FadeCB) g_FadeCB->Release();
    std::cout << "[CursorReveal] Shutdown complete.\n";
}

void CR_OnMouseMove(int x, int y) {
    if (!g_ctx) return;
    float scaleX = (float)g_ctx->processingWidth / GetSystemMetrics(SM_CXSCREEN);
    float scaleY = (float)g_ctx->processingHeight / GetSystemMetrics(SM_CYSCREEN);
    g_brushData.mouseX = x * scaleX;
    g_brushData.mouseY = y * scaleY;
    g_brushData.brushSize = g_settings.brushSize;
    g_brushData.brushHardness = g_settings.brushHardness;
}

void CR_OnWallpaperChanged(const WallpaperLayers* layers) {
    if (!g_ctx || !layers) return;
    
    if (g_TexA) { g_TexA->Release(); g_TexA = nullptr; }
    if (g_SRVA) { g_SRVA->Release(); g_SRVA = nullptr; }
    if (g_TexB) { g_TexB->Release(); g_TexB = nullptr; }
    if (g_SRVB) { g_SRVB->Release(); g_SRVB = nullptr; }

    std::cout << "[CursorReveal] Loading wallpapers...\n";
    if (layers->imagePathA) {
        WICLoader::LoadTexture(g_ctx->device, layers->imagePathA, g_ctx->processingWidth, g_ctx->processingHeight, &g_TexA, &g_SRVA);
    }
    if (layers->imagePathB) {
        WICLoader::LoadTexture(g_ctx->device, layers->imagePathB, g_ctx->processingWidth, g_ctx->processingHeight, &g_TexB, &g_SRVB);
    }
}

void CR_OnMonitorChanged(const MonitorInfo* info) {}
void CR_OnQualityTierChanged(const QualityTier* tier) {}
void CR_LoadSettings(const char* jsonPath) {}
void CR_SaveSettings(const char* jsonPath) {}

static IEffectPlugin g_plugin = {
    CR_Initialize,
    CR_Update,
    CR_Render,
    CR_Shutdown,
    CR_OnMouseMove,
    CR_OnWallpaperChanged,
    CR_OnMonitorChanged,
    CR_OnQualityTierChanged,
    CR_LoadSettings,
    CR_SaveSettings
};

extern "C" __declspec(dllexport) IEffectPlugin* CreateEffectPlugin() {
    return &g_plugin;
}
