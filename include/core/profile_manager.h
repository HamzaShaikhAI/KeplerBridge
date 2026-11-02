#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace kepler {

// ============================================================================
// CapabilityPolicy — Strongly typed, authentic hardware-respecting policy model
// ============================================================================
struct CapabilityPolicy {
    // General
    bool enableLogging = true;
    bool logVerboseCalls = false;

    // Feature Level & Hardware Advertisement
    bool enableFeatureLevelSpoof = false;
    std::string targetFeatureLevelStr = "12_0";
    D3D_FEATURE_LEVEL targetFeatureLevel = D3D_FEATURE_LEVEL_12_0;
    uint32_t spoofVRAMMB = 4096;

    // Translation Engine
    bool enableD3D11Translation = false;
    bool emulateBarriers = true;
    bool enableStateTracking = true;
    bool immediateQueueFlush = true;
    bool emulateQueries = true;

    // Downgrader & Resource Management
    bool enableShaderInspection = true;
    bool enableShaderFallback = false;
    bool enableTextureClamping = false;
    uint32_t maxTextureResolution = 1024;
    bool stripHeavyShaders = false;

    // Hardware Boundary Constraints (Kepler FL 11_0)
    uint32_t maxConcurrentUAVs = 8;        // Native Kepler FL 11_0 limit (u0..u7)
    bool safeFlipModeFallback = true;      // Auto-fallback FLIP modes to DISCARD on D3D11

    // Shader Disk Cache
    bool enableShaderDiskCache = true;
    std::string shaderCacheDir = "kepler_cache/shaders";

    // Milestone 7 & KeplerBridge 0.5 Extensions
    std::string targetExecutableHash;
    uint32_t memorySoftBudgetMB = 3072;
    bool enableFSR1Upscaling = false;
    float fsr1Sharpness = 0.8f;
    std::string fsr1Preset = "Quality";
    bool allowAdaptiveEngine = true;
    std::string diagnosticsLevel = "Standard";
    std::string profileType = "Conservative"; // "Conservative", "Manual", "Performance"
    std::string knownLimitations;

    // Profile Metadata
    std::string profileName = "Default";
    std::string description;
    std::string matchedExecutable;
    std::string selectionReason;
    bool isDefaultFallback = false;
    uint32_t schemaVersion = 2;
};

// ============================================================================
// Runtime Telemetry — Thread-safe, bounded telemetry recorder
// ============================================================================
enum class SessionOutcome {
    LoadedNotUsed = 1,
    InterceptedNoTranslation = 2,
    TranslatedNoRender = 3,
    TranslatedAndRendered = 4,
    FallbackOrFailure = 5
};

struct TelemetrySnapshot {
    uint64_t totalFramesPresented = 0;
    double averageFrameTimeMs = 0.0;
    double minFrameTimeMs = 0.0;
    double maxFrameTimeMs = 0.0;
    double p99FrameTimeMs = 0.0;
    uint32_t psoCompileCount = 0;
    uint32_t shaderCacheHits = 0;
    uint32_t shaderCacheMisses = 0;
    uint32_t peakUAVSlotBound = 0;
    HRESULT lastDeviceRemovalReason = S_OK;

    // Phase 12.12 Telemetry & Diagnostic Fields
    uint64_t apiCallsIntercepted = 0;
    uint32_t d3d12DevicesCreated = 0;
    uint32_t d3d11DevicesCreated = 0;
    uint32_t resourcesCreated = 0;
    uint32_t rootSignaturesCreated = 0;
    uint32_t dxilShadersEncountered = 0;
    uint32_t dxilShadersTranslated = 0;
    uint32_t dxilShadersRejected = 0;
    uint32_t dxbcValidationFailures = 0;
    uint32_t commandBatchesExecuted = 0;
    uint32_t fallbackEvents = 0;

    // Feature Level Contract
    D3D_FEATURE_LEVEL physicalFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL virtualizedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL requestedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL negotiatedFeatureLevel = D3D_FEATURE_LEVEL_11_0; // backward compat alias
    uint32_t hardwareBindingTier = 1;
    std::string hardwareHighestShaderModel = "5.1";

    // Presentation Deduplication & Pacing
    uint64_t occludedFramesPresented = 0;
    uint32_t failedPresentations = 0;
    uint64_t duplicatePresentationsAvoided = 0;

    // Presentation Semantics Breakdown (Phase 12.12.2)
    uint64_t presentAttempts = 0;
    uint64_t successfulPresentCalls = 0;
    uint64_t occludedPresentAttempts = 0;
    uint64_t visiblePresentSuccesses = 0;
    uint32_t presentFailures = 0;
    uint64_t presentTestCalls = 0;
    uint64_t renderedWorkSubmissions = 0;
    uint64_t completedTranslatedDraws = 0;
    uint64_t causallyRenderedVisibleFrames = 0;
    uint64_t causallyRenderedOccludedFrames = 0;

