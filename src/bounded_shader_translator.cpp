#include "downgrader/bounded_shader_translator.h"
#include "downgrader/shader_disk_cache.h"
#include "downgrader/sm51_lowering.h"
#include "downgrader/dxil_translation.h"
#include "core/config.h"
#include "core/logger.h"
#include "core/profile_manager.h"
#include <chrono>

namespace kepler {

BoundedShaderTranslator::BoundedShaderTranslator() {
    LoadCompiler();
}

BoundedShaderTranslator::~BoundedShaderTranslator() {
    UnloadCompiler();
}

void BoundedShaderTranslator::LoadCompiler() {
    if (m_hCompiler) return;

    // Load d3dcompiler_47.dll for authentic, compiler-backed DXBC SM 5.0 generation
    m_hCompiler = LoadLibraryA("d3dcompiler_47.dll");
    if (m_hCompiler) {
        m_pfnCompile = reinterpret_cast<pD3DCompile>(GetProcAddress(m_hCompiler, "D3DCompile"));
        if (m_pfnCompile) {
            LOG_INFO("[BoundedShaderTranslator] Successfully bound D3DCompile from d3dcompiler_47.dll.");
        } else {
            LOG_WARN("[BoundedShaderTranslator] Found d3dcompiler_47.dll but failed to locate D3DCompile entry point.");
            FreeLibrary(m_hCompiler);
            m_hCompiler = nullptr;
        }
    } else {
        LOG_WARN("[BoundedShaderTranslator] Failed to load d3dcompiler_47.dll. Runtime shader compilation unavailable.");
    }
}

void BoundedShaderTranslator::UnloadCompiler() {
    if (m_hCompiler) {
        FreeLibrary(m_hCompiler);
        m_hCompiler = nullptr;
    }
    m_pfnCompile = nullptr;
}

void BoundedShaderTranslator::RegisterStageFallbackSource(
    ShaderStage stage,
    const std::string& hlslSource,
    const std::string& entryPoint)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fallbackSources[stage] = { hlslSource, entryPoint };
}

bool BoundedShaderTranslator::CanTranslate(const ShaderInspectionResult& info) const {
    // 1. Strict Stage Boundary Check: Only Vertex, Pixel, and Compute are supported on Kepler D3D11 backend
    if (info.stage != ShaderStage::Vertex &&
        info.stage != ShaderStage::Pixel &&
        info.stage != ShaderStage::Compute)
    {
        return false;
    }

    // 2. Reject Raytracing and Mesh/Amplification shaders
    if (info.stage == ShaderStage::Mesh ||
        info.stage == ShaderStage::Amplification ||
        info.stage == ShaderStage::Library ||
        info.stage == ShaderStage::RayGeneration ||
        info.stage == ShaderStage::Intersection ||
        info.stage == ShaderStage::AnyHit ||
        info.stage == ShaderStage::ClosestHit ||
        info.stage == ShaderStage::Miss ||
        info.stage == ShaderStage::Callable)
    {
        return false;
    }

    // 3. Direct bytecode lowering for SM 5.1 DXBC shaders
    if (info.containerType == ShaderContainerType::LegacyDXBC &&
        info.majorVersion == 5 && info.minorVersion == 1)
    {
        return true;
    }

    // 4. Direct bytecode lowering for DXIL SM 6.x shaders
    if (info.hasDXILChunk || info.containerType == ShaderContainerType::DXIL || info.majorVersion >= 6)
    {
        return true;
    }

    // 4. Check if compiler is available and a fallback or bounded translation path exists
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_pfnCompile) {
        return false;
    }

    auto it = m_fallbackSources.find(info.stage);
    if (it != m_fallbackSources.end() && !it->second.source.empty()) {
        return true;
    }

    return false;
}

bool BoundedShaderTranslator::CompileHLSLToDXBC(
    const std::string& hlslSource,
    const std::string& entryPoint,
    const std::string& targetProfile,
    std::vector<uint8_t>& outBytecode,
    std::string& outErrorMessage)
{
    outBytecode.clear();
    outErrorMessage.clear();

    const uint32_t compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

    // Check disk cache first if enabled
    uint64_t shaderKey = 0;
    if (Config::Instance().IsShaderDiskCacheEnabled()) {
        shaderKey = ShaderDiskCache::ComputeShaderKey(hlslSource, entryPoint, targetProfile, compileFlags);
        if (ShaderDiskCache::Instance().TryGet(shaderKey, compileFlags, outBytecode)) {
            m_cacheBypassCount.fetch_add(1);
            RuntimeTelemetry::Instance().RecordPSOCompilation(true, 0.0);
            LOG_INFO("[BoundedShaderTranslator] Cache HIT for shader [" + entryPoint + ", " + targetProfile +
                     "] (key: 0x" + Logger::HexToString(shaderKey) + ", " +
                     std::to_string(outBytecode.size()) + " bytes). Compilation bypassed.");
            return true;
        }
    }

    if (!m_pfnCompile) {
        outErrorMessage = "d3dcompiler_47.dll is not loaded.";
        return false;
    }

    ID3DBlob* pCode = nullptr;
    ID3DBlob* pErrors = nullptr;

    auto tStart = std::chrono::high_resolution_clock::now();
    HRESULT hr = m_pfnCompile(
        hlslSource.c_str(),
        hlslSource.length(),
        "KeplerShader",
        nullptr,
        nullptr,
        entryPoint.c_str(),
        targetProfile.c_str(),
        compileFlags,
        0,
        &pCode,
        &pErrors);
    uint64_t elapsedUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - tStart).count());
    m_lastCompileTimeUs.store(elapsedUs);
    m_totalCompileTimeUs.fetch_add(elapsedUs);
    m_compileCount.fetch_add(1);

    if (FAILED(hr) || !pCode) {
        if (pErrors) {
            outErrorMessage = static_cast<const char*>(pErrors->GetBufferPointer());
            pErrors->Release();
        } else {
            outErrorMessage = "Compilation failed with HRESULT 0x" + Logger::HResultToString(hr);
        }
        return false;
    }

    if (pErrors) {
        pErrors->Release();
    }

    const uint8_t* pData = static_cast<const uint8_t*>(pCode->GetBufferPointer());
    size_t size = pCode->GetBufferSize();
    outBytecode.assign(pData, pData + size);
    pCode->Release();
    RuntimeTelemetry::Instance().RecordPSOCompilation(false, 0.0);

    LOG_INFO("[BoundedShaderTranslator] Compiled HLSL (" + entryPoint + ", " + targetProfile +
             ") -> " + std::to_string(size) + " bytes authentic DXBC SM 5.0.");

    // Store to disk cache if enabled
    if (Config::Instance().IsShaderDiskCacheEnabled()) {
        if (shaderKey == 0) {
            shaderKey = ShaderDiskCache::ComputeShaderKey(hlslSource, entryPoint, targetProfile, compileFlags);
        }
        if (ShaderDiskCache::Instance().Put(shaderKey, compileFlags, outBytecode.data(), outBytecode.size())) {
            LOG_INFO("[BoundedShaderTranslator] Stored compiled shader in disk cache [" + entryPoint + ", " +
                     targetProfile + "] (key: 0x" + Logger::HexToString(shaderKey) + ").");
        }
    }

    return true;
}

