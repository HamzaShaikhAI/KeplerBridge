#include "core/feature_level_compat.h"
#include "core/proxy_loader.h"
#include "core/logger.h"
#include "core/config.h"
#include "downgrader/pso_hook.h"
#include "downgrader/texture_clamper.h"
#include "translator/wrapped_command_objects.h"
#include "translator/d3d11_backend.h"
#include "translator/resource_manager.h"
#include "translator/command_translator.h"

#include <algorithm>
#include <sstream>

namespace kepler {

// ============================================================================
// WrappedD3D12Device Implementation
// ============================================================================

static std::atomic<int> s_activeDevices{ 0 };

WrappedD3D12Device::WrappedD3D12Device(ID3D12Device* realDevice, D3D_FEATURE_LEVEL spoofedLevel)
    : m_pReal(realDevice), m_refCount(1), m_spoofedLevel(spoofedLevel)
{
    s_activeDevices++;
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
    if (--s_activeDevices == 0) {
        if (D3D11Backend::Instance().IsInitialized()) {
            ResourceManager::Instance().Clear();
            D3D11Backend::Instance().Shutdown();
        }
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
    if (!pDesc || !ppCommandQueue) return E_POINTER;
    *ppCommandQueue = nullptr;
    if (!m_pReal) return E_FAIL;

    if (Config::Instance().IsD3D11TranslationEnabled() || Config::Instance().IsStateTrackingEnabled()) {
        if (Config::Instance().IsD3D11TranslationEnabled() && !D3D11Backend::Instance().IsInitialized()) {
            IDXGIAdapter* pAdapter = D3D11Backend::GetMatchingAdapter(m_pReal);
            D3D11Backend::Instance().Initialize(pAdapter);
            if (pAdapter) pAdapter->Release();
        }
        ID3D12CommandQueue* pRealQueue = nullptr;
        HRESULT hr = m_pReal->CreateCommandQueue(pDesc, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&pRealQueue));
        if (FAILED(hr)) return hr;

        auto* wrapped = new WrappedD3D12CommandQueue(pRealQueue, pDesc, this);
        if (pRealQueue) pRealQueue->Release();

        HRESULT hrQI = wrapped->QueryInterface(riid, ppCommandQueue);
        wrapped->Release();
        return hrQI;
    }

    return m_pReal->CreateCommandQueue(pDesc, riid, ppCommandQueue);
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE type, REFIID riid, void** ppCommandAllocator) {
    if (!ppCommandAllocator) return E_POINTER;
    *ppCommandAllocator = nullptr;
    if (!m_pReal) return E_FAIL;

    if (Config::Instance().IsD3D11TranslationEnabled() || Config::Instance().IsStateTrackingEnabled()) {
        ID3D12CommandAllocator* pRealAlloc = nullptr;
        HRESULT hr = m_pReal->CreateCommandAllocator(type, __uuidof(ID3D12CommandAllocator), reinterpret_cast<void**>(&pRealAlloc));
        if (FAILED(hr)) return hr;

        auto* wrapped = new WrappedD3D12CommandAllocator(pRealAlloc, type, this);
        if (pRealAlloc) pRealAlloc->Release();

        HRESULT hrQI = wrapped->QueryInterface(riid, ppCommandAllocator);
        wrapped->Release();
        return hrQI;
    }

    return m_pReal->CreateCommandAllocator(type, riid, ppCommandAllocator);
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPipelineState) {
    return PSOHook::InterceptCreateGraphicsPipelineState(m_pReal, pDesc, riid, ppPipelineState);
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc, REFIID riid, void** ppPipelineState) {
    return PSOHook::InterceptCreateComputePipelineState(m_pReal, pDesc, riid, ppPipelineState);
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateCommandList(UINT nodeMask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator* pCommandAllocator, ID3D12PipelineState* pInitialState, REFIID riid, void** ppCommandList) {
    if (!pCommandAllocator || !ppCommandList) return E_POINTER;
    *ppCommandList = nullptr;
    if (!m_pReal) return E_FAIL;

    if (Config::Instance().IsD3D11TranslationEnabled() || Config::Instance().IsStateTrackingEnabled()) {
        ID3D12CommandAllocator* realAlloc = pCommandAllocator;
        auto* wrappedAlloc = dynamic_cast<WrappedD3D12CommandAllocator*>(pCommandAllocator);
        if (wrappedAlloc && wrappedAlloc->GetReal()) {
            realAlloc = wrappedAlloc->GetReal();
        }

        ID3D12GraphicsCommandList* pRealList = nullptr;
        HRESULT hr = m_pReal->CreateCommandList(nodeMask, type, realAlloc, pInitialState, __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&pRealList));
        if (FAILED(hr)) return hr;

        auto* wrapped = new WrappedD3D12GraphicsCommandList(pRealList, type, pCommandAllocator, this);
        if (pInitialState) {
            wrapped->SetPipelineState(pInitialState);
        }
        if (pRealList) pRealList->Release();

        HRESULT hrQI = wrapped->QueryInterface(riid, ppCommandList);
        wrapped->Release();
        return hrQI;
    }

    return m_pReal->CreateCommandList(nodeMask, type, pCommandAllocator, pInitialState, riid, ppCommandList);
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
    if (!pDescriptorHeapDesc || !ppvHeap) return E_POINTER;
    *ppvHeap = nullptr;
    return m_pReal ? m_pReal->CreateDescriptorHeap(pDescriptorHeapDesc, riid, ppvHeap) : E_FAIL;
}

UINT STDMETHODCALLTYPE WrappedD3D12Device::GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) {
    return m_pReal ? m_pReal->GetDescriptorHandleIncrementSize(DescriptorHeapType) : 0;
}

