#include "core/unified_feature_policy.h"
#include <iostream>
#include <cassert>

using namespace kepler;

int main() {
    std::cout << "========================================================\n";
    std::cout << "   KeplerBridge Milestone 13: Unified Feature Policy    \n";
    std::cout << "========================================================\n" << std::endl;

    auto& engine = UnifiedFeaturePolicyEngine::Instance();
    auto pCons = GameProfileManager::CreateConservativeProfile();
    auto pPerf = GameProfileManager::CreatePerformanceProfile();

    // 1. Conservative Profile Evaluation
    std::cout << "[TEST 1] Testing Conservative profile gating..." << std::endl;
    FeatureDecisionRequest reqFL12{};
    reqFL12.featureId = CapabilityID::FeatureLevel_12_0;

    auto res1 = engine.EvaluateRequest(reqFL12, pCons);
    assert(!res1.isPermitted);
    assert(res1.route == "RejectedByProfile");
    std::cout << "  [PASS] FL 12_0 translation gated by Conservative profile: " << res1.diagnostic << std::endl;

    auto res2 = engine.EvaluateRequest(reqFL12, pPerf);
    assert(res2.isPermitted);
    assert(res2.route == "D3D12toD3D11Translation");
    std::cout << "  [PASS] FL 12_0 translation permitted by Performance profile." << std::endl;

    // 2. Wave Operations Stage Gating
    std::cout << "\n[TEST 2] Testing Wave Operations stage gating..." << std::endl;
    FeatureDecisionRequest reqWaveCS{};
    reqWaveCS.featureId = CapabilityID::WaveOperations_Compute;
    reqWaveCS.stage = STAGE_COMPUTE;
    auto resWaveCS = engine.EvaluateRequest(reqWaveCS, pPerf);
    assert(resWaveCS.isPermitted);
    assert(resWaveCS.route == "ThreadgroupLDSEmulation");

    FeatureDecisionRequest reqWavePS{};
    reqWavePS.featureId = CapabilityID::WaveOperations_Compute;
    reqWavePS.stage = STAGE_PIXEL;
    auto resWavePS = engine.EvaluateRequest(reqWavePS, pPerf);
    assert(!resWavePS.isPermitted);
    assert(resWavePS.route == "RejectedStageMismatch");
    std::cout << "  [PASS] Wave ops permitted in Compute stage, rejected in Pixel stage." << std::endl;

    // 3. 64-Bit Integer ALU vs Atomics
    std::cout << "\n[TEST 3] Testing 64-Bit Integer ALU vs Atomics..." << std::endl;
    FeatureDecisionRequest reqI64{};
    reqI64.featureId = CapabilityID::Int64_ALU_Lowering;
    auto resI64 = engine.EvaluateRequest(reqI64, pPerf);
    assert(resI64.isPermitted);
    assert(resI64.route == "RegisterPairLowering");

    FeatureDecisionRequest reqI64Atomic{};
    reqI64Atomic.featureId = CapabilityID::Int64_Atomics;
    auto resI64Atomic = engine.EvaluateRequest(reqI64Atomic, pPerf);
    assert(!resI64Atomic.isPermitted);
    assert(resI64Atomic.route == "Rejected");
    std::cout << "  [PASS] 64-bit ALU lowered to register pairs; 64-bit atomics safely rejected." << std::endl;

    // 4. Mesh Shader Bounded vs Amplification
    std::cout << "\n[TEST 4] Testing Mesh Shader Bounded vs Amplification..." << std::endl;
    FeatureDecisionRequest reqMeshBounded{};
    reqMeshBounded.featureId = CapabilityID::MeshShaders_BoundedCompute;
    reqMeshBounded.meshVertexCount = 3;
    reqMeshBounded.meshPrimitiveCount = 1;
    auto resMeshBounded = engine.EvaluateRequest(reqMeshBounded, pPerf);
    assert(resMeshBounded.isPermitted);
    assert(resMeshBounded.route == "ComputeGeometryTransformation");

    FeatureDecisionRequest reqMeshAmp = reqMeshBounded;
    reqMeshAmp.hasAmplificationShader = true;
    auto resMeshAmp = engine.EvaluateRequest(reqMeshAmp, pPerf);
    assert(!resMeshAmp.isPermitted);
    assert(resMeshAmp.route == "RejectedAmplificationShader");
    std::cout << "  [PASS] Bounded meshlet permitted; amplification shader cleanly rejected." << std::endl;

    // 5. Descriptor Virtualization Bounded vs Unbounded
    std::cout << "\n[TEST 5] Testing Descriptor Virtualization Bounded vs Unbounded..." << std::endl;
    FeatureDecisionRequest reqDescBounded{};
    reqDescBounded.featureId = CapabilityID::DescriptorVirtualization_Bounded;
    reqDescBounded.isUnboundedDescriptorRange = false;
    auto resDescBounded = engine.EvaluateRequest(reqDescBounded, pPerf);
    assert(resDescBounded.isPermitted);
    assert(resDescBounded.route == "RemappedD3D11Slots");

    FeatureDecisionRequest reqDescUnbounded = reqDescBounded;
    reqDescUnbounded.isUnboundedDescriptorRange = true;
    auto resDescUnbounded = engine.EvaluateRequest(reqDescUnbounded, pPerf);
    assert(!resDescUnbounded.isPermitted);
    assert(resDescUnbounded.route == "RejectedUnboundedHeap");
    std::cout << "  [PASS] Bounded table remapped; unbounded descriptor heap cleanly rejected." << std::endl;

    // 6. FSR 1 Upscaling
    std::cout << "\n[TEST 6] Testing FSR 1 Upscaling Profile & SDR Gating..." << std::endl;
    FeatureDecisionRequest reqFsr{};
    reqFsr.featureId = CapabilityID::FSR1_Upscaling_SDR;
    reqFsr.isSdrBackbuffer = true;

    auto resFsrCons = engine.EvaluateRequest(reqFsr, pCons);
    assert(!resFsrCons.isPermitted);
    assert(resFsrCons.route == "DisabledByProfile");

    auto resFsrPerf = engine.EvaluateRequest(reqFsr, pPerf);
    assert(resFsrPerf.isPermitted);
    assert(resFsrPerf.route == "FSR1_EASU_RCAS_CS50");

    FeatureDecisionRequest reqFsrHdr = reqFsr;
    reqFsrHdr.isSdrBackbuffer = false;
    auto resFsrHdr = engine.EvaluateRequest(reqFsrHdr, pPerf);
    assert(!resFsrHdr.isPermitted);
    assert(resFsrHdr.route == "RejectedUnsupportedFormat");
    std::cout << "  [PASS] FSR 1 enabled in Performance SDR, rejected in HDR or Conservative." << std::endl;

    std::cout << "\n========================================================\n";
    std::cout << "   ALL UNIFIED FEATURE POLICY TESTS PASSED!             \n";
    std::cout << "========================================================\n" << std::endl;
    return 0;
}
