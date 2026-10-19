#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>

#include <mutex>

namespace kepler {

typedef HRESULT (WINAPI *PFN_D3D12CreateDevice)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12GetDebugInterface)(REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12SerializeRootSignature)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
typedef HRESULT (WINAPI *PFN_D3D12CreateRootSignatureDeserializer)(LPCVOID, SIZE_T, REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12SerializeVersionedRootSignature)(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*, ID3DBlob**, ID3DBlob**);
typedef HRESULT (WINAPI *PFN_D3D12CreateVersionedRootSignatureDeserializer)(LPCVOID, SIZE_T, REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12GetInterface)(REFCLSID, REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12EnableExperimentalFeatures)(UINT, const IID*, void*, UINT*);

// Agility SDK and diagnostics entry points
typedef HRESULT (WINAPI *PFN_D3D12CoreCreateLayeredDevice)(const void*, const void*, REFIID, void**);
typedef SIZE_T  (WINAPI *PFN_D3D12CoreGetLayeredDeviceSize)(const void*, const void*);
typedef HRESULT (WINAPI *PFN_D3D12CoreRegisterLayers)(const void*, const void*);
typedef HRESULT (WINAPI *PFN_D3D12DeviceRemovedExtendedData)(void*);
typedef void    (WINAPI *PFN_D3D12PIXEventsReplaceBlock)(void*);
typedef void*   (WINAPI *PFN_D3D12PIXGetThreadInfo)();
typedef void    (WINAPI *PFN_D3D12PIXNotifyWakeFromFenceSignal)(void*);
typedef void    (WINAPI *PFN_D3D12PIXReportCounter)(void*, UINT64);
typedef UINT    (WINAPI *PFN_GetBehaviorValue)(UINT);
typedef HRESULT (WINAPI *PFN_SetAppCompatStringPointer)(UINT, PCWSTR);

class ProxyLoader {
public:
    static ProxyLoader& Instance();

    bool Initialize(HMODULE ourModule);
    bool EnsureInitialized();
    void Shutdown();

    bool IsLoaded() const { return m_hRealD3D12 != nullptr; }

    // Core D3D12 Entry Points
    PFN_D3D12CreateDevice RealD3D12CreateDevice = nullptr;
    PFN_D3D12GetDebugInterface RealD3D12GetDebugInterface = nullptr;
    PFN_D3D12SerializeRootSignature RealD3D12SerializeRootSignature = nullptr;
    PFN_D3D12CreateRootSignatureDeserializer RealD3D12CreateRootSignatureDeserializer = nullptr;
    PFN_D3D12SerializeVersionedRootSignature RealD3D12SerializeVersionedRootSignature = nullptr;
    PFN_D3D12CreateVersionedRootSignatureDeserializer RealD3D12CreateVersionedRootSignatureDeserializer = nullptr;
    PFN_D3D12GetInterface RealD3D12GetInterface = nullptr;
    PFN_D3D12EnableExperimentalFeatures RealD3D12EnableExperimentalFeatures = nullptr;

    // Agility SDK and system exports
    PFN_D3D12CoreCreateLayeredDevice RealD3D12CoreCreateLayeredDevice = nullptr;
    PFN_D3D12CoreGetLayeredDeviceSize RealD3D12CoreGetLayeredDeviceSize = nullptr;
    PFN_D3D12CoreRegisterLayers RealD3D12CoreRegisterLayers = nullptr;
    PFN_D3D12DeviceRemovedExtendedData RealD3D12DeviceRemovedExtendedData = nullptr;
    PFN_D3D12PIXEventsReplaceBlock RealD3D12PIXEventsReplaceBlock = nullptr;
    PFN_D3D12PIXGetThreadInfo RealD3D12PIXGetThreadInfo = nullptr;
    PFN_D3D12PIXNotifyWakeFromFenceSignal RealD3D12PIXNotifyWakeFromFenceSignal = nullptr;
    PFN_D3D12PIXReportCounter RealD3D12PIXReportCounter = nullptr;
    PFN_GetBehaviorValue RealGetBehaviorValue = nullptr;
    PFN_SetAppCompatStringPointer RealSetAppCompatStringPointer = nullptr;

private:
    ProxyLoader() = default;
    ~ProxyLoader();
    ProxyLoader(const ProxyLoader&) = delete;
    ProxyLoader& operator=(const ProxyLoader&) = delete;

    HMODULE m_hRealD3D12 = nullptr;
    HMODULE m_ourModule = nullptr;
    std::mutex m_initMutex;
    bool m_initialized = false;
};

} // namespace kepler

// Exported standard D3D12 entry points
extern "C" {
    HRESULT WINAPI D3D12CreateDevice(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void** ppDevice);
    HRESULT WINAPI D3D12GetDebugInterface(REFIID riid, void** ppvDebug);
    HRESULT WINAPI D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC* pRootSignature, D3D_ROOT_SIGNATURE_VERSION Version, ID3DBlob** ppBlob, ID3DBlob** ppErrorBlob);
    HRESULT WINAPI D3D12CreateRootSignatureDeserializer(LPCVOID pSrcData, SIZE_T SrcDataSizeInBytes, REFIID pRootSignatureDeserializerInterface, void** ppvRootSignatureDeserializer);
    HRESULT WINAPI D3D12SerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pRootSignatureDesc, ID3DBlob** ppBlob, ID3DBlob** ppErrorBlob);
    HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(LPCVOID pSrcData, SIZE_T SrcDataSizeInBytes, REFIID pRootSignatureDeserializerInterface, void** ppvRootSignatureDeserializer);
    HRESULT WINAPI D3D12GetInterface(REFCLSID rclsid, REFIID riid, void** ppvInterface);
    HRESULT WINAPI D3D12EnableExperimentalFeatures(UINT NumFeatures, const IID* pIIDs, void* pConfigurationStructs, UINT* pConfigurationStructSizes);

    // Additional Windows 11 / Agility SDK Exports
    HRESULT WINAPI D3D12CoreCreateLayeredDevice(const void* a, const void* b, REFIID riid, void** ppv);
    SIZE_T  WINAPI D3D12CoreGetLayeredDeviceSize(const void* a, const void* b);
    HRESULT WINAPI D3D12CoreRegisterLayers(const void* a, const void* b);
    HRESULT WINAPI D3D12DeviceRemovedExtendedData(void* pData);
    void    WINAPI D3D12PIXEventsReplaceBlock(void* pBlock);
    void*   WINAPI D3D12PIXGetThreadInfo();
    void    WINAPI D3D12PIXNotifyWakeFromFenceSignal(void* pFence);
    void    WINAPI D3D12PIXReportCounter(void* pCounter, UINT64 value);
    UINT    WINAPI GetBehaviorValue(UINT feature);
    HRESULT WINAPI SetAppCompatStringPointer(UINT index, PCWSTR str);
}
