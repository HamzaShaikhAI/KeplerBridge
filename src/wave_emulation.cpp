#include "downgrader/wave_emulation.h"
#include <sstream>

namespace kepler {

WaveEmulationEngine& WaveEmulationEngine::Instance() {
    static WaveEmulationEngine s_instance;
    return s_instance;
}

WaveEmulationEngine::WaveEmulationEngine() {
    m_classifications = {
        {
            WaveOpCode::WaveGetLaneIndex,
            "WaveGetLaneIndex",
            CapabilityStatus::Emulated,
            STAGE_COMPUTE,
            "Mapped to SV_GroupIndex % 32 (warp lane index)",
            "Unsupported in Vertex/Pixel stages on Kepler FL 11_0"
        },
        {
            WaveOpCode::WaveGetLaneCount,
            "WaveGetLaneCount",
            CapabilityStatus::Emulated,
            STAGE_COMPUTE,
            "Constant 32 (Kepler GK107 warp size)",
            "Unsupported in Vertex/Pixel stages"
        },
        {
            WaveOpCode::WaveReadLaneFirst,
            "WaveReadLaneFirst",
            CapabilityStatus::Emulated,
            STAGE_COMPUTE,
            "groupshared memory broadcast from lane 0 with GroupMemoryBarrierWithGroupSync()",
            "Unsupported in Vertex/Pixel stages; requires convergent execution"
        },
        {
            WaveOpCode::WaveActiveSum,
            "WaveActiveSum",
            CapabilityStatus::Emulated,
            STAGE_COMPUTE,
            "Parallel reduction tree in groupshared memory with power-of-2 strides",
            "Unsupported in Vertex/Pixel stages; requires convergent execution"
        },
        {
            WaveOpCode::WaveActiveAllTrue,
            "WaveActiveAllTrue",
            CapabilityStatus::Emulated,
            STAGE_COMPUTE,
            "InterlockedAnd on groupshared 32-bit ballot mask",
            "Unsupported in Vertex/Pixel stages"
        },
        {
            WaveOpCode::WaveActiveAnyTrue,
            "WaveActiveAnyTrue",
            CapabilityStatus::Emulated,
            STAGE_COMPUTE,
            "InterlockedOr on groupshared 32-bit ballot mask",
            "Unsupported in Vertex/Pixel stages"
        },
        {
            WaveOpCode::WavePrefixSum,
            "WavePrefixSum",
            CapabilityStatus::Emulated,
            STAGE_COMPUTE,
            "Parallel Kogge-Stone / Brent-Kung scan tree in groupshared memory",
            "Unsupported in Vertex/Pixel stages"
        },
        {
            WaveOpCode::WaveShuffle,
            "WaveShuffle",
            CapabilityStatus::Unsupported,
            STAGE_NONE,
            "None",
            "Arbitrary lane shuffle with dynamic indices cannot safely guarantee convergence in LDS"
        },
        {
            WaveOpCode::QuadReadAcrossX,
            "QuadReadAcrossX",
            CapabilityStatus::Unsupported,
            STAGE_NONE,
            "None",
            "Quad cross-lane communication unsupported on Kepler FL 11_0 hardware"
        }
    };
}

const WaveOpClassification& WaveEmulationEngine::GetClassification(WaveOpCode op) const {
    for (const auto& c : m_classifications) {
        if (c.opCode == op) return c;
    }
    static const WaveOpClassification s_unknown{
        WaveOpCode::WaveShuffle, "Unknown", CapabilityStatus::Unsupported, STAGE_NONE, "None", "Unknown operation"
    };
    return s_unknown;
}

std::vector<WaveOpClassification> WaveEmulationEngine::GetAllClassifications() const {
    return m_classifications;
}

bool WaveEmulationEngine::CanEmulateInStage(WaveOpCode op, ShaderStageMask stage, std::string& outDiagnostic) const {
    const auto& c = GetClassification(op);
    if (c.status == CapabilityStatus::Unsupported) {
        outDiagnostic = "Wave operation '" + c.name + "' is physically unsupported: " + c.limitationOrRejection;
        return false;
    }

    if ((c.supportedStagesMask & stage) == 0) {
        outDiagnostic = "Wave operation '" + c.name + "' cannot be emulated in target stage: " + c.limitationOrRejection;
        return false;
    }

    return true;
}

std::string WaveEmulationEngine::GenerateComputeLdsEmulationHLSL(
    WaveOpCode op,
    const std::string& dataType,
    const std::string& inputVarName) const
{
    std::ostringstream ss;
    switch (op) {
        case WaveOpCode::WaveGetLaneIndex:
            ss << "(groupIndex % 32)";
            break;

        case WaveOpCode::WaveGetLaneCount:
            ss << "32";
            break;

        case WaveOpCode::WaveReadLaneFirst:
            ss << "/* WaveReadLaneFirst Emulation */\n"
               << "s_waveLds[groupIndex] = " << inputVarName << ";\n"
               << "GroupMemoryBarrierWithGroupSync();\n"
               << "s_waveLds[(groupIndex / 32) * 32]";
            break;

        case WaveOpCode::WaveActiveSum:
            ss << "/* WaveActiveSum Parallel Reduction Emulation */\n"
               << "s_waveLds[groupIndex] = " << inputVarName << ";\n"
               << "GroupMemoryBarrierWithGroupSync();\n"
               << "if ((groupIndex % 32) < 16) s_waveLds[groupIndex] += s_waveLds[groupIndex + 16];\n"
               << "GroupMemoryBarrierWithGroupSync();\n"
               << "if ((groupIndex % 32) < 8)  s_waveLds[groupIndex] += s_waveLds[groupIndex + 8];\n"
               << "GroupMemoryBarrierWithGroupSync();\n"
               << "if ((groupIndex % 32) < 4)  s_waveLds[groupIndex] += s_waveLds[groupIndex + 4];\n"
               << "GroupMemoryBarrierWithGroupSync();\n"
               << "if ((groupIndex % 32) < 2)  s_waveLds[groupIndex] += s_waveLds[groupIndex + 2];\n"
               << "GroupMemoryBarrierWithGroupSync();\n"
               << "if ((groupIndex % 32) < 1)  s_waveLds[groupIndex] += s_waveLds[groupIndex + 1];\n"
               << "GroupMemoryBarrierWithGroupSync();\n"
               << "s_waveLds[(groupIndex / 32) * 32]";
            break;

        default:
            ss << "/* Unsupported Wave Op */ " << inputVarName;
            break;
    }
    return ss.str();
}

} // namespace kepler
