#include <iostream>
#include <fstream>
#include <cassert>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

#include "core/profile_manager.h"
#include "core/config.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            return false; \
        } \
    } while (0)

static void CreateTempFile(const std::string& path, const std::string& content) {
    std::ofstream ofs(path, std::ios::out | std::ios::trunc);
    ofs << content;
    ofs.close();
}

static void DeleteTempFile(const std::string& path) {
    DeleteFileA(path.c_str());
}

// ----------------------------------------------------------------------------
// Test 1: Exact and Base Profile Matching
// ----------------------------------------------------------------------------
bool TestExactAndBaseProfileMatching() {
    using namespace kepler;
    std::string testIni = "test_profile_match.ini";
    std::string content =
        "[Configuration]\n"
        "SchemaVersion=2\n"
        "EnableProfileAutoDetect=1\n"
        "ActiveProfile=Auto\n\n"
        "[Profile.Default]\n"
        "ProfileDescription=Default Profile\n"
        "EnableFeatureLevelSpoof=0\n"
        "TargetFeatureLevel=11_0\n"
        "EnableD3D11Translation=0\n\n"
        "[Profile.Palworld-Win64-Shipping]\n"
        "ProfileDescription=Palworld Base Match\n"
        "EnableFeatureLevelSpoof=1\n"
        "TargetFeatureLevel=12_0\n"
        "SpoofVRAMMB=4096\n"
        "EnableD3D11Translation=1\n"
        "EnableTextureClamping=1\n"
        "MaxTextureResolution=1024\n\n"
        "[Profile.ExactGame.exe]\n"
        "ProfileDescription=Exact Match With Extension\n"
        "EnableFeatureLevelSpoof=1\n"
        "TargetFeatureLevel=12_1\n"
        "EnableD3D11Translation=1\n";

    CreateTempFile(testIni, content);

    auto& mgr = GameProfileManager::Instance();

    // 1. Test Base matching: Palworld-Win64-Shipping.exe matches [Profile.Palworld-Win64-Shipping]
    bool ok1 = mgr.Initialize("D:\\Games\\Palworld\\Pal\\Binaries\\Win64\\Palworld-Win64-Shipping.exe", testIni);
    TEST_ASSERT(ok1, "Initialization must succeed");
    const auto& p1 = mgr.GetActivePolicy();
    TEST_ASSERT(p1.profileName == "Palworld-Win64-Shipping", "Profile name must match Palworld-Win64-Shipping");
    TEST_ASSERT(p1.enableFeatureLevelSpoof == true, "Feature level spoof must be true");
    TEST_ASSERT(p1.targetFeatureLevel == D3D_FEATURE_LEVEL_12_0, "Target feature level must be 12_0");
    TEST_ASSERT(p1.enableD3D11Translation == true, "Translation must be true");
    TEST_ASSERT(p1.enableTextureClamping == true, "Clamping must be true");
    TEST_ASSERT(p1.maxTextureResolution == 1024, "Texture resolution must be 1024");
    TEST_ASSERT(p1.isDefaultFallback == false, "Must not be fallback");

    // 2. Test Exact matching: ExactGame.exe matches [Profile.ExactGame.exe]
    bool ok2 = mgr.Initialize("C:\\Games\\ExactGame\\ExactGame.exe", testIni);
    TEST_ASSERT(ok2, "Initialization must succeed");
    const auto& p2 = mgr.GetActivePolicy();
    TEST_ASSERT(p2.profileName == "ExactGame.exe", "Profile name must match ExactGame.exe");
    TEST_ASSERT(p2.targetFeatureLevel == D3D_FEATURE_LEVEL_12_1, "Target feature level must be 12_1");
    TEST_ASSERT(p2.isDefaultFallback == false, "Must not be fallback");

    DeleteTempFile(testIni);
    std::cout << "[PASS] TestExactAndBaseProfileMatching\n";
    return true;
}

