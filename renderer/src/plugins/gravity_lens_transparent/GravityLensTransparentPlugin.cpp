#include "../interface/PluginAPI.h"
#include "../cursor_reveal/WICLoader.h"
#include "../../ipc/json.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>

using json = nlohmann::json;

static RendererContext* g_ctx = nullptr;

struct Settings {
    float pressDepth = 0.030f;
    float pressRadius = 0.10f;
    float stiffness = 50.0f;
    float damping = 0.90f;
    float dispersion = 0.020f;
    float coreDarkening = 0.15f;
    float trailLength = 0.4f;
    float fadeDecay = 0.92f; // 0=instant reset, 1=never fades
    float shadingStrength = 0.70f; // 0=no shading, 1=full directional shading
} g_settings;

static int g_qualityTier = 2; // 0=Low, 1=Balanced, 2=High
static int g_gridSize = 128;

// Textures
static ID3D11Texture2D*          g_TexBase = nullptr;
static ID3D11ShaderResourceView* g_SRVBase = nullptr;

static ID3D11Texture2D*          g_SimTex[2] = {nullptr, nullptr};
static ID3D11RenderTargetView*   g_SimRTV[2] = {nullptr, nullptr};
static ID3D11ShaderResourceView* g_SimSRV[2] = {nullptr, nullptr};
static int g_simIndex = 0;

static ID3D11VertexShader* g_FullscreenVS = nullptr;
static ID3D11PixelShader*  g_SimPS = nullptr;
static ID3D11PixelShader*  g_CompositePS = nullptr;

static ID3D11SamplerState* g_Sampler = nullptr;
static ID3D11BlendState*   g_BlendOpaque = nullptr;

static ID3D11Buffer* g_SimCB = nullptr;
static ID3D11Buffer* g_CompositeCB = nullptr;

__declspec(align(16))
struct SimConstantBuffer {
    float packedPoints[64]; // 16 float4s = 32 points
    float pressRadius;
    float pressDepth;
    float stiffness;
    float damping;
    float deltaTime;
    float aspectRatio;
    int numPoints;
    float fadeDecay;
};

__declspec(align(16))
struct CompositeConstantBuffer {
    float dispersion;
    float coreDarkening;
    int enableFX;
    float aspectRatio;
    float cursorUV[2];
    float pressRadius;
    float shadingStrength;
};

static float g_mouseX = -1000.0f;
static float g_mouseY = -1000.0f;

static std::vector<std::pair<float, float>> g_mouseTrail;
static float g_timeSinceLastPoint = 0.0f;

std::vector<char> GLT_ReadFileContent(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) return {};
    size_t size = (size_t)file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    file.read(buffer.data(), size);
    return buffer;
}

void GLT_CreateSimTargets() {
    for (int i=0; i<2; i++) {
        if (g_SimTex[i]) { g_SimTex[i]->Release(); g_SimTex[i] = nullptr; }
        if (g_SimRTV[i]) { g_SimRTV[i]->Release(); g_SimRTV[i] = nullptr; }
        if (g_SimSRV[i]) { g_SimSRV[i]->Release(); g_SimSRV[i] = nullptr; }
    }
    
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = g_gridSize;
    texDesc.Height = g_gridSize;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    
    for (int i=0; i<2; i++) {
        g_ctx->device->CreateTexture2D(&texDesc, nullptr, &g_SimTex[i]);
        if (g_SimTex[i]) {
            g_ctx->device->CreateRenderTargetView(g_SimTex[i], nullptr, &g_SimRTV[i]);
            g_ctx->device->CreateShaderResourceView(g_SimTex[i], nullptr, &g_SimSRV[i]);
            
            float clearColor[4] = {0,0,0,0};
            g_ctx->context->ClearRenderTargetView(g_SimRTV[i], clearColor);
        }
    }
}

void GLT_Initialize(RendererContext* ctx) {
    g_ctx = ctx;
    std::cout << "[GravityLensTransparent] Initializing...\n";

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir = exePath;
    exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));
    std::string pluginDir = exeDir + "\\plugins\\";

    auto vsData = GLT_ReadFileContent(pluginDir + "GLT_FullscreenVS.cso");
    auto simData = GLT_ReadFileContent(pluginDir + "GLT_SimPS.cso");
    auto compData = GLT_ReadFileContent(pluginDir + "GLT_CompositePS.cso");

    g_ctx->device->CreateVertexShader(vsData.data(), vsData.size(), nullptr, &g_FullscreenVS);
    g_ctx->device->CreatePixelShader(simData.data(), simData.size(), nullptr, &g_SimPS);
    g_ctx->device->CreatePixelShader(compData.data(), compData.size(), nullptr, &g_CompositePS);

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    cbDesc.ByteWidth = sizeof(SimConstantBuffer);
    g_ctx->device->CreateBuffer(&cbDesc, nullptr, &g_SimCB);
    
    cbDesc.ByteWidth = sizeof(CompositeConstantBuffer);
    g_ctx->device->CreateBuffer(&cbDesc, nullptr, &g_CompositeCB);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    g_ctx->device->CreateSamplerState(&sampDesc, &g_Sampler);

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_ctx->device->CreateBlendState(&blendDesc, &g_BlendOpaque);

    GLT_CreateSimTargets();
}