    // Causal Rendering Tracking
    uint64_t causalPsosCreated = 0;
    uint64_t causalPsosWithTranslatedShaders = 0;
    uint64_t causalPipelinesBound = 0;
    uint64_t causalDrawsRecorded = 0;
    uint64_t causalTranslatedDrawsRecorded = 0;
    uint64_t causalBatchesWithTranslatedWork = 0;
    uint64_t causallyRenderedFrames = 0;
    uint64_t lastCorrelatedPresentationId = 0;

    // Causal Event Identity Chain
    uint64_t lastCausalShaderHash = 0;
    uint64_t lastCausalTranslationId = 0;
    uint64_t lastCausalDxbcHash = 0;
    void* lastCausalPsoPtr = nullptr;
    void* lastCausalCmdListPtr = nullptr;
    bool lastCausalChainComplete = false;

    std::string sessionId;
    std::string dllPath;
    std::string dllBuildId = "KeplerBridge-0.5-Phase12.12.2";
    SessionOutcome outcome = SessionOutcome::LoadedNotUsed;
};

class RuntimeTelemetry {
public:
    static RuntimeTelemetry& Instance();

    bool RecordPresent(HRESULT hr, double frameTimeMs, void* pSwapChain = nullptr, uint64_t presentToken = 0, UINT flags = 0);
    void RecordPresent(double frameTimeMs);
    void RecordPSOCompilation(bool cacheHit, double compileTimeMs);
    void RecordUAVBinding(uint32_t slot);
    void RecordDeviceRemoval(HRESULT reason, const std::string& failingOp = "Unknown", const std::string& contextInfo = "");
    void SetLastSubmittedWork(uint64_t commandListId, const std::string& desc);
    void SetLastActivePipeline(uint64_t psoHash, const std::string& desc);
    uint32_t GetDeviceRemovalCount() const;

    // Presentation Semantics Queries (Phase 12.12.2)
    uint64_t GetPresentAttempts() const;
    uint64_t GetSuccessfulPresentCalls() const;
    uint64_t GetOccludedPresentAttempts() const;
    uint64_t GetVisiblePresentSuccesses() const;
    uint64_t GetPresentFailures() const;
    uint64_t GetPresentTestCalls() const;
    uint64_t GetRenderedWorkSubmissions() const;
    uint64_t GetCompletedTranslatedDraws() const;
    uint64_t GetCausallyRenderedVisibleFrames() const;
    uint64_t GetCausallyRenderedOccludedFrames() const;
    bool IsCausalChainComplete(bool requireVisible = true) const;

    // Phase 12.12 End-to-End Recording Primitives
    void RecordApiCall(const std::string& name = "");
    void RecordD3D12DeviceCreated(D3D_FEATURE_LEVEL physicalLevel, D3D_FEATURE_LEVEL virtualizedLevel = D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL requestedLevel = D3D_FEATURE_LEVEL_11_0);
    void RecordD3D11DeviceCreated();
    void RecordResourceCreated();
    void RecordRootSignatureCreated();
    void RecordDxilShaderEncountered();
    void RecordDxilShaderTranslated();
    void RecordDxilShaderRejected();
    void RecordDxbcValidationFailure();
    void RecordCommandBatchExecuted();
    void RecordFallbackEvent();
    void SetModuleInfo(const std::string& path, const std::string& buildId);

    // Phase 12.12.1 Causal Tracking Primitives
    uint64_t RecordCausalShaderEncountered(uint64_t shaderHash);
    uint64_t RecordCausalShaderTranslated(uint64_t shaderId, uint64_t dxbcHash, bool success);
    void RecordCausalDXBCValidated(uint64_t translationId, bool isValid);
    void RecordCausalPSOCreated(void* psoPtr, bool hasTranslatedShader);
    void RecordCausalPipelineBound(void* cmdListPtr, void* psoPtr);
    void RecordCausalDrawOrDispatch(void* cmdListPtr, bool isDraw);
    void RecordCausalBatchExecuted(const void* batchOrCmdListPtr);
    void SetUnpresentedTranslatedWork(bool flag = true);
    uint64_t GetCausallyRenderedFrames() const;
    uint64_t GetCausalTranslatedDrawCount() const;
    uint64_t GetTotalFramesPresented() const;
    void RegisterWrappedSwapChain(void* pSwapChain);
    void UnregisterWrappedSwapChain(void* pSwapChain);
    bool IsWrappedSwapChain(void* pSwapChain) const;