// ----------------------------------------------------------------------------
// Test 2: Unknown Executable Fallback
// ----------------------------------------------------------------------------
bool TestUnknownExecutableFallback() {
    using namespace kepler;
    std::string testIni = "test_profile_fallback.ini";
    std::string content =
        "[Configuration]\n"
        "SchemaVersion=2\n"
        "EnableProfileAutoDetect=1\n"
        "ActiveProfile=Auto\n\n"
        "[Profile.Default]\n"
        "ProfileDescription=Conservative Baseline\n"
        "EnableFeatureLevelSpoof=0\n"
        "TargetFeatureLevel=11_0\n"
        "EnableD3D11Translation=0\n";

    CreateTempFile(testIni, content);

    auto& mgr = GameProfileManager::Instance();
    bool ok = mgr.Initialize("C:\\SomeUnknownGame\\RandomGame.exe", testIni);
    TEST_ASSERT(ok, "Initialization must succeed");

    const auto& p = mgr.GetActivePolicy();
    TEST_ASSERT(p.profileName == "Default", "Profile name must fall back to Default");
    TEST_ASSERT(p.isDefaultFallback == true, "Must be flagged as fallback");
    TEST_ASSERT(p.enableFeatureLevelSpoof == false, "Default spoof must be false");
    TEST_ASSERT(p.targetFeatureLevel == D3D_FEATURE_LEVEL_11_0, "Default FL must be 11_0");

    DeleteTempFile(testIni);
    std::cout << "[PASS] TestUnknownExecutableFallback\n";
    return true;
}

// ----------------------------------------------------------------------------
// Test 3: Missing and Malformed Settings Sanitization
// ----------------------------------------------------------------------------
bool TestMissingAndMalformedSettings() {
    using namespace kepler;
    std::string testIni = "test_profile_malformed.ini";
    std::string content =
        "[Configuration]\n"
        "SchemaVersion=2\n"
        "ActiveProfile=Auto\n\n"
        "[Profile.Default]\n"
        "TargetFeatureLevel=invalid_garbage\n"
        "MaxTextureResolution=333\n"
        "MaxConcurrentUAVs=64\n"
        "CacheDirectory=\n";

    CreateTempFile(testIni, content);

    auto& mgr = GameProfileManager::Instance();
    mgr.Initialize("C:\\Games\\Unknown.exe", testIni);
    const auto& p = mgr.GetActivePolicy();

    // 1. Invalid feature level string must sanitize to 11_0
    TEST_ASSERT(p.targetFeatureLevel == D3D_FEATURE_LEVEL_11_0, "Malformed FL must default to 11_0");

    // 2. Non-power-of-two texture resolution must sanitize to 1024
    TEST_ASSERT(p.maxTextureResolution == 1024, "Malformed texture resolution must normalize to 1024");

    // 3. MaxConcurrentUAVs exceeding Kepler FL 11_0 limit (8) must clamp to 8
    TEST_ASSERT(p.maxConcurrentUAVs == 8, "UAV limit above 8 must clamp to 8");

    // 4. Empty cache directory must default
    TEST_ASSERT(!p.shaderCacheDir.empty(), "Cache dir must not be empty");

    DeleteTempFile(testIni);
    std::cout << "[PASS] TestMissingAndMalformedSettings\n";
    return true;
}

// ----------------------------------------------------------------------------
// Test 4: Configuration Precedence (ActiveProfile Override)
// ----------------------------------------------------------------------------
bool TestConfigurationPrecedence() {
    using namespace kepler;
    std::string testIni = "test_profile_precedence.ini";
    std::string content =
        "[Configuration]\n"
        "SchemaVersion=2\n"
        "ActiveProfile=CustomGame\n\n"
        "[Profile.Default]\n"
        "TargetFeatureLevel=11_0\n\n"
        "[Profile.AutoDetectedGame]\n"
        "TargetFeatureLevel=12_0\n\n"
        "[Profile.CustomGame]\n"
        "TargetFeatureLevel=12_1\n";

    CreateTempFile(testIni, content);

    auto& mgr = GameProfileManager::Instance();
    // Exe matches AutoDetectedGame, but ActiveProfile=CustomGame takes precedence
    mgr.Initialize("C:\\Games\\AutoDetectedGame.exe", testIni);
    const auto& p = mgr.GetActivePolicy();

    TEST_ASSERT(p.profileName == "CustomGame", "ActiveProfile override must take precedence");
    TEST_ASSERT(p.targetFeatureLevel == D3D_FEATURE_LEVEL_12_1, "Policy must reflect overridden profile");

    DeleteTempFile(testIni);
    std::cout << "[PASS] TestConfigurationPrecedence\n";
    return true;
}

