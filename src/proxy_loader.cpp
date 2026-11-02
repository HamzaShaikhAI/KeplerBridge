#include "core/proxy_loader.h"
#include "core/feature_level_compat.h"
#include "core/logger.h"
#include "core/config.h"
#include "core/profile_manager.h"
#include "translator/d3d11_backend.h"
#include "translator/resource_manager.h"
#include "translator/wrapped_d3d12_device_factory.h"

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

void ProxyLoader::Shutdown(bool isProcessDetach) {
    std::lock_guard<std::mutex> lock(m_initMutex);
    // Note: Do not call FreeLibrary(m_hRealD3D12) here, as Shutdown is invoked from
    // DllMain (DLL_PROCESS_DETACH) under OS loader lock, which can deadlock graphics driver detach hooks.
    m_hRealD3D12 = nullptr;
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
    RuntimeTelemetry::Instance().RecordApiCall("D3D12CreateDevice");

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

    bool needsWrapper = policy.should_intercept ||
                        Config::Instance().IsD3D11TranslationEnabled() ||
                        Config::Instance().IsTextureClampingEnabled() ||
                        Config::Instance().IsShaderFallbackEnabled();

    LOG_INFO("[COMPAT] Policy Evaluation: " + policy.reason);
    if (Config::Instance().IsD3D11TranslationEnabled()) {
        LOG_INFO("[BACKEND] Active execution backend: Direct3D 11 Translation Engine.");
    } else {
        LOG_INFO("[BACKEND] Active execution backend: Direct3D 12 Native Hardware.");
    }

    if (needsWrapper) {
        D3D_FEATURE_LEVEL creationLevel = policy.creation_level;
        D3D_FEATURE_LEVEL spoofLevel = Config::Instance().IsFeatureLevelSpoofEnabled() ?
            policy.target_spoof_level :
            (policy.should_intercept ? policy.target_spoof_level : MinimumFeatureLevel);

        LOG_HOOK("[COMPAT] Intercepting device creation: MinimumFeatureLevel " +
                 Logger::FeatureLevelToString(MinimumFeatureLevel) + " -> " +
                 Logger::FeatureLevelToString(creationLevel) +
                 " (Target Spoofed Level: " + Logger::FeatureLevelToString(spoofLevel) + ")");

        // If ppDevice is null, caller only tests if device creation would succeed
        if (!ppDevice) {
            HRESULT hrProbe = loader.RealD3D12CreateDevice(pAdapter, creationLevel, riid, nullptr);
            LOG_HOOK("Returned HRESULT (Probe): " + Logger::HResultToString(hrProbe));
            LOG_HOOK("============================================");
            return hrProbe;
        }

        // Create the underlying real hardware device at native capability level
        ID3D12Device* pRealDevice = nullptr;
        HRESULT hr = loader.RealD3D12CreateDevice(pAdapter, creationLevel, __uuidof(ID3D12Device), reinterpret_cast<void**>(&pRealDevice));
        LOG_HOOK("Returned HRESULT (Native Real): " + Logger::HResultToString(hr));

        if (FAILED(hr) || !pRealDevice) {
            LOG_DIAGNOSTIC("[DIAGNOSTIC] Real hardware rejected device creation! Return code: " + Logger::HResultToString(hr));
            *ppDevice = nullptr;
            LOG_HOOK("============================================");
            return hr;
        }

        // Wrap the real device in our ABI-safe COM compatibility wrapper
        void* pWrapped = nullptr;
        HRESULT hrWrap = compat.WrapDevice(pRealDevice, spoofLevel, riid, &pWrapped);
        pRealDevice->Release(); // WrapDevice holds internal reference; release the creation ref

        if (SUCCEEDED(hrWrap) && pWrapped) {
            *ppDevice = pWrapped;
            RuntimeTelemetry::Instance().RecordD3D12DeviceCreated(creationLevel, spoofLevel, MinimumFeatureLevel);
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
    LOG_INFO("[BACKEND] Operating in raw native passthrough mode (wrapper bypassed).");
    HRESULT hr = loader.RealD3D12CreateDevice(pAdapter, MinimumFeatureLevel, riid, ppDevice);
    LOG_HOOK("Returned HRESULT: " + Logger::HResultToString(hr));

    if (FAILED(hr)) {
        LOG_DIAGNOSTIC("[DIAGNOSTIC] Hardware rejected device creation! Return code: " + Logger::HResultToString(hr));
    } else {
        if (ppDevice && *ppDevice) {
            RuntimeTelemetry::Instance().RecordD3D12DeviceCreated(MinimumFeatureLevel, MinimumFeatureLevel, MinimumFeatureLevel);
        }
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

static std::string ClsidToString(REFCLSID clsid) {
    if (clsid == CLSID_D3D12SDKConfiguration) return "CLSID_D3D12SDKConfiguration";
    if (clsid == CLSID_D3D12DeviceFactory) return "CLSID_D3D12DeviceFactory";
    static const GUID guidD3D12Debug = { 0xf2352aeb, 0xdd84, 0x49fe, { 0xb9, 0x7b, 0xa9, 0xdc, 0xfd, 0xcc, 0x1b, 0x4f } };
    static const GUID guidD3D12Tools = { 0xe38216b1, 0x3c8c, 0x4833, { 0xaa, 0x09, 0x0a, 0x06, 0xb6, 0x5d, 0x96, 0xc8 } };
    static const GUID guidD3D12Dred  = { 0x4a75bbc4, 0x9ff4, 0x4ad8, { 0x9f, 0x18, 0xab, 0xae, 0x84, 0xdc, 0x5f, 0xf2 } };
    if (clsid == guidD3D12Debug) return "CLSID_D3D12Debug";
    if (clsid == guidD3D12Tools) return "CLSID_D3D12Tools";
    if (clsid == guidD3D12Dred) return "CLSID_D3D12DeviceRemovedExtendedData";
    return kepler::Logger::GuidToString(clsid);
}

static std::string RiidToString(REFIID riid) {
    if (riid == IID_ID3D12SDKConfiguration) return "IID_ID3D12SDKConfiguration";
    if (riid == IID_ID3D12SDKConfiguration1) return "IID_ID3D12SDKConfiguration1";
    if (riid == IID_ID3D12DeviceFactory) return "IID_ID3D12DeviceFactory";
    if (riid == __uuidof(IUnknown)) return "IID_IUnknown";
    return kepler::Logger::GuidToString(riid);
}

HRESULT WINAPI D3D12GetInterface(REFCLSID rclsid, REFIID riid, void** ppvInterface) {
    using namespace kepler;
    auto& loader = ProxyLoader::Instance();
    if (!loader.EnsureInitialized()) {
        LOG_ERROR("[HOOK] D3D12GetInterface: ProxyLoader failed to initialize.");
        return E_FAIL;
    }

    if (!ppvInterface) {
        LOG_WARN("[HOOK] D3D12GetInterface: ppvInterface is null (E_POINTER).");
        return E_POINTER;
    }
    *ppvInterface = nullptr;

    std::string clsidStr = ClsidToString(rclsid);
    std::string riidStr = RiidToString(riid);

    LOG_HOOK("=== [HOOK] D3D12GetInterface Intercepted ===");
    LOG_HOOK("Requested CLSID: " + clsidStr + " | RIID: " + riidStr);

    // 1. SDK Configuration Interception (Agility SDK Dynamic Loader)
    if (rclsid == CLSID_D3D12SDKConfiguration) {
        LOG_HOOK("[SDKCONFIG] Direct SDK configuration query intercepted. Wrapping interface.");

        ID3D12SDKConfiguration* pRealConfig = nullptr;
        if (loader.RealD3D12GetInterface) {
            HRESULT hrReal = loader.RealD3D12GetInterface(rclsid, riid, reinterpret_cast<void**>(&pRealConfig));
            LOG_HOOK("[SDKCONFIG] Real D3D12GetInterface(CLSID_D3D12SDKConfiguration) returned: " +
                     Logger::HResultToString(hrReal));
        }

        auto* wrappedConfig = new (std::nothrow) WrappedD3D12SDKConfiguration1(pRealConfig);
        if (pRealConfig) {
            pRealConfig->Release();
        }

        if (!wrappedConfig) {
            LOG_ERROR("[SDKCONFIG] Failed to allocate WrappedD3D12SDKConfiguration1.");
            LOG_HOOK("============================================");
            return E_OUTOFMEMORY;
        }

        HRESULT hrQI = wrappedConfig->QueryInterface(riid, ppvInterface);
        wrappedConfig->Release();

        LOG_HOOK("[SDKCONFIG] Returning wrapped ID3D12SDKConfiguration interface. hr=" + Logger::HResultToString(hrQI));
        LOG_HOOK("============================================");
        return hrQI;
    }

    // 2. Device Factory Interception (Direct Factory Query)
    if (rclsid == CLSID_D3D12DeviceFactory || rclsid == IID_ID3D12DeviceFactory || riid == IID_ID3D12DeviceFactory) {
        LOG_HOOK("[FACTORY] Direct DeviceFactory query intercepted. Wrapping factory.");

        ID3D12DeviceFactory* pRealFactory = nullptr;
        if (loader.RealD3D12GetInterface) {
            HRESULT hrReal = loader.RealD3D12GetInterface(rclsid, riid, reinterpret_cast<void**>(&pRealFactory));
            LOG_HOOK("[FACTORY] Real D3D12GetInterface(DeviceFactory) returned: " +
                     Logger::HResultToString(hrReal));
        }

        auto* wrappedFactory = new (std::nothrow) WrappedD3D12DeviceFactory(pRealFactory);
        if (pRealFactory) {
            pRealFactory->Release();
        }

        if (!wrappedFactory) {
            LOG_ERROR("[FACTORY] Failed to allocate WrappedD3D12DeviceFactory.");
            LOG_HOOK("============================================");
            return E_OUTOFMEMORY;
        }

        HRESULT hrQI = wrappedFactory->QueryInterface(riid, ppvInterface);
        wrappedFactory->Release();

        LOG_HOOK("[FACTORY] Returning wrapped ID3D12DeviceFactory interface. hr=" + Logger::HResultToString(hrQI));
        LOG_HOOK("============================================");
        return hrQI;
    }

    // 3. Fallback Passthrough for Diagnostics / Tools / DRED / Debug
    if (!loader.RealD3D12GetInterface) {
        LOG_ERROR("[HOOK] Real D3D12GetInterface pointer is null!");
        LOG_HOOK("============================================");
        return E_FAIL;
    }

    LOG_HOOK("[PASSTHROUGH] Forwarding D3D12GetInterface to System32 D3D12.");
    HRESULT hr = loader.RealD3D12GetInterface(rclsid, riid, ppvInterface);
    LOG_HOOK("Returned HRESULT: " + Logger::HResultToString(hr));
    LOG_HOOK("============================================");
    return hr;
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

// ============================================================================
// KeplerBridge Diagnostic & Proof Exports
// ============================================================================

HRESULT WINAPI KeplerBridge_ReadbackRenderTarget(
    ID3D12Resource* pD3D12Resource,
    void* pOutPixels,
    UINT* pOutWidth,
    UINT* pOutHeight)
{
    using namespace kepler;
    if (!pD3D12Resource || !pOutPixels || !pOutWidth || !pOutHeight) {
        return E_POINTER;
    }
    std::vector<uint32_t> pixels;
    UINT w = 0, h = 0;
    HRESULT hr = D3D11Backend::Instance().ReadbackRenderTarget(pD3D12Resource, pixels, w, h);
    if (FAILED(hr)) return hr;

    *pOutWidth = w;
    *pOutHeight = h;
    memcpy(pOutPixels, pixels.data(), pixels.size() * sizeof(uint32_t));
    return S_OK;
}

HRESULT WINAPI KeplerBridge_ReadbackBuffer(
    ID3D12Resource* pD3D12Resource,
    void* pOutData,
    SIZE_T numBytes)
{
    using namespace kepler;
    if (!pD3D12Resource || !pOutData || numBytes == 0) {
        return E_POINTER;
    }
    return D3D11Backend::Instance().ReadbackBuffer(pD3D12Resource, pOutData, numBytes);
}

HRESULT WINAPI KeplerBridge_CreateSwapChainForHwnd(
    HWND hwnd,
    UINT width,
    UINT height,
    IDXGISwapChain** ppSwapChain)
{
    using namespace kepler;
    return D3D11Backend::Instance().CreateSwapChainForHwnd(hwnd, width, height, ppSwapChain);
}

HRESULT WINAPI KeplerBridge_PresentSwapChain(
    IDXGISwapChain* pSwapChain,
    UINT syncInterval,
    UINT flags)
{
    using namespace kepler;
    return D3D11Backend::Instance().PresentSwapChain(pSwapChain, syncInterval, flags);
}

void WINAPI KeplerBridge_GetTranslationStats(
    UINT* pTranslatedCount,
    UINT* pRejectedCount)
{
    using namespace kepler;
    if (pTranslatedCount) *pTranslatedCount = D3D11Backend::Instance().GetTranslatedCommandsCount();
    if (pRejectedCount) *pRejectedCount = D3D11Backend::Instance().GetRejectedCommandsCount();
}

void WINAPI KeplerBridge_GetSessionMetrics(
    UINT* pOutcome,
    UINT* pFramesPresented,
    UINT* pShadersTranslated,
    UINT* pShadersRejected,
    UINT* pBatchesExecuted)
{
    using namespace kepler;
    auto snap = RuntimeTelemetry::Instance().GetSnapshot();
    if (pOutcome) *pOutcome = static_cast<UINT>(snap.outcome);
    if (pFramesPresented) *pFramesPresented = static_cast<UINT>(snap.totalFramesPresented);
    if (pShadersTranslated) *pShadersTranslated = snap.dxilShadersTranslated;
    if (pShadersRejected) *pShadersRejected = snap.dxilShadersRejected;
    if (pBatchesExecuted) *pBatchesExecuted = snap.commandBatchesExecuted;
}

void WINAPI KeplerBridge_GetCausalMetrics(
    UINT64* pCausallyRenderedFrames,
    UINT64* pCausalTranslatedDraws,
    UINT64* pDuplicatePresentationsAvoided,
    UINT* pPhysicalFL,
    UINT* pVirtualizedFL)
{
    using namespace kepler;
    auto snap = RuntimeTelemetry::Instance().GetSnapshot();
    if (pCausallyRenderedFrames) *pCausallyRenderedFrames = snap.causallyRenderedFrames;
    if (pCausalTranslatedDraws) *pCausalTranslatedDraws = snap.causalTranslatedDrawsRecorded;
    if (pDuplicatePresentationsAvoided) *pDuplicatePresentationsAvoided = snap.duplicatePresentationsAvoided;
    if (pPhysicalFL) *pPhysicalFL = static_cast<UINT>(snap.physicalFeatureLevel);
    if (pVirtualizedFL) *pVirtualizedFL = static_cast<UINT>(snap.virtualizedFeatureLevel);
}

void WINAPI KeplerBridge_GetPresentationBreakdown(
    UINT64* pPresentAttempts,
    UINT64* pSuccessfulPresentCalls,
    UINT64* pOccludedPresentAttempts,
    UINT64* pVisiblePresentSuccesses,
    UINT32* pPresentFailures,
    UINT64* pPresentTestCalls,
    UINT64* pRenderedWorkSubmissions,
    UINT64* pCompletedTranslatedDraws,
    UINT64* pCausallyRenderedVisible,
    UINT64* pCausallyRenderedOccluded)
{
    using namespace kepler;
    auto snap = RuntimeTelemetry::Instance().GetSnapshot();
    if (pPresentAttempts) *pPresentAttempts = snap.presentAttempts;
    if (pSuccessfulPresentCalls) *pSuccessfulPresentCalls = snap.successfulPresentCalls;
    if (pOccludedPresentAttempts) *pOccludedPresentAttempts = snap.occludedPresentAttempts;
    if (pVisiblePresentSuccesses) *pVisiblePresentSuccesses = snap.visiblePresentSuccesses;
    if (pPresentFailures) *pPresentFailures = snap.presentFailures;
    if (pPresentTestCalls) *pPresentTestCalls = snap.presentTestCalls;
    if (pRenderedWorkSubmissions) *pRenderedWorkSubmissions = snap.renderedWorkSubmissions;
    if (pCompletedTranslatedDraws) *pCompletedTranslatedDraws = snap.completedTranslatedDraws;
    if (pCausallyRenderedVisible) *pCausallyRenderedVisible = snap.causallyRenderedVisibleFrames;
    if (pCausallyRenderedOccluded) *pCausallyRenderedOccluded = snap.causallyRenderedOccludedFrames;
}

} // extern "C"
