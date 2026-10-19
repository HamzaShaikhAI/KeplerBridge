#include "core/proxy_loader.h"
#include "core/logger.h"
#include "core/config.h"

#include <sstream>
#include <iomanip>

namespace kepler {

ProxyLoader& ProxyLoader::Instance() {
    static ProxyLoader s_instance;
    return s_instance;
}

ProxyLoader::~ProxyLoader() {
    Shutdown();
}

bool ProxyLoader::Initialize(HMODULE ourModule) {
    m_ourModule = ourModule;

    wchar_t systemDir[MAX_PATH] = { 0 };
    UINT len = GetSystemDirectoryW(systemDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        LOG_ERROR("Failed to query Windows System directory! GetLastError=" + std::to_string(GetLastError()));
        return false;
    }

    std::wstring d3d12Path = std::wstring(systemDir) + L"\\d3d12.dll";
    m_hRealD3D12 = LoadLibraryW(d3d12Path.c_str());
    if (!m_hRealD3D12) {
        std::string pathNarrow(d3d12Path.begin(), d3d12Path.end());
        LOG_ERROR("Failed to load real System32 D3D12 DLL from: " + pathNarrow + " (GetLastError=" + std::to_string(GetLastError()) + ")");
        return false;
    }

    // Guard against recursive self-loading
    if (m_hRealD3D12 == m_ourModule) {
        LOG_ERROR("CRITICAL ERROR: Self-loading recursion detected! The loaded module matches our own proxy DLL handle.");
        FreeLibrary(m_hRealD3D12);
        m_hRealD3D12 = nullptr;
        return false;
    }

    std::string pathNarrow(d3d12Path.begin(), d3d12Path.end());
    LOG_INFO("Loaded real system D3D12 library: " + pathNarrow);

    RealD3D12CreateDevice = reinterpret_cast<PFN_D3D12CreateDevice>(
        GetProcAddress(m_hRealD3D12, "D3D12CreateDevice"));
    RealD3D12GetDebugInterface = reinterpret_cast<PFN_D3D12GetDebugInterface>(
        GetProcAddress(m_hRealD3D12, "D3D12GetDebugInterface"));
    RealD3D12SerializeRootSignature = reinterpret_cast<PFN_D3D12SerializeRootSignature>(
        GetProcAddress(m_hRealD3D12, "D3D12SerializeRootSignature"));
    RealD3D12CreateRootSignatureDeserializer = reinterpret_cast<PFN_D3D12CreateRootSignatureDeserializer>(
        GetProcAddress(m_hRealD3D12, "D3D12CreateRootSignatureDeserializer"));
    RealD3D12SerializeVersionedRootSignature = reinterpret_cast<PFN_D3D12SerializeVersionedRootSignature>(
        GetProcAddress(m_hRealD3D12, "D3D12SerializeVersionedRootSignature"));
    RealD3D12CreateVersionedRootSignatureDeserializer = reinterpret_cast<PFN_D3D12CreateVersionedRootSignatureDeserializer>(
        GetProcAddress(m_hRealD3D12, "D3D12CreateVersionedRootSignatureDeserializer"));
    RealD3D12GetInterface = reinterpret_cast<PFN_D3D12GetInterface>(
        GetProcAddress(m_hRealD3D12, "D3D12GetInterface"));
    RealD3D12EnableExperimentalFeatures = reinterpret_cast<PFN_D3D12EnableExperimentalFeatures>(
        GetProcAddress(m_hRealD3D12, "D3D12EnableExperimentalFeatures"));

    if (!RealD3D12CreateDevice) {
        LOG_ERROR("Failed to resolve D3D12CreateDevice procedure address!");
        return false;
    }

    LOG_INFO("Successfully bound all real D3D12 entry points.");
    return true;
}

void ProxyLoader::Shutdown() {
    if (m_hRealD3D12) {
        FreeLibrary(m_hRealD3D12);
        m_hRealD3D12 = nullptr;
    }
    RealD3D12CreateDevice = nullptr;
    RealD3D12GetDebugInterface = nullptr;
    RealD3D12SerializeRootSignature = nullptr;
    RealD3D12CreateRootSignatureDeserializer = nullptr;
    RealD3D12SerializeVersionedRootSignature = nullptr;
    RealD3D12CreateVersionedRootSignatureDeserializer = nullptr;
    RealD3D12GetInterface = nullptr;
    RealD3D12EnableExperimentalFeatures = nullptr;
}

} // namespace kepler

// ============================================================================
// Exported Proxy Functions
// ============================================================================

