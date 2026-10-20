#include "downgrader/pso_hook.h"
#include "downgrader/shader_policy.h"
#include "core/config.h"
#include "core/logger.h"
#include "translator/d3d11_backend.h"
#include "translator/resource_manager.h"
#include <sstream>

namespace kepler {

HRESULT PSOHook::InterceptCreateGraphicsPipelineState(
    ID3D12Device* pRealDevice,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc,
    REFIID riid,
    void** ppPipelineState)
{
    if (!pRealDevice) return E_FAIL;
    if (!pDesc) return pRealDevice->CreateGraphicsPipelineState(pDesc, riid, ppPipelineState);

    bool inspectionEnabled = Config::Instance().IsShaderInspectionEnabled();
    auto& policy = ShaderPolicy::Instance();

    ShaderPolicyDecision vsDecision, psDecision, dsDecision, hsDecision, gsDecision;
    bool anyFallbackApplied = false;

    if (inspectionEnabled) {
        LOG_INFO("[PSO] === Intercepted CreateGraphicsPipelineState ===");

        // Vertex Shader
        if (pDesc->VS.pShaderBytecode && pDesc->VS.BytecodeLength > 0) {
            vsDecision = policy.EvaluateShader(pDesc->VS.pShaderBytecode, pDesc->VS.BytecodeLength, ShaderStage::Vertex);
            LOG_INFO("[PSO] VS: " + DxilInspector::ContainerTypeToString(vsDecision.inspection.containerType) +
                     " | " + DxilInspector::ShaderModelToString(vsDecision.inspection.majorVersion, vsDecision.inspection.minorVersion) +
                     " | " + std::to_string(pDesc->VS.BytecodeLength) + " bytes | " + vsDecision.actionReason);
            if (vsDecision.fallbackApplied) anyFallbackApplied = true;
        }

        // Pixel Shader
        if (pDesc->PS.pShaderBytecode && pDesc->PS.BytecodeLength > 0) {
            psDecision = policy.EvaluateShader(pDesc->PS.pShaderBytecode, pDesc->PS.BytecodeLength, ShaderStage::Pixel);
            LOG_INFO("[PSO] PS: " + DxilInspector::ContainerTypeToString(psDecision.inspection.containerType) +
                     " | " + DxilInspector::ShaderModelToString(psDecision.inspection.majorVersion, psDecision.inspection.minorVersion) +
                     " | " + std::to_string(pDesc->PS.BytecodeLength) + " bytes | " + psDecision.actionReason);
            if (psDecision.fallbackApplied) anyFallbackApplied = true;
        }

        // Domain Shader
        if (pDesc->DS.pShaderBytecode && pDesc->DS.BytecodeLength > 0) {
            dsDecision = policy.EvaluateShader(pDesc->DS.pShaderBytecode, pDesc->DS.BytecodeLength, ShaderStage::Domain);
            LOG_INFO("[PSO] DS: " + DxilInspector::ContainerTypeToString(dsDecision.inspection.containerType) +
                     " | " + std::to_string(pDesc->DS.BytecodeLength) + " bytes | " + dsDecision.actionReason);
            if (dsDecision.fallbackApplied) anyFallbackApplied = true;
        }

        // Hull Shader
        if (pDesc->HS.pShaderBytecode && pDesc->HS.BytecodeLength > 0) {
            hsDecision = policy.EvaluateShader(pDesc->HS.pShaderBytecode, pDesc->HS.BytecodeLength, ShaderStage::Hull);
            LOG_INFO("[PSO] HS: " + DxilInspector::ContainerTypeToString(hsDecision.inspection.containerType) +
                     " | " + std::to_string(pDesc->HS.BytecodeLength) + " bytes | " + hsDecision.actionReason);
            if (hsDecision.fallbackApplied) anyFallbackApplied = true;
        }

        // Geometry Shader
        if (pDesc->GS.pShaderBytecode && pDesc->GS.BytecodeLength > 0) {
            gsDecision = policy.EvaluateShader(pDesc->GS.pShaderBytecode, pDesc->GS.BytecodeLength, ShaderStage::Geometry);
            LOG_INFO("[PSO] GS: " + DxilInspector::ContainerTypeToString(gsDecision.inspection.containerType) +
                     " | " + std::to_string(pDesc->GS.BytecodeLength) + " bytes | " + gsDecision.actionReason);
            if (gsDecision.fallbackApplied) anyFallbackApplied = true;
        }
    }

    HRESULT hr = E_FAIL;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC modifiedDesc = {};

    if (anyFallbackApplied) {
        modifiedDesc = *pDesc;
        if (vsDecision.fallbackApplied) {
            modifiedDesc.VS.pShaderBytecode = vsDecision.effectiveBytecode;
            modifiedDesc.VS.BytecodeLength = vsDecision.effectiveLength;
        }
        if (psDecision.fallbackApplied) {
            modifiedDesc.PS.pShaderBytecode = psDecision.effectiveBytecode;
            modifiedDesc.PS.BytecodeLength = psDecision.effectiveLength;
        }
        if (dsDecision.fallbackApplied) {
            modifiedDesc.DS.pShaderBytecode = dsDecision.effectiveBytecode;
            modifiedDesc.DS.BytecodeLength = dsDecision.effectiveLength;
        }
        if (hsDecision.fallbackApplied) {
            modifiedDesc.HS.pShaderBytecode = hsDecision.effectiveBytecode;
            modifiedDesc.HS.BytecodeLength = hsDecision.effectiveLength;
        }
        if (gsDecision.fallbackApplied) {
            modifiedDesc.GS.pShaderBytecode = gsDecision.effectiveBytecode;
            modifiedDesc.GS.BytecodeLength = gsDecision.effectiveLength;
        }

        hr = pRealDevice->CreateGraphicsPipelineState(&modifiedDesc, riid, ppPipelineState);
    } else {
        // Unmodified native passthrough
        hr = pRealDevice->CreateGraphicsPipelineState(pDesc, riid, ppPipelineState);
    }

    if (SUCCEEDED(hr) && ppPipelineState && *ppPipelineState) {
        if (Config::Instance().IsD3D11TranslationEnabled()) {
            if (!D3D11Backend::Instance().IsInitialized()) {
                IDXGIAdapter* pAdapter = D3D11Backend::GetMatchingAdapter(pRealDevice);
                D3D11Backend::Instance().Initialize(pAdapter);
                if (pAdapter) pAdapter->Release();
            }
            if (D3D11Backend::Instance().IsInitialized()) {
                const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pEffDesc = anyFallbackApplied ? &modifiedDesc : pDesc;
                TranslatedPipelineState translatedPSO = {};
                if (SUCCEEDED(D3D11Backend::Instance().CreateTranslatedPSO(pEffDesc, translatedPSO))) {
                    ResourceManager::Instance().RegisterPSO(reinterpret_cast<ID3D12PipelineState*>(*ppPipelineState), translatedPSO);
                    LOG_HOOK("[PSO] Successfully registered translated D3D11 PSO.");
                }
            }
        } else {
            D3D11Backend::Instance().NotifyPsoCreated();
        }
    }

    if (inspectionEnabled) {
        LOG_INFO("[PSO] Real D3D12CreateGraphicsPipelineState returned: " + Logger::HResultToString(hr));
        if (FAILED(hr)) {
            LOG_WARN("[PSO] Native pipeline creation failed (HRESULT: " + Logger::HResultToString(hr) + ")");
            DiagnosticReport rep;
            bool isDXIL = (vsDecision.inspection.containerType == ShaderContainerType::DXIL || psDecision.inspection.containerType == ShaderContainerType::DXIL);
            rep.errorCode = isDXIL ? KeplerErrorCode::UnsupportedShaderInstruction : KeplerErrorCode::ShaderCompilationFailed;
            rep.severity = ErrorSeverity::Error;
            rep.subsystem = "PSOHook";
            rep.apiOperation = "CreateGraphicsPipelineState";
            rep.nativeHResult = hr;
            rep.disposition = ExecutionDisposition::Rejected;
            rep.shaderStage = "Graphics (VS+PS)";
            rep.shaderProfile = "VS:" + DxilInspector::ShaderModelToString(vsDecision.inspection.majorVersion, vsDecision.inspection.minorVersion) +
                                ", PS:" + DxilInspector::ShaderModelToString(psDecision.inspection.majorVersion, psDecision.inspection.minorVersion);
            rep.policyResult = "VS:" + vsDecision.actionReason + " | PS:" + psDecision.actionReason;
            rep.resourceInfo = "Bytecode: VS=" + std::to_string(pDesc ? pDesc->VS.BytecodeLength : 0) +
                               "B, PS=" + std::to_string(pDesc ? pDesc->PS.BytecodeLength : 0) + "B";
            rep.explanation = "Graphics pipeline creation failed on native Kepler driver.";
            rep.actionableHint = isDXIL ?
                "Shader contains Shader Model 6.x DXIL bytecode. Force -dx11 mode or add SM 5.0 fallback shaders." :
                "Ensure blend/depth-stencil states and root signatures match D3D11 Feature Level 11_0 constraints.";
            LOG_DIAGNOSTIC_REPORT(rep);
        } else {
            LOG_INFO("[PSO] Graphics pipeline state created successfully.");
        }
        LOG_INFO("[PSO] ================================================");
    }

    return hr;
}

HRESULT PSOHook::InterceptCreateComputePipelineState(
    ID3D12Device* pRealDevice,
    const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc,
    REFIID riid,
    void** ppPipelineState)
{
    if (!pRealDevice) return E_FAIL;
    if (!pDesc) return pRealDevice->CreateComputePipelineState(pDesc, riid, ppPipelineState);

    bool inspectionEnabled = Config::Instance().IsShaderInspectionEnabled();
    auto& policy = ShaderPolicy::Instance();

    ShaderPolicyDecision csDecision;
    bool fallbackApplied = false;

    if (inspectionEnabled) {
        LOG_INFO("[PSO] === Intercepted CreateComputePipelineState ===");
        if (pDesc->CS.pShaderBytecode && pDesc->CS.BytecodeLength > 0) {
            csDecision = policy.EvaluateShader(pDesc->CS.pShaderBytecode, pDesc->CS.BytecodeLength, ShaderStage::Compute);
            LOG_INFO("[PSO] CS: " + DxilInspector::ContainerTypeToString(csDecision.inspection.containerType) +
                     " | " + DxilInspector::ShaderModelToString(csDecision.inspection.majorVersion, csDecision.inspection.minorVersion) +
                     " | " + std::to_string(pDesc->CS.BytecodeLength) + " bytes | " + csDecision.actionReason);
            if (csDecision.fallbackApplied) fallbackApplied = true;
        }
    }

    HRESULT hr = E_FAIL;
    D3D12_COMPUTE_PIPELINE_STATE_DESC modifiedDesc = pDesc ? *pDesc : D3D12_COMPUTE_PIPELINE_STATE_DESC{};

    if (fallbackApplied) {
        modifiedDesc.CS.pShaderBytecode = csDecision.effectiveBytecode;
        modifiedDesc.CS.BytecodeLength = csDecision.effectiveLength;
        hr = pRealDevice->CreateComputePipelineState(&modifiedDesc, riid, ppPipelineState);
    } else {
        hr = pRealDevice->CreateComputePipelineState(pDesc, riid, ppPipelineState);
    }

    if (SUCCEEDED(hr) && ppPipelineState && *ppPipelineState) {
        if (Config::Instance().IsD3D11TranslationEnabled()) {
            if (!D3D11Backend::Instance().IsInitialized()) {
                IDXGIAdapter* pAdapter = D3D11Backend::GetMatchingAdapter(pRealDevice);
                D3D11Backend::Instance().Initialize(pAdapter);
                if (pAdapter) pAdapter->Release();
            }
            if (D3D11Backend::Instance().IsInitialized()) {
                const D3D12_COMPUTE_PIPELINE_STATE_DESC* pEffDesc = fallbackApplied ? &modifiedDesc : pDesc;
                TranslatedPipelineState translatedPSO = {};
                if (SUCCEEDED(D3D11Backend::Instance().CreateTranslatedComputePSO(pEffDesc, translatedPSO))) {
                    ResourceManager::Instance().RegisterPSO(reinterpret_cast<ID3D12PipelineState*>(*ppPipelineState), translatedPSO);
                    LOG_HOOK("[PSO] Successfully registered translated D3D11 Compute PSO.");
                }
            }
        } else {
            D3D11Backend::Instance().NotifyPsoCreated();
        }
    }

    if (inspectionEnabled) {
        LOG_INFO("[PSO] Real D3D12CreateComputePipelineState returned: " + Logger::HResultToString(hr));
        if (FAILED(hr)) {
            LOG_WARN("[PSO] Native compute pipeline creation failed (HRESULT: " + Logger::HResultToString(hr) + ")");
            DiagnosticReport rep;
            rep.errorCode = (csDecision.inspection.containerType == ShaderContainerType::DXIL) ?
                KeplerErrorCode::UnsupportedShaderInstruction : KeplerErrorCode::ShaderCompilationFailed;
            rep.severity = ErrorSeverity::Error;
            rep.subsystem = "PSOHook";
            rep.apiOperation = "CreateComputePipelineState";
            rep.nativeHResult = hr;
            rep.disposition = ExecutionDisposition::Rejected;
            rep.shaderStage = "Compute";
            rep.shaderProfile = DxilInspector::ShaderModelToString(csDecision.inspection.majorVersion, csDecision.inspection.minorVersion);
            rep.policyResult = csDecision.actionReason;
            rep.resourceInfo = "BytecodeLength: " + std::to_string(pDesc ? pDesc->CS.BytecodeLength : 0);
            rep.explanation = "Compute pipeline creation failed on native driver. " + csDecision.actionReason;
            rep.actionableHint = (csDecision.inspection.containerType == ShaderContainerType::DXIL) ?
                "Target shader uses Shader Model 6.x DXIL which requires translation or forcing -dx11 engine mode." :
                "Check shader syntax, root signature compatibility, or resource binding tier limits.";
            LOG_DIAGNOSTIC_REPORT(rep);
        } else {
            LOG_INFO("[PSO] Compute pipeline state created successfully.");
        }
        LOG_INFO("[PSO] ===============================================");
    }

    return hr;
}

} // namespace kepler
