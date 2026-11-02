#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <atomic>
#include <string>

#ifndef CLSID_D3D12DeviceFactory
#define CLSID_D3D12DeviceFactory IID_ID3D12DeviceFactory
#endif

namespace kepler {

/**
 * @brief KeplerBridge Wrapped ID3D12DeviceFactory
 *
 * Intercepts Agility SDK and Direct3D 12 Independent Device Factory creation calls,
 * routing device creation through KeplerBridge's feature level compatibility policy
 * and D3D11 translation backend rather than bypassing it via raw driver instantiation.
 */
class WrappedD3D12DeviceFactory : public ID3D12DeviceFactory {
public:
    explicit WrappedD3D12DeviceFactory(ID3D12DeviceFactory* pRealFactory);
    virtual ~WrappedD3D12DeviceFactory();

    // IUnknown methods
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // ID3D12DeviceFactory methods
    HRESULT STDMETHODCALLTYPE InitializeFromGlobalState() override;
    HRESULT STDMETHODCALLTYPE ApplyToGlobalState() override;
    HRESULT STDMETHODCALLTYPE SetFlags(D3D12_DEVICE_FACTORY_FLAGS flags) override;
    D3D12_DEVICE_FACTORY_FLAGS STDMETHODCALLTYPE GetFlags() override;
    HRESULT STDMETHODCALLTYPE GetConfigurationInterface(REFCLSID clsid, REFIID iid, void** ppv) override;
    HRESULT STDMETHODCALLTYPE EnableExperimentalFeatures(
        UINT num_features,
        const IID* iids,
        void* configuration_structs,
        UINT* configuration_struct_sizes) override;
    HRESULT STDMETHODCALLTYPE CreateDevice(
        IUnknown* adapter,
        D3D_FEATURE_LEVEL feature_level,
        REFIID riid,
        void** ppv_device) override;

    ID3D12DeviceFactory* GetRealFactory() const { return m_pRealFactory; }

private:
    ID3D12DeviceFactory* m_pRealFactory{ nullptr };
    std::atomic<ULONG> m_refCount{ 1 };
    D3D12_DEVICE_FACTORY_FLAGS m_flags{ D3D12_DEVICE_FACTORY_FLAG_NONE };
};

/**
 * @brief KeplerBridge Wrapped ID3D12SDKConfiguration1
 *
 * Intercepts SDK configuration calls, specifically CreateDeviceFactory, ensuring that
 * all factories returned to the caller are wrapped by WrappedD3D12DeviceFactory.
 */
class WrappedD3D12SDKConfiguration1 : public ID3D12SDKConfiguration1 {
public:
    explicit WrappedD3D12SDKConfiguration1(ID3D12SDKConfiguration* pRealConfig);
    virtual ~WrappedD3D12SDKConfiguration1();

    // IUnknown methods
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // ID3D12SDKConfiguration methods
    HRESULT STDMETHODCALLTYPE SetSDKVersion(UINT version, const char* path) override;

    // ID3D12SDKConfiguration1 methods
    HRESULT STDMETHODCALLTYPE CreateDeviceFactory(
        UINT sdk_version,
        const char* sdk_path,
        REFIID riid,
        void** ppv) override;
    void STDMETHODCALLTYPE FreeUnusedSDKs() override;

    ID3D12SDKConfiguration* GetRealConfig() const { return m_pRealConfig; }
    ID3D12SDKConfiguration1* GetRealConfig1() const { return m_pRealConfig1; }

private:
    ID3D12SDKConfiguration* m_pRealConfig{ nullptr };
    ID3D12SDKConfiguration1* m_pRealConfig1{ nullptr };
    std::atomic<ULONG> m_refCount{ 1 };
};

} // namespace kepler
