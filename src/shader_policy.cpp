#include "downgrader/shader_policy.h"
#include "core/config.h"
#include "core/logger.h"

namespace kepler {

ShaderPolicy& ShaderPolicy::Instance() {
    static ShaderPolicy s_instance;
    return s_instance;
}

void ShaderPolicy::RegisterTranslator(std::shared_ptr<IShaderTranslator> translator) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (translator) {
        m_translators.push_back(translator);
        LOG_INFO("[SHADER_POLICY] Registered translator: " + std::string(translator->GetName()));
    }
}

void ShaderPolicy::ClearTranslators() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_translators.clear();
}

bool ShaderPolicy::HasActiveTranslator() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_translators.empty();
}

ShaderPolicyDecision ShaderPolicy::EvaluateShader(
    const void* pBytecode,
    size_t lengthInBytes,
    ShaderStage expectedStage)
{
    ShaderPolicyDecision decision;
    decision.effectiveBytecode = pBytecode;
    decision.effectiveLength = lengthInBytes;

    if (!pBytecode || lengthInBytes == 0) {
        decision.isSupported = true;
        decision.actionReason = "Empty shader stage.";
        return decision;
    }

    // Inspect the bytecode container
    decision.inspection = DxilInspector::InspectShader(pBytecode, lengthInBytes);

    if (!decision.inspection.isValidContainer) {
        decision.isSupported = false;
        decision.actionReason = "Malformed container: " + decision.inspection.statusMessage;
        return decision;
    }

    // If natively supported on Kepler (SM 4.x / 5.x DXBC), passthrough directly
    if (decision.inspection.isSupportedOnKepler) {
        decision.isSupported = true;
        decision.shouldIntercept = false;
        decision.actionReason = "Native SM " + std::to_string(decision.inspection.majorVersion) + "." +
                                std::to_string(decision.inspection.minorVersion) + " shader is natively supported.";
        return decision;
    }

    // Shader is unsupported natively (e.g. DXIL SM 6.x)
    decision.isSupported = false;

    // Check if fallback translation is enabled in configuration
    if (!Config::Instance().IsShaderFallbackEnabled()) {
        decision.shouldIntercept = false;
        decision.fallbackApplied = false;
        decision.actionReason = "Unsupported shader (DXIL SM " + std::to_string(decision.inspection.majorVersion) +
                                "." + std::to_string(decision.inspection.minorVersion) +
                                "). Fallback disabled in kepler_bridge.ini; preserving native driver error propagation.";
        return decision;
    }

    // Fallback is enabled: query registered translators
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& translator : m_translators) {
        if (translator && translator->CanTranslate(decision.inspection)) {
            std::vector<uint8_t> translated;
            std::string errorMsg;
            if (translator->Translate(pBytecode, lengthInBytes, decision.inspection, translated, errorMsg)) {
                decision.fallbackStorage = std::move(translated);
                decision.effectiveBytecode = decision.fallbackStorage.data();
                decision.effectiveLength = decision.fallbackStorage.size();
                decision.fallbackApplied = true;
                decision.shouldIntercept = true;
                decision.isSupported = true;
                decision.actionReason = "Successfully translated via " + std::string(translator->GetName());
                return decision;
            } else {
                LOG_WARN("[SHADER_POLICY] Translator " + std::string(translator->GetName()) +
                         " failed: " + errorMsg);
            }
        }
    }

    decision.shouldIntercept = false;
    decision.fallbackApplied = false;
    decision.actionReason = "No suitable translator available for DXIL SM " +
                            std::to_string(decision.inspection.majorVersion) + "." +
                            std::to_string(decision.inspection.minorVersion) +
                            ". Preserving native error propagation.";
    return decision;
}

} // namespace kepler
