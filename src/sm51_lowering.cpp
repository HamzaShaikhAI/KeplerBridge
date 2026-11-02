#include "downgrader/sm51_lowering.h"
#include "downgrader/dxbc_checksum.h"
#include <cstring>
#include <vector>
#include <sstream>

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
#pragma pack(pop)

constexpr uint32_t FOURCC_DXBC = 0x43425844; // 'DXBC'
constexpr uint32_t FOURCC_SHEX = 0x58454853; // 'SHEX'

// D3D10/D3D11 Opcode constants
constexpr uint32_t OPCODE_DCL_RESOURCE             = 0x58; // 88
constexpr uint32_t OPCODE_DCL_CONSTANT_BUFFER      = 0x59; // 89
constexpr uint32_t OPCODE_DCL_SAMPLER              = 0x5A; // 90
constexpr uint32_t OPCODE_DCL_UAV_TYPED_ALT        = 0x64; // 100
constexpr uint32_t OPCODE_DCL_UAV_TYPED            = 0x9C; // 156
constexpr uint32_t OPCODE_DCL_UAV_RAW              = 0x9D; // 157
constexpr uint32_t OPCODE_DCL_UAV_STRUCTURED       = 0x9E; // 158

// Operand Types (bits 12..19 of operand token)
constexpr uint32_t OPERAND_TYPE_RESOURCE           = 7;
constexpr uint32_t OPERAND_TYPE_SAMPLER            = 10;
constexpr uint32_t OPERAND_TYPE_CONSTANT_BUFFER    = 14;
constexpr uint32_t OPERAND_TYPE_UAV                = 24;

inline uint32_t GetOperandType(uint32_t token) {
    return (token >> 12) & 0xFF;
}

inline uint32_t GetIndexDimension(uint32_t token) {
    return (token >> 20) & 0x3;
}

inline uint32_t SetIndexDimension(uint32_t token, uint32_t dim) {
    return (token & ~(0x3 << 20)) | ((dim & 0x3) << 20);
}

} // anonymous namespace

std::string SM51LoweringEngine::StatusToString(SM51LoweringStatus status) {
    switch (status) {
        case SM51LoweringStatus::Success:                    return "Success";
        case SM51LoweringStatus::NotSM51:                   return "Not a Shader Model 5.1 bytecode container";
        case SM51LoweringStatus::InvalidContainer:          return "Invalid or truncated DXBC container";
        case SM51LoweringStatus::MultiSpaceCollision:       return "Multiple register spaces detected (cannot flatten to space0)";
        case SM51LoweringStatus::UnboundedDescriptorRange:  return "Unbounded descriptor range detected (unsupported on FL 11_0)";
        case SM51LoweringStatus::DynamicIndexingUnsupported: return "Dynamic resource indexing across arrays unsupported";
        case SM51LoweringStatus::ExceedsD3D11BindingLimits: return "Resource register index exceeds Direct3D 11 hardware limits";
        case SM51LoweringStatus::MalformedBytecode:         return "Malformed or truncated instruction stream";
        default:                                            return "Unknown";
    }
}

