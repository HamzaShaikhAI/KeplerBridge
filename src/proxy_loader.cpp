#include "core/proxy_loader.h"
#include "core/feature_level_compat.h"
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
    std::lock_guard<std::mutex> lock(m_initMutex);
    if (m_initialized) {
        return true;
    }

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

    // Standard D3D12 exports
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

    // Extended system / Agility SDK exports
    RealD3D12CoreCreateLayeredDevice = reinterpret_cast<PFN_D3D12CoreCreateLayeredDevice>(
        GetProcAddress(m_hRealD3D12, "D3D12CoreCreateLayeredDevice"));
    RealD3D12CoreGetLayeredDeviceSize = reinterpret_cast<PFN_D3D12CoreGetLayeredDeviceSize>(
        GetProcAddress(m_hRealD3D12, "D3D12CoreGetLayeredDeviceSize"));
    RealD3D12CoreRegisterLayers = reinterpret_cast<PFN_D3D12CoreRegisterLayers>(
        GetProcAddress(m_hRealD3D12, "D3D12CoreRegisterLayers"));
    RealD3D12DeviceRemovedExtendedData = reinterpret_cast<PFN_D3D12DeviceRemovedExtendedData>(
        GetProcAddress(m_hRealD3D12, "D3D12DeviceRemovedExtendedData"));
    RealD3D12PIXEventsReplaceBlock = reinterpret_cast<PFN_D3D12PIXEventsReplaceBlock>(
        GetProcAddress(m_hRealD3D12, "D3D12PIXEventsReplaceBlock"));
    RealD3D12PIXGetThreadInfo = reinterpret_cast<PFN_D3D12PIXGetThreadInfo>(
        GetProcAddress(m_hRealD3D12, "D3D12PIXGetThreadInfo"));
    RealD3D12PIXNotifyWakeFromFenceSignal = reinterpret_cast<PFN_D3D12PIXNotifyWakeFromFenceSignal>(
        GetProcAddress(m_hRealD3D12, "D3D12PIXNotifyWakeFromFenceSignal"));
    RealD3D12PIXReportCounter = reinterpret_cast<PFN_D3D12PIXReportCounter>(
        GetProcAddress(m_hRealD3D12, "D3D12PIXReportCounter"));
    RealGetBehaviorValue = reinterpret_cast<PFN_GetBehaviorValue>(
        GetProcAddress(m_hRealD3D12, "GetBehaviorValue"));
    RealSetAppCompatStringPointer = reinterpret_cast<PFN_SetAppCompatStringPointer>(
        GetProcAddress(m_hRealD3D12, "SetAppCompatStringPointer"));

    if (!RealD3D12CreateDevice) {
        LOG_ERROR("Failed to resolve critical entry point: D3D12CreateDevice!");
        return false;
    }

    m_initialized = true;
    LOG_INFO("Successfully bound real System32 D3D12 entry points.");
    return true;
}

bool ProxyLoader::EnsureInitialized() {
    if (m_initialized) {
        return true;
    }
    return Initialize(nullptr);
}

