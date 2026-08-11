#include "../interface/PluginAPI.h"
#include "../cursor_reveal/WICLoader.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <random>

static RendererContext* g_ctx = nullptr;

struct Settings {
    float wipeWidth = 120.0f;
    float wipeRoughness = 0.15f;
    float fogDensity = 0.9f;
    float rainDensity = 1.0f; 
    float rainSize = 1.0f;
    float regrowthSpeed = 0.02f;
    float dripIntensity = 1.0f;
} g_settings;

struct Droplet {
    float x, y;
    float radius;
    float velocityY;
    float clingTimer;
    float trailLength;
    float age;
    bool active;
    float seed;
    float stickTimer;
    float slipBurst;
};
static std::vector<Droplet> g_droplets;
static int g_dropletCap = 60;
static int g_qualityTier = 2;
static bool g_trailsEnabled = true;
static bool g_mergingEnabled = true;

static ID3D11VertexShader* g_FullscreenVS = nullptr;
static ID3D11PixelShader*  g_CompositePS = nullptr;
static ID3D11PixelShader*  g_BrushPS = nullptr;
static ID3D11PixelShader*  g_RegrowthPS = nullptr;
static ID3D11VertexShader* g_DropletVS = nullptr;
static ID3D11PixelShader*  g_DropletPS = nullptr;
static ID3D11InputLayout*  g_DropletLayout = nullptr;

static ID3D11Texture2D*          g_MaskTexture = nullptr;
static ID3D11RenderTargetView*   g_MaskRTV = nullptr;
static ID3D11ShaderResourceView* g_MaskSRV = nullptr;

static ID3D11Texture2D*          g_MaskPingPongTex = nullptr;
static ID3D11RenderTargetView*   g_MaskPingPongRTV = nullptr;
static ID3D11ShaderResourceView* g_MaskPingPongSRV = nullptr;

static ID3D11Texture2D*          g_TexBase = nullptr;
static ID3D11ShaderResourceView* g_SRVBase = nullptr;

static ID3D11SamplerState*       g_Sampler = nullptr;
static ID3D11BlendState*         g_BlendOpaque = nullptr;
static ID3D11BlendState*         g_BlendAlpha = nullptr;

static ID3D11Buffer* g_BrushCB = nullptr;
static ID3D11Buffer* g_RegrowthCB = nullptr;
static ID3D11Buffer* g_DropletCB = nullptr;
static ID3D11Buffer* g_DropletInstanceBuffer = nullptr;

static std::vector<std::pair<float, float>> g_mousePoints;
static float g_time = 0.0f;

__declspec(align(16))
struct BrushConstantBuffer {
    float packedPoints[32];
    int numPoints;
    float brushSize;
    float wipeRoughness;
    float time;
};

__declspec(align(16))
struct RegrowthConstantBuffer {
    float regrowthSpeed;
    float time;
    float padding[2];
};

__declspec(align(16))
struct DropletConstantBuffer {
    float screenWidth;
    float screenHeight;
    int qualityTier;
    float padding;
};

struct DropletInstance {
    float x, y;
    float radius;
    float alpha;
    float trailLength;
    float seed;
};

std::vector<char> ReadFileContent(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) return {};
    size_t size = (size_t)file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    file.read(buffer.data(), size);
    return buffer;
}

void SpawnDroplet(bool randomY = false, float x = -1.0f, float y = -1.0f, float fallSpeed = 0.0f) {
    for (auto& drop : g_droplets) {
        if (!drop.active) {
            drop.active = true;
            
            float r = (rand() % 1000) / 1000.0f;
            float radiusScale = -log(1.0f - r); 
            drop.radius = max(1.5f, min(radiusScale * 2.0f, 6.0f)) * g_settings.rainSize;
            
            float clusterX = (rand() % 10) / 10.0f;
            float clusterY = (rand() % 10) / 10.0f;
            float jitterX = ((rand() % 1000) / 1000.0f - 0.5f) * 0.3f;
            float jitterY = ((rand() % 1000) / 1000.0f - 0.5f) * 0.3f;
            
            drop.x = (x >= 0.0f) ? x : max(0.0f, min(clusterX + jitterX, 1.0f));
            drop.y = (y >= 0.0f) ? y : (randomY ? max(-0.1f, min(clusterY + jitterY, 1.0f)) : -0.05f);
            
            drop.seed = (rand() % 1000) / 1000.0f;
            drop.velocityY = fallSpeed;
            drop.clingTimer = (fallSpeed > 0.0f) ? 0.0f : 0.5f + (rand() % 200) / 100.0f;
            drop.trailLength = 0.0f;
            drop.age = 0.0f;
            drop.stickTimer = 0.0f;
            drop.slipBurst = 0.0f;
            break;
        }
    }
}

