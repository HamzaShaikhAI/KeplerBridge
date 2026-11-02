#include "downgrader/shader_autopsy.h"
#include "downgrader/dxil_translation.h"
#include "core/logger.h"
#include <windows.h>
#include <wincrypt.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace kepler {

static std::string ComputeSha256Hex(const void* data, size_t size) {
    if (!data || size == 0) return "0000000000000000000000000000000000000000000000000000000000000000";
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return "UNKNOWN_HASH";
    }
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return "UNKNOWN_HASH";
    }
    CryptHashData(hHash, reinterpret_cast<const BYTE*>(data), static_cast<DWORD>(size), 0);
    BYTE hashBytes[32] = {0};
    DWORD hashLen = sizeof(hashBytes);
    CryptGetHashParam(hHash, HP_HASHVAL, hashBytes, &hashLen, 0);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    std::ostringstream ss;
    for (DWORD i = 0; i < hashLen; ++i) {
        ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(hashBytes[i]);
    }
    return ss.str();
}

static std::string GetCurrentTimestampIso() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm;
#if defined(_WIN32)
    gmtime_s(&now_tm, &now_c);
#else
    gmtime_r(&now_c, &now_tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &now_tm);
    return std::string(buf);
}

ShaderAutopsy& ShaderAutopsy::Instance() {
    static ShaderAutopsy s_instance;
    return s_instance;
}

ShaderAutopsy::ShaderAutopsy() {
    Initialize();
}

void ShaderAutopsy::Initialize(const std::string& logFilePath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_jsonlPath = logFilePath;
}

void ShaderAutopsy::RecordShaderEvent(const ShaderAutopsyRecord& record) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_totalInspected++;
    if (record.translationSuccess) {
        m_totalTranslated++;
    } else {
        m_totalRejected++;
    }

    // Rate-limiting repeated identical shaders
    uint32_t count = ++m_seenShaderCounts[record.inputSha256];
    if (count > MAX_LOGGED_DUPLICATES) {
        return; // Suppress repeated duplicate diagnostic logs
    }

    if (m_history.size() >= MAX_HISTORY) {
        m_history.erase(m_history.begin());
    }
    m_history.push_back(record);

    // Append JSONL line
    if (!m_jsonlPath.empty()) {
        std::ofstream outfile(m_jsonlPath, std::ios::app);
        if (outfile.is_open()) {
            std::string jsonLine = FormatJson(record);
            // Replace newlines with spaces for single-line JSONL format
            for (char& c : jsonLine) {
                if (c == '\n' || c == '\r') c = ' ';
            }
            outfile << jsonLine << "\n";
        }
    }
}

