#include "PluginLoader.h"
#include <iostream>

PluginLoader::PluginLoader() : m_activeIndex(-1) {}

PluginLoader::~PluginLoader() {
    UnloadAllPlugins();
}

void PluginLoader::LoadAllPlugins(const std::string& directory) {
    std::string searchPath = directory + "\\*.dll";
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        std::cout << "No plugins found in: " << directory << "\n";
        return;
    }

    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::string fullPath = directory + "\\" + findData.cFileName;
            HMODULE hMod = LoadLibraryA(fullPath.c_str());
            if (hMod) {
                PFN_CreateEffectPlugin createFunc = (PFN_CreateEffectPlugin)GetProcAddress(hMod, "CreateEffectPlugin");
                if (createFunc) {
                    IEffectPlugin* plugin = createFunc();
                    if (plugin) {
                        m_plugins.push_back({ findData.cFileName, hMod, plugin });
                        std::cout << "Successfully loaded plugin: " << findData.cFileName << "\n";
                    } else {
                        std::cout << "CreateEffectPlugin returned null for: " << findData.cFileName << "\n";
                        FreeLibrary(hMod);
                    }
                } else {
                    std::cout << "No CreateEffectPlugin export found in: " << findData.cFileName << "\n";
                    FreeLibrary(hMod);
                }
            } else {
                std::cout << "Failed to load DLL: " << fullPath << "\n";
            }
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
}

void PluginLoader::UnloadAllPlugins() {
    for (auto& p : m_plugins) {
        if (p.effect && p.effect->Shutdown) {
            p.effect->Shutdown();
        }
        if (p.handle) {
            FreeLibrary(p.handle);
        }
    }
    m_plugins.clear();
}

IEffectPlugin* PluginLoader::GetActivePlugin() const {
    if (m_activeIndex >= 0 && m_activeIndex < (int)m_plugins.size()) {
        return m_plugins[m_activeIndex].effect;
    }
    return nullptr;
}

std::string PluginLoader::GetActivePluginName() const {
    if (m_activeIndex >= 0 && m_activeIndex < (int)m_plugins.size()) {
        return m_plugins[m_activeIndex].name;
    }
    return "none";
}

void PluginLoader::SetActivePlugin(const std::string& name) {
    if (name.empty() || name == "none") {
        m_activeIndex = -1;
        std::cout << "[PluginLoader] Switched to no active plugin\n";
        return;
    }
    
    for (size_t i = 0; i < m_plugins.size(); i++) {
        std::string lowerName = m_plugins[i].name;
        std::string query = name;
        for (auto& c : lowerName) c = tolower(c);
        for (auto& c : query) c = tolower(c);
        
        query.erase(std::remove(query.begin(), query.end(), '_'), query.end());
        
        if (lowerName.find(query) != std::string::npos) {
            m_activeIndex = static_cast<int>(i);
            std::cout << "[PluginLoader] Switched active plugin to: " << m_plugins[i].name << "\n";
            return;
        }
    }
    std::cout << "[PluginLoader] Failed to find plugin matching: " << name << "\n";
}

size_t PluginLoader::GetPluginCount() const {
    return m_plugins.size();
}

IEffectPlugin* PluginLoader::GetPlugin(size_t index) const {
    if (index < m_plugins.size()) return m_plugins[index].effect;
    return nullptr;
}
