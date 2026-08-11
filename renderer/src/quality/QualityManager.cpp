#include "QualityManager.h"
#include <iostream>
#include <algorithm>

QualityTier QualityManager::s_currentTier = {};
bool QualityManager::s_tierChanged = false;

QualityTier QualityManager::GetTierSettings(QualityTierLevel level) {
    QualityTier tier = {};
    tier.level = level;
    switch (level) {
        case QUALITY_TIER_LOW:
            tier.fpsCap = 30;
            tier.disableOptionalPasses = true;
            break;
        case QUALITY_TIER_BALANCED:
            tier.fpsCap = 60;
            tier.disableOptionalPasses = false;
            break;
        case QUALITY_TIER_HIGH:
            tier.fpsCap = 0; // Uncapped/VSync
            tier.disableOptionalPasses = false;
            break;
    }
    return tier;
}

void QualityManager::Initialize(uint64_t vramMB, const std::string& adapterName) {
    std::cout << "[Quality] Initializing QualityManager...\n";
    
    // Check for integrated graphics
    std::string lowerAdapter = adapterName;
    std::transform(lowerAdapter.begin(), lowerAdapter.end(), lowerAdapter.begin(), ::tolower);
    bool isIntegrated = (lowerAdapter.find("intel") != std::string::npos) || 
                        (lowerAdapter.find("radeon graphics") != std::string::npos);

    QualityTierLevel initialLevel = QUALITY_TIER_BALANCED;

    if (vramMB < 1536 || isIntegrated) {
        std::cout << "[Quality] VRAM < 1.5GB or Integrated GPU detected. Defaulting to LOW tier.\n";
        initialLevel = QUALITY_TIER_LOW;
    } else {
        std::cout << "[Quality] Dedicated GPU detected. Defaulting to BALANCED tier.\n";
        initialLevel = QUALITY_TIER_BALANCED;
    }

    s_currentTier = GetTierSettings(initialLevel);
    s_tierChanged = true;
}

void QualityManager::SetQualityTierOverride(QualityTierLevel level) {
    if (s_currentTier.level != level) {
        s_currentTier = GetTierSettings(level);
        s_tierChanged = true;
        std::cout << "[Quality] Quality Tier overridden to: " << (int)level << "\n";
    }
}

const QualityTier* QualityManager::GetCurrentTier() {
    return &s_currentTier;
}

bool QualityManager::HasTierChanged() {
    return s_tierChanged;
}

void QualityManager::ClearTierChangedFlag() {
    s_tierChanged = false;
}