bool ShaderAutopsy::PerformAutopsy(
    const void* pBytecode,
    size_t size,
    const std::string& correlationId,
    ShaderAutopsyRecord& outRecord)
{
    if (!pBytecode || size < 32) {
        outRecord.correlationId = correlationId;
        outRecord.timestamp = GetCurrentTimestampIso();
        outRecord.inputFormat = "INVALID";
        outRecord.failureStage = "Preflight";
        outRecord.failureReason = "Bytecode is null or smaller than 32 bytes";
        RecordShaderEvent(outRecord);
        return false;
    }

    outRecord.correlationId = correlationId;
    outRecord.timestamp = GetCurrentTimestampIso();
    outRecord.inputBytecodeSize = static_cast<uint32_t>(size);
    outRecord.inputSha256 = ComputeSha256Hex(pBytecode, size);

    const uint8_t* pRaw = reinterpret_cast<const uint8_t*>(pBytecode);
    uint32_t magic = *reinterpret_cast<const uint32_t*>(pRaw);
    if (magic != 0x43425844) { // "DXBC"
        outRecord.inputFormat = "NOT_DXBC";
        outRecord.failureStage = "ContainerValidation";
        outRecord.failureReason = "Missing DXBC magic header";
        RecordShaderEvent(outRecord);
        return false;
    }

    // Check for DXIL chunk vs SHEX chunk
    uint32_t chunkCount = *reinterpret_cast<const uint32_t*>(pRaw + 28);
    const uint32_t* pChunkOffsets = reinterpret_cast<const uint32_t*>(pRaw + 32);

    bool hasDxil = false;
    bool hasShex = false;
    uint64_t featureFlags = 0;
    std::vector<std::string> flagNames;

    for (uint32_t i = 0; i < chunkCount && (32 + (i + 1) * 4) <= size; ++i) {
        uint32_t offset = pChunkOffsets[i];
        if (offset + 8 > size) continue;
        uint32_t fourcc = *reinterpret_cast<const uint32_t*>(pRaw + offset);
        uint32_t chunkSize = *reinterpret_cast<const uint32_t*>(pRaw + offset + 4);

        if (fourcc == 0x4C495844) { // "DXIL"
            hasDxil = true;
        } else if (fourcc == 0x58454853) { // "SHEX"
            hasShex = true;
        } else if (fourcc == 0x30494653) { // "SFI0"
            if (offset + 8 + 8 <= size) {
                featureFlags = *reinterpret_cast<const uint64_t*>(pRaw + offset + 8);
            }
        }
    }

    outRecord.featureFlags = featureFlags;

    // Interpret SFI0 bits
    if (featureFlags & SFI_WAVE_OPS) flagNames.push_back("WaveOps");
    if (featureFlags & SFI_INT64_OPS) flagNames.push_back("Int64Ops");
    if (featureFlags & SFI_ROVS) flagNames.push_back("ROVs");
    if (featureFlags & SFI_VIEW_ID) flagNames.push_back("ViewID");
    if (featureFlags & SFI_BARYCENTRICS) flagNames.push_back("Barycentrics");
    if (featureFlags & SFI_NATIVE_LOW_PRECISION) flagNames.push_back("NativeLowPrecision");
    if (featureFlags & SFI_SHADING_RATE) flagNames.push_back("ShadingRate");
    if (featureFlags & SFI_RAYTRACING_TIER_1_1) flagNames.push_back("RaytracingTier1_1");
    if (featureFlags & SFI_SAMPLER_FEEDBACK) flagNames.push_back("SamplerFeedback");
    if (featureFlags & SFI_ATOMIC_INT64_TYPED) flagNames.push_back("AtomicInt64Typed");
    if (featureFlags & SFI_ATOMIC_INT64_GROUPSHARED) flagNames.push_back("AtomicInt64GroupShared");
    if (featureFlags & SFI_DERIVATIVES_IN_MESH_AMP) flagNames.push_back("DerivativesInMeshAmp");
    if (featureFlags & SFI_RESOURCE_DESCRIPTOR_HEAP) flagNames.push_back("ResourceDescriptorHeap");
    if (featureFlags & SFI_SAMPLER_DESCRIPTOR_HEAP) flagNames.push_back("SamplerDescriptorHeap");
    if (featureFlags & SFI_ATOMIC_INT64_HEAP) flagNames.push_back("AtomicInt64Heap");
    if (featureFlags & SFI_MINIMUM_PRECISION) flagNames.push_back("MinimumPrecision");
    if (featureFlags & SFI_TILED_RESOURCES) flagNames.push_back("TiledResources");
    if (featureFlags & SFI_TYPED_UAV_ADDITIONAL_FORMATS) flagNames.push_back("TypedUAVLoadAdditionalFormats");
    outRecord.featureFlagNames = flagNames;

    if (hasDxil) {
        outRecord.inputFormat = "DXIL";
        DXILTranslationReport report{};
        bool canTrans = DXILTranslationEngine::CanTranslate(pBytecode, size, &report);
        outRecord.shaderModelMajor = report.majorVersion;
        outRecord.shaderModelMinor = report.minorVersion;
        outRecord.stage = (report.stage == ShaderStage::Pixel) ? "Pixel" :
                          (report.stage == ShaderStage::Vertex) ? "Vertex" :
                          (report.stage == ShaderStage::Compute) ? "Compute" : "Unknown";
        outRecord.srvCount = report.declaredSRVs;
        outRecord.cbvCount = report.declaredCBVs;
        outRecord.samplerCount = report.declaredSamplers;

        if (report.hasWaveOps) {
            outRecord.unsupportedRequirements.push_back("WaveOps");
        }
        if (report.has64BitOps) {
            outRecord.unsupportedRequirements.push_back("64BitOps");
        }

        if (canTrans) {
            outRecord.translationRoute = "BoundedDXILToSM50";
            outRecord.targetProfile = (report.stage == ShaderStage::Pixel) ? "ps_5_0" :
                                      (report.stage == ShaderStage::Vertex) ? "vs_5_0" : "cs_5_0";
            outRecord.translationSuccess = true;
            outRecord.validationPassed = true;
        } else {
            outRecord.translationRoute = "Rejected";
            outRecord.failureStage = "DXILPreflight";
            outRecord.failureReason = report.diagnostic;
            outRecord.translationSuccess = false;
        }
    } else if (hasShex) {
        outRecord.inputFormat = "DXBC";
        outRecord.translationRoute = "SM51LoweringOrPassthrough";
        outRecord.translationSuccess = true;
        outRecord.validationPassed = true;
    } else {
        outRecord.inputFormat = "UNKNOWN_DXBC";
        outRecord.translationRoute = "Passthrough";
        outRecord.translationSuccess = true;
        outRecord.validationPassed = true;
    }

    RecordShaderEvent(outRecord);
    return outRecord.translationSuccess;
}