void SetupMaskTargets(int w, int h) {
    if (g_MaskTexture) g_MaskTexture->Release();
    if (g_MaskRTV) g_MaskRTV->Release();
    if (g_MaskSRV) g_MaskSRV->Release();
    if (g_MaskPingPongTex) g_MaskPingPongTex->Release();
    if (g_MaskPingPongRTV) g_MaskPingPongRTV->Release();
    if (g_MaskPingPongSRV) g_MaskPingPongSRV->Release();

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    
    g_ctx->device->CreateTexture2D(&td, nullptr, &g_MaskTexture);
    g_ctx->device->CreateRenderTargetView(g_MaskTexture, nullptr, &g_MaskRTV);
    g_ctx->device->CreateShaderResourceView(g_MaskTexture, nullptr, &g_MaskSRV);
    
    g_ctx->device->CreateTexture2D(&td, nullptr, &g_MaskPingPongTex);
    g_ctx->device->CreateRenderTargetView(g_MaskPingPongTex, nullptr, &g_MaskPingPongRTV);
    g_ctx->device->CreateShaderResourceView(g_MaskPingPongTex, nullptr, &g_MaskPingPongSRV);

    float clearColor[4] = {0,0,0,0};
    g_ctx->context->ClearRenderTargetView(g_MaskRTV, clearColor);
    g_ctx->context->ClearRenderTargetView(g_MaskPingPongRTV, clearColor);
}

void WW_Initialize(RendererContext* ctx) {
    g_ctx = ctx;
    std::cout << "[WindowWipe] Initializing...\n";

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir = exePath;
    exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));
    std::string pluginDir = exeDir + "\\plugins\\";

    auto vsData = ReadFileContent(pluginDir + "FullscreenVS.cso");
    auto compData = ReadFileContent(pluginDir + "CompositePS.cso");
    auto brushData = ReadFileContent(pluginDir + "WipeBrushPS.cso");
    auto regrowthData = ReadFileContent(pluginDir + "RegrowthPS.cso");
    auto dropVsData = ReadFileContent(pluginDir + "DropletVS.cso");
    auto dropPsData = ReadFileContent(pluginDir + "DropletPS.cso");

    if (vsData.empty() || compData.empty() || brushData.empty() || regrowthData.empty() || dropVsData.empty() || dropPsData.empty()) {
        std::cout << "[WindowWipe] Failed to load shaders.\n";
        return;
    }

    g_ctx->device->CreateVertexShader(vsData.data(), vsData.size(), nullptr, &g_FullscreenVS);
    g_ctx->device->CreatePixelShader(compData.data(), compData.size(), nullptr, &g_CompositePS);
    g_ctx->device->CreatePixelShader(brushData.data(), brushData.size(), nullptr, &g_BrushPS);
    g_ctx->device->CreatePixelShader(regrowthData.data(), regrowthData.size(), nullptr, &g_RegrowthPS);
    g_ctx->device->CreateVertexShader(dropVsData.data(), dropVsData.size(), nullptr, &g_DropletVS);
    g_ctx->device->CreatePixelShader(dropPsData.data(), dropPsData.size(), nullptr, &g_DropletPS);

    D3D11_INPUT_ELEMENT_DESC instLayout[] = {
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 8, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "COLOR", 0, DXGI_FORMAT_R32_FLOAT, 0, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "COLOR", 1, DXGI_FORMAT_R32_FLOAT, 0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "COLOR", 2, DXGI_FORMAT_R32_FLOAT, 0, 20, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
    };
    g_ctx->device->CreateInputLayout(instLayout, 5, dropVsData.data(), dropVsData.size(), &g_DropletLayout);

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    g_ctx->device->CreateSamplerState(&sd, &g_Sampler);

    D3D11_BUFFER_DESC cbd = {};
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.ByteWidth = sizeof(BrushConstantBuffer);
    g_ctx->device->CreateBuffer(&cbd, nullptr, &g_BrushCB);
    cbd.ByteWidth = sizeof(RegrowthConstantBuffer);
    g_ctx->device->CreateBuffer(&cbd, nullptr, &g_RegrowthCB);
    cbd.ByteWidth = sizeof(DropletConstantBuffer);
    g_ctx->device->CreateBuffer(&cbd, nullptr, &g_DropletCB);
    
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbd.ByteWidth = sizeof(DropletInstance) * 200;
    g_ctx->device->CreateBuffer(&cbd, nullptr, &g_DropletInstanceBuffer);

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_ctx->device->CreateBlendState(&bd, &g_BlendOpaque);

    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    g_ctx->device->CreateBlendState(&bd, &g_BlendAlpha);

    g_droplets.resize(200);
    for (int i=0; i<60; i++) SpawnDroplet(true);
    SetupMaskTargets(g_ctx->processingWidth, g_ctx->processingHeight);
}

