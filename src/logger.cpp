#include "core/logger.h"

#include <iomanip>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <regex>
#include <dxgi1_6.h>

namespace kepler {

Logger& Logger::Instance() {
    static Logger s_instance;
    return s_instance;
}

Logger::~Logger() {
    Shutdown();
}

bool Logger::Initialize(const std::string& logFilePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        return true;
    }

    m_currentLogPath = logFilePath;
    m_sessionStartTime = std::chrono::steady_clock::now();
    m_sessionStats.clear();

    m_logFile.open(logFilePath, std::ios::out | std::ios::trunc);
    if (!m_logFile.is_open()) {
        return false;
    }

    m_currentWrittenBytes = 0;
    m_initialized = true;
    return true;
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized && m_logFile.is_open()) {
        PrintSessionDiagnosticSummary();
        m_logFile.flush();
        m_logFile.close();
        m_initialized = false;
    }
}

std::string Logger::GetTimestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return std::string(buffer);
}

const char* Logger::LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Info:       return "[INFO]";
        case LogLevel::Warn:       return "[WARN]";
        case LogLevel::Hook:       return "[HOOK]";
        case LogLevel::Error:      return "[ERROR]";
        case LogLevel::Diagnostic: return "[DIAGNOSTIC]";
        default:                   return "[DEBUG]";
    }
}

void Logger::CheckAndRotateLog_Locked() {
    if (m_currentWrittenBytes < m_maxLogSizeBytes || m_currentLogPath.empty()) {
        return;
    }

    m_logFile.flush();
    m_logFile.close();

    std::string oldPath = m_currentLogPath + ".old";
    DeleteFileA(oldPath.c_str());
    MoveFileA(m_currentLogPath.c_str(), oldPath.c_str());

    m_logFile.open(m_currentLogPath, std::ios::out | std::ios::trunc);
    m_currentWrittenBytes = 0;
    if (m_logFile.is_open()) {
        std::string notice = "[LOGGER] Log file rotated due to size limit (" +
                             std::to_string(m_maxLogSizeBytes / (1024 * 1024)) + " MB). Previous log saved to .old\n";
        m_logFile << notice;
        m_currentWrittenBytes += notice.size();
    }
}

std::string Logger::SanitizePath(const std::string& rawPath) {
    // Redact personal user directory names (e.g. C:\Users\<Username>\ -> C:\Users\[USER]\)
    static const std::regex userRegex(R"(([Uu][Ss][Ee][Rr][Ss][\\\/])([^\\\/]+)([\\\/]))");
    return std::regex_replace(rawPath, userRegex, "$1[USER]$3");
}

void Logger::Log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || !m_logFile.is_open()) {
        return;
    }

    CheckAndRotateLog_Locked();

    std::string sanitized = SanitizePath(message);
    DWORD tid = GetCurrentThreadId();
    std::ostringstream ss;
    ss << GetTimestamp() << " [TID " << std::setw(5) << std::setfill('0') << tid << "] "
       << LevelToString(level) << " " << sanitized << "\n";

    std::string line = ss.str();
    m_logFile << line;
    m_logFile.flush();
    m_currentWrittenBytes += line.size();
}

void Logger::LogRaw(const std::string& rawMessage) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || !m_logFile.is_open()) {
        return;
    }

    CheckAndRotateLog_Locked();

    std::string sanitized = SanitizePath(rawMessage);
    std::string line = sanitized + "\n";
    m_logFile << line;
    m_logFile.flush();
    m_currentWrittenBytes += line.size();
}

