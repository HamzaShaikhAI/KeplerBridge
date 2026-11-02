#include "translator/d3d11_backend.h"
#include "translator/command_translator.h"
#include "translator/resource_manager.h"
#include "core/logger.h"
#include "core/config.h"
#include "core/profile_manager.h"
#include "downgrader/dxil_inspector.h"
#include "downgrader/shader_policy.h"
#include "downgrader/bounded_shader_translator.h"
#include <iostream>
#include <chrono>

namespace kepler {

static uint64_t ComputeBytecodeHash(const void* data, size_t size) {
    if (!data || size == 0) return 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

D3D11Backend& D3D11Backend::Instance() {
    static D3D11Backend s_instance;
    return s_instance;
}

D3D11Backend::~D3D11Backend() {
    Shutdown(true);
}

bool D3D11Backend::Initialize(IDXGIAdapter* pAdapter) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        return true;
    }

    D3D_DRIVER_TYPE driverType = pAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
    UINT createFlags = 0;

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    UINT numFeatureLevels = sizeof(featureLevels) / sizeof(featureLevels[0]);

    HRESULT hr = D3D11CreateDevice(
        pAdapter,
        driverType,
        nullptr,
        createFlags,
        featureLevels,
        numFeatureLevels,
        D3D11_SDK_VERSION,
        &m_pDevice,
        &m_featureLevel,
        &m_pContext);

    if (FAILED(hr) || !m_pDevice || !m_pContext) {
        LOG_ERROR("[D3D11Backend] Failed to initialize D3D11 device and context: " + Logger::HResultToString(hr));
        DiagnosticReport rep;
        rep.errorCode = KeplerErrorCode::InitializationFailed;
        rep.severity = ErrorSeverity::Fatal;
        rep.subsystem = "D3D11Backend";
        rep.apiOperation = "D3D11CreateDevice";
        rep.nativeHResult = hr;
        rep.disposition = ExecutionDisposition::Rejected;
        rep.explanation = "Failed to create Direct3D 11 device and context for translation.";
        rep.actionableHint = "Ensure DirectX 11 runtime is available and graphics driver supports Feature Level 11_0.";
        LOG_DIAGNOSTIC_REPORT(rep);
        return false;
    }

    D3D11_QUERY_DESC qDesc = {};
    qDesc.Query = D3D11_QUERY_EVENT;
    qDesc.MiscFlags = 0;
    hr = m_pDevice->CreateQuery(&qDesc, &m_pSyncQuery);
    if (FAILED(hr)) {
        LOG_WARN("[D3D11Backend] Failed to create sync query: " + Logger::HResultToString(hr));
        m_pSyncQuery = nullptr;
    }

    m_initialized = true;
    RuntimeTelemetry::Instance().RecordD3D11DeviceCreated();
    LOG_INFO("[D3D11Backend] Initialized Direct3D 11 backend successfully. Feature Level: " +
             Logger::FeatureLevelToString(static_cast<D3D_FEATURE_LEVEL>(m_featureLevel)));

    // Ensure production shader lowering translator is registered with ShaderPolicy
    if (!ShaderPolicy::Instance().HasActiveTranslator()) {
        ShaderPolicy::Instance().RegisterTranslator(std::make_shared<BoundedShaderTranslator>());
        LOG_INFO("[D3D11Backend] Registered BoundedShaderTranslator with ShaderPolicy for production shader lowering.");
    }

    return true;
}

void D3D11Backend::Shutdown(bool isProcessDetach) {
    LOG_INFO("[D3D11Backend] Shutdown called (isProcessDetach=" + std::to_string(isProcessDetach) + ")");
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!isProcessDetach) {
        LOG_INFO("[D3D11Backend] Releasing constant buffers...");
        for (auto& pair : m_graphicsConstantBuffers) {
            if (pair.second) pair.second->Release();
        }
        for (auto& pair : m_computeConstantBuffers) {
            if (pair.second) pair.second->Release();
        }
        if (m_pSyncQuery) {
            LOG_INFO("[D3D11Backend] Releasing m_pSyncQuery...");
            m_pSyncQuery->Release();
            m_pSyncQuery = nullptr;
        }
        if (m_pContext) {
            LOG_INFO("[D3D11Backend] Clearing and releasing m_pContext...");
            m_pContext->ClearState();
            m_pContext->Release();
            m_pContext = nullptr;
        }
        if (m_pDevice) {
            LOG_INFO("[D3D11Backend] Releasing m_pDevice...");
            m_pDevice->Release();
            m_pDevice = nullptr;
        }
    }
    LOG_INFO("[D3D11Backend] Clearing internal maps...");
    m_graphicsConstantBuffers.clear();
    m_computeConstantBuffers.clear();
    m_graphicsConstantShadow.clear();
    m_computeConstantShadow.clear();
    m_rootConstantMapCount = 0;
    m_rootConstantMapSkippedCount = 0;
    m_activeGraphicsRootSig = nullptr;
    m_activeComputeRootSig = nullptr;
    for (UINT i = 0; i < 8; ++i) {
        m_activeComputeUAVs[i] = {};
        m_activeGraphicsUAVs[i] = {};
        m_boundRTVs[i] = nullptr;
    }
    for (UINT i = 0; i < 16; ++i) {
        m_activeComputeSRVs[i] = {};
        m_activePixelSRVs[i] = {};
        m_activeVertexSRVs[i] = {};
    }
    m_numBoundRTVs = 0;
    m_boundDSV = nullptr;
    m_pContext = nullptr;
    m_pDevice = nullptr;
    m_pSyncQuery = nullptr;
    ResetPsoBurstStats();
    m_initialized = false;
    LOG_INFO("[D3D11Backend] Shutdown finished.");
}

IDXGIAdapter* D3D11Backend::GetMatchingAdapter(ID3D12Device* pRealDevice) {
    if (!pRealDevice) return nullptr;
    LUID targetLuid = pRealDevice->GetAdapterLuid();
    IDXGIFactory1* pFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&pFactory)))) {
        IDXGIAdapter1* pAdapter1 = nullptr;
        for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter1) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc = {};
            if (SUCCEEDED(pAdapter1->GetDesc1(&desc))) {
                if (desc.AdapterLuid.LowPart == targetLuid.LowPart &&
                    desc.AdapterLuid.HighPart == targetLuid.HighPart) {
                    pFactory->Release();
                    return pAdapter1;
                }
            }
            pAdapter1->Release();
        }
        pFactory->Release();
    }
    return nullptr;
}

HRESULT D3D11Backend::CreateTranslatedBuffer(
    const D3D12_RESOURCE_DESC* pDesc,
    const D3D12_HEAP_PROPERTIES* pHeapProperties,
    ID3D11Buffer** ppOutBuffer)
{
    if (!ppOutBuffer) return E_POINTER;
    *ppOutBuffer = nullptr;
    if (!m_initialized || !m_pDevice || !pDesc) return E_FAIL;

    D3D11_BUFFER_DESC desc11 = {};
    desc11.ByteWidth = static_cast<UINT>(pDesc->Width);

    // Map bind flags
    desc11.BindFlags = 0;
    if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) {
        desc11.BindFlags |= D3D11_BIND_RENDER_TARGET;
    }
    if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) {
        desc11.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
        desc11.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    }
    if (!(pDesc->Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
        desc11.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }

    // Default to vertex buffer and constant buffer binding if zero
    if (desc11.BindFlags == 0) {
        desc11.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER;
    }

    // Map heap properties to usage
    if (pHeapProperties) {
        if (pHeapProperties->Type == D3D12_HEAP_TYPE_UPLOAD) {
            desc11.Usage = D3D11_USAGE_DYNAMIC;
            desc11.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            // In D3D11 dynamic buffers can be bound as Vertex Buffer and Index Buffer
            desc11.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER;
        } else if (pHeapProperties->Type == D3D12_HEAP_TYPE_READBACK) {
            desc11.Usage = D3D11_USAGE_STAGING;
            desc11.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc11.BindFlags = 0;
        } else {
            desc11.Usage = D3D11_USAGE_DEFAULT;
        }
    } else {
        desc11.Usage = D3D11_USAGE_DEFAULT;
    }

    HRESULT hr = m_pDevice->CreateBuffer(&desc11, nullptr, ppOutBuffer);
    if (FAILED(hr)) {
        LOG_WARN("[D3D11Backend] CreateBuffer failed: " + Logger::HResultToString(hr) +
                 " (ByteWidth=" + std::to_string(desc11.ByteWidth) + ", BindFlags=0x" + std::to_string(desc11.BindFlags) + ")");
    }
    return hr;
}

HRESULT D3D11Backend::CreateTranslatedTexture2D(
    const D3D12_RESOURCE_DESC* pDesc,
    const D3D12_HEAP_PROPERTIES* pHeapProperties,
    ID3D11Texture2D** ppOutTexture)
{
    if (!ppOutTexture) return E_POINTER;
    *ppOutTexture = nullptr;
    if (!m_initialized || !m_pDevice || !pDesc) return E_FAIL;

    D3D11_TEXTURE2D_DESC desc11 = {};
    desc11.Width = static_cast<UINT>(pDesc->Width);
    desc11.Height = pDesc->Height;
    desc11.MipLevels = pDesc->MipLevels;
    desc11.ArraySize = pDesc->DepthOrArraySize;
    desc11.Format = pDesc->Format;
    desc11.SampleDesc.Count = pDesc->SampleDesc.Count;
    desc11.SampleDesc.Quality = pDesc->SampleDesc.Quality;

    desc11.BindFlags = 0;
    if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) {
        desc11.BindFlags |= D3D11_BIND_RENDER_TARGET;
    }
    if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
        desc11.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
    }
    if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) {
        desc11.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    }
    if (!(pDesc->Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
        desc11.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }

    if (pHeapProperties && pHeapProperties->Type == D3D12_HEAP_TYPE_READBACK) {
        desc11.Usage = D3D11_USAGE_STAGING;
        desc11.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc11.BindFlags = 0;
    } else {
        desc11.Usage = D3D11_USAGE_DEFAULT;
    }

    return m_pDevice->CreateTexture2D(&desc11, nullptr, ppOutTexture);
}

