#pragma once

#include "downgrader/dxil_inspector.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>

namespace kepler {

struct ShaderPolicyDecision {
    bool shouldIntercept = false;
    bool isSupported = true;
    bool fallbackApplied = false;
    const void* effectiveBytecode = nullptr;
    size_t effectiveLength = 0;
    std::vector<uint8_t> fallbackStorage;
    ShaderInspectionResult inspection;
    std::string actionReason;
};

class IShaderTranslator {
public:
    virtual ~IShaderTranslator() = default;
    virtual const char* GetName() const = 0;
    virtual bool CanTranslate(const ShaderInspectionResult& info) const = 0;
    virtual bool Translate(
        const void* srcBytecode,
        size_t srcLength,
        const ShaderInspectionResult& info,
        std::vector<uint8_t>& outBytecode,
        std::string& outErrorMessage) = 0;
};

class ShaderPolicy {
public:
    static ShaderPolicy& Instance();

    // Evaluates a shader stage bytecode blob against hardware capabilities and active configuration
    ShaderPolicyDecision EvaluateShader(
        const void* pBytecode,
        size_t lengthInBytes,
        ShaderStage expectedStage);

    // Register a custom translator implementation (extensible for Phase 3/4)
    void RegisterTranslator(std::shared_ptr<IShaderTranslator> translator);
    void ClearTranslators();

    // Check if any translator is registered and active
    bool HasActiveTranslator() const;

private:
    ShaderPolicy() = default;
    ~ShaderPolicy() = default;
    ShaderPolicy(const ShaderPolicy&) = delete;
    ShaderPolicy& operator=(const ShaderPolicy&) = delete;

    mutable std::mutex m_mutex;
    std::vector<std::shared_ptr<IShaderTranslator>> m_translators;
};

} // namespace kepler
