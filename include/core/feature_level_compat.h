#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>

#include <string>
#include <mutex>
#include <atomic>
#include <vector>

namespace kepler {

struct PolicyEvaluationResult {
    bool should_intercept = false;
    D3D_FEATURE_LEVEL native_level = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL requested_level = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL target_spoof_level = D3D_FEATURE_LEVEL_12_0;
    D3D_FEATURE_LEVEL creation_level = D3D_FEATURE_LEVEL_11_0;
    std::string reason;
};

// Safe, standard COM Wrapper implementing ID3D12Device through ID3D12Device5
class WrappedD3D12Device : public ID3D12Device5 {
public:
    WrappedD3D12Device(ID3D12Device* realDevice, D3D_FEATURE_LEVEL spoofedLevel);
    virtual ~WrappedD3D12Device();

    // IUnknown
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    virtual ULONG STDMETHODCALLTYPE AddRef() override;
    virtual ULONG STDMETHODCALLTYPE Release() override;

    // ID3D12Object
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) override;
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) override;
    virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) override;
    virtual HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override;

    // ID3D12Device
    virtual UINT STDMETHODCALLTYPE GetNodeCount() override;
    virtual HRESULT STDMETHODCALLTYPE CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC* pDesc, REFIID riid, void** ppCommandQueue) override;
    virtual HRESULT STDMETHODCALLTYPE CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE type, REFIID riid, void** ppCommandAllocator) override;
    virtual HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPipelineState) override;
    virtual HRESULT STDMETHODCALLTYPE CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPipelineState) override;
    virtual HRESULT STDMETHODCALLTYPE CreateCommandList(UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* pCommandAllocator, ID3D12PipelineState* pInitialState, REFIID riid, void** ppCommandList) override;
    virtual HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D12_FEATURE Feature, void* pFeatureSupportData, UINT FeatureSupportDataSize) override;
    virtual HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc, REFIID riid, void** ppvHeap) override;
    virtual UINT STDMETHODCALLTYPE GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) override;
    virtual HRESULT STDMETHODCALLTYPE CreateRootSignature(UINT nodeMask, const void* pBlobWithRootSignature, SIZE_T blobLengthInBytes, REFIID riid, void** ppvRootSignature) override;
    virtual void STDMETHODCALLTYPE CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
    virtual void STDMETHODCALLTYPE CreateShaderResourceView(ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
    virtual void STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D12Resource* pResource, ID3D12Resource* pCounterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
    virtual void STDMETHODCALLTYPE CreateRenderTargetView(ID3D12Resource* pResource, const D3D12_RENDER_TARGET_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
    virtual void STDMETHODCALLTYPE CreateDepthStencilView(ID3D12Resource* pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
    virtual void STDMETHODCALLTYPE CreateSampler(const D3D12_SAMPLER_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override;
    virtual void STDMETHODCALLTYPE CopyDescriptors(UINT NumDestDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts, const UINT* pDestDescriptorRangeSizes, UINT NumSrcDescriptorRanges, const D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts, const UINT* pSrcDescriptorRangeSizes, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) override;
    virtual void STDMETHODCALLTYPE CopyDescriptorsSimple(UINT NumDescriptors, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart, D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart, D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) override;
    virtual D3D12_RESOURCE_ALLOCATION_INFO* STDMETHODCALLTYPE GetResourceAllocationInfo(D3D12_RESOURCE_ALLOCATION_INFO* pRet, UINT visibleMask, UINT numResourceDescs, const D3D12_RESOURCE_DESC* pResourceDescs) override;
    virtual D3D12_HEAP_PROPERTIES* STDMETHODCALLTYPE GetCustomHeapProperties(D3D12_HEAP_PROPERTIES* pRet, UINT nodeMask, D3D12_HEAP_TYPE heapType) override;
    virtual HRESULT STDMETHODCALLTYPE CreateCommittedResource(const D3D12_HEAP_PROPERTIES* pHeapProperties, D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialResourceState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource, void** ppvResource) override;
    virtual HRESULT STDMETHODCALLTYPE CreateHeap(const D3D12_HEAP_DESC* pDesc, REFIID riid, void** ppvHeap) override;
    virtual HRESULT STDMETHODCALLTYPE CreatePlacedResource(ID3D12Heap* pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) override;
    virtual HRESULT STDMETHODCALLTYPE CreateReservedResource(const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) override;
    virtual HRESULT STDMETHODCALLTYPE CreateSharedHandle(ID3D12DeviceChild* pObject, const SECURITY_ATTRIBUTES* pAttributes, DWORD Access, LPCWSTR Name, HANDLE* pHandle) override;
    virtual HRESULT STDMETHODCALLTYPE OpenSharedHandle(HANDLE NTHandle, REFIID riid, void** ppvObj) override;
    virtual HRESULT STDMETHODCALLTYPE OpenSharedHandleByName(LPCWSTR Name, DWORD Access, HANDLE* pNTHandle) override;
    virtual HRESULT STDMETHODCALLTYPE MakeResident(UINT NumObjects, ID3D12Pageable* const* ppObjects) override;
    virtual HRESULT STDMETHODCALLTYPE Evict(UINT NumObjects, ID3D12Pageable* const* ppObjects) override;
    virtual HRESULT STDMETHODCALLTYPE CreateFence(UINT64 InitialValue, D3D12_FENCE_FLAGS Flags, REFIID riid, void** ppFence) override;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override;
    virtual void STDMETHODCALLTYPE GetCopyableFootprints(const D3D12_RESOURCE_DESC* pResourceDesc, UINT FirstSubresource, UINT NumSubresources, UINT64 BaseOffset, D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts, UINT* pNumRows, UINT64* pRowSizeInBytes, UINT64* pTotalBytes) override;
    virtual HRESULT STDMETHODCALLTYPE CreateQueryHeap(const D3D12_QUERY_HEAP_DESC* pDesc, REFIID riid, void** ppvHeap) override;
    virtual HRESULT STDMETHODCALLTYPE SetStablePowerState(BOOL Enable) override;
    virtual HRESULT STDMETHODCALLTYPE CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC* pDesc, ID3D12RootSignature* pRootSignature, REFIID riid, void** ppv) override;
    virtual void STDMETHODCALLTYPE GetResourceTiling(ID3D12Resource* pTiledResource, UINT* pNumTilesForEntireResource, D3D12_PACKED_MIP_INFO* pPackedMipDesc, D3D12_TILE_SHAPE* pStandardTileShapeForNonPackedMips, UINT* pNumSubresourceTilings, UINT FirstSubresourceTilingToGet, D3D12_SUBRESOURCE_TILING* pSubresourceTilingsForNonPackedMips) override;
    virtual LUID* STDMETHODCALLTYPE GetAdapterLuid(LUID* pLuid) override;

    // ID3D12Device1
    virtual HRESULT STDMETHODCALLTYPE CreatePipelineLibrary(const void* pLibraryBlob, SIZE_T BlobLength, REFIID riid, void** ppPipelineLibrary) override;
    virtual HRESULT STDMETHODCALLTYPE SetEventOnMultipleFenceCompletion(ID3D12Fence* const* ppFences, const UINT64* pFenceValues, UINT NumFences, D3D12_MULTIPLE_FENCE_WAIT_FLAGS Flags, HANDLE hEvent) override;
    virtual HRESULT STDMETHODCALLTYPE SetResidencyPriority(UINT NumObjects, ID3D12Pageable* const* ppObjects, const D3D12_RESIDENCY_PRIORITY* pPriorities) override;

    // ID3D12Device2
    virtual HRESULT STDMETHODCALLTYPE CreatePipelineState(const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid, void** ppPipelineState) override;

    // ID3D12Device3
    virtual HRESULT STDMETHODCALLTYPE OpenExistingHeapFromAddress(const void* address, REFIID riid, void** ppvHeap) override;
    virtual HRESULT STDMETHODCALLTYPE OpenExistingHeapFromFileMapping(HANDLE file_mapping, REFIID riid, void** ppvHeap) override;
    virtual HRESULT STDMETHODCALLTYPE EnqueueMakeResident(D3D12_RESIDENCY_FLAGS flags, UINT num_objects, ID3D12Pageable* const* objects, ID3D12Fence* fence, UINT64 fence_value) override;

    // ID3D12Device4
    virtual HRESULT STDMETHODCALLTYPE CreateCommandList1(UINT node_mask, D3D12_COMMAND_LIST_TYPE type, D3D12_COMMAND_LIST_FLAGS flags, REFIID riid, void** ppCommandList) override;
    virtual HRESULT STDMETHODCALLTYPE CreateProtectedResourceSession(const D3D12_PROTECTED_RESOURCE_SESSION_DESC* desc, REFIID riid, void** ppSession) override;
    virtual HRESULT STDMETHODCALLTYPE CreateCommittedResource1(const D3D12_HEAP_PROPERTIES* heap_properties, D3D12_HEAP_FLAGS heap_flags, const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES initial_resource_state, const D3D12_CLEAR_VALUE* optimized_clear_value, ID3D12ProtectedResourceSession* protected_session, REFIID riid_resource, void** ppvResource) override;
    virtual HRESULT STDMETHODCALLTYPE CreateHeap1(const D3D12_HEAP_DESC* desc, ID3D12ProtectedResourceSession* protected_session, REFIID riid, void** ppvHeap) override;
    virtual HRESULT STDMETHODCALLTYPE CreateReservedResource1(const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE* optimized_clear_value, ID3D12ProtectedResourceSession* protected_session, REFIID riid, void** ppvResource) override;
    virtual D3D12_RESOURCE_ALLOCATION_INFO* STDMETHODCALLTYPE GetResourceAllocationInfo1(D3D12_RESOURCE_ALLOCATION_INFO* pRet, UINT visible_mask, UINT resource_descs_count, const D3D12_RESOURCE_DESC* resource_descs, D3D12_RESOURCE_ALLOCATION_INFO1* resource_allocation_info1) override;

    // ID3D12Device5
    virtual HRESULT STDMETHODCALLTYPE CreateLifetimeTracker(ID3D12LifetimeOwner* owner, REFIID riid, void** ppvTracker) override;
    virtual void STDMETHODCALLTYPE RemoveDevice() override;
    virtual HRESULT STDMETHODCALLTYPE EnumerateMetaCommands(UINT* meta_commands_count, D3D12_META_COMMAND_DESC* descs) override;
    virtual HRESULT STDMETHODCALLTYPE EnumerateMetaCommandParameters(REFGUID command_id, D3D12_META_COMMAND_PARAMETER_STAGE stage, UINT* total_structure_size_in_bytes, UINT* parameter_count, D3D12_META_COMMAND_PARAMETER_DESC* parameter_descs) override;
    virtual HRESULT STDMETHODCALLTYPE CreateMetaCommand(REFGUID command_id, UINT node_mask, const void* creation_parameters_data, SIZE_T creation_parameters_data_size_in_bytes, REFIID riid, void** ppvMetaCommand) override;
    virtual HRESULT STDMETHODCALLTYPE CreateStateObject(const D3D12_STATE_OBJECT_DESC* desc, REFIID riid, void** ppvStateObject) override;
    virtual void STDMETHODCALLTYPE GetRaytracingAccelerationStructurePrebuildInfo(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS* desc, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO* info) override;
    virtual D3D12_DRIVER_MATCHING_IDENTIFIER_STATUS STDMETHODCALLTYPE CheckDriverMatchingIdentifier(D3D12_SERIALIZED_DATA_TYPE serialized_data_type, const D3D12_SERIALIZED_DATA_DRIVER_MATCHING_IDENTIFIER* identifier_to_check) override;

    ID3D12Device* GetRealDevice() const { return m_pReal; }

    // Test seam for controlled device-removal verification
    void SetSimulatedDeviceRemoval(HRESULT hr) { m_simulatedDeviceRemoval = hr; }
    HRESULT GetSimulatedDeviceRemoval() const { return m_simulatedDeviceRemoval; }

private:
    ID3D12Device* m_pReal = nullptr;
    ID3D12Device1* m_pReal1 = nullptr;
    ID3D12Device2* m_pReal2 = nullptr;
    ID3D12Device3* m_pReal3 = nullptr;
    ID3D12Device4* m_pReal4 = nullptr;
    ID3D12Device5* m_pReal5 = nullptr;
    std::atomic<ULONG> m_refCount{ 1 };
    D3D_FEATURE_LEVEL m_spoofedLevel = D3D_FEATURE_LEVEL_12_0;
    HRESULT m_simulatedDeviceRemoval = S_OK;
};

class FeatureLevelCompat {
public:
    static FeatureLevelCompat& Instance();

    // Pure policy evaluation (deterministic, testable in isolation without GPU)
    PolicyEvaluationResult EvaluatePolicy(
        bool spoofEnabled,
        const std::string& targetFeatureLevelStr,
        D3D_FEATURE_LEVEL nativeMaxLevel,
        D3D_FEATURE_LEVEL requestedLevel);

    // Queries native Direct3D 12 maximum feature level from the actual hardware adapter (cached)
    D3D_FEATURE_LEVEL DetectNativeMaxFeatureLevel(IUnknown* pAdapter);

    // Reset cached native capability (useful for multi-adapter testing)
    void InvalidateCachedNativeLevel();

    // Parses configuration string (e.g., "12_0", "12_1", "11_1", "11_0") to D3D_FEATURE_LEVEL
    static D3D_FEATURE_LEVEL ParseFeatureLevelString(const std::string& str);

    // Converts D3D_FEATURE_LEVEL to standardized short string ("11_0", "12_0", etc.)
    static std::string FeatureLevelToShortString(D3D_FEATURE_LEVEL fl);

    // Creates a safe wrapped device instance
    HRESULT WrapDevice(ID3D12Device* pRealDevice, D3D_FEATURE_LEVEL targetSpoofLevel, REFIID riid, void** ppOutDevice);

private:
    FeatureLevelCompat() = default;
    ~FeatureLevelCompat() = default;
    FeatureLevelCompat(const FeatureLevelCompat&) = delete;
    FeatureLevelCompat& operator=(const FeatureLevelCompat&) = delete;

    std::mutex m_mutex;
    bool m_nativeLevelCached = false;
    D3D_FEATURE_LEVEL m_cachedNativeLevel = D3D_FEATURE_LEVEL_11_0;
};

} // namespace kepler
