#include "PluginLoader.h"
#include <iostream>

PluginLoader::PluginLoader() {}

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

IEffectPlugin* PluginLoader::GetFirstPlugin() const {
    if (!m_plugins.empty()) {
        return m_plugins.front().effect;
    }
    return nullptr;
}
