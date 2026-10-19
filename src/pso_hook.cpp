#include "downgrader/pso_hook.h"
#include "downgrader/shader_policy.h"
#include "core/config.h"
#include "core/logger.h"
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

    if (anyFallbackApplied) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC modifiedDesc = *pDesc;
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

    if (inspectionEnabled) {
        LOG_INFO("[PSO] Real D3D12CreateGraphicsPipelineState returned: " + Logger::HResultToString(hr));
        if (FAILED(hr)) {
            LOG_WARN("[PSO] Native pipeline creation failed (HRESULT: " + Logger::HResultToString(hr) + ")");
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

    if (fallbackApplied) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC modifiedDesc = *pDesc;
        modifiedDesc.CS.pShaderBytecode = csDecision.effectiveBytecode;
        modifiedDesc.CS.BytecodeLength = csDecision.effectiveLength;
        hr = pRealDevice->CreateComputePipelineState(&modifiedDesc, riid, ppPipelineState);
    } else {
        hr = pRealDevice->CreateComputePipelineState(pDesc, riid, ppPipelineState);
    }

    if (inspectionEnabled) {
        LOG_INFO("[PSO] Real D3D12CreateComputePipelineState returned: " + Logger::HResultToString(hr));
        if (FAILED(hr)) {
            LOG_WARN("[PSO] Native compute pipeline creation failed (HRESULT: " + Logger::HResultToString(hr) + ")");
        } else {
            LOG_INFO("[PSO] Compute pipeline state created successfully.");
        }
        LOG_INFO("[PSO] ===============================================");
    }

    return hr;
}

} // namespace kepler