extern "C" {

HRESULT WINAPI D3D12CreateDevice(
    IUnknown* pAdapter,
    D3D_FEATURE_LEVEL MinimumFeatureLevel,
    REFIID riid,
    void** ppDevice)
{
    using namespace kepler;

    LOG_HOOK("=== [HOOK] D3D12CreateDevice Intercepted ===");

    // Adapter Inspection
    if (pAdapter != nullptr) {
        IDXGIAdapter* pDXGIAdapter = nullptr;
        HRESULT hrQI = pAdapter->QueryInterface(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&pDXGIAdapter));
        if (SUCCEEDED(hrQI) && pDXGIAdapter != nullptr) {
            DXGI_ADAPTER_DESC desc;
            HRESULT hrDesc = pDXGIAdapter->GetDesc(&desc);
            if (SUCCEEDED(hrDesc)) {
                char descUtf8[256] = { 0 };
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, descUtf8, sizeof(descUtf8), nullptr, nullptr);
                uint64_t vramMB = desc.DedicatedVideoMemory / (1024 * 1024);

                std::ostringstream ss;
                ss << "Adapter: \"" << descUtf8 << "\""
                   << " | VendorId: 0x" << std::hex << std::uppercase << desc.VendorId
                   << " | DeviceId: 0x" << desc.DeviceId << std::dec
                   << " | Dedicated VRAM: " << vramMB << " MB";
                LOG_HOOK(ss.str());
            } else {
                LOG_WARN("Failed to obtain DXGI_ADAPTER_DESC: " + Logger::HResultToString(hrDesc));
            }
            pDXGIAdapter->Release();
            pDXGIAdapter = nullptr;
        } else {
            LOG_HOOK("Adapter: Non-null IUnknown* (QueryInterface(IDXGIAdapter) returned " + Logger::HResultToString(hrQI) + ")");
        }
    } else {
        LOG_HOOK("Adapter: nullptr (Default DXGI Adapter)");
    }

    // Log Requested Configuration
    LOG_HOOK("Requested MinimumFeatureLevel: " + Logger::FeatureLevelToString(MinimumFeatureLevel));
    LOG_HOOK("Requested REFIID: " + Logger::GuidToString(riid));

    // Forward to Real System32 D3D12
    auto& loader = ProxyLoader::Instance();
    if (!loader.RealD3D12CreateDevice) {
        LOG_ERROR("CRITICAL: Real D3D12CreateDevice pointer is null!");
        return E_FAIL;
    }

    HRESULT hr = loader.RealD3D12CreateDevice(pAdapter, MinimumFeatureLevel, riid, ppDevice);

    // Telemetry and Diagnostics
    LOG_HOOK("Returned HRESULT: " + Logger::HResultToString(hr));

    if (FAILED(hr)) {
        LOG_DIAGNOSTIC("[DIAGNOSTIC] Hardware rejected MinimumFeatureLevel! This is the exact lock Phase 2 will spoof.");
    } else {
        LOG_INFO("Real D3D12CreateDevice succeeded. Device pointer: " +
                 (ppDevice && *ppDevice ? std::string("Non-null") : std::string("null")));
    }

    LOG_HOOK("============================================");
    return hr;
}

HRESULT WINAPI D3D12GetDebugInterface(REFIID riid, void** ppvDebug) {
    using namespace kepler;
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12GetDebugInterface called (riid: " + Logger::GuidToString(riid) + ")");
    }

    auto& loader = ProxyLoader::Instance();
    if (!loader.RealD3D12GetDebugInterface) {
        LOG_ERROR("Real D3D12GetDebugInterface pointer is null!");
        return E_FAIL;
    }

    HRESULT hr = loader.RealD3D12GetDebugInterface(riid, ppvDebug);
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12GetDebugInterface returned: " + Logger::HResultToString(hr));
    }
    return hr;
}

HRESULT WINAPI D3D12SerializeRootSignature(
    const D3D12_ROOT_SIGNATURE_DESC* pRootSignature,
    D3D_ROOT_SIGNATURE_VERSION Version,
    ID3DBlob** ppBlob,
    ID3DBlob** ppErrorBlob)
{
    using namespace kepler;
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12SerializeRootSignature called (Version=" + std::to_string(Version) + ")");
    }

    auto& loader = ProxyLoader::Instance();
    if (!loader.RealD3D12SerializeRootSignature) {
        LOG_ERROR("Real D3D12SerializeRootSignature pointer is null!");
        return E_FAIL;
    }

    HRESULT hr = loader.RealD3D12SerializeRootSignature(pRootSignature, Version, ppBlob, ppErrorBlob);
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12SerializeRootSignature returned: " + Logger::HResultToString(hr));
    }
    return hr;
}

