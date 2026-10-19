#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3d11.h>

#include <unordered_map>
#include <mutex>
#include <memory>
#include <string>
#include <vector>

namespace kepler {

struct TranslatedResourceInfo {
    ID3D12Resource* pD3D12Resource = nullptr;
    D3D12_RESOURCE_DESC d3d12Desc = {};
    D3D12_HEAP_PROPERTIES heapProps = {};
    ID3D11Resource* pD3D11Resource = nullptr;
    ID3D11Buffer* pD3D11Buffer = nullptr;
    ID3D11Texture2D* pD3D11Texture = nullptr;
    bool isUploadHeap = false;
    bool isReadbackHeap = false;
};

class ResourceManager {
public:
    static ResourceManager& Instance();

    void RegisterResource(ID3D12Resource* pD3D12Resource,
                          const D3D12_RESOURCE_DESC& desc,
                          const D3D12_HEAP_PROPERTIES* pHeapProps,
                          ID3D11Resource* pD3D11Resource);
    void UnregisterResource(ID3D12Resource* pD3D12Resource);
    bool GetResourceInfo(ID3D12Resource* pD3D12Resource, TranslatedResourceInfo& outInfo);

    // Descriptor View Registration & Lookup
    void RegisterRTV(SIZE_T cpuHandlePtr, ID3D11RenderTargetView* pRTV);
    ID3D11RenderTargetView* GetRTV(SIZE_T cpuHandlePtr);

    void RegisterDSV(SIZE_T cpuHandlePtr, ID3D11DepthStencilView* pDSV);
    ID3D11DepthStencilView* GetDSV(SIZE_T cpuHandlePtr);

    void RegisterSRV(SIZE_T cpuHandlePtr, ID3D11ShaderResourceView* pSRV);
    ID3D11ShaderResourceView* GetSRV(SIZE_T cpuHandlePtr);

    void RegisterUAV(SIZE_T cpuHandlePtr, ID3D11UnorderedAccessView* pUAV);
    ID3D11UnorderedAccessView* GetUAV(SIZE_T cpuHandlePtr);

    // Root parameter mapping
    void BindRootConstants(UINT rootParamIndex, const std::vector<uint32_t>& constants, bool isCompute);
    const std::vector<uint32_t>* GetRootConstants(UINT rootParamIndex, bool isCompute) const;

    void Clear();

private:
    ResourceManager() = default;
    ~ResourceManager();
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    std::mutex m_mutex;
    std::unordered_map<ID3D12Resource*, TranslatedResourceInfo> m_resources;
    std::unordered_map<SIZE_T, ID3D11RenderTargetView*> m_rtvs;
    std::unordered_map<SIZE_T, ID3D11DepthStencilView*> m_dsvs;
    std::unordered_map<SIZE_T, ID3D11ShaderResourceView*> m_srvs;
    std::unordered_map<SIZE_T, ID3D11UnorderedAccessView*> m_uavs;

    std::unordered_map<UINT, std::vector<uint32_t>> m_graphicsRootConstants;
    std::unordered_map<UINT, std::vector<uint32_t>> m_computeRootConstants;
};

} // namespace kepler
