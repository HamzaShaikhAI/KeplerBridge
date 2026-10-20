#pragma once

#include <string>
#include <cstdint>

namespace kepler {

class Config {
public:
    static Config& Instance();

    bool Load(const std::string& iniFilePath);

    // General settings
    bool IsLoggingEnabled() const { return m_enableLogging; }
    bool IsVerboseCallsEnabled() const { return m_logVerboseCalls; }

    // Spoof settings
    bool IsFeatureLevelSpoofEnabled() const { return m_enableFeatureLevelSpoof; }
    const std::string& GetTargetFeatureLevel() const { return m_targetFeatureLevel; }
    uint32_t GetSpoofVRAMMB() const { return m_spoofVRAMMB; }

    // Downgrader settings
    bool IsShaderInspectionEnabled() const { return m_enableShaderInspection; }
    bool IsShaderFallbackEnabled() const { return m_enableShaderFallback; }
    void SetShaderFallbackEnabled(bool enabled) { m_enableShaderFallback = enabled; }
    bool IsTextureClampingEnabled() const { return m_enableTextureClamping; }
    uint32_t GetMaxTextureResolution() const { return m_maxTextureResolution; }
    bool ShouldStripHeavyShaders() const { return m_stripHeavyShaders; }

    // Translation settings (Phase 4)
    bool IsD3D11TranslationEnabled() const { return m_enableD3D11Translation; }
    void SetD3D11TranslationEnabled(bool enabled) { m_enableD3D11Translation = enabled; }
    bool IsBarrierEmulationEnabled() const { return m_emulateBarriers; }
    bool IsStateTrackingEnabled() const { return m_enableStateTracking; }
    bool IsImmediateQueueFlushEnabled() const { return m_immediateQueueFlush; }
    bool IsQueryEmulationEnabled() const { return m_emulateQueries; }

    // Shader Cache settings (Phase 6.1)
    bool IsShaderDiskCacheEnabled() const { return m_enableShaderDiskCache; }
    void SetShaderDiskCacheEnabled(bool enabled) { m_enableShaderDiskCache = enabled; }
    const std::string& GetShaderCacheDirectory() const { return m_shaderCacheDir; }

    void LogSettings() const;

private:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    bool m_enableLogging = true;
    bool m_logVerboseCalls = true;

    bool m_enableFeatureLevelSpoof = false;
    std::string m_targetFeatureLevel = "12_0";
    uint32_t m_spoofVRAMMB = 4096;

    bool m_enableShaderInspection = true;
    bool m_enableShaderFallback = false;
    bool m_enableTextureClamping = false;
    uint32_t m_maxTextureResolution = 512;
    bool m_stripHeavyShaders = false;

    // Translation settings
    bool m_enableD3D11Translation = false;
    bool m_emulateBarriers = true;
    bool m_enableStateTracking = true;
    bool m_immediateQueueFlush = true;
    bool m_emulateQueries = true;

    // Shader Cache settings
    bool m_enableShaderDiskCache = true;
    std::string m_shaderCacheDir = "kepler_cache/shaders";

    std::string m_configFilePath;
};

} // namespace kepler
