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
    ID3D11Resource* pD3D11Resource,
    D3D12_GPU_VIRTUAL_ADDRESS gpuVA)
{
    if (!pD3D12Resource) return;
    std::lock_guard<std::mutex> lock(m_mutex);

    TranslatedResourceInfo info;
    info.pD3D12Resource = pD3D12Resource;
    info.d3d12Desc = desc;
    info.gpuVA = gpuVA;
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

    if (gpuVA != 0 && desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
        m_gpuVaToBuffer.push_back({ gpuVA, pD3D12Resource });
    }
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

    for (auto vaIt = m_gpuVaToBuffer.begin(); vaIt != m_gpuVaToBuffer.end();) {
        if (vaIt->second == pD3D12Resource) {
            vaIt = m_gpuVaToBuffer.erase(vaIt);
        } else {
            ++vaIt;
        }
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

void ResourceManager::RegisterBufferGPUVA(ID3D12Resource* pD3D12Resource, D3D12_GPU_VIRTUAL_ADDRESS gpuVA) {
    if (!pD3D12Resource || gpuVA == 0) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_resources.find(pD3D12Resource);
    if (it != m_resources.end()) {
        it->second.gpuVA = gpuVA;
    }
    m_gpuVaToBuffer.push_back({ gpuVA, pD3D12Resource });
}

bool ResourceManager::FindBufferByGPUVA(D3D12_GPU_VIRTUAL_ADDRESS va, TranslatedResourceInfo& outInfo, UINT64& outByteOffset) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& pair : m_gpuVaToBuffer) {
        auto it = m_resources.find(pair.second);
        if (it != m_resources.end()) {
            UINT64 size = it->second.d3d12Desc.Width;
            if (va >= pair.first && va < pair.first + size) {
                outInfo = it->second;
                outByteOffset = va - pair.first;
                return true;
            }
        }
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
    TranslatedUAVInfo info = {};
    info.pUAV = pUAV;
    RegisterUAVInfo(cpuHandlePtr, info);
}

void ResourceManager::RegisterUAVInfo(SIZE_T cpuHandlePtr, const TranslatedUAVInfo& info) {
    if (!cpuHandlePtr) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (info.pUAV) info.pUAV->AddRef();
    auto it = m_uavs.find(cpuHandlePtr);
    if (it != m_uavs.end() && it->second.pUAV) {
        it->second.pUAV->Release();
    }
    m_uavs[cpuHandlePtr] = info;
}

ID3D11UnorderedAccessView* ResourceManager::GetUAV(SIZE_T cpuHandlePtr) {
    if (!cpuHandlePtr) return nullptr;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_uavs.find(cpuHandlePtr);
    return (it != m_uavs.end()) ? it->second.pUAV : nullptr;
}

bool ResourceManager::GetUAVInfo(SIZE_T cpuHandlePtr, TranslatedUAVInfo& outInfo) {
    if (!cpuHandlePtr) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_uavs.find(cpuHandlePtr);
    if (it != m_uavs.end()) {
        outInfo = it->second;
        return true;
    }
    return false;
}

void ResourceManager::SetUAVInitialCount(SIZE_T cpuHandlePtr, UINT count) {
    if (!cpuHandlePtr) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_uavs.find(cpuHandlePtr);
    if (it != m_uavs.end()) {
        it->second.initialCount = count;
    }
}

void ResourceManager::UpdateD3D11ResourceForBuffer(ID3D12Resource* pD3D12Resource, ID3D11Buffer* pNewBuffer) {
    if (!pD3D12Resource || !pNewBuffer) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_resources.find(pD3D12Resource);
    if (it != m_resources.end()) {
        if (it->second.pD3D11Buffer) it->second.pD3D11Buffer->Release();
        if (it->second.pD3D11Resource) it->second.pD3D11Resource->Release();

        pNewBuffer->AddRef();
        it->second.pD3D11Buffer = pNewBuffer;
        it->second.pD3D11Resource = pNewBuffer;
        it->second.pD3D11Resource->AddRef();
    }
}

void ResourceManager::RegisterCBV(SIZE_T cpuHandlePtr, ID3D11Buffer* pBuffer) {
    if (!cpuHandlePtr) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (pBuffer) pBuffer->AddRef();
    auto it = m_cbvs.find(cpuHandlePtr);
    if (it != m_cbvs.end() && it->second) {
        it->second->Release();
    }
    m_cbvs[cpuHandlePtr] = pBuffer;
}

ID3D11Buffer* ResourceManager::GetCBV(SIZE_T cpuHandlePtr) {
    if (!cpuHandlePtr) return nullptr;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cbvs.find(cpuHandlePtr);
    return (it != m_cbvs.end()) ? it->second : nullptr;
}

void ResourceManager::RegisterSampler(SIZE_T cpuHandlePtr, ID3D11SamplerState* pSampler) {
    if (!cpuHandlePtr) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (pSampler) pSampler->AddRef();
    auto it = m_samplers.find(cpuHandlePtr);
    if (it != m_samplers.end() && it->second) {
        it->second->Release();
    }
    m_samplers[cpuHandlePtr] = pSampler;
}

ID3D11SamplerState* ResourceManager::GetSampler(SIZE_T cpuHandlePtr) {
    if (!cpuHandlePtr) return nullptr;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_samplers.find(cpuHandlePtr);
    return (it != m_samplers.end()) ? it->second : nullptr;
}

void ResourceManager::RegisterRootSignature(ID3D12RootSignature* pRootSig, const TranslatedRootSignature& sig) {
    if (!pRootSig) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_rootSignatures[pRootSig] = sig;
}

void ResourceManager::UnregisterRootSignature(ID3D12RootSignature* pRootSig) {
    if (!pRootSig) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_rootSignatures.erase(pRootSig);
}

bool ResourceManager::GetRootSignature(ID3D12RootSignature* pRootSig, TranslatedRootSignature& outSig) {
    if (!pRootSig) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_rootSignatures.find(pRootSig);
    if (it != m_rootSignatures.end()) {
        outSig = it->second;
        return true;
    }
    return false;
}

void ResourceManager::RegisterPSO(ID3D12PipelineState* pD3D12PSO, const TranslatedPipelineState& translated) {
    if (!pD3D12PSO) return;
    std::lock_guard<std::mutex> lock(m_mutex);

    // Release old if overwriting
    auto it = m_psos.find(pD3D12PSO);
    if (it != m_psos.end()) {
        if (it->second.pVS) it->second.pVS->Release();
        if (it->second.pPS) it->second.pPS->Release();
        if (it->second.pCS) it->second.pCS->Release();
        if (it->second.pInputLayout) it->second.pInputLayout->Release();
        if (it->second.pRasterizerState) it->second.pRasterizerState->Release();
        if (it->second.pBlendState) it->second.pBlendState->Release();
        if (it->second.pDepthStencilState) it->second.pDepthStencilState->Release();
    }

    TranslatedPipelineState copy = translated;
    if (copy.pVS) copy.pVS->AddRef();
    if (copy.pPS) copy.pPS->AddRef();
    if (copy.pCS) copy.pCS->AddRef();
    if (copy.pInputLayout) copy.pInputLayout->AddRef();
    if (copy.pRasterizerState) copy.pRasterizerState->AddRef();
    if (copy.pBlendState) copy.pBlendState->AddRef();
    if (copy.pDepthStencilState) copy.pDepthStencilState->AddRef();

    m_psos[pD3D12PSO] = copy;
}

bool ResourceManager::GetPSO(ID3D12PipelineState* pD3D12PSO, TranslatedPipelineState& outPSO) {
    if (!pD3D12PSO) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_psos.find(pD3D12PSO);
    if (it != m_psos.end()) {
        outPSO = it->second;
        return true;
    }
    return false;
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

    for (auto& pair : m_psos) {
        if (pair.second.pVS) pair.second.pVS->Release();
        if (pair.second.pPS) pair.second.pPS->Release();
        if (pair.second.pCS) pair.second.pCS->Release();
        if (pair.second.pInputLayout) pair.second.pInputLayout->Release();
        if (pair.second.pRasterizerState) pair.second.pRasterizerState->Release();
        if (pair.second.pBlendState) pair.second.pBlendState->Release();
        if (pair.second.pDepthStencilState) pair.second.pDepthStencilState->Release();
    }
    m_psos.clear();

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
        if (pair.second.pUAV) pair.second.pUAV->Release();
    }
    m_uavs.clear();

    for (auto& pair : m_cbvs) {
        if (pair.second) pair.second->Release();
    }
    m_cbvs.clear();

    for (auto& pair : m_samplers) {
        if (pair.second) pair.second->Release();
    }
    m_samplers.clear();
    m_rootSignatures.clear();

    for (auto& pair : m_resources) {
        if (pair.second.pD3D11Buffer) pair.second.pD3D11Buffer->Release();
        if (pair.second.pD3D11Texture) pair.second.pD3D11Texture->Release();
        if (pair.second.pD3D11Resource) pair.second.pD3D11Resource->Release();
    }
    m_resources.clear();
    m_gpuVaToBuffer.clear();
    m_graphicsRootConstants.clear();
    m_computeRootConstants.clear();
}