void GLT_Update(float deltaTime) {
    if (!g_ctx || !g_FullscreenVS || !g_SimPS) return;

#ifdef _DEBUG
    static float s_debugTimer = 0.0f;
    s_debugTimer += deltaTime;
    if (s_debugTimer >= 1.0f) {
        s_debugTimer = 0.0f;
        std::cout << "[GravityLensTransparent] LIVE SETTINGS -> "
                  << "Strength: " << g_settings.pressDepth << ", "
                  << "Radius: " << g_settings.pressRadius << ", "
                  << "Stiffness: " << g_settings.stiffness << ", "
                  << "Damping: " << g_settings.damping << ", "
                  << "Dispersion: " << g_settings.dispersion << ", "
                  << "CoreDarkening: " << g_settings.coreDarkening << "\n";
    }
#endif

    // Ping-pong sim update
    int nextSim = (g_simIndex + 1) % 2;
    
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)g_gridSize;
    vp.Height = (float)g_gridSize;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_ctx->context->RSSetViewports(1, &vp);

    // Explicitly unbind all SRVs before our pass begins to ensure zero state bleeding
    ID3D11ShaderResourceView* nullSRVs[8] = {nullptr};
    g_ctx->context->PSSetShaderResources(0, 8, nullSRVs);

    g_ctx->context->OMSetRenderTargets(1, &g_SimRTV[nextSim], nullptr);
    g_ctx->context->OMSetBlendState(g_BlendOpaque, nullptr, 0xffffffff);
    
    g_ctx->context->IASetInputLayout(nullptr);
    g_ctx->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->context->VSSetShader(g_FullscreenVS, nullptr, 0);
    g_ctx->context->PSSetShader(g_SimPS, nullptr, 0);

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_ctx->context->Map(g_SimCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        SimConstantBuffer* scb = (SimConstantBuffer*)mapped.pData;
        memset(scb->packedPoints, 0, sizeof(scb->packedPoints));
        
        float pointInterval = (g_settings.trailLength > 0.0f) ? (g_settings.trailLength / 32.0f) : 0.0f;
        g_timeSinceLastPoint += deltaTime;
        
        if (g_settings.trailLength <= 0.01f) {
            g_mouseTrail.clear();
            g_mouseTrail.push_back({ g_mouseX, g_mouseY });
        } else {
            // Only add a new point if interval passed, or if trail is empty
            if (g_mouseTrail.empty() || g_timeSinceLastPoint >= pointInterval) {
                g_timeSinceLastPoint = 0.0f;
                g_mouseTrail.insert(g_mouseTrail.begin(), { g_mouseX, g_mouseY });
                if (g_mouseTrail.size() > 32) g_mouseTrail.pop_back();
            } else if (!g_mouseTrail.empty()) {
                // Always keep the very front point updated to the exact current cursor for responsiveness
                g_mouseTrail[0] = { g_mouseX, g_mouseY };
            }
        }
        
        scb->numPoints = (int)g_mouseTrail.size();
        for (int i = 0; i < scb->numPoints; i++) {
            float uvX = (g_ctx->screenWidth > 0) ? (g_mouseTrail[i].first / (float)g_ctx->screenWidth) : 0.5f;
            float uvY = (g_ctx->screenHeight > 0) ? (g_mouseTrail[i].second / (float)g_ctx->screenHeight) : 0.5f;
            
            scb->packedPoints[i * 2 + 0] = uvX;
            scb->packedPoints[i * 2 + 1] = uvY;
        }

        scb->pressRadius = g_settings.pressRadius;
        scb->pressDepth = g_settings.pressDepth;
        scb->stiffness = g_settings.stiffness;
        scb->damping = g_settings.damping;
        
        // Clamp deltaTime to prevent physics explosions / stuttering on frame spikes
        scb->deltaTime = std::clamp(deltaTime, 0.001f, 0.033f);
        
        scb->aspectRatio = (float)g_ctx->screenWidth / (float)g_ctx->screenHeight;
        scb->fadeDecay = g_settings.fadeDecay;
        
        g_ctx->context->Unmap(g_SimCB, 0);
    }
    g_ctx->context->PSSetConstantBuffers(0, 1, &g_SimCB);
    
    ID3D11ShaderResourceView* srvs[1] = { g_SimSRV[g_simIndex] };
    g_ctx->context->PSSetShaderResources(0, 1, srvs);
    g_ctx->context->PSSetSamplers(0, 1, &g_Sampler);

    g_ctx->context->Draw(3, 0);
    
    // Unbind SRV
    ID3D11ShaderResourceView* nullSRV[1] = {nullptr};
    g_ctx->context->PSSetShaderResources(0, 1, nullSRV);

    g_simIndex = nextSim;
}

