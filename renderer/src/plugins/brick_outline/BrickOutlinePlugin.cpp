#include "../interface/PluginAPI.h"
#include "../cursor_reveal/WICLoader.h"
#include "../../ipc/json.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

using json = nlohmann::json;

static RendererContext* g_ctx = nullptr;

struct Settings {
    float brickWidth = 100.0f;
    float brickHeight = 50.0f;
    float lineThickness = 3.0f;
    float effectRadius = 0.2f;
    float edgeSoftness = 0.1f;
    float glowIntensity = 1.0f;
    float outlineColor[3] = {1.0f, 1.0f, 1.0f};
} g_settings;

struct BrickConstantBuffer {
    float resolution[2];
    float cursorPos[2];
    float brickWidth;
    float brickHeight;
    float lineThickness;
    float effectRadius;
    float edgeSoftness;
    float glowIntensity;
    int qualityTier;
    float padding1;
    float outlineColor[3];
    float padding2;
};

static BrickConstantBuffer g_cbData = {};
static float g_targetCursorUV[2] = {0.5f, 0.5f};

static ID3D11VertexShader* g_FullscreenVS = nullptr;
static ID3D11PixelShader*  g_CompositePS = nullptr;

static ID3D11Texture2D*          g_BaseTex = nullptr;
static ID3D11ShaderResourceView* g_BaseSRV = nullptr;

static ID3D11SamplerState*       g_Sampler = nullptr;
static ID3D11BlendState*         g_BlendOpaque = nullptr;
static ID3D11Buffer*             g_CB = nullptr;
static int g_qualityTier = 2; // Default High

std::vector<char> BO_ReadFileContent(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) return {};
    size_t size = (size_t)file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    file.read(buffer.data(), size);
    return buffer;
}

void BO_Initialize(RendererContext* ctx) {
    g_ctx = ctx;
    std::cout << "[BrickOutline] Initializing...\n";

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir = exePath;
    exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));
    std::string pluginDir = exeDir + "\\plugins\\";

    auto vsData = BO_ReadFileContent(pluginDir + "BO_FullscreenVS.cso");
    auto psData = BO_ReadFileContent(pluginDir + "BO_CompositePS.cso");

    if (vsData.empty() || psData.empty()) {
        std::cout << "[BrickOutline] Failed to load shaders.\n";
        return;
    }

    g_ctx->device->CreateVertexShader(vsData.data(), vsData.size(), nullptr, &g_FullscreenVS);
    g_ctx->device->CreatePixelShader(psData.data(), psData.size(), nullptr, &g_CompositePS);

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = sizeof(BrickConstantBuffer);
    g_ctx->device->CreateBuffer(&cbDesc, nullptr, &g_CB);

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

    g_cbData.cursorPos[0] = 0.5f;
    g_cbData.cursorPos[1] = 0.5f;
}

void BO_Shutdown() {
    if (g_BaseSRV) { g_BaseSRV->Release(); g_BaseSRV = nullptr; }
    if (g_BaseTex) { g_BaseTex->Release(); g_BaseTex = nullptr; }
    if (g_FullscreenVS) { g_FullscreenVS->Release(); g_FullscreenVS = nullptr; }
    if (g_CompositePS) { g_CompositePS->Release(); g_CompositePS = nullptr; }
    if (g_CB) { g_CB->Release(); g_CB = nullptr; }
    if (g_Sampler) { g_Sampler->Release(); g_Sampler = nullptr; }
    if (g_BlendOpaque) { g_BlendOpaque->Release(); g_BlendOpaque = nullptr; }
}

void BO_Update(float deltaTime) {
    // Instant tracking
    g_cbData.cursorPos[0] = g_targetCursorUV[0];
    g_cbData.cursorPos[1] = g_targetCursorUV[1];
}

void BO_Render() {
    if (!g_ctx || !g_ctx->context) return;

    if (!g_BaseSRV) {
        float clearColor[4] = {0,0,0,1};
        g_ctx->context->ClearRenderTargetView(g_ctx->mainRenderTargetView, clearColor);
        return;
    }

    g_cbData.resolution[0] = (float)g_ctx->screenWidth;
    g_cbData.resolution[1] = (float)g_ctx->screenHeight;
    g_cbData.brickWidth = g_settings.brickWidth;
    g_cbData.brickHeight = g_settings.brickHeight;
    g_cbData.lineThickness = g_settings.lineThickness;
    g_cbData.effectRadius = g_settings.effectRadius;
    g_cbData.edgeSoftness = g_settings.edgeSoftness;
    g_cbData.glowIntensity = g_settings.glowIntensity;
    g_cbData.qualityTier = g_qualityTier;
    g_cbData.outlineColor[0] = g_settings.outlineColor[0];
    g_cbData.outlineColor[1] = g_settings.outlineColor[1];
    g_cbData.outlineColor[2] = g_settings.outlineColor[2];

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_ctx->context->Map(g_CB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &g_cbData, sizeof(BrickConstantBuffer));
        g_ctx->context->Unmap(g_CB, 0);
    }

    ID3D11ShaderResourceView* nullSRVs[8] = {nullptr};
    g_ctx->context->PSSetShaderResources(0, 8, nullSRVs);

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)g_ctx->screenWidth;
    vp.Height = (float)g_ctx->screenHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_ctx->context->RSSetViewports(1, &vp);

    g_ctx->context->OMSetRenderTargets(1, &g_ctx->mainRenderTargetView, nullptr);
    g_ctx->context->OMSetBlendState(g_BlendOpaque, nullptr, 0xFFFFFFFF);

    g_ctx->context->VSSetShader(g_FullscreenVS, nullptr, 0);
    g_ctx->context->PSSetShader(g_CompositePS, nullptr, 0);

    g_ctx->context->PSSetConstantBuffers(0, 1, &g_CB);
    g_ctx->context->PSSetShaderResources(0, 1, &g_BaseSRV);
    g_ctx->context->PSSetSamplers(0, 1, &g_Sampler);

    g_ctx->context->IASetInputLayout(nullptr);
    g_ctx->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->context->Draw(3, 0);

    ID3D11ShaderResourceView* clearSRVs[1] = { nullptr };
    g_ctx->context->PSSetShaderResources(0, 1, clearSRVs);
}

