#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <string>
#include <cstdio>
#include <vector>
#include <fstream>
#include <sstream>
#include "../power/PowerManager.h"

using namespace Microsoft::WRL;

HWND g_hWnd = nullptr;
ComPtr<ICoreWebView2Controller> webviewController;
ComPtr<ICoreWebView2> webview;

// ---- Diagnostic globals (populated before WebView2 init, sent to page later) ----
static char g_diagJson[2048] = {0};

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

std::wstring GetBasePath() {
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring basePath = exePath;
    size_t lastSlash = basePath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        basePath = basePath.substr(0, lastSlash);
    }
    return basePath;
}

std::wstring GetConfigString() {
    char appData[MAX_PATH];
    size_t len;
    getenv_s(&len, appData, MAX_PATH, "APPDATA");
    std::string configPath = std::string(appData) + "\\Graffiti\\web_config.json";
    
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return L"{\"type\":\"config\", \"model\":\"\", \"backgroundType\":\"color\", \"backgroundColor\":\"#000000\"}";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    int wchars = MultiByteToWideChar(CP_UTF8, 0, content.c_str(), -1, NULL, 0);
    std::vector<wchar_t> wstr(wchars);
    MultiByteToWideChar(CP_UTF8, 0, content.c_str(), -1, &wstr[0], wchars);
    return std::wstring(wstr.data());
}

// ---------------------------------------------------------------
// WorkerW helpers
// ---------------------------------------------------------------
HWND g_defView = nullptr;

struct WorkerWSearch {
    HWND targetParent = nullptr;
    HWND siblingWorkerW = nullptr;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    HWND workerW = nullptr;
    EnumWindows([](HWND tophandle, LPARAM topparamhandle) -> BOOL {
        HWND p = FindWindowExA(tophandle, nullptr, "SHELLDLL_DefView", nullptr);
        if (p != nullptr) {
            *(HWND*)topparamhandle = FindWindowExA(nullptr, tophandle, "WorkerW", nullptr);
        }
        return TRUE;
    }, (LPARAM)&workerW);

    char className[256];
    GetClassNameA(hwnd, className, sizeof(className));

    HWND defView = FindWindowExA(hwnd, nullptr, "SHELLDLL_DefView", nullptr);
    if (defView != nullptr) {
        g_defView = defView; 
        WorkerWSearch* search = reinterpret_cast<WorkerWSearch*>(lParam);
        search->targetParent = hwnd;
        search->siblingWorkerW = FindWindowExA(nullptr, hwnd, "WorkerW", nullptr);
        return FALSE; 
    }
    return TRUE;
}

HWND FindWorkerW() {
    HWND progman = FindWindowA("Progman", nullptr);
    if (!progman) return nullptr;

    WorkerWSearch search;
    
    // First attempt: check if WorkerW already exists (e.g., from a previous effect run)
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&search));
    if (search.siblingWorkerW) return search.siblingWorkerW;

    // Only if not found, send the undocumented 0x052C message to Progman to spawn a WorkerW
    SendMessageTimeoutA(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);
    SendMessageTimeoutA(progman, 0x052C, 0x0000000D, 0, SMTO_NORMAL, 1000, nullptr);
    SendMessageTimeoutA(progman, 0x052C, 0x0000000D, 1, SMTO_NORMAL, 1000, nullptr);

    // Second attempt: wait for it to spawn
    for (int i = 0; i < 20; i++) {
        search.siblingWorkerW = nullptr;
        search.targetParent = nullptr;
        g_defView = nullptr;
        
        EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&search));

        if (search.siblingWorkerW) return search.siblingWorkerW;
        Sleep(50);
    }

    if (search.targetParent) {
        HWND childWorkerW = FindWindowExA(search.targetParent, nullptr, "WorkerW", nullptr);
        if (childWorkerW) return childWorkerW;
        return search.targetParent;
    }
    return progman;
}

#include <thread>