HRESULT D3D11Backend::CreateTranslatedPSO(
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc,
    TranslatedPipelineState& outPSO)
{
    if (!m_initialized || !m_pDevice || !pDesc) return E_FAIL;

    outPSO = {};

    // 1. Vertex Shader
    const void* vsBytecode = pDesc->VS.pShaderBytecode;
    size_t vsLength = pDesc->VS.BytecodeLength;
    std::vector<uint8_t> vsFallback;

    if (vsBytecode && vsLength > 0) {
        ShaderInspectionResult insp = DxilInspector::InspectShader(vsBytecode, vsLength);
        if (!insp.isSupportedOnKepler) {
            ShaderPolicyDecision dec = ShaderPolicy::Instance().EvaluateShader(vsBytecode, vsLength, ShaderStage::Vertex);
            if (dec.fallbackApplied && dec.effectiveBytecode && dec.effectiveLength > 0) {
                vsFallback = std::move(dec.fallbackStorage);
                vsBytecode = vsFallback.data();
                vsLength = vsFallback.size();
                LOG_INFO("[D3D11Backend] Vertex Shader lowered to SM 5.0 (" + std::to_string(vsLength) + " bytes).");
            } else {
                uint64_t hash = ComputeBytecodeHash(vsBytecode, vsLength);
                LOG_WARN("[D3D11Backend] Vertex Shader SM " + std::to_string(insp.majorVersion) + "." + std::to_string(insp.minorVersion) +
                         " (size: " + std::to_string(vsLength) + " bytes, hash: 0x" + Logger::HexToString(hash) +
                         ") exceeds Kepler D3D11 Feature Level 11_0 limit (SM 5.0) and cannot be lowered within bounds. "
                         "Safely skipping D3D11 shader creation without feeding invalid bytecode to CreateVertexShader.");
                return DXGI_ERROR_UNSUPPORTED;
            }
        }

        HRESULT hr = m_pDevice->CreateVertexShader(
            vsBytecode,
            vsLength,
            nullptr,
            &outPSO.pVS);
        if (FAILED(hr)) {
            uint64_t hash = ComputeBytecodeHash(vsBytecode, vsLength);
            LOG_ERROR("[D3D11Backend] Failed to create translated Vertex Shader (size: " + std::to_string(vsLength) +
                      " bytes, hash: 0x" + Logger::HexToString(hash) + "): " + Logger::HResultToString(hr));
            return hr;
        }
    }

    // 2. Pixel Shader
    const void* psBytecode = pDesc->PS.pShaderBytecode;
    size_t psLength = pDesc->PS.BytecodeLength;
    std::vector<uint8_t> psFallback;

    if (psBytecode && psLength > 0) {
        ShaderInspectionResult insp = DxilInspector::InspectShader(psBytecode, psLength);
        if (!insp.isSupportedOnKepler) {
            ShaderPolicyDecision dec = ShaderPolicy::Instance().EvaluateShader(psBytecode, psLength, ShaderStage::Pixel);
            if (dec.fallbackApplied && dec.effectiveBytecode && dec.effectiveLength > 0) {
                psFallback = std::move(dec.fallbackStorage);
                psBytecode = psFallback.data();
                psLength = psFallback.size();
                LOG_INFO("[D3D11Backend] Pixel Shader lowered to SM 5.0 (" + std::to_string(psLength) + " bytes).");
            } else {
                uint64_t hash = ComputeBytecodeHash(psBytecode, psLength);
                LOG_WARN("[D3D11Backend] Pixel Shader SM " + std::to_string(insp.majorVersion) + "." + std::to_string(insp.minorVersion) +
                         " (size: " + std::to_string(psLength) + " bytes, hash: 0x" + Logger::HexToString(hash) +
                         ") exceeds Kepler D3D11 Feature Level 11_0 limit (SM 5.0) and cannot be lowered within bounds. "
                         "Safely skipping D3D11 shader creation without feeding invalid bytecode to CreatePixelShader.");
                return DXGI_ERROR_UNSUPPORTED;
            }
        }

        HRESULT hr = m_pDevice->CreatePixelShader(
            psBytecode,
            psLength,
            nullptr,
            &outPSO.pPS);
        if (FAILED(hr)) {
            uint64_t hash = ComputeBytecodeHash(psBytecode, psLength);
            LOG_ERROR("[D3D11Backend] Failed to create translated Pixel Shader (size: " + std::to_string(psLength) +
                      " bytes, hash: 0x" + Logger::HexToString(hash) + "): " + Logger::HResultToString(hr));
            return hr;
        }
    }

    // 3. Input Layout
    if (pDesc->InputLayout.NumElements > 0 && pDesc->InputLayout.pInputElementDescs && vsBytecode) {
        std::vector<D3D11_INPUT_ELEMENT_DESC> d11Elements;
        d11Elements.reserve(pDesc->InputLayout.NumElements);

        for (UINT i = 0; i < pDesc->InputLayout.NumElements; ++i) {
            const auto& d12 = pDesc->InputLayout.pInputElementDescs[i];
            D3D11_INPUT_ELEMENT_DESC d11 = {};
            d11.SemanticName = d12.SemanticName;
            d11.SemanticIndex = d12.SemanticIndex;
            d11.Format = d12.Format;
            d11.InputSlot = d12.InputSlot;
            d11.AlignedByteOffset = d12.AlignedByteOffset;
            d11.InputSlotClass = (d12.InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA) ?
                D3D11_INPUT_PER_VERTEX_DATA : D3D11_INPUT_PER_INSTANCE_DATA;
            d11.InstanceDataStepRate = d12.InstanceDataStepRate;
            d11Elements.push_back(d11);
        }

        HRESULT hr = m_pDevice->CreateInputLayout(
            d11Elements.data(),
            static_cast<UINT>(d11Elements.size()),
            vsBytecode,
            vsLength,
            &outPSO.pInputLayout);
        if (FAILED(hr)) {
            LOG_ERROR("[D3D11Backend] Failed to create translated Input Layout: " + Logger::HResultToString(hr));
            return hr;
        }
    }

    // 4. Rasterizer State
    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = (pDesc->RasterizerState.FillMode == D3D12_FILL_MODE_WIREFRAME) ?
        D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
    rsDesc.CullMode = (pDesc->RasterizerState.CullMode == D3D12_CULL_MODE_NONE) ?
        D3D11_CULL_NONE : (pDesc->RasterizerState.CullMode == D3D12_CULL_MODE_FRONT ? D3D11_CULL_FRONT : D3D11_CULL_BACK);
    rsDesc.FrontCounterClockwise = pDesc->RasterizerState.FrontCounterClockwise;
    rsDesc.DepthBias = pDesc->RasterizerState.DepthBias;
    rsDesc.DepthBiasClamp = pDesc->RasterizerState.DepthBiasClamp;
    rsDesc.SlopeScaledDepthBias = pDesc->RasterizerState.SlopeScaledDepthBias;
    rsDesc.DepthClipEnable = pDesc->RasterizerState.DepthClipEnable;
    rsDesc.ScissorEnable = FALSE;
    rsDesc.MultisampleEnable = pDesc->RasterizerState.MultisampleEnable;
    rsDesc.AntialiasedLineEnable = pDesc->RasterizerState.AntialiasedLineEnable;

    HRESULT hrRS = m_pDevice->CreateRasterizerState(&rsDesc, &outPSO.pRasterizerState);
    if (FAILED(hrRS)) {
        LOG_WARN("[D3D11Backend] CreateRasterizerState failed: " + Logger::HResultToString(hrRS));
    }

    // 5. Blend State
    D3D11_BLEND_DESC bsDesc = {};
    bsDesc.AlphaToCoverageEnable = pDesc->BlendState.AlphaToCoverageEnable;
    bsDesc.IndependentBlendEnable = pDesc->BlendState.IndependentBlendEnable;
    for (UINT i = 0; i < 8; ++i) {
        const auto& rt12 = pDesc->BlendState.RenderTarget[i];
        auto& rt11 = bsDesc.RenderTarget[i];
        rt11.BlendEnable = rt12.BlendEnable;
        rt11.SrcBlend = rt12.SrcBlend ? static_cast<D3D11_BLEND>(rt12.SrcBlend) : D3D11_BLEND_ONE;
        rt11.DestBlend = rt12.DestBlend ? static_cast<D3D11_BLEND>(rt12.DestBlend) : D3D11_BLEND_ZERO;
        rt11.BlendOp = rt12.BlendOp ? static_cast<D3D11_BLEND_OP>(rt12.BlendOp) : D3D11_BLEND_OP_ADD;
        rt11.SrcBlendAlpha = rt12.SrcBlendAlpha ? static_cast<D3D11_BLEND>(rt12.SrcBlendAlpha) : D3D11_BLEND_ONE;
        rt11.DestBlendAlpha = rt12.DestBlendAlpha ? static_cast<D3D11_BLEND>(rt12.DestBlendAlpha) : D3D11_BLEND_ZERO;
        rt11.BlendOpAlpha = rt12.BlendOpAlpha ? static_cast<D3D11_BLEND_OP>(rt12.BlendOpAlpha) : D3D11_BLEND_OP_ADD;
        rt11.RenderTargetWriteMask = rt12.RenderTargetWriteMask ? rt12.RenderTargetWriteMask : D3D11_COLOR_WRITE_ENABLE_ALL;
    }
    HRESULT hrBS = m_pDevice->CreateBlendState(&bsDesc, &outPSO.pBlendState);
    if (FAILED(hrBS)) {
        LOG_WARN("[D3D11Backend] CreateBlendState failed: " + Logger::HResultToString(hrBS));
    }

    // 6. Depth Stencil State
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = pDesc->DepthStencilState.DepthEnable;
    dsDesc.DepthWriteMask = pDesc->DepthStencilState.DepthWriteMask ?
        static_cast<D3D11_DEPTH_WRITE_MASK>(pDesc->DepthStencilState.DepthWriteMask) : D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = pDesc->DepthStencilState.DepthFunc ?
        static_cast<D3D11_COMPARISON_FUNC>(pDesc->DepthStencilState.DepthFunc) : D3D11_COMPARISON_ALWAYS;
    dsDesc.StencilEnable = pDesc->DepthStencilState.StencilEnable;
    dsDesc.StencilReadMask = pDesc->DepthStencilState.StencilReadMask;
    dsDesc.StencilWriteMask = pDesc->DepthStencilState.StencilWriteMask;
    dsDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    dsDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    dsDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    dsDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    dsDesc.BackFace = dsDesc.FrontFace;

    HRESULT hrDS = m_pDevice->CreateDepthStencilState(&dsDesc, &outPSO.pDepthStencilState);
    if (FAILED(hrDS)) {
        LOG_WARN("[D3D11Backend] CreateDepthStencilState failed: " + Logger::HResultToString(hrDS));
    }

    outPSO.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    NotifyPsoCreated();
    return S_OK;
}

