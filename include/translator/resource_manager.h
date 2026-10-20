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
    D3D12_GPU_VIRTUAL_ADDRESS gpuVA = 0;
    ID3D11Resource* pD3D11Resource = nullptr;
    ID3D11Buffer* pD3D11Buffer = nullptr;
    ID3D11Texture2D* pD3D11Texture = nullptr;
    bool isUploadHeap = false;
    bool isReadbackHeap = false;
};

struct TranslatedDescriptorRange {
    D3D12_DESCRIPTOR_RANGE_TYPE rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    UINT numDescriptors = 0;
    UINT baseShaderRegister = 0;
    UINT registerSpace = 0;
    UINT offsetInDescriptorsFromTableStart = 0;
};

struct TranslatedRootParameter {
    D3D12_ROOT_PARAMETER_TYPE type = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL;
    UINT shaderRegister = 0;
    UINT registerSpace = 0;
    UINT num32BitValues = 0;
    std::vector<TranslatedDescriptorRange> ranges;
};

struct TranslatedRootSignature {
    std::vector<TranslatedRootParameter> parameters;
    D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
};

struct TranslatedUAVInfo {
    ID3D11UnorderedAccessView* pUAV = nullptr;
    ID3D12Resource* pD3D12Resource = nullptr;
    ID3D11Resource* pD3D11Resource = nullptr;
    ID3D12Resource* pCounterResource = nullptr;
    UINT initialCount = static_cast<UINT>(-1); // (UINT)-1 sentinel preserves counter in D3D11
    bool hasCounter = false;
    UINT structureByteStride = 0;
    UINT numElements = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

struct TranslatedPipelineState {
    ID3D12PipelineState* pD3D12PSO = nullptr;
    ID3D11VertexShader* pVS = nullptr;
    ID3D11PixelShader* pPS = nullptr;
    ID3D11ComputeShader* pCS = nullptr;
    ID3D11InputLayout* pInputLayout = nullptr;
    ID3D11RasterizerState* pRasterizerState = nullptr;
    ID3D11BlendState* pBlendState = nullptr;
    ID3D11DepthStencilState* pDepthStencilState = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

class ResourceManager {
public:
    static ResourceManager& Instance();

    void RegisterResource(ID3D12Resource* pD3D12Resource,
                          const D3D12_RESOURCE_DESC& desc,
                          const D3D12_HEAP_PROPERTIES* pHeapProps,
                          ID3D11Resource* pD3D11Resource,
                          D3D12_GPU_VIRTUAL_ADDRESS gpuVA = 0);
    void UnregisterResource(ID3D12Resource* pD3D12Resource);
    bool GetResourceInfo(ID3D12Resource* pD3D12Resource, TranslatedResourceInfo& outInfo);

    // GPU Virtual Address to Buffer Lookup
    void RegisterBufferGPUVA(ID3D12Resource* pD3D12Resource, D3D12_GPU_VIRTUAL_ADDRESS gpuVA);
    bool FindBufferByGPUVA(D3D12_GPU_VIRTUAL_ADDRESS va, TranslatedResourceInfo& outInfo, UINT64& outByteOffset);

    // Descriptor View Registration & Lookup
    void RegisterRTV(SIZE_T cpuHandlePtr, ID3D11RenderTargetView* pRTV);
    ID3D11RenderTargetView* GetRTV(SIZE_T cpuHandlePtr);

    void RegisterDSV(SIZE_T cpuHandlePtr, ID3D11DepthStencilView* pDSV);
    ID3D11DepthStencilView* GetDSV(SIZE_T cpuHandlePtr);

    void RegisterSRV(SIZE_T cpuHandlePtr, ID3D11ShaderResourceView* pSRV);
    ID3D11ShaderResourceView* GetSRV(SIZE_T cpuHandlePtr);

    void RegisterUAV(SIZE_T cpuHandlePtr, ID3D11UnorderedAccessView* pUAV);
    void RegisterUAVInfo(SIZE_T cpuHandlePtr, const TranslatedUAVInfo& info);
    ID3D11UnorderedAccessView* GetUAV(SIZE_T cpuHandlePtr);
    bool GetUAVInfo(SIZE_T cpuHandlePtr, TranslatedUAVInfo& outInfo);
    void SetUAVInitialCount(SIZE_T cpuHandlePtr, UINT count);
    void UpdateD3D11ResourceForBuffer(ID3D12Resource* pD3D12Resource, ID3D11Buffer* pNewBuffer);

    void RegisterCBV(SIZE_T cpuHandlePtr, ID3D11Buffer* pBuffer);
    ID3D11Buffer* GetCBV(SIZE_T cpuHandlePtr);

    void RegisterSampler(SIZE_T cpuHandlePtr, ID3D11SamplerState* pSampler);
    ID3D11SamplerState* GetSampler(SIZE_T cpuHandlePtr);

    // Root Signature Registration & Lookup
    void RegisterRootSignature(ID3D12RootSignature* pRootSig, const TranslatedRootSignature& sig);
    void UnregisterRootSignature(ID3D12RootSignature* pRootSig);
    bool GetRootSignature(ID3D12RootSignature* pRootSig, TranslatedRootSignature& outSig);

    // Pipeline State Registration & Lookup
    void RegisterPSO(ID3D12PipelineState* pD3D12PSO, const TranslatedPipelineState& translated);
    bool GetPSO(ID3D12PipelineState* pD3D12PSO, TranslatedPipelineState& outPSO);

    // Root parameter mapping
    void BindRootConstants(UINT rootParamIndex, const std::vector<uint32_t>& constants, bool isCompute);
    const std::vector<uint32_t>* GetRootConstants(UINT rootParamIndex, bool isCompute) const;

    void Clear();

    struct MemoryFootprint {
        size_t resourceCount = 0;
        size_t rtvCount = 0;
        size_t dsvCount = 0;
        size_t srvCount = 0;
        size_t uavCount = 0;
        size_t cbvCount = 0;
        size_t samplerCount = 0;
        size_t rootSigCount = 0;
        size_t psoCount = 0;
        size_t estimatedMemoryBytes = 0;
    };
    MemoryFootprint GetMemoryFootprint();

private:
    ResourceManager() = default;
    ~ResourceManager();
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    std::mutex m_mutex;
    std::unordered_map<ID3D12Resource*, TranslatedResourceInfo> m_resources;
    std::vector<std::pair<D3D12_GPU_VIRTUAL_ADDRESS, ID3D12Resource*>> m_gpuVaToBuffer;

    std::unordered_map<SIZE_T, ID3D11RenderTargetView*> m_rtvs;
    std::unordered_map<SIZE_T, ID3D11DepthStencilView*> m_dsvs;
    std::unordered_map<SIZE_T, ID3D11ShaderResourceView*> m_srvs;
    std::unordered_map<SIZE_T, TranslatedUAVInfo> m_uavs;
    std::unordered_map<SIZE_T, ID3D11Buffer*> m_cbvs;
    std::unordered_map<SIZE_T, ID3D11SamplerState*> m_samplers;
    std::unordered_map<ID3D12RootSignature*, TranslatedRootSignature> m_rootSignatures;

    std::unordered_map<ID3D12PipelineState*, TranslatedPipelineState> m_psos;

    std::unordered_map<UINT, std::vector<uint32_t>> m_graphicsRootConstants;
    std::unordered_map<UINT, std::vector<uint32_t>> m_computeRootConstants;
};

} // namespace kepler
