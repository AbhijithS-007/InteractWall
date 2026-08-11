#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <iostream>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include "PluginLoader.h"
#include "../power/PowerManager.h"
#include "../quality/QualityManager.h"
#include "../ipc/IPCServer.h"
#include <wtsapi32.h>
#pragma comment(lib, "Wtsapi32.lib")
#pragma comment(lib, "PowrProf.lib")

// Global variables for D3D
ID3D11Device*            g_pd3dDevice           = nullptr;
ID3D11DeviceContext*     g_pd3dDeviceContext     = nullptr;
IDXGISwapChain1*         g_pSwapChain           = nullptr;
ID3D11RenderTargetView*  g_mainRenderTargetView  = nullptr;

HWND g_hwnd    = nullptr;
HWND g_workerw = nullptr;

PluginLoader g_pluginLoader;
RendererContext g_pluginContext = {};

static std::string g_lastLayerA = "";
static std::string g_lastLayerB = "";

// Timer state
auto     g_lastFrameTime = std::chrono::high_resolution_clock::now();
uint64_t g_frameCount    = 0;

// ---------------------------------------------------------------
// Console
// ---------------------------------------------------------------
void InitConsole() {
#ifdef _DEBUG
    AllocConsole();
    FILE* dummy;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);
    std::cout << "[Core] Console initialized.\n";
#endif
}

// ---------------------------------------------------------------
// WorkerW helpers
// ---------------------------------------------------------------
struct WorkerWSearch {
    HWND result = nullptr;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    // Is there a SHELLDLL_DefView child of this window?
    HWND defView = FindWindowEx(hwnd, nullptr, "SHELLDLL_DefView", nullptr);
    if (defView != nullptr) {
        std::cout << "Found SHELLDLL_DefView inside HWND: 0x" << std::hex << reinterpret_cast<uintptr_t>(hwnd) << std::dec << "\n";
        WorkerWSearch* search = reinterpret_cast<WorkerWSearch*>(lParam);
        // The WorkerW we want is the NEXT sibling after hwnd, not a child.
        search->result = FindWindowEx(nullptr, hwnd, "WorkerW", nullptr);
        std::cout << "Next WorkerW sibling: 0x" << std::hex << reinterpret_cast<uintptr_t>(search->result) << std::dec << "\n";
        return FALSE; // Stop enumeration
    }
    return TRUE;
}

HWND GetWorkerW() {
    HWND progman = FindWindow("Progman", nullptr);
    if (!progman) {
        std::cout << "Progman not found!\n";
        return nullptr;
    }
    // Send the undocumented 0x052C message to Progman to spawn a WorkerW
    SendMessageTimeout(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);
    
    // Windows 11 additional fallback messages
    SendMessageTimeout(progman, 0x052C, 0x0000000D, 0, SMTO_NORMAL, 1000, nullptr);
    SendMessageTimeout(progman, 0x052C, 0x0000000D, 1, SMTO_NORMAL, 1000, nullptr);

    WorkerWSearch search;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&search));
    
    // Fallback: If we couldn't find the sibling WorkerW, just use Progman.
    // Progman draws the wallpaper. Attaching to it guarantees we render on top of the wallpaper but behind the icons.
    if (!search.result) {
        std::cout << "Standard WorkerW sibling not found. Using Progman as fallback.\n";
        search.result = progman;
    }

    return search.result;
}

// ---------------------------------------------------------------
// D3D helpers
// ---------------------------------------------------------------
void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer));
    if (SUCCEEDED(hr) && pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
    // Update the global plugin context so plugins see the new RTV (especially after resize)
    g_pluginContext.mainRenderTargetView = g_mainRenderTargetView;
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}
#include <fstream>
std::string ReadPreferredGPU() {
    char appData[MAX_PATH];
    if (GetEnvironmentVariableA("APPDATA", appData, MAX_PATH) > 0) {
        std::string path = std::string(appData) + "\\InteractWall\\renderer.cfg";
        std::ifstream file(path);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (line.rfind("preferredGPU=", 0) == 0) {
                    return line.substr(13);
                }
            }
        }
    }
    return "";
}

