#include "core/capability_registry.h"
#include "core/logger.h"
#include <sstream>
#include <iomanip>

namespace kepler {

CapabilityRegistry& CapabilityRegistry::Instance() {
    static CapabilityRegistry s_instance;
    return s_instance;
}

CapabilityRegistry::CapabilityRegistry() {
    InitializeDefaultPolicies();
}

void CapabilityRegistry::InitializeDefaultPolicies() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_records.clear();

    m_records[CapabilityID::FeatureLevel_11_0] = {
        CapabilityID::FeatureLevel_11_0,
        "FeatureLevel_11_0",
        CapabilityStatus::Native,
        "D3D12 driver support for FL 11_0",
        "Direct D3D12 Hardware Execution / D3D11 FL 11_0 Core",
        "D3D11 FL 11_0 Device",
        STAGE_ALL,
        "",
        FeatureMaturity::Stable,
        "Physical GK107 D3D11 hardware probe (test_d3d11_standalone)",
        "None for FL 11_0 baseline"
    };

    m_records[CapabilityID::FeatureLevel_11_1] = {
        CapabilityID::FeatureLevel_11_1,
        "FeatureLevel_11_1",
        CapabilityStatus::Unverified,
        "Hardware FL 11_1 support",
        "Driver pass-through if available",
        "D3D11 FL 11_1 Device",
        STAGE_ALL,
        "",
        FeatureMaturity::Experimental,
        "Probed conditionally via CheckFeatureSupport",
        "Driver-dependent hardware feature level"
    };

    m_records[CapabilityID::FeatureLevel_12_0] = {
        CapabilityID::FeatureLevel_12_0,
        "FeatureLevel_12_0",
        CapabilityStatus::Translated,
        "KeplerBridge Translation Wrapper",
        "D3D12-to-D3D11 Command & PSO Translation",
        "D3D11 FL 11_0 Device",
        STAGE_ALL,
        "Physically unsupported natively on GK107 (FL 11_0 max)",
        FeatureMaturity::Experimental,
        "WrappedD3D12Device FL 12_0 spoofing + live rendering readback",
        "Kepler GK107 physical maximum is FL 11_0 (0xb000). FL 12_0 is virtualized; requires translated D3D11 command execution."
    };

    m_records[CapabilityID::FeatureLevel_12_1] = {
        CapabilityID::FeatureLevel_12_1,
        "FeatureLevel_12_1",
        CapabilityStatus::Unsupported,
        "Conservative Rasterization Tier 1, ROVs",
        "None",
        "None",
        STAGE_NONE,
        "Hardware lacks Tier 2 Tiled Resources and Rasterizer Ordered Views",
        FeatureMaturity::Unsupported,
        "test_capability_registry / unified_policy rejection check",
        "Physically impossible on Kepler architecture"
    };

    m_records[CapabilityID::FeatureLevel_12_2] = {
        CapabilityID::FeatureLevel_12_2,
        "FeatureLevel_12_2",
        CapabilityStatus::Unsupported,
        "DirectX 12 Ultimate (Raytracing, Mesh Shaders, Sampler Feedback, VRS)",
        "None",
        "None",
        STAGE_NONE,
        "Kepler architecture lacks hardware Raytracing, VRS, and Sampler Feedback",
        FeatureMaturity::Unsupported,
        "test_capability_registry rejection check",
        "Physically impossible on Kepler architecture"
    };

    m_records[CapabilityID::ShaderModel_5_1_Lowering] = {
        CapabilityID::ShaderModel_5_1_Lowering,
        "ShaderModel_5_1_Lowering",
        CapabilityStatus::Translated,
        "DXBC SM 5.1 input container",
        "SM51Lowering Pipeline (Resource slot flattening & RDEF/ISGN re-emission)",
        "D3D11 SM 5.0 Compiler & Runtime",
        STAGE_ALL,
        "",
        FeatureMaturity::Stable,
        "test_sm51_lowering (41/41) + live rendering SM 5.1",
        "Descriptor arrays must be statically bounded within FL 11_0 slots"
    };

    m_records[CapabilityID::ShaderModel_6_0_DXIL] = {
        CapabilityID::ShaderModel_6_0_DXIL,
        "ShaderModel_6_0_DXIL",
        CapabilityStatus::Translated,
        "DXIL container with valid bitcode and supported arithmetic/texture ops",
        "Bounded DXIL-to-DXBC Lowering Engine",
        "D3D11 SM 5.0 Runtime",
        STAGE_ALL,
        "",
        FeatureMaturity::Stable,
        "test_dxil_translation (39/39) + E2E integration workloads",
        "Bounded to supported arithmetic, control flow, local memory, and loops"
    };