bool SM51LoweringEngine::CanLower(
    const void* pBytecode,
    size_t size,
    SM51LoweringReport* pOutReport)
{
    SM51LoweringReport rep;
    if (!pBytecode || size < sizeof(DXBCHeader)) {
        rep.status = SM51LoweringStatus::InvalidContainer;
        rep.diagnostic = "Bytecode pointer is null or size smaller than DXBC header.";
        if (pOutReport) *pOutReport = rep;
        return false;
    }

    const uint8_t* pRaw = reinterpret_cast<const uint8_t*>(pBytecode);
    const DXBCHeader* pHdr = reinterpret_cast<const DXBCHeader*>(pRaw);

    if (pHdr->magic != FOURCC_DXBC) {
        rep.status = SM51LoweringStatus::InvalidContainer;
        rep.diagnostic = "Missing DXBC magic header.";
        if (pOutReport) *pOutReport = rep;
        return false;
    }

    if (pHdr->totalSize > size) {
        rep.status = SM51LoweringStatus::InvalidContainer;
        rep.diagnostic = "Container truncated.";
        if (pOutReport) *pOutReport = rep;
        return false;
    }

    // Locate SHEX chunk
    const uint32_t* chunkOffsets = reinterpret_cast<const uint32_t*>(pRaw + sizeof(DXBCHeader));
    int shexIdx = -1;
    for (uint32_t i = 0; i < pHdr->chunkCount; ++i) {
        if (chunkOffsets[i] + sizeof(DXBCChunkHeader) > pHdr->totalSize) {
            rep.status = SM51LoweringStatus::InvalidContainer;
            rep.diagnostic = "Chunk offset exceeds container size.";
            if (pOutReport) *pOutReport = rep;
            return false;
        }
        const DXBCChunkHeader* pChunk = reinterpret_cast<const DXBCChunkHeader*>(pRaw + chunkOffsets[i]);
        if (pChunk->fourCC == FOURCC_SHEX) {
            shexIdx = static_cast<int>(i);
            break;
        }
    }

    if (shexIdx < 0) {
        rep.status = SM51LoweringStatus::NotSM51;
        rep.diagnostic = "No SHEX chunk found.";
        if (pOutReport) *pOutReport = rep;
        return false;
    }

    uint32_t shexOffset = chunkOffsets[shexIdx];
    const DXBCChunkHeader* pShexChunk = reinterpret_cast<const DXBCChunkHeader*>(pRaw + shexOffset);
    if (shexOffset + sizeof(DXBCChunkHeader) + pShexChunk->chunkSize > pHdr->totalSize) {
        rep.status = SM51LoweringStatus::InvalidContainer;
        rep.diagnostic = "SHEX chunk body truncated.";
        if (pOutReport) *pOutReport = rep;
        return false;
    }

    const uint32_t* pShexData = reinterpret_cast<const uint32_t*>(pRaw + shexOffset + sizeof(DXBCChunkHeader));
    uint32_t dwordCount = pShexChunk->chunkSize / sizeof(uint32_t);
    rep.originalDwordCount = dwordCount;

    if (dwordCount < 2) {
        rep.status = SM51LoweringStatus::MalformedBytecode;
        rep.diagnostic = "SHEX chunk too small for version and length tokens.";
        if (pOutReport) *pOutReport = rep;
        return false;
    }

    uint32_t verToken = pShexData[0];
    uint32_t major = (verToken >> 4) & 0xF;
    uint32_t minor = verToken & 0xF;

    if (major != 5 || minor != 1) {
        rep.status = SM51LoweringStatus::NotSM51;
        rep.diagnostic = "Shader is not Shader Model 5.1 (found SM " + std::to_string(major) + "." + std::to_string(minor) + ").";
        if (pOutReport) *pOutReport = rep;
        return false;
    }

    // Inspect instructions for space, bounds, and limits
    uint32_t idx = 2;
    while (idx < dwordCount) {
        uint32_t token = pShexData[idx];
        uint32_t opcode = token & 0x7FF;
        uint32_t len = (token >> 24) & 0x7F;

        if (len == 0 || idx + len > dwordCount) {
            rep.status = SM51LoweringStatus::MalformedBytecode;
            rep.diagnostic = "Instruction length invalid at DWORD index " + std::to_string(idx);
            if (pOutReport) *pOutReport = rep;
            return false;
        }

        if (opcode == OPCODE_DCL_RESOURCE) {
            rep.declaredSRVs++;
            if (len == 7) {
                uint32_t regId = pShexData[idx + 2];
                uint32_t rangeLower = pShexData[idx + 3];
                uint32_t rangeUpper = pShexData[idx + 4];
                uint32_t spaceId = pShexData[idx + 6];

                if (spaceId != 0) {
                    rep.hasMultiSpace = true;
                    rep.status = SM51LoweringStatus::MultiSpaceCollision;
                    rep.diagnostic = "Resource T" + std::to_string(regId) + " resides in space " + std::to_string(spaceId) + " != space0.";
                    if (pOutReport) *pOutReport = rep;
                    return false;
                }
                if (rangeUpper == 0xFFFFFFFF || rangeUpper > rangeLower) {
                    rep.hasUnboundedRange = true;
                    rep.status = SM51LoweringStatus::UnboundedDescriptorRange;
                    rep.diagnostic = "Resource T" + std::to_string(regId) + " has unbounded or multi-element range [" +
                                     std::to_string(rangeLower) + ":" + std::to_string(rangeUpper) + "].";
                    if (pOutReport) *pOutReport = rep;
                    return false;
                }
                if (rangeLower >= 128 || rangeUpper >= 128) {
                    rep.status = SM51LoweringStatus::ExceedsD3D11BindingLimits;
                    rep.diagnostic = "Resource T register " + std::to_string(rangeLower) + " exceeds D3D11 limit of 128 SRVs.";
                    if (pOutReport) *pOutReport = rep;
                    return false;
                }
            }
        } else if (opcode == OPCODE_DCL_UAV_TYPED || opcode == OPCODE_DCL_UAV_TYPED_ALT ||
                   opcode == OPCODE_DCL_UAV_RAW || opcode == OPCODE_DCL_UAV_STRUCTURED) {
            rep.declaredUAVs++;
            if (len >= 7) {
                uint32_t regId = pShexData[idx + 2];
                uint32_t rangeLower = pShexData[idx + 3];
                uint32_t rangeUpper = pShexData[idx + 4];
                uint32_t spaceId = pShexData[idx + 6];

                if (spaceId != 0) {
                    rep.hasMultiSpace = true;
                    rep.status = SM51LoweringStatus::MultiSpaceCollision;
                    rep.diagnostic = "UAV U" + std::to_string(regId) + " resides in space " + std::to_string(spaceId) + " != space0.";
                    if (pOutReport) *pOutReport = rep;
                    return false;
                }
                if (rangeUpper == 0xFFFFFFFF || rangeUpper > rangeLower) {
                    rep.hasUnboundedRange = true;
                    rep.status = SM51LoweringStatus::UnboundedDescriptorRange;
                    rep.diagnostic = "UAV U" + std::to_string(regId) + " has unbounded range.";
                    if (pOutReport) *pOutReport = rep;
                    return false;
                }
                if (rangeLower >= 8 || rangeUpper >= 8) {
                    rep.status = SM51LoweringStatus::ExceedsD3D11BindingLimits;
                    rep.diagnostic = "UAV register u" + std::to_string(rangeLower) + " exceeds Feature Level 11_0 limit of 8 UAVs.";
                    if (pOutReport) *pOutReport = rep;
                    return false;
                }
            }
        } else if (opcode == OPCODE_DCL_CONSTANT_BUFFER) {
            rep.declaredCBVs++;
            if (len >= 6) {
                uint32_t regId = pShexData[idx + 2];
                uint32_t rangeLower = pShexData[idx + 3];
                uint32_t rangeUpper = pShexData[idx + 4];
                uint32_t spaceId = (len >= 7) ? pShexData[idx + 6] : 0;
                if (spaceId != 0) {
                    rep.hasMultiSpace = true;
                    rep.status = SM51LoweringStatus::MultiSpaceCollision;
                    rep.diagnostic = "Constant buffer CB" + std::to_string(regId) + " in space " + std::to_string(spaceId);
                    if (pOutReport) *pOutReport = rep;
                    return false;
                }
                if (rangeLower >= 14 || rangeUpper >= 14) {
                    rep.status = SM51LoweringStatus::ExceedsD3D11BindingLimits;
                    rep.diagnostic = "Constant buffer register b" + std::to_string(rangeLower) + " exceeds D3D11 limit of 14 CBVs.";
                    if (pOutReport) *pOutReport = rep;
                    return false;
                }
            }
        } else if (opcode == OPCODE_DCL_SAMPLER) {
            rep.declaredSamplers++;
            if (len == 6) {
                uint32_t regId = pShexData[idx + 2];
                uint32_t rangeLower = pShexData[idx + 3];
                uint32_t rangeUpper = pShexData[idx + 4];
                uint32_t spaceId = pShexData[idx + 5];
                if (spaceId != 0) {
                    rep.hasMultiSpace = true;
                    rep.status = SM51LoweringStatus::MultiSpaceCollision;
                    rep.diagnostic = "Sampler S" + std::to_string(regId) + " in space " + std::to_string(spaceId);
                    if (pOutReport) *pOutReport = rep;
                    return false;
                }
                if (rangeLower >= 16 || rangeUpper >= 16) {
                    rep.status = SM51LoweringStatus::ExceedsD3D11BindingLimits;
                    rep.diagnostic = "Sampler register s" + std::to_string(rangeLower) + " exceeds D3D11 limit of 16 samplers.";
                    if (pOutReport) *pOutReport = rep;
                    return false;
                }
            }
        }

        idx += len;
    }

    rep.status = SM51LoweringStatus::Success;
    rep.diagnostic = "Shader Model 5.1 bytecode satisfies bounded lowering invariants.";
    if (pOutReport) *pOutReport = rep;
    return true;
}

