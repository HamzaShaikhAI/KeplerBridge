#include "translator/command_translator.h"
#include "core/logger.h"
#include <new>

namespace kepler {

RecordedCommand::RecordedCommand() : type(CommandType::DrawInstanced) {
    drawInstanced = { 0, 0, 0, 0 };
}

RecordedCommand::~RecordedCommand() {
    Cleanup();
}

void RecordedCommand::Cleanup() {
    switch (type) {
        case CommandType::ClearRenderTarget:
            clearRTV.~ClearRenderTargetCmd();
            break;
        case CommandType::ClearDepthStencil:
            clearDSV.~ClearDepthStencilCmd();
            break;
        case CommandType::ResourceBarrier:
            barrier.~ResourceBarrierCmd();
            break;
        case CommandType::SetRenderTargets:
            setRTVs.~SetRenderTargetsCmd();
            break;
        case CommandType::SetViewports:
            setViewports.~SetViewportsCmd();
            break;
        case CommandType::SetScissorRects:
            setScissors.~SetScissorRectsCmd();
            break;
        case CommandType::IASetVertexBuffers:
            setVBs.~IASetVertexBuffersCmd();
            break;
        case CommandType::SetGraphicsRoot32BitConstants:
        case CommandType::SetComputeRoot32BitConstants:
            setRootConstants.~SetRoot32BitConstantsCmd();
            break;
        default:
            break;
    }
}

RecordedCommand::RecordedCommand(const RecordedCommand& other) : type(other.type) {
    switch (other.type) {
        case CommandType::ClearRenderTarget:
            new (&clearRTV) ClearRenderTargetCmd(other.clearRTV);
            break;
        case CommandType::ClearDepthStencil:
            new (&clearDSV) ClearDepthStencilCmd(other.clearDSV);
            break;
        case CommandType::DrawInstanced:
            drawInstanced = other.drawInstanced;
            break;
        case CommandType::DrawIndexedInstanced:
            drawIndexed = other.drawIndexed;
            break;
        case CommandType::Dispatch:
            dispatch = other.dispatch;
            break;
        case CommandType::CopyBufferRegion:
            copyBuffer = other.copyBuffer;
            break;
        case CommandType::CopyResource:
            copyResource = other.copyResource;
            break;
        case CommandType::ResourceBarrier:
            new (&barrier) ResourceBarrierCmd(other.barrier);
            break;
        case CommandType::SetRenderTargets:
            new (&setRTVs) SetRenderTargetsCmd(other.setRTVs);
            break;
        case CommandType::SetViewports:
            new (&setViewports) SetViewportsCmd(other.setViewports);
            break;
        case CommandType::SetScissorRects:
            new (&setScissors) SetScissorRectsCmd(other.setScissors);
            break;
        case CommandType::IASetPrimitiveTopology:
            setTopology = other.setTopology;
            break;
        case CommandType::IASetVertexBuffers:
            new (&setVBs) IASetVertexBuffersCmd(other.setVBs);
            break;
        case CommandType::IASetIndexBuffer:
            setIB = other.setIB;
            break;
        case CommandType::SetPipelineState:
            setPSO = other.setPSO;
            break;
        case CommandType::SetGraphicsRootSignature:
        case CommandType::SetComputeRootSignature:
            setRootSig = other.setRootSig;
            break;
        case CommandType::SetGraphicsRootDescriptorTable:
        case CommandType::SetComputeRootDescriptorTable:
            setRootTable = other.setRootTable;
            break;
        case CommandType::SetGraphicsRoot32BitConstants:
        case CommandType::SetComputeRoot32BitConstants:
            new (&setRootConstants) SetRoot32BitConstantsCmd(other.setRootConstants);
            break;
        default:
            break;
    }
}

RecordedCommand& RecordedCommand::operator=(const RecordedCommand& other) {
    if (this != &other) {
        Cleanup();
        type = other.type;
        switch (other.type) {
            case CommandType::ClearRenderTarget:
                new (&clearRTV) ClearRenderTargetCmd(other.clearRTV);
                break;
            case CommandType::ClearDepthStencil:
                new (&clearDSV) ClearDepthStencilCmd(other.clearDSV);
                break;
            case CommandType::DrawInstanced:
                drawInstanced = other.drawInstanced;
                break;
            case CommandType::DrawIndexedInstanced:
                drawIndexed = other.drawIndexed;
                break;
            case CommandType::Dispatch:
                dispatch = other.dispatch;
                break;
            case CommandType::CopyBufferRegion:
                copyBuffer = other.copyBuffer;
                break;
            case CommandType::CopyResource:
                copyResource = other.copyResource;
                break;
            case CommandType::ResourceBarrier:
                new (&barrier) ResourceBarrierCmd(other.barrier);
                break;
            case CommandType::SetRenderTargets:
                new (&setRTVs) SetRenderTargetsCmd(other.setRTVs);
                break;
            case CommandType::SetViewports:
                new (&setViewports) SetViewportsCmd(other.setViewports);
                break;
            case CommandType::SetScissorRects:
                new (&setScissors) SetScissorRectsCmd(other.setScissors);
                break;
            case CommandType::IASetPrimitiveTopology:
                setTopology = other.setTopology;
                break;
            case CommandType::IASetVertexBuffers:
                new (&setVBs) IASetVertexBuffersCmd(other.setVBs);
                break;
            case CommandType::IASetIndexBuffer:
                setIB = other.setIB;
                break;
            case CommandType::SetPipelineState:
                setPSO = other.setPSO;
                break;
            case CommandType::SetGraphicsRootSignature:
            case CommandType::SetComputeRootSignature:
                setRootSig = other.setRootSig;
                break;
            case CommandType::SetGraphicsRootDescriptorTable:
            case CommandType::SetComputeRootDescriptorTable:
                setRootTable = other.setRootTable;
                break;
            case CommandType::SetGraphicsRoot32BitConstants:
            case CommandType::SetComputeRoot32BitConstants:
                new (&setRootConstants) SetRoot32BitConstantsCmd(other.setRootConstants);
                break;
            default:
                break;
        }
    }
    return *this;
}

RecordedCommand::RecordedCommand(RecordedCommand&& other) noexcept : type(other.type) {
    switch (other.type) {
        case CommandType::ClearRenderTarget:
            new (&clearRTV) ClearRenderTargetCmd(std::move(other.clearRTV));
            break;
        case CommandType::ClearDepthStencil:
            new (&clearDSV) ClearDepthStencilCmd(std::move(other.clearDSV));
            break;
        case CommandType::DrawInstanced:
            drawInstanced = other.drawInstanced;
            break;
        case CommandType::DrawIndexedInstanced:
            drawIndexed = other.drawIndexed;
            break;
        case CommandType::Dispatch:
            dispatch = other.dispatch;
            break;
        case CommandType::CopyBufferRegion:
            copyBuffer = other.copyBuffer;
            break;
        case CommandType::CopyResource:
            copyResource = other.copyResource;
            break;
        case CommandType::ResourceBarrier:
            new (&barrier) ResourceBarrierCmd(std::move(other.barrier));
            break;
        case CommandType::SetRenderTargets:
            new (&setRTVs) SetRenderTargetsCmd(std::move(other.setRTVs));
            break;
        case CommandType::SetViewports:
            new (&setViewports) SetViewportsCmd(std::move(other.setViewports));
            break;
        case CommandType::SetScissorRects:
            new (&setScissors) SetScissorRectsCmd(std::move(other.setScissors));
            break;
        case CommandType::IASetPrimitiveTopology:
            setTopology = other.setTopology;
            break;
        case CommandType::IASetVertexBuffers:
            new (&setVBs) IASetVertexBuffersCmd(std::move(other.setVBs));
            break;
        case CommandType::IASetIndexBuffer:
            setIB = other.setIB;
            break;
        case CommandType::SetPipelineState:
            setPSO = other.setPSO;
            break;
        case CommandType::SetGraphicsRootSignature:
        case CommandType::SetComputeRootSignature:
            setRootSig = other.setRootSig;
            break;
        case CommandType::SetGraphicsRootDescriptorTable:
        case CommandType::SetComputeRootDescriptorTable:
            setRootTable = other.setRootTable;
            break;
        case CommandType::SetGraphicsRoot32BitConstants:
        case CommandType::SetComputeRoot32BitConstants:
            new (&setRootConstants) SetRoot32BitConstantsCmd(std::move(other.setRootConstants));
            break;
        default:
            break;
    }
}

RecordedCommand& RecordedCommand::operator=(RecordedCommand&& other) noexcept {
    if (this != &other) {
        Cleanup();
        type = other.type;
        switch (other.type) {
            case CommandType::ClearRenderTarget:
                new (&clearRTV) ClearRenderTargetCmd(std::move(other.clearRTV));
                break;
            case CommandType::ClearDepthStencil:
                new (&clearDSV) ClearDepthStencilCmd(std::move(other.clearDSV));
                break;
            case CommandType::DrawInstanced:
                drawInstanced = other.drawInstanced;
                break;
            case CommandType::DrawIndexedInstanced:
                drawIndexed = other.drawIndexed;
                break;
            case CommandType::Dispatch:
                dispatch = other.dispatch;
                break;
            case CommandType::CopyBufferRegion:
                copyBuffer = other.copyBuffer;
                break;
            case CommandType::CopyResource:
                copyResource = other.copyResource;
                break;
            case CommandType::ResourceBarrier:
                new (&barrier) ResourceBarrierCmd(std::move(other.barrier));
                break;
            case CommandType::SetRenderTargets:
                new (&setRTVs) SetRenderTargetsCmd(std::move(other.setRTVs));
                break;
            case CommandType::SetViewports:
                new (&setViewports) SetViewportsCmd(std::move(other.setViewports));
                break;
            case CommandType::SetScissorRects:
                new (&setScissors) SetScissorRectsCmd(std::move(other.setScissors));
                break;
            case CommandType::IASetPrimitiveTopology:
                setTopology = other.setTopology;
                break;
            case CommandType::IASetVertexBuffers:
                new (&setVBs) IASetVertexBuffersCmd(std::move(other.setVBs));
                break;
            case CommandType::IASetIndexBuffer:
                setIB = other.setIB;
                break;
            case CommandType::SetPipelineState:
                setPSO = other.setPSO;
                break;
            case CommandType::SetGraphicsRootSignature:
            case CommandType::SetComputeRootSignature:
                setRootSig = other.setRootSig;
                break;
            case CommandType::SetGraphicsRootDescriptorTable:
            case CommandType::SetComputeRootDescriptorTable:
                setRootTable = other.setRootTable;
                break;
            case CommandType::SetGraphicsRoot32BitConstants:
            case CommandType::SetComputeRoot32BitConstants:
                new (&setRootConstants) SetRoot32BitConstantsCmd(std::move(other.setRootConstants));
                break;
            default:
                break;
        }
    }
    return *this;
}

void CommandBatch::Append(RecordedCommand&& cmd) {
    m_commands.push_back(std::move(cmd));
}

void CommandBatch::Clear() {
    m_commands.clear();
}

// ============================================================================
// ResourceStateTracker Implementation
// ============================================================================

ResourceStateTracker& ResourceStateTracker::Instance() {
    static ResourceStateTracker s_instance;
    return s_instance;
}

void ResourceStateTracker::RegisterResource(ID3D12Resource* pResource, D3D12_RESOURCE_STATES initialState) {
    if (!pResource) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_states[pResource].globalState = initialState;
}

void ResourceStateTracker::UnregisterResource(ID3D12Resource* pResource) {
    if (!pResource) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_states.erase(pResource);
}

void ResourceStateTracker::UpdateState(ID3D12Resource* pResource, UINT subresource, D3D12_RESOURCE_STATES newState) {
    if (!pResource) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_states.find(pResource);
    if (it == m_states.end()) {
        m_states[pResource].globalState = newState;
        return;
    }

    if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
        it->second.globalState = newState;
        it->second.subresourceStates.clear();
    } else {
        it->second.subresourceStates[subresource] = newState;
    }
}

