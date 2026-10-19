#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3d11.h>

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace kepler {

enum class CommandType {
    ClearRenderTarget,
    ClearDepthStencil,
    DrawInstanced,
    DrawIndexedInstanced,
    Dispatch,
    CopyBufferRegion,
    CopyTextureRegion,
    CopyResource,
    ResourceBarrier,
    SetRenderTargets,
    SetViewports,
    SetScissorRects,
    IASetPrimitiveTopology,
    IASetVertexBuffers,
    IASetIndexBuffer,
    SetPipelineState,
    SetGraphicsRootSignature,
    SetComputeRootSignature,
    SetGraphicsRootDescriptorTable,
    SetComputeRootDescriptorTable,
    SetGraphicsRoot32BitConstants,
    SetComputeRoot32BitConstants
};

struct ClearRenderTargetCmd {
    D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView;
    float colorRGBA[4];
    std::vector<D3D12_RECT> rects;
};

struct ClearDepthStencilCmd {
    D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView;
    D3D12_CLEAR_FLAGS clearFlags;
    float depth;
    uint8_t stencil;
    std::vector<D3D12_RECT> rects;
};

struct DrawInstancedCmd {
    UINT vertexCountPerInstance;
    UINT instanceCount;
    UINT startVertexLocation;
    UINT startInstanceLocation;
};

struct DrawIndexedInstancedCmd {
    UINT indexCountPerInstance;
    UINT instanceCount;
    UINT startIndexLocation;
    INT baseVertexLocation;
    UINT startInstanceLocation;
};

struct DispatchCmd {
    UINT threadGroupCountX;
    UINT threadGroupCountY;
    UINT threadGroupCountZ;
};

struct CopyBufferRegionCmd {
    ID3D12Resource* pDstBuffer;
    UINT64 dstOffset;
    ID3D12Resource* pSrcBuffer;
    UINT64 srcOffset;
    UINT64 numBytes;
};

struct CopyResourceCmd {
    ID3D12Resource* pDstResource;
    ID3D12Resource* pSrcResource;
};

struct ResourceBarrierCmd {
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
};

struct SetRenderTargetsCmd {
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles;
    bool hasDepthStencil;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle;
};

struct SetViewportsCmd {
    std::vector<D3D12_VIEWPORT> viewports;
};

struct SetScissorRectsCmd {
    std::vector<D3D12_RECT> rects;
};

struct IASetPrimitiveTopologyCmd {
    D3D12_PRIMITIVE_TOPOLOGY topology;
};

struct VertexBufferBinding {
    ID3D12Resource* pBuffer;
    UINT64 offset;
    UINT strideInBytes;
    UINT sizeInBytes;
};

struct IASetVertexBuffersCmd {
    UINT startSlot;
    std::vector<VertexBufferBinding> views;
};

struct IASetIndexBufferCmd {
    ID3D12Resource* pBuffer;
    UINT64 offset;
    UINT sizeInBytes;
    DXGI_FORMAT format;
};

struct SetPipelineStateCmd {
    ID3D12PipelineState* pPipelineState;
};

struct SetRootSignatureCmd {
    ID3D12RootSignature* pRootSignature;
    bool isCompute;
};

struct SetRootDescriptorTableCmd {
    UINT rootParameterIndex;
    D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor;
    bool isCompute;
};

struct SetRoot32BitConstantsCmd {
    UINT rootParameterIndex;
    UINT num32BitValuesToSet;
    std::vector<uint32_t> constants;
    UINT destOffsetIn32BitValues;
    bool isCompute;
};

struct RecordedCommand {
    CommandType type;
    union {
        ClearRenderTargetCmd clearRTV;
        ClearDepthStencilCmd clearDSV;
        DrawInstancedCmd drawInstanced;
        DrawIndexedInstancedCmd drawIndexed;
        DispatchCmd dispatch;
        CopyBufferRegionCmd copyBuffer;
        CopyResourceCmd copyResource;
        ResourceBarrierCmd barrier;
        SetRenderTargetsCmd setRTVs;
        SetViewportsCmd setViewports;
        SetScissorRectsCmd setScissors;
        IASetPrimitiveTopologyCmd setTopology;
        IASetVertexBuffersCmd setVBs;
        IASetIndexBufferCmd setIB;
        SetPipelineStateCmd setPSO;
        SetRootSignatureCmd setRootSig;
        SetRootDescriptorTableCmd setRootTable;
        SetRoot32BitConstantsCmd setRootConstants;
    };

    RecordedCommand();
    ~RecordedCommand();
    RecordedCommand(const RecordedCommand& other);
    RecordedCommand& operator=(const RecordedCommand& other);
    RecordedCommand(RecordedCommand&& other) noexcept;
    RecordedCommand& operator=(RecordedCommand&& other) noexcept;
    void Cleanup();
};

class CommandBatch {
public:
    CommandBatch() = default;
    ~CommandBatch() = default;

    void Append(RecordedCommand&& cmd);
    void Clear();
    const std::vector<RecordedCommand>& GetCommands() const { return m_commands; }
    size_t GetCount() const { return m_commands.size(); }

private:
    std::vector<RecordedCommand> m_commands;
};

// Tracks resource states across barriers to validate transitions and detect hazards
class ResourceStateTracker {
public:
    static ResourceStateTracker& Instance();

    void RegisterResource(ID3D12Resource* pResource, D3D12_RESOURCE_STATES initialState);
    void UnregisterResource(ID3D12Resource* pResource);
    void UpdateState(ID3D12Resource* pResource, UINT subresource, D3D12_RESOURCE_STATES newState);
    D3D12_RESOURCE_STATES GetState(ID3D12Resource* pResource, UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
    bool ValidateTransition(ID3D12Resource* pResource, UINT subresource, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter, std::string& outWarning);
    void Reset();

private:
    ResourceStateTracker() = default;
    std::mutex m_mutex;
    struct ResourceSubresourceState {
        D3D12_RESOURCE_STATES globalState = D3D12_RESOURCE_STATE_COMMON;
        std::unordered_map<UINT, D3D12_RESOURCE_STATES> subresourceStates;
    };
    std::unordered_map<ID3D12Resource*, ResourceSubresourceState> m_states;
};

} // namespace kepler
