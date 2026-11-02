#pragma once

#include <string>
#include <vector>
#include "core/capability_registry.h"
#include "core/profile_manager.h"
#include "translator/memory_guardian.h"
#include "core/adaptive_engine.h"
#include "downgrader/shader_autopsy.h"
#include "translator/descriptor_virtualizer.h"
#include "downgrader/wave_emulation.h"
#include "downgrader/int64_lowering.h"
#include "downgrader/mesh_shader_compat.h"
#include "translator/fsr1_upscaler.h"

namespace kepler {

struct FeatureDecisionRequest {
    CapabilityID featureId = CapabilityID::FeatureLevel_11_0;
    ShaderStageMask stage = STAGE_NONE;
    const void* bytecode = nullptr;
    size_t bytecodeSize = 0;
    uint32_t descriptorCount = 1;
    bool isUnboundedDescriptorRange = false;
    uint32_t meshVertexCount = 0;
    uint32_t meshPrimitiveCount = 0;
    bool hasAmplificationShader = false;
    bool isSdrBackbuffer = true;
};

struct FeatureDecisionResult {
    bool isPermitted = false;
    CapabilityStatus status = CapabilityStatus::Unsupported;
    std::string route;
    std::string targetProfile;
    std::string diagnostic;
};

class UnifiedFeaturePolicyEngine {
public:
    static UnifiedFeaturePolicyEngine& Instance();

    /**
     * @brief Evaluates an incoming application feature or workload request across all
     *        KeplerBridge subsystems (Capabilities, Profile, Memory, Adaptive, Downgraders).
     */
    FeatureDecisionResult EvaluateRequest(
        const FeatureDecisionRequest& request,
        const CapabilityPolicy& profile);

private:
    UnifiedFeaturePolicyEngine() = default;
    ~UnifiedFeaturePolicyEngine() = default;
};

} // namespace kepler
