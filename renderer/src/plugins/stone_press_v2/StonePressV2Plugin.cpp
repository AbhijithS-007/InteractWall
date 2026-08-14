#include "../interface/PluginAPI.h"
#include <d3dcompiler.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <algorithm>
#include "../cursor_reveal/WICLoader.h"
#include "../../ipc/json.hpp"

using json = nlohmann::json;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

static RendererContext* g_ctx = nullptr;

struct Settings {
    float pressDepth = 2.80f;
    float pressRadius = 0.09f;
    float stiffness = 300.0f;
    float damping = 0.99f;
    float depthDarkening = 1.35f;
    float directionalShading = 0.70f;
    float parallaxStrength = 0.3f;
};
static Settings g_settings;
static int g_qualityTier = 2;

static ID3D11ShaderResourceView* g_SRVBase = nullptr;
static ID3D11Texture2D* g_TexBase = nullptr;

static ID3D11VertexShader* g_FullscreenVS = nullptr;
static ID3D11PixelShader* g_CompositePS = nullptr;
static ID3D11SamplerState* g_Sampler = nullptr;
static ID3D11BlendState* g_BlendOpaque = nullptr;

static ID3D11Buffer* g_CompositeCB = nullptr;

__declspec(align(16))
struct CompositeConstantBuffer {
    float parallaxStrength;
    float depthDarkening;
    float directionalShading;
    int enableFX;
    
    float cursorUV[2];
    float pressRadius;
    float pressDepth;
    float aspectRatio;
    float pad[3];
};

static float g_mouseX = -1000.0f;
static float g_mouseY = -1000.0f;
static float g_physX = -1000.0f;
static float g_physY = -1000.0f;
static float g_velX = 0.0f;
static float g_velY = 0.0f;
static float g_speed = 0.0f;

std::vector<char> SP2_ReadFileContent(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) return {};
    size_t size = (size_t)file.tellg();
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), size);
    return buffer;
}

void SP2_Initialize(RendererContext* ctx) {
    g_ctx = ctx;
    std::cout << "[StonePress V2] Initializing...\n";

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string pathStr(exePath);
    size_t lastSlash = pathStr.find_last_of("\\/");
    std::string pluginDir = pathStr.substr(0, lastSlash + 1) + "plugins\\";

    auto vsData = SP2_ReadFileContent(pluginDir + "SP2_FullscreenVS.cso");
    auto compData = SP2_ReadFileContent(pluginDir + "SP2_CompositePS.cso");

    if (!vsData.empty()) g_ctx->device->CreateVertexShader(vsData.data(), vsData.size(), nullptr, &g_FullscreenVS);
    if (!compData.empty()) g_ctx->device->CreatePixelShader(compData.data(), compData.size(), nullptr, &g_CompositePS);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    g_ctx->device->CreateSamplerState(&sampDesc, &g_Sampler);

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    cbDesc.ByteWidth = sizeof(CompositeConstantBuffer);
    g_ctx->device->CreateBuffer(&cbDesc, nullptr, &g_CompositeCB);

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_ctx->device->CreateBlendState(&blendDesc, &g_BlendOpaque);
}

void SP2_Update(float deltaTime) {
    if (g_physX == -1000.0f || g_mouseX == -1000.0f) {
        g_physX = g_mouseX;
        g_physY = g_mouseY;
        return;
    }
    
    // Hooke's law physics for smooth rubber sheet tracking
    float k = max(g_settings.stiffness, 1.0f); 
    float c = max(g_settings.damping, 0.0f) * 2.0f * sqrt(k); // 1.0 damping = critically damped
    
    float forceX = (g_mouseX - g_physX) * k - g_velX * c;
    float forceY = (g_mouseY - g_physY) * k - g_velY * c;
    
    g_velX += forceX * deltaTime;
    g_velY += forceY * deltaTime;
    
    g_physX += g_velX * deltaTime;
    g_physY += g_velY * deltaTime;
    
    g_speed = sqrt(g_velX * g_velX + g_velY * g_velY);
}

void SP2_Render() {
    if (!g_ctx || !g_SRVBase) return;

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_ctx->context->Map(g_CompositeCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        CompositeConstantBuffer* ccb = (CompositeConstantBuffer*)mapped.pData;
        ccb->parallaxStrength = g_settings.parallaxStrength;
        ccb->depthDarkening = g_settings.depthDarkening;
        ccb->directionalShading = (g_qualityTier > 0) ? g_settings.directionalShading : 0.0f;
        ccb->enableFX = 1;
        ccb->cursorUV[0] = std::clamp((float)g_physX / (float)g_ctx->screenWidth, 0.0f, 1.0f);
        ccb->cursorUV[1] = std::clamp((float)g_physY / (float)g_ctx->screenHeight, 0.0f, 1.0f);
        ccb->pressRadius = g_settings.pressRadius;
        
        // Dynamically increase depth based on velocity (dynamic tension)
        float dynamicMultiplier = 1.0f + min(g_speed * 0.0003f, 1.5f);
        ccb->pressDepth = g_settings.pressDepth * dynamicMultiplier;
        ccb->aspectRatio = (float)g_ctx->screenWidth / (float)g_ctx->screenHeight;
        g_ctx->context->Unmap(g_CompositeCB, 0);
    }

    g_ctx->context->OMSetRenderTargets(1, &g_ctx->mainRenderTargetView, nullptr);
    g_ctx->context->OMSetBlendState(g_BlendOpaque, nullptr, 0xFFFFFFFF);

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)g_ctx->screenWidth;
    vp.Height = (float)g_ctx->screenHeight;
    vp.MaxDepth = 1.0f;
    g_ctx->context->RSSetViewports(1, &vp);

    g_ctx->context->IASetInputLayout(nullptr);
    g_ctx->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->context->VSSetShader(g_FullscreenVS, nullptr, 0);
    g_ctx->context->PSSetShader(g_CompositePS, nullptr, 0);

    g_ctx->context->PSSetConstantBuffers(0, 1, &g_CompositeCB);

    g_ctx->context->PSSetShaderResources(0, 1, &g_SRVBase);
    g_ctx->context->PSSetSamplers(0, 1, &g_Sampler);

    g_ctx->context->Draw(3, 0);

    ID3D11ShaderResourceView* nullSRV[1] = {nullptr};
    g_ctx->context->PSSetShaderResources(0, 1, nullSRV);
}