    m_records[CapabilityID::WaveOperations_Compute] = {
        CapabilityID::WaveOperations_Compute,
        "WaveOperations_Compute",
        CapabilityStatus::Emulated,
        "Compute Shader (CS 5.0) execution model with Threadgroup LDS",
        "GroupShared Memory Tree Reductions & Barrier Synchronization",
        "D3D11 Compute Shader CS 5.0 with 32KB Threadgroup Memory",
        STAGE_COMPUTE,
        "",
        FeatureMaturity::Experimental,
        "test_wave_emulation live GPU reduction proof",
        "Compute stage only; threadgroup LDS tree reduction"
    };

    m_records[CapabilityID::WaveOperations_Graphics] = {
        CapabilityID::WaveOperations_Graphics,
        "WaveOperations_Graphics",
        CapabilityStatus::Unsupported,
        "Native Hardware SIMD Cross-Lane Communication in Vertex/Pixel units",
        "None",
        "None",
        STAGE_NONE,
        "GK107 Kepler hardware physically lacks cross-lane wave instructions in graphics stages",
        FeatureMaturity::Unsupported,
        "test_dxil_translation Test 2 authentic rejection",
        "Cross-lane hardware communication unavailable in graphics units"
    };

    m_records[CapabilityID::Int64_ALU_Lowering] = {
        CapabilityID::Int64_ALU_Lowering,
        "Int64_ALU_Lowering",
        CapabilityStatus::Translated,
        "64-bit integer ALU instructions (add, sub, bitwise, shift, mul)",
        "32-bit Register Pair (Lo, Hi) Lowering with Carry/Borrow Propagation",
        "D3D11 SM 5.0 integer arithmetic (addc, subb)",
        STAGE_ALL,
        "",
        FeatureMaturity::Experimental,
        "test_int64_lowering live GPU verification",
        "ALU instructions only; 64-bit UAV atomics unsupported"
    };

    m_records[CapabilityID::Int64_Atomics] = {
        CapabilityID::Int64_Atomics,
        "Int64_Atomics",
        CapabilityStatus::Unsupported,
        "64-bit atomic operations on UAVs or Groupshared memory",
        "None",
        "None",
        STAGE_NONE,
        "Direct3D 11 FL 11_0 does not support 64-bit atomic memory instructions",
        FeatureMaturity::Unsupported,
        "test_unified_policy rejection verification",
        "Hardware and D3D11 FL 11_0 lack 64-bit atomic instructions"
    };

    m_records[CapabilityID::MeshShaders_BoundedCompute] = {
        CapabilityID::MeshShaders_BoundedCompute,
        "MeshShaders_BoundedCompute",
        CapabilityStatus::Emulated,
        "Bounded geometry mesh shaders with known static topology and vertex counts",
        "Compute Shader transformation writing intermediate VB/IB buffers + DrawIndexed",
        "D3D11 CS 5.0 + Input Assembler",
        STAGE_COMPUTE,
        "",
        FeatureMaturity::Experimental,
        "test_mesh_shader_compat verification",
        "Static topology only; dynamic amplification shaders unsupported"
    };

    m_records[CapabilityID::MeshShaders_General] = {
        CapabilityID::MeshShaders_General,
        "MeshShaders_General",
        CapabilityStatus::Unsupported,
        "Arbitrary mesh and amplification shader pipelines",
        "None",
        "None",
        STAGE_NONE,
        "General amplification shaders and dynamic primitive culling require hardware mesh units",
        FeatureMaturity::Unsupported,
        "test_mesh_shader_compat rejection verification",
        "Kepler architecture lacks hardware mesh pipeline"
    };

    m_records[CapabilityID::Raytracing_Tier1_0] = {
        CapabilityID::Raytracing_Tier1_0,
        "Raytracing_Tier1_0",
        CapabilityStatus::Unsupported,
        "DXR Tier 1.0 acceleration structures and ray pipelines",
        "None",
        "None",
        STAGE_NONE,
        "Kepler GK107 lacks hardware BVH traversal and ray dispatch pipeline",
        FeatureMaturity::Unsupported,
        "test_capability_registry rejection check",
        "No hardware raytracing cores or BVH traversal"
    };