// ----------------------------------------------------------------------------
// Test 5: Unsupported Capability Requests
// ----------------------------------------------------------------------------
bool TestUnsupportedCapabilityRequests() {
    using namespace kepler;
    std::string testIni = "test_profile_unsupported.ini";
    std::string content =
        "[Configuration]\n"
        "SchemaVersion=2\n\n"
        "[Profile.Default]\n"
        "MaxConcurrentUAVs=16\n"
        "MaxTextureResolution=16384\n";

    CreateTempFile(testIni, content);

    auto& mgr = GameProfileManager::Instance();
    mgr.Initialize("C:\\App.exe", testIni);
    const auto& p = mgr.GetActivePolicy();

    // MaxConcurrentUAVs cannot exceed Kepler hardware FL 11_0 limit (8)
    TEST_ASSERT(p.maxConcurrentUAVs <= 8, "Max concurrent UAVs must not exceed authentic hardware capability");

    // MaxTextureResolution cannot exceed 4096 on GT 740 DDR3
    TEST_ASSERT(p.maxTextureResolution <= 4096, "Max texture resolution must be clamped to hardware bounds");

    DeleteTempFile(testIni);
    std::cout << "[PASS] TestUnsupportedCapabilityRequests\n";
    return true;
}

// ----------------------------------------------------------------------------
// Test 6: Profile Isolation Between Distinct Executables
// ----------------------------------------------------------------------------
bool TestProfileIsolation() {
    using namespace kepler;
    std::string testIni = "test_profile_isolation.ini";
    std::string content =
        "[Configuration]\n"
        "SchemaVersion=2\n"
        "ActiveProfile=Auto\n\n"
        "[Profile.GameA]\n"
        "EnableD3D11Translation=1\n"
        "EnableFeatureLevelSpoof=1\n"
        "TargetFeatureLevel=12_0\n"
        "MaxTextureResolution=512\n\n"
        "[Profile.GameB]\n"
        "EnableD3D11Translation=0\n"
        "EnableFeatureLevelSpoof=0\n"
        "TargetFeatureLevel=11_0\n"
        "MaxTextureResolution=2048\n";

    CreateTempFile(testIni, content);

    auto& mgr = GameProfileManager::Instance();

    // Load Game A
    mgr.Initialize("C:\\Games\\GameA.exe", testIni);
    const auto& pA = mgr.GetActivePolicy();
    TEST_ASSERT(pA.profileName == "GameA", "GameA profile selected");
    TEST_ASSERT(pA.enableD3D11Translation == true, "GameA translation true");
    TEST_ASSERT(pA.maxTextureResolution == 512, "GameA max texture 512");

    // Load Game B — must not retain Game A's settings
    mgr.Initialize("C:\\Games\\GameB.exe", testIni);
    const auto& pB = mgr.GetActivePolicy();
    TEST_ASSERT(pB.profileName == "GameB", "GameB profile selected");
    TEST_ASSERT(pB.enableD3D11Translation == false, "GameB translation false");
    TEST_ASSERT(pB.enableFeatureLevelSpoof == false, "GameB spoof false");
    TEST_ASSERT(pB.targetFeatureLevel == D3D_FEATURE_LEVEL_11_0, "GameB FL 11_0");
    TEST_ASSERT(pB.maxTextureResolution == 2048, "GameB max texture 2048");

    DeleteTempFile(testIni);
    std::cout << "[PASS] TestProfileIsolation\n";
    return true;
}

