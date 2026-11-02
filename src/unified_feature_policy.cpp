#include "core/unified_feature_policy.h"
#include "core/logger.h"

namespace kepler {

UnifiedFeaturePolicyEngine& UnifiedFeaturePolicyEngine::Instance() {
    static UnifiedFeaturePolicyEngine s_instance;
    return s_instance;
}

FeatureDecisionResult UnifiedFeaturePolicyEngine::EvaluateRequest(
    const FeatureDecisionRequest& request,
    const CapabilityPolicy& profile)
{
    FeatureDecisionResult res{};
    auto& capReg = CapabilityRegistry::Instance();
    res.status = capReg.GetStatus(request.featureId);

    // 1. Check if the capability is natively or translationally supported in the registry
    if (res.status == CapabilityStatus::Unsupported) {
        res.isPermitted = false;
        res.route = "Rejected";
        const auto* rec = capReg.GetRecord(request.featureId);
        res.diagnostic = (rec && !rec->failureReason.empty()) ? rec->failureReason : "Unsupported capability requested.";
        return res;
    }

    // 2. Memory Guardian Pressure Gate: If under Critical pressure, reject non-essential emulation
    if (MemoryGuardian::Instance().GetPressureLevel() == MemoryPressureLevel::Critical) {
        if (res.status == CapabilityStatus::Emulated) {
            res.isPermitted = false;
            res.route = "RejectedUnderMemoryPressure";
            res.diagnostic = "Memory Guardian: Emulated feature rejected to prevent out-of-memory GPU crash under critical memory pressure.";
            return res;
        }
    }

    // 3. Capability-Specific Evaluation
    switch (request.featureId) {
        case CapabilityID::FeatureLevel_12_0:
            if (!profile.enableD3D11Translation && !profile.enableFeatureLevelSpoof) {
                res.isPermitted = false;
                res.route = "RejectedByProfile";
                res.diagnostic = "Active profile (Conservative) prohibits D3D12 FL 12_0 translation without explicit enablement.";
                return res;
            }
            res.isPermitted = true;
            res.route = "D3D12toD3D11Translation";
            break;

        case CapabilityID::WaveOperations_Compute:
            if (request.stage != STAGE_COMPUTE) {
                res.isPermitted = false;
                res.route = "RejectedStageMismatch";
                res.diagnostic = "Wave operations are physically unsupported in Vertex/Pixel stages on Kepler FL 11_0.";
                return res;
            }
            res.isPermitted = true;
            res.route = "ThreadgroupLDSEmulation";
            break;

        case CapabilityID::WaveOperations_Graphics:
            res.isPermitted = false;
            res.route = "Rejected";
            res.diagnostic = "Wave operations in graphics units physically unsupported on Kepler FL 11_0.";
            break;

        case CapabilityID::Int64_ALU_Lowering:
            res.isPermitted = true;
            res.route = "RegisterPairLowering";
            break;

        case CapabilityID::Int64_Atomics:
            res.isPermitted = false;
            res.route = "Rejected";
            res.diagnostic = Int64LoweringEngine::GetAtomicsRejectionDiagnostic();
            break;

        case CapabilityID::MeshShaders_BoundedCompute:
            if (request.hasAmplificationShader) {
                res.isPermitted = false;
                res.route = "RejectedAmplificationShader";
                res.diagnostic = "Amplification/task shaders require hardware primitive expansion units.";
                return res;
            }
            if (request.meshVertexCount > 64 || request.meshPrimitiveCount > 126) {
                res.isPermitted = false;
                res.route = "RejectedBoundsExceeded";
                res.diagnostic = "Meshlet exceeds bounded prototype limits (max 64 vertices, 126 primitives).";
                return res;
            }
            res.isPermitted = true;
            res.route = "ComputeGeometryTransformation";
            break;

        case CapabilityID::DescriptorVirtualization_Bounded:
            if (request.isUnboundedDescriptorRange) {
                res.isPermitted = false;
                res.route = "RejectedUnboundedHeap";
                res.diagnostic = "Unbounded descriptor range (ResourceDescriptorHeap) cannot map to D3D11 fixed slots.";
                return res;
            }
            res.isPermitted = true;
            res.route = "RemappedD3D11Slots";
            break;

        case CapabilityID::FSR1_Upscaling_SDR:
            if (!profile.enableFSR1Upscaling) {
                res.isPermitted = false;
                res.route = "DisabledByProfile";
                res.diagnostic = "FSR 1 spatial upscaling is disabled in the active profile.";
                return res;
            }
            if (!request.isSdrBackbuffer) {
                res.isPermitted = false;
                res.route = "RejectedUnsupportedFormat";
                res.diagnostic = "FSR 1 is restricted to SDR (8-bit UNORM) presentation swapchains.";
                return res;
            }
            res.isPermitted = true;
            res.route = "FSR1_EASU_RCAS_CS50";
            break;

        default:
            res.isPermitted = true;
            res.route = "DefaultPassthrough";
            break;
    }

    return res;
}

} // namespace kepler