static bool ParseRootSignatureBlob(const void* pBlob, SIZE_T blobLength, TranslatedRootSignature& outSig) {
    if (!pBlob || blobLength == 0) return false;
    outSig = {};

    auto& loader = ProxyLoader::Instance();
    loader.EnsureInitialized();

    if (loader.RealD3D12CreateVersionedRootSignatureDeserializer) {
        ID3D12VersionedRootSignatureDeserializer* pDeserializer = nullptr;
        HRESULT hr = loader.RealD3D12CreateVersionedRootSignatureDeserializer(
            pBlob, blobLength, IID_PPV_ARGS(&pDeserializer));
        if (SUCCEEDED(hr) && pDeserializer) {
            const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pVerDesc = pDeserializer->GetUnconvertedRootSignatureDesc();
            if (pVerDesc) {
                if (pVerDesc->Version == D3D_ROOT_SIGNATURE_VERSION_1_0) {
                    const auto& desc = pVerDesc->Desc_1_0;
                    outSig.flags = desc.Flags;
                    outSig.parameters.resize(desc.NumParameters);
                    for (UINT i = 0; i < desc.NumParameters; ++i) {
                        const auto& p = desc.pParameters[i];
                        outSig.parameters[i].type = p.ParameterType;
                        outSig.parameters[i].visibility = p.ShaderVisibility;
                        if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) {
                            outSig.parameters[i].shaderRegister = p.Constants.ShaderRegister;
                            outSig.parameters[i].registerSpace = p.Constants.RegisterSpace;
                            outSig.parameters[i].num32BitValues = p.Constants.Num32BitValues;
                        } else if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
                            UINT runningOffset = 0;
                            for (UINT r = 0; r < p.DescriptorTable.NumDescriptorRanges; ++r) {
                                const auto& range = p.DescriptorTable.pDescriptorRanges[r];
                                UINT offset = (range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND) ?
                                    runningOffset : range.OffsetInDescriptorsFromTableStart;
                                TranslatedDescriptorRange trRange;
                                trRange.rangeType = range.RangeType;
                                trRange.numDescriptors = range.NumDescriptors;
                                trRange.baseShaderRegister = range.BaseShaderRegister;
                                trRange.registerSpace = range.RegisterSpace;
                                trRange.offsetInDescriptorsFromTableStart = offset;
                                outSig.parameters[i].ranges.push_back(trRange);
                                runningOffset = offset + range.NumDescriptors;
                            }
                        } else if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_CBV ||
                                   p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_SRV ||
                                   p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_UAV) {
                            outSig.parameters[i].shaderRegister = p.Descriptor.ShaderRegister;
                            outSig.parameters[i].registerSpace = p.Descriptor.RegisterSpace;
                        }
                    }
                } else if (pVerDesc->Version == D3D_ROOT_SIGNATURE_VERSION_1_1) {
                    const auto& desc = pVerDesc->Desc_1_1;
                    outSig.flags = desc.Flags;
                    outSig.parameters.resize(desc.NumParameters);
                    for (UINT i = 0; i < desc.NumParameters; ++i) {
                        const auto& p = desc.pParameters[i];
                        outSig.parameters[i].type = p.ParameterType;
                        outSig.parameters[i].visibility = p.ShaderVisibility;
                        if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) {
                            outSig.parameters[i].shaderRegister = p.Constants.ShaderRegister;
                            outSig.parameters[i].registerSpace = p.Constants.RegisterSpace;
                            outSig.parameters[i].num32BitValues = p.Constants.Num32BitValues;
                        } else if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
                            UINT runningOffset = 0;
                            for (UINT r = 0; r < p.DescriptorTable.NumDescriptorRanges; ++r) {
                                const auto& range = p.DescriptorTable.pDescriptorRanges[r];
                                UINT offset = (range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND) ?
                                    runningOffset : range.OffsetInDescriptorsFromTableStart;
                                TranslatedDescriptorRange trRange;
                                trRange.rangeType = range.RangeType;
                                trRange.numDescriptors = range.NumDescriptors;
                                trRange.baseShaderRegister = range.BaseShaderRegister;
                                trRange.registerSpace = range.RegisterSpace;
                                trRange.offsetInDescriptorsFromTableStart = offset;
                                outSig.parameters[i].ranges.push_back(trRange);
                                runningOffset = offset + range.NumDescriptors;
                            }
                        } else if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_CBV ||
                                   p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_SRV ||
                                   p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_UAV) {
                            outSig.parameters[i].shaderRegister = p.Descriptor.ShaderRegister;
                            outSig.parameters[i].registerSpace = p.Descriptor.RegisterSpace;
                        }
                    }
                }
            }
            pDeserializer->Release();
            return true;
        }
    }

    if (loader.RealD3D12CreateRootSignatureDeserializer) {
        ID3D12RootSignatureDeserializer* pDeserializer = nullptr;
        HRESULT hr = loader.RealD3D12CreateRootSignatureDeserializer(
            pBlob, blobLength, IID_PPV_ARGS(&pDeserializer));
        if (SUCCEEDED(hr) && pDeserializer) {
            const D3D12_ROOT_SIGNATURE_DESC* pDesc = pDeserializer->GetRootSignatureDesc();
            if (pDesc) {
                outSig.flags = pDesc->Flags;
                outSig.parameters.resize(pDesc->NumParameters);
                for (UINT i = 0; i < pDesc->NumParameters; ++i) {
                    const auto& p = pDesc->pParameters[i];
                    outSig.parameters[i].type = p.ParameterType;
                    outSig.parameters[i].visibility = p.ShaderVisibility;
                    if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) {
                        outSig.parameters[i].shaderRegister = p.Constants.ShaderRegister;
                        outSig.parameters[i].registerSpace = p.Constants.RegisterSpace;
                        outSig.parameters[i].num32BitValues = p.Constants.Num32BitValues;
                    } else if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
                        UINT runningOffset = 0;
                        for (UINT r = 0; r < p.DescriptorTable.NumDescriptorRanges; ++r) {
                            const auto& range = p.DescriptorTable.pDescriptorRanges[r];
                            UINT offset = (range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND) ?
                                runningOffset : range.OffsetInDescriptorsFromTableStart;
                            TranslatedDescriptorRange trRange;
                            trRange.rangeType = range.RangeType;
                            trRange.numDescriptors = range.NumDescriptors;
                            trRange.baseShaderRegister = range.BaseShaderRegister;
                            trRange.registerSpace = range.RegisterSpace;
                            trRange.offsetInDescriptorsFromTableStart = offset;
                            outSig.parameters[i].ranges.push_back(trRange);
                            runningOffset = offset + range.NumDescriptors;
                        }
                    } else if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_CBV ||
                               p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_SRV ||
                               p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_UAV) {
                        outSig.parameters[i].shaderRegister = p.Descriptor.ShaderRegister;
                        outSig.parameters[i].registerSpace = p.Descriptor.RegisterSpace;
                    }
                }
            }
            pDeserializer->Release();
            return true;
        }
    }

    return false;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateRootSignature(UINT nodeMask, const void* pBlobWithRootSignature, SIZE_T blobLengthInBytes, REFIID riid, void** ppvRootSignature) {
    HRESULT hr = m_pReal ? m_pReal->CreateRootSignature(nodeMask, pBlobWithRootSignature, blobLengthInBytes, riid, ppvRootSignature) : E_FAIL;
    if (SUCCEEDED(hr) && ppvRootSignature && *ppvRootSignature) {
        TranslatedRootSignature sig = {};
        if (ParseRootSignatureBlob(pBlobWithRootSignature, blobLengthInBytes, sig)) {
            ResourceManager::Instance().RegisterRootSignature(
                reinterpret_cast<ID3D12RootSignature*>(*ppvRootSignature), sig);
        }
    }
    return hr;
}