void ProxyLoader::Shutdown() {
    std::lock_guard<std::mutex> lock(m_initMutex);
    if (m_hRealD3D12) {
        FreeLibrary(m_hRealD3D12);
        m_hRealD3D12 = nullptr;
    }
    m_initialized = false;
    RealD3D12CreateDevice = nullptr;
    RealD3D12GetDebugInterface = nullptr;
    RealD3D12SerializeRootSignature = nullptr;
    RealD3D12CreateRootSignatureDeserializer = nullptr;
    RealD3D12SerializeVersionedRootSignature = nullptr;
    RealD3D12CreateVersionedRootSignatureDeserializer = nullptr;
    RealD3D12GetInterface = nullptr;
    RealD3D12EnableExperimentalFeatures = nullptr;
    RealD3D12CoreCreateLayeredDevice = nullptr;
    RealD3D12CoreGetLayeredDeviceSize = nullptr;
    RealD3D12CoreRegisterLayers = nullptr;
    RealD3D12DeviceRemovedExtendedData = nullptr;
    RealD3D12PIXEventsReplaceBlock = nullptr;
    RealD3D12PIXGetThreadInfo = nullptr;
    RealD3D12PIXNotifyWakeFromFenceSignal = nullptr;
    RealD3D12PIXReportCounter = nullptr;
    RealGetBehaviorValue = nullptr;
    RealSetAppCompatStringPointer = nullptr;
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

    auto& loader = ProxyLoader::Instance();
    if (!loader.EnsureInitialized() || !loader.RealD3D12CreateDevice) {
        LOG_ERROR("CRITICAL: Real D3D12CreateDevice pointer is null!");
        return E_FAIL;
    }

    LOG_HOOK("=== [HOOK] D3D12CreateDevice Intercepted ===");

    // Inspect Adapter
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

    LOG_HOOK("Requested MinimumFeatureLevel: " + Logger::FeatureLevelToString(MinimumFeatureLevel));
    LOG_HOOK("Requested REFIID: " + Logger::GuidToString(riid));

    // Phase 2: Feature Level Compatibility Engine Policy Evaluation
    auto& compat = FeatureLevelCompat::Instance();
    D3D_FEATURE_LEVEL nativeMaxLevel = compat.DetectNativeMaxFeatureLevel(pAdapter);
    auto policy = compat.EvaluatePolicy(
        Config::Instance().IsFeatureLevelSpoofEnabled(),
        Config::Instance().GetTargetFeatureLevel(),
        nativeMaxLevel,
        MinimumFeatureLevel);

    LOG_INFO("[COMPAT] Policy Evaluation: " + policy.reason);
    if (policy.should_intercept) {
        LOG_HOOK("[COMPAT] Intercepting device creation: MinimumFeatureLevel " +
                 Logger::FeatureLevelToString(MinimumFeatureLevel) + " -> " +
                 Logger::FeatureLevelToString(policy.creation_level) +
                 " (Target Spoofed Level: " + Logger::FeatureLevelToString(policy.target_spoof_level) + ")");

        // If ppDevice is null, caller only tests if device creation would succeed
        if (!ppDevice) {
            HRESULT hrProbe = loader.RealD3D12CreateDevice(pAdapter, policy.creation_level, riid, nullptr);
            LOG_HOOK("Returned HRESULT (Probe): " + Logger::HResultToString(hrProbe));
            LOG_HOOK("============================================");
            return hrProbe;
        }

        // Create the underlying real hardware device at native capability level
        ID3D12Device* pRealDevice = nullptr;
        HRESULT hr = loader.RealD3D12CreateDevice(pAdapter, policy.creation_level, __uuidof(ID3D12Device), reinterpret_cast<void**>(&pRealDevice));
        LOG_HOOK("Returned HRESULT (Native Real): " + Logger::HResultToString(hr));

        if (FAILED(hr) || !pRealDevice) {
            LOG_DIAGNOSTIC("[DIAGNOSTIC] Real hardware rejected device creation! Return code: " + Logger::HResultToString(hr));
            *ppDevice = nullptr;
            LOG_HOOK("============================================");
            return hr;
        }

        // Wrap the real device in our ABI-safe COM compatibility wrapper
        void* pWrapped = nullptr;
        HRESULT hrWrap = compat.WrapDevice(pRealDevice, policy.target_spoof_level, riid, &pWrapped);
        pRealDevice->Release(); // WrapDevice holds internal reference; release the creation ref

        if (SUCCEEDED(hrWrap) && pWrapped) {
            *ppDevice = pWrapped;
            LOG_HOOK("[COMPAT] Successfully wrapped ID3D12Device with COM compatibility wrapper.");
            LOG_HOOK("============================================");
            return S_OK;
        } else {
            LOG_ERROR("[COMPAT] Failed to wrap ID3D12Device: " + Logger::HResultToString(hrWrap));
            *ppDevice = nullptr;
            LOG_HOOK("============================================");
            return hrWrap;
        }
    }

    // Native passthrough path (unmodified call to real d3d12.dll)
    HRESULT hr = loader.RealD3D12CreateDevice(pAdapter, MinimumFeatureLevel, riid, ppDevice);
    LOG_HOOK("Returned HRESULT: " + Logger::HResultToString(hr));

    if (FAILED(hr)) {
        LOG_DIAGNOSTIC("[DIAGNOSTIC] Hardware rejected device creation! Return code: " + Logger::HResultToString(hr));
    } else {
        LOG_INFO("Real D3D12CreateDevice succeeded. Device pointer: " +
                 (ppDevice && *ppDevice ? std::string("Non-null") : std::string("null")));
    }

    LOG_HOOK("============================================");
    return hr;
}

