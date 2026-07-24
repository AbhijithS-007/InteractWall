#include "../interface/PluginAPI.h"
#include <iostream>
#include <cmath>

static RendererContext* g_ctx = nullptr;
static float g_hue = 0.0f;

// Helper to convert HSV to RGB
void HSVtoRGB(float h, float s, float v, float& r, float& g, float& b) {
    int i = (int)(h * 6);
    float f = h * 6 - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    switch (i % 6) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        case 5: r = v, g = p, b = q; break;
    }
}

void SC_Initialize(RendererContext* ctx) {
    g_ctx = ctx;
    std::cout << "[SolidColorTest] Initialized.\n";
    std::cout << "[SolidColorTest] Processing Resolution: " 
              << g_ctx->processingWidth << "x" << g_ctx->processingHeight << "\n";
}

void SC_Update(float deltaTime) {
    // Cycle hue over time
    g_hue += deltaTime * 0.1f; // Slow color change
    if (g_hue > 1.0f) {
        g_hue -= 1.0f;
    }
}

void SC_Render() {
    if (!g_ctx || !g_ctx->context) return;

    float r, g, b;
    HSVtoRGB(g_hue, 1.0f, 0.5f, r, g, b); // Use 0.5 value for a darker, nicer color

    const float clearColor[4] = { r, g, b, 1.0f };

    // Use the explicitly passed RTV
    if (g_ctx->mainRenderTargetView) {
        g_ctx->context->ClearRenderTargetView(g_ctx->mainRenderTargetView, clearColor);
    }
}

void SC_Shutdown() {
    std::cout << "[SolidColorTest] Shutdown.\n";
}

void SC_OnMouseMove(int x, int y) {}
void SC_OnWallpaperChanged(const WallpaperLayers* layers) {}
void SC_OnMonitorChanged(const MonitorInfo* info) {}
void SC_OnQualityTierChanged(const QualityTier* tier) {}
void SC_LoadSettings(const char* jsonPath) {}
void SC_SaveSettings(const char* jsonPath) {}

// Global instance of the plugin interface
static IEffectPlugin g_plugin = {
    SC_Initialize,
    SC_Update,
    SC_Render,
    SC_Shutdown,
    SC_OnMouseMove,
    SC_OnWallpaperChanged,
    SC_OnMonitorChanged,
    SC_OnQualityTierChanged,
    SC_LoadSettings,
    SC_SaveSettings
};

extern "C" __declspec(dllexport) IEffectPlugin* CreateEffectPlugin() {
    return &g_plugin;
}
