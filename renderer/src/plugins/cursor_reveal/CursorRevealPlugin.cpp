#include "../interface/PluginAPI.h"
#include "WICLoader.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <deque>

static RendererContext* g_ctx = nullptr;
static std::vector<std::pair<float, float>> g_mousePoints;

struct HistoryPoint {
    float x, y;
    float timeAdded;
};
static std::deque<HistoryPoint> g_historyPoints;
static float g_currentTime = 0.0f;
static float g_lastFrameDelta = 0.0f;

struct Settings {
    float brushSize = 160.0f;
    float brushHardness = 0.2f; // Lower = softer edge
    float fadeSpeed = 0.035f;   // Lower = lasts longer
    float trailLength = 1.0f;
    bool fadeWhenResting = true;
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
    float packedPoints[32]; // 16 float2s
    int numPoints;
    float brushSize;
    float brushHardness;
    float padding;
};

__declspec(align(16))
struct FadeConstantBuffer {
    float fadeAmount;
    float padding[3];
};

static BrushConstantBuffer g_brushData = {};

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
    auto vsData = ReadFileContent(pluginDir + "CR_FullscreenVS.cso");
    auto compData = ReadFileContent(pluginDir + "CR_CompositePS.cso");
    auto brushData = ReadFileContent(pluginDir + "CR_BrushPS.cso");
    auto fadeData = ReadFileContent(pluginDir + "CR_FadePS.cso");

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

void CR_Update(float deltaTime) {
    g_currentTime += deltaTime;
    g_lastFrameDelta = deltaTime;
    
#ifdef _DEBUG
    static float s_debugTimer = 0.0f;
    s_debugTimer += deltaTime;
    if (s_debugTimer >= 1.0f) {
        s_debugTimer = 0.0f;
        std::cout << "[CursorReveal] LIVE SETTINGS -> "
                  << "Trail Length: " << g_settings.trailLength << "s, "
                  << "Fade Speed: " << g_settings.fadeSpeed << " (per frame base)\n";
    }
#endif
}

