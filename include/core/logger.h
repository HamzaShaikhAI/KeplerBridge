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
#include <unordered_map>
#include <atomic>
#include <chrono>

namespace kepler {

enum class LogLevel {
    Info,
    Warn,
    Hook,
    Error,
    Diagnostic
};

enum class ErrorSeverity {
    Warning,
    Error,
    Fatal
};

enum class ExecutionDisposition {
    Translated,
    Rejected,
    PassedThrough,
    Fallback
};

enum class KeplerErrorCode {
    Success = 0,
    DeviceRemoved = 1001,
    InitializationFailed = 1002,
    DeviceTimeout = 1003,
    InvalidDescriptor = 2001,
    DescriptorHeapExhausted = 2002,
    UnsupportedShaderInstruction = 3001,
    UnsupportedShaderStage = 3002,
    ShaderCompilationFailed = 3003,
    ShaderCacheCorruption = 3004,
    ResourceStateMismatch = 4001,
    ResourceHazardDetected = 4002,
    BufferDimensionExceeded = 4003,
    PresentationFailed = 5001,
    SwapChainCreationDenied = 5002,
    FeatureLevelUnsupported = 6001,
    UnknownError = 9999
};

struct DiagnosticReport {
    KeplerErrorCode errorCode = KeplerErrorCode::Success;
    ErrorSeverity severity = ErrorSeverity::Error;
    std::string subsystem;
    std::string apiOperation;
    HRESULT nativeHResult = S_OK;
    std::string shaderStage = "N/A";
    std::string shaderProfile = "N/A";
    std::string policyResult = "N/A";
    std::string resourceInfo = "N/A";
    std::string descriptorInfo = "N/A";
    ExecutionDisposition disposition = ExecutionDisposition::Rejected;
    std::string explanation;
    std::string actionableHint;
};

class Logger {
public:
    static Logger& Instance();

    bool Initialize(const std::string& logFilePath);
    void Shutdown();

    void Log(LogLevel level, const std::string& message);
    void LogRaw(const std::string& rawMessage);
    void ReportDiagnostic(const DiagnosticReport& report);

    void SetMaxLogSizeBytes(size_t maxBytes) { m_maxLogSizeBytes = maxBytes; }
    void RecordSessionStat(const std::string& key, uint64_t delta = 1);
    uint64_t GetSessionStat(const std::string& key);

    void PrintStartupCapabilityBanner(
        const std::string& appPath,
        const std::string& dllPath,
        bool translationActive,
        const std::string& adapterDesc,
        uint64_t vramBytes,
        D3D_FEATURE_LEVEL maxFL);

    void PrintSessionDiagnosticSummary();

    // Sanitization & formatters
    static std::string SanitizePath(const std::string& rawPath);
    static std::string FeatureLevelToString(D3D_FEATURE_LEVEL fl);
    static std::string GuidToString(REFIID riid);
    static std::string HResultToString(HRESULT hr);
    static std::string HexToString(uint64_t val);
    static const char* ErrorCodeToString(KeplerErrorCode code);
    static const char* SeverityToString(ErrorSeverity sev);
    static const char* DispositionToString(ExecutionDisposition disp);

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string GetTimestamp();
    const char* LevelToString(LogLevel level);
    void CheckAndRotateLog_Locked();

    std::ofstream m_logFile;
    std::string m_currentLogPath;
    std::mutex m_mutex;
    bool m_initialized = false;
    size_t m_maxLogSizeBytes = 10 * 1024 * 1024; // 10 MB default cap
    size_t m_currentWrittenBytes = 0;

    std::chrono::steady_clock::time_point m_sessionStartTime;
    std::unordered_map<std::string, uint64_t> m_sessionStats;
};

// Convenience logging macros
#define LOG_INFO(msg)        kepler::Logger::Instance().Log(kepler::LogLevel::Info, msg)
#define LOG_WARN(msg)        kepler::Logger::Instance().Log(kepler::LogLevel::Warn, msg)
#define LOG_HOOK(msg)        kepler::Logger::Instance().Log(kepler::LogLevel::Hook, msg)
#define LOG_ERROR(msg)       kepler::Logger::Instance().Log(kepler::LogLevel::Error, msg)
#define LOG_DIAGNOSTIC(msg)  kepler::Logger::Instance().Log(kepler::LogLevel::Diagnostic, msg)
#define LOG_RAW(msg)         kepler::Logger::Instance().LogRaw(msg)
#define LOG_DIAGNOSTIC_REPORT(report) kepler::Logger::Instance().ReportDiagnostic(report)

} // namespace kepler
