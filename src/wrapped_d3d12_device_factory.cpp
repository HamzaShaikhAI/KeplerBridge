#include "translator/wrapped_d3d12_device_factory.h"
#include "core/proxy_loader.h"
#include "core/feature_level_compat.h"
#include "core/logger.h"
#include "core/config.h"
#include "translator/d3d11_backend.h"

#include <dxgi.h>
#include <sstream>
#include <iomanip>

namespace kepler {

// ============================================================================
// WrappedD3D12DeviceFactory Implementation
// ============================================================================

WrappedD3D12DeviceFactory::WrappedD3D12DeviceFactory(ID3D12DeviceFactory* pRealFactory)
    : m_pRealFactory(pRealFactory)
    , m_refCount(1)
    , m_flags(D3D12_DEVICE_FACTORY_FLAG_NONE)
{
    if (m_pRealFactory) {
        m_pRealFactory->AddRef();
        m_flags = m_pRealFactory->GetFlags();
    }
    LOG_INFO("[FACTORY] WrappedD3D12DeviceFactory instantiated (Underlying real factory: " +
             std::string(m_pRealFactory ? "YES" : "NO") + ")");
}

WrappedD3D12DeviceFactory::~WrappedD3D12DeviceFactory() {
    if (m_pRealFactory) {
        m_pRealFactory->Release();
        m_pRealFactory = nullptr;
    }
    LOG_INFO("[FACTORY] WrappedD3D12DeviceFactory destroyed.");
}

HRESULT STDMETHODCALLTYPE WrappedD3D12DeviceFactory::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) {
        return E_POINTER;
    }
    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == IID_ID3D12DeviceFactory) {
        *ppvObject = static_cast<ID3D12DeviceFactory*>(this);
        AddRef();
        return S_OK;
    }

    if (m_pRealFactory) {
        return m_pRealFactory->QueryInterface(riid, ppvObject);
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WrappedD3D12DeviceFactory::AddRef() {
    return ++m_refCount;
}

ULONG STDMETHODCALLTYPE WrappedD3D12DeviceFactory::Release() {
    ULONG count = --m_refCount;
    if (count == 0) {
        delete this;
    }
    return count;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12DeviceFactory::InitializeFromGlobalState() {
    LOG_HOOK("[FACTORY] InitializeFromGlobalState called.");
    if (m_pRealFactory) {
        return m_pRealFactory->InitializeFromGlobalState();
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12DeviceFactory::ApplyToGlobalState() {
    LOG_HOOK("[FACTORY] ApplyToGlobalState called.");
    if (m_pRealFactory) {
        return m_pRealFactory->ApplyToGlobalState();
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12DeviceFactory::SetFlags(D3D12_DEVICE_FACTORY_FLAGS flags) {
    LOG_HOOK("[FACTORY] SetFlags called: 0x" + Logger::HResultToString(flags));
    m_flags = flags;
    if (m_pRealFactory) {
        return m_pRealFactory->SetFlags(flags);
    }
    return S_OK;
}

D3D12_DEVICE_FACTORY_FLAGS STDMETHODCALLTYPE WrappedD3D12DeviceFactory::GetFlags() {
    if (m_pRealFactory) {
        return m_pRealFactory->GetFlags();
    }
    return m_flags;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12DeviceFactory::GetConfigurationInterface(REFCLSID clsid, REFIID iid, void** ppv) {
    LOG_HOOK("[FACTORY] GetConfigurationInterface called: CLSID=" + Logger::GuidToString(clsid) +
             ", IID=" + Logger::GuidToString(iid));
    if (!ppv) {
        return E_POINTER;
    }
    *ppv = nullptr;

    if (m_pRealFactory) {
        return m_pRealFactory->GetConfigurationInterface(clsid, iid, ppv);
    }

    auto& loader = ProxyLoader::Instance();
    if (loader.RealD3D12GetInterface) {
        return loader.RealD3D12GetInterface(clsid, iid, ppv);
    }

    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12DeviceFactory::EnableExperimentalFeatures(
    UINT num_features,
    const IID* iids,
    void* configuration_structs,
    UINT* configuration_struct_sizes)
{
    LOG_HOOK("[FACTORY] EnableExperimentalFeatures called with " + std::to_string(num_features) + " features.");
    if (m_pRealFactory) {
        return m_pRealFactory->EnableExperimentalFeatures(num_features, iids, configuration_structs, configuration_struct_sizes);
    }
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12DeviceFactory::CreateDevice(
    IUnknown* adapter,
    D3D_FEATURE_LEVEL feature_level,
    REFIID riid,
    void** ppv_device)
{
    LOG_HOOK("=== [HOOK] ID3D12DeviceFactory::CreateDevice Intercepted ===");
    LOG_HOOK("Factory CreateDevice: Requested MinimumFeatureLevel: " + Logger::FeatureLevelToString(feature_level));
    LOG_HOOK("Factory CreateDevice: Requested REFIID: " + Logger::GuidToString(riid));

    // Inspect Adapter
    if (adapter != nullptr) {
        IDXGIAdapter* pDXGIAdapter = nullptr;
        HRESULT hrQI = adapter->QueryInterface(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&pDXGIAdapter));
        if (SUCCEEDED(hrQI) && pDXGIAdapter != nullptr) {
            DXGI_ADAPTER_DESC desc;
            HRESULT hrDesc = pDXGIAdapter->GetDesc(&desc);
            if (SUCCEEDED(hrDesc)) {
                char descUtf8[256] = { 0 };
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, descUtf8, sizeof(descUtf8), nullptr, nullptr);
                uint64_t vramMB = desc.DedicatedVideoMemory / (1024 * 1024);

                std::ostringstream ss;
                ss << "Factory Adapter: \"" << descUtf8 << "\""
                   << " | VendorId: 0x" << std::hex << std::uppercase << desc.VendorId
                   << " | DeviceId: 0x" << desc.DeviceId << std::dec
                   << " | Dedicated VRAM: " << vramMB << " MB";
                LOG_HOOK(ss.str());
            }
            pDXGIAdapter->Release();
        }
    } else {
        LOG_HOOK("Factory Adapter: nullptr (Default DXGI Adapter)");
    }

    auto& compat = FeatureLevelCompat::Instance();
    D3D_FEATURE_LEVEL nativeMaxLevel = compat.DetectNativeMaxFeatureLevel(adapter);
    auto policy = compat.EvaluatePolicy(
        Config::Instance().IsFeatureLevelSpoofEnabled(),
        Config::Instance().GetTargetFeatureLevel(),
        nativeMaxLevel,
        feature_level);

    bool needsWrapper = policy.should_intercept ||
                        Config::Instance().IsD3D11TranslationEnabled() ||
                        Config::Instance().IsTextureClampingEnabled() ||
                        Config::Instance().IsShaderFallbackEnabled();

    LOG_INFO("[COMPAT] Factory Policy Evaluation: " + policy.reason);

    if (needsWrapper) {
        // Interface validation: Ensure the requested interface is supported by WrappedD3D12Device
        if (!InlineIsEqualGUID(riid, IID_IUnknown) &&
            !InlineIsEqualGUID(riid, __uuidof(ID3D12Object)) &&
            !InlineIsEqualGUID(riid, __uuidof(ID3D12DeviceChild)) &&
            !InlineIsEqualGUID(riid, __uuidof(ID3D12Device)) &&
            !InlineIsEqualGUID(riid, __uuidof(ID3D12Device1)) &&
            !InlineIsEqualGUID(riid, __uuidof(ID3D12Device2)) &&
            !InlineIsEqualGUID(riid, __uuidof(ID3D12Device3)) &&
            !InlineIsEqualGUID(riid, __uuidof(ID3D12Device4)) &&
            !InlineIsEqualGUID(riid, __uuidof(ID3D12Device5)))
        {
            LOG_WARN("[FACTORY] Requested REFIID " + Logger::GuidToString(riid) +
                     " is an extended device interface unsupported by translation layer. Returning E_NOINTERFACE.");
            if (ppv_device) {
                *ppv_device = nullptr;
            }
            LOG_HOOK("============================================");
            return E_NOINTERFACE;
        }

        D3D_FEATURE_LEVEL creationLevel = policy.should_intercept ? policy.creation_level : feature_level;
        D3D_FEATURE_LEVEL spoofLevel = policy.should_intercept ? policy.target_spoof_level : feature_level;

        LOG_HOOK("[COMPAT] Factory intercepting device creation: feature_level " +
                 Logger::FeatureLevelToString(feature_level) + " -> " +
                 Logger::FeatureLevelToString(creationLevel) +
                 " (Target Spoofed Level: " + Logger::FeatureLevelToString(spoofLevel) + ")");

        // Probe call: caller is testing if device creation would succeed
        if (!ppv_device) {
            HRESULT hrProbe = E_FAIL;
            if (m_pRealFactory) {
                hrProbe = m_pRealFactory->CreateDevice(adapter, creationLevel, riid, nullptr);
            }
            if (FAILED(hrProbe)) {
                auto& loader = ProxyLoader::Instance();
                if (loader.EnsureInitialized() && loader.RealD3D12CreateDevice) {
                    hrProbe = loader.RealD3D12CreateDevice(adapter, creationLevel, riid, nullptr);
                }
            }
            LOG_HOOK("Returned HRESULT (Factory Probe): " + Logger::HResultToString(hrProbe));
            LOG_HOOK("============================================");
            return hrProbe;
        }

        // Create underlying hardware device
        ID3D12Device* pRealDevice = nullptr;
        HRESULT hr = E_FAIL;
        if (m_pRealFactory) {
            hr = m_pRealFactory->CreateDevice(adapter, creationLevel, __uuidof(ID3D12Device), reinterpret_cast<void**>(&pRealDevice));
        }
        if (FAILED(hr) || !pRealDevice) {
            auto& loader = ProxyLoader::Instance();
            if (loader.EnsureInitialized() && loader.RealD3D12CreateDevice) {
                hr = loader.RealD3D12CreateDevice(adapter, creationLevel, __uuidof(ID3D12Device), reinterpret_cast<void**>(&pRealDevice));
            }
        }

        if (FAILED(hr) || !pRealDevice) {
            LOG_DIAGNOSTIC("[DIAGNOSTIC] Factory real hardware rejected device creation! Return code: " + Logger::HResultToString(hr));
            *ppv_device = nullptr;
            LOG_HOOK("============================================");
            return hr;
        }

        void* pWrapped = nullptr;
        HRESULT hrWrap = compat.WrapDevice(pRealDevice, spoofLevel, riid, &pWrapped);
        pRealDevice->Release();

        if (SUCCEEDED(hrWrap) && pWrapped) {
            *ppv_device = pWrapped;
            LOG_HOOK("[COMPAT] Successfully wrapped ID3D12Device from ID3D12DeviceFactory with COM compatibility wrapper.");
            LOG_HOOK("============================================");
            return S_OK;
        } else {
            LOG_ERROR("[COMPAT] Failed to wrap factory ID3D12Device: " + Logger::HResultToString(hrWrap));
            *ppv_device = nullptr;
            LOG_HOOK("============================================");
            return hrWrap;
        }
    }

    // Native passthrough
    LOG_INFO("[BACKEND] Factory operating in raw native passthrough mode.");
    HRESULT hr = E_FAIL;
    if (m_pRealFactory) {
        hr = m_pRealFactory->CreateDevice(adapter, feature_level, riid, ppv_device);
    } else {
        auto& loader = ProxyLoader::Instance();
        if (loader.EnsureInitialized() && loader.RealD3D12CreateDevice) {
            hr = loader.RealD3D12CreateDevice(adapter, feature_level, riid, ppv_device);
        }
    }
    LOG_HOOK("Returned HRESULT: " + Logger::HResultToString(hr));
    LOG_HOOK("============================================");
    return hr;
}

// ============================================================================
// WrappedD3D12SDKConfiguration1 Implementation
// ============================================================================

WrappedD3D12SDKConfiguration1::WrappedD3D12SDKConfiguration1(ID3D12SDKConfiguration* pRealConfig)
    : m_pRealConfig(pRealConfig)
    , m_pRealConfig1(nullptr)
    , m_refCount(1)
{
    if (m_pRealConfig) {
        m_pRealConfig->AddRef();
        m_pRealConfig->QueryInterface(IID_ID3D12SDKConfiguration1, reinterpret_cast<void**>(&m_pRealConfig1));
    }
    LOG_INFO("[SDKCONFIG] WrappedD3D12SDKConfiguration1 instantiated (RealConfig: " +
             std::string(m_pRealConfig ? "YES" : "NO") +
             ", RealConfig1: " + std::string(m_pRealConfig1 ? "YES" : "NO") + ")");
}

WrappedD3D12SDKConfiguration1::~WrappedD3D12SDKConfiguration1() {
    if (m_pRealConfig1) {
        m_pRealConfig1->Release();
        m_pRealConfig1 = nullptr;
    }
    if (m_pRealConfig) {
        m_pRealConfig->Release();
        m_pRealConfig = nullptr;
    }
    LOG_INFO("[SDKCONFIG] WrappedD3D12SDKConfiguration1 destroyed.");
}

HRESULT STDMETHODCALLTYPE WrappedD3D12SDKConfiguration1::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) {
        return E_POINTER;
    }
    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == IID_ID3D12SDKConfiguration) {
        *ppvObject = static_cast<ID3D12SDKConfiguration*>(this);
        AddRef();
        return S_OK;
    }

    if (riid == IID_ID3D12SDKConfiguration1) {
        *ppvObject = static_cast<ID3D12SDKConfiguration1*>(this);
        AddRef();
        return S_OK;
    }

    if (m_pRealConfig1) {
        return m_pRealConfig1->QueryInterface(riid, ppvObject);
    }
    if (m_pRealConfig) {
        return m_pRealConfig->QueryInterface(riid, ppvObject);
    }

    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WrappedD3D12SDKConfiguration1::AddRef() {
    return ++m_refCount;
}

ULONG STDMETHODCALLTYPE WrappedD3D12SDKConfiguration1::Release() {
    ULONG count = --m_refCount;
    if (count == 0) {
        delete this;
    }
    return count;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12SDKConfiguration1::SetSDKVersion(UINT version, const char* path) {
    LOG_HOOK("[SDKCONFIG] SetSDKVersion called: Version=" + std::to_string(version) +
             ", Path=" + (path ? path : "nullptr"));
    if (m_pRealConfig) {
        return m_pRealConfig->SetSDKVersion(version, path);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12SDKConfiguration1::CreateDeviceFactory(
    UINT sdk_version,
    const char* sdk_path,
    REFIID riid,
    void** ppv)
{
    LOG_HOOK("=== [HOOK] ID3D12SDKConfiguration1::CreateDeviceFactory Intercepted ===");
    LOG_HOOK("[SDKCONFIG] SDKVersion: " + std::to_string(sdk_version) +
             " | SDKPath: " + (sdk_path ? sdk_path : "nullptr") +
             " | RIID: " + Logger::GuidToString(riid));

    if (!ppv) {
        return E_POINTER;
    }
    *ppv = nullptr;

    ID3D12DeviceFactory* pRealFactory = nullptr;
    HRESULT hrReal = E_FAIL;

    if (m_pRealConfig1) {
        hrReal = m_pRealConfig1->CreateDeviceFactory(sdk_version, sdk_path, riid, reinterpret_cast<void**>(&pRealFactory));
        LOG_HOOK("[SDKCONFIG] Real ID3D12SDKConfiguration1::CreateDeviceFactory returned: " +
                 Logger::HResultToString(hrReal));
    }

    if (FAILED(hrReal) || !pRealFactory) {
        LOG_WARN("[SDKCONFIG] Real CreateDeviceFactory failed (" + Logger::HResultToString(hrReal) +
                 "); creating standalone WrappedD3D12DeviceFactory with translation fallback.");
        pRealFactory = nullptr;
    }

    auto* wrappedFactory = new (std::nothrow) WrappedD3D12DeviceFactory(pRealFactory);
    if (pRealFactory) {
        pRealFactory->Release(); // WrappedD3D12DeviceFactory constructor added its own ref
    }

    if (!wrappedFactory) {
        return E_OUTOFMEMORY;
    }

    HRESULT hrQI = wrappedFactory->QueryInterface(riid, ppv);
    wrappedFactory->Release(); // Releases initial construction ref; QueryInterface holds caller's ref

    LOG_HOOK("[SDKCONFIG] CreateDeviceFactory returning wrapped factory. hr=" + Logger::HResultToString(hrQI));
    LOG_HOOK("============================================");
    return hrQI;
}

void STDMETHODCALLTYPE WrappedD3D12SDKConfiguration1::FreeUnusedSDKs() {
    LOG_HOOK("[SDKCONFIG] FreeUnusedSDKs called.");
    if (m_pRealConfig1) {
        m_pRealConfig1->FreeUnusedSDKs();
    }
}

} // namespace kepler
