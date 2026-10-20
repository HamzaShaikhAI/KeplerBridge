#pragma once

#include "downgrader/shader_policy.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <unordered_map>

namespace kepler {

/**
 * @brief Bounded Shader Translator for NVIDIA Kepler Architecture.
 *
 * Provides compiler-backed translation and strict capability boundary enforcement
 * for Direct3D 12 workloads targeting Kepler GK208 hardware (Feature Level 11_0, SM 5.0).
 *
 * Boundaries:
 * - Supported Stages: Vertex, Pixel, Compute
 * - Unsupported Stages: Geometry, Hull, Domain, Mesh, Amplification, Raytracing
 * - Strictly Rejected Features: Wave Intrinsics, Raytracing pipelines, Mesh shading, >8 UAVs
 * - Target Binary Format: Genuine, cryptographically valid DXBC Shader Model 5.0
 */
class BoundedShaderTranslator : public IShaderTranslator {
public:
    BoundedShaderTranslator();
    virtual ~BoundedShaderTranslator();

    const char* GetName() const override { return "KeplerBoundedShaderTranslator"; }

    bool CanTranslate(const ShaderInspectionResult& info) const override;

    bool Translate(
        const void* srcBytecode,
        size_t srcLength,
        const ShaderInspectionResult& info,
        std::vector<uint8_t>& outBytecode,
        std::string& outErrorMessage) override;

    // Compiler-backed compilation helper producing verified DXBC SM 5.0
    bool CompileHLSLToDXBC(
        const std::string& hlslSource,
        const std::string& entryPoint,
        const std::string& targetProfile,
        std::vector<uint8_t>& outBytecode,
        std::string& outErrorMessage);

    // Register a known HLSL template or fallback source for a specific stage
    void RegisterStageFallbackSource(ShaderStage stage, const std::string& hlslSource, const std::string& entryPoint);

    bool IsCompilerAvailable() const { return m_pfnCompile != nullptr; }

    // Telemetry
    uint64_t GetCompileCount() const { return m_compileCount.load(); }
    uint64_t GetCacheBypassCount() const { return m_cacheBypassCount.load(); }
    uint64_t GetTotalCompileTimeUs() const { return m_totalCompileTimeUs.load(); }
    uint64_t GetLastCompileTimeUs() const { return m_lastCompileTimeUs.load(); }
    void ResetStats() {
        m_compileCount.store(0);
        m_cacheBypassCount.store(0);
        m_totalCompileTimeUs.store(0);
        m_lastCompileTimeUs.store(0);
    }

private:
    HMODULE m_hCompiler = nullptr;
    pD3DCompile m_pfnCompile = nullptr;

    std::atomic<uint64_t> m_compileCount{ 0 };
    std::atomic<uint64_t> m_cacheBypassCount{ 0 };
    std::atomic<uint64_t> m_totalCompileTimeUs{ 0 };
    std::atomic<uint64_t> m_lastCompileTimeUs{ 0 };

    mutable std::mutex m_mutex;
    struct FallbackSource {
        std::string source;
        std::string entryPoint;
    };
    std::unordered_map<ShaderStage, FallbackSource> m_fallbackSources;

    void LoadCompiler();
    void UnloadCompiler();
};

} // namespace kepler