HRESULT D3D11Backend::CreateTranslatedComputePSO(
    const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc,
    TranslatedPipelineState& outPSO)
{
    if (!m_initialized || !m_pDevice || !pDesc) return E_FAIL;
    outPSO = {};

    const void* csBytecode = pDesc->CS.pShaderBytecode;
    size_t csLength = pDesc->CS.BytecodeLength;
    std::vector<uint8_t> csFallback;

    if (csBytecode && csLength > 0) {
        ShaderInspectionResult insp = DxilInspector::InspectShader(csBytecode, csLength);
        if (!insp.isSupportedOnKepler) {
            ShaderPolicyDecision dec = ShaderPolicy::Instance().EvaluateShader(csBytecode, csLength, ShaderStage::Compute);
            if (dec.fallbackApplied && dec.effectiveBytecode && dec.effectiveLength > 0) {
                csFallback = std::move(dec.fallbackStorage);
                csBytecode = csFallback.data();
                csLength = csFallback.size();
                LOG_INFO("[D3D11Backend] Compute Shader lowered to SM 5.0 (" + std::to_string(csLength) + " bytes).");
            } else {
                uint64_t hash = ComputeBytecodeHash(csBytecode, csLength);
                LOG_WARN("[D3D11Backend] Compute Shader SM " + std::to_string(insp.majorVersion) + "." + std::to_string(insp.minorVersion) +
                         " (size: " + std::to_string(csLength) + " bytes, hash: 0x" + Logger::HexToString(hash) +
                         ") exceeds Kepler D3D11 Feature Level 11_0 limit (SM 5.0) and cannot be lowered within bounds. "
                         "Safely skipping D3D11 shader creation without feeding invalid bytecode to CreateComputeShader.");
                return DXGI_ERROR_UNSUPPORTED;
            }
        }

        HRESULT hr = m_pDevice->CreateComputeShader(
            csBytecode,
            csLength,
            nullptr,
            &outPSO.pCS);
        if (FAILED(hr)) {
            uint64_t hash = ComputeBytecodeHash(csBytecode, csLength);
            LOG_ERROR("[D3D11Backend] CreateComputeShader failed (size: " + std::to_string(csLength) +
                      " bytes, hash: 0x" + Logger::HexToString(hash) + "): " + Logger::HResultToString(hr));
            return hr;
        }
    } else {
        LOG_ERROR("[D3D11Backend] CreateTranslatedComputePSO: Missing CS bytecode.");
        return E_INVALIDARG;
    }

    NotifyPsoCreated();
    return S_OK;
}

HRESULT D3D11Backend::ReadbackRenderTarget(
    ID3D12Resource* pD3D12Resource,
    std::vector<uint32_t>& outPixels,
    UINT& outWidth,
    UINT& outHeight)
{
    if (!m_initialized || !m_pDevice || !m_pContext || !pD3D12Resource) return E_FAIL;

    TranslatedResourceInfo info;
    if (!ResourceManager::Instance().GetResourceInfo(pD3D12Resource, info) || !info.pD3D11Texture) {
        LOG_ERROR("[D3D11Backend] ReadbackRenderTarget: Failed to resolve underlying D3D11 texture.");
        return E_FAIL;
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    info.pD3D11Texture->GetDesc(&texDesc);

    D3D11_TEXTURE2D_DESC stagingDesc = texDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ID3D11Texture2D* pStaging = nullptr;
    HRESULT hr = m_pDevice->CreateTexture2D(&stagingDesc, nullptr, &pStaging);
    if (FAILED(hr) || !pStaging) {
        LOG_ERROR("[D3D11Backend] ReadbackRenderTarget: Failed to create staging texture: " + Logger::HResultToString(hr));
        return hr;
    }

    m_pContext->CopyResource(pStaging, info.pD3D11Texture);
    m_pContext->Flush();

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = m_pContext->Map(pStaging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        LOG_ERROR("[D3D11Backend] ReadbackRenderTarget: Failed to map staging texture: " + Logger::HResultToString(hr));
        pStaging->Release();
        return hr;
    }

    outWidth = texDesc.Width;
    outHeight = texDesc.Height;
    outPixels.resize(outWidth * outHeight);

    const uint8_t* pSrc = reinterpret_cast<const uint8_t*>(mapped.pData);
    for (UINT y = 0; y < outHeight; ++y) {
        const uint32_t* pRow = reinterpret_cast<const uint32_t*>(pSrc + y * mapped.RowPitch);
        for (UINT x = 0; x < outWidth; ++x) {
            outPixels[y * outWidth + x] = pRow[x];
        }
    }

    m_pContext->Unmap(pStaging, 0);
    pStaging->Release();
    return S_OK;
}

HRESULT D3D11Backend::ReadbackBuffer(
    ID3D12Resource* pD3D12Resource,
    void* pOutData,
    SIZE_T numBytes)
{
    if (!m_initialized || !m_pDevice || !m_pContext || !pD3D12Resource || !pOutData || numBytes == 0) {
        return E_FAIL;
    }

    TranslatedResourceInfo info;
    if (!ResourceManager::Instance().GetResourceInfo(pD3D12Resource, info) || !info.pD3D11Buffer) {
        LOG_ERROR("[D3D11Backend] ReadbackBuffer: Failed to resolve underlying D3D11 buffer.");
        return E_FAIL;
    }

    D3D11_BUFFER_DESC stagingDesc = {};
    stagingDesc.ByteWidth = static_cast<UINT>(numBytes);
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ID3D11Buffer* pStaging = nullptr;
    HRESULT hr = m_pDevice->CreateBuffer(&stagingDesc, nullptr, &pStaging);
    if (FAILED(hr) || !pStaging) {
        LOG_ERROR("[D3D11Backend] ReadbackBuffer: Failed to create staging buffer: " + Logger::HResultToString(hr));
        return hr;
    }

    D3D11_BOX box = {};
    box.left = 0;
    box.right = static_cast<UINT>(numBytes);
    box.top = 0; box.bottom = 1;
    box.front = 0; box.back = 1;

    m_pContext->CopySubresourceRegion(pStaging, 0, 0, 0, 0, info.pD3D11Buffer, 0, &box);
    m_pContext->Flush();

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = m_pContext->Map(pStaging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        LOG_ERROR("[D3D11Backend] ReadbackBuffer: Failed to map staging buffer: " + Logger::HResultToString(hr));
        pStaging->Release();
        return hr;
    }

    memcpy(pOutData, mapped.pData, numBytes);
    m_pContext->Unmap(pStaging, 0);
    pStaging->Release();
    return S_OK;
}

HRESULT D3D11Backend::CreateSwapChainForHwnd(
    HWND hwnd,
    UINT width,
    UINT height,
    IDXGISwapChain** ppSwapChain)
{
    if (!ppSwapChain) return E_POINTER;
    *ppSwapChain = nullptr;
    if (!m_initialized || !m_pDevice || !hwnd) return E_FAIL;

    IDXGIDevice* pDXGIDevice = nullptr;
    HRESULT hr = m_pDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&pDXGIDevice));
    if (FAILED(hr) || !pDXGIDevice) return hr;

    IDXGIAdapter* pAdapter = nullptr;
    hr = pDXGIDevice->GetAdapter(&pAdapter);
    pDXGIDevice->Release();
    if (FAILED(hr) || !pAdapter) return hr;

    IDXGIFactory* pFactory = nullptr;
    hr = pAdapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&pFactory));
    pAdapter->Release();
    if (FAILED(hr) || !pFactory) return hr;

    DXGI_SWAP_CHAIN_DESC scDesc = {};
    scDesc.BufferCount = 1;
    scDesc.BufferDesc.Width = width;
    scDesc.BufferDesc.Height = height;
    scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferDesc.RefreshRate.Numerator = 60;
    scDesc.BufferDesc.RefreshRate.Denominator = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.OutputWindow = hwnd;
    scDesc.SampleDesc.Count = 1;
    scDesc.SampleDesc.Quality = 0;
    scDesc.Windowed = TRUE;

    hr = pFactory->CreateSwapChain(m_pDevice, &scDesc, ppSwapChain);
    pFactory->Release();

    if (SUCCEEDED(hr)) {
        LOG_INFO("[D3D11Backend] Successfully created DXGI Swapchain for HWND: " +
                 std::to_string(width) + "x" + std::to_string(height));
    }
    return hr;
}