// ----------------------------------------------------------------------------
// Test 7: Backward Compatibility With Legacy Flat v1 Configuration
// ----------------------------------------------------------------------------
bool TestBackwardCompatibilityWithLegacyConfig() {
    using namespace kepler;
    std::string testIni = "test_legacy_flat.ini";
    std::string content =
        "[General]\n"
        "EnableLogging=1\n"
        "LogVerboseCalls=1\n\n"
        "[Spoof]\n"
        "EnableFeatureLevelSpoof=1\n"
        "TargetFeatureLevel=12_0\n"
        "SpoofVRAMMB=4096\n\n"
        "[Downgrader]\n"
        "EnableShaderInspection=1\n"
        "EnableShaderFallback=1\n"
        "EnableTextureClamping=1\n"
        "MaxTextureResolution=512\n"
        "StripHeavyShaders=0\n\n"
        "[Translation]\n"
        "EnableD3D11Translation=1\n"
        "EmulateBarriers=1\n"
        "EnableStateTracking=1\n"
        "ImmediateQueueFlush=1\n"
        "EmulateQueries=1\n\n"
        "[Cache]\n"
        "EnableShaderDiskCache=1\n"
        "CacheDirectory=legacy_cache\n";

    CreateTempFile(testIni, content);

    auto& mgr = GameProfileManager::Instance();
    bool ok = mgr.Initialize("C:\\Games\\AnyLegacyGame.exe", testIni);
    TEST_ASSERT(ok, "Legacy config must load successfully");

    const auto& p = mgr.GetActivePolicy();
    TEST_ASSERT(p.profileName == "Legacy.v1", "Profile name must reflect legacy v1");
    TEST_ASSERT(p.enableFeatureLevelSpoof == true, "Legacy spoof must be true");
    TEST_ASSERT(p.targetFeatureLevel == D3D_FEATURE_LEVEL_12_0, "Legacy FL must be 12_0");
    TEST_ASSERT(p.enableD3D11Translation == true, "Legacy translation must be true");
    TEST_ASSERT(p.enableShaderFallback == true, "Legacy fallback must be true");
    TEST_ASSERT(p.enableTextureClamping == true, "Legacy clamping must be true");
    TEST_ASSERT(p.maxTextureResolution == 512, "Legacy max texture must be 512");
    TEST_ASSERT(p.shaderCacheDir == "legacy_cache", "Legacy cache dir must be legacy_cache");

    // Also verify Config::Instance() is synchronized
    auto& cfg = Config::Instance();
    cfg.Load(testIni);
    TEST_ASSERT(cfg.IsD3D11TranslationEnabled() == true, "Config sync translation");
    TEST_ASSERT(cfg.IsFeatureLevelSpoofEnabled() == true, "Config sync spoof");
    TEST_ASSERT(cfg.GetMaxTextureResolution() == 512, "Config sync texture res");
    TEST_ASSERT(cfg.GetShaderCacheDirectory() == "legacy_cache", "Config sync cache dir");

    DeleteTempFile(testIni);
    std::cout << "[PASS] TestBackwardCompatibilityWithLegacyConfig\n";
    return true;
}

// ----------------------------------------------------------------------------
// Test 8: Repeatable Selection and Diagnostic Output
// ----------------------------------------------------------------------------
bool TestRepeatableSelectionAndDiagnostics() {
    using namespace kepler;
    std::string testIni = "test_repeatable.ini";
    std::string content =
        "[Configuration]\n"
        "SchemaVersion=2\n\n"
        "[Profile.Default]\n"
        "TargetFeatureLevel=12_0\n"
        "EnableD3D11Translation=1\n";

    CreateTempFile(testIni, content);

    auto& mgr = GameProfileManager::Instance();
    for (int i = 0; i < 5; ++i) {
        mgr.Initialize("C:\\TestPath\\Game.exe", testIni);
        const auto& p = mgr.GetActivePolicy();
        TEST_ASSERT(p.profileName == "Default", "Profile selection must be deterministic");
        TEST_ASSERT(p.targetFeatureLevel == D3D_FEATURE_LEVEL_12_0, "FL must remain 12_0");
        TEST_ASSERT(p.enableD3D11Translation == true, "Translation must remain true");
    }

    DeleteTempFile(testIni);
    std::cout << "[PASS] TestRepeatableSelectionAndDiagnostics\n";
    return true;
}

