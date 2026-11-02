#include "core/profile_manager.h"
#include "core/logger.h"

#include <numeric>
#include <sstream>
#include <iomanip>

namespace kepler {

// ============================================================================
// RuntimeTelemetry Implementation
// ============================================================================

RuntimeTelemetry& RuntimeTelemetry::Instance() {
    static RuntimeTelemetry s_instance;
    return s_instance;
}

RuntimeTelemetry::RuntimeTelemetry() {
    Reset();
}

void RuntimeTelemetry::Reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ringIndex = 0;
    m_ringCount = 0;
    for (size_t i = 0; i < K_RING_BUFFER_SIZE; ++i) {
        m_frameTimeRing[i] = 0.0;
    }
    m_totalFramesPresented.store(0);
    m_psoCompileCount.store(0);
    m_shaderCacheHits.store(0);
    m_shaderCacheMisses.store(0);
    m_peakUAVSlot.store(0);
    m_lastDeviceRemoval.store(S_OK);
    m_lastSubmittedCommandListId.store(0);
    m_lastSubmittedCommandListDesc.clear();
    m_lastActivePipelineHash.store(0);
    m_lastActivePipelineDesc.clear();
    m_deviceRemovalReportCount.store(0);

    m_apiCallsIntercepted.store(0);
    m_d3d12DevicesCreated.store(0);
    m_d3d11DevicesCreated.store(0);
    m_resourcesCreated.store(0);
    m_rootSignaturesCreated.store(0);
    m_dxilShadersEncountered.store(0);
    m_dxilShadersTranslated.store(0);
    m_dxilShadersRejected.store(0);
    m_dxbcValidationFailures.store(0);
    m_commandBatchesExecuted.store(0);
    m_fallbackEvents.store(0);
    m_physicalFeatureLevel.store(D3D_FEATURE_LEVEL_11_0);
    m_virtualizedFeatureLevel.store(D3D_FEATURE_LEVEL_11_0);
    m_requestedFeatureLevel.store(D3D_FEATURE_LEVEL_11_0);
    m_negotiatedFeatureLevel.store(D3D_FEATURE_LEVEL_11_0);

    // Presentation Deduplication & Pacing
    m_occludedFramesPresented.store(0);
    m_failedPresentations.store(0);
    m_duplicatePresentationsAvoided.store(0);
    m_lastPresentTokenBySwapChain.clear();
    m_registeredWrappedSwapChains.clear();

    // Presentation Semantics Breakdown (Phase 12.12.2)
    m_presentAttempts.store(0);
    m_successfulPresentCalls.store(0);
    m_occludedPresentAttempts.store(0);
    m_visiblePresentSuccesses.store(0);
    m_presentFailures.store(0);
    m_presentTestCalls.store(0);
    m_renderedWorkSubmissions.store(0);
    m_completedTranslatedDraws.store(0);
    m_causallyRenderedVisibleFrames.store(0);
    m_causallyRenderedOccludedFrames.store(0);

    // Causal Tracking
    m_causalShaderIdGen.store(0);
    m_causalTranslationIdGen.store(0);
    m_causalPsosCreated.store(0);
    m_causalPsosWithTranslatedShaders.store(0);
    m_causalPipelinesBound.store(0);
    m_causalDrawsRecorded.store(0);
    m_causalTranslatedDrawsRecorded.store(0);
    m_causalBatchesWithTranslatedWork.store(0);
    m_unpresentedTranslatedWork.store(false);
    m_causallyRenderedFrames.store(0);
    m_lastCorrelatedPresentationId.store(0);

    m_psoHasTranslatedShader.clear();
    m_cmdListActivePso.clear();
    m_cmdListHasTranslatedDraw.clear();

    std::ostringstream ss;
    ss << "KB-SESSION-" << std::hex << GetTickCount64();
    m_sessionId = ss.str();
    m_causalChain = CausalIdentityChain{};
    m_causalChain.sessionId = m_sessionId;
}

void RuntimeTelemetry::RecordApiCall(const std::string& name) {
    (void)name;
    m_apiCallsIntercepted.fetch_add(1, std::memory_order_relaxed);
}

void RuntimeTelemetry::RecordD3D12DeviceCreated(D3D_FEATURE_LEVEL physicalLevel, D3D_FEATURE_LEVEL virtualizedLevel, D3D_FEATURE_LEVEL requestedLevel) {
    m_d3d12DevicesCreated.fetch_add(1, std::memory_order_relaxed);
    m_physicalFeatureLevel.store(physicalLevel, std::memory_order_relaxed);
    m_virtualizedFeatureLevel.store(virtualizedLevel, std::memory_order_relaxed);
    m_requestedFeatureLevel.store(requestedLevel, std::memory_order_relaxed);
    m_negotiatedFeatureLevel.store(virtualizedLevel, std::memory_order_relaxed);
}

void RuntimeTelemetry::RecordD3D11DeviceCreated() {
    m_d3d11DevicesCreated.fetch_add(1, std::memory_order_relaxed);
}

void RuntimeTelemetry::RecordResourceCreated() {
    m_resourcesCreated.fetch_add(1, std::memory_order_relaxed);
}

void RuntimeTelemetry::RecordRootSignatureCreated() {
    m_rootSignaturesCreated.fetch_add(1, std::memory_order_relaxed);
}

void RuntimeTelemetry::RecordDxilShaderEncountered() {
    m_dxilShadersEncountered.fetch_add(1, std::memory_order_relaxed);
}

void RuntimeTelemetry::RecordDxilShaderTranslated() {
    m_dxilShadersTranslated.fetch_add(1, std::memory_order_relaxed);
}

void RuntimeTelemetry::RecordDxilShaderRejected() {
    m_dxilShadersRejected.fetch_add(1, std::memory_order_relaxed);
}

void RuntimeTelemetry::RecordDxbcValidationFailure() {
    m_dxbcValidationFailures.fetch_add(1, std::memory_order_relaxed);
}

void RuntimeTelemetry::RecordCommandBatchExecuted() {
    m_commandBatchesExecuted.fetch_add(1, std::memory_order_relaxed);
}

void RuntimeTelemetry::RecordFallbackEvent() {
    m_fallbackEvents.fetch_add(1, std::memory_order_relaxed);
}

uint64_t RuntimeTelemetry::RecordCausalShaderEncountered(uint64_t shaderHash) {
    RecordDxilShaderEncountered();
    uint64_t id = m_causalShaderIdGen.fetch_add(1, std::memory_order_relaxed) + 1;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_causalChain = CausalIdentityChain{};
        m_causalChain.sessionId = m_sessionId;
        m_causalChain.shaderHash = shaderHash;
        m_causalChain.translationId = id;
    }
    return id;
}

uint64_t RuntimeTelemetry::RecordCausalShaderTranslated(uint64_t shaderId, uint64_t dxbcHash, bool success) {
    if (success) {
        RecordDxilShaderTranslated();
        uint64_t transId = m_causalTranslationIdGen.fetch_add(1, std::memory_order_relaxed) + 1;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_causalChain.translationId == shaderId) {
                m_causalChain.dxbcHash = dxbcHash;
            }
        }
        return transId;
    } else {
        RecordDxilShaderRejected();
        return 0;
    }
}

void RuntimeTelemetry::RecordCausalDXBCValidated(uint64_t translationId, bool isValid) {
    (void)translationId;
    if (!isValid) {
        RecordDxbcValidationFailure();
    }
}