void CR_Render() {
    if (!g_ctx || !g_ctx->context || !g_FullscreenVS) return;

    // 1. Setup Viewport to processing resolution
    D3D11_VIEWPORT maskVp = {};
    maskVp.Width = (float)g_ctx->processingWidth;
    maskVp.Height = (float)g_ctx->processingHeight;
    maskVp.MinDepth = 0.0f;
    maskVp.MaxDepth = 1.0f;
    g_ctx->context->RSSetViewports(1, &maskVp);

    // Explicitly unbind all SRVs before our pass begins to ensure zero state bleeding
    ID3D11ShaderResourceView* nullSRVs[8] = {nullptr};
    g_ctx->context->PSSetShaderResources(0, 8, nullSRVs);

    // Bind common state
    g_ctx->context->IASetInputLayout(nullptr);
    g_ctx->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->context->VSSetShader(g_FullscreenVS, nullptr, 0);
    float blendFactor[4] = {0,0,0,0};
    g_ctx->context->OMSetBlendState(g_BlendSubtractive, blendFactor, 0xffffffff);
    g_ctx->context->PSSetShader(g_FadePS, nullptr, 0);

    // 2. FADE PASS OR CLEAR
    if (g_settings.trailLength <= 0.001f) {
        float clearColor[4] = {0,0,0,0};
        g_ctx->context->ClearRenderTargetView(g_MaskRTV, clearColor);
    } else {
        g_ctx->context->OMSetRenderTargets(1, &g_MaskRTV, nullptr);
        
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(g_ctx->context->Map(g_FadeCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            FadeConstantBuffer* fc = (FadeConstantBuffer*)mapped.pData;
            fc->fadeAmount = g_settings.fadeSpeed * g_lastFrameDelta * 60.0f;
            g_ctx->context->Unmap(g_FadeCB, 0);
        }
        g_ctx->context->PSSetConstantBuffers(0, 1, &g_FadeCB);
        g_ctx->context->Draw(3, 0);
    }

    // 3. BRUSH PASS (Additive)
    g_ctx->context->OMSetRenderTargets(1, &g_MaskRTV, nullptr);
    g_ctx->context->OMSetBlendState(g_BlendAdditive, blendFactor, 0xffffffff);
    g_ctx->context->PSSetShader(g_BrushPS, nullptr, 0);
    
    static float lastRenderedX = -1000.0f;
    static float lastRenderedY = -1000.0f;
    static auto lastMouseMoveTime = std::chrono::steady_clock::now();
    
    if (!g_mousePoints.empty()) {
        lastMouseMoveTime = std::chrono::steady_clock::now();
    }
    float stationarySeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - lastMouseMoveTime).count();
    bool isResting = (stationarySeconds > 0.05f);

    for (const auto& p : g_mousePoints) {
        g_historyPoints.push_back({p.first, p.second, g_currentTime});
    }
    g_mousePoints.clear();

    // Remove points older than trailLength
    while (!g_historyPoints.empty() && (g_currentTime - g_historyPoints.front().timeAdded) > g_settings.trailLength) {
        g_historyPoints.pop_front();
    }

    std::vector<std::pair<float, float>> strokePoints;
    for (const auto& hp : g_historyPoints) {
        strokePoints.push_back({hp.x, hp.y});
    }

    if (!g_historyPoints.empty()) {
        lastRenderedX = g_historyPoints.back().x;
        lastRenderedY = g_historyPoints.back().y;
    }

    if (!g_settings.fadeWhenResting && lastRenderedX > -999.0f) {
        strokePoints.push_back({lastRenderedX, lastRenderedY});
    }

    if (strokePoints.size() == 1) {
        strokePoints.push_back(strokePoints[0]); // single point becomes a zero-length segment
    }

    if (!strokePoints.empty()) {
        int pointsLeft = strokePoints.size();
        int offset = 0;
        float scaleY = (float)g_ctx->processingHeight / GetSystemMetrics(SM_CYSCREEN);

        while (pointsLeft > 1) {
            int pointsToDraw = (std::min)(pointsLeft, 16);
            
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(g_ctx->context->Map(g_BrushCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                BrushConstantBuffer* bc = (BrushConstantBuffer*)mapped.pData;
                memset(bc, 0, sizeof(BrushConstantBuffer));
                
                bc->numPoints = pointsToDraw;
                for (int i = 0; i < pointsToDraw; ++i) {
                    bc->packedPoints[i * 2] = strokePoints[offset + i].first;
                    bc->packedPoints[i * 2 + 1] = strokePoints[offset + i].second;
                }
                bc->brushSize = g_settings.brushSize * scaleY;
                bc->brushHardness = g_settings.brushHardness;
                
                g_ctx->context->Unmap(g_BrushCB, 0);
            }
            g_ctx->context->PSSetConstantBuffers(0, 1, &g_BrushCB);
            
            if (g_settings.fadeWhenResting && isResting) {
                // Do not draw, let it fade out
            } else {
                g_ctx->context->Draw(3, 0);
            }

            offset += (pointsToDraw - 1);
            pointsLeft -= (pointsToDraw - 1);
        }
    }

    // 4. COMPOSITE PASS (Opaque to Screen)
    if (g_ctx->mainRenderTargetView) {
        // Change viewport to native screen resolution for final output
        D3D11_VIEWPORT screenVp = {};
        screenVp.Width = (float)g_ctx->screenWidth;
        screenVp.Height = (float)g_ctx->screenHeight;
        screenVp.MinDepth = 0.0f;
        screenVp.MaxDepth = 1.0f;
        g_ctx->context->RSSetViewports(1, &screenVp);

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
    if (g_FullscreenVS) { g_FullscreenVS->Release(); g_FullscreenVS = nullptr; }
    if (g_CompositePS) { g_CompositePS->Release(); g_CompositePS = nullptr; }
    if (g_BrushPS) { g_BrushPS->Release(); g_BrushPS = nullptr; }
    if (g_FadePS) { g_FadePS->Release(); g_FadePS = nullptr; }
    if (g_MaskTexture) { g_MaskTexture->Release(); g_MaskTexture = nullptr; }
    if (g_MaskRTV) { g_MaskRTV->Release(); g_MaskRTV = nullptr; }
    if (g_MaskSRV) { g_MaskSRV->Release(); g_MaskSRV = nullptr; }
    if (g_TexA) { g_TexA->Release(); g_TexA = nullptr; }
    if (g_SRVA) { g_SRVA->Release(); g_SRVA = nullptr; }
    if (g_TexB) { g_TexB->Release(); g_TexB = nullptr; }
    if (g_SRVB) { g_SRVB->Release(); g_SRVB = nullptr; }
    if (g_Sampler) { g_Sampler->Release(); g_Sampler = nullptr; }
    if (g_BlendAdditive) { g_BlendAdditive->Release(); g_BlendAdditive = nullptr; }
    if (g_BlendSubtractive) { g_BlendSubtractive->Release(); g_BlendSubtractive = nullptr; }
    if (g_BlendOpaque) { g_BlendOpaque->Release(); g_BlendOpaque = nullptr; }
    if (g_BrushCB) { g_BrushCB->Release(); g_BrushCB = nullptr; }
    if (g_FadeCB) { g_FadeCB->Release(); g_FadeCB = nullptr; }
    
    g_mousePoints.clear();
    g_historyPoints.clear();
    g_currentTime = 0.0f;
    
    // Explicit safety net: Ensure these slots are completely cleared from the GPU
    if (g_ctx && g_ctx->context) {
        ID3D11ShaderResourceView* nullSRVs[8] = {nullptr};
        g_ctx->context->PSSetShaderResources(0, 8, nullSRVs);
    }
    
    std::cout << "[CursorReveal] Shutdown complete.\n";
}

void CR_OnMouseMove(int x, int y) {
    if (!g_ctx) return;
    float scaleX = (float)g_ctx->processingWidth / GetSystemMetrics(SM_CXSCREEN);
    float scaleY = (float)g_ctx->processingHeight / GetSystemMetrics(SM_CYSCREEN);
    
    float mappedX = x * scaleX;
    float mappedY = y * scaleY;
    
    g_mousePoints.push_back({mappedX, mappedY});
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

void CR_OnSettingChanged(const char* key, float value) {
    std::string k(key);
    if (k == "brushSize") g_settings.brushSize = value;
    else if (k == "brushHardness") g_settings.brushHardness = value;
    else if (k == "fadeSpeed") g_settings.fadeSpeed = value;
    else if (k == "trailLength") g_settings.trailLength = value;
    else if (k == "fadeWhenResting") g_settings.fadeWhenResting = (value > 0.5f);
}

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
    CR_SaveSettings,
    CR_OnSettingChanged
};

extern "C" __declspec(dllexport) IEffectPlugin* CreateEffectPlugin() {
    return &g_plugin;
}