    SessionOutcome GetSessionOutcome() const;
    static std::string OutcomeToString(SessionOutcome outcome);
    std::string GetStructuredSessionReport() const;

    TelemetrySnapshot GetSnapshot() const;
    void Reset();
    void LogSummary() const;

private:
    RuntimeTelemetry();
    ~RuntimeTelemetry() = default;
    RuntimeTelemetry(const RuntimeTelemetry&) = delete;
    RuntimeTelemetry& operator=(const RuntimeTelemetry&) = delete;

    mutable std::mutex m_mutex;
    static constexpr size_t K_RING_BUFFER_SIZE = 128;
    double m_frameTimeRing[K_RING_BUFFER_SIZE] = { 0.0 };
    size_t m_ringIndex = 0;
    size_t m_ringCount = 0;

    std::atomic<uint64_t> m_totalFramesPresented{ 0 };
    std::atomic<uint32_t> m_psoCompileCount{ 0 };
    std::atomic<uint32_t> m_shaderCacheHits{ 0 };
    std::atomic<uint32_t> m_shaderCacheMisses{ 0 };
    std::atomic<uint32_t> m_peakUAVSlot{ 0 };
    std::atomic<HRESULT> m_lastDeviceRemoval{ S_OK };
    std::atomic<uint64_t> m_lastSubmittedCommandListId{ 0 };
    std::string m_lastSubmittedCommandListDesc;
    std::atomic<uint64_t> m_lastActivePipelineHash{ 0 };
    std::string m_lastActivePipelineDesc;
    std::atomic<uint32_t> m_deviceRemovalReportCount{ 0 };

    // Phase 12.12 Atomic Counters
    std::atomic<uint64_t> m_apiCallsIntercepted{ 0 };
    std::atomic<uint32_t> m_d3d12DevicesCreated{ 0 };
    std::atomic<uint32_t> m_d3d11DevicesCreated{ 0 };
    std::atomic<uint32_t> m_resourcesCreated{ 0 };
    std::atomic<uint32_t> m_rootSignaturesCreated{ 0 };
    std::atomic<uint32_t> m_dxilShadersEncountered{ 0 };
    std::atomic<uint32_t> m_dxilShadersTranslated{ 0 };
    std::atomic<uint32_t> m_dxilShadersRejected{ 0 };
    std::atomic<uint32_t> m_dxbcValidationFailures{ 0 };
    std::atomic<uint32_t> m_commandBatchesExecuted{ 0 };
    std::atomic<uint32_t> m_fallbackEvents{ 0 };
    std::atomic<D3D_FEATURE_LEVEL> m_physicalFeatureLevel{ D3D_FEATURE_LEVEL_11_0 };
    std::atomic<D3D_FEATURE_LEVEL> m_virtualizedFeatureLevel{ D3D_FEATURE_LEVEL_11_0 };
    std::atomic<D3D_FEATURE_LEVEL> m_requestedFeatureLevel{ D3D_FEATURE_LEVEL_11_0 };
    std::atomic<D3D_FEATURE_LEVEL> m_negotiatedFeatureLevel{ D3D_FEATURE_LEVEL_11_0 };
    std::atomic<uint32_t> m_hardwareBindingTier{ 1 };
    std::string m_hardwareHighestShaderModel{ "5.1" };
    std::string m_dllPath;
    std::string m_dllBuildId{ "KeplerBridge-0.5-Phase12.12.2" };
    std::string m_sessionId;

    // Presentation Deduplication & Pacing
    std::atomic<uint64_t> m_occludedFramesPresented{ 0 };
    std::atomic<uint32_t> m_failedPresentations{ 0 };
    std::atomic<uint64_t> m_duplicatePresentationsAvoided{ 0 };
    std::unordered_map<void*, uint64_t> m_lastPresentTokenBySwapChain;
    std::unordered_set<void*> m_registeredWrappedSwapChains;

    // Phase 12.12.2 Presentation Semantics Breakdown
    std::atomic<uint64_t> m_presentAttempts{ 0 };
    std::atomic<uint64_t> m_successfulPresentCalls{ 0 };
    std::atomic<uint64_t> m_occludedPresentAttempts{ 0 };
    std::atomic<uint64_t> m_visiblePresentSuccesses{ 0 };
    std::atomic<uint32_t> m_presentFailures{ 0 };
    std::atomic<uint64_t> m_presentTestCalls{ 0 };
    std::atomic<uint64_t> m_renderedWorkSubmissions{ 0 };
    std::atomic<uint64_t> m_completedTranslatedDraws{ 0 };
    std::atomic<uint64_t> m_causallyRenderedVisibleFrames{ 0 };
    std::atomic<uint64_t> m_causallyRenderedOccludedFrames{ 0 };