HRESULT WINAPI D3D12GetDebugInterface(REFIID riid, void** ppvDebug) {
    using namespace kepler;
    auto& loader = ProxyLoader::Instance();
    if (!loader.EnsureInitialized() || !loader.RealD3D12GetDebugInterface) {
        return E_FAIL;
    }
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12GetDebugInterface called (riid: " + Logger::GuidToString(riid) + ")");
    }
    return loader.RealD3D12GetDebugInterface(riid, ppvDebug);
}

HRESULT WINAPI D3D12SerializeRootSignature(
    const D3D12_ROOT_SIGNATURE_DESC* pRootSignature,
    D3D_ROOT_SIGNATURE_VERSION Version,
    ID3DBlob** ppBlob,
    ID3DBlob** ppErrorBlob)
{
    using namespace kepler;
    auto& loader = ProxyLoader::Instance();
    if (!loader.EnsureInitialized() || !loader.RealD3D12SerializeRootSignature) {
        return E_FAIL;
    }
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12SerializeRootSignature called (Version=" + std::to_string(Version) + ")");
    }
    return loader.RealD3D12SerializeRootSignature(pRootSignature, Version, ppBlob, ppErrorBlob);
}

HRESULT WINAPI D3D12CreateRootSignatureDeserializer(
    LPCVOID pSrcData,
    SIZE_T SrcDataSizeInBytes,
    REFIID pRootSignatureDeserializerInterface,
    void** ppvRootSignatureDeserializer)
{
    using namespace kepler;
    auto& loader = ProxyLoader::Instance();
    if (!loader.EnsureInitialized() || !loader.RealD3D12CreateRootSignatureDeserializer) {
        return E_FAIL;
    }
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12CreateRootSignatureDeserializer called");
    }
    return loader.RealD3D12CreateRootSignatureDeserializer(
        pSrcData, SrcDataSizeInBytes, pRootSignatureDeserializerInterface, ppvRootSignatureDeserializer);
}

HRESULT WINAPI D3D12SerializeVersionedRootSignature(
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pRootSignatureDesc,
    ID3DBlob** ppBlob,
    ID3DBlob** ppErrorBlob)
{
    using namespace kepler;
    auto& loader = ProxyLoader::Instance();
    if (!loader.EnsureInitialized() || !loader.RealD3D12SerializeVersionedRootSignature) {
        return E_FAIL;
    }
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12SerializeVersionedRootSignature called");
    }
    return loader.RealD3D12SerializeVersionedRootSignature(pRootSignatureDesc, ppBlob, ppErrorBlob);
}

HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(
    LPCVOID pSrcData,
    SIZE_T SrcDataSizeInBytes,
    REFIID pRootSignatureDeserializerInterface,
    void** ppvRootSignatureDeserializer)
{
    using namespace kepler;
    auto& loader = ProxyLoader::Instance();
    if (!loader.EnsureInitialized() || !loader.RealD3D12CreateVersionedRootSignatureDeserializer) {
        return E_FAIL;
    }
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12CreateVersionedRootSignatureDeserializer called");
    }
    return loader.RealD3D12CreateVersionedRootSignatureDeserializer(
        pSrcData, SrcDataSizeInBytes, pRootSignatureDeserializerInterface, ppvRootSignatureDeserializer);
}

HRESULT WINAPI D3D12GetInterface(REFCLSID rclsid, REFIID riid, void** ppvInterface) {
    using namespace kepler;
    auto& loader = ProxyLoader::Instance();
    if (!loader.EnsureInitialized() || !loader.RealD3D12GetInterface) {
        return E_FAIL;
    }
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("D3D12GetInterface called");
    }
    return loader.RealD3D12GetInterface(rclsid, riid, ppvInterface);
}

