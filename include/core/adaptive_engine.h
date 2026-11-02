#pragma once

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <cstdint>
#include "translator/memory_guardian.h"

namespace kepler {

enum class AdaptiveMode {
    Disabled,
    Manual,
    Adaptive
};

struct AdaptiveDecision {
    uint32_t frame = 0;
    std::string metricName;
    double measuredValue = 0.0;
    std::string chosenAction;
    std::string reason;
    std::string expectedBenefit;
    std::string observedResult;
};

class AdaptiveEngine {
public:
    static AdaptiveEngine& Instance();

    void SetMode(AdaptiveMode mode);
    AdaptiveMode GetMode() const { return m_mode; }

    // Telemetry sampling
    void RecordFrameTime(double frameTimeMs);
    void RecordCpuSubmitTime(double submitTimeMs);
    void RecordTranslationTime(double transTimeUs, bool isCacheHit);

    // Periodic evaluation loop (called each frame or Present)
    bool UpdateAndEvaluate(uint32_t currentFrame, MemoryPressureLevel memPressure = MemoryPressureLevel::Normal);

    // Metrics getters
    double GetAverageFrameTimeMs() const;
    double GetP95FrameTimeMs() const;
    double GetAverageSubmitTimeMs() const;
    double GetCacheHitRatio() const;

    // Decision history
    std::vector<AdaptiveDecision> GetDecisionHistory() const;
    void ClearHistory();
    void ResetMetrics();

    // Hysteresis & Tuning parameters
    void SetMinSampleCount(size_t samples) { m_minSampleCount = samples; }
    void SetCooldownFrames(uint32_t cooldown) { m_cooldownFrames = cooldown; }

private:
    AdaptiveEngine();
    ~AdaptiveEngine() = default;
    AdaptiveEngine(const AdaptiveEngine&) = delete;
    AdaptiveEngine& operator=(const AdaptiveEngine&) = delete;

    mutable std::mutex m_mutex;
    AdaptiveMode m_mode = AdaptiveMode::Adaptive;

    static constexpr size_t ROLLING_WINDOW_SIZE = 120;
    std::deque<double> m_frameTimes;
    std::deque<double> m_submitTimes;
    std::deque<double> m_translationTimes;

    uint64_t m_cacheHits = 0;
    uint64_t m_cacheQueries = 0;

    size_t m_minSampleCount = 30;
    uint32_t m_cooldownFrames = 60;
    uint32_t m_lastDecisionFrame = 0;
    bool m_hasPriorDecision = false;

    std::vector<AdaptiveDecision> m_decisions;
};

} // namespace kepler
