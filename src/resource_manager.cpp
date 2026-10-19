#include "translator/resource_manager.h"
#include "core/logger.h"

namespace kepler {

ResourceManager& ResourceManager::Instance() {
    static ResourceManager s_instance;
    return s_instance;
}

ResourceManager::~ResourceManager() {
    Clear();
}

void ResourceManager::RegisterResource(
    ID3D12Resource* pD3D12Resource,
    const D3D12_RESOURCE_DESC& desc,
    const D3D12_HEAP_PROPERTIES* pHeapProps,
    ID3D11Resource* pD3D11Resource)
{
    if (!pD3D12Resource) return;
    std::lock_guard<std::mutex> lock(m_mutex);

    TranslatedResourceInfo info;
    info.pD3D12Resource = pD3D12Resource;
    info.d3d12Desc = desc;
    if (pHeapProps) {
        info.heapProps = *pHeapProps;
        info.isUploadHeap = (pHeapProps->Type == D3D12_HEAP_TYPE_UPLOAD);
        info.isReadbackHeap = (pHeapProps->Type == D3D12_HEAP_TYPE_READBACK);
    }
    info.pD3D11Resource = pD3D11Resource;
    if (pD3D11Resource) {
        pD3D11Resource->AddRef();
        pD3D11Resource->QueryInterface(__uuidof(ID3D11Buffer), reinterpret_cast<void**>(&info.pD3D11Buffer));
        pD3D11Resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&info.pD3D11Texture));
    }
    m_resources[pD3D12Resource] = info;
}

void ResourceManager::UnregisterResource(ID3D12Resource* pD3D12Resource) {
    if (!pD3D12Resource) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_resources.find(pD3D12Resource);
    if (it != m_resources.end()) {
        if (it->second.pD3D11Buffer) it->second.pD3D11Buffer->Release();
        if (it->second.pD3D11Texture) it->second.pD3D11Texture->Release();
        if (it->second.pD3D11Resource) it->second.pD3D11Resource->Release();
        m_resources.erase(it);
    }
}

bool ResourceManager::GetResourceInfo(ID3D12Resource* pD3D12Resource, TranslatedResourceInfo& outInfo) {
    if (!pD3D12Resource) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_resources.find(pD3D12Resource);
    if (it != m_resources.end()) {
        outInfo = it->second;
        return true;
    }
    return false;
}

void ResourceManager::RegisterRTV(SIZE_T cpuHandlePtr, ID3D11RenderTargetView* pRTV) {
    if (!cpuHandlePtr) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (pRTV) pRTV->AddRef();
    auto it = m_rtvs.find(cpuHandlePtr);
    if (it != m_rtvs.end() && it->second) {
        it->second->Release();
    }
    m_rtvs[cpuHandlePtr] = pRTV;
}

ID3D11RenderTargetView* ResourceManager::GetRTV(SIZE_T cpuHandlePtr) {
    if (!cpuHandlePtr) return nullptr;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_rtvs.find(cpuHandlePtr);
    return (it != m_rtvs.end()) ? it->second : nullptr;
}

void ResourceManager::RegisterDSV(SIZE_T cpuHandlePtr, ID3D11DepthStencilView* pDSV) {
    if (!cpuHandlePtr) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (pDSV) pDSV->AddRef();
    auto it = m_dsvs.find(cpuHandlePtr);
    if (it != m_dsvs.end() && it->second) {
        it->second->Release();
    }
    m_dsvs[cpuHandlePtr] = pDSV;
}

ID3D11DepthStencilView* ResourceManager::GetDSV(SIZE_T cpuHandlePtr) {
    if (!cpuHandlePtr) return nullptr;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_dsvs.find(cpuHandlePtr);
    return (it != m_dsvs.end()) ? it->second : nullptr;
}

void ResourceManager::RegisterSRV(SIZE_T cpuHandlePtr, ID3D11ShaderResourceView* pSRV) {
    if (!cpuHandlePtr) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (pSRV) pSRV->AddRef();
    auto it = m_srvs.find(cpuHandlePtr);
    if (it != m_srvs.end() && it->second) {
        it->second->Release();
    }
    m_srvs[cpuHandlePtr] = pSRV;
}

ID3D11ShaderResourceView* ResourceManager::GetSRV(SIZE_T cpuHandlePtr) {
    if (!cpuHandlePtr) return nullptr;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_srvs.find(cpuHandlePtr);
    return (it != m_srvs.end()) ? it->second : nullptr;
}

void ResourceManager::RegisterUAV(SIZE_T cpuHandlePtr, ID3D11UnorderedAccessView* pUAV) {
    if (!cpuHandlePtr) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (pUAV) pUAV->AddRef();
    auto it = m_uavs.find(cpuHandlePtr);
    if (it != m_uavs.end() && it->second) {
        it->second->Release();
    }
    m_uavs[cpuHandlePtr] = pUAV;
}

ID3D11UnorderedAccessView* ResourceManager::GetUAV(SIZE_T cpuHandlePtr) {
    if (!cpuHandlePtr) return nullptr;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_uavs.find(cpuHandlePtr);
    return (it != m_uavs.end()) ? it->second : nullptr;
}

void ResourceManager::BindRootConstants(UINT rootParamIndex, const std::vector<uint32_t>& constants, bool isCompute) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (isCompute) {
        m_computeRootConstants[rootParamIndex] = constants;
    } else {
        m_graphicsRootConstants[rootParamIndex] = constants;
    }
}

const std::vector<uint32_t>* ResourceManager::GetRootConstants(UINT rootParamIndex, bool isCompute) const {
    if (isCompute) {
        auto it = m_computeRootConstants.find(rootParamIndex);
        return (it != m_computeRootConstants.end()) ? &it->second : nullptr;
    } else {
        auto it = m_graphicsRootConstants.find(rootParamIndex);
        return (it != m_graphicsRootConstants.end()) ? &it->second : nullptr;
    }
}

void ResourceManager::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_rtvs) {
        if (pair.second) pair.second->Release();
    }
    m_rtvs.clear();

    for (auto& pair : m_dsvs) {
        if (pair.second) pair.second->Release();
    }
    m_dsvs.clear();

    for (auto& pair : m_srvs) {
        if (pair.second) pair.second->Release();
    }
    m_srvs.clear();

    for (auto& pair : m_uavs) {
        if (pair.second) pair.second->Release();
    }
    m_uavs.clear();

    for (auto& pair : m_resources) {
        if (pair.second.pD3D11Buffer) pair.second.pD3D11Buffer->Release();
        if (pair.second.pD3D11Texture) pair.second.pD3D11Texture->Release();
        if (pair.second.pD3D11Resource) pair.second.pD3D11Resource->Release();
    }
    m_resources.clear();
    m_graphicsRootConstants.clear();
    m_computeRootConstants.clear();
}

} // namespace kepler
