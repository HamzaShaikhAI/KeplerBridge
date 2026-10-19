#include "core/logger.h"

#include <iomanip>
#include <sstream>
#include <iostream>
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

    m_logFile.open(logFilePath, std::ios::out | std::ios::trunc);
    if (!m_logFile.is_open()) {
        return false;
    }

    m_initialized = true;
    return true;
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized && m_logFile.is_open()) {
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

void Logger::Log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || !m_logFile.is_open()) {
        return;
    }

    DWORD tid = GetCurrentThreadId();
    m_logFile << GetTimestamp() << " [TID " << std::setw(5) << std::setfill('0') << tid << "] "
              << LevelToString(level) << " " << message << "\n";
    m_logFile.flush();
}

void Logger::LogRaw(const std::string& rawMessage) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || !m_logFile.is_open()) {
        return;
    }

    m_logFile << rawMessage << "\n";
    m_logFile.flush();
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
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Device9))) {
        name = "ID3D12Device9";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Device10))) {
        name = "ID3D12Device10";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Debug))) {
        name = "ID3D12Debug";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Debug1))) {
        name = "ID3D12Debug1";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Debug2))) {
        name = "ID3D12Debug2";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Debug3))) {
        name = "ID3D12Debug3";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Debug4))) {
        name = "ID3D12Debug4";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12Debug5))) {
        name = "ID3D12Debug5";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12CommandQueue))) {
        name = "ID3D12CommandQueue";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12CommandAllocator))) {
        name = "ID3D12CommandAllocator";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12GraphicsCommandList))) {
        name = "ID3D12GraphicsCommandList";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12RootSignature))) {
        name = "ID3D12RootSignature";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12RootSignatureDeserializer))) {
        name = "ID3D12RootSignatureDeserializer";
    } else if (InlineIsEqualGUID(riid, __uuidof(ID3D12VersionedRootSignatureDeserializer))) {
        name = "ID3D12VersionedRootSignatureDeserializer";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGIAdapter))) {
        name = "IDXGIAdapter";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGIAdapter1))) {
        name = "IDXGIAdapter1";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGIAdapter2))) {
        name = "IDXGIAdapter2";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGIAdapter3))) {
        name = "IDXGIAdapter3";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGIAdapter4))) {
        name = "IDXGIAdapter4";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGIFactory))) {
        name = "IDXGIFactory";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGIFactory1))) {
        name = "IDXGIFactory1";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGIFactory2))) {
        name = "IDXGIFactory2";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGIFactory4))) {
        name = "IDXGIFactory4";
    } else if (InlineIsEqualGUID(riid, __uuidof(IDXGIFactory6))) {
        name = "IDXGIFactory6";
    }

    if (!name.empty()) {
        return name + " (" + guidStr + ")";
    }
    return std::string(guidStr);
}

std::string Logger::HResultToString(HRESULT hr) {
    char hexBuf[32];
    snprintf(hexBuf, sizeof(hexBuf), "0x%08lX", (unsigned long)hr);

    std::string desc;
    switch (hr) {
        case S_OK:
            desc = "S_OK (Operation successful)";
            break;
        case S_FALSE:
            desc = "S_FALSE (Operation succeeded with non-standard result)";
            break;
        case E_NOTIMPL:
            desc = "E_NOTIMPL (Not implemented)";
            break;
        case E_NOINTERFACE:
            desc = "E_NOINTERFACE (No such interface supported)";
            break;
        case E_POINTER:
            desc = "E_POINTER (Invalid pointer)";
            break;
        case E_ABORT:
            desc = "E_ABORT (Operation aborted)";
            break;
        case E_FAIL:
            desc = "E_FAIL (Unspecified failure)";
            break;
        case E_ACCESSDENIED:
            desc = "E_ACCESSDENIED (General access denied error)";
            break;
        case E_HANDLE:
            desc = "E_HANDLE (Invalid handle)";
            break;
        case E_INVALIDARG:
            desc = "E_INVALIDARG (The parameter is incorrect)";
            break;
        case E_OUTOFMEMORY:
            desc = "E_OUTOFMEMORY (Ran out of memory)";
            break;
        case DXGI_ERROR_UNSUPPORTED:
            desc = "DXGI_ERROR_UNSUPPORTED (The specified device interface or feature level is not supported on this system)";
            break;
        case DXGI_ERROR_NOT_FOUND:
            desc = "DXGI_ERROR_NOT_FOUND (The item was not found)";
            break;
        case DXGI_ERROR_MORE_DATA:
            desc = "DXGI_ERROR_MORE_DATA (The buffer is not large enough to hold requested data)";
            break;
        case DXGI_ERROR_DEVICE_REMOVED:
            desc = "DXGI_ERROR_DEVICE_REMOVED (The video card has been physically removed or driver crashed)";
            break;
        case DXGI_ERROR_DEVICE_RESET:
            desc = "DXGI_ERROR_DEVICE_RESET (The device failed due to a badly formed command)";
            break;
        case DXGI_ERROR_DEVICE_HUNG:
            desc = "DXGI_ERROR_DEVICE_HUNG (The application device failed due to hung commands)";
            break;
        case DXGI_ERROR_INVALID_CALL:
            desc = "DXGI_ERROR_INVALID_CALL (The application provided invalid parameter data)";
            break;
        case DXGI_ERROR_WAS_STILL_DRAWING:
            desc = "DXGI_ERROR_WAS_STILL_DRAWING (The previous blit is still taking place)";
            break;
        case D3D12_ERROR_ADAPTER_NOT_FOUND:
            desc = "D3D12_ERROR_ADAPTER_NOT_FOUND (The specified adapter was not found)";
            break;
        case D3D12_ERROR_DRIVER_VERSION_MISMATCH:
            desc = "D3D12_ERROR_DRIVER_VERSION_MISMATCH (Driver version is mismatched)";
            break;
        default:
            desc = "Unknown HRESULT";
            break;
    }

    return std::string(hexBuf) + " - " + desc;
}

} // namespace kepler