void STDMETHODCALLTYPE WrappedD3D12Device::CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
    if (m_pReal) m_pReal->CreateConstantBufferView(pDesc, DestDescriptor);
    if (Config::Instance().IsD3D11TranslationEnabled() && D3D11Backend::Instance().IsInitialized() && pDesc) {
        TranslatedResourceInfo bufInfo;
        UINT64 byteOffset = 0;
        if (ResourceManager::Instance().FindBufferByGPUVA(pDesc->BufferLocation, bufInfo, byteOffset)) {
            if (bufInfo.pD3D11Buffer) {
                ResourceManager::Instance().RegisterCBV(DestDescriptor.ptr, bufInfo.pD3D11Buffer);
            }
        }
    }
}

void STDMETHODCALLTYPE WrappedD3D12Device::CreateShaderResourceView(ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
    if (m_pReal) m_pReal->CreateShaderResourceView(pResource, pDesc, DestDescriptor);
    if (Config::Instance().IsD3D11TranslationEnabled() && D3D11Backend::Instance().IsInitialized() && pResource) {
        TranslatedResourceInfo info;
        if (ResourceManager::Instance().GetResourceInfo(pResource, info) && info.pD3D11Resource) {
            ID3D11ShaderResourceView* pSRV = nullptr;
            D3D11Backend::Instance().GetDevice()->CreateShaderResourceView(info.pD3D11Resource, nullptr, &pSRV);
            if (pSRV) {
                ResourceManager::Instance().RegisterSRV(DestDescriptor.ptr, pSRV);
                pSRV->Release();
            }
        }
    }
}

