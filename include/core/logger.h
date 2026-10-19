#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>

#include <string>
#include <fstream>
#include <mutex>
#include <memory>

namespace kepler {

enum class LogLevel {
    Info,
    Warn,
    Hook,
    Error,
    Diagnostic
};

class Logger {
public:
    static Logger& Instance();

    bool Initialize(const std::string& logFilePath);
    void Shutdown();

    void Log(LogLevel level, const std::string& message);
    void LogRaw(const std::string& rawMessage);

    // Formatter helpers
    static std::string FeatureLevelToString(D3D_FEATURE_LEVEL fl);
    static std::string GuidToString(REFIID riid);
    static std::string HResultToString(HRESULT hr);

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string GetTimestamp();
    const char* LevelToString(LogLevel level);

    std::ofstream m_logFile;
    std::mutex m_mutex;
    bool m_initialized = false;
};

// Convenience logging macros
#define LOG_INFO(msg)        kepler::Logger::Instance().Log(kepler::LogLevel::Info, msg)
#define LOG_WARN(msg)        kepler::Logger::Instance().Log(kepler::LogLevel::Warn, msg)
#define LOG_HOOK(msg)        kepler::Logger::Instance().Log(kepler::LogLevel::Hook, msg)
#define LOG_ERROR(msg)       kepler::Logger::Instance().Log(kepler::LogLevel::Error, msg)
#define LOG_DIAGNOSTIC(msg)  kepler::Logger::Instance().Log(kepler::LogLevel::Diagnostic, msg)
#define LOG_RAW(msg)         kepler::Logger::Instance().LogRaw(msg)

} // namespace kepler