bool InitD3D(HWND hwnd, int width, int height) {
    // ---- Feature levels ----
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL featureLevel;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    std::string preferredGPU = ReadPreferredGPU();
    IDXGIAdapter1* selectedAdapter = nullptr;

    IDXGIFactory1* pFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory))) {
        IDXGIAdapter1* pAdapter = nullptr;
        for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            pAdapter->GetDesc1(&desc);
            char adapterName[128] = {};
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, adapterName, sizeof(adapterName), nullptr, nullptr);
            if (preferredGPU == adapterName) {
                selectedAdapter = pAdapter;
                break; // Keep reference
            }
            pAdapter->Release();
        }
        pFactory->Release();
    }

    D3D_DRIVER_TYPE driverType = selectedAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;

    HRESULT hr = D3D11CreateDevice(
        selectedAdapter,
        driverType,
        nullptr,
        createDeviceFlags,
        featureLevels, ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &g_pd3dDevice,
        &featureLevel,
        &g_pd3dDeviceContext
    );
    
    if (selectedAdapter) {
        selectedAdapter->Release();
    }

    if (FAILED(hr)) {
        std::cout << "D3D11CreateDevice failed (HRESULT 0x" << std::hex << hr << std::dec << ")\n";
        return false;
    }

    std::cout << "D3D11 Device created. Feature Level: 0x"
              << std::hex << featureLevel << std::dec << "\n";

    // ---- Enumerate adapter for VRAM / name ----
    IDXGIDevice* dxgiDevice = nullptr;
    hr = g_pd3dDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
    if (FAILED(hr)) { std::cout << "QueryInterface IDXGIDevice failed\n"; return false; }

    IDXGIAdapter* dxgiAdapterBase = nullptr;
    hr = dxgiDevice->GetAdapter(&dxgiAdapterBase);
    dxgiDevice->Release();
    if (FAILED(hr)) { std::cout << "GetAdapter failed\n"; return false; }

    IDXGIAdapter1* dxgiAdapter = nullptr;
    hr = dxgiAdapterBase->QueryInterface(__uuidof(IDXGIAdapter1), reinterpret_cast<void**>(&dxgiAdapter));
    if (SUCCEEDED(hr) && dxgiAdapter) {
        DXGI_ADAPTER_DESC1 desc = {};
        if (SUCCEEDED(dxgiAdapter->GetDesc1(&desc))) {
            // Dedicated VRAM — SIZE_T, cast to uint64_t to avoid overflow on large cards.
            uint64_t vramMB = static_cast<uint64_t>(desc.DedicatedVideoMemory) / (1024ULL * 1024ULL);
            // Print adapter name as narrow string via WideCharToMultiByte.
            char adapterName[128] = {};
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, adapterName, sizeof(adapterName), nullptr, nullptr);
            std::cout << "Adapter: " << adapterName << "\n";
            std::cout << "Dedicated VRAM: " << vramMB << " MB\n";
            QualityManager::Initialize(vramMB, adapterName);
            QualityManager::SetQualityTierOverride(QUALITY_TIER_HIGH); // Force maximum quality
        }
        dxgiAdapter->Release();
    }

    // ---- Get the IDXGIFactory2 to create the swap chain ----
    IDXGIFactory2* dxgiFactory = nullptr;
    hr = dxgiAdapterBase->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory));
    dxgiAdapterBase->Release();
    if (FAILED(hr)) { std::cout << "GetParent IDXGIFactory2 failed\n"; return false; }

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width              = static_cast<UINT>(width);
    sd.Height             = static_cast<UINT>(height);
    sd.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.Stereo             = FALSE;
    sd.SampleDesc.Count   = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount        = 2;
    sd.Scaling            = DXGI_SCALING_STRETCH;
    sd.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode          = DXGI_ALPHA_MODE_UNSPECIFIED;

    hr = dxgiFactory->CreateSwapChainForHwnd(g_pd3dDevice, hwnd, &sd, nullptr, nullptr, &g_pSwapChain);

    // Prevent DXGI from handling Alt+Enter (would conflict with WorkerW parent).
    dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    dxgiFactory->Release();

    if (FAILED(hr)) {
        std::cout << "CreateSwapChainForHwnd failed (HRESULT 0x" << std::hex << hr << std::dec << ")\n";
        return false;
    }

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain)        { g_pSwapChain->Release();        g_pSwapChain        = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice)        { g_pd3dDevice->Release();        g_pd3dDevice        = nullptr; }
}