ResourceManager::MemoryFootprint ResourceManager::GetMemoryFootprint() {
    std::lock_guard<std::mutex> lock(m_mutex);
    MemoryFootprint mf = {};
    mf.resourceCount = m_resources.size();
    mf.rtvCount = m_rtvs.size();
    mf.dsvCount = m_dsvs.size();
    mf.srvCount = m_srvs.size();
    mf.uavCount = m_uavs.size();
    mf.cbvCount = m_cbvs.size();
    mf.samplerCount = m_samplers.size();
    mf.rootSigCount = m_rootSignatures.size();
    mf.psoCount = m_psos.size();

    // Estimate internal hash map storage overhead (node pointers, key, value, buckets)
    mf.estimatedMemoryBytes += m_resources.size() * (sizeof(ID3D12Resource*) + sizeof(TranslatedResourceInfo) + 32);
    mf.estimatedMemoryBytes += m_rtvs.size() * (sizeof(SIZE_T) + sizeof(ID3D11RenderTargetView*) + 32);
    mf.estimatedMemoryBytes += m_dsvs.size() * (sizeof(SIZE_T) + sizeof(ID3D11DepthStencilView*) + 32);
    mf.estimatedMemoryBytes += m_srvs.size() * (sizeof(SIZE_T) + sizeof(ID3D11ShaderResourceView*) + 32);
    mf.estimatedMemoryBytes += m_uavs.size() * (sizeof(SIZE_T) + sizeof(TranslatedUAVInfo) + 32);
    mf.estimatedMemoryBytes += m_cbvs.size() * (sizeof(SIZE_T) + sizeof(ID3D11Buffer*) + 32);
    mf.estimatedMemoryBytes += m_samplers.size() * (sizeof(SIZE_T) + sizeof(ID3D11SamplerState*) + 32);
    mf.estimatedMemoryBytes += m_rootSignatures.size() * (sizeof(ID3D12RootSignature*) + sizeof(TranslatedRootSignature) + 32);
    mf.estimatedMemoryBytes += m_psos.size() * (sizeof(ID3D12PipelineState*) + sizeof(TranslatedPipelineState) + 32);
    return mf;
}

} // namespace kepler