const char* Logger::ErrorCodeToString(KeplerErrorCode code) {
    switch (code) {
        case KeplerErrorCode::Success:                      return "KB_OK (0)";
        case KeplerErrorCode::DeviceRemoved:                return "KB_ERR_DEVICE_REMOVED (1001)";
        case KeplerErrorCode::InitializationFailed:         return "KB_ERR_INIT_FAILED (1002)";
        case KeplerErrorCode::DeviceTimeout:                return "KB_ERR_DEVICE_TIMEOUT (1003)";
        case KeplerErrorCode::InvalidDescriptor:            return "KB_ERR_INVALID_DESCRIPTOR (2001)";
        case KeplerErrorCode::DescriptorHeapExhausted:      return "KB_ERR_HEAP_EXHAUSTED (2002)";
        case KeplerErrorCode::UnsupportedShaderInstruction: return "KB_ERR_UNSUPPORTED_INSTRUCTION (3001)";
        case KeplerErrorCode::UnsupportedShaderStage:       return "KB_ERR_UNSUPPORTED_STAGE (3002)";
        case KeplerErrorCode::ShaderCompilationFailed:      return "KB_ERR_SHADER_COMPILATION (3003)";
        case KeplerErrorCode::ShaderCacheCorruption:        return "KB_ERR_CACHE_CORRUPTION (3004)";
        case KeplerErrorCode::ResourceStateMismatch:        return "KB_ERR_RESOURCE_STATE_MISMATCH (4001)";
        case KeplerErrorCode::ResourceHazardDetected:       return "KB_ERR_RESOURCE_HAZARD (4002)";
        case KeplerErrorCode::BufferDimensionExceeded:      return "KB_ERR_BUFFER_DIMENSION (4003)";
        case KeplerErrorCode::PresentationFailed:           return "KB_ERR_PRESENT_FAILED (5001)";
        case KeplerErrorCode::SwapChainCreationDenied:      return "KB_ERR_SWAPCHAIN_DENIED (5002)";
        case KeplerErrorCode::FeatureLevelUnsupported:      return "KB_ERR_FEATURE_LEVEL (6001)";
        default:                                            return "KB_ERR_UNKNOWN (9999)";
    }
}

const char* Logger::SeverityToString(ErrorSeverity sev) {
    switch (sev) {
        case ErrorSeverity::Warning: return "WARNING";
        case ErrorSeverity::Error:   return "ERROR";
        case ErrorSeverity::Fatal:   return "FATAL";
        default:                     return "UNKNOWN";
    }
}

const char* Logger::DispositionToString(ExecutionDisposition disp) {
    switch (disp) {
        case ExecutionDisposition::Translated:    return "TRANSLATED";
        case ExecutionDisposition::Rejected:      return "REJECTED";
        case ExecutionDisposition::PassedThrough: return "PASSED_THROUGH";
        case ExecutionDisposition::Fallback:      return "FALLBACK";
        default:                                  return "UNKNOWN";
    }
}

void Logger::ReportDiagnostic(const DiagnosticReport& report) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || !m_logFile.is_open()) {
        return;
    }

    CheckAndRotateLog_Locked();

    // Track session stats
    if (report.severity == ErrorSeverity::Fatal) {
        m_sessionStats["errors_fatal"]++;
    } else if (report.severity == ErrorSeverity::Error) {
        m_sessionStats["errors_error"]++;
    } else {
        m_sessionStats["errors_warning"]++;
    }

    std::ostringstream ss;
    ss << "\n"
       << "================== [KEPLER DIAGNOSTIC EVENT] ==================\n"
       << " [CODE]        : " << ErrorCodeToString(report.errorCode)
       << " [Severity: " << SeverityToString(report.severity) << "]\n"
       << " [SUBSYSTEM]   : " << report.subsystem << " :: " << report.apiOperation << "\n"
       << " [HRESULT]     : " << HResultToString(report.nativeHResult) << "\n"
       << " [DISPOSITION] : " << DispositionToString(report.disposition) << "\n"
       << " [SHADER]      : Stage: " << report.shaderStage << " | Profile: " << report.shaderProfile
       << " | Policy: " << report.policyResult << "\n"
       << " [RESOURCE]    : " << SanitizePath(report.resourceInfo) << "\n"
       << " [DESCRIPTOR]  : " << report.descriptorInfo << "\n"
       << " [EXPLANATION] : " << report.explanation << "\n"
       << " [ACTION HINT] : " << report.actionableHint << "\n"
       << "================================================================\n";

    std::string block = ss.str();
    m_logFile << block;
    m_logFile.flush();
    m_currentWrittenBytes += block.size();
}

void Logger::RecordSessionStat(const std::string& key, uint64_t delta) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessionStats[key] += delta;
}

uint64_t Logger::GetSessionStat(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessionStats.find(key);
    return (it != m_sessionStats.end()) ? it->second : 0;
}

void Logger::PrintStartupCapabilityBanner(
    const std::string& appPath,
    const std::string& dllPath,
    bool translationActive,
    const std::string& adapterDesc,
    uint64_t vramBytes,
    D3D_FEATURE_LEVEL maxFL)
{
    DWORD pid = GetCurrentProcessId();
    std::ostringstream ss;
    ss << "================================================================================\n"
       << "  KeplerBridge Startup Capability Detection & Telemetry\n"
       << "================================================================================\n"
       << " [APP]       : " << SanitizePath(appPath) << " (PID: " << pid << ")\n"
       << " [PROXY DLL] : " << SanitizePath(dllPath) << "\n"
       << " [GPU]       : " << adapterDesc << " (" << (vramBytes / (1024 * 1024)) << " MB VRAM)\n"
       << " [NATIVE FL] : " << FeatureLevelToString(maxFL) << "\n"
       << " [CORE MODE] : " << (translationActive ? "TRANSLATION MODE (D3D12->D3D11 Active)" : "DIRECT PASSTHROUGH MODE") << "\n"
       << " [TIME]      : " << GetTimestamp() << "\n"
       << "================================================================================\n";

    LogRaw(ss.str());
}