HRESULT WINAPI D3D12EnableExperimentalFeatures(
    UINT NumFeatures,
    const IID* pIIDs,
    void* pConfigurationStructs,
    UINT* pConfigurationStructSizes)
{
    using namespace kepler;
    auto& loader = ProxyLoader::Instance();
    if (!loader.EnsureInitialized() || !loader.RealD3D12EnableExperimentalFeatures) {
        return E_FAIL;
    }
    LOG_HOOK("D3D12EnableExperimentalFeatures called (NumFeatures=" + std::to_string(NumFeatures) + ")");
    return loader.RealD3D12EnableExperimentalFeatures(
        NumFeatures, pIIDs, pConfigurationStructs, pConfigurationStructSizes);
}

// ============================================================================
// Extended Windows 11 / Agility SDK Exports Forwarding
// ============================================================================

HRESULT WINAPI D3D12CoreCreateLayeredDevice(const void* a, const void* b, REFIID riid, void** ppv) {
    auto& loader = kepler::ProxyLoader::Instance();
    if (!loader.EnsureInitialized() || !loader.RealD3D12CoreCreateLayeredDevice) {
        return E_NOTIMPL;
    }
    return loader.RealD3D12CoreCreateLayeredDevice(a, b, riid, ppv);
}

SIZE_T WINAPI D3D12CoreGetLayeredDeviceSize(const void* a, const void* b) {
    auto& loader = kepler::ProxyLoader::Instance();
    if (!loader.EnsureInitialized() || !loader.RealD3D12CoreGetLayeredDeviceSize) {
        return 0;
    }
    return loader.RealD3D12CoreGetLayeredDeviceSize(a, b);
}

HRESULT WINAPI D3D12CoreRegisterLayers(const void* a, const void* b) {
    auto& loader = kepler::ProxyLoader::Instance();
    if (!loader.EnsureInitialized() || !loader.RealD3D12CoreRegisterLayers) {
        return E_NOTIMPL;
    }
    return loader.RealD3D12CoreRegisterLayers(a, b);
}

HRESULT WINAPI D3D12DeviceRemovedExtendedData(void* pData) {
    auto& loader = kepler::ProxyLoader::Instance();
    if (!loader.EnsureInitialized() || !loader.RealD3D12DeviceRemovedExtendedData) {
        return E_NOTIMPL;
    }
    return loader.RealD3D12DeviceRemovedExtendedData(pData);
}

void WINAPI D3D12PIXEventsReplaceBlock(void* pBlock) {
    auto& loader = kepler::ProxyLoader::Instance();
    if (loader.EnsureInitialized() && loader.RealD3D12PIXEventsReplaceBlock) {
        loader.RealD3D12PIXEventsReplaceBlock(pBlock);
    }
}

void* WINAPI D3D12PIXGetThreadInfo() {
    auto& loader = kepler::ProxyLoader::Instance();
    if (loader.EnsureInitialized() && loader.RealD3D12PIXGetThreadInfo) {
        return loader.RealD3D12PIXGetThreadInfo();
    }
    return nullptr;
}

void WINAPI D3D12PIXNotifyWakeFromFenceSignal(void* pFence) {
    auto& loader = kepler::ProxyLoader::Instance();
    if (loader.EnsureInitialized() && loader.RealD3D12PIXNotifyWakeFromFenceSignal) {
        loader.RealD3D12PIXNotifyWakeFromFenceSignal(pFence);
    }
}

void WINAPI D3D12PIXReportCounter(void* pCounter, UINT64 value) {
    auto& loader = kepler::ProxyLoader::Instance();
    if (loader.EnsureInitialized() && loader.RealD3D12PIXReportCounter) {
        loader.RealD3D12PIXReportCounter(pCounter, value);
    }
}

UINT WINAPI GetBehaviorValue(UINT feature) {
    auto& loader = kepler::ProxyLoader::Instance();
    if (loader.EnsureInitialized() && loader.RealGetBehaviorValue) {
        return loader.RealGetBehaviorValue(feature);
    }
    return 0;
}

HRESULT WINAPI SetAppCompatStringPointer(UINT index, PCWSTR str) {
    auto& loader = kepler::ProxyLoader::Instance();
    if (loader.EnsureInitialized() && loader.RealSetAppCompatStringPointer) {
        return loader.RealSetAppCompatStringPointer(index, str);
    }
    return E_NOTIMPL;
}

} // extern "C"
