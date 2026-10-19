#include "translator/wrapped_command_objects.h"
#include "translator/d3d11_backend.h"
#include "core/logger.h"
#include "core/config.h"

namespace kepler {

// ============================================================================
// WrappedD3D12CommandAllocator
// ============================================================================

WrappedD3D12CommandAllocator::WrappedD3D12CommandAllocator(ID3D12CommandAllocator* pReal, D3D12_COMMAND_LIST_TYPE type)
    : m_pReal(pReal), m_type(type), m_refCount(1)
{
    if (m_pReal) m_pReal->AddRef();
}

WrappedD3D12CommandAllocator::~WrappedD3D12CommandAllocator() {
    if (m_pReal) {
        m_pReal->Release();
        m_pReal = nullptr;
    }
}

HRESULT STDMETHODCALLTYPE WrappedD3D12CommandAllocator::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (InlineIsEqualGUID(riid, IID_IUnknown) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D12Object)) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D12DeviceChild)) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D12Pageable)) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D12CommandAllocator)))
    {
        *ppvObject = static_cast<ID3D12CommandAllocator*>(this);
        AddRef();
        return S_OK;
    }
    if (m_pReal) return m_pReal->QueryInterface(riid, ppvObject);
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WrappedD3D12CommandAllocator::AddRef() {
    return ++m_refCount;
}

ULONG STDMETHODCALLTYPE WrappedD3D12CommandAllocator::Release() {
    ULONG count = --m_refCount;
    if (count == 0) {
        delete this;
    }
    return count;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12CommandAllocator::GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) {
    return m_pReal ? m_pReal->GetPrivateData(guid, pDataSize, pData) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12CommandAllocator::SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) {
    return m_pReal ? m_pReal->SetPrivateData(guid, DataSize, pData) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12CommandAllocator::SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) {
    return m_pReal ? m_pReal->SetPrivateDataInterface(guid, pData) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12CommandAllocator::SetName(LPCWSTR Name) {
    return m_pReal ? m_pReal->SetName(Name) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12CommandAllocator::GetDevice(REFIID riid, void** ppvDevice) {
    return m_pReal ? m_pReal->GetDevice(riid, ppvDevice) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12CommandAllocator::Reset() {
    return m_pReal ? m_pReal->Reset() : S_OK;
}

// ============================================================================
// WrappedD3D12GraphicsCommandList
// ============================================================================

WrappedD3D12GraphicsCommandList::WrappedD3D12GraphicsCommandList(
    ID3D12GraphicsCommandList* pReal,
    D3D12_COMMAND_LIST_TYPE type,
    ID3D12CommandAllocator* pAllocator)
    : m_pReal(pReal), m_type(type), m_pCurrentAllocator(pAllocator), m_refCount(1), m_isClosed(false)
{
    if (m_pReal) m_pReal->AddRef();
    if (m_pCurrentAllocator) m_pCurrentAllocator->AddRef();
}

WrappedD3D12GraphicsCommandList::~WrappedD3D12GraphicsCommandList() {
    if (m_pCurrentAllocator) {
        m_pCurrentAllocator->Release();
        m_pCurrentAllocator = nullptr;
    }
    if (m_pReal) {
        m_pReal->Release();
        m_pReal = nullptr;
    }
}

HRESULT STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (InlineIsEqualGUID(riid, IID_IUnknown) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D12Object)) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D12DeviceChild)) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D12CommandList)) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D12GraphicsCommandList)))
    {
        *ppvObject = static_cast<ID3D12GraphicsCommandList*>(this);
        AddRef();
        return S_OK;
    }
    if (m_pReal) return m_pReal->QueryInterface(riid, ppvObject);
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::AddRef() {
    return ++m_refCount;
}