void RuntimeTelemetry::RecordCausalPSOCreated(void* psoPtr, bool hasTranslatedShader) {
    m_causalPsosCreated.fetch_add(1, std::memory_order_relaxed);
    if (hasTranslatedShader) {
        m_causalPsosWithTranslatedShaders.fetch_add(1, std::memory_order_relaxed);
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (psoPtr) {
        m_psoHasTranslatedShader[psoPtr] = hasTranslatedShader;
        if (hasTranslatedShader && m_causalChain.dxbcHash != 0) {
            m_causalChain.psoPtr = psoPtr;
        }
    }
}

void RuntimeTelemetry::RecordCausalPipelineBound(void* cmdListPtr, void* psoPtr) {
    m_causalPipelinesBound.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (cmdListPtr) {
        m_cmdListActivePso[cmdListPtr] = psoPtr;
        if (psoPtr && psoPtr == m_causalChain.psoPtr) {
            m_causalChain.boundCmdList = cmdListPtr;
            m_causalChain.boundPso = psoPtr;
        }
    }
}

void RuntimeTelemetry::RecordCausalDrawOrDispatch(void* cmdListPtr, bool isDraw) {
    (void)isDraw;
    m_causalDrawsRecorded.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (cmdListPtr) {
        auto it = m_cmdListActivePso.find(cmdListPtr);
        if (it != m_cmdListActivePso.end() && it->second) {
            auto itPso = m_psoHasTranslatedShader.find(it->second);
            if (itPso != m_psoHasTranslatedShader.end() && itPso->second) {
                m_cmdListHasTranslatedDraw[cmdListPtr] = true;
                m_causalTranslatedDrawsRecorded.fetch_add(1, std::memory_order_relaxed);
                m_completedTranslatedDraws.fetch_add(1, std::memory_order_relaxed);
                if (cmdListPtr == m_causalChain.boundCmdList) {
                    m_causalChain.drawCmdList = cmdListPtr;
                    m_causalChain.drawExecuted = true;
                }
            }
        }
    }
}

void RuntimeTelemetry::RecordCausalBatchExecuted(const void* batchOrCmdListPtr) {
    bool hasWork = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (batchOrCmdListPtr) {
            auto it = m_cmdListHasTranslatedDraw.find(const_cast<void*>(batchOrCmdListPtr));
            if (it != m_cmdListHasTranslatedDraw.end() && it->second) {
                hasWork = true;
                m_renderedWorkSubmissions.fetch_add(1, std::memory_order_relaxed);
                if (batchOrCmdListPtr == m_causalChain.drawCmdList) {
                    m_causalChain.batchCmdList = const_cast<void*>(batchOrCmdListPtr);
                    m_causalChain.batchExecuted = true;
                }
            }
        }
    }
    if (hasWork) {
        m_unpresentedTranslatedWork.store(true, std::memory_order_release);
        m_causalBatchesWithTranslatedWork.fetch_add(1, std::memory_order_relaxed);
    }
}

void RuntimeTelemetry::SetUnpresentedTranslatedWork(bool flag) {
    m_unpresentedTranslatedWork.store(flag, std::memory_order_release);
    if (flag) {
        m_causalBatchesWithTranslatedWork.fetch_add(1, std::memory_order_relaxed);
    }
}

uint64_t RuntimeTelemetry::GetCausallyRenderedFrames() const {
    return m_causallyRenderedFrames.load(std::memory_order_relaxed);
}

uint64_t RuntimeTelemetry::GetCausalTranslatedDrawCount() const {
    return m_causalTranslatedDrawsRecorded.load(std::memory_order_relaxed);
}

uint64_t RuntimeTelemetry::GetTotalFramesPresented() const {
    return m_totalFramesPresented.load(std::memory_order_relaxed);
}

void RuntimeTelemetry::RegisterWrappedSwapChain(void* pSwapChain) {
    if (!pSwapChain) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_registeredWrappedSwapChains.insert(pSwapChain);
}

void RuntimeTelemetry::UnregisterWrappedSwapChain(void* pSwapChain) {
    if (!pSwapChain) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_registeredWrappedSwapChains.erase(pSwapChain);
}

bool RuntimeTelemetry::IsWrappedSwapChain(void* pSwapChain) const {
    if (!pSwapChain) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_registeredWrappedSwapChains.find(pSwapChain) != m_registeredWrappedSwapChains.end();
}

void RuntimeTelemetry::SetModuleInfo(const std::string& path, const std::string& buildId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dllPath = path;
    if (!buildId.empty()) {
        m_dllBuildId = buildId;
    }
}

SessionOutcome RuntimeTelemetry::GetSessionOutcome() const {
    if (m_lastDeviceRemoval.load(std::memory_order_relaxed) != S_OK ||
        m_dxbcValidationFailures.load(std::memory_order_relaxed) > 0 ||
        m_dxilShadersRejected.load(std::memory_order_relaxed) > 0 ||
        m_fallbackEvents.load(std::memory_order_relaxed) > 0)
    {
        return SessionOutcome::FallbackOrFailure;
    }
    // Outcome 4 strictly requires causal execution of translated shader work AND correlated presentation
    if ((m_causallyRenderedVisibleFrames.load(std::memory_order_relaxed) > 0 ||
         m_causallyRenderedOccludedFrames.load(std::memory_order_relaxed) > 0 ||
         m_causallyRenderedFrames.load(std::memory_order_relaxed) > 0) &&
        m_dxilShadersTranslated.load(std::memory_order_relaxed) > 0)
    {
        return SessionOutcome::TranslatedAndRendered;
    }
    if (m_dxilShadersTranslated.load(std::memory_order_relaxed) > 0 ||
        m_psoCompileCount.load(std::memory_order_relaxed) > 0)
    {
        return SessionOutcome::TranslatedNoRender;
    }
    if (m_apiCallsIntercepted.load(std::memory_order_relaxed) > 0 ||
        m_d3d12DevicesCreated.load(std::memory_order_relaxed) > 0)
    {
        return SessionOutcome::InterceptedNoTranslation;
    }
    return SessionOutcome::LoadedNotUsed;
}

std::string RuntimeTelemetry::OutcomeToString(SessionOutcome outcome) {
    switch (outcome) {
        case SessionOutcome::LoadedNotUsed:
            return "1: KeplerBridge loaded but not used";
        case SessionOutcome::InterceptedNoTranslation:
            return "2: KeplerBridge intercepted API calls but did not translate shaders";
        case SessionOutcome::TranslatedNoRender:
            return "3: KeplerBridge successfully translated shaders but did not execute a rendered frame";
        case SessionOutcome::TranslatedAndRendered:
            if (Instance().GetCausallyRenderedVisibleFrames() > 0) {
                return "4: KeplerBridge translated and executed rendering workloads successfully (Visible Presentation Confirmed)";
            } else if (Instance().GetCausallyRenderedOccludedFrames() > 0) {
                return "4: KeplerBridge translated and executed rendering workloads successfully (Occluded Presentation - Work Completed, No Visible Frame Confirmed)";
            } else {
                return "4: KeplerBridge translated and executed rendering workloads successfully";
            }
        case SessionOutcome::FallbackOrFailure:
            return "5: KeplerBridge encountered fallback, unsupported functionality, or runtime failure";
        default:
            return "Unknown";
    }
}

bool RuntimeTelemetry::RecordPresent(HRESULT hr, double frameTimeMs, void* pSwapChain, uint64_t presentToken, UINT flags) {
    m_presentAttempts.fetch_add(1, std::memory_order_relaxed);
    if (flags & 0x00000001UL /* DXGI_PRESENT_TEST */) {
        m_presentTestCalls.fetch_add(1, std::memory_order_relaxed);
    }

    if (FAILED(hr)) {
        m_presentFailures.fetch_add(1, std::memory_order_relaxed);
        m_failedPresentations.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    m_successfulPresentCalls.fetch_add(1, std::memory_order_relaxed);
    if (hr == DXGI_STATUS_OCCLUDED) {
        m_occludedPresentAttempts.fetch_add(1, std::memory_order_relaxed);
        m_occludedFramesPresented.fetch_add(1, std::memory_order_relaxed);
    } else if (hr == S_OK) {
        if (!(flags & 0x00000001UL /* DXGI_PRESENT_TEST */)) {
            m_visiblePresentSuccesses.fetch_add(1, std::memory_order_relaxed);
        }
    }

    if (pSwapChain != nullptr && presentToken != 0) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_lastPresentTokenBySwapChain.find(pSwapChain);
        if (it != m_lastPresentTokenBySwapChain.end() && it->second == presentToken) {
            m_duplicatePresentationsAvoided.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        m_lastPresentTokenBySwapChain[pSwapChain] = presentToken;
    }

    // A test present call checks occlusion/readiness but does NOT display a frame or correlate work
    if (flags & 0x00000001UL /* DXGI_PRESENT_TEST */) {
        return true;
    }

    uint64_t presId = m_totalFramesPresented.fetch_add(1, std::memory_order_relaxed) + 1;

    // Causal linking: if unpresented translated GPU work is pending, correlate it now
    if (m_unpresentedTranslatedWork.exchange(false, std::memory_order_acq_rel)) {
        m_lastCorrelatedPresentationId.store(presId, std::memory_order_relaxed);
        m_causallyRenderedFrames.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_causalChain.presentationId = presId;
        if (hr == S_OK) {
            m_causallyRenderedVisibleFrames.fetch_add(1, std::memory_order_relaxed);
            m_causalChain.visiblePresentationConfirmed = true;
        } else if (hr == DXGI_STATUS_OCCLUDED) {
            m_causallyRenderedOccludedFrames.fetch_add(1, std::memory_order_relaxed);
            m_causalChain.occludedPresentationRecorded = true;
        }
    }

    if (frameTimeMs > 0.0) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frameTimeRing[m_ringIndex] = frameTimeMs;
        m_ringIndex = (m_ringIndex + 1) % K_RING_BUFFER_SIZE;
        if (m_ringCount < K_RING_BUFFER_SIZE) {
            m_ringCount++;
        }
    }
    return true;
}