void ResetGPUState() {
    if (!g_pd3dDeviceContext) return;
    
    // Unbind Render Targets
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
    
    // Unbind Shader Resources
    ID3D11ShaderResourceView* nullSRVs[8] = {nullptr};
    g_pd3dDeviceContext->PSSetShaderResources(0, 8, nullSRVs);
    g_pd3dDeviceContext->VSSetShaderResources(0, 8, nullSRVs);
    
    // Unbind Constant Buffers
    ID3D11Buffer* nullCBs[8] = {nullptr};
    g_pd3dDeviceContext->PSSetConstantBuffers(0, 8, nullCBs);
    g_pd3dDeviceContext->VSSetConstantBuffers(0, 8, nullCBs);
    
    // Unbind Samplers
    ID3D11SamplerState* nullSamps[8] = {nullptr};
    g_pd3dDeviceContext->PSSetSamplers(0, 8, nullSamps);
    
    // Reset Blend State to opaque (null is default opaque)
    g_pd3dDeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);
}

// ---------------------------------------------------------------
// Render
// ---------------------------------------------------------------
void Render() {
    if (!g_mainRenderTargetView) return;

    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> frameTime = now - g_lastFrameTime;
    float deltaTime = frameTime.count();
    g_lastFrameTime = now;

    g_frameCount++;

    IEffectPlugin* plugin = g_pluginLoader.GetActivePlugin();
    if (!plugin) {
        if (IsWindowVisible(g_hwnd)) {
            ShowWindow(g_hwnd, SW_HIDE);
        }
        return; // Don't render anything if no plugin is active
    }

    if (!IsWindowVisible(g_hwnd)) {
        ShowWindow(g_hwnd, SW_SHOW);
    }

    // Bind the render target (required before clear / present have any effect).
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);

    if (plugin->Update) plugin->Update(deltaTime);
    if (plugin->Render) plugin->Render();

    g_pSwapChain->Present(1, 0); // VSync on
}

