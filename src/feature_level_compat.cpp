#include "core/feature_level_compat.h"
#include "core/proxy_loader.h"
#include "core/logger.h"
#include "core/config.h"

#include <algorithm>
#include <sstream>

namespace kepler {

// ============================================================================
// WrappedD3D12Device Implementation
// ============================================================================

WrappedD3D12Device::WrappedD3D12Device(ID3D12Device* realDevice, D3D_FEATURE_LEVEL spoofedLevel)
    : m_pReal(realDevice), m_refCount(1), m_spoofedLevel(spoofedLevel)
{
    if (m_pReal) {
        m_pReal->AddRef();
        m_pReal->QueryInterface(__uuidof(ID3D12Device1), reinterpret_cast<void**>(&m_pReal1));
    }
}

WrappedD3D12Device::~WrappedD3D12Device() {
    if (m_pReal1) {
        m_pReal1->Release();
        m_pReal1 = nullptr;
    }
    if (m_pReal) {
        m_pReal->Release();
        m_pReal = nullptr;
    }
}

// IUnknown
HRESULT STDMETHODCALLTYPE WrappedD3D12Device::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;

    if (InlineIsEqualGUID(riid, IID_IUnknown) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D12Object)) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D12Device)))
    {
        *ppvObject = static_cast<ID3D12Device*>(this);
        AddRef();
        return S_OK;
    }

    if (InlineIsEqualGUID(riid, __uuidof(ID3D12Device1))) {
        *ppvObject = static_cast<ID3D12Device1*>(this);
        AddRef();
        return S_OK;
    }

    // Forward other interfaces (e.g. ID3D12Device2..10, ID3D12DebugDevice) to real driver
    if (m_pReal) {
        return m_pReal->QueryInterface(riid, ppvObject);
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WrappedD3D12Device::AddRef() {
    return ++m_refCount;
}

ULONG STDMETHODCALLTYPE WrappedD3D12Device::Release() {
    ULONG count = --m_refCount;
    if (count == 0) {
        LOG_INFO("WrappedD3D12Device: RefCount reached zero, destroying wrapper.");
        delete this;
    }
    return count;
}

// ID3D12Object
HRESULT STDMETHODCALLTYPE WrappedD3D12Device::GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) {
    return m_pReal ? m_pReal->GetPrivateData(guid, pDataSize, pData) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) {
    return m_pReal ? m_pReal->SetPrivateData(guid, DataSize, pData) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) {
    return m_pReal ? m_pReal->SetPrivateDataInterface(guid, pData) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::SetName(LPCWSTR Name) {
    return m_pReal ? m_pReal->SetName(Name) : E_FAIL;
}

// ID3D12Device
UINT STDMETHODCALLTYPE WrappedD3D12Device::GetNodeCount() {
    return m_pReal ? m_pReal->GetNodeCount() : 1;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC* pDesc, REFIID riid, void** ppCommandQueue) {
    return m_pReal ? m_pReal->CreateCommandQueue(pDesc, riid, ppCommandQueue) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE type, REFIID riid, void** ppCommandAllocator) {
    return m_pReal ? m_pReal->CreateCommandAllocator(type, riid, ppCommandAllocator) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPipelineState) {
    return m_pReal ? m_pReal->CreateGraphicsPipelineState(pDesc, riid, ppPipelineState) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPipelineState) {
    return m_pReal ? m_pReal->CreateComputePipelineState(pDesc, riid, ppPipelineState) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateCommandList(UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* pCommandAllocator, ID3D12PipelineState* pInitialState, REFIID riid, void** ppCommandList) {
    return m_pReal ? m_pReal->CreateCommandList(nodeMask, type, pCommandAllocator, pInitialState, riid, ppCommandList) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CheckFeatureSupport(D3D12_FEATURE Feature, void* pFeatureSupportData, UINT FeatureSupportDataSize) {
    if (!m_pReal) return E_FAIL;

    if (Feature == D3D12_FEATURE_FEATURE_LEVELS && pFeatureSupportData &&
        FeatureSupportDataSize >= sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS))
    {
        auto* pData = reinterpret_cast<D3D12_FEATURE_DATA_FEATURE_LEVELS*>(pFeatureSupportData);
        D3D_FEATURE_LEVEL reported = m_spoofedLevel;

        if (pData->pFeatureLevelsRequested && pData->NumFeatureLevels > 0) {
            D3D_FEATURE_LEVEL best = D3D_FEATURE_LEVEL_11_0;
            bool found = false;
            for (UINT i = 0; i < pData->NumFeatureLevels; ++i) {
                D3D_FEATURE_LEVEL req = pData->pFeatureLevelsRequested[i];
                if (req <= m_spoofedLevel) {
                    if (!found || req > best) {
                        best = req;
                        found = true;
                    }
                }
            }
            if (found) {
                reported = best;
            }
        }

        pData->MaxSupportedFeatureLevel = reported;
        LOG_HOOK("[COMPAT] Spoofed CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS) -> MaxSupportedFeatureLevel = " +
                 Logger::FeatureLevelToString(reported) + " (Native hardware is 11_0)");
        return S_OK;
    }

    // Pass through all real GPU queries to native driver
    return m_pReal->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize);
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc, REFIID riid, void** ppvHeap) {
    return m_pReal ? m_pReal->CreateDescriptorHeap(pDescriptorHeapDesc, riid, ppvHeap) : E_FAIL;
}

UINT STDMETHODCALLTYPE WrappedD3D12Device::GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) {
    return m_pReal ? m_pReal->GetDescriptorHandleIncrementSize(DescriptorHeapType) : 0;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateRootSignature(UINT nodeMask, const void* pBlobWithRootSignature, SIZE_T blobLengthInBytes, REFIID riid, void** ppvRootSignature) {
    return m_pReal ? m_pReal->CreateRootSignature(nodeMask, pBlobWithRootSignature, blobLengthInBytes, riid, ppvRootSignature) : E_FAIL;
}

void STDMETHODCALLTYPE WrappedD3D12Device::CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
    if (m_pReal) m_pReal->CreateConstantBufferView(pDesc, DestDescriptor);
}

