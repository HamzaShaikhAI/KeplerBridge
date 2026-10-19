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

    // Spoof settings (Phase 2 preparation)
    bool IsFeatureLevelSpoofEnabled() const { return m_enableFeatureLevelSpoof; }
    const std::string& GetTargetFeatureLevel() const { return m_targetFeatureLevel; }
    uint32_t GetSpoofVRAMMB() const { return m_spoofVRAMMB; }

    // Downgrader settings
    uint32_t GetMaxTextureResolution() const { return m_maxTextureResolution; }
    bool ShouldStripHeavyShaders() const { return m_stripHeavyShaders; }

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

    uint32_t m_maxTextureResolution = 512;
    bool m_stripHeavyShaders = true;

    std::string m_configFilePath;
};

} // namespace kepler
