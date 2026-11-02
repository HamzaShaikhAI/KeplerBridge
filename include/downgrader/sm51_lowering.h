#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace kepler {

enum class SM51LoweringStatus {
    Success,
    NotSM51,
    InvalidContainer,
    MultiSpaceCollision,
    UnboundedDescriptorRange,
    DynamicIndexingUnsupported,
    ExceedsD3D11BindingLimits,
    MalformedBytecode
};

struct SM51LoweringReport {
    SM51LoweringStatus status = SM51LoweringStatus::InvalidContainer;
    std::string diagnostic;
    uint32_t originalDwordCount = 0;
    uint32_t loweredDwordCount = 0;
    uint32_t declaredSRVs = 0;
    uint32_t declaredUAVs = 0;
    uint32_t declaredCBVs = 0;
    uint32_t declaredSamplers = 0;
    bool hasMultiSpace = false;
    bool hasUnboundedRange = false;
};

class SM51LoweringEngine {
public:
    /**
     * @brief Performs preflight analysis to check whether the SM 5.1 shader satisfies
     *        the strict mathematical and architectural constraints for safe lowering to SM 5.0.
     */
    static bool CanLower(
        const void* pBytecode,
        size_t size,
        SM51LoweringReport* pOutReport = nullptr);

    /**
     * @brief Lowers a compatible Shader Model 5.1 DXBC container to an authentic Shader Model 5.0
     *        container, including instruction lowering, token remapping, chunk table rebuilding,
     *        and DXBC checksum recalculation.
     */
    static bool LowerSM51ToSM50(
        const void* pSrcBytecode,
        size_t srcSize,
        std::vector<uint8_t>& outLoweredBytecode,
        SM51LoweringReport* pOutReport = nullptr);

    static std::string StatusToString(SM51LoweringStatus status);
};

} // namespace kepler
