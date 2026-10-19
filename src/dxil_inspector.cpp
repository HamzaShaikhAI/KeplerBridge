#include "downgrader/dxil_inspector.h"
#include <cstring>
#include <sstream>
#include <iomanip>

namespace kepler {

namespace {

#pragma pack(push, 1)
struct DXBCHeader {
    uint32_t magic;         // 0x43425844 ('DXBC')
    uint8_t  checksum[16];  // 128-bit hash
    uint32_t version;       // 1
    uint32_t totalSize;     // Total container size
    uint32_t chunkCount;    // Number of chunks
};

struct DXBCChunkHeader {
    uint32_t fourCC;        // Chunk type
    uint32_t chunkSize;     // Size of chunk body
};

struct DxilProgramHeader {
    uint32_t bitcodeHeaderVersion; // Packed version token
    uint32_t sizeInBytes;          // Total size
    uint32_t dxilMagic;            // 0x4C495844 ('DXIL')
    uint32_t dxilVersion;          // Format version
    uint32_t bitcodeOffset;        // Offset relative to start of DxilProgramHeader
    uint32_t bitcodeSize;          // Bitcode length
};
#pragma pack(pop)

constexpr uint32_t FOURCC_DXBC = 0x43425844; // 'DXBC'
constexpr uint32_t FOURCC_DXIL = 0x4C495844; // 'DXIL'
constexpr uint32_t FOURCC_SHDR = 0x52444853; // 'SHDR'
constexpr uint32_t FOURCC_SHEX = 0x58454853; // 'SHEX'

} // anonymous namespace

ShaderInspectionResult DxilInspector::InspectShader(const void* pBytecode, size_t lengthInBytes) {
    ShaderInspectionResult result;
    result.totalBytecodeSize = lengthInBytes;

    if (!pBytecode) {
        result.isValidContainer = false;
        result.containerType = ShaderContainerType::Invalid;
        result.statusMessage = "Null bytecode pointer provided.";
        return result;
    }

    if (lengthInBytes < sizeof(DXBCHeader)) {
        result.isValidContainer = false;
        result.containerType = ShaderContainerType::Invalid;
        result.statusMessage = "Bytecode truncated: smaller than minimum DXBC container header (" +
                               std::to_string(lengthInBytes) + " < " + std::to_string(sizeof(DXBCHeader)) + " bytes).";
        return result;
    }

    const uint8_t* pRaw = reinterpret_cast<const uint8_t*>(pBytecode);
    const DXBCHeader* pHeader = reinterpret_cast<const DXBCHeader*>(pRaw);

    // Validate DXBC Magic signature
    if (pHeader->magic != FOURCC_DXBC) {
        result.isValidContainer = false;
        result.containerType = ShaderContainerType::Invalid;
        result.statusMessage = "Invalid container signature: expected 'DXBC' (0x43425844), got 0x" +
                               FourCCToString(pHeader->magic);
        return result;
    }

    // Validate container size against buffer length
    if (pHeader->totalSize > lengthInBytes) {
        result.isValidContainer = false;
        result.containerType = ShaderContainerType::Invalid;
        result.statusMessage = "Container truncated: header specifies totalSize of " +
                               std::to_string(pHeader->totalSize) + " bytes, but buffer only contains " +
                               std::to_string(lengthInBytes) + " bytes.";
        return result;
    }

    // Validate chunk table offset boundaries
    size_t chunkTableEnd = sizeof(DXBCHeader) + static_cast<size_t>(pHeader->chunkCount) * sizeof(uint32_t);
    if (chunkTableEnd > pHeader->totalSize || chunkTableEnd > lengthInBytes) {
        result.isValidContainer = false;
        result.containerType = ShaderContainerType::Invalid;
        result.statusMessage = "Malformed chunk table: offsets table exceeds container bounds.";
        return result;
    }

    result.isValidContainer = true;
    result.chunkCount = pHeader->chunkCount;

    const uint32_t* pChunkOffsets = reinterpret_cast<const uint32_t*>(pRaw + sizeof(DXBCHeader));

    // Iterate through chunks with strict bounds checking
    for (uint32_t i = 0; i < pHeader->chunkCount; ++i) {
        uint32_t offset = pChunkOffsets[i];

        // Ensure chunk header is within bounds
        if (offset + sizeof(DXBCChunkHeader) > pHeader->totalSize) {
            result.isValidContainer = false;
            result.containerType = ShaderContainerType::Invalid;
            result.statusMessage = "Malformed chunk " + std::to_string(i) + ": chunk header offset (" +
                                   std::to_string(offset) + ") exceeds container size.";
            return result;
        }

        const DXBCChunkHeader* pChunk = reinterpret_cast<const DXBCChunkHeader*>(pRaw + offset);
        uint32_t fourCC = pChunk->fourCC;
        uint32_t chunkSize = pChunk->chunkSize;

        // Ensure chunk body is within bounds
        if (offset + sizeof(DXBCChunkHeader) + chunkSize > pHeader->totalSize) {
            result.isValidContainer = false;
            result.containerType = ShaderContainerType::Invalid;
            result.statusMessage = "Malformed chunk " + std::to_string(i) + " ('" + FourCCToString(fourCC) +
                                   "'): body size (" + std::to_string(chunkSize) + ") exceeds container bounds.";
            return result;
        }

        result.chunkTypes.push_back(FourCCToString(fourCC));

        const uint8_t* pChunkData = pRaw + offset + sizeof(DXBCChunkHeader);

        // Check for DXIL chunk
        if (fourCC == FOURCC_DXIL) {
            result.hasDXILChunk = true;
            if (chunkSize >= sizeof(DxilProgramHeader)) {
                const DxilProgramHeader* pDxilHdr = reinterpret_cast<const DxilProgramHeader*>(pChunkData);
                if (pDxilHdr->dxilMagic == FOURCC_DXIL) {
                    uint32_t verToken = pDxilHdr->bitcodeHeaderVersion;
                    result.stage = DecodeStageFromDxil((verToken >> 16) & 0xFFFF);
                    result.majorVersion = (verToken >> 4) & 0xF;
                    result.minorVersion = verToken & 0xF;
                }
            }
        }
        // Check for Legacy Shader bytecode (SHEX = SM 5.x, SHDR = SM 4.x)
        else if (fourCC == FOURCC_SHEX || fourCC == FOURCC_SHDR) {
            result.hasLegacyBytecodeChunk = true;
            if (chunkSize >= sizeof(uint32_t)) {
                uint32_t verToken = *reinterpret_cast<const uint32_t*>(pChunkData);
                result.stage = DecodeStageFromToken((verToken >> 16) & 0xFFFF);
                result.majorVersion = (verToken >> 4) & 0xF;
                result.minorVersion = verToken & 0xF;
            }
        }
    }

    // Determine final container type and hardware compatibility
    if (result.hasDXILChunk) {
        result.containerType = ShaderContainerType::DXIL;
        result.isSupportedOnKepler = false;
        result.statusMessage = "Shader Model 6.x (DXIL) container detected. Unsupported natively on Kepler hardware.";
    } else if (result.hasLegacyBytecodeChunk) {
        result.containerType = ShaderContainerType::LegacyDXBC;
        result.isSupportedOnKepler = (result.majorVersion <= 5);
        result.statusMessage = "Legacy Shader Model " + std::to_string(result.majorVersion) + "." +
                               std::to_string(result.minorVersion) + " (DXBC) container detected. Supported on Kepler.";
    } else {
        result.containerType = ShaderContainerType::LegacyDXBC;
        result.isSupportedOnKepler = true;
        result.statusMessage = "DXBC container with no executable shader chunk (e.g. Serialized Root Signature).";
    }

    return result;
}

ShaderStage DxilInspector::DecodeStageFromToken(uint32_t token) {
    switch (token) {
        case 0x0000: return ShaderStage::Pixel;
        case 0x0001: return ShaderStage::Vertex;
        case 0x0002: return ShaderStage::Geometry;
        case 0x0003: return ShaderStage::Hull;
        case 0x0004: return ShaderStage::Domain;
        case 0x0005: return ShaderStage::Compute;
        case 0x4C53: return ShaderStage::Library;
        default:     return ShaderStage::Unknown;
    }
}

ShaderStage DxilInspector::DecodeStageFromDxil(uint32_t token) {
    switch (token) {
        case 0:  return ShaderStage::Pixel;
        case 1:  return ShaderStage::Vertex;
        case 2:  return ShaderStage::Geometry;
        case 3:  return ShaderStage::Hull;
        case 4:  return ShaderStage::Domain;
        case 5:  return ShaderStage::Compute;
        case 6:  return ShaderStage::Library;
        case 7:  return ShaderStage::RayGeneration;
        case 8:  return ShaderStage::Intersection;
        case 9:  return ShaderStage::AnyHit;
        case 10: return ShaderStage::ClosestHit;
        case 11: return ShaderStage::Miss;
        case 12: return ShaderStage::Callable;
        case 13: return ShaderStage::Mesh;
        case 14: return ShaderStage::Amplification;
        default: return ShaderStage::Unknown;
    }
}

std::string DxilInspector::FourCCToString(uint32_t fourCC) {
    char buf[5] = { 0 };
    buf[0] = static_cast<char>(fourCC & 0xFF);
    buf[1] = static_cast<char>((fourCC >> 8) & 0xFF);
    buf[2] = static_cast<char>((fourCC >> 16) & 0xFF);
    buf[3] = static_cast<char>((fourCC >> 24) & 0xFF);
    buf[4] = '\0';
    return std::string(buf);
}

std::string DxilInspector::StageToString(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:        return "Vertex";
        case ShaderStage::Pixel:         return "Pixel";
        case ShaderStage::Geometry:      return "Geometry";
        case ShaderStage::Hull:          return "Hull";
        case ShaderStage::Domain:        return "Domain";
        case ShaderStage::Compute:       return "Compute";
        case ShaderStage::Library:       return "Library";
        case ShaderStage::RayGeneration: return "RayGeneration";
        case ShaderStage::Intersection:  return "Intersection";
        case ShaderStage::AnyHit:        return "AnyHit";
        case ShaderStage::ClosestHit:    return "ClosestHit";
        case ShaderStage::Miss:          return "Miss";
        case ShaderStage::Callable:      return "Callable";
        case ShaderStage::Mesh:          return "Mesh";
        case ShaderStage::Amplification: return "Amplification";
        default:                         return "Unknown";
    }
}

std::string DxilInspector::ContainerTypeToString(ShaderContainerType type) {
    switch (type) {
        case ShaderContainerType::LegacyDXBC: return "Legacy DXBC";
        case ShaderContainerType::DXIL:       return "DXIL (Shader Model 6.x)";
        case ShaderContainerType::Invalid:    return "Invalid / Corrupted";
        default:                              return "Unknown";
    }
}

std::string DxilInspector::ShaderModelToString(uint32_t major, uint32_t minor) {
    if (major == 0) return "Unknown";
    return "SM " + std::to_string(major) + "." + std::to_string(minor);
}

} // namespace kepler