void Logger::PrintSessionDiagnosticSummary() {
    auto now = std::chrono::steady_clock::now();
    double elapsedSec = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_sessionStartTime).count() / 1000.0;

    uint64_t fatalErr = m_sessionStats["errors_fatal"];
    uint64_t errCount = m_sessionStats["errors_error"];
    uint64_t warnCount = m_sessionStats["errors_warning"];
    uint64_t translated = m_sessionStats["commands_translated"];
    uint64_t rejected = m_sessionStats["commands_rejected"];
    uint64_t coldCompiles = m_sessionStats["shader_cold_compiles"];
    uint64_t cacheHits = m_sessionStats["shader_cache_hits"];

    std::ostringstream ss;
    ss << "\n================================================================================\n"
       << "  KeplerBridge Session Diagnostic Summary\n"
       << "================================================================================\n"
       << " [SESSION DURATION]  : " << std::fixed << std::setprecision(2) << elapsedSec << " seconds\n"
       << " [COMMANDS EXECUTED] : Translated: " << translated << " | Rejected: " << rejected << "\n"
       << " [SHADER PIPELINE]   : Cold Compiles: " << coldCompiles << " | Cache Hits: " << cacheHits << "\n"
       << " [DIAGNOSTIC EVENTS] : Fatal: " << fatalErr << " | Errors: " << errCount << " | Warnings: " << warnCount << "\n"
       << " [FINAL STATUS]      : " << (fatalErr > 0 ? "TERMINATED WITH FATAL ERRORS" : (errCount > 0 ? "COMPLETED WITH ERRORS" : "COMPLETED CLEANLY")) << "\n"
       << "================================================================================\n";

    if (m_logFile.is_open()) {
        m_logFile << ss.str();
        m_logFile.flush();
    }
}

std::string Logger::FeatureLevelToString(D3D_FEATURE_LEVEL fl) {
    std::ostringstream ss;
    switch (fl) {
        case D3D_FEATURE_LEVEL_1_0_CORE:
            ss << "D3D_FEATURE_LEVEL_1_0_CORE (0x1000)";
            break;
        case D3D_FEATURE_LEVEL_9_1:
            ss << "D3D_FEATURE_LEVEL_9_1 (0x9100)";
            break;
        case D3D_FEATURE_LEVEL_9_2:
            ss << "D3D_FEATURE_LEVEL_9_2 (0x9200)";
            break;
        case D3D_FEATURE_LEVEL_9_3:
            ss << "D3D_FEATURE_LEVEL_9_3 (0x9300)";
            break;
        case D3D_FEATURE_LEVEL_10_0:
            ss << "D3D_FEATURE_LEVEL_10_0 (0xa000)";
            break;
        case D3D_FEATURE_LEVEL_10_1:
            ss << "D3D_FEATURE_LEVEL_10_1 (0xa100)";
            break;
        case D3D_FEATURE_LEVEL_11_0:
            ss << "D3D_FEATURE_LEVEL_11_0 (0xb000)";
            break;
        case D3D_FEATURE_LEVEL_11_1:
            ss << "D3D_FEATURE_LEVEL_11_1 (0xb100)";
            break;
        case D3D_FEATURE_LEVEL_12_0:
            ss << "D3D_FEATURE_LEVEL_12_0 (0xc000)";
            break;
        case D3D_FEATURE_LEVEL_12_1:
            ss << "D3D_FEATURE_LEVEL_12_1 (0xc100)";
            break;
        case D3D_FEATURE_LEVEL_12_2:
            ss << "D3D_FEATURE_LEVEL_12_2 (0xc200)";
            break;
        default:
            ss << "D3D_FEATURE_LEVEL_UNKNOWN (0x" << std::hex << (uint32_t)fl << ")";
            break;
    }
    return ss.str();
}

