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

    IEffectPlugin* GetActivePlugin() const;
    std::string GetActivePluginName() const;
    void SetActivePlugin(const std::string& name);

    size_t GetPluginCount() const;
    IEffectPlugin* GetPlugin(size_t index) const;

private:
    std::vector<LoadedPlugin> m_plugins;
    int m_activeIndex = -1;
};

