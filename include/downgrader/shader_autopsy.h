#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <cstdint>
#include "downgrader/shader_policy.h"

namespace kepler {

struct ShaderAutopsyRecord {
    std::string correlationId;        // Unique shader or PSO identifier
    std::string timestamp;            // Timestamp of the inspection
    std::string inputFormat;          // "DXBC", "DXIL", "UNKNOWN"
    uint32_t shaderModelMajor = 0;
    uint32_t shaderModelMinor = 0;
    std::string stage;                // "Vertex", "Pixel", "Compute", etc.
    std::string inputSha256;          // Cryptographic hash of source bytecode
    uint32_t inputBytecodeSize = 0;
    uint64_t featureFlags = 0;        // SFI0 or SM51 flags
    std::vector<std::string> featureFlagNames;
    uint32_t srvCount = 0;
    uint32_t cbvCount = 0;
    uint32_t uavCount = 0;
    uint32_t samplerCount = 0;
    std::string signatureSummary;
    std::vector<std::string> detectedOps;
    std::vector<std::string> unsupportedRequirements;
    std::string translationRoute;     // "SM51Lowering", "DXILtoSM50", "Passthrough", "Rejected"
    std::string targetProfile;        // "vs_5_0", "ps_5_0", "cs_5_0"
    uint64_t durationUs = 0;
    std::string cacheStatus;          // "Hit", "Miss", "Bypassed", "Stored"
    std::string outputSha256;
    uint32_t outputBytecodeSize = 0;
    bool translationSuccess = false;
    bool validationPassed = false;
    std::string failureStage;
    std::string failureReason;
};

class ShaderAutopsy {
public:
    static ShaderAutopsy& Instance();

    void Initialize(const std::string& logFilePath = "test_results/shader_autopsy.jsonl");

    // Capture an autopsy event
    void RecordShaderEvent(const ShaderAutopsyRecord& record);

    // Perform static autopsy on an in-memory shader blob
    bool PerformAutopsy(
        const void* pBytecode,
        size_t size,
        const std::string& correlationId,
        ShaderAutopsyRecord& outRecord);

    // Inspect a shader file on disk
    bool InspectFile(const std::string& filePath, ShaderAutopsyRecord& outRecord);

    // Export formats
    std::string FormatHumanReadable(const ShaderAutopsyRecord& record) const;
    std::string FormatJson(const ShaderAutopsyRecord& record) const;

    // Statistics and history
    std::vector<ShaderAutopsyRecord> GetRecentRecords(size_t limit = 100) const;
    void ClearHistory();

    uint64_t GetTotalInspected() const { return m_totalInspected; }
    uint64_t GetTotalTranslated() const { return m_totalTranslated; }
    uint64_t GetTotalRejected() const { return m_totalRejected; }

private:
    ShaderAutopsy();
    ~ShaderAutopsy() = default;
    ShaderAutopsy(const ShaderAutopsy&) = delete;
    ShaderAutopsy& operator=(const ShaderAutopsy&) = delete;

    mutable std::mutex m_mutex;
    std::string m_jsonlPath;
    std::vector<ShaderAutopsyRecord> m_history;
    std::unordered_map<std::string, uint32_t> m_seenShaderCounts; // Rate-limiting

    uint64_t m_totalInspected = 0;
    uint64_t m_totalTranslated = 0;
    uint64_t m_totalRejected = 0;
    static constexpr size_t MAX_HISTORY = 1000;
    static constexpr uint32_t MAX_LOGGED_DUPLICATES = 5;
};

} // namespace kepler
