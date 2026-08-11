#include "PowerManager.h"
#include <iostream>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

HWND PowerManager::s_rendererHwnd = nullptr;
PowerState PowerManager::s_currentState = POWER_STATE_VISIBLE_ACTIVE;
std::chrono::steady_clock::time_point PowerManager::s_lastMouseMoveTime;
std::chrono::steady_clock::time_point PowerManager::s_lastFrameTime;

float PowerManager::s_idleLowFpsTimeoutSec = 30.0f;
float PowerManager::s_idlePausedTimeoutSec = 60.0f;
bool PowerManager::s_pauseHidden = true;
bool PowerManager::s_pauseFullscreen = true;
bool PowerManager::s_pauseBattery = false;
bool PowerManager::s_pauseSessionLocked = true;
bool PowerManager::s_sessionLocked = false;
std::chrono::steady_clock::time_point PowerManager::s_lastPowerPollTime;

void PowerManager::Initialize(HWND rendererHwnd) {
    s_rendererHwnd = rendererHwnd;
    s_lastMouseMoveTime = std::chrono::steady_clock::now();
    s_lastFrameTime = std::chrono::steady_clock::now();
}

void PowerManager::OnMouseMove() {
    s_lastMouseMoveTime = std::chrono::steady_clock::now();
    if (s_currentState == POWER_STATE_VISIBLE_IDLE_LOW_FPS || s_currentState == POWER_STATE_IDLE_PAUSED) {
        SetState(POWER_STATE_VISIBLE_ACTIVE);
    }
}

void PowerManager::SetState(PowerState newState) {
    if (s_currentState != newState) {
        s_currentState = newState;
        const char* stateNames[] = {
            "VISIBLE_ACTIVE",
            "VISIBLE_IDLE_LOW_FPS",
            "IDLE_PAUSED",
            "HIDDEN_OCCLUDED",
            "HIDDEN_FULLSCREEN",
            "HIDDEN_BATTERY",
            "HIDDEN_SESSION_LOCKED",
            "HIDDEN_REMOTE_SESSION"
        };
        std::cout << "[Power] State transitioned to: " << stateNames[newState] << "\n";
    }
}

PowerState PowerManager::GetState() {
    return s_currentState;
}

// Struct for EnumWindows
struct OcclusionData {
    HWND targetHwnd;
    HWND targetTopLevel;
    RECT desktopRect;
    HRGN unoccludedRegion;
    bool fullyOccluded;
};

static BOOL CALLBACK OcclusionEnumProc(HWND hwnd, LPARAM lParam) {
    OcclusionData* data = reinterpret_cast<OcclusionData*>(lParam);

    // Stop if we reached our own top-level window (WorkerW or Progman)
    if (hwnd == data->targetTopLevel) {
        return FALSE; // We've checked all windows ABOVE us
    }

    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;

    // Skip the desktop manager windows themselves
    char className[256] = {};
    GetClassNameA(hwnd, className, sizeof(className));
    if (strcmp(className, "WorkerW") == 0 || strcmp(className, "Progman") == 0) return TRUE;

    // Skip transparent layered windows
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_LAYERED) {
        BYTE alpha;
        DWORD flags;
        if (GetLayeredWindowAttributes(hwnd, nullptr, &alpha, &flags)) {
            if ((flags & LWA_ALPHA) && alpha < 255) return TRUE;
        }
    }

    // Skip cloaked windows (e.g. on other virtual desktops)
    int cloaked = 0;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return TRUE;

    RECT rect;
    if (GetWindowRect(hwnd, &rect)) {
        HRGN windowRgn = CreateRectRgnIndirect(&rect);
        // Subtract this window's region from our unoccluded region
        int result = CombineRgn(data->unoccludedRegion, data->unoccludedRegion, windowRgn, RGN_DIFF);
        DeleteObject(windowRgn);
        
        if (result == NULLREGION) {
            data->fullyOccluded = true;
            return FALSE; // Fully occluded, stop iterating
        }
    }
    return TRUE;
}

bool PowerManager::IsOccluded() {
    int cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(s_rendererHwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
        if (cloaked) return true;
    }

    // Full-coverage check
    OcclusionData data = {};
    data.targetHwnd = s_rendererHwnd;
    data.targetTopLevel = GetAncestor(s_rendererHwnd, GA_ROOT);
    data.desktopRect = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    data.unoccludedRegion = CreateRectRgnIndirect(&data.desktopRect);
    data.fullyOccluded = false;

    EnumWindows(OcclusionEnumProc, reinterpret_cast<LPARAM>(&data));
    DeleteObject(data.unoccludedRegion);

    return data.fullyOccluded;
}