    m_records[CapabilityID::Raytracing_Tier1_1] = {
        CapabilityID::Raytracing_Tier1_1,
        "Raytracing_Tier1_1",
        CapabilityStatus::Unsupported,
        "DXR Tier 1.1 inline raytracing (RayQuery)",
        "None",
        "None",
        STAGE_NONE,
        "Hardware lacks ray query instruction support",
        FeatureMaturity::Unsupported,
        "test_capability_registry rejection check",
        "Inline ray queries unsupported"
    };

    m_records[CapabilityID::SamplerFeedback] = {
        CapabilityID::SamplerFeedback,
        "SamplerFeedback",
        CapabilityStatus::Unsupported,
        "Sampler Feedback maps and feedback UAV write hardware",
        "None",
        "None",
        STAGE_NONE,
        "Kepler architecture lacks hardware texture sampler feedback",
        FeatureMaturity::Unsupported,
        "test_capability_registry rejection check",
        "Hardware feedback maps unavailable"
    };

    m_records[CapabilityID::RasterizerOrderedViews] = {
        CapabilityID::RasterizerOrderedViews,
        "RasterizerOrderedViews",
        CapabilityStatus::Unsupported,
        "Hardware Rasterizer Ordered Views (ROVs) for pixel synchronization",
        "None",
        "None",
        STAGE_NONE,
        "Kepler GK107 hardware lacks pixel shader UAV ordering",
        FeatureMaturity::Unsupported,
        "test_capability_registry rejection check",
        "Pixel shader ordered memory synchronization unavailable"
    };

    m_records[CapabilityID::DescriptorVirtualization_Bounded] = {
        CapabilityID::DescriptorVirtualization_Bounded,
        "DescriptorVirtualization_Bounded",
        CapabilityStatus::Translated,
        "Statically bounded descriptor ranges within D3D11 slot limits (128 SRV, 8 UAV, 14 CBV, 16 Sampler)",
        "Deterministic Root Parameter to D3D11 Slot Remapping Engine",
        "D3D11 FL 11_0 Device Context",
        STAGE_ALL,
        "",
        FeatureMaturity::Stable,
        "test_descriptor_virtualizer (10/10) + translation engine tests",
        "Bounded to 128 SRV, 8 UAV, 14 CBV, 16 Sampler"
    };

    m_records[CapabilityID::DescriptorVirtualization_Unbounded] = {
        CapabilityID::DescriptorVirtualization_Unbounded,
        "DescriptorVirtualization_Unbounded",
        CapabilityStatus::Unsupported,
        "Unbounded dynamic descriptor tables (ResourceDescriptorHeap[i])",
        "None",
        "None",
        STAGE_NONE,
        "D3D11 hardware slot architecture physically limits active bindings per stage",
        FeatureMaturity::Unsupported,
        "test_descriptor_virtualizer unbounded rejection check",
        "Unbounded dynamic indexing not supported by FL 11_0 driver"
    };

    m_records[CapabilityID::FSR1_Upscaling_SDR] = {
        CapabilityID::FSR1_Upscaling_SDR,
        "FSR1_Upscaling_SDR",
        CapabilityStatus::Emulated,
        "SDR DXGI SwapChain (DXGI_FORMAT_R8G8B8A8_UNORM / B8G8R8A8_UNORM) with enabled upscaler",
        "AMD FidelityFX Super Resolution 1.0 (EASU + RCAS CS 5.0 passes)",
        "D3D11 CS 5.0 Compute Pipeline",
        STAGE_COMPUTE,
        "",
        FeatureMaturity::Experimental,
        "test_fsr1_upscaler EASU+RCAS live compute verification",
        "SDR 8-bit swapchains only; compute shader overhead"
    };

    m_records[CapabilityID::FSR1_Upscaling_HDR] = {
        CapabilityID::FSR1_Upscaling_HDR,
        "FSR1_Upscaling_HDR",
        CapabilityStatus::Unsupported,
        "Wide color gamut / HDR10 SwapChain",
        "None",
        "None",
        STAGE_NONE,
        "KeplerBridge 0.5 currently restricts FSR 1 to validated SDR 8-bit presentation pipelines",
        FeatureMaturity::Unsupported,
        "test_fsr1_upscaler HDR gating check",
        "Restricted to SDR in 0.5 release"
    };