static UINT GetFormatByteSize(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_UINT:
        case DXGI_FORMAT_R32G32B32A32_SINT:
            return 16;
        case DXGI_FORMAT_R32G32B32_TYPELESS:
        case DXGI_FORMAT_R32G32B32_FLOAT:
        case DXGI_FORMAT_R32G32B32_UINT:
        case DXGI_FORMAT_R32G32B32_SINT:
            return 12;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
        case DXGI_FORMAT_R16G16B16A16_UINT:
        case DXGI_FORMAT_R16G16B16A16_SNORM:
        case DXGI_FORMAT_R16G16B16A16_SINT:
        case DXGI_FORMAT_R32G32_TYPELESS:
        case DXGI_FORMAT_R32G32_FLOAT:
        case DXGI_FORMAT_R32G32_UINT:
        case DXGI_FORMAT_R32G32_SINT:
            return 8;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_UINT:
        case DXGI_FORMAT_R8G8B8A8_SNORM:
        case DXGI_FORMAT_R8G8B8A8_SINT:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_R16G16_TYPELESS:
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R16G16_UNORM:
        case DXGI_FORMAT_R16G16_UINT:
        case DXGI_FORMAT_R16G16_SNORM:
        case DXGI_FORMAT_R16G16_SINT:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_UINT:
        case DXGI_FORMAT_R32_SINT:
            return 4;
        case DXGI_FORMAT_R8G8_TYPELESS:
        case DXGI_FORMAT_R8G8_UNORM:
        case DXGI_FORMAT_R8G8_UINT:
        case DXGI_FORMAT_R8G8_SNORM:
        case DXGI_FORMAT_R8G8_SINT:
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_R16_FLOAT:
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_R16_UNORM:
        case DXGI_FORMAT_R16_UINT:
        case DXGI_FORMAT_R16_SNORM:
        case DXGI_FORMAT_R16_SINT:
            return 2;
        case DXGI_FORMAT_R8_TYPELESS:
        case DXGI_FORMAT_R8_UNORM:
        case DXGI_FORMAT_R8_UINT:
        case DXGI_FORMAT_R8_SNORM:
        case DXGI_FORMAT_R8_SINT:
        case DXGI_FORMAT_A8_UNORM:
            return 1;
        default:
            return 0;
    }
}

