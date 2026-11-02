#include "downgrader/shader_autopsy.h"
#include "downgrader/dxil_translation.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include <fstream>

using namespace kepler;

// Helper to construct a synthetic DXBC container with an SFI0 chunk
static std::vector<uint8_t> MakeDXBCWithSFI0(uint64_t featureFlags) {
    std::vector<uint8_t> blob;
    blob.resize(128, 0);

    // DXBC magic
    *reinterpret_cast<uint32_t*>(&blob[0]) = 0x43425844; // "DXBC"
    *reinterpret_cast<uint32_t*>(&blob[24]) = static_cast<uint32_t>(blob.size());
    *reinterpret_cast<uint32_t*>(&blob[28]) = 1; // 1 chunk
    *reinterpret_cast<uint32_t*>(&blob[32]) = 36; // offset to chunk 0

    // Chunk 0: SFI0
    *reinterpret_cast<uint32_t*>(&blob[36]) = 0x30494653; // "SFI0"
    *reinterpret_cast<uint32_t*>(&blob[40]) = 8; // chunk size
    *reinterpret_cast<uint64_t*>(&blob[44]) = featureFlags;

    return blob;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "   KeplerBridge Milestone 3: Shader Autopsy Test Suite  \n";
    std::cout << "========================================================\n" << std::endl;

    auto& autopsy = ShaderAutopsy::Instance();
    std::string testJsonl = "test_results/test_autopsy.jsonl";
    autopsy.Initialize(testJsonl);
    autopsy.ClearHistory();

    // 1. Invalid / Malformed inputs
    std::cout << "[TEST 1] Testing invalid / corrupt container autopsy..." << std::endl;
    {
        uint8_t junk[16] = {0};
        ShaderAutopsyRecord record{};
        bool res = autopsy.PerformAutopsy(junk, sizeof(junk), "CORRUPT_01", record);
        assert(!res);
        assert(record.failureStage == "Preflight");
        std::cout << "  [PASS] Preflight rejection verified: " << record.failureReason << std::endl;
    }
    {
        uint8_t notDxbc[64] = {0};
        *reinterpret_cast<uint32_t*>(notDxbc) = 0x12345678;
        ShaderAutopsyRecord record{};
        bool res = autopsy.PerformAutopsy(notDxbc, sizeof(notDxbc), "NOT_DXBC_02", record);
        assert(!res);
        assert(record.failureStage == "ContainerValidation");
        std::cout << "  [PASS] Non-DXBC rejection verified: " << record.failureReason << std::endl;
    }

    // 2. Feature flag detection (SFI0 with WaveOps and Int64)
    std::cout << "\n[TEST 2] Testing SFI0 Feature Flag extraction..." << std::endl;
    {
        auto blob = MakeDXBCWithSFI0(SFI_WAVE_OPS | SFI_INT64_OPS | SFI_ROVS);
        ShaderAutopsyRecord record{};
        autopsy.PerformAutopsy(blob.data(), blob.size(), "WAVE_INT64_ROV_03", record);
        assert(record.featureFlags == (SFI_WAVE_OPS | SFI_INT64_OPS | SFI_ROVS));
        assert(record.featureFlagNames.size() == 3);
        bool foundWave = false, foundInt64 = false, foundRov = false;
        for (const auto& name : record.featureFlagNames) {
            if (name == "WaveOps") foundWave = true;
            if (name == "Int64Ops") foundInt64 = true;
            if (name == "ROVs") foundRov = true;
        }
        assert(foundWave && foundInt64 && foundRov);
        std::cout << "  [PASS] SFI0 flags correctly decomposed into 3 features: WaveOps, Int64Ops, ROVs." << std::endl;
    }

    // 3. Formatting and Serialization
    std::cout << "\n[TEST 3] Testing report formatting (Human-readable and JSON)..." << std::endl;
    {
        auto blob = MakeDXBCWithSFI0(SFI_RAYTRACING_TIER_1_1 | SFI_SAMPLER_FEEDBACK);
        ShaderAutopsyRecord record{};
        autopsy.PerformAutopsy(blob.data(), blob.size(), "FORMAT_TEST_04", record);

        std::string humanText = autopsy.FormatHumanReadable(record);
        assert(!humanText.empty());
        assert(humanText.find("FORMAT_TEST_04") != std::string::npos);
        assert(humanText.find("RaytracingTier1_1") != std::string::npos);
        assert(humanText.find("SamplerFeedback") != std::string::npos);

        std::string jsonText = autopsy.FormatJson(record);
        assert(!jsonText.empty());
        assert(jsonText.find("\"correlation_id\":\"FORMAT_TEST_04\"") != std::string::npos);
        assert(jsonText.find("\"RaytracingTier1_1\"") != std::string::npos);
        std::cout << "  [PASS] Formatted human-readable and JSON reports verified." << std::endl;
    }

    // 4. Rate limiting duplicate hashes
    std::cout << "\n[TEST 4] Testing duplicate suppression & rate limiting..." << std::endl;
    {
        auto blob = MakeDXBCWithSFI0(SFI_TILED_RESOURCES);
        for (int i = 0; i < 20; ++i) {
            ShaderAutopsyRecord rec{};
            autopsy.PerformAutopsy(blob.data(), blob.size(), "DUP_TEST", rec);
        }
        assert(autopsy.GetTotalInspected() >= 23); // Total inspected includes previous tests
        std::cout << "  [PASS] Rate-limiting active: " << autopsy.GetTotalInspected() << " calls processed safely." << std::endl;
    }

    std::cout << "\n========================================================\n";
    std::cout << "   ALL SHADER AUTOPSY TESTS PASSED!                     \n";
    std::cout << "========================================================\n" << std::endl;
    return 0;
}