    m_records[CapabilityID::App_3DMarkNightRaid] = {
        CapabilityID::App_3DMarkNightRaid,
        "App_3DMarkNightRaid",
        CapabilityStatus::Translated,
        "DirectX 12 FL 11_0 (0xb000), Shader Model 5.1, Resource Binding Tier 1",
        "SM 5.1 lowering & D3D11 command translation (Historical native baseline verified)",
        "D3D11 FL 11_0 Device Context",
        STAGE_ALL,
        "",
        FeatureMaturity::Experimental,
        "Historical native run: Score 4,903 (GT 740, Driver 456.71); 3DMarkNightRaid.exe currently uninstalled",
        "Does NOT require DXR raytracing or wave intrinsics. Full translation not re-tested on current candidate"
    };

    m_records[CapabilityID::App_Palworld] = {
        CapabilityID::App_Palworld,
        "App_Palworld",
        CapabilityStatus::Unsupported,
        "DirectX 12 FL 12_0 (0xc000), Resource Binding Tier 3 (bindless heaps)",
        "None (UE5 native D3D11 RHI fallback)",
        "None",
        STAGE_NONE,
        "Hardware lacks Resource Binding Tier 3 bindless heaps; native driver 456.71 rejects FL 12_0 (0x887A0004)",
        FeatureMaturity::Unsupported,
        "Palworld runtime report (Phase 11A): fell back to UE5 native D3D11 RHI; never translated",
        "Title requires Tier 3 bindless descriptor tables; Nanite/Lumen requires D3D12 Tier 3"
    };

    m_records[CapabilityID::AsyncCompute_Queues] = {
        CapabilityID::AsyncCompute_Queues,
        "AsyncCompute_Queues",
        CapabilityStatus::Emulated,
        "D3D12 CommandQueue Type COMPUTE",
        "Serializing compute command batches onto D3D11 immediate context or deferred contexts",
        "D3D11 CS 5.0 Compute Pipeline",
        STAGE_COMPUTE,
        "",
        FeatureMaturity::Experimental,
        "Async compute queue creation and submission tests",
        "Serialized execution on D3D11 context; does not execute concurrent hardware async compute"
    };
}

void CapabilityRegistry::ProbeCapabilities(IUnknown* pAdapter, ID3D12Device* pD3D12Device, ID3D11Device* pD3D11Device) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (pD3D12Device) {
        // Query D3D12 Feature Levels
        static const D3D_FEATURE_LEVEL levelsToQuery[] = {
            D3D_FEATURE_LEVEL_12_2,
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0
        };

        D3D12_FEATURE_DATA_FEATURE_LEVELS flData{};
        flData.NumFeatureLevels = static_cast<UINT>(sizeof(levelsToQuery) / sizeof(levelsToQuery[0]));
        flData.pFeatureLevelsRequested = levelsToQuery;

        if (SUCCEEDED(pD3D12Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &flData, sizeof(flData)))) {
            m_nativeMaxFeatureLevel = flData.MaxSupportedFeatureLevel;
            LOG_INFO("CapabilityRegistry: Probed D3D12 native MaxSupportedFeatureLevel = 0x" + 
                     [&]() { std::stringstream ss; ss << std::hex << m_nativeMaxFeatureLevel; return ss.str(); }());
        }

        // Query WaveOps (OPTIONS1)
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 opt1{};
        if (SUCCEEDED(pD3D12Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &opt1, sizeof(opt1)))) {
            m_hasNativeWaveOps = (opt1.WaveOps != FALSE);
            LOG_INFO("CapabilityRegistry: Probed D3D12 WaveOps = " + std::string(m_hasNativeWaveOps ? "TRUE" : "FALSE"));
        } else {
            m_hasNativeWaveOps = false;
        }

        // Query Shader Model
        D3D12_FEATURE_DATA_SHADER_MODEL smData{};
        smData.HighestShaderModel = D3D_SHADER_MODEL_6_6;
        if (SUCCEEDED(pD3D12Device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &smData, sizeof(smData)))) {
            LOG_INFO("CapabilityRegistry: Probed D3D12 HighestShaderModel = 0x" + 
                     [&]() { std::stringstream ss; ss << std::hex << smData.HighestShaderModel; return ss.str(); }());
        }
    }

    if (pD3D11Device) {
        m_d3d11FeatureLevel = pD3D11Device->GetFeatureLevel();
        LOG_INFO("CapabilityRegistry: Probed D3D11 FeatureLevel = 0x" + 
                 [&]() { std::stringstream ss; ss << std::hex << m_d3d11FeatureLevel; return ss.str(); }());

        D3D11_FEATURE_DATA_DOUBLES doubles{};
        if (SUCCEEDED(pD3D11Device->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &doubles, sizeof(doubles)))) {
            m_hasNativeInt64 = (doubles.DoublePrecisionFloatShaderOps != FALSE);
        }
    }

    // Reconcile status based on real probe
    if (m_nativeMaxFeatureLevel >= D3D_FEATURE_LEVEL_11_1) {
        m_records[CapabilityID::FeatureLevel_11_1].status = CapabilityStatus::Native;
    }
    if (m_nativeMaxFeatureLevel >= D3D_FEATURE_LEVEL_12_0) {
        m_records[CapabilityID::FeatureLevel_12_0].status = CapabilityStatus::Native;
    }
    if (m_hasNativeWaveOps) {
        m_records[CapabilityID::WaveOperations_Graphics].status = CapabilityStatus::Native;
        m_records[CapabilityID::WaveOperations_Compute].status = CapabilityStatus::Native;
    }

    m_probed = true;
}