HRESULT D3D11Backend::PresentSwapChain(IDXGISwapChain* pSwapChain, UINT syncInterval, UINT flags) {
    if (!pSwapChain) return E_POINTER;
    HRESULT hr = pSwapChain->Present(syncInterval, flags);
    if (!RuntimeTelemetry::Instance().IsWrappedSwapChain(pSwapChain)) {
        RuntimeTelemetry::Instance().RecordPresent(hr, 0.0, pSwapChain, 0, flags);
    }
    if (FAILED(hr)) {
        DiagnosticReport rep;
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            rep.errorCode = KeplerErrorCode::DeviceRemoved;
            rep.severity = ErrorSeverity::Fatal;
            rep.explanation = "Device removed or reset during swap chain presentation. Reason: " +
                              Logger::HResultToString(m_pDevice ? m_pDevice->GetDeviceRemovedReason() : hr);
            rep.actionableHint = "The GPU timed out (TDR) or crashed. Reduce resolution, texture quality, or GPU workload.";
        } else {
            rep.errorCode = KeplerErrorCode::PresentationFailed;
            rep.severity = ErrorSeverity::Error;
            rep.explanation = "DXGI swap chain Present call failed.";
            rep.actionableHint = "Check if target window was destroyed, minimized, or occluded during presentation.";
        }
        rep.subsystem = "D3D11Backend";
        rep.apiOperation = "PresentSwapChain";
        rep.nativeHResult = hr;
        rep.disposition = ExecutionDisposition::Rejected;
        LOG_DIAGNOSTIC_REPORT(rep);
    }
    return hr;
}

void D3D11Backend::ResolveBarrierHazards(const RecordedCommand& cmd) {
    if (!m_pContext) return;
    for (const auto& b : cmd.barrier.barriers) {
        if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
            ID3D12Resource* pRes = b.Transition.pResource;
            // If transitioning to Shader Resource, unbind RTV to avoid D3D11 simultaneous RTV/SRV binding hazard
            if (b.Transition.StateAfter & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE ||
                b.Transition.StateAfter & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
            {
                ID3D11RenderTargetView* nullRTV[1] = { nullptr };
                m_pContext->OMSetRenderTargets(1, nullRTV, nullptr);
                for (UINT i = 0; i < 8; ++i) m_boundRTVs[i] = nullptr;
                m_numBoundRTVs = 0;
            }
            // If transitioning from UAV, unbind active UAV slots that bind this resource
            if (b.Transition.StateBefore & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                for (UINT s = 0; s < 8; ++s) {
                    if (!pRes || m_activeComputeUAVs[s].pD3D12Resource == pRes) {
                        if (m_activeComputeUAVs[s].pUAV) {
                            ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
                            UINT initCount = static_cast<UINT>(-1);
                            m_pContext->CSSetUnorderedAccessViews(s, 1, nullUAV, &initCount);
                            m_activeComputeUAVs[s] = {};
                        }
                    }
                    if (!pRes || m_activeGraphicsUAVs[s].pD3D12Resource == pRes) {
                        if (m_activeGraphicsUAVs[s].pUAV) {
                            m_activeGraphicsUAVs[s] = {};
                        }
                    }
                }
            }
        }
        else if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV) {
            ID3D12Resource* pTargetRes = b.UAV.pResource;
            // UAV barrier: Synchronize preceding UAV writes with subsequent reads/writes
            // Unbind active UAV slots matching target resource (or all if nullptr)
            for (UINT s = 0; s < 8; ++s) {
                if (!pTargetRes || m_activeComputeUAVs[s].pD3D12Resource == pTargetRes) {
                    if (m_activeComputeUAVs[s].pUAV) {
                        ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
                        UINT initCount = static_cast<UINT>(-1);
                        m_pContext->CSSetUnorderedAccessViews(s, 1, nullUAV, &initCount);
                        m_activeComputeUAVs[s] = {};
                    }
                }
                if (!pTargetRes || m_activeGraphicsUAVs[s].pD3D12Resource == pTargetRes) {
                    if (m_activeGraphicsUAVs[s].pUAV) {
                        m_activeGraphicsUAVs[s] = {};
                    }
                }
            }
            // Flush immediate context to drain queued UAV writes
            m_pContext->Flush();
        }
    }
}

void D3D11Backend::ResolveUavSrvHazardBeforeSrvBind(ID3D11Resource* pTargetRes, const std::string& stageName) {
    if (!pTargetRes || !m_pContext) return;

    // Check active Compute UAVs
    for (UINT s = 0; s < 8; ++s) {
        if (m_activeComputeUAVs[s].pUAV) {
            ID3D11Resource* pUavRes = m_activeComputeUAVs[s].pD3D11Resource;
            if (!pUavRes) {
                m_activeComputeUAVs[s].pUAV->GetResource(&pUavRes);
                if (pUavRes) {
                    m_activeComputeUAVs[s].pD3D11Resource = pUavRes;
                    pUavRes->Release(); // GetResource AddRefs
                }
            }
            if (pUavRes == pTargetRes) {
                ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
                UINT initCount = static_cast<UINT>(-1);
                m_pContext->CSSetUnorderedAccessViews(s, 1, nullUAV, &initCount);
                m_activeComputeUAVs[s] = {};
                LOG_INFO("[HAZARD] Automatically unbound conflicting Compute UAV slot " + std::to_string(s) +
                         " prior to SRV bind in " + stageName);
            }
        }
    }

    // Check active Graphics UAVs
    for (UINT s = 0; s < 8; ++s) {
        if (m_activeGraphicsUAVs[s].pUAV) {
            ID3D11Resource* pUavRes = m_activeGraphicsUAVs[s].pD3D11Resource;
            if (!pUavRes) {
                m_activeGraphicsUAVs[s].pUAV->GetResource(&pUavRes);
                if (pUavRes) {
                    m_activeGraphicsUAVs[s].pD3D11Resource = pUavRes;
                    pUavRes->Release();
                }
            }
            if (pUavRes == pTargetRes) {
                m_activeGraphicsUAVs[s] = {};
                LOG_INFO("[HAZARD] Cleared conflicting Graphics UAV slot " + std::to_string(s) +
                         " prior to SRV bind in " + stageName);
            }
        }
    }

    // Check active RTVs
    for (UINT s = 0; s < 8; ++s) {
        if (m_boundRTVs[s]) {
            ID3D11Resource* pRtvRes = nullptr;
            m_boundRTVs[s]->GetResource(&pRtvRes);
            if (pRtvRes) {
                pRtvRes->Release();
                if (pRtvRes == pTargetRes) {
                    m_boundRTVs[s] = nullptr;
                    m_pContext->OMSetRenderTargets(8, m_boundRTVs, m_boundDSV);
                    LOG_INFO("[HAZARD] Automatically unbound conflicting RTV slot " + std::to_string(s) +
                             " prior to SRV bind in " + stageName);
                }
            }
        }
    }
}

void D3D11Backend::ResolveUavSrvHazardBeforeUavBind(ID3D11Resource* pTargetRes, UINT targetUavSlot) {
    if (!pTargetRes || !m_pContext) return;

    // Check active Compute SRVs
    for (UINT s = 0; s < 16; ++s) {
        if (m_activeComputeSRVs[s].pSRV && m_activeComputeSRVs[s].pD3D11Resource == pTargetRes) {
            ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
            m_pContext->CSSetShaderResources(s, 1, nullSRV);
            m_activeComputeSRVs[s] = {};
            LOG_INFO("[HAZARD] Automatically unbound conflicting CS SRV slot " + std::to_string(s) +
                     " prior to UAV bind in slot " + std::to_string(targetUavSlot));
        }
    }

    // Check active Pixel SRVs
    for (UINT s = 0; s < 16; ++s) {
        if (m_activePixelSRVs[s].pSRV && m_activePixelSRVs[s].pD3D11Resource == pTargetRes) {
            ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
            m_pContext->PSSetShaderResources(s, 1, nullSRV);
            m_activePixelSRVs[s] = {};
            LOG_INFO("[HAZARD] Automatically unbound conflicting PS SRV slot " + std::to_string(s) +
                     " prior to UAV bind in slot " + std::to_string(targetUavSlot));
        }
    }

    // Check active Vertex SRVs
    for (UINT s = 0; s < 16; ++s) {
        if (m_activeVertexSRVs[s].pSRV && m_activeVertexSRVs[s].pD3D11Resource == pTargetRes) {
            ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
            m_pContext->VSSetShaderResources(s, 1, nullSRV);
            m_activeVertexSRVs[s] = {};
            LOG_INFO("[HAZARD] Automatically unbound conflicting VS SRV slot " + std::to_string(s) +
                     " prior to UAV bind in slot " + std::to_string(targetUavSlot));
        }
    }
}