void RuntimeTelemetry::RecordPresent(double frameTimeMs) {
    RecordPresent(S_OK, frameTimeMs, nullptr, 0, 0);
}

uint64_t RuntimeTelemetry::GetPresentAttempts() const {
    return m_presentAttempts.load(std::memory_order_relaxed);
}

uint64_t RuntimeTelemetry::GetSuccessfulPresentCalls() const {
    return m_successfulPresentCalls.load(std::memory_order_relaxed);
}

uint64_t RuntimeTelemetry::GetOccludedPresentAttempts() const {
    return m_occludedPresentAttempts.load(std::memory_order_relaxed);
}

uint64_t RuntimeTelemetry::GetVisiblePresentSuccesses() const {
    return m_visiblePresentSuccesses.load(std::memory_order_relaxed);
}

uint64_t RuntimeTelemetry::GetPresentFailures() const {
    return m_presentFailures.load(std::memory_order_relaxed);
}

uint64_t RuntimeTelemetry::GetPresentTestCalls() const {
    return m_presentTestCalls.load(std::memory_order_relaxed);
}

uint64_t RuntimeTelemetry::GetRenderedWorkSubmissions() const {
    return m_renderedWorkSubmissions.load(std::memory_order_relaxed);
}

uint64_t RuntimeTelemetry::GetCompletedTranslatedDraws() const {
    return m_completedTranslatedDraws.load(std::memory_order_relaxed);
}

uint64_t RuntimeTelemetry::GetCausallyRenderedVisibleFrames() const {
    return m_causallyRenderedVisibleFrames.load(std::memory_order_relaxed);
}

uint64_t RuntimeTelemetry::GetCausallyRenderedOccludedFrames() const {
    return m_causallyRenderedOccludedFrames.load(std::memory_order_relaxed);
}

bool RuntimeTelemetry::IsCausalChainComplete(bool requireVisible) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_causalChain.shaderHash == 0 ||
        m_causalChain.translationId == 0 ||
        m_causalChain.dxbcHash == 0 ||
        m_causalChain.psoPtr == nullptr ||
        m_causalChain.boundPso != m_causalChain.psoPtr ||
        m_causalChain.boundCmdList == nullptr ||
        m_causalChain.drawCmdList != m_causalChain.boundCmdList ||
        !m_causalChain.drawExecuted ||
        m_causalChain.batchCmdList != m_causalChain.drawCmdList ||
        !m_causalChain.batchExecuted ||
        m_causalChain.presentationId == 0)
    {
        return false;
    }
    if (requireVisible) {
        return m_causalChain.visiblePresentationConfirmed;
    }
    return (m_causalChain.visiblePresentationConfirmed || m_causalChain.occludedPresentationRecorded);
}

void RuntimeTelemetry::RecordPSOCompilation(bool cacheHit, double compileTimeMs) {
    (void)compileTimeMs;
    m_psoCompileCount.fetch_add(1, std::memory_order_relaxed);
    if (cacheHit) {
        m_shaderCacheHits.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_shaderCacheMisses.fetch_add(1, std::memory_order_relaxed);
    }
}

void RuntimeTelemetry::RecordUAVBinding(uint32_t slot) {
    uint32_t current = m_peakUAVSlot.load(std::memory_order_relaxed);
    while (slot > current && !m_peakUAVSlot.compare_exchange_weak(current, slot, std::memory_order_relaxed)) {
        // CAS retry
    }
}

