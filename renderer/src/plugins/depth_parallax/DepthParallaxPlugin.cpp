#include "DepthParallaxPlugin.h"
#include "../cursor_reveal/WICLoader.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

static RendererContext* g_ctx = nullptr;

struct Settings {
    float parallaxStrength = 0.05f;
} g_settings;

static ID3D11VertexShader* g_FullscreenVS = nullptr;
static ID3D11PixelShader*  g_DepthParallaxPS = nullptr;

static ID3D11Texture2D*          g_BaseTex = nullptr;
static ID3D11ShaderResourceView* g_BaseSRV = nullptr;
static ID3D11Texture2D*          g_DepthTex = nullptr;
static ID3D11ShaderResourceView* g_DepthSRV = nullptr;

static ID3D11SamplerState*       g_Sampler = nullptr;
static ID3D11BlendState*         g_BlendOpaque = nullptr;
static ID3D11Buffer*             g_ParallaxCB = nullptr;

__declspec(align(16))
struct ParallaxConstantBuffer {
    float cursorUV[2];
    float parallaxStrength;
    float padding;
};

static ParallaxConstantBuffer g_cbData = {};
static float g_targetCursorUV[2] = {0.5f, 0.5f};

std::vector<char> DP_ReadFileContent(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) return {};
    size_t size = (size_t)file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    file.read(buffer.data(), size);
    return buffer;
}

void DP_Initialize(RendererContext* ctx) {
    g_ctx = ctx;
    std::cout << "ParallaxEnabled=true\n";
    std::cout << "[DepthParallax] Initializing...\n";

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir = exePath;
    exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));
    std::string pluginDir = exeDir + "\\plugins\\";

    auto vsData = DP_ReadFileContent(pluginDir + "DP_FullscreenVS.cso");
    auto psData = DP_ReadFileContent(pluginDir + "DP_DepthParallaxPS.cso");

    if (vsData.empty() || psData.empty()) {
        std::cout << "[DepthParallax] Failed to load shaders.\n";
        return;
    }

    g_ctx->device->CreateVertexShader(vsData.data(), vsData.size(), nullptr, &g_FullscreenVS);
    g_ctx->device->CreatePixelShader(psData.data(), psData.size(), nullptr, &g_DepthParallaxPS);

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = sizeof(ParallaxConstantBuffer);
    g_ctx->device->CreateBuffer(&cbDesc, nullptr, &g_ParallaxCB);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    g_ctx->device->CreateSamplerState(&sampDesc, &g_Sampler);

    D3D11_BLEND_DESC bDesc = {};
    bDesc.RenderTarget[0].BlendEnable = FALSE;
    bDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_ctx->device->CreateBlendState(&bDesc, &g_BlendOpaque);

    g_cbData.cursorUV[0] = 0.5f;
    g_cbData.cursorUV[1] = 0.5f;
    g_cbData.parallaxStrength = g_settings.parallaxStrength;
}

void DP_Update(float deltaTime) {
    // Correct Approach: Smooth only the 2D scalar cursor value itself each frame.
    // This is entirely stateless regarding rendered texture output (no accumulation buffers).
    float smoothingFactor = 45.0f; // Increased from 15.0f for more immediate tracking
    float blend = std::clamp(smoothingFactor * deltaTime, 0.0f, 1.0f);
    g_cbData.cursorUV[0] += (g_targetCursorUV[0] - g_cbData.cursorUV[0]) * blend;
    g_cbData.cursorUV[1] += (g_targetCursorUV[1] - g_cbData.cursorUV[1]) * blend;
}

void DP_Render() {
    if (!g_ctx || !g_ctx->context) return;

    if (!g_BaseSRV) {
        float clearColor[4] = {0,0,0,1};
        g_ctx->context->ClearRenderTargetView(g_ctx->mainRenderTargetView, clearColor);
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_ctx->context->Map(g_ParallaxCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        ParallaxConstantBuffer cb = g_cbData;
        if (!g_DepthSRV) {
            cb.parallaxStrength = 0.0f; // Graceful fallback: flat image
        }
        memcpy(mapped.pData, &cb, sizeof(ParallaxConstantBuffer));
        g_ctx->context->Unmap(g_ParallaxCB, 0);
    }

    // Explicitly unbind all SRVs before our pass begins to ensure zero state bleeding
    ID3D11ShaderResourceView* nullSRVs[8] = {nullptr};
    g_ctx->context->PSSetShaderResources(0, 8, nullSRVs);

    // Set viewport to full screen resolution
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)g_ctx->screenWidth;
    vp.Height = (float)g_ctx->screenHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_ctx->context->RSSetViewports(1, &vp);

    g_ctx->context->OMSetRenderTargets(1, &g_ctx->mainRenderTargetView, nullptr);
    g_ctx->context->OMSetBlendState(g_BlendOpaque, nullptr, 0xFFFFFFFF);

    g_ctx->context->VSSetShader(g_FullscreenVS, nullptr, 0);
    g_ctx->context->PSSetShader(g_DepthParallaxPS, nullptr, 0);

    g_ctx->context->PSSetConstantBuffers(0, 1, &g_ParallaxCB);
    ID3D11ShaderResourceView* srvs[2] = { g_BaseSRV, g_DepthSRV };
    g_ctx->context->PSSetShaderResources(0, 2, srvs);
    g_ctx->context->PSSetSamplers(0, 1, &g_Sampler);

    g_ctx->context->IASetInputLayout(nullptr);
    g_ctx->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->context->Draw(3, 0);

    ID3D11ShaderResourceView* clearSRVs[2] = { nullptr, nullptr };
    g_ctx->context->PSSetShaderResources(0, 2, clearSRVs);
}

