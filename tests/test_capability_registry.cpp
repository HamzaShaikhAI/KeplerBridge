#include "core/capability_registry.h"
#include <iostream>
#include <cassert>

using namespace kepler;

int main() {
    std::cout << "========================================================\n";
    std::cout << "   KeplerBridge Milestone 1: Capability Registry Test   \n";
    std::cout << "========================================================\n" << std::endl;

    auto& reg = CapabilityRegistry::Instance();
    reg.InitializeDefaultPolicies();

    // 1. Verify baseline defaults
    std::cout << "[TEST 1] Verifying baseline status classifications..." << std::endl;
    assert(reg.GetStatus(CapabilityID::FeatureLevel_11_0) == CapabilityStatus::Native);
    assert(reg.GetStatus(CapabilityID::FeatureLevel_12_0) == CapabilityStatus::Translated);
    assert(reg.GetStatus(CapabilityID::FeatureLevel_12_1) == CapabilityStatus::Unsupported);
    assert(reg.GetStatus(CapabilityID::FeatureLevel_12_2) == CapabilityStatus::Unsupported);
    assert(reg.GetStatus(CapabilityID::ShaderModel_5_1_Lowering) == CapabilityStatus::Translated);
    assert(reg.GetStatus(CapabilityID::ShaderModel_6_0_DXIL) == CapabilityStatus::Translated);
    assert(reg.GetStatus(CapabilityID::WaveOperations_Compute) == CapabilityStatus::Emulated);
    assert(reg.GetStatus(CapabilityID::WaveOperations_Graphics) == CapabilityStatus::Unsupported);
    assert(reg.GetStatus(CapabilityID::Int64_ALU_Lowering) == CapabilityStatus::Translated);
    assert(reg.GetStatus(CapabilityID::Int64_Atomics) == CapabilityStatus::Unsupported);
    assert(reg.GetStatus(CapabilityID::MeshShaders_BoundedCompute) == CapabilityStatus::Emulated);
    assert(reg.GetStatus(CapabilityID::MeshShaders_General) == CapabilityStatus::Unsupported);
    assert(reg.GetStatus(CapabilityID::Raytracing_Tier1_0) == CapabilityStatus::Unsupported);
    assert(reg.GetStatus(CapabilityID::DescriptorVirtualization_Bounded) == CapabilityStatus::Translated);
    assert(reg.GetStatus(CapabilityID::DescriptorVirtualization_Unbounded) == CapabilityStatus::Unsupported);
    assert(reg.GetStatus(CapabilityID::FSR1_Upscaling_SDR) == CapabilityStatus::Emulated);
    assert(reg.GetStatus(CapabilityID::FSR1_Upscaling_HDR) == CapabilityStatus::Unsupported);
    std::cout << "  [PASS] All baseline capability statuses correctly classified.\n" << std::endl;

    // 2. Stage support checks
    std::cout << "[TEST 2] Verifying stage-mask gating..." << std::endl;
    assert(reg.IsStageSupported(CapabilityID::WaveOperations_Compute, STAGE_COMPUTE) == true);
    assert(reg.IsStageSupported(CapabilityID::WaveOperations_Compute, STAGE_PIXEL) == false);
    assert(reg.IsStageSupported(CapabilityID::WaveOperations_Compute, STAGE_VERTEX) == false);
    assert(reg.IsStageSupported(CapabilityID::WaveOperations_Graphics, STAGE_PIXEL) == false);
    assert(reg.IsStageSupported(CapabilityID::Int64_ALU_Lowering, STAGE_PIXEL) == true);
    assert(reg.IsStageSupported(CapabilityID::Int64_ALU_Lowering, STAGE_COMPUTE) == true);
    assert(reg.IsStageSupported(CapabilityID::MeshShaders_BoundedCompute, STAGE_COMPUTE) == true);
    assert(reg.IsStageSupported(CapabilityID::MeshShaders_BoundedCompute, STAGE_VERTEX) == false);
    std::cout << "  [PASS] Stage-mask gating verified (Compute-only vs All-stage).\n" << std::endl;

    // 3. Live Hardware Probing (D3D11)
    std::cout << "[TEST 3] Live D3D11 Hardware Probe..." << std::endl;
    ID3D11Device* pD3D11Device = nullptr;
    ID3D11DeviceContext* pD3D11Context = nullptr;
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL obtainedLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevels,
        2,
        D3D11_SDK_VERSION,
        &pD3D11Device,
        &obtainedLevel,
        &pD3D11Context
    );

    if (SUCCEEDED(hr) && pD3D11Device) {
        reg.ProbeCapabilities(nullptr, nullptr, pD3D11Device);
        std::cout << "  Live D3D11 Hardware probed: Feature Level = 0x" << std::hex << reg.GetBackendD3D11FeatureLevel() << std::dec << std::endl;
        assert(reg.GetBackendD3D11FeatureLevel() >= D3D_FEATURE_LEVEL_11_0);
        pD3D11Context->Release();
        pD3D11Device->Release();
        std::cout << "  [PASS] Live hardware D3D11 capability probe succeeded.\n" << std::endl;
    } else {
        std::cout << "  [SKIP] D3D11 hardware creation skipped or unavailable.\n" << std::endl;
    }

    // 4. JSON Serialization
    std::cout << "[TEST 4] JSON serialization..." << std::endl;
    std::string json = reg.ToJson();
    assert(!json.empty());
    assert(json.find("\"FeatureLevel_11_0\"") != std::string::npos);
    assert(json.find("\"WaveOperations_Compute\"") != std::string::npos);
    assert(json.find("\"Emulated\"") != std::string::npos);
    assert(json.find("\"Translated\"") != std::string::npos);
    std::cout << "  [PASS] JSON telemetry string successfully generated (" << json.size() << " bytes).\n" << std::endl;

    // 5. Manual override / fallback
    std::cout << "[TEST 5] Manual override and fallback..." << std::endl;
    reg.SetManualOverride(CapabilityID::FeatureLevel_12_0, CapabilityStatus::Unsupported, "Test manual override");
    assert(reg.GetStatus(CapabilityID::FeatureLevel_12_0) == CapabilityStatus::Unsupported);
    assert(reg.GetRecord(CapabilityID::FeatureLevel_12_0)->failureReason == "Test manual override");
    std::cout << "  [PASS] Manual override behavior validated.\n" << std::endl;

    std::cout << "========================================================\n";
    std::cout << "   ALL CAPABILITY REGISTRY TESTS PASSED!                \n";
    std::cout << "========================================================\n" << std::endl;
    return 0;
}