void WW_Update(float deltaTime) {
    if (deltaTime == 0.0f) return;
    g_time += deltaTime;

    int activeCount = 0;
    for (auto& drop : g_droplets) {
        if (!drop.active) continue;
        activeCount++;
        drop.age += deltaTime;
        
        if (drop.clingTimer > 0.0f) {
            drop.clingTimer -= deltaTime;
            if (drop.clingTimer <= 0.0f) {
                drop.slipBurst = 150.0f + (rand() % 200) + drop.radius * 30.0f;
            }
        } else {
            if (drop.stickTimer > 0.0f) {
                drop.stickTimer -= deltaTime;
                drop.velocityY = max(drop.velocityY - 400.0f * deltaTime, 0.0f);
            } else {
                drop.velocityY += drop.slipBurst * deltaTime;
                if (drop.velocityY > drop.slipBurst) drop.velocityY = drop.slipBurst;
                
                if ((rand() % 100) < 5) {
                    drop.stickTimer = 0.1f + (rand() % 400) / 1000.0f; 
                    drop.slipBurst = 150.0f + (rand() % 200) + drop.radius * 30.0f;
                }
            }
            
            float traveled = (drop.velocityY / g_ctx->processingHeight) * deltaTime;
            drop.y += traveled;
            if (g_trailsEnabled) {
                drop.trailLength += traveled;
                drop.trailLength -= 0.15f * deltaTime; 
                drop.trailLength = max(0.0f, min(drop.trailLength, 0.15f));
            }
        }

        if (drop.y > 1.1f || drop.age > 20.0f) {
            drop.active = false;
        }
    }

    int toSpawn = g_dropletCap - activeCount;
    if (toSpawn > 0) {
        int spawnCount = (toSpawn > 10) ? 2 : ((rand() % 100) < 15 ? 1 : 0);
        for (int i = 0; i < spawnCount; i++) {
            SpawnDroplet();
        }
    }

    static float debugTimer = 0.0f;
    debugTimer += deltaTime;
    if (debugTimer > 1.0f) {
        debugTimer = 0.0f;
        std::cout << "[Developer Mode] dropletPoolSize=" << activeCount 
                  << " targetPoolSize=" << g_dropletCap 
                  << " refraction=ON meander=" << (g_qualityTier > 0 ? "ON" : "OFF(Low Tier)") 
                  << " stickSlip=ON trails=ON\n";
    }

    if (g_mergingEnabled) {
        for (size_t i=0; i<g_droplets.size(); i++) {
            if (!g_droplets[i].active || g_droplets[i].velocityY > 0.0f) continue;
            for (size_t j=0; j<g_droplets.size(); j++) {
                if (i == j || !g_droplets[j].active || g_droplets[j].velocityY == 0.0f) continue;
                float dx = (g_droplets[i].x - g_droplets[j].x) * g_ctx->processingWidth;
                float dy = (g_droplets[i].y - g_droplets[j].y) * g_ctx->processingHeight;
                float dist = sqrt(dx*dx + dy*dy);
                if (dist < g_droplets[i].radius + g_droplets[j].radius) {
                    g_droplets[j].radius += g_droplets[i].radius * 0.2f;
                    g_droplets[i].active = false;
                    break;
                }
            }
        }
    }
}