    // Phase 12.12.1 & Phase 12.12.2 Causal Event Tracking
    std::atomic<uint64_t> m_causalShaderIdGen{ 0 };
    std::atomic<uint64_t> m_causalTranslationIdGen{ 0 };
    std::atomic<uint64_t> m_causalPsosCreated{ 0 };
    std::atomic<uint64_t> m_causalPsosWithTranslatedShaders{ 0 };
    std::atomic<uint64_t> m_causalPipelinesBound{ 0 };
    std::atomic<uint64_t> m_causalDrawsRecorded{ 0 };
    std::atomic<uint64_t> m_causalTranslatedDrawsRecorded{ 0 };
    std::atomic<uint64_t> m_causalBatchesWithTranslatedWork{ 0 };
    std::atomic<bool>     m_unpresentedTranslatedWork{ false };
    std::atomic<uint64_t> m_causallyRenderedFrames{ 0 };
    std::atomic<uint64_t> m_lastCorrelatedPresentationId{ 0 };

    struct CausalIdentityChain {
        std::string sessionId;
        uint64_t shaderHash = 0;
        uint64_t translationId = 0;
        uint64_t dxbcHash = 0;
        void* psoPtr = nullptr;
        void* boundCmdList = nullptr;
        void* boundPso = nullptr;
        void* drawCmdList = nullptr;
        void* batchCmdList = nullptr;
        uint64_t presentationId = 0;
        bool drawExecuted = false;
        bool batchExecuted = false;
        bool visiblePresentationConfirmed = false;
        bool occludedPresentationRecorded = false;
    } m_causalChain;

    std::unordered_map<void*, bool> m_psoHasTranslatedShader;
    std::unordered_map<void*, void*> m_cmdListActivePso;
    std::unordered_map<void*, bool> m_cmdListHasTranslatedDraw;
};

// ============================================================================
// GameProfileManager — Title-specific configuration and capability mediation
// ============================================================================
class GameProfileManager {
public:
    static GameProfileManager& Instance();

    // Initialize with optional explicit host executable path and INI path
    bool Initialize(const std::string& hostExePath = "", const std::string& iniFilePath = "");

    // Capability Policy Access
    const CapabilityPolicy& GetActivePolicy() const;
    CapabilityPolicy& GetMutablePolicy(); // For runtime adjustments and unit testing

    // Telemetry Access
    RuntimeTelemetry& GetTelemetry() { return RuntimeTelemetry::Instance(); }

    // State Inspection
    std::string GetHostExecutableName() const { return m_hostExeName; }
    std::string GetHostExecutablePath() const { return m_hostExePath; }
    std::string GetConfigFilePath() const { return m_iniPath; }
    bool IsInitialized() const { return m_initialized; }

    void LogProfileDetails() const;

    // Static Utility Functions
    static std::string ExtractFileName(const std::string& fullPath);
    static std::string StripExtension(const std::string& fileName);
    static D3D_FEATURE_LEVEL ParseFeatureLevel(const std::string& flStr, D3D_FEATURE_LEVEL defaultLevel = D3D_FEATURE_LEVEL_11_0);
    static std::string FeatureLevelToString(D3D_FEATURE_LEVEL fl);
    static bool SectionExists(const std::string& sectionName, const std::string& iniPath);

    // Schema Validation & Preset Factories (Milestone 7)
    static bool ValidatePolicySchema(const CapabilityPolicy& policy, std::vector<std::string>& outErrors);
    static CapabilityPolicy CreateConservativeProfile(const std::string& name = "DefaultConservative");
    static CapabilityPolicy CreateManualProfile(const std::string& name = "ManualCompatibility");
    static CapabilityPolicy CreatePerformanceProfile(const std::string& name = "PerformanceMode");
    static bool MigrateSchema(CapabilityPolicy& policy, uint32_t fromVersion, uint32_t toVersion);

private:
    GameProfileManager() = default;
    ~GameProfileManager() = default;
    GameProfileManager(const GameProfileManager&) = delete;
    GameProfileManager& operator=(const GameProfileManager&) = delete;

    bool LoadProfileFromIni(const std::string& iniPath, const std::string& sectionName, CapabilityPolicy& outPolicy, bool isLegacyFlat);
    void SanitizePolicy(CapabilityPolicy& policy);

    mutable std::mutex m_mutex;
    bool m_initialized = false;
    std::string m_hostExePath;
    std::string m_hostExeName;
    std::string m_iniPath;
    CapabilityPolicy m_activePolicy;
};

} // namespace kepler