D3D12_RESOURCE_STATES ResourceStateTracker::GetState(ID3D12Resource* pResource, UINT subresource) {
    if (!pResource) return D3D12_RESOURCE_STATE_COMMON;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_states.find(pResource);
    if (it == m_states.end()) {
        return D3D12_RESOURCE_STATE_COMMON;
    }
    if (subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
        auto subIt = it->second.subresourceStates.find(subresource);
        if (subIt != it->second.subresourceStates.end()) {
            return subIt->second;
        }
    }
    return it->second.globalState;
}

bool ResourceStateTracker::ValidateTransition(
    ID3D12Resource* pResource,
    UINT subresource,
    D3D12_RESOURCE_STATES stateBefore,
    D3D12_RESOURCE_STATES stateAfter,
    std::string& outWarning)
{
    if (!pResource) {
        outWarning = "Null resource in barrier transition";
        return false;
    }

    D3D12_RESOURCE_STATES current = GetState(pResource, subresource);
    if (current != stateBefore && stateBefore != D3D12_RESOURCE_STATE_COMMON) {
        outWarning = "State mismatch for resource: expected " + std::to_string(stateBefore) +
                     " but current tracked state is " + std::to_string(current);
        return false;
    }

    UpdateState(pResource, subresource, stateAfter);
    return true;
}

void ResourceStateTracker::Reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_states.clear();
}

} // namespace kepler
