#include "core/config.h"
#include "core/logger.h"
#include "core/profile_manager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sstream>

namespace kepler {

Config& Config::Instance() {
    static Config s_instance;
    return s_instance;
}

bool Config::Load(const std::string& iniFilePath) {
    m_configFilePath = iniFilePath;

    // Initialize GameProfileManager with the given configuration path
    // Pass empty hostExePath to allow automatic detection if running inside a host process
    bool loaded = GameProfileManager::Instance().Initialize("", iniFilePath);

    // Sync Config member variables from the active profile's CapabilityPolicy
    SyncFromActivePolicy();

    if (loaded) {
        LOG_INFO("Successfully loaded configuration from: " + iniFilePath);
    }
    return loaded;
}

void Config::SyncFromActivePolicy() {
    const auto& policy = GameProfileManager::Instance().GetActivePolicy();

    m_enableLogging = policy.enableLogging;
    m_logVerboseCalls = policy.logVerboseCalls;

    m_enableFeatureLevelSpoof = policy.enableFeatureLevelSpoof;
    m_targetFeatureLevel = policy.targetFeatureLevelStr;
    m_spoofVRAMMB = policy.spoofVRAMMB;

    m_enableShaderInspection = policy.enableShaderInspection;
    m_enableShaderFallback = policy.enableShaderFallback;
    m_enableTextureClamping = policy.enableTextureClamping;
    m_maxTextureResolution = policy.maxTextureResolution;
    m_stripHeavyShaders = policy.stripHeavyShaders;

    m_enableD3D11Translation = policy.enableD3D11Translation;
    m_emulateBarriers = policy.emulateBarriers;
    m_enableStateTracking = policy.enableStateTracking;
    m_immediateQueueFlush = policy.immediateQueueFlush;
    m_emulateQueries = policy.emulateQueries;

    m_maxConcurrentUAVs = policy.maxConcurrentUAVs;
    m_safeFlipModeFallback = policy.safeFlipModeFallback;
    m_activeProfileName = policy.profileName;

    m_enableShaderDiskCache = policy.enableShaderDiskCache;
    m_shaderCacheDir = policy.shaderCacheDir;
}

void Config::SetShaderFallbackEnabled(bool enabled) {
    m_enableShaderFallback = enabled;
    GameProfileManager::Instance().GetMutablePolicy().enableShaderFallback = enabled;
}

void Config::SetD3D11TranslationEnabled(bool enabled) {
    m_enableD3D11Translation = enabled;
    GameProfileManager::Instance().GetMutablePolicy().enableD3D11Translation = enabled;
}

void Config::SetShaderDiskCacheEnabled(bool enabled) {
    m_enableShaderDiskCache = enabled;
    GameProfileManager::Instance().GetMutablePolicy().enableShaderDiskCache = enabled;
}

void Config::LogSettings() const {
    GameProfileManager::Instance().LogProfileDetails();
}

} // namespace kepler
