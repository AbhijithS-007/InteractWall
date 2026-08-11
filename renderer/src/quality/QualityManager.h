#pragma once
#include <string>
#include <cstdint>
#include "../plugins/interface/PluginAPI.h"

class QualityManager {
public:
    static void Initialize(uint64_t vramMB, const std::string& adapterName);
    static void SetQualityTierOverride(QualityTierLevel level);
    static const QualityTier* GetCurrentTier();
    static bool HasTierChanged();
    static void ClearTierChangedFlag();

private:
    static QualityTier s_currentTier;
    static bool s_tierChanged;
    static QualityTier GetTierSettings(QualityTierLevel level);
};
