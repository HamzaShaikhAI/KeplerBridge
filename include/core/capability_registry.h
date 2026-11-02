#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3d11.h>
#include <dxgi.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace kepler {

enum class CapabilityStatus {
    Native,        // Hardware/driver natively supports this feature without bridge translation
    Translated,    // Bridge translates instruction sequence to D3D11 FL 11_0 / SM 5.0
    Emulated,      // Bridge emulates behavior via compute/LDS or software virtualization
    Unsupported,   // Hardware and bridge cannot support; explicitly rejected with diagnostic
    Unverified     // Present in API specification but not verified under live hardware tests
};

enum class FeatureMaturity {
    Stable,        // implemented and sufficiently validated for the explicitly supported subset
    Experimental,  // implemented, but restricted or insufficiently validated for general use
    Unsupported,   // detected and deterministically rejected or safely handled
    Planned        // not implemented
};

enum class CapabilityID {
    FeatureLevel_11_0,
    FeatureLevel_11_1,
    FeatureLevel_12_0,
    FeatureLevel_12_1,
    FeatureLevel_12_2,
    ShaderModel_5_1_Lowering,
    ShaderModel_6_0_DXIL,
    WaveOperations_Compute,
    WaveOperations_Graphics,
    Int64_ALU_Lowering,
    Int64_Atomics,
    MeshShaders_BoundedCompute,
    MeshShaders_General,
    Raytracing_Tier1_0,
    Raytracing_Tier1_1,
    SamplerFeedback,
    RasterizerOrderedViews,
    DescriptorVirtualization_Bounded,
    DescriptorVirtualization_Unbounded,
    FSR1_Upscaling_SDR,
    FSR1_Upscaling_HDR,
    App_3DMarkNightRaid,
    App_Palworld,
    AsyncCompute_Queues
};

// Shader stage bitmask
enum ShaderStageMask : uint32_t {
    STAGE_NONE     = 0,
    STAGE_VERTEX   = 1 << 0,
    STAGE_PIXEL    = 1 << 1,
    STAGE_GEOMETRY = 1 << 2,
    STAGE_HULL     = 1 << 3,
    STAGE_DOMAIN   = 1 << 4,
    STAGE_COMPUTE  = 1 << 5,
    STAGE_ALL_GRAPHICS = (STAGE_VERTEX | STAGE_PIXEL | STAGE_GEOMETRY | STAGE_HULL | STAGE_DOMAIN),
    STAGE_ALL      = (STAGE_ALL_GRAPHICS | STAGE_COMPUTE)
};

struct CapabilityRecord {
    CapabilityID id;
    std::string name;
    CapabilityStatus status;
    std::string requiredConditions;
    std::string implementationRoute;
    std::string backendPrerequisites;
    uint32_t supportedStagesMask = STAGE_NONE;
    std::string failureReason;
    FeatureMaturity maturity = FeatureMaturity::Unsupported;
    std::string testEvidence;
    std::string knownLimitations;
};

class CapabilityRegistry {
public:
    static CapabilityRegistry& Instance();

    void InitializeDefaultPolicies();

    // Live probe against adapter, D3D12 device, and/or D3D11 device
    void ProbeCapabilities(IUnknown* pAdapter, ID3D12Device* pD3D12Device, ID3D11Device* pD3D11Device);

    CapabilityStatus GetStatus(CapabilityID id) const;
    FeatureMaturity GetMaturity(CapabilityID id) const;
    const CapabilityRecord* GetRecord(CapabilityID id) const;
    std::vector<CapabilityRecord> GetAllRecords() const;

    bool IsSupported(CapabilityID id) const {
        CapabilityStatus s = GetStatus(id);
        return s == CapabilityStatus::Native || s == CapabilityStatus::Translated || s == CapabilityStatus::Emulated;
    }

    bool IsStageSupported(CapabilityID id, ShaderStageMask stage) const;

    void SetManualOverride(CapabilityID id, CapabilityStatus status, const std::string& reason = "");

    std::string StatusToString(CapabilityStatus status) const;
    std::string MaturityToString(FeatureMaturity maturity) const;
    std::string CapabilityIDToString(CapabilityID id) const;
    std::string ToJson() const;

    // Direct query helpers
    D3D_FEATURE_LEVEL GetNativeMaxFeatureLevel() const { return m_nativeMaxFeatureLevel; }
    D3D_FEATURE_LEVEL GetBackendD3D11FeatureLevel() const { return m_d3d11FeatureLevel; }
    bool HasNativeWaveOps() const { return m_hasNativeWaveOps; }
    bool HasNativeInt64() const { return m_hasNativeInt64; }

private:
    CapabilityRegistry();
    ~CapabilityRegistry() = default;
    CapabilityRegistry(const CapabilityRegistry&) = delete;
    CapabilityRegistry& operator=(const CapabilityRegistry&) = delete;

    mutable std::mutex m_mutex;
    std::unordered_map<CapabilityID, CapabilityRecord> m_records;

    D3D_FEATURE_LEVEL m_nativeMaxFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL m_d3d11FeatureLevel = D3D_FEATURE_LEVEL_11_0;
    bool m_hasNativeWaveOps = false;
    bool m_hasNativeInt64 = false;
    bool m_probed = false;
};

} // namespace kepler