void D3D11Backend::BindSRV(UINT slot, ID3D11ShaderResourceView* pSRV, bool isCompute, D3D12_SHADER_VISIBILITY visibility, ID3D12Resource* pD3D12Res) {
    if (!m_initialized || !m_pContext) return;

    ID3D11Resource* pD3D11Res = nullptr;
    if (pSRV) {
        pSRV->GetResource(&pD3D11Res);
        if (pD3D11Res) {
            std::string stageName = isCompute ? "CS" : (visibility == D3D12_SHADER_VISIBILITY_VERTEX ? "VS" : "PS");
            ResolveUavSrvHazardBeforeSrvBind(pD3D11Res, stageName);
            pD3D11Res->Release(); // GetResource AddRefs
        }
    }

    if (isCompute) {
        m_pContext->CSSetShaderResources(slot, 1, &pSRV);
        if (slot < 16) {
            m_activeComputeSRVs[slot] = { pSRV, pD3D12Res, pD3D11Res };
        }
    } else {
        if (visibility == D3D12_SHADER_VISIBILITY_VERTEX || visibility == D3D12_SHADER_VISIBILITY_ALL) {
            m_pContext->VSSetShaderResources(slot, 1, &pSRV);
            if (slot < 16) {
                m_activeVertexSRVs[slot] = { pSRV, pD3D12Res, pD3D11Res };
            }
        }
        if (visibility == D3D12_SHADER_VISIBILITY_PIXEL || visibility == D3D12_SHADER_VISIBILITY_ALL) {
            m_pContext->PSSetShaderResources(slot, 1, &pSRV);
            if (slot < 16) {
                m_activePixelSRVs[slot] = { pSRV, pD3D12Res, pD3D11Res };
            }
        }
    }
}

void D3D11Backend::BindUAV(UINT slot, ID3D11UnorderedAccessView* pUAV, UINT initCount, bool isCompute, D3D12_SHADER_VISIBILITY visibility, ID3D12Resource* pD3D12Res, ID3D11Resource* pD3D11Res) {
    if (!m_initialized || !m_pContext || slot >= 8) return;

    if (pUAV && !pD3D11Res) {
        pUAV->GetResource(&pD3D11Res);
        if (pD3D11Res) {
            pD3D11Res->Release(); // GetResource AddRefs
        }
    }

    if (pD3D11Res) {
        ResolveUavSrvHazardBeforeUavBind(pD3D11Res, slot);
    }

    if (isCompute) {
        m_pContext->CSSetUnorderedAccessViews(slot, 1, &pUAV, &initCount);
        m_activeComputeUAVs[slot] = { pUAV, pD3D12Res, pD3D11Res, initCount };
    } else {
        if (visibility == D3D12_SHADER_VISIBILITY_PIXEL || visibility == D3D12_SHADER_VISIBILITY_ALL) {
            m_activeGraphicsUAVs[slot] = { pUAV, pD3D12Res, pD3D11Res, initCount };
        }
    }
}

HRESULT D3D11Backend::UpdateAndBindRootConstants(
    UINT slot,
    const std::vector<uint32_t>& constants,
    bool isCompute,
    D3D12_SHADER_VISIBILITY visibility)
{
    if (!m_initialized || !m_pDevice || !m_pContext) return E_FAIL;
    if (slot >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT) {
        LOG_WARN("[D3D11Backend] Root constant slot " + std::to_string(slot) +
                 " exceeds D3D11 hardware slot limit (14). Command rejected.");
        return E_INVALIDARG;
    }

    auto& cbMap = isCompute ? m_computeConstantBuffers : m_graphicsConstantBuffers;
    UINT numBytes = static_cast<UINT>(constants.size() * sizeof(uint32_t));
    if (numBytes == 0) return S_OK;

    // D3D11 constant buffer size must be multiple of 16 bytes.
    // Also pad to minimum 256 bytes for standard D3D12 constant buffer alignment.
    UINT alignedBytes = ((numBytes + 15) / 16) * 16;
    if (alignedBytes < 256) alignedBytes = 256;

    ID3D11Buffer* pBuffer = nullptr;
    auto it = cbMap.find(slot);
    if (it != cbMap.end() && it->second) {
        pBuffer = it->second;
    } else {
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = alignedBytes;
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        HRESULT hr = m_pDevice->CreateBuffer(&cbDesc, nullptr, &pBuffer);
        if (FAILED(hr) || !pBuffer) {
            LOG_ERROR("[D3D11Backend] Failed to create dynamic root constant buffer for slot " +
                      std::to_string(slot) + ": " + Logger::HResultToString(hr));
            return hr;
        }
        cbMap[slot] = pBuffer;
    }

    auto& shadowMap = isCompute ? m_computeConstantShadow : m_graphicsConstantShadow;
    auto sIt = shadowMap.find(slot);
    bool dataChanged = true;
    if (sIt != shadowMap.end() && sIt->second == constants) {
        dataChanged = false;
    }

    if (dataChanged) {
        m_rootConstantMapCount++;
        // Upload constants via Map(WRITE_DISCARD)
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = m_pContext->Map(pBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            memcpy(mapped.pData, constants.data(), numBytes);
            if (alignedBytes > numBytes) {
                memset(static_cast<uint8_t*>(mapped.pData) + numBytes, 0, alignedBytes - numBytes);
            }
            m_pContext->Unmap(pBuffer, 0);
            shadowMap[slot] = constants;
        } else {
            LOG_WARN("[D3D11Backend] Failed to map root constant buffer for slot " +
                     std::to_string(slot) + ": " + Logger::HResultToString(hr));
            return hr;
        }
    } else {
        m_rootConstantMapSkippedCount++;
    }

    // Bind to active shader stages respecting visibility
    if (!isCompute) {
        if (visibility == D3D12_SHADER_VISIBILITY_VERTEX || visibility == D3D12_SHADER_VISIBILITY_ALL) {
            m_pContext->VSSetConstantBuffers(slot, 1, &pBuffer);
        }
        if (visibility == D3D12_SHADER_VISIBILITY_PIXEL || visibility == D3D12_SHADER_VISIBILITY_ALL) {
            m_pContext->PSSetConstantBuffers(slot, 1, &pBuffer);
        }
    } else {
        m_pContext->CSSetConstantBuffers(slot, 1, &pBuffer);
    }

    return S_OK;
}

