#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>

namespace kepler {

typedef HRESULT (WINAPI *PFN_D3D12CreateDevice)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12GetDebugInterface)(REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12SerializeRootSignature)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
typedef HRESULT (WINAPI *PFN_D3D12CreateRootSignatureDeserializer)(LPCVOID, SIZE_T, REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12SerializeVersionedRootSignature)(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC*, ID3DBlob**, ID3DBlob**);
typedef HRESULT (WINAPI *PFN_D3D12CreateVersionedRootSignatureDeserializer)(LPCVOID, SIZE_T, REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12GetInterface)(REFCLSID, REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12EnableExperimentalFeatures)(UINT, const IID*, void*, UINT*);

class ProxyLoader {
public:
    static ProxyLoader& Instance();

    bool Initialize(HMODULE ourModule);
    void Shutdown();

    bool IsLoaded() const { return m_hRealD3D12 != nullptr; }

    PFN_D3D12CreateDevice RealD3D12CreateDevice = nullptr;
    PFN_D3D12GetDebugInterface RealD3D12GetDebugInterface = nullptr;
    PFN_D3D12SerializeRootSignature RealD3D12SerializeRootSignature = nullptr;
    PFN_D3D12CreateRootSignatureDeserializer RealD3D12CreateRootSignatureDeserializer = nullptr;
    PFN_D3D12SerializeVersionedRootSignature RealD3D12SerializeVersionedRootSignature = nullptr;
    PFN_D3D12CreateVersionedRootSignatureDeserializer RealD3D12CreateVersionedRootSignatureDeserializer = nullptr;
    PFN_D3D12GetInterface RealD3D12GetInterface = nullptr;
    PFN_D3D12EnableExperimentalFeatures RealD3D12EnableExperimentalFeatures = nullptr;

private:
    ProxyLoader() = default;
    ~ProxyLoader();
    ProxyLoader(const ProxyLoader&) = delete;
    ProxyLoader& operator=(const ProxyLoader&) = delete;

    HMODULE m_hRealD3D12 = nullptr;
    HMODULE m_ourModule = nullptr;
};

} // namespace kepler

// Standard D3D12 entry points exported by proxy
extern "C" {
    HRESULT WINAPI D3D12CreateDevice(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void** ppDevice);
    HRESULT WINAPI D3D12GetDebugInterface(REFIID riid, void** ppvDebug);
    HRESULT WINAPI D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC* pRootSignature, D3D_ROOT_SIGNATURE_VERSION Version, ID3DBlob** ppBlob, ID3DBlob** ppErrorBlob);
    HRESULT WINAPI D3D12CreateRootSignatureDeserializer(LPCVOID pSrcData, SIZE_T SrcDataSizeInBytes, REFIID pRootSignatureDeserializerInterface, void** ppvRootSignatureDeserializer);
    HRESULT WINAPI D3D12SerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pRootSignatureDesc, ID3DBlob** ppBlob, ID3DBlob** ppErrorBlob);
    HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(LPCVOID pSrcData, SIZE_T SrcDataSizeInBytes, REFIID pRootSignatureDeserializerInterface, void** ppvRootSignatureDeserializer);
    HRESULT WINAPI D3D12GetInterface(REFCLSID rclsid, REFIID riid, void** ppvInterface);
    HRESULT WINAPI D3D12EnableExperimentalFeatures(UINT NumFeatures, const IID* pIIDs, void* pConfigurationStructs, UINT* pConfigurationStructSizes);
}