void STDMETHODCALLTYPE WrappedD3D12Device::CreateUnorderedAccessView(ID3D12Resource* pResource, ID3D12Resource* pCounterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
    if (!Config::Instance().IsD3D11TranslationEnabled() || !D3D11Backend::Instance().IsInitialized()) {
        if (m_pReal) m_pReal->CreateUnorderedAccessView(pResource, pCounterResource, pDesc, DestDescriptor);
        return;
    }

    if (!DestDescriptor.ptr) {
        LOG_WARN("[UAV] CreateUnorderedAccessView called with NULL DestDescriptor.");
        return;
    }

    // D3D12 allows creating a null view descriptor when pResource is nullptr
    if (!pResource) {
        LOG_INFO("[UAV] CreateUnorderedAccessView creating NULL UAV descriptor.");
        TranslatedUAVInfo nullInfo = {};
        ResourceManager::Instance().RegisterUAVInfo(DestDescriptor.ptr, nullInfo);
        if (m_pReal) m_pReal->CreateUnorderedAccessView(nullptr, pCounterResource, pDesc, DestDescriptor);
        return;
    }

    TranslatedResourceInfo resInfo;
    if (!ResourceManager::Instance().GetResourceInfo(pResource, resInfo) || !resInfo.pD3D11Resource) {
        LOG_WARN("[UAV] CreateUnorderedAccessView: Resource not tracked or missing D3D11 resource.");
        return;
    }

    if (!(resInfo.d3d12Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) {
        LOG_WARN("[UAV] CreateUnorderedAccessView rejected: Resource lacks D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS.");
        return;
    }

    ID3D11UnorderedAccessView* pUAV = nullptr;
    TranslatedUAVInfo uavInfo = {};
    uavInfo.pD3D12Resource = pResource;
    uavInfo.pD3D11Resource = resInfo.pD3D11Resource;
    uavInfo.pCounterResource = pCounterResource;

    if (resInfo.pD3D11Buffer) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;

        if (pDesc) {
            if (pDesc->ViewDimension != D3D12_UAV_DIMENSION_BUFFER) {
                LOG_WARN("[UAV] CreateUnorderedAccessView rejected: ViewDimension mismatch for buffer resource.");
                return;
            }

            uavDesc.Format = pDesc->Format;
            uavDesc.Buffer.FirstElement = static_cast<UINT>(pDesc->Buffer.FirstElement);
            uavDesc.Buffer.NumElements = pDesc->Buffer.NumElements;
            uavDesc.Buffer.Flags = 0;

            UINT64 bufferWidth = resInfo.d3d12Desc.Width;

            // 1. Structured Buffer validation
            if (pDesc->Buffer.StructureByteStride > 0) {
                if (pDesc->Buffer.StructureByteStride % 4 != 0) {
                    LOG_WARN("[UAV] CreateUnorderedAccessView rejected: StructureByteStride (" +
                             std::to_string(pDesc->Buffer.StructureByteStride) + ") is not a multiple of 4.");
                    DiagnosticReport rep;
                    rep.errorCode = KeplerErrorCode::InvalidDescriptor;
                    rep.severity = ErrorSeverity::Warning;
                    rep.subsystem = "WrappedD3D12Device";
                    rep.apiOperation = "CreateUnorderedAccessView";
                    rep.nativeHResult = E_INVALIDARG;
                    rep.disposition = ExecutionDisposition::Rejected;
                    rep.descriptorInfo = "Handle: 0x" + Logger::HexToString(DestDescriptor.ptr);
                    rep.resourceInfo = "Stride: " + std::to_string(pDesc->Buffer.StructureByteStride) + "B";
                    rep.explanation = "Structured buffer stride is not a multiple of 4 bytes.";
                    rep.actionableHint = "Direct3D 11 hardware requires structured buffer element strides to be a multiple of 4.";
                    LOG_DIAGNOSTIC_REPORT(rep);
                    return;
                }
                if (pDesc->Buffer.StructureByteStride > 2048) {
                    LOG_WARN("[UAV] CreateUnorderedAccessView rejected: StructureByteStride (" +
                             std::to_string(pDesc->Buffer.StructureByteStride) + ") exceeds FL 11_0 maximum of 2048 bytes.");
                    DiagnosticReport rep;
                    rep.errorCode = KeplerErrorCode::InvalidDescriptor;
                    rep.severity = ErrorSeverity::Warning;
                    rep.subsystem = "WrappedD3D12Device";
                    rep.apiOperation = "CreateUnorderedAccessView";
                    rep.nativeHResult = E_INVALIDARG;
                    rep.disposition = ExecutionDisposition::Rejected;
                    rep.descriptorInfo = "Handle: 0x" + Logger::HexToString(DestDescriptor.ptr);
                    rep.resourceInfo = "Stride: " + std::to_string(pDesc->Buffer.StructureByteStride) + "B";
                    rep.explanation = "Structured buffer stride exceeds Feature Level 11_0 limit of 2048 bytes.";
                    rep.actionableHint = "Keep structured buffer element sizes <= 2048 bytes for Kepler hardware compatibility.";
                    LOG_DIAGNOSTIC_REPORT(rep);
                    return;
                }
                uavDesc.Format = DXGI_FORMAT_UNKNOWN;

                UINT64 totalBytes = (static_cast<UINT64>(pDesc->Buffer.FirstElement) + pDesc->Buffer.NumElements) * pDesc->Buffer.StructureByteStride;
                if (totalBytes > bufferWidth) {
                    LOG_WARN("[UAV] CreateUnorderedAccessView rejected: Structured buffer view (" +
                             std::to_string(totalBytes) + " bytes) exceeds buffer width (" + std::to_string(bufferWidth) + ").");
                    DiagnosticReport rep;
                    rep.errorCode = KeplerErrorCode::BufferDimensionExceeded;
                    rep.severity = ErrorSeverity::Warning;
                    rep.subsystem = "WrappedD3D12Device";
                    rep.apiOperation = "CreateUnorderedAccessView";
                    rep.nativeHResult = E_INVALIDARG;
                    rep.disposition = ExecutionDisposition::Rejected;
                    rep.descriptorInfo = "Handle: 0x" + Logger::HexToString(DestDescriptor.ptr);
                    rep.resourceInfo = "ViewBytes: " + std::to_string(totalBytes) + "B > Width: " + std::to_string(bufferWidth) + "B";
                    rep.explanation = "Structured buffer view bounds exceed the buffer capacity.";
                    rep.actionableHint = "Ensure FirstElement + NumElements does not overflow buffer Width.";
                    LOG_DIAGNOSTIC_REPORT(rep);
                    return;
                }

                // If underlying D3D11 buffer was created without D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
                // recreate it with STRUCTURED flag so D3D11 driver allows the structured UAV.
                D3D11_BUFFER_DESC currentBufDesc = {};
                resInfo.pD3D11Buffer->GetDesc(&currentBufDesc);
                if (!(currentBufDesc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) ||
                    currentBufDesc.StructureByteStride != pDesc->Buffer.StructureByteStride)
                {
                    D3D11_BUFFER_DESC newDesc = currentBufDesc;
                    newDesc.MiscFlags &= ~D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
                    newDesc.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
                    newDesc.StructureByteStride = pDesc->Buffer.StructureByteStride;

                    ID3D11Buffer* pNewBuf = nullptr;
                    if (SUCCEEDED(D3D11Backend::Instance().GetDevice()->CreateBuffer(&newDesc, nullptr, &pNewBuf))) {
                        D3D11Backend::Instance().GetContext()->CopyResource(pNewBuf, resInfo.pD3D11Buffer);
                        ResourceManager::Instance().UpdateD3D11ResourceForBuffer(pResource, pNewBuf);
                        resInfo.pD3D11Buffer = pNewBuf;
                        resInfo.pD3D11Resource = pNewBuf;
                        uavInfo.pD3D11Resource = pNewBuf;
                        pNewBuf->Release();
                    }
                }

                uavInfo.structureByteStride = pDesc->Buffer.StructureByteStride;
            }
            // 2. Raw (ByteAddress) Buffer validation
            else if (pDesc->Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW) {
                uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
                uavDesc.Buffer.Flags |= D3D11_BUFFER_UAV_FLAG_RAW;
                UINT64 totalBytes = (static_cast<UINT64>(pDesc->Buffer.FirstElement) + pDesc->Buffer.NumElements) * 4;
                if (totalBytes > bufferWidth) {
                    LOG_WARN("[UAV] CreateUnorderedAccessView rejected: Raw buffer view exceeds buffer width.");
                    return;
                }
            }
            // 3. Typed Buffer validation
            else {
                if (pDesc->Format == DXGI_FORMAT_UNKNOWN) {
                    LOG_WARN("[UAV] CreateUnorderedAccessView rejected: Typed buffer UAV cannot have DXGI_FORMAT_UNKNOWN with zero stride.");
                    return;
                }
                UINT formatSize = GetFormatByteSize(pDesc->Format);
                if (formatSize == 0) formatSize = 4;
                UINT64 totalBytes = (static_cast<UINT64>(pDesc->Buffer.FirstElement) + pDesc->Buffer.NumElements) * formatSize;
                if (totalBytes > bufferWidth) {
                    LOG_WARN("[UAV] CreateUnorderedAccessView rejected: Typed buffer view exceeds buffer width.");
                    return;
                }
            }

            // Counter-resource handling
            if (pCounterResource) {
                TranslatedResourceInfo counterInfo;
                if (ResourceManager::Instance().GetResourceInfo(pCounterResource, counterInfo)) {
                    if (counterInfo.d3d12Desc.Width >= 4) {
                        uavInfo.hasCounter = true;
                        uavInfo.initialCount = 0; // Default reset count to 0
                        uavDesc.Buffer.Flags |= D3D11_BUFFER_UAV_FLAG_APPEND;
                    } else {
                        LOG_WARN("[UAV] CreateUnorderedAccessView: Counter buffer is smaller than 4 bytes.");
                    }
                }
            }
            uavInfo.numElements = pDesc->Buffer.NumElements;
            uavInfo.format = uavDesc.Format;
        } else {
            // Default raw/typed R32 view
            uavDesc.Format = DXGI_FORMAT_R32_UINT;
            uavDesc.Buffer.FirstElement = 0;
            uavDesc.Buffer.NumElements = static_cast<UINT>(resInfo.d3d12Desc.Width / 4);
            uavDesc.Buffer.Flags = 0;
            uavInfo.numElements = uavDesc.Buffer.NumElements;
            uavInfo.format = DXGI_FORMAT_R32_UINT;
        }

        HRESULT hr = D3D11Backend::Instance().GetDevice()->CreateUnorderedAccessView(
            resInfo.pD3D11Resource, &uavDesc, &pUAV);
        if (FAILED(hr)) {
            LOG_WARN("[UAV] ID3D11Device::CreateUnorderedAccessView failed with hr = 0x" + Logger::HResultToString(hr));
            return;
        }
    } else {
        // Texture UAV
        D3D11Backend::Instance().GetDevice()->CreateUnorderedAccessView(
            resInfo.pD3D11Resource, nullptr, &pUAV);
    }

    if (pUAV) {
        uavInfo.pUAV = pUAV;
        ResourceManager::Instance().RegisterUAVInfo(DestDescriptor.ptr, uavInfo);
        pUAV->Release();
    }

    if (m_pReal) {
        m_pReal->CreateUnorderedAccessView(pResource, pCounterResource, pDesc, DestDescriptor);
    }
}