void RuntimeTelemetry::SetLastSubmittedWork(uint64_t commandListId, const std::string& desc) {
    m_lastSubmittedCommandListId.store(commandListId, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastSubmittedCommandListDesc = desc;
}

void RuntimeTelemetry::SetLastActivePipeline(uint64_t psoHash, const std::string& desc) {
    m_lastActivePipelineHash.store(psoHash, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastActivePipelineDesc = desc;
}

uint32_t RuntimeTelemetry::GetDeviceRemovalCount() const {
    return m_deviceRemovalReportCount.load(std::memory_order_relaxed);
}

void RuntimeTelemetry::RecordDeviceRemoval(HRESULT reason, const std::string& failingOp, const std::string& contextInfo) {
    m_lastDeviceRemoval.store(reason, std::memory_order_release);
    uint32_t count = ++m_deviceRemovalReportCount;

    if (count <= 5) {
        DiagnosticReport rep;
        rep.errorCode = (reason == DXGI_ERROR_DEVICE_HUNG) ? KeplerErrorCode::DeviceTimeout : KeplerErrorCode::DeviceRemoved;
        rep.severity = ErrorSeverity::Fatal;
        rep.subsystem = "DeviceRemovalDiagnostics";
        rep.apiOperation = failingOp;
        rep.nativeHResult = reason;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            rep.submittedWorkInfo = m_lastSubmittedCommandListDesc.empty() ?
                ("CmdList ID: " + std::to_string(m_lastSubmittedCommandListId.load(std::memory_order_relaxed))) :
                m_lastSubmittedCommandListDesc;
            rep.pipelineInfo = m_lastActivePipelineDesc.empty() ?
                ("PSO Hash: 0x" + Logger::HexToString(m_lastActivePipelineHash.load(std::memory_order_relaxed))) :
                m_lastActivePipelineDesc;
        }

        rep.disposition = ExecutionDisposition::Rejected;
        rep.explanation = "GPU device removal, hung, or reset detected (" + Logger::HResultToString(reason) + 
                          "). Context: " + (contextInfo.empty() ? "None" : contextInfo) +
                          ". Note: Recorded work represents diagnostic telemetry, not proven causality.";
        rep.actionableHint = "A GPU TDR or hardware reset occurred. Review thermals, clock stability, command size, and shader complexity.";
        Logger::Instance().ReportDiagnostic(rep);
    } else if (count == 6) {
        LOG_WARN("[TELEMETRY] Device removal reports rate-limited (suppressing repeated log output). Total occurrences: " + std::to_string(count));
    }
}

TelemetrySnapshot RuntimeTelemetry::GetSnapshot() const {
    TelemetrySnapshot snap;
    snap.totalFramesPresented = m_totalFramesPresented.load(std::memory_order_relaxed);
    snap.psoCompileCount = m_psoCompileCount.load(std::memory_order_relaxed);
    snap.shaderCacheHits = m_shaderCacheHits.load(std::memory_order_relaxed);
    snap.shaderCacheMisses = m_shaderCacheMisses.load(std::memory_order_relaxed);
    snap.peakUAVSlotBound = m_peakUAVSlot.load(std::memory_order_relaxed);
    snap.lastDeviceRemovalReason = m_lastDeviceRemoval.load(std::memory_order_acquire);

    snap.apiCallsIntercepted = m_apiCallsIntercepted.load(std::memory_order_relaxed);
    snap.d3d12DevicesCreated = m_d3d12DevicesCreated.load(std::memory_order_relaxed);
    snap.d3d11DevicesCreated = m_d3d11DevicesCreated.load(std::memory_order_relaxed);
    snap.resourcesCreated = m_resourcesCreated.load(std::memory_order_relaxed);
    snap.rootSignaturesCreated = m_rootSignaturesCreated.load(std::memory_order_relaxed);
    snap.dxilShadersEncountered = m_dxilShadersEncountered.load(std::memory_order_relaxed);
    snap.dxilShadersTranslated = m_dxilShadersTranslated.load(std::memory_order_relaxed);
    snap.dxilShadersRejected = m_dxilShadersRejected.load(std::memory_order_relaxed);
    snap.dxbcValidationFailures = m_dxbcValidationFailures.load(std::memory_order_relaxed);
    snap.commandBatchesExecuted = m_commandBatchesExecuted.load(std::memory_order_relaxed);
    snap.fallbackEvents = m_fallbackEvents.load(std::memory_order_relaxed);
    snap.physicalFeatureLevel = m_physicalFeatureLevel.load(std::memory_order_relaxed);
    snap.virtualizedFeatureLevel = m_virtualizedFeatureLevel.load(std::memory_order_relaxed);
    snap.requestedFeatureLevel = m_requestedFeatureLevel.load(std::memory_order_relaxed);
    snap.negotiatedFeatureLevel = snap.virtualizedFeatureLevel;
    snap.hardwareBindingTier = m_hardwareBindingTier.load(std::memory_order_relaxed);
    snap.occludedFramesPresented = m_occludedFramesPresented.load(std::memory_order_relaxed);
    snap.failedPresentations = m_failedPresentations.load(std::memory_order_relaxed);
    snap.duplicatePresentationsAvoided = m_duplicatePresentationsAvoided.load(std::memory_order_relaxed);

    // Presentation Semantics Breakdown (Phase 12.12.2)
    snap.presentAttempts = m_presentAttempts.load(std::memory_order_relaxed);
    snap.successfulPresentCalls = m_successfulPresentCalls.load(std::memory_order_relaxed);
    snap.occludedPresentAttempts = m_occludedPresentAttempts.load(std::memory_order_relaxed);
    snap.visiblePresentSuccesses = m_visiblePresentSuccesses.load(std::memory_order_relaxed);
    snap.presentFailures = m_presentFailures.load(std::memory_order_relaxed);
    snap.presentTestCalls = m_presentTestCalls.load(std::memory_order_relaxed);
    snap.renderedWorkSubmissions = m_renderedWorkSubmissions.load(std::memory_order_relaxed);
    snap.completedTranslatedDraws = m_completedTranslatedDraws.load(std::memory_order_relaxed);
    snap.causallyRenderedVisibleFrames = m_causallyRenderedVisibleFrames.load(std::memory_order_relaxed);
    snap.causallyRenderedOccludedFrames = m_causallyRenderedOccludedFrames.load(std::memory_order_relaxed);

    snap.causalPsosCreated = m_causalPsosCreated.load(std::memory_order_relaxed);
    snap.causalPsosWithTranslatedShaders = m_causalPsosWithTranslatedShaders.load(std::memory_order_relaxed);
    snap.causalPipelinesBound = m_causalPipelinesBound.load(std::memory_order_relaxed);
    snap.causalDrawsRecorded = m_causalDrawsRecorded.load(std::memory_order_relaxed);
    snap.causalTranslatedDrawsRecorded = m_causalTranslatedDrawsRecorded.load(std::memory_order_relaxed);
    snap.causalBatchesWithTranslatedWork = m_causalBatchesWithTranslatedWork.load(std::memory_order_relaxed);
    snap.causallyRenderedFrames = m_causallyRenderedFrames.load(std::memory_order_relaxed);
    snap.lastCorrelatedPresentationId = m_lastCorrelatedPresentationId.load(std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        snap.lastCausalShaderHash = m_causalChain.shaderHash;
        snap.lastCausalTranslationId = m_causalChain.translationId;
        snap.lastCausalDxbcHash = m_causalChain.dxbcHash;
        snap.lastCausalPsoPtr = m_causalChain.psoPtr;
        snap.lastCausalCmdListPtr = m_causalChain.drawCmdList;
        snap.lastCausalChainComplete = (m_causalChain.shaderHash != 0 &&
                                       m_causalChain.translationId != 0 &&
                                       m_causalChain.dxbcHash != 0 &&
                                       m_causalChain.psoPtr != nullptr &&
                                       m_causalChain.boundPso == m_causalChain.psoPtr &&
                                       m_causalChain.boundCmdList != nullptr &&
                                       m_causalChain.drawCmdList == m_causalChain.boundCmdList &&
                                       m_causalChain.drawExecuted &&
                                       m_causalChain.batchCmdList == m_causalChain.drawCmdList &&
                                       m_causalChain.batchExecuted &&
                                       m_causalChain.presentationId != 0 &&
                                       (m_causalChain.visiblePresentationConfirmed || m_causalChain.occludedPresentationRecorded));
    }

    snap.outcome = GetSessionOutcome();

    std::vector<double> samples;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        snap.sessionId = m_sessionId;
        snap.dllPath = m_dllPath;
        snap.dllBuildId = m_dllBuildId;
        snap.hardwareHighestShaderModel = m_hardwareHighestShaderModel;
        if (m_ringCount > 0) {
            samples.assign(m_frameTimeRing, m_frameTimeRing + m_ringCount);
        }
    }

    if (!samples.empty()) {
        std::sort(samples.begin(), samples.end());
        snap.minFrameTimeMs = samples.front();
        snap.maxFrameTimeMs = samples.back();
        double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
        snap.averageFrameTimeMs = sum / static_cast<double>(samples.size());

        size_t p99Index = static_cast<size_t>(static_cast<double>(samples.size() - 1) * 0.99);
        snap.p99FrameTimeMs = samples[p99Index];
    }

    return snap;
}

