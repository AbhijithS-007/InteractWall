#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

struct IPCMessage {
    std::string cmd;
    std::string strArg1;
    std::string strArg2;
    float floatArg = 0.0f;
};

struct StatusSnapshot {
    float fps;
    float cpu;
    float gpuMemMB;
    std::string state;
    std::string tier;
    std::string activePlugin;
};

class IPCServer {
public:
    static void Start();
    static void Stop();
    
    // Called by the main thread to process queued commands
    static std::vector<IPCMessage> GetPendingCommands();
    
    // Called by the main thread to keep the status fresh for 'get_status' requests
    static void UpdateStatus(const StatusSnapshot& status);
    static float GetTimeSinceLastMessage();

private:
    static void ServerThreadFunc();
    static void ProcessClient(HANDLE hPipe);

    static std::thread s_thread;
    static std::atomic<bool> s_running;
    
    static std::mutex s_cmdMutex;
    static std::vector<IPCMessage> s_cmdQueue;

    static std::mutex s_statusMutex;
    static StatusSnapshot s_status;
};
