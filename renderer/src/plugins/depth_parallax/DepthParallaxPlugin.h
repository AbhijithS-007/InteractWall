#pragma once
#include "../interface/PluginAPI.h"

extern "C" {
    __declspec(dllexport) IEffectPlugin* CreateEffectPlugin();
}
