#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace kepler {

enum class ShaderContainerType {
    Unknown,
    Invalid,         // Truncated, bad offset, corrupted magic, out-of-bounds
    LegacyDXBC,      // Standard DXBC with SHDR or SHEX chunk (SM 4.x / 5.x)
    DXIL             // DXBC container containing DXIL chunk (SM 6.x)
};

enum class ShaderStage {
    Unknown,
    Vertex,
    Pixel,
    Geometry,
    Hull,
    Domain,
    Compute,
    Library,
    RayGeneration,
    Intersection,
    AnyHit,
    ClosestHit,
    Miss,
    Callable,
    Mesh,
    Amplification
};

struct ShaderInspectionResult {
    bool isValidContainer = false;
    ShaderContainerType containerType = ShaderContainerType::Unknown;
    ShaderStage stage = ShaderStage::Unknown;
    uint32_t majorVersion = 0;
    uint32_t minorVersion = 0;
    bool hasDXILChunk = false;
    bool hasLegacyBytecodeChunk = false;
    uint32_t chunkCount = 0;
    std::vector<std::string> chunkTypes;
    std::string statusMessage;
    size_t totalBytecodeSize = 0;
    bool isSupportedOnKepler = false; // True if SM <= 5.1 and not DXIL
};

class DxilInspector {
public:
    // Performs rigorous bounds-checked inspection of Direct3D shader bytecode
    static ShaderInspectionResult InspectShader(const void* pBytecode, size_t lengthInBytes);

    static std::string StageToString(ShaderStage stage);
    static std::string ContainerTypeToString(ShaderContainerType type);
    static std::string ShaderModelToString(uint32_t major, uint32_t minor);

private:
    static ShaderStage DecodeStageFromToken(uint32_t token);
    static ShaderStage DecodeStageFromDxil(uint32_t token);
    static std::string FourCCToString(uint32_t fourCC);
};

} // namespace kepler