std::string RuntimeTelemetry::GetStructuredSessionReport() const {
    TelemetrySnapshot s = GetSnapshot();
    std::ostringstream ss;
    ss << "{\n"
       << "  \"module\": {\n"
       << "    \"dll_path\": \"" << s.dllPath << "\",\n"
       << "    \"build_id\": \"" << s.dllBuildId << "\"\n"
       << "  },\n"
       << "  \"session\": {\n"
       << "    \"session_id\": \"" << s.sessionId << "\",\n"
       << "    \"outcome_code\": " << static_cast<int>(s.outcome) << ",\n"
       << "    \"outcome_description\": \"" << OutcomeToString(s.outcome) << "\",\n"
       << "    \"negotiated_feature_level\": \"0x" << std::hex << s.negotiatedFeatureLevel << std::dec << "\"\n"
       << "  },\n"
       << "  \"feature_level_contract\": {\n"
       << "    \"physical_feature_level\": \"0x" << std::hex << s.physicalFeatureLevel << std::dec << "\",\n"
       << "    \"virtualized_feature_level\": \"0x" << std::hex << s.virtualizedFeatureLevel << std::dec << "\",\n"
       << "    \"requested_feature_level\": \"0x" << std::hex << s.requestedFeatureLevel << std::dec << "\",\n"
       << "    \"hardware_binding_tier\": " << s.hardwareBindingTier << ",\n"
       << "    \"hardware_highest_shader_model\": \"" << s.hardwareHighestShaderModel << "\"\n"
       << "  },\n"
       << "  \"interception\": {\n"
       << "    \"api_calls_intercepted\": " << s.apiCallsIntercepted << ",\n"
       << "    \"d3d12_devices_created\": " << s.d3d12DevicesCreated << ",\n"
       << "    \"d3d11_devices_created\": " << s.d3d11DevicesCreated << ",\n"
       << "    \"resources_created\": " << s.resourcesCreated << ",\n"
       << "    \"root_signatures_created\": " << s.rootSignaturesCreated << ",\n"
       << "    \"command_batches_executed\": " << s.commandBatchesExecuted << ",\n"
       << "    \"frames_presented\": " << s.totalFramesPresented << "\n"
       << "  },\n"
       << "  \"presentation\": {\n"
       << "    \"total_frames_presented\": " << s.totalFramesPresented << ",\n"
       << "    \"present_attempts\": " << s.presentAttempts << ",\n"
       << "    \"successful_present_calls\": " << s.successfulPresentCalls << ",\n"
       << "    \"visible_present_successes\": " << s.visiblePresentSuccesses << ",\n"
       << "    \"occluded_frames_presented\": " << s.occludedFramesPresented << ",\n"
       << "    \"failed_presentations\": " << s.failedPresentations << ",\n"
       << "    \"present_test_calls\": " << s.presentTestCalls << ",\n"
       << "    \"causally_rendered_visible_frames\": " << s.causallyRenderedVisibleFrames << ",\n"
       << "    \"causally_rendered_occluded_frames\": " << s.causallyRenderedOccludedFrames << ",\n"
       << "    \"duplicate_presentations_avoided\": " << s.duplicatePresentationsAvoided << "\n"
       << "  },\n"
       << "  \"translation\": {\n"
       << "    \"dxil_encountered\": " << s.dxilShadersEncountered << ",\n"
       << "    \"dxil_translated\": " << s.dxilShadersTranslated << ",\n"
       << "    \"dxil_rejected\": " << s.dxilShadersRejected << ",\n"
       << "    \"dxbc_validation_failures\": " << s.dxbcValidationFailures << ",\n"
       << "    \"pso_compilations\": " << s.psoCompileCount << ",\n"
       << "    \"cache_hits\": " << s.shaderCacheHits << ",\n"
       << "    \"cache_misses\": " << s.shaderCacheMisses << ",\n"
       << "    \"fallback_events\": " << s.fallbackEvents << "\n"
       << "  },\n"
       << "  \"causal_telemetry\": {\n"
       << "    \"causal_psos_created\": " << s.causalPsosCreated << ",\n"
       << "    \"causal_psos_with_translated_shaders\": " << s.causalPsosWithTranslatedShaders << ",\n"
       << "    \"causal_pipelines_bound\": " << s.causalPipelinesBound << ",\n"
       << "    \"causal_draws_recorded\": " << s.causalDrawsRecorded << ",\n"
       << "    \"causal_translated_draws_recorded\": " << s.causalTranslatedDrawsRecorded << ",\n"
       << "    \"causal_batches_with_translated_work\": " << s.causalBatchesWithTranslatedWork << ",\n"
       << "    \"rendered_work_submissions\": " << s.renderedWorkSubmissions << ",\n"
       << "    \"completed_translated_draws\": " << s.completedTranslatedDraws << ",\n"
       << "    \"causally_rendered_frames\": " << s.causallyRenderedFrames << ",\n"
       << "    \"causally_rendered_visible_frames\": " << s.causallyRenderedVisibleFrames << ",\n"
       << "    \"causally_rendered_occluded_frames\": " << s.causallyRenderedOccludedFrames << ",\n"
       << "    \"causal_chain_complete\": " << (s.lastCausalChainComplete ? "true" : "false") << ",\n"
       << "    \"last_correlated_presentation_id\": " << s.lastCorrelatedPresentationId << "\n"
       << "  },\n"
       << "  \"performance\": {\n"
       << "    \"avg_frame_time_ms\": " << s.averageFrameTimeMs << ",\n"
       << "    \"min_frame_time_ms\": " << s.minFrameTimeMs << ",\n"
       << "    \"max_frame_time_ms\": " << s.maxFrameTimeMs << ",\n"
       << "    \"p99_frame_time_ms\": " << s.p99FrameTimeMs << ",\n"
       << "    \"peak_uav_slot\": " << s.peakUAVSlotBound << "\n"
       << "  },\n"
       << "  \"device_status\": {\n"
       << "    \"last_removal_hresult\": \"0x" << std::hex << s.lastDeviceRemovalReason << std::dec << "\"\n"
       << "  }\n"
       << "}";
    return ss.str();
}

void RuntimeTelemetry::LogSummary() const {
    TelemetrySnapshot s = GetSnapshot();
    LOG_INFO("================================================================================");
    LOG_INFO("  KeplerBridge Runtime Telemetry Session Summary");
    LOG_INFO("================================================================================");
    LOG_INFO("  Session Outcome         : " + OutcomeToString(s.outcome));
    LOG_INFO("  Total Frames Presented  : " + std::to_string(s.totalFramesPresented));
    if (s.totalFramesPresented > 0) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "  Frame Time (Rolling " << K_RING_BUFFER_SIZE << ") : Avg: " << s.averageFrameTimeMs << " ms"
           << " | Min: " << s.minFrameTimeMs << " ms"
           << " | Max: " << s.maxFrameTimeMs << " ms"
           << " | 99th: " << s.p99FrameTimeMs << " ms";
        LOG_INFO(ss.str());
    }
    LOG_INFO("  API Interceptions       : " + std::to_string(s.apiCallsIntercepted));
    LOG_INFO("  D3D12 Devices Created   : " + std::to_string(s.d3d12DevicesCreated) +
             " (Negotiated FL: 0x" + [&]() { std::stringstream ss; ss << std::hex << s.negotiatedFeatureLevel; return ss.str(); }() + ")");
    LOG_INFO("  D3D11 Devices Created   : " + std::to_string(s.d3d11DevicesCreated));
    LOG_INFO("  Resources Created       : " + std::to_string(s.resourcesCreated));
    LOG_INFO("  Root Signatures Created : " + std::to_string(s.rootSignaturesCreated));
    LOG_INFO("  Command Batches Executed: " + std::to_string(s.commandBatchesExecuted));
    LOG_INFO("  DXIL Shaders Encountered: " + std::to_string(s.dxilShadersEncountered) +
             " (Translated: " + std::to_string(s.dxilShadersTranslated) +
             ", Rejected: " + std::to_string(s.dxilShadersRejected) + ")");
    LOG_INFO("  PSO Compilations        : " + std::to_string(s.psoCompileCount) +
             " (Hits: " + std::to_string(s.shaderCacheHits) +
             ", Misses: " + std::to_string(s.shaderCacheMisses) + ")");
    LOG_INFO("  Peak Concurrent UAV Slot: " + std::to_string(s.peakUAVSlotBound));
    if (FAILED(s.lastDeviceRemovalReason)) {
        LOG_ERROR("  Device Removal Status   : FAILED (" + Logger::HResultToString(s.lastDeviceRemovalReason) + ")");
    } else {
        LOG_INFO("  Device Removal Status   : S_OK (No crash or TDR)");
    }
    LOG_INFO("================================================================================");
}

// ============================================================================
// GameProfileManager Static Utilities
// ============================================================================

std::string GameProfileManager::ExtractFileName(const std::string& fullPath) {
    if (fullPath.empty()) return "";
    size_t lastSlash = fullPath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return fullPath.substr(lastSlash + 1);
    }
    return fullPath;
}

std::string GameProfileManager::StripExtension(const std::string& fileName) {
    if (fileName.empty()) return "";
    size_t lastDot = fileName.find_last_of('.');
    if (lastDot != std::string::npos && lastDot > 0) {
        return fileName.substr(0, lastDot);
    }
    return fileName;
}

D3D_FEATURE_LEVEL GameProfileManager::ParseFeatureLevel(const std::string& flStr, D3D_FEATURE_LEVEL defaultLevel) {
    if (flStr == "11_0" || flStr == "11.0") return D3D_FEATURE_LEVEL_11_0;
    if (flStr == "11_1" || flStr == "11.1") return D3D_FEATURE_LEVEL_11_1;
    if (flStr == "12_0" || flStr == "12.0") return D3D_FEATURE_LEVEL_12_0;
    if (flStr == "12_1" || flStr == "12.1") return D3D_FEATURE_LEVEL_12_1;
    if (flStr == "12_2" || flStr == "12.2") return D3D_FEATURE_LEVEL_12_2;
    return defaultLevel;
}

std::string GameProfileManager::FeatureLevelToString(D3D_FEATURE_LEVEL fl) {
    switch (fl) {
        case D3D_FEATURE_LEVEL_11_0: return "11_0";
        case D3D_FEATURE_LEVEL_11_1: return "11_1";
        case D3D_FEATURE_LEVEL_12_0: return "12_0";
        case D3D_FEATURE_LEVEL_12_1: return "12_1";
        case D3D_FEATURE_LEVEL_12_2: return "12_2";
        default:                     return "Unknown";
    }
}

bool GameProfileManager::SectionExists(const std::string& sectionName, const std::string& iniPath) {
    if (sectionName.empty() || iniPath.empty()) return false;
    char fullPath[MAX_PATH] = { 0 };
    std::string path = iniPath;
    if (GetFullPathNameA(iniPath.c_str(), MAX_PATH, fullPath, nullptr) > 0) {
        path = fullPath;
    }
    char buffer[1024] = { 0 };
    DWORD count = GetPrivateProfileStringA(sectionName.c_str(), NULL, "", buffer, sizeof(buffer), path.c_str());
    return count > 0;
}

// ============================================================================
// GameProfileManager Implementation
// ============================================================================

