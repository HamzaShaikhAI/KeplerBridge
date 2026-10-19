#include <iostream>
#include <cassert>
#include <string>
#include <vector>

#include "core/feature_level_compat.h"
#include "core/config.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            return false; \
        } \
    } while (0)

bool TestParseFeatureLevelString() {
    using namespace kepler;
    TEST_ASSERT(FeatureLevelCompat::ParseFeatureLevelString("11_0") == D3D_FEATURE_LEVEL_11_0, "11_0 mismatch");
    TEST_ASSERT(FeatureLevelCompat::ParseFeatureLevelString("11.0") == D3D_FEATURE_LEVEL_11_0, "11.0 mismatch");
    TEST_ASSERT(FeatureLevelCompat::ParseFeatureLevelString("11_1") == D3D_FEATURE_LEVEL_11_1, "11_1 mismatch");
    TEST_ASSERT(FeatureLevelCompat::ParseFeatureLevelString("12_0") == D3D_FEATURE_LEVEL_12_0, "12_0 mismatch");
    TEST_ASSERT(FeatureLevelCompat::ParseFeatureLevelString("12_1") == D3D_FEATURE_LEVEL_12_1, "12_1 mismatch");
    TEST_ASSERT(FeatureLevelCompat::ParseFeatureLevelString("12_2") == D3D_FEATURE_LEVEL_12_2, "12_2 mismatch");
    TEST_ASSERT(FeatureLevelCompat::ParseFeatureLevelString("unknown") == D3D_FEATURE_LEVEL_12_0, "default fallback mismatch");
    std::cout << "[PASS] TestParseFeatureLevelString\n";
    return true;
}

bool TestFeatureLevelToShortString() {
    using namespace kepler;
    TEST_ASSERT(FeatureLevelCompat::FeatureLevelToShortString(D3D_FEATURE_LEVEL_11_0) == "11_0", "11_0 short str");
    TEST_ASSERT(FeatureLevelCompat::FeatureLevelToShortString(D3D_FEATURE_LEVEL_11_1) == "11_1", "11_1 short str");
    TEST_ASSERT(FeatureLevelCompat::FeatureLevelToShortString(D3D_FEATURE_LEVEL_12_0) == "12_0", "12_0 short str");
    TEST_ASSERT(FeatureLevelCompat::FeatureLevelToShortString(D3D_FEATURE_LEVEL_12_1) == "12_1", "12_1 short str");
    TEST_ASSERT(FeatureLevelCompat::FeatureLevelToShortString(D3D_FEATURE_LEVEL_12_2) == "12_2", "12_2 short str");
    std::cout << "[PASS] TestFeatureLevelToShortString\n";
    return true;
}

bool TestPolicySpoofDisabled() {
    using namespace kepler;
    auto& compat = FeatureLevelCompat::Instance();

    // When spoofing is disabled, all requests must pass through untouched
    auto res1 = compat.EvaluatePolicy(false, "12_0", D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_12_0);
    TEST_ASSERT(!res1.should_intercept, "Must not intercept when spoofing is disabled");
    TEST_ASSERT(res1.creation_level == D3D_FEATURE_LEVEL_12_0, "Creation level must match requested level");

    auto res2 = compat.EvaluatePolicy(false, "12_0", D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_0);
    TEST_ASSERT(!res2.should_intercept, "Must not intercept native requests");
    TEST_ASSERT(res2.creation_level == D3D_FEATURE_LEVEL_11_0, "Creation level must be 11_0");

    std::cout << "[PASS] TestPolicySpoofDisabled\n";
    return true;
}

bool TestPolicySpoofEnabledAtOrBelowNative() {
    using namespace kepler;
    auto& compat = FeatureLevelCompat::Instance();

    // Requesting 11_0 when native is 11_0 should NOT intercept (native is already sufficient)
    auto res1 = compat.EvaluatePolicy(true, "12_0", D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_0);
    TEST_ASSERT(!res1.should_intercept, "Must not intercept when request <= native");
    TEST_ASSERT(res1.creation_level == D3D_FEATURE_LEVEL_11_0, "Creation level must remain 11_0");

    // Requesting 10_0 when native is 11_0 should NOT intercept
    auto res2 = compat.EvaluatePolicy(true, "12_0", D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0);
    TEST_ASSERT(!res2.should_intercept, "Must not intercept when request < native");
    TEST_ASSERT(res2.creation_level == D3D_FEATURE_LEVEL_10_0, "Creation level must remain 10_0");

    std::cout << "[PASS] TestPolicySpoofEnabledAtOrBelowNative\n";
    return true;
}

bool TestPolicySpoofEnabledAboveNative() {
    using namespace kepler;
    auto& compat = FeatureLevelCompat::Instance();

    // Requesting 12_0 when native is 11_0 MUST intercept and downgrade creation to 11_0
    auto res1 = compat.EvaluatePolicy(true, "12_0", D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_12_0);
    TEST_ASSERT(res1.should_intercept, "Must intercept when request > native");
    TEST_ASSERT(res1.creation_level == D3D_FEATURE_LEVEL_11_0, "Creation level must downgrade to native 11_0");
    TEST_ASSERT(res1.target_spoof_level == D3D_FEATURE_LEVEL_12_0, "Target spoof level must be 12_0");

    // Requesting 12_1 when target is 12_1
    auto res2 = compat.EvaluatePolicy(true, "12_1", D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_12_1);
    TEST_ASSERT(res2.should_intercept, "Must intercept when request 12_1 > native");
    TEST_ASSERT(res2.creation_level == D3D_FEATURE_LEVEL_11_0, "Creation level must downgrade to native 11_0");
    TEST_ASSERT(res2.target_spoof_level == D3D_FEATURE_LEVEL_12_1, "Target spoof level must be 12_1");

    std::cout << "[PASS] TestPolicySpoofEnabledAboveNative\n";
    return true;
}

bool TestConfigParsing() {
    using namespace kepler;
    auto& cfg = Config::Instance();

    // Load non-existent file should safely fallback to internal defaults
    bool loaded = cfg.Load("non_existent_config.ini");
    TEST_ASSERT(!loaded, "Non-existent file should report false");
    TEST_ASSERT(cfg.IsLoggingEnabled() == true, "Default logging should be true");
    TEST_ASSERT(cfg.IsFeatureLevelSpoofEnabled() == false, "Default spoof should be false");
    TEST_ASSERT(cfg.GetSpoofVRAMMB() == 4096, "Default VRAM should be 4096");

    std::cout << "[PASS] TestConfigParsing\n";
    return true;
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "  KeplerBridge Phase 2 Policy Engine Unit Tests    \n";
    std::cout << "====================================================\n";

    int failures = 0;
    if (!TestParseFeatureLevelString()) failures++;
    if (!TestFeatureLevelToShortString()) failures++;
    if (!TestPolicySpoofDisabled()) failures++;
    if (!TestPolicySpoofEnabledAtOrBelowNative()) failures++;
    if (!TestPolicySpoofEnabledAboveNative()) failures++;
    if (!TestConfigParsing()) failures++;

    std::cout << "====================================================\n";
    if (failures == 0) {
        std::cout << "ALL POLICY ENGINE TESTS PASSED (6/6)!\n";
        return 0;
    } else {
        std::cerr << failures << " TESTS FAILED!\n";
        return 1;
    }
}