void GLT_Render() {
    if (!g_ctx || !g_ctx->mainRenderTargetView || !g_CompositePS || !g_TexBase) return;

    D3D11_VIEWPORT screenVp = {};
    screenVp.Width = (float)g_ctx->screenWidth;
    screenVp.Height = (float)g_ctx->screenHeight;
    screenVp.MinDepth = 0.0f;
    screenVp.MaxDepth = 1.0f;
    g_ctx->context->RSSetViewports(1, &screenVp);

    g_ctx->context->OMSetRenderTargets(1, &g_ctx->mainRenderTargetView, nullptr);
    g_ctx->context->OMSetBlendState(g_BlendOpaque, nullptr, 0xffffffff);
    
    g_ctx->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->context->VSSetShader(g_FullscreenVS, nullptr, 0);
    g_ctx->context->PSSetShader(g_CompositePS, nullptr, 0);

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_ctx->context->Map(g_CompositeCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        CompositeConstantBuffer* ccb = (CompositeConstantBuffer*)mapped.pData;
        ccb->dispersion = g_settings.dispersion;
        ccb->coreDarkening = g_settings.coreDarkening;
        ccb->enableFX = (g_qualityTier > 0) ? 1 : 0;
        ccb->aspectRatio = (float)g_ctx->screenWidth / (float)g_ctx->screenHeight;
        
        if (g_ctx && g_ctx->screenWidth > 0 && g_ctx->screenHeight > 0) {
            ccb->cursorUV[0] = std::clamp((float)g_mouseX / (float)g_ctx->screenWidth, 0.0f, 1.0f);
            ccb->cursorUV[1] = std::clamp((float)g_mouseY / (float)g_ctx->screenHeight, 0.0f, 1.0f);
        }
        ccb->pressRadius = g_settings.pressRadius;
        ccb->shadingStrength = g_settings.shadingStrength;
        g_ctx->context->Unmap(g_CompositeCB, 0);
    }
    g_ctx->context->PSSetConstantBuffers(0, 1, &g_CompositeCB);

    ID3D11ShaderResourceView* srvs[2] = { g_SRVBase, g_SimSRV[g_simIndex] };
    g_ctx->context->PSSetShaderResources(0, 2, srvs);
    g_ctx->context->PSSetSamplers(0, 1, &g_Sampler);

    g_ctx->context->Draw(3, 0);

    ID3D11ShaderResourceView* nullSRV[2] = {nullptr, nullptr};
    g_ctx->context->PSSetShaderResources(0, 2, nullSRV);
}

void GLT_Shutdown() {
    if (g_TexBase) { g_TexBase->Release(); g_TexBase = nullptr; }
    if (g_SRVBase) { g_SRVBase->Release(); g_SRVBase = nullptr; }
    for(int i=0; i<2; i++) {
        if (g_SimTex[i]) { g_SimTex[i]->Release(); g_SimTex[i] = nullptr; }
        if (g_SimRTV[i]) { g_SimRTV[i]->Release(); g_SimRTV[i] = nullptr; }
        if (g_SimSRV[i]) { g_SimSRV[i]->Release(); g_SimSRV[i] = nullptr; }
    }
    if (g_FullscreenVS) { g_FullscreenVS->Release(); g_FullscreenVS = nullptr; }
    if (g_SimPS) { g_SimPS->Release(); g_SimPS = nullptr; }
    if (g_CompositePS) { g_CompositePS->Release(); g_CompositePS = nullptr; }
    if (g_Sampler) { g_Sampler->Release(); g_Sampler = nullptr; }
    if (g_BlendOpaque) { g_BlendOpaque->Release(); g_BlendOpaque = nullptr; }
    if (g_SimCB) { g_SimCB->Release(); g_SimCB = nullptr; }
    if (g_CompositeCB) { g_CompositeCB->Release(); g_CompositeCB = nullptr; }

    // Explicit safety net: Ensure these slots are completely cleared from the GPU
    if (g_ctx && g_ctx->context) {
        ID3D11ShaderResourceView* nullSRVs[8] = {nullptr};
        g_ctx->context->PSSetShaderResources(0, 8, nullSRVs);
    }
}