HRESULT WINAPI D3D12CreateRootSignatureDeserializer(
    LPCVOID pSrcData,
    SIZE_T SrcDataSizeInBytes,
    REFIID pRootSignatureDeserializerInterface,
    void** ppvRootSignatureDeserializer)
{
    using namespace kepler;
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12CreateRootSignatureDeserializer called (riid: " +
                 Logger::GuidToString(pRootSignatureDeserializerInterface) +
                 ", size: " + std::to_string(SrcDataSizeInBytes) + " bytes)");
    }

    auto& loader = ProxyLoader::Instance();
    if (!loader.RealD3D12CreateRootSignatureDeserializer) {
        LOG_ERROR("Real D3D12CreateRootSignatureDeserializer pointer is null!");
        return E_FAIL;
    }

    HRESULT hr = loader.RealD3D12CreateRootSignatureDeserializer(
        pSrcData, SrcDataSizeInBytes, pRootSignatureDeserializerInterface, ppvRootSignatureDeserializer);
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12CreateRootSignatureDeserializer returned: " + Logger::HResultToString(hr));
    }
    return hr;
}

HRESULT WINAPI D3D12SerializeVersionedRootSignature(
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pRootSignatureDesc,
    ID3DBlob** ppBlob,
    ID3DBlob** ppErrorBlob)
{
    using namespace kepler;
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12SerializeVersionedRootSignature called");
    }

    auto& loader = ProxyLoader::Instance();
    if (!loader.RealD3D12SerializeVersionedRootSignature) {
        LOG_ERROR("Real D3D12SerializeVersionedRootSignature pointer is null!");
        return E_FAIL;
    }

    HRESULT hr = loader.RealD3D12SerializeVersionedRootSignature(pRootSignatureDesc, ppBlob, ppErrorBlob);
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12SerializeVersionedRootSignature returned: " + Logger::HResultToString(hr));
    }
    return hr;
}

HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(
    LPCVOID pSrcData,
    SIZE_T SrcDataSizeInBytes,
    REFIID pRootSignatureDeserializerInterface,
    void** ppvRootSignatureDeserializer)
{
    using namespace kepler;
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12CreateVersionedRootSignatureDeserializer called (riid: " +
                 Logger::GuidToString(pRootSignatureDeserializerInterface) +
                 ", size: " + std::to_string(SrcDataSizeInBytes) + " bytes)");
    }

    auto& loader = ProxyLoader::Instance();
    if (!loader.RealD3D12CreateVersionedRootSignatureDeserializer) {
        LOG_ERROR("Real D3D12CreateVersionedRootSignatureDeserializer pointer is null!");
        return E_FAIL;
    }

    HRESULT hr = loader.RealD3D12CreateVersionedRootSignatureDeserializer(
        pSrcData, SrcDataSizeInBytes, pRootSignatureDeserializerInterface, ppvRootSignatureDeserializer);
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12CreateVersionedRootSignatureDeserializer returned: " + Logger::HResultToString(hr));
    }
    return hr;
}

HRESULT WINAPI D3D12GetInterface(REFCLSID rclsid, REFIID riid, void** ppvInterface) {
    using namespace kepler;
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12GetInterface called (rclsid: " + Logger::GuidToString(rclsid) +
                 ", riid: " + Logger::GuidToString(riid) + ")");
    }

    auto& loader = ProxyLoader::Instance();
    if (!loader.RealD3D12GetInterface) {
        LOG_ERROR("Real D3D12GetInterface pointer is null!");
        return E_FAIL;
    }

    HRESULT hr = loader.RealD3D12GetInterface(rclsid, riid, ppvInterface);
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12GetInterface returned: " + Logger::HResultToString(hr));
    }
    return hr;
}

HRESULT WINAPI D3D12EnableExperimentalFeatures(
    UINT NumFeatures,
    const IID* pIIDs,
    void* pConfigurationStructs,
    UINT* pConfigurationStructSizes)
{
    using namespace kepler;
    LOG_HOOK("D3D12EnableExperimentalFeatures called (NumFeatures=" + std::to_string(NumFeatures) + ")");

    auto& loader = ProxyLoader::Instance();
    if (!loader.RealD3D12EnableExperimentalFeatures) {
        LOG_ERROR("Real D3D12EnableExperimentalFeatures pointer is null!");
        return E_FAIL;
    }

    HRESULT hr = loader.RealD3D12EnableExperimentalFeatures(
        NumFeatures, pIIDs, pConfigurationStructs, pConfigurationStructSizes);
    LOG_HOOK("D3D12EnableExperimentalFeatures returned: " + Logger::HResultToString(hr));
    return hr;
}

} // extern "C"
