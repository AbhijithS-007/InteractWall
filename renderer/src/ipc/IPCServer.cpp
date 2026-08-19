#include "IPCServer.h"
#include "json.hpp"
#include <iostream>
#include <sstream>
#include <dxgi1_2.h>
#pragma comment(lib, "dxgi.lib")

using json = nlohmann::json;

std::thread IPCServer::s_thread;
std::atomic<bool> IPCServer::s_running{false};
std::mutex IPCServer::s_cmdMutex;
std::vector<IPCMessage> IPCServer::s_cmdQueue;
std::mutex IPCServer::s_statusMutex;
StatusSnapshot IPCServer::s_status = {0, 0, 0, "UNKNOWN", "UNKNOWN", "none"};
static std::chrono::steady_clock::time_point s_lastMessageTime = std::chrono::steady_clock::now();

void IPCServer::Start() {
    if (s_running) return;
    s_running = true;
    s_thread = std::thread(ServerThreadFunc);
    std::cout << "[IPC] Server started.\n";
}

void IPCServer::Stop() {
    if (!s_running) return;
    s_running = false;
    
    // Connect to the pipe locally to unblock ConnectNamedPipe
    HANDLE hPipe = CreateFileA(
        "\\\\.\\pipe\\InteractWall",
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, 0, nullptr
    );
    if (hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe);
    }
    
    if (s_thread.joinable()) {
        s_thread.join();
    }
    std::cout << "[IPC] Server stopped.\n";
}

void IPCServer::UpdateStatus(const StatusSnapshot& status) {
    std::lock_guard<std::mutex> lock(s_statusMutex);
    s_status = status;
}

std::vector<IPCMessage> IPCServer::GetPendingCommands() {
    std::lock_guard<std::mutex> lock(s_cmdMutex);
    std::vector<IPCMessage> cmds = s_cmdQueue;
    s_cmdQueue.clear();
    return cmds;
}

float IPCServer::GetTimeSinceLastMessage() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(now - s_lastMessageTime).count();
}

void IPCServer::ServerThreadFunc() {
    while (s_running) {
        HANDLE hPipe = CreateNamedPipeA(
            "\\\\.\\pipe\\InteractWall",
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            4096, 4096, 0, nullptr
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            std::cerr << "[IPC] CreateNamedPipeA failed: " << GetLastError() << "\n";
            Sleep(1000);
            continue;
        }

        bool connected = ConnectNamedPipe(hPipe, nullptr) ? true : (GetLastError() == ERROR_PIPE_CONNECTED);
        
        if (!s_running) {
            CloseHandle(hPipe);
            break;
        }

        if (connected) {
            ProcessClient(hPipe);
        }
        
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
}

void IPCServer::ProcessClient(HANDLE hPipe) {
    char buffer[4096];
    DWORD bytesRead = 0;

    while (s_running && ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        
        std::string payload(buffer);
        // Process newline-delimited JSON
        std::istringstream stream(payload);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.empty() || line == "\r") continue;
            
            s_lastMessageTime = std::chrono::steady_clock::now();
            
            try {
                auto j = json::parse(line);
                std::string cmd = j.value("cmd", "");
                
                if (cmd == "get_status") {
                    StatusSnapshot snap;
                    {
                        std::lock_guard<std::mutex> lock(s_statusMutex);
                        snap = s_status;
                    }
                    
                    json resp;
                    resp["fps"] = snap.fps;
                    resp["cpu"] = snap.cpu;
                    resp["gpuMemMB"] = snap.gpuMemMB;
                    resp["state"] = snap.state;
                    resp["tier"] = snap.tier;
                    resp["activePlugin"] = snap.activePlugin;
                    
                    std::string respStr = resp.dump() + "\n";
                    DWORD bytesWritten = 0;
                    WriteFile(hPipe, respStr.c_str(), respStr.size(), &bytesWritten, nullptr);
                } else if (cmd == "get_adapters") {
                    json resp = json::array();
                    IDXGIFactory1* pFactory = nullptr;
                    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory))) {
                        IDXGIAdapter1* pAdapter = nullptr;
                        for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
                            DXGI_ADAPTER_DESC1 desc;
                            pAdapter->GetDesc1(&desc);
                            char adapterName[128] = {};
                            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, adapterName, sizeof(adapterName), nullptr, nullptr);
                            
                            // Exclude Microsoft Basic Render Driver
                            if (strcmp(adapterName, "Microsoft Basic Render Driver") != 0) {
                                resp.push_back(adapterName);
                            }
                            pAdapter->Release();
                        }
                        pFactory->Release();
                    }
                    
                    std::string respStr = resp.dump() + "\n";
                    DWORD bytesWritten = 0;
                    WriteFile(hPipe, respStr.c_str(), respStr.size(), &bytesWritten, nullptr);
                } else {
                    IPCMessage msg = {};
                    msg.cmd = cmd;
                    if (cmd == "apply_wallpaper") {
                        msg.strArg1 = j.value("layerA", "");
                        msg.strArg2 = j.value("layerB", "");
                        
                        BOOL success = SystemParametersInfoA(SPI_SETDESKWALLPAPER, 0, (void*)msg.strArg1.c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
                        std::string respStr = success ? "{\"status\":\"ok\"}\n" : "{\"status\":\"error\"}\n";
                        DWORD bytesWritten = 0;
                        WriteFile(hPipe, respStr.c_str(), respStr.size(), &bytesWritten, nullptr);
                    } else if (cmd == "set_effect") {
                        msg.strArg1 = j.value("plugin", "");
                    } else if (cmd == "set_setting") {
                        msg.strArg1 = j.value("key", "");
                        if (msg.strArg1 == "engine.preferredGPU") {
                            msg.strArg2 = j.value("valueStr", ""); // Use strArg2 for string setting
                        } else {
                            msg.floatArg = j.value("value", 0.0f);
                        }
                    } else if (cmd == "set_quality_tier") {
                        msg.strArg1 = j.value("tier", "");
                    } else if (cmd == "remove_effect") {
                        // no args needed
                    } else if (cmd == "quit") {
                        // no args needed
                    }
                    
                    if (!msg.cmd.empty()) {
                        std::lock_guard<std::mutex> lock(s_cmdMutex);
                        s_cmdQueue.push_back(msg);
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[IPC] JSON parsing error: " << e.what() << "\n";
            }
        }
    }
}