bool BoundedShaderTranslator::Translate(
    const void* srcBytecode,
    size_t srcLength,
    const ShaderInspectionResult& info,
    std::vector<uint8_t>& outBytecode,
    std::string& outErrorMessage)
{
    outBytecode.clear();
    outErrorMessage.clear();

    // 1. Stage verification
    if (!CanTranslate(info)) {
        outErrorMessage = "Shader stage " + DxilInspector::StageToString(info.stage) +
                          " is outside KeplerBridge supported translation boundary.";
        return false;
    }

    // 2. Direct bytecode lowering for SM 5.1 DXBC shaders
    if (info.containerType == ShaderContainerType::LegacyDXBC &&
        info.majorVersion == 5 && info.minorVersion == 1)
    {
        SM51LoweringReport rep;
        if (SM51LoweringEngine::LowerSM51ToSM50(srcBytecode, srcLength, outBytecode, &rep)) {
            LOG_INFO("[BoundedShaderTranslator] Successfully lowered SM 5.1 shader (" +
                     std::to_string(srcLength) + "B -> " + std::to_string(outBytecode.size()) +
                     "B authentic SM 5.0 DXBC). SRVs=" + std::to_string(rep.declaredSRVs) +
                     ", UAVs=" + std::to_string(rep.declaredUAVs) +
                     ", CBVs=" + std::to_string(rep.declaredCBVs) +
                     ", Samplers=" + std::to_string(rep.declaredSamplers));
            return true;
        } else {
            outErrorMessage = "SM 5.1 Lowering failed: " + rep.diagnostic + " (" +
                              SM51LoweringEngine::StatusToString(rep.status) + ")";
            LOG_WARN("[BoundedShaderTranslator] " + outErrorMessage);
            return false;
        }
    }

    // 3. Direct bytecode lowering for DXIL SM 6.x shaders
    std::string directDxilError;
    if (info.hasDXILChunk || info.containerType == ShaderContainerType::DXIL || info.majorVersion >= 6)
    {
        RuntimeTelemetry::Instance().RecordDxilShaderEncountered();
        DXILTranslationReport rep;
        if (DXILTranslationEngine::TranslateDXILToSM50(srcBytecode, srcLength, outBytecode, &rep)) {
            RuntimeTelemetry::Instance().RecordDxilShaderTranslated();
            LOG_INFO("[BoundedShaderTranslator] Successfully translated DXIL SM 6.x shader (" +
                     std::to_string(srcLength) + "B -> " + std::to_string(outBytecode.size()) +
                     "B authentic SM 5.0 DXBC). Inputs=" + std::to_string(rep.declaredInputs) +
                     ", Outputs=" + std::to_string(rep.declaredOutputs) +
                     ", CBVs=" + std::to_string(rep.declaredCBVs) +
                     ", SRVs=" + std::to_string(rep.declaredSRVs) +
                     ", Samplers=" + std::to_string(rep.declaredSamplers));
            return true;
        } else {
            RuntimeTelemetry::Instance().RecordDxilShaderRejected();
            directDxilError = "DXIL Translation failed: " + rep.diagnostic + " (" +
                              DXILTranslationEngine::StatusToString(rep.status) + ")";
            LOG_WARN("[BoundedShaderTranslator] " + directDxilError);
        }
    }

    // 4. Select appropriate SM 5.0 profile
    std::string targetProfile;
    switch (info.stage) {
        case ShaderStage::Vertex:  targetProfile = "vs_5_0"; break;
        case ShaderStage::Pixel:   targetProfile = "ps_5_0"; break;
        case ShaderStage::Compute: targetProfile = "cs_5_0"; break;
        default:
            outErrorMessage = "Unsupported target shader stage.";
            return false;
    }

    // 5. Check for registered stage fallback source
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_fallbackSources.find(info.stage);
    if (it != m_fallbackSources.end() && !it->second.source.empty()) {
        return CompileHLSLToDXBC(it->second.source, it->second.entryPoint, targetProfile, outBytecode, outErrorMessage);
    }

    if (!directDxilError.empty()) {
        outErrorMessage = directDxilError;
    } else {
        outErrorMessage = "No validated translation path exists for this shader bytecode. Preserving native rejection.";
    }
    return false;
}

} // namespace kepler
