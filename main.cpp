#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <string>
#include <cstdio>
#include <vector>

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

// ---------------------------------------------------------------
// WorkerW helpers — EXACT copy from renderer/src/core/main.cpp
// Lines 53-95 of the original, translated to printf instead of std::cout
// ---------------------------------------------------------------
HWND g_defView = nullptr;

struct WorkerWSearch {
    HWND targetParent = nullptr;
    HWND siblingWorkerW = nullptr;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    char className[256];
    GetClassNameA(hwnd, className, sizeof(className));

    // Check if this window has SHELLDLL_DefView as a child
    HWND defView = FindWindowExA(hwnd, nullptr, "SHELLDLL_DefView", nullptr);
    if (defView != nullptr) {
        printf("[WorkerW] Found SHELLDLL_DefView (0x%p) inside HWND: 0x%p (class: %s)\n", defView, hwnd, className);
        g_defView = defView; // Store globally for Z-ordering later

        WorkerWSearch* search = reinterpret_cast<WorkerWSearch*>(lParam);
        search->targetParent = hwnd;
        
        // The WorkerW we want is the NEXT sibling after hwnd, not a child.
        search->siblingWorkerW = FindWindowExA(nullptr, hwnd, "WorkerW", nullptr);
        
        if (search->siblingWorkerW) {
            printf("[WorkerW] SUCCESS: Found next WorkerW sibling: 0x%p\n", search->siblingWorkerW);
        } else {
            printf("[WorkerW] EVAL: targetParent 0x%p has no sibling WorkerW after it in Z-order.\n", hwnd);
        }
        return FALSE; // Stop enumeration
    }
    return TRUE;
}