void STDMETHODCALLTYPE WrappedD3D12Device::CreateRenderTargetView(ID3D12Resource* pResource, const D3D12_RENDER_TARGET_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
    if (m_pReal) m_pReal->CreateRenderTargetView(pResource, pDesc, DestDescriptor);
    if (Config::Instance().IsD3D11TranslationEnabled() && D3D11Backend::Instance().IsInitialized() && pResource) {
        TranslatedResourceInfo info;
        if (ResourceManager::Instance().GetResourceInfo(pResource, info) && info.pD3D11Resource) {
            ID3D11RenderTargetView* pRTV = nullptr;
            D3D11Backend::Instance().GetDevice()->CreateRenderTargetView(info.pD3D11Resource, nullptr, &pRTV);
            if (pRTV) {
                ResourceManager::Instance().RegisterRTV(DestDescriptor.ptr, pRTV);
                pRTV->Release();
            }
        }
    }
}

void STDMETHODCALLTYPE WrappedD3D12Device::CreateDepthStencilView(ID3D12Resource* pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
    if (m_pReal) m_pReal->CreateDepthStencilView(pResource, pDesc, DestDescriptor);
    if (Config::Instance().IsD3D11TranslationEnabled() && D3D11Backend::Instance().IsInitialized() && pResource) {
        TranslatedResourceInfo info;
        if (ResourceManager::Instance().GetResourceInfo(pResource, info) && info.pD3D11Resource) {
            ID3D11DepthStencilView* pDSV = nullptr;
            D3D11Backend::Instance().GetDevice()->CreateDepthStencilView(info.pD3D11Resource, nullptr, &pDSV);
            if (pDSV) {
                ResourceManager::Instance().RegisterDSV(DestDescriptor.ptr, pDSV);
                pDSV->Release();
            }
        }
    }
}

