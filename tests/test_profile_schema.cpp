#include "core/profile_manager.h"
#include <iostream>
#include <cassert>
#include <vector>

using namespace kepler;

int main() {
    std::cout << "========================================================\n";
    std::cout << "  KeplerBridge Milestone 7: Profile Schema & Presets    \n";
    std::cout << "========================================================\n" << std::endl;

    // 1. Factory Profiles
    std::cout << "[TEST 1] Testing factory profiles generation..." << std::endl;
    auto pCons = GameProfileManager::CreateConservativeProfile();
    assert(pCons.profileType == "Conservative");
    assert(pCons.enableD3D11Translation == false);
    assert(pCons.enableFeatureLevelSpoof == false);
    assert(pCons.schemaVersion == 2);

    auto pManual = GameProfileManager::CreateManualProfile();
    assert(pManual.profileType == "Manual");
    assert(pManual.enableD3D11Translation == true);
    assert(pManual.allowAdaptiveEngine == false);

    auto pPerf = GameProfileManager::CreatePerformanceProfile();
    assert(pPerf.profileType == "Performance");
    assert(pPerf.enableFSR1Upscaling == true);
    assert(pPerf.fsr1Preset == "Performance");
    std::cout << "  [PASS] Conservative, Manual, and Performance factory profiles validated." << std::endl;

    // 2. Schema Validation (Valid Profiles)
    std::cout << "\n[TEST 2] Testing schema validation on valid profiles..." << std::endl;
    std::vector<std::string> errors;
    assert(GameProfileManager::ValidatePolicySchema(pCons, errors));
    assert(errors.empty());
    assert(GameProfileManager::ValidatePolicySchema(pManual, errors));
    assert(errors.empty());
    assert(GameProfileManager::ValidatePolicySchema(pPerf, errors));
    assert(errors.empty());
    std::cout << "  [PASS] Valid profiles passed schema validation with 0 errors." << std::endl;

    // 3. Schema Validation (Invalid Profiles)
    std::cout << "\n[TEST 3] Testing schema validation on invalid profiles..." << std::endl;
    auto pInvalid = pCons;
    pInvalid.maxConcurrentUAVs = 16; // Illegal on Kepler FL 11_0 (>8)
    pInvalid.maxTextureResolution = 3000; // Not a power of two
    pInvalid.fsr1Sharpness = 1.5f; // Out of range [0, 1]
    pInvalid.fsr1Preset = "InvalidPresetName";

    bool valid = GameProfileManager::ValidatePolicySchema(pInvalid, errors);
    assert(!valid);
    assert(errors.size() >= 4);
    std::cout << "  [PASS] Detected " << errors.size() << " schema violations correctly:" << std::endl;
    for (const auto& err : errors) {
        std::cout << "    - " << err << std::endl;
    }

    // 4. Schema Migration
    std::cout << "\n[TEST 4] Testing schema migration (v1 -> v2)..." << std::endl;
    CapabilityPolicy pV1;
    pV1.schemaVersion = 1;
    bool migrated = GameProfileManager::MigrateSchema(pV1, 1, 2);
    assert(migrated);
    assert(pV1.schemaVersion == 2);
    assert(pV1.memorySoftBudgetMB == 3072);
    assert(pV1.diagnosticsLevel == "Standard");
    std::cout << "  [PASS] Schema migration v1 -> v2 succeeded." << std::endl;

    std::cout << "\n========================================================\n";
    std::cout << "   ALL PROFILE SCHEMA TESTS PASSED!                     \n";
    std::cout << "========================================================\n" << std::endl;
    return 0;
}