CapabilityStatus CapabilityRegistry::GetStatus(CapabilityID id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_records.find(id);
    if (it != m_records.end()) {
        return it->second.status;
    }
    return CapabilityStatus::Unsupported;
}

FeatureMaturity CapabilityRegistry::GetMaturity(CapabilityID id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_records.find(id);
    if (it != m_records.end()) {
        return it->second.maturity;
    }
    return FeatureMaturity::Unsupported;
}

const CapabilityRecord* CapabilityRegistry::GetRecord(CapabilityID id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_records.find(id);
    if (it != m_records.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<CapabilityRecord> CapabilityRegistry::GetAllRecords() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<CapabilityRecord> res;
    res.reserve(m_records.size());
    for (const auto& pair : m_records) {
        res.push_back(pair.second);
    }
    return res;
}

bool CapabilityRegistry::IsStageSupported(CapabilityID id, ShaderStageMask stage) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_records.find(id);
    if (it == m_records.end()) return false;
    if (it->second.status == CapabilityStatus::Unsupported) return false;
    return (it->second.supportedStagesMask & stage) != 0;
}

void CapabilityRegistry::SetManualOverride(CapabilityID id, CapabilityStatus status, const std::string& reason) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_records.find(id);
    if (it != m_records.end()) {
        it->second.status = status;
        if (!reason.empty()) {
            it->second.failureReason = reason;
        }
    }
}

std::string CapabilityRegistry::StatusToString(CapabilityStatus status) const {
    switch (status) {
        case CapabilityStatus::Native: return "Native";
        case CapabilityStatus::Translated: return "Translated";
        case CapabilityStatus::Emulated: return "Emulated";
        case CapabilityStatus::Unsupported: return "Unsupported";
        case CapabilityStatus::Unverified: return "Unverified";
        default: return "Unknown";
    }
}

std::string CapabilityRegistry::MaturityToString(FeatureMaturity maturity) const {
    switch (maturity) {
        case FeatureMaturity::Stable: return "Stable";
        case FeatureMaturity::Experimental: return "Experimental";
        case FeatureMaturity::Unsupported: return "Unsupported";
        case FeatureMaturity::Planned: return "Planned";
        default: return "Unknown";
    }
}

std::string CapabilityRegistry::CapabilityIDToString(CapabilityID id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_records.find(id);
    if (it != m_records.end()) {
        return it->second.name;
    }
    return "UnknownCapability";
}

std::string CapabilityRegistry::ToJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"capabilities\": [\n";

    bool first = true;
    for (const auto& pair : m_records) {
        if (!first) ss << ",\n";
        first = false;

        const auto& rec = pair.second;
        ss << "    {\n";
        ss << "      \"id\": \"" << rec.name << "\",\n";
        ss << "      \"status\": \"" << StatusToString(rec.status) << "\",\n";
        ss << "      \"maturity\": \"" << MaturityToString(rec.maturity) << "\",\n";
        ss << "      \"required_conditions\": \"" << rec.requiredConditions << "\",\n";
        ss << "      \"route\": \"" << rec.implementationRoute << "\",\n";
        ss << "      \"backend\": \"" << rec.backendPrerequisites << "\",\n";
        ss << "      \"stages_mask\": " << rec.supportedStagesMask << ",\n";
        ss << "      \"evidence\": \"" << rec.testEvidence << "\",\n";
        ss << "      \"limitations\": \"" << rec.knownLimitations << "\",\n";
        ss << "      \"failure_reason\": \"" << rec.failureReason << "\"\n";
        ss << "    }";
    }

    ss << "\n  ]\n}";
    return ss.str();
}

} // namespace kepler