ULONG STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::Release() {
    ULONG count = --m_refCount;
    if (count == 0) {
        delete this;
    }
    return count;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) {
    return m_pReal ? m_pReal->GetPrivateData(guid, pDataSize, pData) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) {
    return m_pReal ? m_pReal->SetPrivateData(guid, DataSize, pData) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) {
    return m_pReal ? m_pReal->SetPrivateDataInterface(guid, pData) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetName(LPCWSTR Name) {
    return m_pReal ? m_pReal->SetName(Name) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::GetDevice(REFIID riid, void** ppvDevice) {
    return m_pReal ? m_pReal->GetDevice(riid, ppvDevice) : E_FAIL;
}

D3D12_COMMAND_LIST_TYPE STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::GetType() {
    return m_type;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::Close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_isClosed = true;
    if (m_pReal) return m_pReal->Close();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::Reset(ID3D12CommandAllocator* pAllocator, ID3D12PipelineState* pInitialState) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_batch.Clear();
    m_isClosed = false;

    if (pAllocator != m_pCurrentAllocator) {
        if (m_pCurrentAllocator) m_pCurrentAllocator->Release();
        m_pCurrentAllocator = pAllocator;
        if (m_pCurrentAllocator) m_pCurrentAllocator->AddRef();
    }

    ID3D12CommandAllocator* realAlloc = pAllocator;
    auto* wrappedAlloc = dynamic_cast<WrappedD3D12CommandAllocator*>(pAllocator);
    if (wrappedAlloc && wrappedAlloc->GetReal()) {
        realAlloc = wrappedAlloc->GetReal();
    }

    if (m_pReal) return m_pReal->Reset(realAlloc, pInitialState);
    return S_OK;
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::ClearState(ID3D12PipelineState* pPipelineState) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_batch.Clear();
    if (m_pReal) m_pReal->ClearState(pPipelineState);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::DrawInstanced;
    cmd.drawInstanced = { VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation };
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::DrawIndexedInstanced;
    cmd.drawIndexed = { IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation };
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::Dispatch;
    cmd.dispatch = { ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ };
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->Dispatch(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::CopyBufferRegion(ID3D12Resource* pDstBuffer, UINT64 DstOffset, ID3D12Resource* pSrcBuffer, UINT64 SrcOffset, UINT64 NumBytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::CopyBufferRegion;
    cmd.copyBuffer = { pDstBuffer, DstOffset, pSrcBuffer, SrcOffset, NumBytes };
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->CopyBufferRegion(pDstBuffer, DstOffset, pSrcBuffer, SrcOffset, NumBytes);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::CopyTextureRegion(const D3D12_TEXTURE_COPY_LOCATION* pDst, UINT DstX, UINT DstY, UINT DstZ, const D3D12_TEXTURE_COPY_LOCATION* pSrc, const D3D12_BOX* pSrcBox) {
    if (m_pReal) m_pReal->CopyTextureRegion(pDst, DstX, DstY, DstZ, pSrc, pSrcBox);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::CopyResource(ID3D12Resource* pDstResource, ID3D12Resource* pSrcResource) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::CopyResource;
    cmd.copyResource = { pDstResource, pSrcResource };
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->CopyResource(pDstResource, pSrcResource);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::CopyTiles(ID3D12Resource* pTiledResource, const D3D12_TILED_RESOURCE_COORDINATE* pTileRegionStartCoordinate, const D3D12_TILE_REGION_SIZE* pTileRegionSize, ID3D12Resource* pBuffer, UINT64 BufferOffset, D3D12_TILE_COPY_FLAGS Flags) {
    if (m_pReal) m_pReal->CopyTiles(pTiledResource, pTileRegionStartCoordinate, pTileRegionSize, pBuffer, BufferOffset, Flags);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::ResolveSubresource(ID3D12Resource* pDstResource, UINT DstSubresource, ID3D12Resource* pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format) {
    if (m_pReal) m_pReal->ResolveSubresource(pDstResource, DstSubresource, pSrcResource, SrcSubresource, Format);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::IASetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::IASetPrimitiveTopology;
    cmd.setTopology = { PrimitiveTopology };
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->IASetPrimitiveTopology(PrimitiveTopology);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::RSSetViewports(UINT NumViewports, const D3D12_VIEWPORT* pViewports) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::SetViewports;
    new (&cmd.setViewports) SetViewportsCmd();
    if (pViewports && NumViewports > 0) {
        cmd.setViewports.viewports.assign(pViewports, pViewports + NumViewports);
    }
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->RSSetViewports(NumViewports, pViewports);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::RSSetScissorRects(UINT NumRects, const D3D12_RECT* pRects) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::SetScissorRects;
    new (&cmd.setScissors) SetScissorRectsCmd();
    if (pRects && NumRects > 0) {
        cmd.setScissors.rects.assign(pRects, pRects + NumRects);
    }
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->RSSetScissorRects(NumRects, pRects);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::OMSetBlendFactor(const FLOAT BlendFactor[4]) {
    if (m_pReal) m_pReal->OMSetBlendFactor(BlendFactor);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::OMSetStencilRef(UINT StencilRef) {
    if (m_pReal) m_pReal->OMSetStencilRef(StencilRef);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetPipelineState(ID3D12PipelineState* pPipelineState) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::SetPipelineState;
    cmd.setPSO = { pPipelineState };
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->SetPipelineState(pPipelineState);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::ResourceBarrier(UINT NumBarriers, const D3D12_RESOURCE_BARRIER* pBarriers) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::ResourceBarrier;
    new (&cmd.barrier) ResourceBarrierCmd();
    if (pBarriers && NumBarriers > 0) {
        cmd.barrier.barriers.assign(pBarriers, pBarriers + NumBarriers);
        if (Config::Instance().IsStateTrackingEnabled()) {
            for (UINT i = 0; i < NumBarriers; ++i) {
                const auto& b = pBarriers[i];
                if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
                    std::string warn;
                    ResourceStateTracker::Instance().ValidateTransition(
                        b.Transition.pResource,
                        b.Transition.Subresource,
                        b.Transition.StateBefore,
                        b.Transition.StateAfter,
                        warn);
                    if (!warn.empty() && Config::Instance().IsVerboseCallsEnabled()) {
                        LOG_HOOK("[STATE_TRACKER] " + warn);
                    }
                }
            }
        }
    }
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->ResourceBarrier(NumBarriers, pBarriers);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::ExecuteBundle(ID3D12GraphicsCommandList* pCommandList) {
    if (m_pReal) m_pReal->ExecuteBundle(pCommandList);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetDescriptorHeaps(UINT NumDescriptorHeaps, ID3D12DescriptorHeap* const* ppDescriptorHeaps) {
    if (m_pReal) m_pReal->SetDescriptorHeaps(NumDescriptorHeaps, ppDescriptorHeaps);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetComputeRootSignature(ID3D12RootSignature* pRootSignature) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::SetComputeRootSignature;
    cmd.setRootSig = { pRootSignature, true };
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->SetComputeRootSignature(pRootSignature);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetGraphicsRootSignature(ID3D12RootSignature* pRootSignature) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::SetGraphicsRootSignature;
    cmd.setRootSig = { pRootSignature, false };
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->SetGraphicsRootSignature(pRootSignature);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetComputeRootDescriptorTable(UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::SetComputeRootDescriptorTable;
    cmd.setRootTable = { RootParameterIndex, BaseDescriptor, true };
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->SetComputeRootDescriptorTable(RootParameterIndex, BaseDescriptor);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetGraphicsRootDescriptorTable(UINT RootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::SetGraphicsRootDescriptorTable;
    cmd.setRootTable = { RootParameterIndex, BaseDescriptor, false };
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->SetGraphicsRootDescriptorTable(RootParameterIndex, BaseDescriptor);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetComputeRoot32BitConstant(UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues) {
    SetComputeRoot32BitConstants(RootParameterIndex, 1, &SrcData, DestOffsetIn32BitValues);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetGraphicsRoot32BitConstant(UINT RootParameterIndex, UINT SrcData, UINT DestOffsetIn32BitValues) {
    SetGraphicsRoot32BitConstants(RootParameterIndex, 1, &SrcData, DestOffsetIn32BitValues);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetComputeRoot32BitConstants(UINT RootParameterIndex, UINT Num32BitValuesToSet, const void* pSrcData, UINT DestOffsetIn32BitValues) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::SetComputeRoot32BitConstants;
    new (&cmd.setRootConstants) SetRoot32BitConstantsCmd();
    cmd.setRootConstants.rootParameterIndex = RootParameterIndex;
    cmd.setRootConstants.num32BitValuesToSet = Num32BitValuesToSet;
    cmd.setRootConstants.destOffsetIn32BitValues = DestOffsetIn32BitValues;
    cmd.setRootConstants.isCompute = true;
    if (pSrcData && Num32BitValuesToSet > 0) {
        const auto* ptr = reinterpret_cast<const uint32_t*>(pSrcData);
        cmd.setRootConstants.constants.assign(ptr, ptr + Num32BitValuesToSet);
    }
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->SetComputeRoot32BitConstants(RootParameterIndex, Num32BitValuesToSet, pSrcData, DestOffsetIn32BitValues);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetGraphicsRoot32BitConstants(UINT RootParameterIndex, UINT Num32BitValuesToSet, const void* pSrcData, UINT DestOffsetIn32BitValues) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::SetGraphicsRoot32BitConstants;
    new (&cmd.setRootConstants) SetRoot32BitConstantsCmd();
    cmd.setRootConstants.rootParameterIndex = RootParameterIndex;
    cmd.setRootConstants.num32BitValuesToSet = Num32BitValuesToSet;
    cmd.setRootConstants.destOffsetIn32BitValues = DestOffsetIn32BitValues;
    cmd.setRootConstants.isCompute = false;
    if (pSrcData && Num32BitValuesToSet > 0) {
        const auto* ptr = reinterpret_cast<const uint32_t*>(pSrcData);
        cmd.setRootConstants.constants.assign(ptr, ptr + Num32BitValuesToSet);
    }
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->SetGraphicsRoot32BitConstants(RootParameterIndex, Num32BitValuesToSet, pSrcData, DestOffsetIn32BitValues);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetComputeRootConstantBufferView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) {
    if (m_pReal) m_pReal->SetComputeRootConstantBufferView(RootParameterIndex, BufferLocation);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetGraphicsRootConstantBufferView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) {
    if (m_pReal) m_pReal->SetGraphicsRootConstantBufferView(RootParameterIndex, BufferLocation);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetComputeRootShaderResourceView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) {
    if (m_pReal) m_pReal->SetComputeRootShaderResourceView(RootParameterIndex, BufferLocation);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetGraphicsRootShaderResourceView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) {
    if (m_pReal) m_pReal->SetGraphicsRootShaderResourceView(RootParameterIndex, BufferLocation);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetComputeRootUnorderedAccessView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) {
    if (m_pReal) m_pReal->SetComputeRootUnorderedAccessView(RootParameterIndex, BufferLocation);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView(UINT RootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS BufferLocation) {
    if (m_pReal) m_pReal->SetGraphicsRootUnorderedAccessView(RootParameterIndex, BufferLocation);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::IASetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW* pView) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (pView) {
        RecordedCommand cmd;
        cmd.type = CommandType::IASetIndexBuffer;
        cmd.setIB = { nullptr, pView->BufferLocation, pView->SizeInBytes, pView->Format };
        m_batch.Append(std::move(cmd));
    }
    if (m_pReal) m_pReal->IASetIndexBuffer(pView);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::IASetVertexBuffers(UINT StartSlot, UINT NumViews, const D3D12_VERTEX_BUFFER_VIEW* pViews) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::IASetVertexBuffers;
    new (&cmd.setVBs) IASetVertexBuffersCmd();
    cmd.setVBs.startSlot = StartSlot;
    if (pViews && NumViews > 0) {
        for (UINT i = 0; i < NumViews; ++i) {
            VertexBufferBinding vb = { nullptr, pViews[i].BufferLocation, pViews[i].StrideInBytes, pViews[i].SizeInBytes };
            cmd.setVBs.views.push_back(vb);
        }
    }
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->IASetVertexBuffers(StartSlot, NumViews, pViews);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SOSetTargets(UINT StartSlot, UINT NumViews, const D3D12_STREAM_OUTPUT_BUFFER_VIEW* pViews) {
    if (m_pReal) m_pReal->SOSetTargets(StartSlot, NumViews, pViews);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::OMSetRenderTargets(UINT NumRenderTargetDescriptors, const D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors, WINBOOL RTsSingleHandleToDescriptorRange, const D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::SetRenderTargets;
    new (&cmd.setRTVs) SetRenderTargetsCmd();
    if (pRenderTargetDescriptors && NumRenderTargetDescriptors > 0) {
        cmd.setRTVs.rtvHandles.assign(pRenderTargetDescriptors, pRenderTargetDescriptors + NumRenderTargetDescriptors);
    }
    cmd.setRTVs.hasDepthStencil = (pDepthStencilDescriptor != nullptr);
    if (pDepthStencilDescriptor) {
        cmd.setRTVs.dsvHandle = *pDepthStencilDescriptor;
    }
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->OMSetRenderTargets(NumRenderTargetDescriptors, pRenderTargetDescriptors, RTsSingleHandleToDescriptorRange, pDepthStencilDescriptor);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::ClearDepthStencilView(D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView, D3D12_CLEAR_FLAGS ClearFlags, FLOAT Depth, UINT8 Stencil, UINT NumRects, const D3D12_RECT* pRects) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::ClearDepthStencil;
    new (&cmd.clearDSV) ClearDepthStencilCmd();
    cmd.clearDSV.depthStencilView = DepthStencilView;
    cmd.clearDSV.clearFlags = ClearFlags;
    cmd.clearDSV.depth = Depth;
    cmd.clearDSV.stencil = Stencil;
    if (pRects && NumRects > 0) {
        cmd.clearDSV.rects.assign(pRects, pRects + NumRects);
    }
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->ClearDepthStencilView(DepthStencilView, ClearFlags, Depth, Stencil, NumRects, pRects);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView, const FLOAT ColorRGBA[4], UINT NumRects, const D3D12_RECT* pRects) {
    std::lock_guard<std::mutex> lock(m_mutex);
    RecordedCommand cmd;
    cmd.type = CommandType::ClearRenderTarget;
    new (&cmd.clearRTV) ClearRenderTargetCmd();
    cmd.clearRTV.renderTargetView = RenderTargetView;
    if (ColorRGBA) {
        cmd.clearRTV.colorRGBA[0] = ColorRGBA[0];
        cmd.clearRTV.colorRGBA[1] = ColorRGBA[1];
        cmd.clearRTV.colorRGBA[2] = ColorRGBA[2];
        cmd.clearRTV.colorRGBA[3] = ColorRGBA[3];
    }
    if (pRects && NumRects > 0) {
        cmd.clearRTV.rects.assign(pRects, pRects + NumRects);
    }
    m_batch.Append(std::move(cmd));
    if (m_pReal) m_pReal->ClearRenderTargetView(RenderTargetView, ColorRGBA, NumRects, pRects);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::ClearUnorderedAccessViewUint(D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap, D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle, ID3D12Resource* pResource, const UINT Values[4], UINT NumRects, const D3D12_RECT* pRects) {
    if (m_pReal) m_pReal->ClearUnorderedAccessViewUint(ViewGPUHandleInCurrentHeap, ViewCPUHandle, pResource, Values, NumRects, pRects);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::ClearUnorderedAccessViewFloat(D3D12_GPU_DESCRIPTOR_HANDLE ViewGPUHandleInCurrentHeap, D3D12_CPU_DESCRIPTOR_HANDLE ViewCPUHandle, ID3D12Resource* pResource, const FLOAT Values[4], UINT NumRects, const D3D12_RECT* pRects) {
    if (m_pReal) m_pReal->ClearUnorderedAccessViewFloat(ViewGPUHandleInCurrentHeap, ViewCPUHandle, pResource, Values, NumRects, pRects);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::DiscardResource(ID3D12Resource* pResource, const D3D12_DISCARD_REGION* pRegion) {
    if (m_pReal) m_pReal->DiscardResource(pResource, pRegion);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::BeginQuery(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index) {
    if (m_pReal) m_pReal->BeginQuery(pQueryHeap, Type, Index);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::EndQuery(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE Type, UINT Index) {
    if (m_pReal) m_pReal->EndQuery(pQueryHeap, Type, Index);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::ResolveQueryData(ID3D12QueryHeap* pQueryHeap, D3D12_QUERY_TYPE Type, UINT StartIndex, UINT NumQueries, ID3D12Resource* pDestinationBuffer, UINT64 AlignedDestinationBufferOffset) {
    if (m_pReal) m_pReal->ResolveQueryData(pQueryHeap, Type, StartIndex, NumQueries, pDestinationBuffer, AlignedDestinationBufferOffset);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetPredication(ID3D12Resource* pBuffer, UINT64 AlignedBufferOffset, D3D12_PREDICATION_OP Operation) {
    if (m_pReal) m_pReal->SetPredication(pBuffer, AlignedBufferOffset, Operation);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::SetMarker(UINT Metadata, const void* pData, UINT Size) {
    if (m_pReal) m_pReal->SetMarker(Metadata, pData, Size);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::BeginEvent(UINT Metadata, const void* pData, UINT Size) {
    if (m_pReal) m_pReal->BeginEvent(Metadata, pData, Size);
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::EndEvent() {
    if (m_pReal) m_pReal->EndEvent();
}

void STDMETHODCALLTYPE WrappedD3D12GraphicsCommandList::ExecuteIndirect(ID3D12CommandSignature* pCommandSignature, UINT MaxCommandCount, ID3D12Resource* pArgumentBuffer, UINT64 ArgumentBufferOffset, ID3D12Resource* pCountBuffer, UINT64 CountBufferOffset) {
    if (m_pReal) m_pReal->ExecuteIndirect(pCommandSignature, MaxCommandCount, pArgumentBuffer, ArgumentBufferOffset, pCountBuffer, CountBufferOffset);
}

// ============================================================================
// WrappedD3D12CommandQueue
// ============================================================================

WrappedD3D12CommandQueue::WrappedD3D12CommandQueue(ID3D12CommandQueue* pReal, const D3D12_COMMAND_QUEUE_DESC* pDesc)
    : m_pReal(pReal), m_refCount(1)
{
    if (m_pReal) m_pReal->AddRef();
    if (pDesc) m_desc = *pDesc;
}

WrappedD3D12CommandQueue::~WrappedD3D12CommandQueue() {
    if (m_pReal) {
        m_pReal->Release();
        m_pReal = nullptr;
    }
}

HRESULT STDMETHODCALLTYPE WrappedD3D12CommandQueue::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    if (InlineIsEqualGUID(riid, IID_IUnknown) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D12Object)) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D12DeviceChild)) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D12Pageable)) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D12CommandQueue)))
    {
        *ppvObject = static_cast<ID3D12CommandQueue*>(this);
        AddRef();
        return S_OK;
    }
    if (m_pReal) return m_pReal->QueryInterface(riid, ppvObject);
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WrappedD3D12CommandQueue::AddRef() {
    return ++m_refCount;
}

ULONG STDMETHODCALLTYPE WrappedD3D12CommandQueue::Release() {
    ULONG count = --m_refCount;
    if (count == 0) {
        delete this;
    }
    return count;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12CommandQueue::GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) {
    return m_pReal ? m_pReal->GetPrivateData(guid, pDataSize, pData) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12CommandQueue::SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) {
    return m_pReal ? m_pReal->SetPrivateData(guid, DataSize, pData) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12CommandQueue::SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) {
    return m_pReal ? m_pReal->SetPrivateDataInterface(guid, pData) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12CommandQueue::SetName(LPCWSTR Name) {
    return m_pReal ? m_pReal->SetName(Name) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12CommandQueue::GetDevice(REFIID riid, void** ppvDevice) {
    return m_pReal ? m_pReal->GetDevice(riid, ppvDevice) : E_FAIL;
}

void STDMETHODCALLTYPE WrappedD3D12CommandQueue::UpdateTileMappings(ID3D12Resource* pResource, UINT NumResourceRegions, const D3D12_TILED_RESOURCE_COORDINATE* pResourceRegionStartCoordinates, const D3D12_TILE_REGION_SIZE* pResourceRegionSizes, ID3D12Heap* pHeap, UINT NumRanges, const D3D12_TILE_RANGE_FLAGS* pRangeFlags, const UINT* pHeapRangeOffsets, const UINT* pRangeTileCounts, D3D12_TILE_MAPPING_FLAGS Flags) {
    if (m_pReal) m_pReal->UpdateTileMappings(pResource, NumResourceRegions, pResourceRegionStartCoordinates, pResourceRegionSizes, pHeap, NumRanges, pRangeFlags, pHeapRangeOffsets, pRangeTileCounts, Flags);
}

void STDMETHODCALLTYPE WrappedD3D12CommandQueue::CopyTileMappings(ID3D12Resource* pDstResource, const D3D12_TILED_RESOURCE_COORDINATE* pDstRegionStartCoordinate, ID3D12Resource* pSrcResource, const D3D12_TILED_RESOURCE_COORDINATE* pSrcRegionStartCoordinate, const D3D12_TILE_REGION_SIZE* pTileRegionSize, D3D12_TILE_MAPPING_FLAGS Flags) {
    if (m_pReal) m_pReal->CopyTileMappings(pDstResource, pDstRegionStartCoordinate, pSrcResource, pSrcRegionStartCoordinate, pTileRegionSize, Flags);
}

void STDMETHODCALLTYPE WrappedD3D12CommandQueue::ExecuteCommandLists(UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // If D3D11 translation mode is enabled, replay recorded command batches onto D3D11 backend
    if (Config::Instance().IsD3D11TranslationEnabled() && D3D11Backend::Instance().IsInitialized()) {
        for (UINT i = 0; i < NumCommandLists; ++i) {
            auto* wrappedList = dynamic_cast<WrappedD3D12GraphicsCommandList*>(ppCommandLists[i]);
            if (wrappedList) {
                D3D11Backend::Instance().ReplayCommandBatch(wrappedList->GetCommandBatch());
            }
        }
    }

    // Pass through to native D3D12 hardware queue if present
    if (m_pReal && ppCommandLists && NumCommandLists > 0) {
        std::vector<ID3D12CommandList*> realLists;
        realLists.reserve(NumCommandLists);
        for (UINT i = 0; i < NumCommandLists; ++i) {
            auto* wrappedList = dynamic_cast<WrappedD3D12GraphicsCommandList*>(ppCommandLists[i]);
            if (wrappedList && wrappedList->GetReal()) {
                realLists.push_back(wrappedList->GetReal());
            } else {
                realLists.push_back(ppCommandLists[i]);
            }
        }
        m_pReal->ExecuteCommandLists(static_cast<UINT>(realLists.size()), realLists.data());
    }
}

void STDMETHODCALLTYPE WrappedD3D12CommandQueue::SetMarker(UINT Metadata, const void* pData, UINT Size) {
    if (m_pReal) m_pReal->SetMarker(Metadata, pData, Size);
}

void STDMETHODCALLTYPE WrappedD3D12CommandQueue::BeginEvent(UINT Metadata, const void* pData, UINT Size) {
    if (m_pReal) m_pReal->BeginEvent(Metadata, pData, Size);
}

void STDMETHODCALLTYPE WrappedD3D12CommandQueue::EndEvent() {
    if (m_pReal) m_pReal->EndEvent();
}

HRESULT STDMETHODCALLTYPE WrappedD3D12CommandQueue::Signal(ID3D12Fence* pFence, UINT64 Value) {
    return m_pReal ? m_pReal->Signal(pFence, Value) : S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12CommandQueue::Wait(ID3D12Fence* pFence, UINT64 Value) {
    return m_pReal ? m_pReal->Wait(pFence, Value) : S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12CommandQueue::GetTimestampFrequency(UINT64* pFrequency) {
    return m_pReal ? m_pReal->GetTimestampFrequency(pFrequency) : S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12CommandQueue::GetClockCalibration(UINT64* pGpuTimestamp, UINT64* pCpuTimestamp) {
    return m_pReal ? m_pReal->GetClockCalibration(pGpuTimestamp, pCpuTimestamp) : S_OK;
}

#ifdef WIDL_EXPLICIT_AGGREGATE_RETURNS
D3D12_COMMAND_QUEUE_DESC* STDMETHODCALLTYPE WrappedD3D12CommandQueue::GetDesc(D3D12_COMMAND_QUEUE_DESC* pRet) {
    if (pRet) *pRet = m_desc;
    return pRet;
}
#else
D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE WrappedD3D12CommandQueue::GetDesc() {
    return m_desc;
}
#endif

} // namespace kepler
