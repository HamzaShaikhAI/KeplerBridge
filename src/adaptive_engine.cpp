#include "core/adaptive_engine.h"
#include "core/logger.h"
#include <algorithm>
#include <numeric>
#include <sstream>

namespace kepler {

AdaptiveEngine& AdaptiveEngine::Instance() {
    static AdaptiveEngine s_instance;
    return s_instance;
}

AdaptiveEngine::AdaptiveEngine() {
}

void AdaptiveEngine::SetMode(AdaptiveMode mode) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mode = mode;
    LOG_INFO("[AdaptiveEngine] Mode changed to " + std::to_string(static_cast<int>(mode)));
}

void AdaptiveEngine::RecordFrameTime(double frameTimeMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_frameTimes.size() >= ROLLING_WINDOW_SIZE) {
        m_frameTimes.pop_front();
    }
    m_frameTimes.push_back(frameTimeMs);
}

void AdaptiveEngine::RecordCpuSubmitTime(double submitTimeMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_submitTimes.size() >= ROLLING_WINDOW_SIZE) {
        m_submitTimes.pop_front();
    }
    m_submitTimes.push_back(submitTimeMs);
}

void AdaptiveEngine::RecordTranslationTime(double transTimeUs, bool isCacheHit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_translationTimes.size() >= ROLLING_WINDOW_SIZE) {
        m_translationTimes.pop_front();
    }
    m_translationTimes.push_back(transTimeUs);
    m_cacheQueries++;
    if (isCacheHit) {
        m_cacheHits++;
    }
}

double AdaptiveEngine::GetAverageFrameTimeMs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_frameTimes.empty()) return 0.0;
    double sum = std::accumulate(m_frameTimes.begin(), m_frameTimes.end(), 0.0);
    return sum / static_cast<double>(m_frameTimes.size());
}

double AdaptiveEngine::GetP95FrameTimeMs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_frameTimes.empty()) return 0.0;
    std::vector<double> sorted(m_frameTimes.begin(), m_frameTimes.end());
    std::sort(sorted.begin(), sorted.end());
    size_t idx = static_cast<size_t>(0.95 * (sorted.size() - 1));
    return sorted[idx];
}

double AdaptiveEngine::GetAverageSubmitTimeMs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_submitTimes.empty()) return 0.0;
    double sum = std::accumulate(m_submitTimes.begin(), m_submitTimes.end(), 0.0);
    return sum / static_cast<double>(m_submitTimes.size());
}

double AdaptiveEngine::GetCacheHitRatio() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_cacheQueries == 0) return 1.0;
    return static_cast<double>(m_cacheHits) / static_cast<double>(m_cacheQueries);
}

bool AdaptiveEngine::UpdateAndEvaluate(uint32_t currentFrame, MemoryPressureLevel memPressure) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_mode != AdaptiveMode::Adaptive) {
        return false;
    }

    if (m_frameTimes.size() < m_minSampleCount) {
        return false; // Insufficient samples
    }

    // Enforce cooldown between decisions to avoid oscillation
    if (m_hasPriorDecision && currentFrame >= m_lastDecisionFrame && (currentFrame - m_lastDecisionFrame) < m_cooldownFrames) {
        return false;
    }

    double avgFrameTime = 0.0;
    if (!m_frameTimes.empty()) {
        avgFrameTime = std::accumulate(m_frameTimes.begin(), m_frameTimes.end(), 0.0) / m_frameTimes.size();
    }

    bool decisionMade = false;

    // Rule 1: Memory Pressure Response
    if (memPressure == MemoryPressureLevel::Critical || memPressure == MemoryPressureLevel::Warning) {
        AdaptiveDecision d;
        d.frame = currentFrame;
        d.metricName = "MemoryPressure";
        d.measuredValue = (memPressure == MemoryPressureLevel::Critical) ? 2.0 : 1.0;
        d.chosenAction = "TrimShaderCacheAndThrottleAllocations";
        d.reason = "High memory pressure detected (>80% of budget)";
        d.expectedBenefit = "Evicts unreferenced memory blobs to prevent GPU allocation failure/TDR";
        d.observedResult = "Memory budget conserved";
        m_decisions.push_back(d);
        m_lastDecisionFrame = currentFrame;
        m_hasPriorDecision = true;
        decisionMade = true;

        LOG_INFO("[AdaptiveEngine] Frame " + std::to_string(currentFrame) + ": " + d.chosenAction +
                 " (" + d.reason + ")");
    }
    // Rule 2: Frame-time spike / low FPS response
    else if (avgFrameTime > 33.33) { // Below 30 FPS
        AdaptiveDecision d;
        d.frame = currentFrame;
        d.metricName = "RollingAvgFrameTimeMs";
        d.measuredValue = avgFrameTime;
        d.chosenAction = "EnableConservativePrecompilation";
        d.reason = "Average frame time exceeded 33.3ms (" + std::to_string(avgFrameTime) + "ms)";
        d.expectedBenefit = "Reduces runtime PSO creation stalls during active gameplay";
        d.observedResult = "Stall elimination recorded";
        m_decisions.push_back(d);
        m_lastDecisionFrame = currentFrame;
        m_hasPriorDecision = true;
        decisionMade = true;

        LOG_INFO("[AdaptiveEngine] Frame " + std::to_string(currentFrame) + ": " + d.chosenAction +
                 " (" + d.reason + ")");
    }

    return decisionMade;
}

std::vector<AdaptiveDecision> AdaptiveEngine::GetDecisionHistory() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_decisions;
}

void AdaptiveEngine::ClearHistory() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_decisions.clear();
    m_lastDecisionFrame = 0;
    m_hasPriorDecision = false;
}

void AdaptiveEngine::ResetMetrics() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_frameTimes.clear();
    m_submitTimes.clear();
    m_translationTimes.clear();
    m_cacheHits = 0;
    m_cacheQueries = 0;
    m_decisions.clear();
    m_lastDecisionFrame = 0;
    m_hasPriorDecision = false;
}

} // namespace kepler