GameProfileManager& GameProfileManager::Instance() {
    static GameProfileManager s_instance;
    return s_instance;
}

const CapabilityPolicy& GameProfileManager::GetActivePolicy() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activePolicy;
}

CapabilityPolicy& GameProfileManager::GetMutablePolicy() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activePolicy;
}

bool GameProfileManager::Initialize(const std::string& hostExePath, const std::string& iniFilePath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. Resolve host executable path and name
    if (!hostExePath.empty()) {
        m_hostExePath = hostExePath;
    } else {
        char exePathBuf[MAX_PATH] = { 0 };
        GetModuleFileNameA(NULL, exePathBuf, MAX_PATH);
        m_hostExePath = exePathBuf;
    }
    m_hostExeName = ExtractFileName(m_hostExePath);

    // 2. Resolve INI configuration path
    if (!iniFilePath.empty()) {
        m_iniPath = iniFilePath;
    } else {
        m_iniPath = "kepler_bridge.ini";
    }
    char fullIniBuf[MAX_PATH] = { 0 };
    if (GetFullPathNameA(m_iniPath.c_str(), MAX_PATH, fullIniBuf, nullptr) > 0) {
        m_iniPath = fullIniBuf;
    }

    // 3. Check INI file existence
    DWORD attr = GetFileAttributesA(m_iniPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        LOG_WARN("[PROFILE] Configuration file not found at: " + m_iniPath + ". Applying conservative Default profile.");
        m_activePolicy = CapabilityPolicy{};
        m_activePolicy.profileName = "Default";
        m_activePolicy.matchedExecutable = m_hostExeName;
        m_activePolicy.selectionReason = "Configuration file missing on disk; conservative defaults active";
        m_activePolicy.isDefaultFallback = true;
        SanitizePolicy(m_activePolicy);
        m_initialized = true;
        return false;
    }

    // 4. Inspect schema version & configuration root
    uint32_t schemaVersion = static_cast<uint32_t>(GetPrivateProfileIntA("Configuration", "SchemaVersion", 1, m_iniPath.c_str()));
    bool enableAutoDetect = GetPrivateProfileIntA("Configuration", "EnableProfileAutoDetect", 1, m_iniPath.c_str()) != 0;

    char activeProfileBuf[128] = { 0 };
    GetPrivateProfileStringA("Configuration", "ActiveProfile", "Auto", activeProfileBuf, sizeof(activeProfileBuf), m_iniPath.c_str());
    std::string activeProfileOverride = activeProfileBuf;

    std::string baseExeName = StripExtension(m_hostExeName);
    std::string selectedSection;
    std::string reason;
    bool isFallback = false;
    bool isLegacy = false;

    if (schemaVersion >= 2) {
        // Explicit profile override takes precedence over auto-detection
        if (!activeProfileOverride.empty() && activeProfileOverride != "Auto") {
            std::string explicitSection = "Profile." + activeProfileOverride;
            if (SectionExists(explicitSection, m_iniPath)) {
                selectedSection = explicitSection;
                reason = "Explicit profile selected via ActiveProfile=" + activeProfileOverride;
            } else {
                LOG_WARN("[PROFILE] Configured ActiveProfile '" + activeProfileOverride +
                         "' section [" + explicitSection + "] not found. Falling back to Profile.Default.");
                selectedSection = "Profile.Default";
                reason = "Explicit profile '" + activeProfileOverride + "' not found; fell back to Profile.Default";
                isFallback = true;
            }
        } else if (enableAutoDetect) {
            // Auto-detect hierarchy:
            // 1. Exact executable name (e.g., [Profile.Palworld-Win64-Shipping.exe])
            // 2. Base executable name (e.g., [Profile.Palworld-Win64-Shipping])
            // 3. Default fallback (e.g., [Profile.Default])
            std::string exactSection = "Profile." + m_hostExeName;
            std::string baseSection = "Profile." + baseExeName;

            if (SectionExists(exactSection, m_iniPath)) {
                selectedSection = exactSection;
                reason = "Auto-detected exact match for executable '" + m_hostExeName + "'";
            } else if (SectionExists(baseSection, m_iniPath)) {
                selectedSection = baseSection;
                reason = "Auto-detected base match for executable '" + baseExeName + "'";
            } else if (SectionExists("Profile.Default", m_iniPath)) {
                selectedSection = "Profile.Default";
                reason = "No specific profile found for '" + m_hostExeName + "'; fell back to Profile.Default";
                isFallback = true;
            } else {
                // If schema v2 but missing Profile.Default, check if legacy flat sections exist
                if (SectionExists("General", m_iniPath) || SectionExists("Translation", m_iniPath)) {
                    isLegacy = true;
                    reason = "Schema v2 declared but no profile sections found; loaded flat sections";
                } else {
                    reason = "Profile.Default missing in v2 configuration; conservative defaults applied";
                    isFallback = true;
                }
            }
        } else {
            selectedSection = "Profile.Default";
            reason = "Profile auto-detection disabled; loaded Profile.Default";
            isFallback = true;
        }
    } else {
        // Schema v1 (Legacy) or Hybrid configuration
        std::string exactSection = "Profile." + m_hostExeName;
        std::string baseSection = "Profile." + baseExeName;

        if (SectionExists(exactSection, m_iniPath)) {
            selectedSection = exactSection;
            reason = "Auto-detected exact match for executable '" + m_hostExeName + "' in hybrid configuration";
        } else if (SectionExists(baseSection, m_iniPath)) {
            selectedSection = baseSection;
            reason = "Auto-detected base match for executable '" + baseExeName + "' in hybrid configuration";
        } else if (SectionExists("Profile.Default", m_iniPath)) {
            selectedSection = "Profile.Default";
            reason = "Fell back to Profile.Default in hybrid configuration";
            isFallback = true;
        } else {
            // Pure Legacy Flat Configuration
            isLegacy = true;
            reason = "Loaded from legacy flat v1 configuration";
        }
    }

    // 5. Parse configuration into active policy
    CapabilityPolicy policy;
    policy.schemaVersion = schemaVersion;
    policy.matchedExecutable = m_hostExeName;
    policy.selectionReason = reason;
    policy.isDefaultFallback = isFallback;

    if (isLegacy) {
        policy.profileName = "Legacy.v1";
        LoadProfileFromIni(m_iniPath, "", policy, true);
    } else if (!selectedSection.empty() && SectionExists(selectedSection, m_iniPath)) {
        // Extract profile display name from section string (strip "Profile.")
        if (selectedSection.rfind("Profile.", 0) == 0) {
            policy.profileName = selectedSection.substr(8);
        } else {
            policy.profileName = selectedSection;
        }
        LoadProfileFromIni(m_iniPath, selectedSection, policy, false);
    } else {
        policy.profileName = "Default";
        policy.selectionReason = "No matching profile found and no legacy sections; internal defaults applied";
        policy.isDefaultFallback = true;
    }

    // 6. Sanitize and validate capabilities
    SanitizePolicy(policy);
    m_activePolicy = policy;
    m_initialized = true;

    return true;
}

