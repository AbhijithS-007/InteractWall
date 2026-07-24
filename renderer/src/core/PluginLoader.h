#pragma once
#include <string>
#include <vector>
#include <windows.h>
#include "../plugins/interface/PluginAPI.h"

struct LoadedPlugin {
    std::string name;
    HMODULE handle;
    IEffectPlugin* effect;
};

class PluginLoader {
public:
    PluginLoader();
    ~PluginLoader();

    void LoadAllPlugins(const std::string& directory);
    void UnloadAllPlugins();

    IEffectPlugin* GetFirstPlugin() const; // Helper for this phase

private:
    std::vector<LoadedPlugin> m_plugins;
};