HRESULT D3D11Backend::ExecuteSingleCommand(const RecordedCommand& cmd) {
    if (!m_initialized || !m_pContext) return E_FAIL;

    switch (cmd.type) {
        case CommandType::ClearRenderTarget: {
            ID3D11RenderTargetView* pRTV = ResourceManager::Instance().GetRTV(cmd.clearRTV.renderTargetView.ptr);
            if (pRTV) {
                m_pContext->ClearRenderTargetView(pRTV, cmd.clearRTV.colorRGBA);
                m_translatedCount++;
            } else {
                LOG_WARN("[TRANSLATION] ClearRenderTarget: RTV descriptor handle not resolved: 0x" +
                         std::to_string(cmd.clearRTV.renderTargetView.ptr));
                m_rejectedCount++;
            }
            break;
        }

        case CommandType::ClearDepthStencil: {
            ID3D11DepthStencilView* pDSV = ResourceManager::Instance().GetDSV(cmd.clearDSV.depthStencilView.ptr);
            if (pDSV) {
                UINT flags11 = 0;
                if (cmd.clearDSV.clearFlags & D3D12_CLEAR_FLAG_DEPTH) flags11 |= D3D11_CLEAR_DEPTH;
                if (cmd.clearDSV.clearFlags & D3D12_CLEAR_FLAG_STENCIL) flags11 |= D3D11_CLEAR_STENCIL;
                if (flags11 != 0) {
                    m_pContext->ClearDepthStencilView(pDSV, flags11, cmd.clearDSV.depth, cmd.clearDSV.stencil);
                    m_translatedCount++;
                }
            } else {
                m_rejectedCount++;
            }
            break;
        }

        case CommandType::DrawInstanced: {
            m_pContext->DrawInstanced(
                cmd.drawInstanced.vertexCountPerInstance,
                cmd.drawInstanced.instanceCount,
                cmd.drawInstanced.startVertexLocation,
                cmd.drawInstanced.startInstanceLocation);
            m_translatedCount++;
            if (m_activePSOIsTranslated) {
                RuntimeTelemetry::Instance().SetUnpresentedTranslatedWork(true);
            }
            break;
        }

        case CommandType::DrawIndexedInstanced: {
            m_pContext->DrawIndexedInstanced(
                cmd.drawIndexed.indexCountPerInstance,
                cmd.drawIndexed.instanceCount,
                cmd.drawIndexed.startIndexLocation,
                cmd.drawIndexed.baseVertexLocation,
                cmd.drawIndexed.startInstanceLocation);
            m_translatedCount++;
            if (m_activePSOIsTranslated) {
                RuntimeTelemetry::Instance().SetUnpresentedTranslatedWork(true);
            }
            break;
        }

        case CommandType::Dispatch: {
            m_pContext->Dispatch(
                cmd.dispatch.threadGroupCountX,
                cmd.dispatch.threadGroupCountY,
                cmd.dispatch.threadGroupCountZ);
            m_translatedCount++;
            if (m_activePSOIsTranslated) {
                RuntimeTelemetry::Instance().SetUnpresentedTranslatedWork(true);
            }
            break;
        }

        case CommandType::CopyBufferRegion: {
            TranslatedResourceInfo dstInfo, srcInfo;
            if (ResourceManager::Instance().GetResourceInfo(cmd.copyBuffer.pDstBuffer, dstInfo) &&
                ResourceManager::Instance().GetResourceInfo(cmd.copyBuffer.pSrcBuffer, srcInfo))
            {
                if (srcInfo.heapProps.Type == D3D12_HEAP_TYPE_UPLOAD && dstInfo.pD3D11Buffer) {
                    void* cpuPtr = nullptr;
                    D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(srcInfo.d3d12Desc.Width) };
                    if (SUCCEEDED(cmd.copyBuffer.pSrcBuffer->Map(0, &readRange, &cpuPtr)) && cpuPtr) {
                        const uint8_t* srcSlice = static_cast<const uint8_t*>(cpuPtr) + cmd.copyBuffer.srcOffset;
                        D3D11_BOX dstBox = {};
                        dstBox.left = static_cast<UINT>(cmd.copyBuffer.dstOffset);
                        dstBox.right = static_cast<UINT>(cmd.copyBuffer.dstOffset + cmd.copyBuffer.numBytes);
                        dstBox.top = 0; dstBox.bottom = 1;
                        dstBox.front = 0; dstBox.back = 1;
                        m_pContext->UpdateSubresource(dstInfo.pD3D11Buffer, 0, &dstBox, srcSlice, 0, 0);
                        D3D12_RANGE emptyRange = { 0, 0 };
                        cmd.copyBuffer.pSrcBuffer->Unmap(0, &emptyRange);
                        m_translatedCount++;
                        break;
                    }
                }
                if (dstInfo.pD3D11Buffer && srcInfo.pD3D11Buffer) {
                    D3D11_BOX srcBox = {};
                    srcBox.left = static_cast<UINT>(cmd.copyBuffer.srcOffset);
                    srcBox.right = static_cast<UINT>(cmd.copyBuffer.srcOffset + cmd.copyBuffer.numBytes);
                    srcBox.top = 0; srcBox.bottom = 1;
                    srcBox.front = 0; srcBox.back = 1;

                    m_pContext->CopySubresourceRegion(
                        dstInfo.pD3D11Buffer, 0,
                        static_cast<UINT>(cmd.copyBuffer.dstOffset), 0, 0,
                        srcInfo.pD3D11Buffer, 0, &srcBox);
                    m_translatedCount++;
                }
            } else {
                m_rejectedCount++;
            }
            break;
        }

        case CommandType::CopyResource: {
            TranslatedResourceInfo dstInfo, srcInfo;
            if (ResourceManager::Instance().GetResourceInfo(cmd.copyResource.pDstResource, dstInfo) &&
                ResourceManager::Instance().GetResourceInfo(cmd.copyResource.pSrcResource, srcInfo))
            {
                if (dstInfo.pD3D11Resource && srcInfo.pD3D11Resource) {
                    m_pContext->CopyResource(dstInfo.pD3D11Resource, srcInfo.pD3D11Resource);
                    m_translatedCount++;
                }
            } else {
                m_rejectedCount++;
            }
            break;
        }

        case CommandType::ResourceBarrier: {
            if (Config::Instance().IsBarrierEmulationEnabled()) {
                ResolveBarrierHazards(cmd);
                m_translatedCount++;
            }
            break;
        }

        case CommandType::SetRenderTargets: {
            m_numBoundRTVs = 0;
            for (size_t i = 0; i < 8; ++i) {
                m_boundRTVs[i] = nullptr;
            }
            for (size_t i = 0; i < cmd.setRTVs.rtvHandles.size() && i < 8; ++i) {
                ID3D11RenderTargetView* pRTV = ResourceManager::Instance().GetRTV(cmd.setRTVs.rtvHandles[i].ptr);
                if (pRTV) {
                    m_boundRTVs[i] = pRTV;
                    m_numBoundRTVs = static_cast<UINT>(i + 1);
                }
            }
            m_boundDSV = cmd.setRTVs.hasDepthStencil ?
                ResourceManager::Instance().GetDSV(cmd.setRTVs.dsvHandle.ptr) : nullptr;

            bool hasGraphicsUAVs = false;
            for (UINT s = 0; s < 8; ++s) {
                if (m_activeGraphicsUAVs[s].pUAV) {
                    hasGraphicsUAVs = true;
                    break;
                }
            }

            if (hasGraphicsUAVs) {
                UINT uavStartSlot = m_numBoundRTVs;
                if (uavStartSlot < 8) {
                    UINT numUAVs = 8 - uavStartSlot;
                    ID3D11UnorderedAccessView* uavArray[8] = { nullptr };
                    UINT initCounts[8];
                    for (UINT s = 0; s < numUAVs; ++s) {
                        uavArray[s] = m_activeGraphicsUAVs[uavStartSlot + s].pUAV;
                        initCounts[s] = m_activeGraphicsUAVs[uavStartSlot + s].initialCount;
                    }
                    m_pContext->OMSetRenderTargetsAndUnorderedAccessViews(
                        m_numBoundRTVs,
                        m_numBoundRTVs > 0 ? m_boundRTVs : nullptr,
                        m_boundDSV,
                        uavStartSlot,
                        numUAVs,
                        uavArray,
                        initCounts);
                }
            } else {
                m_pContext->OMSetRenderTargets(
                    m_numBoundRTVs,
                    m_numBoundRTVs > 0 ? m_boundRTVs : nullptr,
                    m_boundDSV);
            }
            m_translatedCount++;
            break;
        }

        case CommandType::SetViewports: {
            D3D11_VIEWPORT vps[16];
            UINT count = static_cast<UINT>(std::min<size_t>(cmd.setViewports.viewports.size(), 16));
            for (UINT i = 0; i < count; ++i) {
                const auto& vp = cmd.setViewports.viewports[i];
                vps[i].TopLeftX = vp.TopLeftX;
                vps[i].TopLeftY = vp.TopLeftY;
                vps[i].Width = vp.Width;
                vps[i].Height = vp.Height;
                vps[i].MinDepth = vp.MinDepth;
                vps[i].MaxDepth = vp.MaxDepth;
            }
            if (count > 0) {
                m_pContext->RSSetViewports(count, vps);
                m_translatedCount++;
            }
            break;
        }

        case CommandType::SetScissorRects: {
            D3D11_RECT rects[16];
            UINT count = static_cast<UINT>(std::min<size_t>(cmd.setScissors.rects.size(), 16));
            for (UINT i = 0; i < count; ++i) {
                const auto& r = cmd.setScissors.rects[i];
                rects[i].left = r.left;
                rects[i].top = r.top;
                rects[i].right = r.right;
                rects[i].bottom = r.bottom;
            }
            if (count > 0) {
                m_pContext->RSSetScissorRects(count, rects);
                m_translatedCount++;
            }
            break;
        }

        case CommandType::IASetPrimitiveTopology: {
            m_pContext->IASetPrimitiveTopology(
                static_cast<D3D11_PRIMITIVE_TOPOLOGY>(cmd.setTopology.topology));
            m_translatedCount++;
            break;
        }

        case CommandType::IASetVertexBuffers: {
            for (size_t i = 0; i < cmd.setVBs.views.size(); ++i) {
                const auto& vb = cmd.setVBs.views[i];
                ID3D11Buffer* pBuf = nullptr;
                UINT offset = static_cast<UINT>(vb.offset);

                if (vb.pBuffer) {
                    TranslatedResourceInfo info;
                    if (ResourceManager::Instance().GetResourceInfo(vb.pBuffer, info)) {
                        pBuf = info.pD3D11Buffer;
                        if (pBuf && info.heapProps.Type == D3D12_HEAP_TYPE_UPLOAD) {
                            void* cpuPtr = nullptr;
                            D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(info.d3d12Desc.Width) };
                            if (SUCCEEDED(vb.pBuffer->Map(0, &readRange, &cpuPtr)) && cpuPtr) {
                                D3D11_MAPPED_SUBRESOURCE mapped = {};
                                if (SUCCEEDED(m_pContext->Map(pBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                                    memcpy(mapped.pData, cpuPtr, static_cast<size_t>(info.d3d12Desc.Width));
                                    m_pContext->Unmap(pBuf, 0);
                                }
                                D3D12_RANGE emptyRange = { 0, 0 };
                                vb.pBuffer->Unmap(0, &emptyRange);
                            }
                        }
                    }
                }

                UINT stride = vb.strideInBytes;
                UINT slot = cmd.setVBs.startSlot + static_cast<UINT>(i);
                if (!pBuf) {
                    LOG_WARN("[TRANSLATION] IASetVertexBuffers: NULL buffer at slot " + std::to_string(slot) +
                             " (vb.pBuffer=" + (vb.pBuffer ? "resolved" : "unresolved") + ")");
                }
                m_pContext->IASetVertexBuffers(slot, 1, &pBuf, &stride, &offset);
            }
            m_translatedCount++;
            break;
        }

        case CommandType::IASetIndexBuffer: {
            ID3D11Buffer* pBuf = nullptr;
            UINT offset = static_cast<UINT>(cmd.setIB.offset);

            if (cmd.setIB.pBuffer) {
                TranslatedResourceInfo info;
                if (ResourceManager::Instance().GetResourceInfo(cmd.setIB.pBuffer, info)) {
                    pBuf = info.pD3D11Buffer;
                    if (pBuf && info.heapProps.Type == D3D12_HEAP_TYPE_UPLOAD) {
                        void* cpuPtr = nullptr;
                        D3D12_RANGE readRange = { 0, static_cast<SIZE_T>(info.d3d12Desc.Width) };
                        if (SUCCEEDED(cmd.setIB.pBuffer->Map(0, &readRange, &cpuPtr)) && cpuPtr) {
                            D3D11_MAPPED_SUBRESOURCE mapped = {};
                            if (SUCCEEDED(m_pContext->Map(pBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                                memcpy(mapped.pData, cpuPtr, static_cast<size_t>(info.d3d12Desc.Width));
                                m_pContext->Unmap(pBuf, 0);
                            }
                            D3D12_RANGE emptyRange = { 0, 0 };
                            cmd.setIB.pBuffer->Unmap(0, &emptyRange);
                        }
                    }
                }
            }

            m_pContext->IASetIndexBuffer(pBuf, cmd.setIB.format, offset);
            m_translatedCount++;
            break;
        }

        case CommandType::SetPipelineState: {
            TranslatedPipelineState pso = {};
            if (ResourceManager::Instance().GetPSO(cmd.setPSO.pPipelineState, pso)) {
                if (pso.pVS || pso.pPS || pso.pCS) {
                    m_activePSOIsTranslated = true;
                    // Valid translated PSO — apply all shader stages and pipeline state
                    if (pso.pVS) m_pContext->VSSetShader(pso.pVS, nullptr, 0);
                    if (pso.pPS) m_pContext->PSSetShader(pso.pPS, nullptr, 0);
                    if (pso.pCS) m_pContext->CSSetShader(pso.pCS, nullptr, 0);
                    if (pso.pInputLayout) m_pContext->IASetInputLayout(pso.pInputLayout);
                    if (pso.pRasterizerState) m_pContext->RSSetState(pso.pRasterizerState);
                    if (pso.pBlendState) m_pContext->OMSetBlendState(pso.pBlendState, nullptr, 0xFFFFFFFF);
                    if (pso.pDepthStencilState) m_pContext->OMSetDepthStencilState(pso.pDepthStencilState, 0);
                } else {
                    m_activePSOIsTranslated = false;
                    // Null sentinel — PSO was registered but translation was skipped (e.g. SM5.1 shader).
                    // Explicitly unbind all D3D11 shader stages to prevent stale state from corrupting
                    // subsequent translated draw calls when native D3D12 handles this PSO's draws.
                    m_pContext->VSSetShader(nullptr, nullptr, 0);
                    m_pContext->PSSetShader(nullptr, nullptr, 0);
                    m_pContext->CSSetShader(nullptr, nullptr, 0);
                    m_pContext->IASetInputLayout(nullptr);
                    LOG_INFO("[TRANSLATION] SetPipelineState: PSO has no D3D11 translation (SM5.1 or unsupported); D3D11 stages cleared for clean state.");
                }
                m_translatedCount++;
            } else {
                m_activePSOIsTranslated = false;
                LOG_WARN("[TRANSLATION] SetPipelineState: PSO not registered in ResourceManager.");
                m_rejectedCount++;
            }
            break;
        }

        case CommandType::SetGraphicsRootSignature: {
            m_activeGraphicsRootSig = cmd.setRootSig.pRootSignature;
            m_translatedCount++;
            break;
        }

        case CommandType::SetComputeRootSignature: {
            m_activeComputeRootSig = cmd.setRootSig.pRootSignature;
            m_translatedCount++;
            break;
        }

        case CommandType::SetGraphicsRootDescriptorTable:
        case CommandType::SetComputeRootDescriptorTable: {
            bool isCompute = cmd.setRootTable.isCompute;
            UINT rootParamIndex = cmd.setRootTable.rootParameterIndex;
            SIZE_T basePtr = cmd.setRootTable.baseDescriptor.ptr;

            ID3D12RootSignature* pActiveSig = isCompute ? m_activeComputeRootSig : m_activeGraphicsRootSig;
            TranslatedRootSignature rootSig;
            bool hasSig = (pActiveSig && ResourceManager::Instance().GetRootSignature(pActiveSig, rootSig));

            if (hasSig && rootParamIndex < rootSig.parameters.size()) {
                const auto& param = rootSig.parameters[rootParamIndex];
                for (const auto& range : param.ranges) {
                    D3D12_SHADER_VISIBILITY visibility = param.visibility;

                    UINT spaceStride = 0;
                    UINT maxSlotLimit = 0;
                    switch (range.rangeType) {
                        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                            spaceStride = 16;
                            maxSlotLimit = 128; // D3D11 maximum SRV registers
                            break;
                        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
                            spaceStride = 4;
                            maxSlotLimit = 16;  // D3D11 maximum sampler registers
                            break;
                        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                            spaceStride = 4;
                            maxSlotLimit = 13;  // Protects cb13 for Kepler dynamic staging buffer
                            break;
                        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                            spaceStride = 4;
                            maxSlotLimit = 8;   // D3D11 FL 11_0 maximum UAV registers
                            break;
                    }

                    for (UINT d = 0; d < range.numDescriptors; ++d) {
                        UINT currentSlot = range.baseShaderRegister + (range.registerSpace * spaceStride) + d;
                        if (currentSlot >= maxSlotLimit) {
                            LOG_WARN("[TRANSLATION] Descriptor range slot " + std::to_string(currentSlot) +
                                     " (base=" + std::to_string(range.baseShaderRegister) +
                                     ", space=" + std::to_string(range.registerSpace) +
                                     ") exceeds D3D11 limit of " + std::to_string(maxSlotLimit) + " slots.");
                            m_rejectedCount++;
                            continue;
                        }

                        SIZE_T currentHandle = basePtr + (range.offsetInDescriptorsFromTableStart + d) * m_descriptorIncrementSize;

                        switch (range.rangeType) {
                            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV: {
                                ID3D11ShaderResourceView* pSRV = ResourceManager::Instance().GetSRV(currentHandle);
                                if (pSRV) {
                                    BindSRV(currentSlot, pSRV, isCompute, visibility);
                                }
                                break;
                            }
                            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV: {
                                TranslatedUAVInfo uavInfo = {};
                                bool hasUavInfo = ResourceManager::Instance().GetUAVInfo(currentHandle, uavInfo);
                                ID3D11UnorderedAccessView* pUAV = hasUavInfo ? uavInfo.pUAV : ResourceManager::Instance().GetUAV(currentHandle);
                                UINT initCount = hasUavInfo ? uavInfo.initialCount : static_cast<UINT>(-1);

                                if (isCompute) {
                                    if (currentSlot < 8) {
                                        BindUAV(currentSlot, pUAV, initCount, true, visibility, hasUavInfo ? uavInfo.pD3D12Resource : nullptr, hasUavInfo ? uavInfo.pD3D11Resource : nullptr);
                                    } else {
                                        LOG_WARN("[TRANSLATION] Compute UAV slot " + std::to_string(currentSlot) +
                                                 " exceeds FL 11_0 limit of 8 registers.");
                                    }
                                } else {
                                    if (visibility == D3D12_SHADER_VISIBILITY_PIXEL || visibility == D3D12_SHADER_VISIBILITY_ALL) {
                                        if (currentSlot < 8) {
                                            BindUAV(currentSlot, pUAV, initCount, false, visibility, hasUavInfo ? uavInfo.pD3D12Resource : nullptr, hasUavInfo ? uavInfo.pD3D11Resource : nullptr);

                                            UINT uavStartSlot = m_numBoundRTVs;
                                            if (currentSlot >= uavStartSlot && (m_numBoundRTVs + (currentSlot - uavStartSlot + 1) <= 8)) {
                                                UINT numUAVs = 8 - uavStartSlot;
                                                ID3D11UnorderedAccessView* uavArray[8] = { nullptr };
                                                UINT initCounts[8];
                                                for (UINT s = 0; s < numUAVs; ++s) {
                                                    uavArray[s] = m_activeGraphicsUAVs[uavStartSlot + s].pUAV;
                                                    initCounts[s] = m_activeGraphicsUAVs[uavStartSlot + s].initialCount;
                                                }
                                                m_pContext->OMSetRenderTargetsAndUnorderedAccessViews(
                                                    m_numBoundRTVs,
                                                    m_numBoundRTVs > 0 ? m_boundRTVs : nullptr,
                                                    m_boundDSV,
                                                    uavStartSlot,
                                                    numUAVs,
                                                    uavArray,
                                                    initCounts);
                                            } else if (m_numBoundRTVs == 0) {
                                                ID3D11UnorderedAccessView* uavArray[8] = { nullptr };
                                                UINT initCounts[8];
                                                for (UINT s = 0; s < 8; ++s) {
                                                    uavArray[s] = m_activeGraphicsUAVs[s].pUAV;
                                                    initCounts[s] = m_activeGraphicsUAVs[s].initialCount;
                                                }
                                                m_pContext->OMSetRenderTargetsAndUnorderedAccessViews(
                                                    0, nullptr, m_boundDSV, 0, 8, uavArray, initCounts);
                                            } else {
                                                LOG_WARN("[TRANSLATION] Graphics UAV slot " + std::to_string(currentSlot) +
                                                         " overlaps with " + std::to_string(m_numBoundRTVs) + " bound RTVs on FL 11_0.");
                                            }
                                        } else {
                                            LOG_WARN("[TRANSLATION] Graphics UAV slot " + std::to_string(currentSlot) +
                                                     " exceeds FL 11_0 limit of 8 registers.");
                                        }
                                    }
                                }
                                break;
                            }
                            case D3D12_DESCRIPTOR_RANGE_TYPE_CBV: {
                                ID3D11Buffer* pCB = ResourceManager::Instance().GetCBV(currentHandle);
                                if (pCB) {
                                    if (isCompute) {
                                        m_pContext->CSSetConstantBuffers(currentSlot, 1, &pCB);
                                    } else {
                                        if (visibility == D3D12_SHADER_VISIBILITY_VERTEX || visibility == D3D12_SHADER_VISIBILITY_ALL) {
                                            m_pContext->VSSetConstantBuffers(currentSlot, 1, &pCB);
                                        }
                                        if (visibility == D3D12_SHADER_VISIBILITY_PIXEL || visibility == D3D12_SHADER_VISIBILITY_ALL) {
                                            m_pContext->PSSetConstantBuffers(currentSlot, 1, &pCB);
                                        }
                                    }
                                }
                                break;
                            }
                            case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER: {
                                ID3D11SamplerState* pSamp = ResourceManager::Instance().GetSampler(currentHandle);
                                if (pSamp) {
                                    if (isCompute) {
                                        m_pContext->CSSetSamplers(currentSlot, 1, &pSamp);
                                    } else {
                                        if (visibility == D3D12_SHADER_VISIBILITY_VERTEX || visibility == D3D12_SHADER_VISIBILITY_ALL) {
                                            m_pContext->VSSetSamplers(currentSlot, 1, &pSamp);
                                        }
                                        if (visibility == D3D12_SHADER_VISIBILITY_PIXEL || visibility == D3D12_SHADER_VISIBILITY_ALL) {
                                            m_pContext->PSSetSamplers(currentSlot, 1, &pSamp);
                                        }
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            } else {
                // Fallback direct slot mapping
                UINT slot = rootParamIndex;
                ID3D11ShaderResourceView* pSRV = ResourceManager::Instance().GetSRV(basePtr);
                if (pSRV) {
                    BindSRV(slot, pSRV, isCompute, isCompute ? D3D12_SHADER_VISIBILITY_ALL : D3D12_SHADER_VISIBILITY_ALL);
                }

                TranslatedUAVInfo uavInfo = {};
                bool hasUavInfo = ResourceManager::Instance().GetUAVInfo(basePtr, uavInfo);
                ID3D11UnorderedAccessView* pUAV = hasUavInfo ? uavInfo.pUAV : ResourceManager::Instance().GetUAV(basePtr);
                if (pUAV && isCompute && slot < 8) {
                    UINT initCount = hasUavInfo ? uavInfo.initialCount : static_cast<UINT>(-1);
                    BindUAV(slot, pUAV, initCount, true, D3D12_SHADER_VISIBILITY_ALL, hasUavInfo ? uavInfo.pD3D12Resource : nullptr, hasUavInfo ? uavInfo.pD3D11Resource : nullptr);
                }

                ID3D11Buffer* pCB = ResourceManager::Instance().GetCBV(basePtr);
                if (pCB) {
                    if (!isCompute) {
                        m_pContext->PSSetConstantBuffers(slot, 1, &pCB);
                        m_pContext->VSSetConstantBuffers(slot, 1, &pCB);
                    } else {
                        m_pContext->CSSetConstantBuffers(slot, 1, &pCB);
                    }
                }
            }

            m_translatedCount++;
            break;
        }

        case CommandType::SetGraphicsRoot32BitConstants:
        case CommandType::SetComputeRoot32BitConstants: {
            bool isCompute = cmd.setRootConstants.isCompute;
            UINT rootParamIndex = cmd.setRootConstants.rootParameterIndex;

            ResourceManager::Instance().BindRootConstants(
                rootParamIndex,
                cmd.setRootConstants.constants,
                isCompute);

            ID3D12RootSignature* pActiveSig = isCompute ? m_activeComputeRootSig : m_activeGraphicsRootSig;
            TranslatedRootSignature rootSig;
            bool hasSig = (pActiveSig && ResourceManager::Instance().GetRootSignature(pActiveSig, rootSig));

            UINT targetSlot = rootParamIndex;
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL;

            if (hasSig && rootParamIndex < rootSig.parameters.size()) {
                const auto& param = rootSig.parameters[rootParamIndex];
                targetSlot = param.shaderRegister;
                visibility = param.visibility;
            }

            HRESULT hr = UpdateAndBindRootConstants(
                targetSlot,
                cmd.setRootConstants.constants,
                isCompute,
                visibility);

            if (SUCCEEDED(hr)) {
                m_translatedCount++;
            } else {
                m_rejectedCount++;
            }
            break;
        }

        default:
            LOG_WARN("[TRANSLATION] Unsupported command type encountered.");
            m_rejectedCount++;
            break;
    }

    return S_OK;
}

HRESULT D3D11Backend::ReplayCommandBatch(const CommandBatch& batch) {
    if (!m_initialized || !m_pContext) return E_FAIL;
    std::lock_guard<std::mutex> lock(m_mutex);

    uint64_t startTranslated = m_translatedCount.load();
    uint64_t startRejected = m_rejectedCount.load();

    bool hasGpuWork = false;
    for (const auto& cmd : batch.GetCommands()) {
        ExecuteSingleCommand(cmd);
        if (cmd.type == CommandType::DrawInstanced ||
            cmd.type == CommandType::DrawIndexedInstanced ||
            cmd.type == CommandType::Dispatch ||
            cmd.type == CommandType::ClearRenderTarget ||
            cmd.type == CommandType::ClearDepthStencil ||
            cmd.type == CommandType::CopyBufferRegion ||
            cmd.type == CommandType::CopyResource)
        {
            hasGpuWork = true;
        }
    }

    if (Config::Instance().IsImmediateQueueFlushEnabled() && hasGpuWork) {
        m_pContext->Flush();
    }

    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_INFO("[D3D11Backend] Replayed batch of " + std::to_string(batch.GetCount()) + " commands. (" +
                 std::to_string(m_translatedCount.load() - startTranslated) + " executed, " +
                 std::to_string(m_rejectedCount.load() - startRejected) + " rejected/unsupported)");
    }

    if (m_pDevice) {
        HRESULT reason = m_pDevice->GetDeviceRemovedReason();
        if (FAILED(reason)) {
            RuntimeTelemetry::Instance().RecordDeviceRemoval(
                reason, "D3D11Backend::ReplayCommandBatch", "Device failure detected during batch replay");
            return reason;
        }
    }

    return S_OK;
}

void D3D11Backend::Flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pContext) {
        m_pContext->Flush();
    }
}

HRESULT D3D11Backend::WaitForGpuCompletion(DWORD timeoutMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || !m_pContext) {
        return E_FAIL;
    }

    if (!m_pSyncQuery && m_pDevice) {
        D3D11_QUERY_DESC qDesc = {};
        qDesc.Query = D3D11_QUERY_EVENT;
        qDesc.MiscFlags = 0;
        m_pDevice->CreateQuery(&qDesc, &m_pSyncQuery);
    }

    if (!m_pSyncQuery) {
        m_pContext->Flush();
        return S_OK;
    }

    m_pContext->End(m_pSyncQuery);
    m_pContext->Flush();

    auto startTime = std::chrono::steady_clock::now();
    BOOL queryData = FALSE;
    HRESULT hr = S_FALSE;

    while (hr == S_FALSE) {
        hr = m_pContext->GetData(m_pSyncQuery, &queryData, sizeof(BOOL), 0);
        if (hr == S_OK) {
            break;
        }

        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG) {
            RuntimeTelemetry::Instance().RecordDeviceRemoval(
                hr, "D3D11Backend::WaitForGpuCompletion", "GetData returned device removal error");
            return hr;
        }

        if (m_pDevice) {
            HRESULT reason = m_pDevice->GetDeviceRemovedReason();
            if (FAILED(reason)) {
                RuntimeTelemetry::Instance().RecordDeviceRemoval(
                    reason, "D3D11Backend::WaitForGpuCompletion", "Device removed during GPU completion wait");
                return reason;
            }
        }

        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();

        if (elapsedMs >= timeoutMs) {
            LOG_ERROR("[D3D11Backend] WaitForGpuCompletion: GPU query timed out after " + std::to_string(timeoutMs) + " ms!");
            DiagnosticReport rep;
            rep.errorCode = KeplerErrorCode::DeviceTimeout;
            rep.severity = ErrorSeverity::Error;
            rep.subsystem = "D3D11Backend";
            rep.apiOperation = "WaitForGpuCompletion";
            rep.nativeHResult = DXGI_ERROR_WAIT_TIMEOUT;
            rep.disposition = ExecutionDisposition::Rejected;
            rep.explanation = "D3D11 GPU event query timed out waiting for work completion.";
            rep.actionableHint = "Check for long-running compute shaders or GPU TDR resets.";
            LOG_DIAGNOSTIC_REPORT(rep);
            return DXGI_ERROR_WAIT_TIMEOUT;
        }

        SwitchToThread();
    }

    return SUCCEEDED(hr) ? S_OK : hr;
}

void D3D11Backend::NotifyPsoCreated() {
    uint32_t current = ++m_psoBurstCount;
    if (current >= PSO_BURST_THROTTLE_THRESHOLD) {
        OnPsoBurstCheckpoint();
    }
}

void D3D11Backend::OnPsoBurstCheckpoint() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_psoBurstCount = 0;
    m_psoBurstCheckpointCount++;

    if (m_pContext) {
        m_pContext->Flush();
    }

    if (Config::Instance().IsVerboseCallsEnabled()) {
        LOG_INFO("[D3D11Backend] PSO compilation burst threshold (" +
                 std::to_string(PSO_BURST_THROTTLE_THRESHOLD) +
                 ") reached. Yielding thread and flushing context to mitigate TDR.");
    }

    SwitchToThread();
}

} // namespace kepler