void WW_Render() {
    if (!g_SRVBase) return;

    g_ctx->context->OMSetRenderTargets(1, &g_MaskPingPongRTV, nullptr);
    g_ctx->context->OMSetBlendState(g_BlendOpaque, nullptr, 0xFFFFFFFF);
    g_ctx->context->VSSetShader(g_FullscreenVS, nullptr, 0);
    g_ctx->context->PSSetShader(g_RegrowthPS, nullptr, 0);
    g_ctx->context->PSSetShaderResources(0, 1, &g_MaskSRV);
    g_ctx->context->PSSetSamplers(0, 1, &g_Sampler);
    
    RegrowthConstantBuffer rcb = {};
    rcb.regrowthSpeed = g_settings.regrowthSpeed;
    rcb.time = g_time;
    g_ctx->context->UpdateSubresource(g_RegrowthCB, 0, nullptr, &rcb, 0, 0);
    g_ctx->context->PSSetConstantBuffers(0, 1, &g_RegrowthCB);
    g_ctx->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    g_ctx->context->Draw(3, 0); // Regrowth pass
    
    ID3D11ShaderResourceView* nullSRV = nullptr;
    g_ctx->context->PSSetShaderResources(0, 1, &nullSRV);

    g_ctx->context->OMSetRenderTargets(1, &g_MaskRTV, nullptr);
    g_ctx->context->PSSetShader(g_BrushPS, nullptr, 0);
    g_ctx->context->PSSetShaderResources(0, 1, &g_MaskPingPongSRV);
    
    BrushConstantBuffer bcb = {};
    bcb.numPoints = (int)g_mousePoints.size();
    for (size_t i = 0; i < g_mousePoints.size() && i < 16; i++) {
        bcb.packedPoints[i/2 * 4 + (i%2)*2] = g_mousePoints[i].first;
        bcb.packedPoints[i/2 * 4 + (i%2)*2 + 1] = g_mousePoints[i].second;
    }
    bcb.brushSize = g_settings.wipeWidth;
    bcb.wipeRoughness = g_settings.wipeRoughness;
    bcb.time = g_time;
    g_ctx->context->UpdateSubresource(g_BrushCB, 0, nullptr, &bcb, 0, 0);
    g_ctx->context->PSSetConstantBuffers(0, 1, &g_BrushCB);
    g_ctx->context->Draw(3, 0); // Wipe mask pass
    g_ctx->context->PSSetShaderResources(0, 1, &nullSRV);

    g_mousePoints.clear();

    g_ctx->context->OMSetRenderTargets(1, &g_ctx->mainRenderTargetView, nullptr);
    g_ctx->context->PSSetShader(g_CompositePS, nullptr, 0);
    g_ctx->context->PSSetShaderResources(0, 1, &g_SRVBase);
    g_ctx->context->PSSetShaderResources(1, 1, &g_MaskSRV);
    g_ctx->context->Draw(3, 0); // Composite pass
    
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(g_ctx->context->Map(g_DropletInstanceBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        DropletInstance* instData = (DropletInstance*)mapped.pData;
        int drawn = 0;
        for (const auto& drop : g_droplets) {
            if (drop.active) {
                instData[drawn].x = drop.x;
                instData[drawn].y = drop.y;
                instData[drawn].radius = drop.radius;
                instData[drawn].alpha = 1.0f;
                instData[drawn].trailLength = drop.trailLength;
                instData[drawn].seed = drop.seed;
                drawn++;
            }
        }
        g_ctx->context->Unmap(g_DropletInstanceBuffer, 0);

        if (drawn > 0) {
            DropletConstantBuffer dcb = {};
            dcb.screenWidth = (float)g_ctx->processingWidth;
            dcb.screenHeight = (float)g_ctx->processingHeight;
            dcb.qualityTier = g_qualityTier;
            g_ctx->context->UpdateSubresource(g_DropletCB, 0, nullptr, &dcb, 0, 0);

            UINT stride = sizeof(DropletInstance);
            UINT offset = 0;
            g_ctx->context->IASetVertexBuffers(0, 1, &g_DropletInstanceBuffer, &stride, &offset);
            g_ctx->context->IASetInputLayout(g_DropletLayout);
            g_ctx->context->VSSetShader(g_DropletVS, nullptr, 0);
            g_ctx->context->VSSetConstantBuffers(0, 1, &g_DropletCB);
            g_ctx->context->PSSetShader(g_DropletPS, nullptr, 0);
            g_ctx->context->PSSetConstantBuffers(0, 1, &g_DropletCB);
            g_ctx->context->OMSetBlendState(g_BlendAlpha, nullptr, 0xFFFFFFFF);
            
            g_ctx->context->DrawInstanced(4, drawn, 0, 0); // Instanced droplet pass
        }
    }
}

void WW_Shutdown() {
    if (g_TexBase) g_TexBase->Release();
    if (g_SRVBase) g_SRVBase->Release();
    if (g_MaskTexture) g_MaskTexture->Release();
    if (g_MaskRTV) g_MaskRTV->Release();
    if (g_MaskSRV) g_MaskSRV->Release();
    if (g_MaskPingPongTex) g_MaskPingPongTex->Release();
    if (g_MaskPingPongRTV) g_MaskPingPongRTV->Release();
    if (g_MaskPingPongSRV) g_MaskPingPongSRV->Release();
    if (g_FullscreenVS) g_FullscreenVS->Release();
    if (g_CompositePS) g_CompositePS->Release();
    if (g_BrushPS) g_BrushPS->Release();
    if (g_RegrowthPS) g_RegrowthPS->Release();
    if (g_DropletVS) g_DropletVS->Release();
    if (g_DropletPS) g_DropletPS->Release();
    if (g_DropletLayout) g_DropletLayout->Release();
    if (g_BrushCB) g_BrushCB->Release();
    if (g_RegrowthCB) g_RegrowthCB->Release();
    if (g_DropletCB) g_DropletCB->Release();
    if (g_DropletInstanceBuffer) g_DropletInstanceBuffer->Release();
    if (g_Sampler) g_Sampler->Release();
    if (g_BlendOpaque) g_BlendOpaque->Release();
    if (g_BlendAlpha) g_BlendAlpha->Release();
}

void WW_OnMouseMove(int x, int y) {
    float nx = (float)x / g_ctx->screenWidth;
    float ny = (float)y / g_ctx->screenHeight;
    g_mousePoints.push_back({nx * g_ctx->processingWidth, ny * g_ctx->processingHeight});
}

void WW_OnWallpaperChanged(const WallpaperLayers* layers) {
    if (g_TexBase) { g_TexBase->Release(); g_TexBase = nullptr; }
    if (g_SRVBase) { g_SRVBase->Release(); g_SRVBase = nullptr; }
    
    if (layers && layers->imagePathA) {
        WICLoader::LoadTexture(g_ctx->device, layers->imagePathA, g_ctx->processingWidth, g_ctx->processingHeight, &g_TexBase, &g_SRVBase);
    }
}

void WW_OnMonitorChanged(const MonitorInfo* info) {}

void WW_OnQualityTierChanged(const QualityTier* tier) {
    g_qualityTier = tier->level;
    if (tier->level == QUALITY_TIER_LOW) {
        g_dropletCap = 60;
        g_trailsEnabled = true;
        g_mergingEnabled = false;
    } else if (tier->level == QUALITY_TIER_BALANCED) {
        g_dropletCap = 120;
        g_trailsEnabled = true;
        g_mergingEnabled = true;
    } else {
        g_dropletCap = 200;
        g_trailsEnabled = true;
        g_mergingEnabled = true;
    }
}

void WW_LoadSettings(const char* jsonPath) {}
void WW_SaveSettings(const char* jsonPath) {}

extern "C" __declspec(dllexport) IEffectPlugin* CreateEffectPlugin() {
    static IEffectPlugin plugin = {
        WW_Initialize,
        WW_Update,
        WW_Render,
        WW_Shutdown,
        WW_OnMouseMove,
        WW_OnWallpaperChanged,
        WW_OnMonitorChanged,
        WW_OnQualityTierChanged,
        WW_LoadSettings,
        WW_SaveSettings
    };
    return &plugin;
}