void SP2_Shutdown() {
    if (g_SRVBase) { g_SRVBase->Release(); g_SRVBase = nullptr; }
    if (g_TexBase) { g_TexBase->Release(); g_TexBase = nullptr; }
    
    if (g_FullscreenVS) { g_FullscreenVS->Release(); g_FullscreenVS = nullptr; }
    if (g_CompositePS) { g_CompositePS->Release(); g_CompositePS = nullptr; }
    if (g_Sampler) { g_Sampler->Release(); g_Sampler = nullptr; }
    if (g_BlendOpaque) { g_BlendOpaque->Release(); g_BlendOpaque = nullptr; }
    if (g_CompositeCB) { g_CompositeCB->Release(); g_CompositeCB = nullptr; }
    
    std::cout << "[StonePress V2] Shutdown complete.\n";
}

void SP2_OnMouseMove(int x, int y) {
    g_mouseX = (float)x;
    g_mouseY = (float)y;
}

void SP2_OnWallpaperChanged(const WallpaperLayers* layers) {
    if (!g_ctx || !g_ctx->device) return;

    if (g_SRVBase) { g_SRVBase->Release(); g_SRVBase = nullptr; }
    if (g_TexBase) { g_TexBase->Release(); g_TexBase = nullptr; }

    if (layers && layers->imagePathA && strlen(layers->imagePathA) > 0) {
        std::cout << "[StonePress V2] Loading Base Texture: " << layers->imagePathA << "\n";
        WICLoader::LoadTexture(g_ctx->device, layers->imagePathA, g_ctx->processingWidth, g_ctx->processingHeight, &g_TexBase, &g_SRVBase);
    }
}

void SP2_OnMonitorChanged(const MonitorInfo* info) {}

void SP2_OnQualityTierChanged(const QualityTier* tier) {
    if (tier) g_qualityTier = tier->level;
}

void SP2_LoadSettings(const char* jsonPath) {
    try {
        std::ifstream file(jsonPath);
        if (file.is_open()) {
            json j;
            file >> j;
            if (j.contains("pressDepth")) g_settings.pressDepth = j["pressDepth"];
            if (j.contains("pressRadius")) g_settings.pressRadius = j["pressRadius"];
            if (j.contains("stiffness")) g_settings.stiffness = j["stiffness"];
            if (j.contains("damping")) g_settings.damping = j["damping"];
            if (j.contains("depthDarkening")) g_settings.depthDarkening = j["depthDarkening"];
            if (j.contains("directionalShading")) g_settings.directionalShading = j["directionalShading"];
            if (j.contains("parallaxStrength")) g_settings.parallaxStrength = j["parallaxStrength"];
        }
    } catch (...) {}
}

void SP2_SaveSettings(const char* jsonPath) {
    try {
        json j;
        j["pressDepth"] = g_settings.pressDepth;
        j["pressRadius"] = g_settings.pressRadius;
        j["stiffness"] = g_settings.stiffness;
        j["damping"] = g_settings.damping;
        j["depthDarkening"] = g_settings.depthDarkening;
        j["directionalShading"] = g_settings.directionalShading;
        j["parallaxStrength"] = g_settings.parallaxStrength;
        
        std::ofstream file(jsonPath);
        if (file.is_open()) {
            file << j.dump(4);
        }
    } catch (...) {}
}

void SP2_OnSettingChanged(const char* key, float value) {
    std::string k(key);
    if (k == "pressDepth") g_settings.pressDepth = value;
    else if (k == "pressRadius") g_settings.pressRadius = value;
    else if (k == "stiffness") g_settings.stiffness = value;
    else if (k == "damping") g_settings.damping = value;
    else if (k == "depthDarkening") g_settings.depthDarkening = value;
    else if (k == "directionalShading") g_settings.directionalShading = value;
    else if (k == "parallaxStrength") g_settings.parallaxStrength = value;
}

static IEffectPlugin g_plugin = {
    SP2_Initialize,
    SP2_Update,
    SP2_Render,
    SP2_Shutdown,
    SP2_OnMouseMove,
    SP2_OnWallpaperChanged,
    SP2_OnMonitorChanged,
    SP2_OnQualityTierChanged,
    SP2_LoadSettings,
    SP2_SaveSettings,
    SP2_OnSettingChanged
};

extern "C" __declspec(dllexport) IEffectPlugin* GetPluginAPI() { return &g_plugin; }
extern "C" __declspec(dllexport) IEffectPlugin* CreateEffectPlugin() { return &g_plugin; }
