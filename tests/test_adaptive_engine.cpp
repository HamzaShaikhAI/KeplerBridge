#include "core/adaptive_engine.h"
#include <iostream>
#include <cassert>

using namespace kepler;

int main() {
    std::cout << "========================================================\n";
    std::cout << "  KeplerBridge Milestone 6: Adaptive Engine Test Suite  \n";
    std::cout << "========================================================\n" << std::endl;

    auto& engine = AdaptiveEngine::Instance();
    engine.ResetMetrics();
    engine.SetMode(AdaptiveMode::Adaptive);
    engine.SetMinSampleCount(10);
    engine.SetCooldownFrames(20);

    // 1. Metric accumulation
    std::cout << "[TEST 1] Testing telemetry metrics accumulation..." << std::endl;
    for (int i = 0; i < 15; ++i) {
        engine.RecordFrameTime(16.6); // 60 FPS
        engine.RecordCpuSubmitTime(2.1);
        engine.RecordTranslationTime(150.0, true);
    }
    assert(engine.GetAverageFrameTimeMs() > 16.5 && engine.GetAverageFrameTimeMs() < 16.7);
    assert(engine.GetAverageSubmitTimeMs() > 2.0 && engine.GetAverageSubmitTimeMs() < 2.2);
    assert(engine.GetCacheHitRatio() == 1.0);
    std::cout << "  [PASS] Telemetry correctly accumulated (Avg: " << engine.GetAverageFrameTimeMs()
              << " ms, Submit: " << engine.GetAverageSubmitTimeMs() << " ms, Cache: 100%)." << std::endl;

    // 2. Evaluation under normal load
    std::cout << "\n[TEST 2] Testing evaluation under normal load (no action expected)..." << std::endl;
    bool action1 = engine.UpdateAndEvaluate(15, MemoryPressureLevel::Normal);
    assert(!action1);
    assert(engine.GetDecisionHistory().empty());
    std::cout << "  [PASS] No unwarranted actions taken under stable conditions." << std::endl;

    // 3. Evaluation under Memory Pressure
    std::cout << "\n[TEST 3] Testing evaluation under Memory Pressure..." << std::endl;
    bool actionMem = engine.UpdateAndEvaluate(16, MemoryPressureLevel::Warning);
    assert(actionMem);
    auto history = engine.GetDecisionHistory();
    assert(history.size() == 1);
    assert(history[0].chosenAction == "TrimShaderCacheAndThrottleAllocations");
    std::cout << "  [PASS] Memory pressure correctly triggered action: " << history[0].chosenAction << std::endl;

    // 4. Cooldown enforcement
    std::cout << "\n[TEST 4] Testing cooldown enforcement..." << std::endl;
    // Frame 20 is within cooldown (16 + 20 = 36 cooldown)
    bool actionCooldown = engine.UpdateAndEvaluate(20, MemoryPressureLevel::Critical);
    assert(!actionCooldown);
    assert(engine.GetDecisionHistory().size() == 1);
    std::cout << "  [PASS] Action prevented by cooldown period." << std::endl;

    // 5. Disabled mode
    std::cout << "\n[TEST 5] Testing Disabled mode..." << std::endl;
    engine.SetMode(AdaptiveMode::Disabled);
    bool actionDisabled = engine.UpdateAndEvaluate(50, MemoryPressureLevel::Critical);
    assert(!actionDisabled);
    std::cout << "  [PASS] Disabled mode strictly prevents automated decisions." << std::endl;

    std::cout << "\n========================================================\n";
    std::cout << "   ALL ADAPTIVE ENGINE TESTS PASSED!                    \n";
    std::cout << "========================================================\n" << std::endl;
    return 0;
}