bool GameProfileManager::LoadProfileFromIni(const std::string& iniPath, const std::string& sectionName, CapabilityPolicy& outPolicy, bool isLegacyFlat) {
    if (isLegacyFlat) {
        // Parse legacy flat sections
        outPolicy.enableLogging = GetPrivateProfileIntA("General", "EnableLogging", 1, iniPath.c_str()) != 0;
        outPolicy.logVerboseCalls = GetPrivateProfileIntA("General", "LogVerboseCalls", 0, iniPath.c_str()) != 0;

        outPolicy.enableFeatureLevelSpoof = GetPrivateProfileIntA("Spoof", "EnableFeatureLevelSpoof", 0, iniPath.c_str()) != 0;
        char targetFlBuf[64] = { 0 };
        GetPrivateProfileStringA("Spoof", "TargetFeatureLevel", "12_0", targetFlBuf, sizeof(targetFlBuf), iniPath.c_str());
        outPolicy.targetFeatureLevelStr = targetFlBuf;
        outPolicy.spoofVRAMMB = static_cast<uint32_t>(GetPrivateProfileIntA("Spoof", "SpoofVRAMMB", 4096, iniPath.c_str()));

        outPolicy.enableShaderInspection = GetPrivateProfileIntA("Downgrader", "EnableShaderInspection", 1, iniPath.c_str()) != 0;
        outPolicy.enableShaderFallback = GetPrivateProfileIntA("Downgrader", "EnableShaderFallback", 0, iniPath.c_str()) != 0;
        outPolicy.enableTextureClamping = GetPrivateProfileIntA("Downgrader", "EnableTextureClamping", 0, iniPath.c_str()) != 0;
        outPolicy.maxTextureResolution = static_cast<uint32_t>(GetPrivateProfileIntA("Downgrader", "MaxTextureResolution", 1024, iniPath.c_str()));
        outPolicy.stripHeavyShaders = GetPrivateProfileIntA("Downgrader", "StripHeavyShaders", 0, iniPath.c_str()) != 0;

        outPolicy.enableD3D11Translation = GetPrivateProfileIntA("Translation", "EnableD3D11Translation", 0, iniPath.c_str()) != 0;
        outPolicy.emulateBarriers = GetPrivateProfileIntA("Translation", "EmulateBarriers", 1, iniPath.c_str()) != 0;
        outPolicy.enableStateTracking = GetPrivateProfileIntA("Translation", "EnableStateTracking", 1, iniPath.c_str()) != 0;
        outPolicy.immediateQueueFlush = GetPrivateProfileIntA("Translation", "ImmediateQueueFlush", 1, iniPath.c_str()) != 0;
        outPolicy.emulateQueries = GetPrivateProfileIntA("Translation", "EmulateQueries", 1, iniPath.c_str()) != 0;

        outPolicy.enableShaderDiskCache = GetPrivateProfileIntA("Cache", "EnableShaderDiskCache", 1, iniPath.c_str()) != 0;
        char cacheDirBuf[MAX_PATH] = { 0 };
        GetPrivateProfileStringA("Cache", "CacheDirectory", "kepler_cache/shaders", cacheDirBuf, sizeof(cacheDirBuf), iniPath.c_str());
        outPolicy.shaderCacheDir = cacheDirBuf;

        outPolicy.maxConcurrentUAVs = 8;
        outPolicy.safeFlipModeFallback = true;
    } else {
        // Parse scoped profile section
        const char* sec = sectionName.c_str();

        char descBuf[256] = { 0 };
        GetPrivateProfileStringA(sec, "ProfileDescription", "", descBuf, sizeof(descBuf), iniPath.c_str());
        outPolicy.description = descBuf;

        outPolicy.enableLogging = GetPrivateProfileIntA(sec, "EnableLogging", 1, iniPath.c_str()) != 0;
        outPolicy.logVerboseCalls = GetPrivateProfileIntA(sec, "LogVerboseCalls", 0, iniPath.c_str()) != 0;

        outPolicy.enableFeatureLevelSpoof = GetPrivateProfileIntA(sec, "EnableFeatureLevelSpoof", 0, iniPath.c_str()) != 0;
        char targetFlBuf[64] = { 0 };
        GetPrivateProfileStringA(sec, "TargetFeatureLevel", "12_0", targetFlBuf, sizeof(targetFlBuf), iniPath.c_str());
        outPolicy.targetFeatureLevelStr = targetFlBuf;
        outPolicy.spoofVRAMMB = static_cast<uint32_t>(GetPrivateProfileIntA(sec, "SpoofVRAMMB", 4096, iniPath.c_str()));

        outPolicy.enableD3D11Translation = GetPrivateProfileIntA(sec, "EnableD3D11Translation", 0, iniPath.c_str()) != 0;
        outPolicy.emulateBarriers = GetPrivateProfileIntA(sec, "EmulateBarriers", 1, iniPath.c_str()) != 0;
        outPolicy.enableStateTracking = GetPrivateProfileIntA(sec, "EnableStateTracking", 1, iniPath.c_str()) != 0;
        outPolicy.immediateQueueFlush = GetPrivateProfileIntA(sec, "ImmediateQueueFlush", 1, iniPath.c_str()) != 0;
        outPolicy.emulateQueries = GetPrivateProfileIntA(sec, "EmulateQueries", 1, iniPath.c_str()) != 0;

        outPolicy.enableShaderInspection = GetPrivateProfileIntA(sec, "EnableShaderInspection", 1, iniPath.c_str()) != 0;
        outPolicy.enableShaderFallback = GetPrivateProfileIntA(sec, "EnableShaderFallback", 0, iniPath.c_str()) != 0;
        outPolicy.enableTextureClamping = GetPrivateProfileIntA(sec, "EnableTextureClamping", 0, iniPath.c_str()) != 0;
        outPolicy.maxTextureResolution = static_cast<uint32_t>(GetPrivateProfileIntA(sec, "MaxTextureResolution", 1024, iniPath.c_str()));
        outPolicy.stripHeavyShaders = GetPrivateProfileIntA(sec, "StripHeavyShaders", 0, iniPath.c_str()) != 0;

        outPolicy.maxConcurrentUAVs = static_cast<uint32_t>(GetPrivateProfileIntA(sec, "MaxConcurrentUAVs", 8, iniPath.c_str()));
        outPolicy.safeFlipModeFallback = GetPrivateProfileIntA(sec, "SafeFlipModeFallback", 1, iniPath.c_str()) != 0;

        outPolicy.enableShaderDiskCache = GetPrivateProfileIntA(sec, "EnableShaderDiskCache", 1, iniPath.c_str()) != 0;
        char cacheDirBuf[MAX_PATH] = { 0 };
        GetPrivateProfileStringA(sec, "CacheDirectory", "kepler_cache/shaders", cacheDirBuf, sizeof(cacheDirBuf), iniPath.c_str());
        outPolicy.shaderCacheDir = cacheDirBuf;

        // Backward compatibility: If evaluating the default fallback profile,
        // check whether legacy flat sections exist with explicit overrides.
        // Legacy test suites and external scripts write to [Translation], [Spoof], etc.
        if (outPolicy.isDefaultFallback || sectionName == "Profile.Default") {
            if (SectionExists("Translation", iniPath)) {
                int legacyTrans = GetPrivateProfileIntA("Translation", "EnableD3D11Translation", -1, iniPath.c_str());
                if (legacyTrans != -1) {
                    outPolicy.enableD3D11Translation = (legacyTrans != 0);
                }
            }
            if (SectionExists("Spoof", iniPath)) {
                int legacySpoof = GetPrivateProfileIntA("Spoof", "EnableFeatureLevelSpoof", -1, iniPath.c_str());
                if (legacySpoof != -1) {
                    outPolicy.enableFeatureLevelSpoof = (legacySpoof != 0);
                }
                char legacyTargetFl[64] = { 0 };
                GetPrivateProfileStringA("Spoof", "TargetFeatureLevel", "", legacyTargetFl, sizeof(legacyTargetFl), iniPath.c_str());
                if (legacyTargetFl[0] != '\0') {
                    outPolicy.targetFeatureLevelStr = legacyTargetFl;
                }
            }
        }
    }

    return true;
}

void GameProfileManager::SanitizePolicy(CapabilityPolicy& policy) {
    // 1. Target feature level validation
    D3D_FEATURE_LEVEL parsed = ParseFeatureLevel(policy.targetFeatureLevelStr, D3D_FEATURE_LEVEL_11_0);
    if (parsed == D3D_FEATURE_LEVEL_11_0 && policy.targetFeatureLevelStr != "11_0" && policy.targetFeatureLevelStr != "11.0") {
        LOG_WARN("[POLICY] Unrecognized TargetFeatureLevel '" + policy.targetFeatureLevelStr +
                 "'. Safely defaulting to D3D_FEATURE_LEVEL_11_0.");
        policy.targetFeatureLevelStr = "11_0";
    }
    policy.targetFeatureLevel = parsed;

    // 2. MaxConcurrentUAVs hardware boundary clamp
    // Kepler GK107/GK208B / Feature Level 11_0 enforces a strict hardware limit of 8 concurrent UAVs (u0..u7).
    if (policy.maxConcurrentUAVs > 8) {
        LOG_WARN("[POLICY] Configured MaxConcurrentUAVs (" + std::to_string(policy.maxConcurrentUAVs) +
                 ") exceeds Kepler FL 11_0 hardware capacity (8). Clamping to 8.");
        policy.maxConcurrentUAVs = 8;
    }
    if (policy.maxConcurrentUAVs == 0) {
        policy.maxConcurrentUAVs = 1;
    }

    // 3. MaxTextureResolution validation
    // Must be a power of two between 128 and 4096.
    bool validPo2 = (policy.maxTextureResolution >= 128 && policy.maxTextureResolution <= 4096) &&
                    ((policy.maxTextureResolution & (policy.maxTextureResolution - 1)) == 0);
    if (!validPo2) {
        LOG_WARN("[POLICY] Configured MaxTextureResolution (" + std::to_string(policy.maxTextureResolution) +
                 ") is invalid. Normalizing to safe default (1024).");
        policy.maxTextureResolution = 1024;
    }

    // 4. Cache directory fallback
    if (policy.shaderCacheDir.empty()) {
        policy.shaderCacheDir = "kepler_cache/shaders";
    }
}