// ---------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        if (wParam == 2) {
            PowerManager::Update();
        }
        return 0;

    case WM_SIZE:
        // Guard both device AND swap chain (resize can arrive before InitD3D completes).
        if (g_pd3dDevice && g_pSwapChain && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(
                0,
                static_cast<UINT>(LOWORD(lParam)),
                static_cast<UINT>(HIWORD(lParam)),
                DXGI_FORMAT_UNKNOWN,
                0);
            CreateRenderTarget();
        }
        return 0;

    case WM_WTSSESSION_CHANGE:
        if (wParam == WTS_SESSION_LOCK) {
            std::cout << "[PowerManager] Session Locked - Suspending\n";
            PowerManager::SetSessionLocked(true);
        } else if (wParam == WTS_SESSION_UNLOCK) {
            std::cout << "[PowerManager] Session Unlocked - Resuming\n";
            PowerManager::SetSessionLocked(false);
        }
        return 0;

    case WM_POWERBROADCAST:
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
            std::cout << "[Core] System resumed from sleep. Re-attaching to WorkerW...\n";
            // Give Windows a moment to rebuild the desktop hierarchy after wakeup
            Sleep(1500);
            HWND newWorkerW = GetWorkerW();
            if (newWorkerW) {
                SetParent(g_hwnd, newWorkerW);
                g_workerw = newWorkerW;
                std::cout << "[Core] Re-parented to WorkerW: 0x" << std::hex
                          << reinterpret_cast<uintptr_t>(newWorkerW) << std::dec << "\n";
                // Force a resize/repaint to ensure DXGI surface is valid again
                RECT rc;
                GetClientRect(g_hwnd, &rc);
                PostMessage(g_hwnd, WM_SIZE, SIZE_RESTORED,
                    MAKELPARAM(rc.right - rc.left, rc.bottom - rc.top));
            } else {
                std::cout << "[Core] WARNING: Could not re-find WorkerW after resume!\n";
            }
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    // Initialize COM for WIC
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Make the application DPI aware so it gets true physical monitor resolution, not scaled resolution!
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    InitConsole();
    std::cout << "InteractWallRenderer Starting...\n";

    // ---- Find WorkerW ----
    HWND workerW = GetWorkerW();
    if (!workerW) {
        std::cout << "Failed to find WorkerW. Exiting.\n";
        MessageBox(nullptr, "Failed to find WorkerW.", "Initialization Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    std::cout << "WorkerW found! HWND: 0x" << std::hex << reinterpret_cast<uintptr_t>(workerW) << std::dec << "\n";

    // ---- Register window class ----
    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "InteractWallClass";

    if (!RegisterClassEx(&wc)) {
        std::cout << "RegisterClassEx failed.\n";
        MessageBox(nullptr, "RegisterClassEx failed.", "Initialization Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // ---- Primary monitor resolution ----
    int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    std::cout << "Screen: " << screenWidth << "x" << screenHeight << "\n";

    // ---- Create borderless popup window ----
    g_hwnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        "InteractWallRenderer",
        WS_POPUP | WS_VISIBLE,
        0, 0, screenWidth, screenHeight,
        nullptr,    // NO parent during creation to avoid cross-thread ownership locking
        nullptr,
        hInstance,
        nullptr
    );

    if (!g_hwnd) {
        std::cout << "CreateWindowEx failed (error " << GetLastError() << ").\n";
        MessageBox(nullptr, "CreateWindowEx failed.", "Initialization Error", MB_OK | MB_ICONERROR);
        UnregisterClass(wc.lpszClassName, hInstance);
        return 1;
    }

    SetParent(g_hwnd, workerW);
    std::cout << "Renderer HWND: 0x" << std::hex << reinterpret_cast<uintptr_t>(g_hwnd) << std::dec << "\n";

    // Register for session lock/unlock notifications
    WTSRegisterSessionNotification(g_hwnd, NOTIFY_FOR_THIS_SESSION);

    // ---- Initialize Direct3D ----
    if (!InitD3D(g_hwnd, screenWidth, screenHeight)) {
        MessageBox(nullptr, "Failed to initialize Direct3D.", "Initialization Error", MB_OK | MB_ICONERROR);
        CleanupDeviceD3D();
        DestroyWindow(g_hwnd);
        UnregisterClass(wc.lpszClassName, hInstance);
        return 1;
    }

    // ---- Setup Plugin System ----
    int sourceResWidth = 3840;
    int sourceResHeight = 2160;
    
    int processingWidth = std::min({ 2560, screenWidth, sourceResWidth });
    int processingHeight = std::min({ 1440, screenHeight, sourceResHeight });

    if (QualityManager::GetCurrentTier()->level == QUALITY_TIER_LOW) {
        processingWidth /= 2;
        processingHeight /= 2;
    }

    std::cout << "Computed Core Processing Resolution: " << processingWidth << "x" << processingHeight << "\n";

    g_pluginContext.device = g_pd3dDevice;
    g_pluginContext.context = g_pd3dDeviceContext;
    g_pluginContext.swapChain = g_pSwapChain;
    g_pluginContext.processingWidth = processingWidth;
    g_pluginContext.processingHeight = processingHeight;
    g_pluginContext.screenWidth = screenWidth;
    g_pluginContext.screenHeight = screenHeight;
    g_pluginContext.mainRenderTargetView = g_mainRenderTargetView;

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir = exePath;
    exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));
    
    std::string pluginsDir = exeDir + "\\plugins";

    g_pluginLoader.LoadAllPlugins(pluginsDir);
    // Note: We no longer initialize all plugins at startup to prevent state bleeding.
    // Initialization is deferred until a plugin becomes active via set_effect.
    
    // Set initial window state based on active plugin (null on startup)
    ShowWindow(g_hwnd, SW_HIDE);

    // ---- Setup Power Manager & timer ----
    PowerManager::Initialize(g_hwnd);
    IPCServer::Start();

    SetTimer(g_hwnd, 2, 500, nullptr);

    // ---- Message loop ----
    MSG msg = {};
    bool isRunning = true;
    while (isRunning) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                isRunning = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!isRunning) break;

        // Global Mouse Tracking
        POINT pt;
        if (GetCursorPos(&pt)) {
            static POINT lastPt = { -1, -1 };
            if (pt.x != lastPt.x || pt.y != lastPt.y) {
                lastPt = pt;
                PowerManager::OnMouseMove();
                if (IEffectPlugin* plugin = g_pluginLoader.GetActivePlugin()) {
                    if (plugin->OnMouseMove) {
                        plugin->OnMouseMove(pt.x, pt.y);
                    }
                }
            }
        }
        
        // (Removed 15-second heartbeat auto-exit to prevent premature exits during sleep or when UI is backgrounded)

        // Process IPC Commands
        auto cmds = IPCServer::GetPendingCommands();
        for (const auto& cmd : cmds) {
            if (cmd.cmd == "apply_wallpaper") {
                g_lastLayerA = cmd.strArg1;
                g_lastLayerB = cmd.strArg2;
                IEffectPlugin* plugin = g_pluginLoader.GetActivePlugin();
                if (plugin && plugin->OnWallpaperChanged) {
                    WallpaperLayers layers = { g_lastLayerA.c_str(), g_lastLayerB.c_str() };
                    plugin->OnWallpaperChanged(&layers);
                }
            } else if (cmd.cmd == "set_effect") {
                IEffectPlugin* oldPlugin = g_pluginLoader.GetActivePlugin();
                g_pluginLoader.SetActivePlugin(cmd.strArg1);
                IEffectPlugin* newPlugin = g_pluginLoader.GetActivePlugin();
                if (newPlugin && newPlugin != oldPlugin) {
                    if (oldPlugin && oldPlugin->Shutdown) {
                        oldPlugin->Shutdown();
                    }
                    ResetGPUState();
                    
                    if (newPlugin->Initialize) {
                        newPlugin->Initialize(&g_pluginContext);
                    }
                    if (newPlugin->OnWallpaperChanged && !g_lastLayerA.empty()) {
                        WallpaperLayers layers = { g_lastLayerA.c_str(), g_lastLayerB.c_str() };
                        newPlugin->OnWallpaperChanged(&layers);
                    }
                } else if (newPlugin && newPlugin == oldPlugin) {
                    if (newPlugin->OnWallpaperChanged && !g_lastLayerA.empty()) {
                        WallpaperLayers layers = { g_lastLayerA.c_str(), g_lastLayerB.c_str() };
                        newPlugin->OnWallpaperChanged(&layers);
                    }
                }
            } else if (cmd.cmd == "set_quality_tier") {
                if (cmd.strArg1 == "low") QualityManager::SetQualityTierOverride(QUALITY_TIER_LOW);
                else if (cmd.strArg1 == "balanced") QualityManager::SetQualityTierOverride(QUALITY_TIER_BALANCED);
                else if (cmd.strArg1 == "high") QualityManager::SetQualityTierOverride(QUALITY_TIER_HIGH);
            } else if (cmd.cmd == "set_setting") {
                if (cmd.strArg1 == "engine.idleTimeout") {
                    PowerManager::SetIdleTimeout(cmd.floatArg);
                    std::cout << "[main] Engine setting applied: idleTimeout = " << cmd.floatArg << "\n";
                } else if (cmd.strArg1 == "engine.pauseHidden") {
                    PowerManager::SetPauseHidden(cmd.floatArg > 0.5f);
                    std::cout << "[main] Engine setting applied: pauseHidden = " << (cmd.floatArg > 0.5f) << "\n";
                } else if (cmd.strArg1 == "engine.pauseFullscreen") {
                    PowerManager::SetPauseFullscreen(cmd.floatArg > 0.5f);
                    std::cout << "[main] Engine setting applied: pauseFullscreen = " << (cmd.floatArg > 0.5f) << "\n";
                } else if (cmd.strArg1 == "engine.pauseBattery") {
                    PowerManager::SetPauseBattery(cmd.floatArg > 0.5f);
                    std::cout << "[main] Engine setting applied: pauseBattery = " << (cmd.floatArg > 0.5f) << "\n";
                } else if (cmd.strArg1 == "engine.pauseSessionLocked") {
                    PowerManager::SetPauseSessionLocked(cmd.floatArg > 0.5f);
                    std::cout << "[main] Engine setting applied: pauseSessionLocked = " << (cmd.floatArg > 0.5f) << "\n";
                } else if (cmd.strArg1 == "engine.preferredGPU") {
                    char appData[MAX_PATH];
                    if (GetEnvironmentVariableA("APPDATA", appData, MAX_PATH) > 0) {
                        std::string path = std::string(appData) + "\\InteractWall\\renderer.cfg";
                        std::ofstream file(path);
                        if (file.is_open()) {
                            file << "preferredGPU=" << cmd.strArg2 << "\n";
                        }
                    }
                    std::cout << "[main] Engine setting applied: preferredGPU = " << cmd.strArg2 << " (Restarting...)\n";
                    PostQuitMessage(0);
                } else {
                    IEffectPlugin* plugin = g_pluginLoader.GetActivePlugin();
                    if (plugin && plugin->OnSettingChanged) {
                        plugin->OnSettingChanged(cmd.strArg1.c_str(), cmd.floatArg);
                    }
                }
            } else if (cmd.cmd == "remove_effect") {
                IEffectPlugin* oldPlugin = g_pluginLoader.GetActivePlugin();
                if (oldPlugin) {
                    if (oldPlugin->Shutdown) oldPlugin->Shutdown();
                    ResetGPUState();
                }
                g_pluginLoader.SetActivePlugin("");
            } else if (cmd.cmd == "quit") {
                std::cout << "[main] Received quit command. Exiting.\n";
                isRunning = false;
                break;
            }
        }

        int fpsCap = QualityManager::GetCurrentTier()->fpsCap;
        if (PowerManager::ShouldRenderFrame(fpsCap)) {
            auto beforeRender = std::chrono::high_resolution_clock::now();
            Render();
            auto afterRender = std::chrono::high_resolution_clock::now();
            float renderMs = std::chrono::duration<float, std::milli>(afterRender - beforeRender).count();
            
            // Safeguard against VSync failure or early returns (e.g. DXGI_STATUS_OCCLUDED)
            // If the frame took less than 1ms, VSync did not block. Sleep to prevent 100% CPU lockup.
            if (renderMs < 1.0f) {
                Sleep(1);
            }
            
            // Update IPC status periodically
            if (g_frameCount % 60 == 0) {
                StatusSnapshot snap;
                snap.fps = 60.0f; // Calculate real FPS later
                snap.cpu = 0.0f;
                snap.gpuMemMB = 0.0f;
                
                int stateEnum = PowerManager::GetState();
                stateEnum = std::max(0, std::min(stateEnum, 7));
                const char* states[] = {"VISIBLE_ACTIVE", "VISIBLE_IDLE_LOW_FPS", "IDLE_PAUSED", "HIDDEN_OCCLUDED", "HIDDEN_FULLSCREEN", "HIDDEN_BATTERY", "HIDDEN_SESSION_LOCKED", "HIDDEN_REMOTE_SESSION"};
                snap.state = states[stateEnum];
                
                int tierEnum = QualityManager::GetCurrentTier()->level;
                tierEnum = std::max(0, std::min(tierEnum, 2));
                const char* tiers[] = {"LOW", "BALANCED", "HIGH"};
                snap.tier = tiers[tierEnum];
                
                snap.activePlugin = g_pluginLoader.GetActivePluginName();
                
                IPCServer::UpdateStatus(snap);
            }
        } else {
            Sleep(1);
        }
    }

    WTSUnRegisterSessionNotification(g_hwnd);

    // ---- Cleanup ----
    IPCServer::Stop();
    CleanupDeviceD3D();
    DestroyWindow(g_hwnd);
    UnregisterClass(wc.lpszClassName, hInstance);

    std::cout << "InteractWallRenderer Exiting cleanly.\n";
    
    // Force Explorer to redraw the desktop wallpaper
    SystemParametersInfoA(SPI_SETDESKWALLPAPER, 0, nullptr, SPIF_SENDCHANGE);
    
    return 0;
}