void GLT_OnMouseMove(int x, int y) {
    if (!g_ctx) return;
    // Store raw screen-pixel coordinates; normalization to 0-1 UV happens at use sites
    g_mouseX = (float)x;
    g_mouseY = (float)y;
}

void GLT_OnWallpaperChanged(const WallpaperLayers* layers) {
    if (g_TexBase) { g_TexBase->Release(); g_TexBase = nullptr; }
    if (g_SRVBase) { g_SRVBase->Release(); g_SRVBase = nullptr; }
    
    if (layers && layers->imagePathA) {
        WICLoader::LoadTexture(g_ctx->device, layers->imagePathA, g_ctx->processingWidth, g_ctx->processingHeight, &g_TexBase, &g_SRVBase);
    }
}

void GLT_OnMonitorChanged(const MonitorInfo* info) {}

void GLT_OnQualityTierChanged(const QualityTier* tier) {
    g_qualityTier = tier->level;
    int newSize = (g_qualityTier == 0) ? 64 : 128;
    if (g_gridSize != newSize) {
        g_gridSize = newSize;
        if (g_ctx && g_ctx->device) {
            GLT_CreateSimTargets();
        }
    }
}

void GLT_LoadSettings(const char* jsonPath) {
    try {
        std::ifstream file(jsonPath);
        if (file.is_open()) {
            json j;
            file >> j;
            if (j.contains("pressDepth")) g_settings.pressDepth = std::clamp((float)j["pressDepth"], 0.0f, 0.15f);
            if (j.contains("pressRadius")) g_settings.pressRadius = std::clamp((float)j["pressRadius"], 0.01f, 0.5f);
            if (j.contains("stiffness")) g_settings.stiffness = j["stiffness"];
            if (j.contains("damping")) g_settings.damping = j["damping"];
            if (j.contains("dispersion")) g_settings.dispersion = j["dispersion"];
            if (j.contains("coreDarkening")) g_settings.coreDarkening = j["coreDarkening"];
            if (j.contains("trailLength")) g_settings.trailLength = j["trailLength"];
            if (j.contains("fadeDecay")) g_settings.fadeDecay = j["fadeDecay"];
            if (j.contains("shadingStrength")) g_settings.shadingStrength = std::clamp((float)j["shadingStrength"], 0.0f, 1.0f);
        }
    } catch (...) {}
}

void GLT_SaveSettings(const char* jsonPath) {
    try {
        json j;
        j["pressDepth"] = g_settings.pressDepth;
        j["pressRadius"] = g_settings.pressRadius;
        j["stiffness"] = g_settings.stiffness;
        j["damping"] = g_settings.damping;
        j["dispersion"] = g_settings.dispersion;
        j["coreDarkening"] = g_settings.coreDarkening;
        j["trailLength"] = g_settings.trailLength;
        j["fadeDecay"] = g_settings.fadeDecay;
        j["shadingStrength"] = g_settings.shadingStrength;
        
        std::ofstream file(jsonPath);
        if (file.is_open()) {
            file << j.dump(4);
        }
    } catch (...) {}
}

void GLT_OnSettingChanged(const char* key, float value) {
    std::string k(key);
    if (k == "pressDepth") g_settings.pressDepth = std::clamp(value, 0.0f, 0.15f);
    else if (k == "pressRadius") g_settings.pressRadius = std::clamp(value, 0.01f, 0.5f);
    else if (k == "stiffness") g_settings.stiffness = value;
    else if (k == "damping") g_settings.damping = value;
    else if (k == "dispersion") g_settings.dispersion = value;
    else if (k == "coreDarkening") g_settings.coreDarkening = value;
    else if (k == "trailLength") g_settings.trailLength = value;
    else if (k == "fadeDecay") g_settings.fadeDecay = value;
    else if (k == "shadingStrength") g_settings.shadingStrength = std::clamp(value, 0.0f, 1.0f);
}

static IEffectPlugin g_plugin = {
    GLT_Initialize,
    GLT_Update,
    GLT_Render,
    GLT_Shutdown,
    GLT_OnMouseMove,
    GLT_OnWallpaperChanged,
    GLT_OnMonitorChanged,
    GLT_OnQualityTierChanged,
    GLT_LoadSettings,
    GLT_SaveSettings,
    GLT_OnSettingChanged
};

extern "C" __declspec(dllexport) IEffectPlugin* CreateEffectPlugin() {
    return &g_plugin;
}