std::string Logger::GuidToString(REFIID riid) {
    char guidStr[64];
    snprintf(guidStr, sizeof(guidStr),
             "{%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}",
             riid.Data1, riid.Data2, riid.Data3,
             riid.Data4[0], riid.Data4[1], riid.Data4[2], riid.Data4[3],
             riid.Data4[4], riid.Data4[5], riid.Data4[6], riid.Data4[7]);

    std::string name;
    if (InlineIsEqualGUID(riid, IID_IUnknown)) {
        name = "IUnknown";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Object))) {
        name = "ID3D12Object";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Device))) {
        name = "ID3D12Device";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Device1))) {
        name = "ID3D12Device1";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Device2))) {
        name = "ID3D12Device2";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Device3))) {
        name = "ID3D12Device3";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Device4))) {
        name = "ID3D12Device4";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Device5))) {
        name = "ID3D12Device5";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Device6))) {
        name = "ID3D12Device6";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Device7))) {
        name = "ID3D12Device7";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Device8))) {
        name = "ID3D12Device8";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12CommandQueue))) {
        name = "ID3D12CommandQueue";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12CommandAllocator))) {
        name = "ID3D12CommandAllocator";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12GraphicsCommandList))) {
        name = "ID3D12GraphicsCommandList";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Fence))) {
        name = "ID3D12Fence";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Resource))) {
        name = "ID3D12Resource";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12DescriptorHeap))) {
        name = "ID3D12DescriptorHeap";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12RootSignature))) {
        name = "ID3D12RootSignature";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12PipelineState))) {
        name = "ID3D12PipelineState";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGIFactory))) {
        name = "IDXGIFactory";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGIFactory1))) {
        name = "IDXGIFactory1";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGIFactory2))) {
        name = "IDXGIFactory2";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGIFactory3))) {
        name = "IDXGIFactory3";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGIFactory4))) {
        name = "IDXGIFactory4";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGIFactory5))) {
        name = "IDXGIFactory5";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGIFactory6))) {
        name = "IDXGIFactory6";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGISwapChain))) {
        name = "IDXGISwapChain";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGISwapChain1))) {
        name = "IDXGISwapChain1";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGISwapChain2))) {
        name = "IDXGISwapChain2";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGISwapChain3))) {
        name = "IDXGISwapChain3";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGISwapChain4))) {
        name = "IDXGISwapChain4";
    } else {
        name = "Unknown Interface";
    }

    return std::string(guidStr) + " (" + name + ")";
}

std::string Logger::HResultToString(HRESULT hr) {
    char hrStr[32];
    snprintf(hrStr, sizeof(hrStr), "0x%08lX", (unsigned long)hr);

    std::string name;
    switch (hr) {
        case S_OK:                              name = "S_OK"; break;
        case S_FALSE:                           name = "S_FALSE"; break;
        case E_FAIL:                            name = "E_FAIL"; break;
        case E_INVALIDARG:                      name = "E_INVALIDARG"; break;
        case E_OUTOFMEMORY:                     name = "E_OUTOFMEMORY"; break;
        case E_NOTIMPL:                         name = "E_NOTIMPL"; break;
        case E_NOINTERFACE:                     name = "E_NOINTERFACE"; break;
        case E_POINTER:                         name = "E_POINTER"; break;
        case DXGI_ERROR_UNSUPPORTED:            name = "DXGI_ERROR_UNSUPPORTED"; break;
        case DXGI_ERROR_NOT_FOUND:              name = "DXGI_ERROR_NOT_FOUND"; break;
        case DXGI_ERROR_MORE_DATA:              name = "DXGI_ERROR_MORE_DATA"; break;
        case DXGI_ERROR_DEVICE_REMOVED:         name = "DXGI_ERROR_DEVICE_REMOVED"; break;
        case DXGI_ERROR_DEVICE_RESET:           name = "DXGI_ERROR_DEVICE_RESET"; break;
        case DXGI_ERROR_DRIVER_INTERNAL_ERROR:  name = "DXGI_ERROR_DRIVER_INTERNAL_ERROR"; break;
        case DXGI_ERROR_INVALID_CALL:           name = "DXGI_ERROR_INVALID_CALL"; break;
        case D3D12_ERROR_ADAPTER_NOT_FOUND:     name = "D3D12_ERROR_ADAPTER_NOT_FOUND"; break;
        case D3D12_ERROR_DRIVER_VERSION_MISMATCH: name = "D3D12_ERROR_DRIVER_VERSION_MISMATCH"; break;
        default:                                name = "UNKNOWN_HRESULT"; break;
    }

    return std::string(hrStr) + " (" + name + ")";
}

std::string Logger::HexToString(uint64_t val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)val);
    return std::string(buf);
}

} // namespace kepler
