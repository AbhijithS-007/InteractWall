#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <iostream>
#include <chrono>
#include <cstdint>
#include <algorithm>
#include "PluginLoader.h"

// Global variables for D3D
ID3D11Device*            g_pd3dDevice           = nullptr;
ID3D11DeviceContext*     g_pd3dDeviceContext     = nullptr;
IDXGISwapChain1*         g_pSwapChain           = nullptr;
ID3D11RenderTargetView*  g_mainRenderTargetView  = nullptr;

HWND g_hwnd    = nullptr;
HWND g_workerw = nullptr;

PluginLoader g_pluginLoader;
RendererContext g_pluginContext = {};

// Timer state
auto     g_lastFrameTime = std::chrono::high_resolution_clock::now();
uint64_t g_frameCount    = 0;

// ---------------------------------------------------------------
// Console
// ---------------------------------------------------------------
void InitConsole() {
    AllocConsole();
    FILE* dummy;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);
    // Sync C and C++ streams
    std::ios::sync_with_stdio(true);
    std::cout.clear();
    std::cerr.clear();
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

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevels, ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &g_pd3dDevice,
        &featureLevel,
        &g_pd3dDeviceContext
    );

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
    if (g_frameCount % 60 == 0) {
        std::cout << "Frame time: " << (deltaTime * 1000.0f) << " ms\n";
    }

    // Bind the render target (required before clear / present have any effect).
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);

    IEffectPlugin* plugin = g_pluginLoader.GetFirstPlugin();
    if (plugin) {
        if (plugin->Update) plugin->Update(deltaTime);
        if (plugin->Render) plugin->Render();
    } else {
        // Fallback clear
        const float clearColor[4] = { 0.0f, 0.5f, 0.5f, 1.0f };
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
    }

    g_pSwapChain->Present(1, 0); // VSync on
}

// ---------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        Render();
        return 0;

    case WM_MOUSEMOVE:
        if (IEffectPlugin* plugin = g_pluginLoader.GetFirstPlugin()) {
            if (plugin->OnMouseMove) {
                int x = static_cast<int>(LOWORD(lParam));
                int y = static_cast<int>(HIWORD(lParam));
                plugin->OnMouseMove(x, y);
            }
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

    // Explicitly reparent our window into Explorer's WorkerW layer.
    // This is the correct WorkerW hack (SetParent instead of CreateWindow parent).
    SetParent(g_hwnd, workerW);
    std::cout << "Renderer HWND: 0x" << std::hex << reinterpret_cast<uintptr_t>(g_hwnd) << std::dec << "\n";

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

    std::cout << "Computed Core Processing Resolution: " << processingWidth << "x" << processingHeight << "\n";

    g_pluginContext.device = g_pd3dDevice;
    g_pluginContext.context = g_pd3dDeviceContext;
    g_pluginContext.swapChain = g_pSwapChain;
    g_pluginContext.processingWidth = processingWidth;
    g_pluginContext.processingHeight = processingHeight;
    g_pluginContext.mainRenderTargetView = g_mainRenderTargetView;

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir = exePath;
    exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));
    std::string pluginsDir = exeDir + "\\plugins";

    g_pluginLoader.LoadAllPlugins(pluginsDir);
    IEffectPlugin* activePlugin = g_pluginLoader.GetFirstPlugin();
    if (activePlugin && activePlugin->Initialize) {
        activePlugin->Initialize(&g_pluginContext);
    }
    
    if (activePlugin && activePlugin->OnWallpaperChanged) {
        std::string pathA = exeDir + "\\..\\..\\..\\wallpapers\\Witcher.jpg";
        std::string pathB = exeDir + "\\..\\..\\..\\wallpapers\\frieren-magical.jpeg";
        WallpaperLayers layers = { pathA.c_str(), pathB.c_str() };
        activePlugin->OnWallpaperChanged(&layers);
    }

    // ---- Setup rendering timer ----
    // We use 17ms instead of 16ms. At 16ms (which is faster than 60Hz's 16.67ms),
    // the message queue never fully empties because a new timer message arrives 
    // before the VSync wait finishes. This makes Windows think the app is frozen 
    // on startup. 17ms guarantees the queue empties and clears the loading cursor.
    SetTimer(g_hwnd, 1, 17, nullptr);

    // ---- Message loop (blocking — no busy spin) ----
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // ---- Cleanup ----
    KillTimer(g_hwnd, 1);
    CleanupDeviceD3D();
    DestroyWindow(g_hwnd);
    UnregisterClass(wc.lpszClassName, hInstance);
    CoUninitialize();

    return static_cast<int>(msg.wParam);
}