void STDMETHODCALLTYPE WrappedD3D12Device::CreateShaderResourceView(ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
    if (m_pReal) m_pReal->CreateShaderResourceView(pResource, pDesc, DestDescriptor);
}

void STDMETHODCALLTYPE WrappedD3D12Device::CreateUnorderedAccessView(ID3D12Resource* pResource, ID3D12Resource* pCounterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
    if (m_pReal) m_pReal->CreateUnorderedAccessView(pResource, pCounterResource, pDesc, DestDescriptor);
}

void STDMETHODCALLTYPE WrappedD3D12Device::CreateRenderTargetView(ID3D12Resource* pResource, const D3D12_RENDER_TARGET_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
    if (m_pReal) m_pReal->CreateRenderTargetView(pResource, pDesc, DestDescriptor);
}

void STDMETHODCALLTYPE WrappedD3D12Device::CreateDepthStencilView(ID3D12Resource* pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
    if (m_pReal) m_pReal->CreateDepthStencilView(pResource, pDesc, DestDescriptor);
}

void STDMETHODCALLTYPE WrappedD3D12Device::CreateSampler(const D3D12_SAMPLER_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
    if (m_pReal) m_pReal->CreateSampler(pDesc, DestDescriptor);
}

void STDMETHODCALLTYPE WrappedD3D12Device::CopyDescriptors(UINT NumDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts, const UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts, const UINT* pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) {
    if (m_pReal) m_pReal->CopyDescriptors(NumDestDescriptorRanges, pDestDescriptorRangeStarts, pDestDescriptorRangeSizes, NumSrcDescriptorRanges, pSrcDescriptorRangeStarts, pSrcDescriptorRangeSizes, DescriptorHeapsType);
}

void STDMETHODCALLTYPE WrappedD3D12Device::CopyDescriptorsSimple(UINT NumDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart, D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) {
    if (m_pReal) m_pReal->CopyDescriptorsSimple(NumDescriptors, DestDescriptorRangeStart, SrcDescriptorRangeStart, DescriptorHeapsType);
}