void STDMETHODCALLTYPE WrappedD3D12Device::CreateSampler(const D3D12_SAMPLER_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) {
    if (m_pReal) m_pReal->CreateSampler(pDesc, DestDescriptor);
    if (Config::Instance().IsD3D11TranslationEnabled() && D3D11Backend::Instance().IsInitialized() && pDesc) {
        D3D11_SAMPLER_DESC desc11 = {};
        desc11.Filter = static_cast<D3D11_FILTER>(pDesc->Filter);
        desc11.AddressU = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(pDesc->AddressU);
        desc11.AddressV = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(pDesc->AddressV);
        desc11.AddressW = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(pDesc->AddressW);
        desc11.MipLODBias = pDesc->MipLODBias;
        desc11.MaxAnisotropy = pDesc->MaxAnisotropy;
        desc11.ComparisonFunc = static_cast<D3D11_COMPARISON_FUNC>(pDesc->ComparisonFunc);
        desc11.BorderColor[0] = pDesc->BorderColor[0];
        desc11.BorderColor[1] = pDesc->BorderColor[1];
        desc11.BorderColor[2] = pDesc->BorderColor[2];
        desc11.BorderColor[3] = pDesc->BorderColor[3];
        desc11.MinLOD = pDesc->MinLOD;
        desc11.MaxLOD = pDesc->MaxLOD;

        ID3D11SamplerState* pSampler = nullptr;
        if (SUCCEEDED(D3D11Backend::Instance().GetDevice()->CreateSamplerState(&desc11, &pSampler))) {
            ResourceManager::Instance().RegisterSampler(DestDescriptor.ptr, pSampler);
            pSampler->Release();
        }
    }
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
    if (!m_pReal) return E_FAIL;
    auto val = TextureClamper::Instance().ValidateAndClamp(pDesc, pHeapProperties, HeapFlags);
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("[RESOURCE] CreateCommittedResource: " + val.diagnostics);
    }
    const D3D12_RESOURCE_DESC* effectiveDesc = val.isModified ? &val.modifiedDesc : pDesc;
    HRESULT hr = m_pReal->CreateCommittedResource(pHeapProperties, HeapFlags, effectiveDesc, InitialResourceState, pOptimizedClearValue, riidResource, ppvResource);
    if (SUCCEEDED(hr) && ppvResource && *ppvResource) {
        auto* pRes = reinterpret_cast<ID3D12Resource*>(*ppvResource);
        ResourceStateTracker::Instance().RegisterResource(pRes, InitialResourceState);

        if (Config::Instance().IsD3D11TranslationEnabled()) {
            if (!D3D11Backend::Instance().IsInitialized()) {
                IDXGIAdapter* pAdapter = D3D11Backend::GetMatchingAdapter(m_pReal);
                D3D11Backend::Instance().Initialize(pAdapter);
                if (pAdapter) pAdapter->Release();
            }
            if (D3D11Backend::Instance().IsInitialized()) {
                ID3D11Resource* pD3D11Res = nullptr;
                if (effectiveDesc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
                    ID3D11Buffer* pBuf = nullptr;
                    if (SUCCEEDED(D3D11Backend::Instance().CreateTranslatedBuffer(effectiveDesc, pHeapProperties, &pBuf))) {
                        pD3D11Res = pBuf;
                    }
                    D3D12_GPU_VIRTUAL_ADDRESS gpuVA = pRes->GetGPUVirtualAddress();
                    if (gpuVA != 0) {
                        ResourceManager::Instance().RegisterBufferGPUVA(pRes, gpuVA);
                    }
                } else if (effectiveDesc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
                    ID3D11Texture2D* pTex = nullptr;
                    if (SUCCEEDED(D3D11Backend::Instance().CreateTranslatedTexture2D(effectiveDesc, pHeapProperties, &pTex))) {
                        pD3D11Res = pTex;
                    }
                }
                D3D12_GPU_VIRTUAL_ADDRESS va = (effectiveDesc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) ? pRes->GetGPUVirtualAddress() : 0;
                ResourceManager::Instance().RegisterResource(pRes, *effectiveDesc, pHeapProperties, pD3D11Res, va);
                if (pD3D11Res) pD3D11Res->Release();
            }
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateHeap(const D3D12_HEAP_DESC* pDesc, REFIID riid, void** ppvHeap) {
    return m_pReal ? m_pReal->CreateHeap(pDesc, riid, ppvHeap) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreatePlacedResource(ID3D12Heap* pHeap, UINT64 HeapOffset, const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) {
    if (!m_pReal) return E_FAIL;
    auto val = TextureClamper::Instance().ValidateAndClamp(pDesc, nullptr, D3D12_HEAP_FLAG_NONE);
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("[RESOURCE] CreatePlacedResource: " + val.diagnostics);
    }
    const D3D12_RESOURCE_DESC* effectiveDesc = val.isModified ? &val.modifiedDesc : pDesc;
    return m_pReal->CreatePlacedResource(pHeap, HeapOffset, effectiveDesc, InitialState, pOptimizedClearValue, riid, ppvResource);
}

HRESULT STDMETHODCALLTYPE WrappedD3D12Device::CreateReservedResource(const D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState, const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource) {
    if (!m_pReal) return E_FAIL;
    auto val = TextureClamper::Instance().ValidateAndClamp(pDesc, nullptr, D3D12_HEAP_FLAG_NONE);
    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_HOOK("[RESOURCE] CreateReservedResource: " + val.diagnostics);
    }
    const D3D12_RESOURCE_DESC* effectiveDesc = val.isModified ? &val.modifiedDesc : pDesc;
    return m_pReal->CreateReservedResource(effectiveDesc, InitialState, pOptimizedClearValue, riid, ppvResource);
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
    if (!ppFence) return E_POINTER;
    *ppFence = nullptr;
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