void BO_OnMouseMove(int x, int y) {
    if (g_ctx && g_ctx->screenWidth > 0 && g_ctx->screenHeight > 0) {
        g_targetCursorUV[0] = (float)x / g_ctx->screenWidth;
        g_targetCursorUV[1] = (float)y / g_ctx->screenHeight;
    }
}

void BO_OnMouseDown(int button, int x, int y) {}
void BO_OnMouseUp(int button, int x, int y) {}

void BO_OnQualityTierChanged(const QualityTier* tier) {
    if (tier) {
        g_qualityTier = tier->level;
    }
}

void BO_OnSettingChanged(const char* key, float value) {
    std::string k = key;
    if (k == "brickWidth") g_settings.brickWidth = value;
    else if (k == "brickHeight") g_settings.brickHeight = value;
    else if (k == "lineThickness") g_settings.lineThickness = value;
    else if (k == "effectRadius") g_settings.effectRadius = value;
    else if (k == "edgeSoftness") g_settings.edgeSoftness = value;
    else if (k == "glowIntensity") g_settings.glowIntensity = value;
    else if (k == "outlineColorR") g_settings.outlineColor[0] = value;
    else if (k == "outlineColorG") g_settings.outlineColor[1] = value;
    else if (k == "outlineColorB") g_settings.outlineColor[2] = value;
}

void BO_OnWallpaperChanged(const WallpaperLayers* layers) {
    if (g_BaseSRV) { g_BaseSRV->Release(); g_BaseSRV = nullptr; }
    if (g_BaseTex) { g_BaseTex->Release(); g_BaseTex = nullptr; }
    if (layers && layers->imagePathA && strlen(layers->imagePathA) > 0) {
        WICLoader::LoadTexture(g_ctx->device, layers->imagePathA, 8192, 8192, &g_BaseTex, &g_BaseSRV);
    }
}

void BO_SaveSettings(const char* filename) {
    json j;
    j["brickWidth"] = g_settings.brickWidth;
    j["brickHeight"] = g_settings.brickHeight;
    j["lineThickness"] = g_settings.lineThickness;
    j["effectRadius"] = g_settings.effectRadius;
    j["edgeSoftness"] = g_settings.edgeSoftness;
    j["glowIntensity"] = g_settings.glowIntensity;
    j["outlineColorR"] = g_settings.outlineColor[0];
    j["outlineColorG"] = g_settings.outlineColor[1];
    j["outlineColorB"] = g_settings.outlineColor[2];
    std::ofstream o(filename);
    if (o.is_open()) {
        o << j.dump(4);
    }
}

void BO_LoadSettings(const char* filename) {
    std::ifstream i(filename);
    if (i.is_open()) {
        json j;
        try {
            i >> j;
            if (j.contains("brickWidth")) g_settings.brickWidth = j["brickWidth"];
            if (j.contains("brickHeight")) g_settings.brickHeight = j["brickHeight"];
            if (j.contains("lineThickness")) g_settings.lineThickness = j["lineThickness"];
            if (j.contains("effectRadius")) g_settings.effectRadius = j["effectRadius"];
            if (j.contains("edgeSoftness")) g_settings.edgeSoftness = j["edgeSoftness"];
            if (j.contains("glowIntensity")) g_settings.glowIntensity = j["glowIntensity"];
            if (j.contains("outlineColorR")) g_settings.outlineColor[0] = j["outlineColorR"];
            if (j.contains("outlineColorG")) g_settings.outlineColor[1] = j["outlineColorG"];
            if (j.contains("outlineColorB")) g_settings.outlineColor[2] = j["outlineColorB"];
        } catch (...) {}
    }
}

extern "C" {
    __declspec(dllexport) IEffectPlugin* CreateEffectPlugin() {
        IEffectPlugin* plugin = new IEffectPlugin();
        plugin->Initialize = BO_Initialize;
        plugin->Shutdown = BO_Shutdown;
        plugin->Update = BO_Update;
        plugin->Render = BO_Render;
        plugin->OnMouseMove = BO_OnMouseMove;
        plugin->OnWallpaperChanged = BO_OnWallpaperChanged;
        plugin->OnQualityTierChanged = BO_OnQualityTierChanged;
        plugin->OnSettingChanged = BO_OnSettingChanged;
        plugin->SaveSettings = BO_SaveSettings;
        plugin->LoadSettings = BO_LoadSettings;
        return plugin;
    }
}