void DP_Shutdown() {
    if (g_BaseTex) { g_BaseTex->Release(); g_BaseTex = nullptr; }
    if (g_BaseSRV) { g_BaseSRV->Release(); g_BaseSRV = nullptr; }
    if (g_DepthTex) { g_DepthTex->Release(); g_DepthTex = nullptr; }
    if (g_DepthSRV) { g_DepthSRV->Release(); g_DepthSRV = nullptr; }

    if (g_FullscreenVS) { g_FullscreenVS->Release(); g_FullscreenVS = nullptr; }
    if (g_DepthParallaxPS) { g_DepthParallaxPS->Release(); g_DepthParallaxPS = nullptr; }
    if (g_ParallaxCB) { g_ParallaxCB->Release(); g_ParallaxCB = nullptr; }
    if (g_Sampler) { g_Sampler->Release(); g_Sampler = nullptr; }
    if (g_BlendOpaque) { g_BlendOpaque->Release(); g_BlendOpaque = nullptr; }

    // Explicit safety net: Ensure these slots are completely cleared from the GPU
    if (g_ctx && g_ctx->context) {
        ID3D11ShaderResourceView* nullSRVs[8] = {nullptr};
        g_ctx->context->PSSetShaderResources(0, 8, nullSRVs);
    }
    
    std::cout << "ParallaxEnabled=false\n";
    std::cout << "[DepthParallax] Shutdown complete.\n";
}

void DP_OnMouseMove(int x, int y) {
    if (g_ctx && g_ctx->screenWidth > 0 && g_ctx->screenHeight > 0) {
        g_targetCursorUV[0] = std::clamp((float)x / (float)g_ctx->screenWidth, 0.0f, 1.0f);
        g_targetCursorUV[1] = std::clamp((float)y / (float)g_ctx->screenHeight, 0.0f, 1.0f);
    }
}

void DP_OnWallpaperChanged(const WallpaperLayers* layers) {
    if (!layers) return;

    if (g_BaseTex) { g_BaseTex->Release(); g_BaseTex = nullptr; }
    if (g_BaseSRV) { g_BaseSRV->Release(); g_BaseSRV = nullptr; }
    if (g_DepthTex) { g_DepthTex->Release(); g_DepthTex = nullptr; }
    if (g_DepthSRV) { g_DepthSRV->Release(); g_DepthSRV = nullptr; }

    std::string pathA = (layers->imagePathA) ? layers->imagePathA : "";
    std::string pathB = (layers->imagePathB) ? layers->imagePathB : "";

    if (!pathA.empty()) {
        WICLoader::LoadTexture(g_ctx->device, pathA, g_ctx->processingWidth, g_ctx->processingHeight, &g_BaseTex, &g_BaseSRV);
    }
    
    if (!pathB.empty()) {
        WICLoader::LoadTexture(g_ctx->device, pathB, g_ctx->processingWidth, g_ctx->processingHeight, &g_DepthTex, &g_DepthSRV);
    }
    
    std::cout << "[DepthParallax] Loaded wallpapers: " << pathA << " and " << pathB << "\n";
}

void DP_OnMonitorChanged(const MonitorInfo* info) {}
void DP_OnQualityTierChanged(const QualityTier* tier) {}
void DP_LoadSettings(const char* jsonPath) {}
void DP_SaveSettings(const char* jsonPath) {}

void DP_OnSettingChanged(const char* key, float value) {
    std::string k(key);
    if (k == "depth_parallax.strength" || k == "parallaxStrength") {
        g_settings.parallaxStrength = value;
        g_cbData.parallaxStrength = value;
    }
}

IEffectPlugin* CreateEffectPlugin() {
    static IEffectPlugin plugin = {};
    plugin.Initialize = DP_Initialize;
    plugin.Update = DP_Update;
    plugin.Render = DP_Render;
    plugin.Shutdown = DP_Shutdown;
    plugin.OnMouseMove = DP_OnMouseMove;
    plugin.OnWallpaperChanged = DP_OnWallpaperChanged;
    plugin.OnMonitorChanged = DP_OnMonitorChanged;
    plugin.OnQualityTierChanged = DP_OnQualityTierChanged;
    plugin.LoadSettings = DP_LoadSettings;
    plugin.SaveSettings = DP_SaveSettings;
    plugin.OnSettingChanged = DP_OnSettingChanged;
    return &plugin;
}