HWND FindWorkerW() {
    HWND progman = FindWindowA("Progman", nullptr);
    if (!progman) {
        printf("[WorkerW] ERROR: Progman not found!\n");
        return nullptr;
    }
    printf("[WorkerW] Progman HWND: 0x%p\n", progman);

    // Send the undocumented 0x052C message to Progman to spawn a WorkerW
    SendMessageTimeoutA(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);
    // Windows 11 additional fallback messages
    SendMessageTimeoutA(progman, 0x052C, 0x0000000D, 0, SMTO_NORMAL, 1000, nullptr);
    SendMessageTimeoutA(progman, 0x052C, 0x0000000D, 1, SMTO_NORMAL, 1000, nullptr);

    WorkerWSearch search;
    
    // RETRY LOOP: Explorer might take a moment to split the desktop after the message
    for (int i = 0; i < 20; i++) {
        search.siblingWorkerW = nullptr;
        search.targetParent = nullptr;
        g_defView = nullptr;
        
        EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&search));

        if (search.siblingWorkerW) {
            printf("[WorkerW] Sibling WorkerW successfully found after %d retries.\n", i);
            return search.siblingWorkerW;
        }

        printf("[WorkerW] Retry %d: Sibling WorkerW not found yet, waiting 50ms...\n", i);
        Sleep(50);
    }

    // Fallback: If we couldn't find the sibling WorkerW, look for a WorkerW CHILD inside the target parent.
    // On Windows 11, if the desktop doesn't split, Progman has an internal WorkerW child that sits behind SHELLDLL_DefView.
    // Parenting to this internal WorkerW forces the DWM to clip WebView2's DComp visuals behind the icons.
    if (search.targetParent) {
        HWND childWorkerW = FindWindowExA(search.targetParent, nullptr, "WorkerW", nullptr);
        if (childWorkerW) {
            printf("[WorkerW] Sibling not found, but found child WorkerW (0x%p) inside targetParent (0x%p). Using it.\n", childWorkerW, search.targetParent);
            return childWorkerW;
        }

        printf("[WorkerW] Standard WorkerW sibling AND child WorkerW ultimately not found. Using targetParent (0x%p) as fallback.\n", search.targetParent);
        return search.targetParent;
    }

    // Absolute fallback
    printf("[WorkerW] SHELLDLL_DefView never found. Using Progman.\n");
    return progman;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== Web Wallpaper PoC Starting ===\n");

    // DPI awareness — must be set BEFORE any HWND/metrics calls.
    // (matches renderer/src/core/main.cpp line 413)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HINSTANCE hInstance = GetModuleHandle(nullptr);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // ---- Register window class ----
    // (matches renderer/src/core/main.cpp lines 428-434, using WNDCLASSEX with CS_CLASSDC)
    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "WebWallpaperClass";

    if (!RegisterClassExA(&wc)) {
        printf("[Init] FATAL: RegisterClassExA failed (error %lu)\n", GetLastError());
        return 1;
    }

    // ---- Find WorkerW ----
    HWND workerW = FindWorkerW();
    printf("[Init] FindWorkerW result: 0x%p\n", workerW);
    if (!workerW) {
        printf("[Init] FATAL: Could not find WorkerW or Progman. Cannot continue.\n");
        return 1;
    }

    // ---- Primary monitor resolution ----
    // (matches renderer/src/core/main.cpp lines 443-444: SM_CXSCREEN, not SM_CXVIRTUALSCREEN)
    int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    printf("[Init] Primary screen: %dx%d\n", screenWidth, screenHeight);

    // ---- Create borderless popup window ----
    // (matches renderer/src/core/main.cpp lines 448-458)
    g_hWnd = CreateWindowExA(
        0,
        wc.lpszClassName,
        "Web Wallpaper",
        WS_POPUP | WS_VISIBLE,
        0, 0, screenWidth, screenHeight,
        nullptr,    // NO parent during creation to avoid cross-thread ownership locking
        nullptr,
        hInstance,
        nullptr
    );

    if (!g_hWnd) {
        printf("[Init] FATAL: CreateWindowExA failed (error %lu)\n", GetLastError());
        return 1;
    }
    printf("[Init] Window HWND: 0x%p\n", g_hWnd);

    // ---- Force WS_CHILD style before reparenting ----
    LONG style = GetWindowLongA(g_hWnd, GWL_STYLE);
    style &= ~WS_POPUP;
    style |= WS_CHILD;
    SetWindowLongA(g_hWnd, GWL_STYLE, style);
    // Force Windows to apply the style change
    SetWindowPos(g_hWnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);

    // ---- Reparent under WorkerW ----
    // (matches renderer/src/core/main.cpp line 467)
    HWND prevParent = SetParent(g_hWnd, workerW);
    DWORD setParentError = GetLastError();
    printf("[Init] SetParent(g_hWnd, workerW) returned: 0x%p (prevParent). LastError: %lu\n", prevParent, setParentError);

    // Explicitly push to bottom of Z-order in case we share the parent with SHELLDLL_DefView
    // Only insert behind g_defView if they share the same parent, otherwise SetWindowPos will fail or have undefined behavior.
    HWND actualParent = GetParent(g_hWnd);
    HWND defViewParent = g_defView ? GetParent(g_defView) : nullptr;

    if (g_defView && actualParent == defViewParent) {
        SetWindowPos(g_hWnd, g_defView, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        printf("[Init] SetWindowPos behind g_defView (0x%p) executed.\n", g_defView);
    } else {
        SetWindowPos(g_hWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        printf("[Init] SetWindowPos HWND_BOTTOM executed (g_defView parent mismatch or missing).\n");
    }

    // ---- VERIFY: Visibility and Screen Bounds after reparenting ----
    BOOL isVisible = IsWindowVisible(g_hWnd);
    LONG currentStyle = GetWindowLongA(g_hWnd, GWL_STYLE);
    printf("[Init] VERIFY VISIBILITY: IsWindowVisible=%s, WS_VISIBLE=%s\n", 
           isVisible ? "TRUE" : "FALSE", (currentStyle & WS_VISIBLE) ? "YES" : "NO");
    
    if (!isVisible) {
        ShowWindow(g_hWnd, SW_SHOW);
        printf("[Init] VERIFY VISIBILITY: Forced ShowWindow(SW_SHOW) to repair missing visibility.\n");
    }

    RECT windowRect;
    GetWindowRect(g_hWnd, &windowRect);
    printf("[Init] VERIFY BOUNDS: Screen RECT: Left=%ld, Top=%ld, Right=%ld, Bottom=%ld (Size: %ldx%ld)\n",
           windowRect.left, windowRect.top, windowRect.right, windowRect.bottom,
           windowRect.right - windowRect.left, windowRect.bottom - windowRect.top);

    // ---- Verify reparenting with live queries ----
    actualParent = GetParent(g_hWnd);
    char parentClassName[256] = {0};
    char ownClassName[256] = {0};
    if (actualParent) {
        GetClassNameA(actualParent, parentClassName, sizeof(parentClassName));
    }
    GetClassNameA(g_hWnd, ownClassName, sizeof(ownClassName));
    LONG windowStyle = GetWindowLongA(g_hWnd, GWL_STYLE);

    printf("[Init] VERIFICATION:\n");
    printf("[Init]   Own HWND: 0x%p (class: %s)\n", g_hWnd, ownClassName);
    printf("[Init]   Actual parent: 0x%p (class: %s)\n", actualParent, parentClassName);
    printf("[Init]   Expected parent (workerW): 0x%p\n", workerW);
    printf("[Init]   Parent match: %s\n", (actualParent == workerW) ? "YES" : "NO *** MISMATCH ***");
    printf("[Init]   Window style: 0x%08lX (WS_CHILD=%s, WS_POPUP=%s, WS_VISIBLE=%s)\n",
        windowStyle,
        (windowStyle & WS_CHILD) ? "yes" : "no",
        (windowStyle & WS_POPUP) ? "yes" : "no",
        (windowStyle & WS_VISIBLE) ? "yes" : "no");

    // ---- Build diagnostic JSON to send to the HTML overlay later ----
    snprintf(g_diagJson, sizeof(g_diagJson),
        "{\"type\":\"diag\","
        "\"workerW\":\"0x%p\","
        "\"setParentPrev\":\"0x%p\","
        "\"setParentErr\":%lu,"
        "\"actualParent\":\"0x%p\","
        "\"parentClass\":\"%s\","
        "\"ownClass\":\"%s\","
        "\"parentMatch\":%s,"
        "\"windowStyle\":\"0x%08lX\","
        "\"screenW\":%d,"
        "\"screenH\":%d}",
        workerW, prevParent, setParentError,
        actualParent, parentClassName, ownClassName,
        (actualParent == workerW) ? "true" : "false",
        windowStyle,
        screenWidth, screenHeight);

    printf("[Init] DiagJSON: %s\n", g_diagJson);

    // ---- Initialize WebView2 ----
    std::wstring userDataFolder = GetBasePath() + L"\\webview_data";
    std::wstring assetsFolder = GetBasePath() + L"\\assets";
    printf("[Init] UserDataFolder: %ls\n", userDataFolder.c_str());
    printf("[Init] AssetsFolder: %ls\n", assetsFolder.c_str());

    static std::wstring s_assetsFolder;
    s_assetsFolder = assetsFolder;

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, userDataFolder.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result)) {
                    printf("[WebView2] FATAL: Environment creation failed. HRESULT: 0x%08lX\n", result);
                    return result;
                }
                printf("[WebView2] Environment created.\n");

                env->CreateCoreWebView2Controller(g_hWnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(result) || !controller) {
                                printf("[WebView2] FATAL: Controller creation failed. HRESULT: 0x%08lX\n", result);
                                return result;
                            }
                            printf("[WebView2] Controller created.\n");

                            webviewController = controller;
                            webviewController->get_CoreWebView2(&webview);

                            // ---- Set transparent background ----
                            ComPtr<ICoreWebView2Controller2> controller2;
                            webviewController.As(&controller2);
                            if (controller2) {
                                COREWEBVIEW2_COLOR transparent = { 0, 0, 0, 0 };
                                controller2->put_DefaultBackgroundColor(transparent);
                                printf("[WebView2] Background set to transparent.\n");
                            }

                            // ---- Resize and Visibility ----
                            RECT bounds;
                            GetClientRect(g_hWnd, &bounds);
                            webviewController->put_Bounds(bounds);
                            webviewController->put_IsVisible(TRUE);

                            // ---- VERIFY: WebView2 Controller state ----
                            RECT actualBounds;
                            webviewController->get_Bounds(&actualBounds);
                            BOOL isWebViewVisible = FALSE;
                            webviewController->get_IsVisible(&isWebViewVisible);
                            printf("[WebView2] VERIFY CONTROLLER: Bounds set to %ld,%ld %ldx%ld. Visible: %s\n",
                                   actualBounds.left, actualBounds.top, actualBounds.right - actualBounds.left, actualBounds.bottom - actualBounds.top,
                                   isWebViewVisible ? "TRUE" : "FALSE");

                            // ---- Virtual Host mapping ----
                            ComPtr<ICoreWebView2_3> webview3;
                            webview.As(&webview3);
                            if (webview3) {
                                webview3->SetVirtualHostNameToFolderMapping(
                                    L"app-assets.local",
                                    s_assetsFolder.c_str(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW
                                );
                                printf("[WebView2] Virtual host mapped.\n");
                            }

                            // ---- Navigation Completed Handler ----
                            webview->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                        BOOL success = FALSE;
                                        args->get_IsSuccess(&success);
                                        COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                                        args->get_WebErrorStatus(&status);
                                        printf("[WebView2] VERIFY NAVIGATION: NavigationCompleted! Success=%s, ErrorStatus=%d\n", 
                                               success ? "TRUE" : "FALSE", status);
                                        return S_OK;
                                    }).Get(), nullptr);

                            // Navigate
                            printf("[WebView2] Navigating to https://app-assets.local/index.html ...\n");
                            webview->Navigate(L"https://app-assets.local/index.html");

                            // Start timer for cursor position polling (~60 FPS)
                            SetTimer(g_hWnd, 1, 16, nullptr);
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    if (FAILED(hr)) {
        printf("[Init] FATAL: CreateCoreWebView2EnvironmentWithOptions failed. HRESULT: 0x%08lX\n", hr);
        return 1;
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_TIMER:
        if (wParam == 1 && webview) {
            static int timerCount = 0;
            // Broadcast diagnostic JSON every 60 ticks (approx 1s) to ensure the JS overlay receives it
            if (timerCount % 60 == 0) {
                int wchars = MultiByteToWideChar(CP_UTF8, 0, g_diagJson, -1, NULL, 0);
                std::vector<wchar_t> wstr(wchars);
                MultiByteToWideChar(CP_UTF8, 0, g_diagJson, -1, &wstr[0], wchars);
                webview->PostWebMessageAsJson(wstr.data());
            }
            timerCount++;

            POINT pt;
            if (GetCursorPos(&pt)) {
                int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
                int screenHeight = GetSystemMetrics(SM_CYSCREEN);

                float nx = (static_cast<float>(pt.x) / screenWidth) * 2.0f - 1.0f;
                float ny = (static_cast<float>(pt.y) / screenHeight) * 2.0f - 1.0f; // Sign flipped to fix Y inversion

                char json[128];
                snprintf(json, sizeof(json), "{\"type\":\"mousemove\", \"x\":%f, \"y\":%f}", nx, ny);

                int wchars_num = MultiByteToWideChar(CP_UTF8, 0, json, -1, NULL, 0);
                std::vector<wchar_t> wstr(wchars_num);
                MultiByteToWideChar(CP_UTF8, 0, json, -1, &wstr[0], wchars_num);

                webview->PostWebMessageAsJson(wstr.data());
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
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
