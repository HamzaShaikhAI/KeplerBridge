#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "core/capability_registry.h"

namespace kepler {

enum class WaveOpCode {
    WaveGetLaneIndex,
    WaveGetLaneCount,
    WaveReadLaneFirst,
    WaveActiveSum,
    WaveActiveAllTrue,
    WaveActiveAnyTrue,
    WavePrefixSum,
    WaveShuffle,
    QuadReadAcrossX
};

struct WaveOpClassification {
    WaveOpCode opCode;
    std::string name;
    CapabilityStatus status;
    uint32_t supportedStagesMask;
    std::string emulationStrategy;
    std::string limitationOrRejection;
};

class WaveEmulationEngine {
public:
    static WaveEmulationEngine& Instance();

    const WaveOpClassification& GetClassification(WaveOpCode op) const;
    std::vector<WaveOpClassification> GetAllClassifications() const;

    bool CanEmulateInStage(WaveOpCode op, ShaderStageMask stage, std::string& outDiagnostic) const;

    /**
     * @brief Generates standard D3D11 CS 5.0 threadgroup LDS emulation code for a verified wave op.
     */
    std::string GenerateComputeLdsEmulationHLSL(
        WaveOpCode op,
        const std::string& dataType,
        const std::string& inputVarName) const;

private:
    WaveEmulationEngine();
    ~WaveEmulationEngine() = default;

    std::vector<WaveOpClassification> m_classifications;
};

} // namespace kepler