void MonitorParentProcess(DWORD parentPid) {
    HANDLE hParent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    if (hParent) {
        std::thread([hParent]() {
            WaitForSingleObject(hParent, INFINITE);
            CloseHandle(hParent);
            ExitProcess(0);
        }).detach();
    }
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    if (lpCmdLine && strlen(lpCmdLine) > 0) {
        DWORD parentPid = static_cast<DWORD>(atoi(lpCmdLine));
        if (parentPid > 0) {
            MonitorParentProcess(parentPid);
        }
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "WebWallpaperClass";

    RegisterClassExA(&wc);

    HWND workerW = FindWorkerW();
    if (!workerW) return 1;

    int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Create hidden popup first to avoid cross-thread locking and flashing
    g_hWnd = CreateWindowExA(0, wc.lpszClassName, "Web Wallpaper", WS_POPUP, 0, 0, screenWidth, screenHeight, nullptr, nullptr, hInstance, nullptr);
    if (!g_hWnd) return 1;

    PowerManager::Initialize(g_hWnd);

    // Safely migrate styles and reparent
    LONG style = GetWindowLongA(g_hWnd, GWL_STYLE);
    style &= ~WS_POPUP;
    style |= WS_CHILD;
    SetWindowLongA(g_hWnd, GWL_STYLE, style);
    SetWindowPos(g_hWnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);

    SetParent(g_hWnd, workerW);

    if (g_defView && GetParent(g_hWnd) == GetParent(g_defView)) {
        SetWindowPos(g_hWnd, g_defView, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        SetWindowPos(g_hWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    ShowWindow(g_hWnd, SW_SHOW);

    char appData[MAX_PATH];
    size_t len;
    getenv_s(&len, appData, MAX_PATH, "APPDATA");
    
    std::wstring userDataFolder = GetBasePath() + L"\\webview_data";
    std::string assetsPath = std::string(appData) + "\\Graffiti\\web_assets";
    
    int wchars = MultiByteToWideChar(CP_UTF8, 0, assetsPath.c_str(), -1, NULL, 0);
    std::vector<wchar_t> wstr(wchars);
    MultiByteToWideChar(CP_UTF8, 0, assetsPath.c_str(), -1, &wstr[0], wchars);
    static std::wstring s_assetsFolder = wstr.data();

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, userDataFolder.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result)) return result;
                env->CreateCoreWebView2Controller(g_hWnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result) || !controller) return result;

                            webviewController = controller;
                            webviewController->get_CoreWebView2(&webview);

                            ComPtr<ICoreWebView2Controller2> controller2;
                            webviewController.As(&controller2);
                            if (controller2) {
                                COREWEBVIEW2_COLOR transparent = { 0, 0, 0, 0 };
                                controller2->put_DefaultBackgroundColor(transparent);
                            }

                            RECT bounds;
                            GetClientRect(g_hWnd, &bounds);
                            webviewController->put_Bounds(bounds);
                            webviewController->put_IsVisible(TRUE);

                            ComPtr<ICoreWebView2_3> webview3;
                            webview.As(&webview3);
                            if (webview3) {
                                webview3->SetVirtualHostNameToFolderMapping(
                                    L"app-assets.local",
                                    s_assetsFolder.c_str(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW
                                );
                            }

                            webview->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                        // Send config on load
                                        std::wstring config = GetConfigString();
                                        webview->PostWebMessageAsJson(config.c_str());
                                        return S_OK;
                                    }).Get(), nullptr);

                            webview->Navigate(L"https://app-assets.local/index.html");

                            SetTimer(g_hWnd, 1, 16, nullptr); // Mouse tracking timer
                            SetTimer(g_hWnd, 2, 500, nullptr); // Power manager timer

                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    if (FAILED(hr)) return 1;

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static PowerState lastState = POWER_STATE_VISIBLE_ACTIVE;

    switch (message) {
    case WM_TIMER:
        if (wParam == 1 && webview) {
            POINT pt;
            if (GetCursorPos(&pt)) {
                int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
                int screenHeight = GetSystemMetrics(SM_CYSCREEN);

                float nx = (static_cast<float>(pt.x) / screenWidth) * 2.0f - 1.0f;
                float ny = (static_cast<float>(pt.y) / screenHeight) * 2.0f - 1.0f; 

                char json[128];
                snprintf(json, sizeof(json), "{\"type\":\"mousemove\", \"x\":%f, \"y\":%f}", nx, ny);

                int wchars_num = MultiByteToWideChar(CP_UTF8, 0, json, -1, NULL, 0);
                std::vector<wchar_t> wstr(wchars_num);
                MultiByteToWideChar(CP_UTF8, 0, json, -1, &wstr[0], wchars_num);
                webview->PostWebMessageAsJson(wstr.data());
            }
        }
        else if (wParam == 2 && webview) {
            PowerManager::Update();
            PowerState currentState = PowerManager::GetState();
            if (currentState != lastState) {
                lastState = currentState;
                if (currentState == POWER_STATE_HIDDEN_FULLSCREEN || 
                    currentState == POWER_STATE_HIDDEN_OCCLUDED || 
                    currentState == POWER_STATE_HIDDEN_BATTERY || 
                    currentState == POWER_STATE_HIDDEN_SESSION_LOCKED) {
                    webview->PostWebMessageAsJson(L"{\"type\":\"power\", \"action\":\"pause\"}");
                } else {
                    webview->PostWebMessageAsJson(L"{\"type\":\"power\", \"action\":\"resume\"}");
                }
            }
        }
        break;
    case WM_SIZE:
        if (webviewController != nullptr) {
            RECT bounds;
            GetClientRect(hWnd, &bounds);
            webviewController->put_Bounds(bounds);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_MOUSEMOVE:
        PowerManager::OnMouseMove();
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
