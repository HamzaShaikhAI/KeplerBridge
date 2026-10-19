#include "core/config.h"
#include "core/logger.h"

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

    DWORD attr = GetFileAttributesA(iniFilePath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        LOG_WARN("Config file not found at: " + iniFilePath + ". Using internal defaults.");
        return false;
    }

    // [General]
    m_enableLogging = GetPrivateProfileIntA("General", "EnableLogging", 1, iniFilePath.c_str()) != 0;
    m_logVerboseCalls = GetPrivateProfileIntA("General", "LogVerboseCalls", 1, iniFilePath.c_str()) != 0;

    // [Spoof]
    m_enableFeatureLevelSpoof = GetPrivateProfileIntA("Spoof", "EnableFeatureLevelSpoof", 0, iniFilePath.c_str()) != 0;
    char targetFlBuf[64] = { 0 };
    GetPrivateProfileStringA("Spoof", "TargetFeatureLevel", "12_0", targetFlBuf, sizeof(targetFlBuf), iniFilePath.c_str());
    m_targetFeatureLevel = targetFlBuf;
    m_spoofVRAMMB = static_cast<uint32_t>(GetPrivateProfileIntA("Spoof", "SpoofVRAMMB", 4096, iniFilePath.c_str()));

    // [Downgrader]
    m_maxTextureResolution = static_cast<uint32_t>(GetPrivateProfileIntA("Downgrader", "MaxTextureResolution", 512, iniFilePath.c_str()));
    m_stripHeavyShaders = GetPrivateProfileIntA("Downgrader", "StripHeavyShaders", 1, iniFilePath.c_str()) != 0;

    LOG_INFO("Successfully loaded configuration from: " + iniFilePath);
    return true;
}

void Config::LogSettings() const {
    LOG_INFO("--- KeplerBridge Configuration ---");
    LOG_INFO("  [General] EnableLogging: " + std::string(m_enableLogging ? "true" : "false"));
    LOG_INFO("  [General] LogVerboseCalls: " + std::string(m_logVerboseCalls ? "true" : "false"));
    LOG_INFO("  [Spoof] EnableFeatureLevelSpoof: " + std::string(m_enableFeatureLevelSpoof ? "true" : "false"));
    LOG_INFO("  [Spoof] TargetFeatureLevel: " + m_targetFeatureLevel);
    LOG_INFO("  [Spoof] SpoofVRAMMB: " + std::to_string(m_spoofVRAMMB) + " MB");
    LOG_INFO("  [Downgrader] MaxTextureResolution: " + std::to_string(m_maxTextureResolution));
    LOG_INFO("  [Downgrader] StripHeavyShaders: " + std::string(m_stripHeavyShaders ? "true" : "false"));
    LOG_INFO("----------------------------------");
}

} // namespace kepler