// ----------------------------------------------------------------------------
// Test 9: Runtime Telemetry Recording and Statistics
// ----------------------------------------------------------------------------
bool TestRuntimeTelemetry() {
    using namespace kepler;
    auto& telemetry = RuntimeTelemetry::Instance();
    telemetry.Reset();

    // 1. Record frame presentations
    telemetry.RecordPresent(16.6);
    telemetry.RecordPresent(16.7);
    telemetry.RecordPresent(33.3);
    telemetry.RecordPresent(16.5);
    telemetry.RecordPresent(16.8);

    // 2. Record PSO compilation metrics
    telemetry.RecordPSOCompilation(true, 0.5);  // Cache Hit
    telemetry.RecordPSOCompilation(true, 0.4);  // Cache Hit
    telemetry.RecordPSOCompilation(false, 8.2); // Cache Miss
    telemetry.RecordPSOCompilation(false, 6.1); // Cache Miss

    // 3. Record UAV bindings
    telemetry.RecordUAVBinding(0);
    telemetry.RecordUAVBinding(3);
    telemetry.RecordUAVBinding(7);
    telemetry.RecordUAVBinding(2);

    // 4. Record device removal event
    telemetry.RecordDeviceRemoval(0x887A0005); // DXGI_ERROR_DEVICE_REMOVED

    // 5. Verify snapshot
    TelemetrySnapshot snap = telemetry.GetSnapshot();
    TEST_ASSERT(snap.totalFramesPresented == 5, "Total frames presented must be 5");
    TEST_ASSERT(snap.psoCompileCount == 4, "Total PSO compilations must be 4");
    TEST_ASSERT(snap.shaderCacheHits == 2, "Cache hits must be 2");
    TEST_ASSERT(snap.shaderCacheMisses == 2, "Cache misses must be 2");
    TEST_ASSERT(snap.peakUAVSlotBound == 7, "Peak UAV slot must be 7");
    TEST_ASSERT(snap.lastDeviceRemovalReason == static_cast<HRESULT>(0x887A0005), "Device removal HRESULT match");

    TEST_ASSERT(snap.minFrameTimeMs >= 16.4 && snap.minFrameTimeMs <= 16.6, "Min frame time check");
    TEST_ASSERT(snap.maxFrameTimeMs >= 33.0 && snap.maxFrameTimeMs <= 33.5, "Max frame time check");
    TEST_ASSERT(snap.averageFrameTimeMs >= 19.0 && snap.averageFrameTimeMs <= 21.0, "Avg frame time check");

    telemetry.Reset();
    TelemetrySnapshot cleared = telemetry.GetSnapshot();
    TEST_ASSERT(cleared.totalFramesPresented == 0, "Frames reset");
    TEST_ASSERT(cleared.psoCompileCount == 0, "PSO count reset");

    std::cout << "[PASS] TestRuntimeTelemetry\n";
    return true;
}

// ----------------------------------------------------------------------------
// Main Test Runner
// ----------------------------------------------------------------------------
int main() {
    std::cout << "========================================================\n";
    std::cout << "  KeplerBridge Phase 11B Profile Manager Unit Tests     \n";
    std::cout << "========================================================\n";

    int failures = 0;
    if (!TestExactAndBaseProfileMatching()) failures++;
    if (!TestUnknownExecutableFallback()) failures++;
    if (!TestMissingAndMalformedSettings()) failures++;
    if (!TestConfigurationPrecedence()) failures++;
    if (!TestUnsupportedCapabilityRequests()) failures++;
    if (!TestProfileIsolation()) failures++;
    if (!TestBackwardCompatibilityWithLegacyConfig()) failures++;
    if (!TestRepeatableSelectionAndDiagnostics()) failures++;
    if (!TestRuntimeTelemetry()) failures++;

    std::cout << "========================================================\n";
    if (failures == 0) {
        std::cout << "ALL PROFILE MANAGER & CAPABILITY TESTS PASSED (9/9)!\n";
        return 0;
    } else {
        std::cerr << failures << " TESTS FAILED!\n";
        return 1;
    }
}
