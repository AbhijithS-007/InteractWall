#pragma once
#include <windows.h>
#include <chrono>

enum PowerState {
    POWER_STATE_VISIBLE_ACTIVE = 0,
    POWER_STATE_VISIBLE_IDLE_LOW_FPS,
    POWER_STATE_IDLE_PAUSED,
    POWER_STATE_HIDDEN_OCCLUDED,
    POWER_STATE_HIDDEN_FULLSCREEN,
    POWER_STATE_HIDDEN_BATTERY,
    POWER_STATE_HIDDEN_SESSION_LOCKED,
    POWER_STATE_HIDDEN_REMOTE_SESSION
};

class PowerManager {
public:
    static void Initialize(HWND rendererHwnd);
    static void OnMouseMove();
    static void Update(); // Called periodically to check occlusion/idle
    static PowerState GetState();
    static bool ShouldRenderFrame(int currentFpsCap);

    // Configuration setters
    static void SetIdleTimeout(float pausedSec);
    static void SetPauseHidden(bool pause);
    static void SetPauseFullscreen(bool pause);
    static void SetPauseBattery(bool pause);
    static void SetPauseSessionLocked(bool pause);
    static void SetSessionLocked(bool locked);

private:
    static HWND s_rendererHwnd;
    static PowerState s_currentState;
    
    static std::chrono::steady_clock::time_point s_lastMouseMoveTime;
    static std::chrono::steady_clock::time_point s_lastFrameTime;
    
    // Configurable behaviors
    static float s_idleLowFpsTimeoutSec;
    static float s_idlePausedTimeoutSec;
    static bool s_pauseHidden;
    static bool s_pauseFullscreen;
    static bool s_pauseBattery;
    static bool s_pauseSessionLocked;
    static bool s_sessionLocked;
    static std::chrono::steady_clock::time_point s_lastPowerPollTime;

    static void SetState(PowerState newState);
    static bool IsOccluded();
    static bool IsForegroundFullscreen();
    static bool IsOnBatterySaver();
    static bool IsRemoteSession();
};