bool PowerManager::IsForegroundFullscreen() {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == GetDesktopWindow() || fg == GetShellWindow() || fg == s_rendererHwnd) return false;

    char className[256] = {};
    GetClassNameA(fg, className, sizeof(className));
    if (strcmp(className, "WorkerW") == 0 || strcmp(className, "Progman") == 0) return false;

    HMONITOR hMonitor = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfo(hMonitor, &mi)) {
        RECT fgRect;
        GetWindowRect(fg, &fgRect);
        
        // Add a 1-pixel tolerance for borderless windows that might be slightly off
        if (fgRect.left <= mi.rcMonitor.left + 1 && 
            fgRect.top <= mi.rcMonitor.top + 1 &&
            fgRect.right >= mi.rcMonitor.right - 1 && 
            fgRect.bottom >= mi.rcMonitor.bottom - 1) {
            return true;
        }
    }
    return false;
}

void PowerManager::SetIdleTimeout(float pausedSec) {
    s_idlePausedTimeoutSec = pausedSec;
    // We can just set low FPS timeout to half of paused timeout
    s_idleLowFpsTimeoutSec = pausedSec > 0.0f ? pausedSec / 2.0f : 0.0f;
}

void PowerManager::SetPauseHidden(bool pause) {
    s_pauseHidden = pause;
}

void PowerManager::SetPauseFullscreen(bool pause) {
    s_pauseFullscreen = pause;
}

void PowerManager::SetPauseBattery(bool pause) {
    s_pauseBattery = pause;
}

void PowerManager::SetPauseSessionLocked(bool pause) {
    s_pauseSessionLocked = pause;
}

void PowerManager::SetSessionLocked(bool locked) {
    s_sessionLocked = locked;
}

bool PowerManager::IsOnBatterySaver() {
    SYSTEM_POWER_STATUS status = {};
    if (GetSystemPowerStatus(&status)) {
        if (status.ACLineStatus == 0) { // Unplugged
            // Check battery saver flag or low battery (< 20%)
            if (status.SystemStatusFlag == 1 || (status.BatteryLifePercent != 255 && status.BatteryLifePercent < 20)) {
                return true;
            }
        }
    }
    return false;
}

bool PowerManager::IsRemoteSession() {
    return GetSystemMetrics(SM_REMOTESESSION) != 0;
}

void PowerManager::Update() {
    if (s_pauseSessionLocked && s_sessionLocked) {
        SetState(POWER_STATE_HIDDEN_SESSION_LOCKED);
        return;
    }

    if (s_pauseSessionLocked && IsRemoteSession()) {
        SetState(POWER_STATE_HIDDEN_REMOTE_SESSION);
        return;
    }

    auto now = std::chrono::steady_clock::now();

    // Poll power state low frequency (every 5 seconds)
    float powerPollSecs = std::chrono::duration<float>(now - s_lastPowerPollTime).count();
    static bool currentlyOnBatterySaver = false;
    if (powerPollSecs > 5.0f || s_lastPowerPollTime.time_since_epoch().count() == 0) {
        currentlyOnBatterySaver = IsOnBatterySaver();
        s_lastPowerPollTime = now;
    }

    if (s_pauseBattery && currentlyOnBatterySaver) {
        SetState(POWER_STATE_HIDDEN_BATTERY);
        return;
    }

    if (s_pauseFullscreen && IsForegroundFullscreen()) {
        SetState(POWER_STATE_HIDDEN_FULLSCREEN);
        return;
    }

    if (s_pauseHidden && IsOccluded()) {
        SetState(POWER_STATE_HIDDEN_OCCLUDED);
        return;
    }

    float idleSeconds = std::chrono::duration<float>(now - s_lastMouseMoveTime).count();

    if (s_idlePausedTimeoutSec > 0 && idleSeconds >= s_idlePausedTimeoutSec) {
        SetState(POWER_STATE_IDLE_PAUSED);
    } else if (s_idleLowFpsTimeoutSec > 0 && idleSeconds >= s_idleLowFpsTimeoutSec) {
        SetState(POWER_STATE_VISIBLE_IDLE_LOW_FPS);
    } else {
        SetState(POWER_STATE_VISIBLE_ACTIVE);
    }
}

bool PowerManager::ShouldRenderFrame(int currentFpsCap) {
    if (s_currentState == POWER_STATE_HIDDEN_OCCLUDED ||
        s_currentState == POWER_STATE_HIDDEN_FULLSCREEN ||
        s_currentState == POWER_STATE_IDLE_PAUSED ||
        s_currentState == POWER_STATE_HIDDEN_BATTERY ||
        s_currentState == POWER_STATE_HIDDEN_SESSION_LOCKED ||
        s_currentState == POWER_STATE_HIDDEN_REMOTE_SESSION) {
        return false;
    }

    int effectiveFpsCap = currentFpsCap;
    if (s_currentState == POWER_STATE_VISIBLE_IDLE_LOW_FPS) {
        effectiveFpsCap = 10;
    }

    if (effectiveFpsCap > 0) {
        auto now = std::chrono::steady_clock::now();
        float msSinceLastFrame = std::chrono::duration<float, std::milli>(now - s_lastFrameTime).count();
        float targetMs = 1000.0f / effectiveFpsCap;
        if (msSinceLastFrame < targetMs) {
            return false;
        }
        s_lastFrameTime = now;
    } else {
        // Uncapped / Vsync managed by Present()
        s_lastFrameTime = std::chrono::steady_clock::now();
    }
    
    return true;
}