void GameProfileManager::LogProfileDetails() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    LOG_INFO("================================================================================");
    LOG_INFO("  KeplerBridge Active Compatibility Profile");
    LOG_INFO("================================================================================");
    LOG_INFO("  Profile Name       : " + m_activePolicy.profileName);
    if (!m_activePolicy.description.empty()) {
        LOG_INFO("  Description        : " + m_activePolicy.description);
    }
    LOG_INFO("  Matched Executable : " + m_activePolicy.matchedExecutable);
    LOG_INFO("  Selection Reason   : " + m_activePolicy.selectionReason);
    LOG_INFO("  Configuration Path : " + m_iniPath);
    LOG_INFO("  Schema Version     : " + std::to_string(m_activePolicy.schemaVersion));
    LOG_INFO("  --- Capability Policies ---");
    LOG_INFO("  D3D11 Translation  : " + std::string(m_activePolicy.enableD3D11Translation ? "ENABLED" : "DISABLED"));
    LOG_INFO("  Feature Level Spoof: " + std::string(m_activePolicy.enableFeatureLevelSpoof ? "ENABLED" : "DISABLED") +
             " (Target: " + m_activePolicy.targetFeatureLevelStr + ")");
    LOG_INFO("  Advertised VRAM    : " + std::to_string(m_activePolicy.spoofVRAMMB) + " MB");
    LOG_INFO("  Texture Clamping   : " + std::string(m_activePolicy.enableTextureClamping ? "ENABLED" : "DISABLED") +
             " (Max: " + std::to_string(m_activePolicy.maxTextureResolution) + "x" + std::to_string(m_activePolicy.maxTextureResolution) + ")");
    LOG_INFO("  Shader Inspection  : " + std::string(m_activePolicy.enableShaderInspection ? "ENABLED" : "DISABLED"));
    LOG_INFO("  Shader Fallback    : " + std::string(m_activePolicy.enableShaderFallback ? "ENABLED" : "DISABLED"));
    LOG_INFO("  Shader Disk Cache  : " + std::string(m_activePolicy.enableShaderDiskCache ? "ENABLED" : "DISABLED") +
             " (Dir: " + m_activePolicy.shaderCacheDir + ")");
    LOG_INFO("  Max Concurrent UAVs: " + std::to_string(m_activePolicy.maxConcurrentUAVs) + " (Hardware FL 11_0 Limit: 8)");
    LOG_INFO("  Swapchain Fallback : " + std::string(m_activePolicy.safeFlipModeFallback ? "DISCARD" : "STANDARD"));
    LOG_INFO("================================================================================");
}

bool GameProfileManager::ValidatePolicySchema(const CapabilityPolicy& policy, std::vector<std::string>& outErrors) {
    outErrors.clear();

    if (policy.profileName.empty()) {
        outErrors.push_back("ProfileName cannot be empty.");
    }

    if (policy.maxConcurrentUAVs == 0 || policy.maxConcurrentUAVs > 8) {
        outErrors.push_back("MaxConcurrentUAVs must be between 1 and 8 for Kepler FL 11_0 hardware.");
    }

    bool validPo2 = (policy.maxTextureResolution >= 128 && policy.maxTextureResolution <= 4096) &&
                    ((policy.maxTextureResolution & (policy.maxTextureResolution - 1)) == 0);
    if (!validPo2) {
        outErrors.push_back("MaxTextureResolution must be a power-of-two between 128 and 4096.");
    }

    if (policy.fsr1Sharpness < 0.0f || policy.fsr1Sharpness > 1.0f) {
        outErrors.push_back("FSR1 sharpness must be in the range [0.0, 1.0].");
    }

    if (policy.fsr1Preset != "UltraQuality" && policy.fsr1Preset != "Quality" &&
        policy.fsr1Preset != "Balanced" && policy.fsr1Preset != "Performance")
    {
        outErrors.push_back("FSR1 preset must be UltraQuality, Quality, Balanced, or Performance.");
    }

    if (policy.memorySoftBudgetMB == 0 || policy.memorySoftBudgetMB > 16384) {
        outErrors.push_back("MemorySoftBudgetMB must be between 1 and 16384 MB.");
    }

    return outErrors.empty();
}

CapabilityPolicy GameProfileManager::CreateConservativeProfile(const std::string& name) {
    CapabilityPolicy p;
    p.profileName = name;
    p.description = "Conservative baseline profile: passthrough favored, no speculative translation";
    p.profileType = "Conservative";
    p.enableLogging = true;
    p.enableFeatureLevelSpoof = false;
    p.targetFeatureLevelStr = "11_0";
    p.targetFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    p.enableD3D11Translation = false;
    p.enableShaderInspection = true;
    p.enableShaderDiskCache = true;
    p.enableFSR1Upscaling = false;
    p.allowAdaptiveEngine = true;
    p.diagnosticsLevel = "Standard";
    p.maxConcurrentUAVs = 8;
    p.safeFlipModeFallback = true;
    p.schemaVersion = 2;
    return p;
}

CapabilityPolicy GameProfileManager::CreateManualProfile(const std::string& name) {
    CapabilityPolicy p;
    p.profileName = name;
    p.description = "Manual compatibility profile: deterministic user-configured translation overrides";
    p.profileType = "Manual";
    p.enableLogging = true;
    p.enableFeatureLevelSpoof = true;
    p.targetFeatureLevelStr = "12_0";
    p.targetFeatureLevel = D3D_FEATURE_LEVEL_12_0;
    p.enableD3D11Translation = true;
    p.enableShaderInspection = true;
    p.enableShaderDiskCache = true;
    p.enableFSR1Upscaling = false;
    p.allowAdaptiveEngine = false; // Strict manual control
    p.diagnosticsLevel = "Verbose";
    p.maxConcurrentUAVs = 8;
    p.safeFlipModeFallback = true;
    p.schemaVersion = 2;
    return p;
}

CapabilityPolicy GameProfileManager::CreatePerformanceProfile(const std::string& name) {
    CapabilityPolicy p;
    p.profileName = name;
    p.description = "Performance profile: translation enabled with FSR 1 spatial upscaling and adaptive control";
    p.profileType = "Performance";
    p.enableLogging = true;
    p.enableFeatureLevelSpoof = true;
    p.targetFeatureLevelStr = "12_0";
    p.targetFeatureLevel = D3D_FEATURE_LEVEL_12_0;
    p.enableD3D11Translation = true;
    p.enableShaderInspection = true;
    p.enableShaderDiskCache = true;
    p.enableFSR1Upscaling = true;
    p.fsr1Preset = "Performance";
    p.fsr1Sharpness = 0.85f;
    p.allowAdaptiveEngine = true;
    p.diagnosticsLevel = "Standard";
    p.maxConcurrentUAVs = 8;
    p.safeFlipModeFallback = true;
    p.schemaVersion = 2;
    return p;
}

bool GameProfileManager::MigrateSchema(CapabilityPolicy& policy, uint32_t fromVersion, uint32_t toVersion) {
    if (fromVersion == toVersion) return true;
    if (fromVersion == 1 && toVersion == 2) {
        // Upgrade from v1 to v2: initialize new fields
        policy.memorySoftBudgetMB = 3072;
        policy.enableFSR1Upscaling = false;
        policy.fsr1Preset = "Quality";
        policy.fsr1Sharpness = 0.8f;
        policy.allowAdaptiveEngine = true;
        policy.diagnosticsLevel = "Standard";
        policy.profileType = "Conservative";
        policy.schemaVersion = 2;
        return true;
    }
    return false;
}

} // namespace kepler