D3D12_RESOURCE_ALLOCATION_INFO* STDMETHODCALLTYPE WrappedD3D12Device::GetResourceAllocationInfo(D3D12_RESOURCE_ALLOCATION_INFO* pRet, UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC* pResourceDescs) {
    return m_pReal ? m_pReal->GetResourceAllocationInfo(pRet, visibleMask, numResourceDescs, pResourceDescs) : pRet;
}

D3D12_HEAP_PROPERTIES* STDMETHODCALLTYPE WrappedD3D12Device::GetCustomHeapProperties(D3D12_HEAP_PROPERTIES* pRet, UINT nodeMask, D3D12_HEAP_TYPE heapType) {
    return m_pReal ? m_pReal->GetCustomHeapProperties(pRet, nodeMask, heapType) : pRet;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateCommittedResource(const D3D12_HEAP_PROPERTIES* pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource, void** ppvResource) {
    return m_pReal ? m_pReal->CreateCommittedResource(pHeapProperties, HeapFlags, pDesc, InitialResourceState, pOptimizedClearValue, riidResource, ppvResource) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateHeap(const D3D12_HEAP_DESC* pDesc, REFIID riid, void** ppvHeap) {
    return m_pReal ? m_pReal->CreateHeap(pDesc, riid, ppvHeap) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreatePlacedResource(ID3D12Heap* pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) {
    return m_pReal ? m_pReal->CreatePlacedResource(pHeap, HeapOffset, pDesc, InitialState, pOptimizedClearValue, riid, ppvResource) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateReservedResource(const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) {
    return m_pReal ? m_pReal->CreateReservedResource(pDesc, InitialState, pOptimizedClearValue, riid, ppvResource) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateSharedHandle(ID3D12DeviceChild* pObject, const SECURITY_ATTRIBUTES* pAttributes, DWORD Access, LPCWSTR Name, HANDLE* pHandle) {
    return m_pReal ? m_pReal->CreateSharedHandle(pObject, pAttributes, Access, Name, pHandle) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::OpenSharedHandle(HANDLE NTHandle, REFIID riid, void** ppvObj) {
    return m_pReal ? m_pReal->OpenSharedHandle(NTHandle, riid, ppvObj) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::OpenSharedHandleByName(LPCWSTR Name, DWORD Access, HANDLE* pNTHandle) {
    return m_pReal ? m_pReal->OpenSharedHandleByName(Name, Access, pNTHandle) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::MakeResident(UINT NumObjects, ID3D12Pageable* const* ppObjects) {
    return m_pReal ? m_pReal->MakeResident(NumObjects, ppObjects) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::Evict(UINT NumObjects, ID3D12Pageable* const* ppObjects) {
    return m_pReal ? m_pReal->Evict(NumObjects, ppObjects) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateFence(UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void** ppFence) {
    return m_pReal ? m_pReal->CreateFence(InitialValue, Flags, riid, ppFence) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::GetDeviceRemovedReason() {
    return m_pReal ? m_pReal->GetDeviceRemovedReason() : S_OK;
}

void STDMETHODCALLTYPE WrappedD3D12Device::GetCopyableFootprints(const D3D12_RESOURCE_DESC* pResourceDesc, UINT FirstSubresource, UINT NumSubresources, UINT64 BaseOffset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts, UINT* pNumRows, UINT64* pRowSizeInBytes, UINT64* pTotalBytes) {
    if (m_pReal) m_pReal->GetCopyableFootprints(pResourceDesc, FirstSubresource, NumSubresources, BaseOffset, pLayouts, pNumRows, pRowSizeInBytes, pTotalBytes);
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateQueryHeap(const D3D12_QUERY_HEAP_DESC* pDesc, REFIID riid, void** ppvHeap) {
    return m_pReal ? m_pReal->CreateQueryHeap(pDesc, riid, ppvHeap) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::SetStablePowerState(BOOL Enable) {
    return m_pReal ? m_pReal->SetStablePowerState(Enable) : S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC* pDesc, ID3D12RootSignature* pRootSignature, REFIID riid, void** ppv) {
    return m_pReal ? m_pReal->CreateCommandSignature(pDesc, pRootSignature, riid, ppv) : E_FAIL;
}

void STDMETHODCALLTYPE WrappedD3D12Device::GetResourceTiling(ID3D12Resource* pTiledResource, UINT* pNumTilesForEntireResource, D3D12_PACKED_MIP_INFO* pPackedMipDesc, D3D12_TILE_SHAPE* pStandardTileShapeForNonPackedMips, UINT* pNumSubresourceTilings, UINT FirstSubresourceTilingToGet, D3D12_SUBRESOURCE_TILING* pSubresourceTilingsForNonPackedMips) {
    if (m_pReal) m_pReal->GetResourceTiling(pTiledResource, pNumTilesForEntireResource, pPackedMipDesc, pStandardTileShapeForNonPackedMips, pNumSubresourceTilings, FirstSubresourceTilingToGet, pSubresourceTilingsForNonPackedMips);
}

LUID* STDMETHODCALLTYPE WrappedD3D12Device::GetAdapterLuid(LUID* pLuid) {
    return m_pReal ? m_pReal->GetAdapterLuid(pLuid) : pLuid;
}

// ID3D12Device1
HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreatePipelineLibrary(const void* pLibraryBlob, SIZE_T BlobLength, REFIID riid, void** ppPipelineLibrary) {
    return m_pReal1 ? m_pReal1->CreatePipelineLibrary(pLibraryBlob, BlobLength, riid, ppPipelineLibrary) : E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::SetEventOnMultipleFenceCompletion(ID3D12Fence* const* ppFences, const UINT64* pFenceValues, UINT NumFences, D3D12_MULTIPLE_FENCE_WAIT_FLAGS Flags, HANDLE hEvent) {
    return m_pReal1 ? m_pReal1->SetEventOnMultipleFenceCompletion(ppFences, pFenceValues, NumFences, Flags, hEvent) : E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::SetResidencyPriority(UINT NumObjects, ID3D12Pageable* const* ppObjects, const D3D12_RESIDENCY_PRIORITY* pPriorities) {
    return m_pReal1 ? m_pReal1->SetResidencyPriority(NumObjects, ppObjects, pPriorities) : E_NOTIMPL;
}

// ============================================================================
// FeatureLevelCompat Singleton Implementation
// ============================================================================

FeatureLevelCompat& FeatureLevelCompat::Instance() {
    static FeatureLevelCompat s_instance;
    return s_instance;
}

D3D_FEATURE_LEVEL FeatureLevelCompat::ParseFeatureLevelString(const std::string& str) {
    if (str == "12_2" || str == "12.2" || str == "0xc200" || str == "0XC200") {
        return D3D_FEATURE_LEVEL_12_2;
    }
    if (str == "12_1" || str == "12.1" || str == "0xc100" || str == "0XC100") {
        return D3D_FEATURE_LEVEL_12_1;
    }
    if (str == "12_0" || str == "12.0" || str == "0xc000" || str == "0XC000") {
        return D3D_FEATURE_LEVEL_12_0;
    }
    if (str == "11_1" || str == "11.1" || str == "0xb100" || str == "0XB100") {
        return D3D_FEATURE_LEVEL_11_1;
    }
    if (str == "11_0" || str == "11.0" || str == "0xb000" || str == "0XB000") {
        return D3D_FEATURE_LEVEL_11_0;
    }
    return D3D_FEATURE_LEVEL_12_0;
}

std::string FeatureLevelCompat::FeatureLevelToShortString(D3D_FEATURE_LEVEL fl) {
    switch (fl) {
        case D3D_FEATURE_LEVEL_12_2: return "12_2";
        case D3D_FEATURE_LEVEL_12_1: return "12_1";
        case D3D_FEATURE_LEVEL_12_0: return "12_0";
        case D3D_FEATURE_LEVEL_11_1: return "11_1";
        case D3D_FEATURE_LEVEL_11_0: return "11_0";
        case D3D_FEATURE_LEVEL_10_1: return "10_1";
        case D3D_FEATURE_LEVEL_10_0: return "10_0";
        case D3D_FEATURE_LEVEL_9_3:  return "9_3";
        case D3D_FEATURE_LEVEL_9_2:  return "9_2";
        case D3D_FEATURE_LEVEL_9_1:  return "9_1";
        default: {
            char buf[32];
            snprintf(buf, sizeof(buf), "0x%X", (unsigned int)fl);
            return std::string(buf);
        }
    }
}

PolicyEvaluationResult FeatureLevelCompat::EvaluatePolicy(
    bool spoofEnabled,
    const std::string& targetFeatureLevelStr,
    D3D_FEATURE_LEVEL nativeMaxLevel,
    D3D_FEATURE_LEVEL requestedLevel)
{
    PolicyEvaluationResult result;
    result.native_level = nativeMaxLevel;
    result.requested_level = requestedLevel;
    result.target_spoof_level = ParseFeatureLevelString(targetFeatureLevelStr);

    if (!spoofEnabled) {
        result.should_intercept = false;
        result.creation_level = requestedLevel;
        result.reason = "Compatibility spoofing is disabled in kepler_bridge.ini ([Spoof] EnableFeatureLevelSpoof=0).";
        return result;
    }

    if (requestedLevel <= nativeMaxLevel) {
        result.should_intercept = false;
        result.creation_level = requestedLevel;
        result.reason = "Requested feature level (" + FeatureLevelToShortString(requestedLevel) +
                        ") is supported natively by hardware (" + FeatureLevelToShortString(nativeMaxLevel) + ").";
        return result;
    }

    // Interception active
    result.should_intercept = true;
    result.creation_level = nativeMaxLevel;
    result.reason = "Requested feature level (" + FeatureLevelToShortString(requestedLevel) +
                    ") exceeds native hardware capability (" + FeatureLevelToShortString(nativeMaxLevel) +
                    "). Intercepting device creation at native level and attaching compatibility engine.";
    return result;
}

D3D_FEATURE_LEVEL FeatureLevelCompat::DetectNativeMaxFeatureLevel(IUnknown* pAdapter) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_nativeLevelCached) {
        return m_cachedNativeLevel;
    }

    auto& loader = ProxyLoader::Instance();
    if (!loader.EnsureInitialized() || !loader.RealD3D12CreateDevice) {
        LOG_WARN("Cannot probe native feature level: Real D3D12CreateDevice unavailable. Assuming 11_0.");
        m_cachedNativeLevel = D3D_FEATURE_LEVEL_11_0;
        m_nativeLevelCached = true;
        return m_cachedNativeLevel;
    }

    static const D3D_FEATURE_LEVEL testLevels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };

    for (auto fl : testLevels) {
        ID3D12Device* probeDevice = nullptr;
        HRESULT hr = loader.RealD3D12CreateDevice(pAdapter, fl, __uuidof(ID3D12Device), reinterpret_cast<void**>(&probeDevice));
        if (SUCCEEDED(hr) && probeDevice) {
            probeDevice->Release();
            LOG_INFO("Hardware probe detected native maximum D3D12 feature level: " + Logger::FeatureLevelToString(fl));
            m_cachedNativeLevel = fl;
            m_nativeLevelCached = true;
            return m_cachedNativeLevel;
        }
    }

    LOG_WARN("Hardware probe did not accept any standard D3D12 feature levels. Defaulting to 11_0.");
    m_cachedNativeLevel = D3D_FEATURE_LEVEL_11_0;
    m_nativeLevelCached = true;
    return m_cachedNativeLevel;
}

void FeatureLevelCompat::InvalidateCachedNativeLevel() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_nativeLevelCached = false;
}

HRESULT FeatureLevelCompat::WrapDevice(
    ID3D12Device* pRealDevice,
    D3D_FEATURE_LEVEL targetSpoofLevel,
    REFIID riid,
    void** ppOutDevice)
{
    if (!pRealDevice || !ppOutDevice) {
        return E_POINTER;
    }

    WrappedD3D12Device* wrapper = new (std::nothrow) WrappedD3D12Device(pRealDevice, targetSpoofLevel);
    if (!wrapper) {
        return E_OUTOFMEMORY;
    }

    HRESULT hr = wrapper->QueryInterface(riid, ppOutDevice);
    wrapper->Release(); // Releases temporary constructor ref; QueryInterface added the caller's ref
    return hr;
}

} // namespace kepler
