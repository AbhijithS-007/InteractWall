#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RendererContext {
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    IDXGISwapChain1* swapChain;
    ID3D11RenderTargetView* mainRenderTargetView;
    int processingWidth;
    int processingHeight;
} RendererContext;

// Mock structs
typedef struct WallpaperLayers {
    const char* imagePathA;
    const char* imagePathB;
} WallpaperLayers;
typedef struct MonitorInfo MonitorInfo;
typedef struct QualityTier QualityTier;

typedef struct IEffectPlugin {
    void (*Initialize)(RendererContext* ctx);
    void (*Update)(float deltaTime);
    void (*Render)();
    void (*Shutdown)();
    void (*OnMouseMove)(int x, int y);
    void (*OnWallpaperChanged)(const WallpaperLayers* layers);
    void (*OnMonitorChanged)(const MonitorInfo* info);
    void (*OnQualityTierChanged)(const QualityTier* tier);
    void (*LoadSettings)(const char* jsonPath);
    void (*SaveSettings)(const char* jsonPath);
} IEffectPlugin;

typedef IEffectPlugin* (*PFN_CreateEffectPlugin)();

#ifdef __cplusplus
}
#endif