bool ShaderAutopsy::InspectFile(const std::string& filePath, ShaderAutopsyRecord& outRecord) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        outRecord.failureStage = "FileIO";
        outRecord.failureReason = "Could not open file: " + filePath;
        return false;
    }
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
        outRecord.failureStage = "FileIO";
        outRecord.failureReason = "Failed reading file contents: " + filePath;
        return false;
    }

    return PerformAutopsy(buffer.data(), buffer.size(), filePath, outRecord);
}

std::string ShaderAutopsy::FormatHumanReadable(const ShaderAutopsyRecord& record) const {
    std::ostringstream ss;
    ss << "=== KeplerBridge Shader Autopsy Report ===\n";
    ss << "Correlation ID : " << record.correlationId << "\n";
    ss << "Timestamp      : " << record.timestamp << "\n";
    ss << "Input Format   : " << record.inputFormat << " (" << record.stage << " SM "
       << record.shaderModelMajor << "." << record.shaderModelMinor << ")\n";
    ss << "Input SHA-256  : " << record.inputSha256 << " (" << record.inputBytecodeSize << " bytes)\n";
    ss << "Feature Flags  : 0x" << std::hex << record.featureFlags << std::dec;
    if (!record.featureFlagNames.empty()) {
        ss << " [";
        for (size_t i = 0; i < record.featureFlagNames.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << record.featureFlagNames[i];
        }
        ss << "]";
    }
    ss << "\n";
    ss << "Bindings       : " << record.srvCount << " SRVs, " << record.cbvCount << " CBVs, "
       << record.uavCount << " UAVs, " << record.samplerCount << " Samplers\n";
    ss << "Route / Target : " << record.translationRoute << " -> " << record.targetProfile << "\n";
    ss << "Status         : " << (record.translationSuccess ? "SUCCESS" : "REJECTED") << "\n";
    if (!record.translationSuccess) {
        ss << "Failure Stage  : " << record.failureStage << "\n";
        ss << "Failure Reason : " << record.failureReason << "\n";
    }
    if (!record.unsupportedRequirements.empty()) {
        ss << "Unsupported Req: ";
        for (const auto& u : record.unsupportedRequirements) ss << u << " ";
        ss << "\n";
    }
    ss << "==========================================\n";
    return ss.str();
}

std::string ShaderAutopsy::FormatJson(const ShaderAutopsyRecord& record) const {
    std::ostringstream ss;
    ss << "{"
       << "\"correlation_id\":\"" << record.correlationId << "\","
       << "\"timestamp\":\"" << record.timestamp << "\","
       << "\"input_format\":\"" << record.inputFormat << "\","
       << "\"sm_major\":" << record.shaderModelMajor << ","
       << "\"sm_minor\":" << record.shaderModelMinor << ","
       << "\"stage\":\"" << record.stage << "\","
       << "\"input_sha256\":\"" << record.inputSha256 << "\","
       << "\"input_size\":" << record.inputBytecodeSize << ","
       << "\"feature_flags\":" << record.featureFlags << ","
       << "\"feature_flag_names\":[";
    for (size_t i = 0; i < record.featureFlagNames.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "\"" << record.featureFlagNames[i] << "\"";
    }
    ss << "],"
       << "\"srv_count\":" << record.srvCount << ","
       << "\"cbv_count\":" << record.cbvCount << ","
       << "\"uav_count\":" << record.uavCount << ","
       << "\"sampler_count\":" << record.samplerCount << ","
       << "\"route\":\"" << record.translationRoute << "\","
       << "\"target_profile\":\"" << record.targetProfile << "\","
       << "\"duration_us\":" << record.durationUs << ","
       << "\"cache_status\":\"" << record.cacheStatus << "\","
       << "\"success\":" << (record.translationSuccess ? "true" : "false") << ","
       << "\"validation_passed\":" << (record.validationPassed ? "true" : "false") << ","
       << "\"failure_stage\":\"" << record.failureStage << "\","
       << "\"failure_reason\":\"" << record.failureReason << "\""
       << "}";
    return ss.str();
}

std::vector<ShaderAutopsyRecord> ShaderAutopsy::GetRecentRecords(size_t limit) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_history.empty()) return {};
    size_t start = (m_history.size() > limit) ? (m_history.size() - limit) : 0;
    return std::vector<ShaderAutopsyRecord>(m_history.begin() + start, m_history.end());
}

void ShaderAutopsy::ClearHistory() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_history.clear();
    m_seenShaderCounts.clear();
    m_totalInspected = 0;
    m_totalTranslated = 0;
    m_totalRejected = 0;
}

} // namespace kepler