bool SM51LoweringEngine::LowerSM51ToSM50(
    const void* pSrcBytecode,
    size_t srcSize,
    std::vector<uint8_t>& outLoweredBytecode,
    SM51LoweringReport* pOutReport)
{
    outLoweredBytecode.clear();
    SM51LoweringReport rep;

    if (!CanLower(pSrcBytecode, srcSize, &rep)) {
        if (pOutReport) *pOutReport = rep;
        return false;
    }

    const uint8_t* pRaw = reinterpret_cast<const uint8_t*>(pSrcBytecode);
    const DXBCHeader* pHdr = reinterpret_cast<const DXBCHeader*>(pRaw);
    const uint32_t* chunkOffsets = reinterpret_cast<const uint32_t*>(pRaw + sizeof(DXBCHeader));

    int shexIdx = -1;
    for (uint32_t i = 0; i < pHdr->chunkCount; ++i) {
        const DXBCChunkHeader* pChunk = reinterpret_cast<const DXBCChunkHeader*>(pRaw + chunkOffsets[i]);
        if (pChunk->fourCC == FOURCC_SHEX) {
            shexIdx = static_cast<int>(i);
            break;
        }
    }

    if (shexIdx < 0) {
        rep.status = SM51LoweringStatus::NotSM51;
        rep.diagnostic = "SHEX chunk missing.";
        if (pOutReport) *pOutReport = rep;
        return false;
    }

    uint32_t shexOffset = chunkOffsets[shexIdx];
    const DXBCChunkHeader* pShexChunk = reinterpret_cast<const DXBCChunkHeader*>(pRaw + shexOffset);
    const uint32_t* pShexData = reinterpret_cast<const uint32_t*>(pRaw + shexOffset + sizeof(DXBCChunkHeader));
    uint32_t srcDwords = pShexChunk->chunkSize / sizeof(uint32_t);

    std::vector<uint32_t> dstShex;
    dstShex.reserve(srcDwords);

    // 0: Version token -> SM 5.0 (preserve stage in high 16 bits, set 0x0050 in low 16 bits)
    uint32_t origVerToken = pShexData[0];
    uint32_t loweredVerToken = (origVerToken & 0xFFFF0000) | 0x0050;
    dstShex.push_back(loweredVerToken);

    // 1: Length token placeholder
    dstShex.push_back(0);

    uint32_t idx = 2;
    while (idx < srcDwords) {
        uint32_t token = pShexData[idx];
        uint32_t opcode = token & 0x7FF;
        uint32_t len = (token >> 24) & 0x7F;

        if (opcode == OPCODE_DCL_RESOURCE && len == 7) {
            // Lower SM 5.1 dcl_resource (7 DWORDs) to SM 5.0 (4 DWORDs)
            uint32_t newOpToken = (token & 0x00FFFFFF) | (4 << 24);
            uint32_t opToken = pShexData[idx + 1];
            uint32_t newOpTokenOperand = SetIndexDimension(opToken, 1);
            uint32_t regId = pShexData[idx + 3]; // rangeLower is the HLSL register index
            uint32_t retType = pShexData[idx + 5];

            dstShex.push_back(newOpToken);
            dstShex.push_back(newOpTokenOperand);
            dstShex.push_back(regId);
            dstShex.push_back(retType);
            idx += 7;
        } else if ((opcode == OPCODE_DCL_UAV_TYPED || opcode == OPCODE_DCL_UAV_TYPED_ALT) && len == 7) {
            // Lower SM 5.1 dcl_uav_typed (7 DWORDs) to SM 5.0 (4 DWORDs)
            uint32_t newOpToken = (token & 0x00FFFFFF) | (4 << 24);
            uint32_t opToken = pShexData[idx + 1];
            uint32_t newOpTokenOperand = SetIndexDimension(opToken, 1);
            uint32_t regId = pShexData[idx + 3]; // rangeLower is the HLSL register index
            uint32_t retType = pShexData[idx + 5];

            dstShex.push_back(newOpToken);
            dstShex.push_back(newOpTokenOperand);
            dstShex.push_back(regId);
            dstShex.push_back(retType);
            idx += 7;
        } else if (opcode == OPCODE_DCL_CONSTANT_BUFFER && len >= 6) {
            // Lower SM 5.1 dcl_constantbuffer (6 or 7 DWORDs) to SM 5.0 (4 DWORDs)
            uint32_t newOpToken = (token & 0x00FFFFFF) | (4 << 24);
            uint32_t opToken = pShexData[idx + 1];
            uint32_t newOpTokenOperand = SetIndexDimension(opToken, 2);
            uint32_t regId = pShexData[idx + 3]; // rangeLower is the HLSL register index
            uint32_t sizeVectors = pShexData[idx + 5];

            dstShex.push_back(newOpToken);
            dstShex.push_back(newOpTokenOperand);
            dstShex.push_back(regId);
            dstShex.push_back(sizeVectors);
            idx += len;
        } else if (opcode == OPCODE_DCL_SAMPLER && len == 6) {
            // Lower SM 5.1 dcl_sampler (6 DWORDs) to SM 5.0 (3 DWORDs)
            uint32_t newOpToken = (token & 0x00FFFFFF) | (3 << 24);
            uint32_t opToken = pShexData[idx + 1];
            uint32_t newOpTokenOperand = SetIndexDimension(opToken, 1);
            uint32_t regId = pShexData[idx + 3]; // rangeLower is the HLSL register index

            dstShex.push_back(newOpToken);
            dstShex.push_back(newOpTokenOperand);
            dstShex.push_back(regId);
            idx += 6;
        } else {
            // Non-declaration instructions: check operands for 2D/3D resource indexing
            // Scan operands to see if any need lowering
            std::vector<uint32_t> loweredInst;
            loweredInst.reserve(len);

            uint32_t opIdx = 1;
            bool instructionModified = false;

            while (opIdx < len) {
                uint32_t opToken = pShexData[idx + opIdx];
                uint32_t opType = GetOperandType(opToken);
                uint32_t dim = GetIndexDimension(opToken);

                if (opType == OPERAND_TYPE_RESOURCE && dim == 2) {
                    // Lower T0[0] (2D index: range, reg) -> t0 (1D index: reg)
                    instructionModified = true;
                    loweredInst.push_back(SetIndexDimension(opToken, 1));
                    loweredInst.push_back(pShexData[idx + opIdx + 1]); // reg index
                    opIdx += 3; // skipped range index DWORD
                } else if (opType == OPERAND_TYPE_UAV && dim == 2) {
                    // Lower U0[0] (2D index: range, reg) -> u0 (1D index: reg)
                    instructionModified = true;
                    loweredInst.push_back(SetIndexDimension(opToken, 1));
                    loweredInst.push_back(pShexData[idx + opIdx + 1]); // reg index
                    opIdx += 3; // skipped range index DWORD
                } else if (opType == OPERAND_TYPE_SAMPLER && dim == 2) {
                    // Lower S0[0] (2D index: range, reg) -> s0 (1D index: reg)
                    instructionModified = true;
                    loweredInst.push_back(SetIndexDimension(opToken, 1));
                    loweredInst.push_back(pShexData[idx + opIdx + 1]); // reg index
                    opIdx += 3; // skipped range index DWORD
                } else if (opType == OPERAND_TYPE_CONSTANT_BUFFER && dim == 3) {
                    // Lower CB0[0][vec] (3D index: array, reg, vec) -> cb0[vec] (2D index: reg, vec)
                    instructionModified = true;
                    loweredInst.push_back(SetIndexDimension(opToken, 2));
                    loweredInst.push_back(pShexData[idx + opIdx + 2]); // reg index
                    loweredInst.push_back(pShexData[idx + opIdx + 3]); // vector index
                    opIdx += 4; // skipped array index DWORD
                } else {
                    // Standard operand: copy 1 dword
                    loweredInst.push_back(opToken);
                    opIdx += 1;
                }
            }

            if (instructionModified) {
                uint32_t newLen = static_cast<uint32_t>(loweredInst.size() + 1);
                uint32_t newOpcodeToken = (token & 0x00FFFFFF) | (newLen << 24);
                dstShex.push_back(newOpcodeToken);
                dstShex.insert(dstShex.end(), loweredInst.begin(), loweredInst.end());
            } else {
                for (uint32_t k = 0; k < len; ++k) {
                    dstShex.push_back(pShexData[idx + k]);
                }
            }
            idx += len;
        }
    }

    dstShex[1] = static_cast<uint32_t>(dstShex.size());
    rep.loweredDwordCount = static_cast<uint32_t>(dstShex.size());
    uint32_t newShexSizeBytes = static_cast<uint32_t>(dstShex.size() * sizeof(uint32_t));

    // Reassemble container
    uint32_t chunkCount = pHdr->chunkCount;
    std::vector<uint32_t> newChunkOffsets;
    newChunkOffsets.reserve(chunkCount);

    outLoweredBytecode.resize(sizeof(DXBCHeader) + chunkCount * sizeof(uint32_t));

    for (uint32_t i = 0; i < chunkCount; ++i) {
        uint32_t curOffset = static_cast<uint32_t>(outLoweredBytecode.size());
        newChunkOffsets.push_back(curOffset);

        if (i == static_cast<uint32_t>(shexIdx)) {
            // Write SHEX header
            const char tag[4] = { 'S', 'H', 'E', 'X' };
            outLoweredBytecode.insert(outLoweredBytecode.end(), tag, tag + 4);
            const uint8_t* pSize = reinterpret_cast<const uint8_t*>(&newShexSizeBytes);
            outLoweredBytecode.insert(outLoweredBytecode.end(), pSize, pSize + 4);

            // Write lowered SHEX body
            const uint8_t* pBody = reinterpret_cast<const uint8_t*>(dstShex.data());
            outLoweredBytecode.insert(outLoweredBytecode.end(), pBody, pBody + newShexSizeBytes);
        } else {
            uint32_t oldOff = chunkOffsets[i];
            const DXBCChunkHeader* pOldChunk = reinterpret_cast<const DXBCChunkHeader*>(pRaw + oldOff);
            uint32_t fullSize = sizeof(DXBCChunkHeader) + pOldChunk->chunkSize;
            outLoweredBytecode.insert(outLoweredBytecode.end(), pRaw + oldOff, pRaw + oldOff + fullSize);
        }
    }

    // Write Container Header
    uint8_t* pDstRaw = outLoweredBytecode.data();
    memcpy(pDstRaw, "DXBC", 4);
    memset(pDstRaw + 4, 0, 16); // zero checksum slot

    uint32_t totalSize = static_cast<uint32_t>(outLoweredBytecode.size());
    uint32_t ver = 1;
    memcpy(pDstRaw + 20, &ver, 4);
    memcpy(pDstRaw + 24, &totalSize, 4);
    memcpy(pDstRaw + 28, &chunkCount, 4);

    for (uint32_t i = 0; i < chunkCount; ++i) {
        memcpy(pDstRaw + sizeof(DXBCHeader) + i * sizeof(uint32_t), &newChunkOffsets[i], sizeof(uint32_t));
    }

    // Calculate authentic DXBC checksum
    uint32_t checksum[4] = { 0 };
    if (!CalculateDXBCChecksum(outLoweredBytecode.data(), outLoweredBytecode.size(), checksum)) {
        rep.status = SM51LoweringStatus::MalformedBytecode;
        rep.diagnostic = "Failed to calculate DXBC container checksum.";
        if (pOutReport) *pOutReport = rep;
        return false;
    }

    memcpy(pDstRaw + 4, checksum, 16);

    rep.status = SM51LoweringStatus::Success;
    rep.diagnostic = "Successfully lowered SM 5.1 bytecode to authentic SM 5.0 DXBC.";
    if (pOutReport) *pOutReport = rep;
    return true;
}

} // namespace kepler
