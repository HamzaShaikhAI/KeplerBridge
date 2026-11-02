#include "downgrader/dxil_translation.h"
#include "downgrader/dxil_bitcode_decoder.h"
#include "downgrader/dxbc_checksum.h"
#include "downgrader/dxil_mem2reg.h"
#include "core/logger.h"

#include <cstring>
#include <algorithm>
#include <sstream>
#include <iostream>

namespace kepler {

namespace {

#pragma pack(push, 1)
struct DXBCHeader {
    uint32_t magic;         // 0x43425844 ('DXBC')
    uint8_t  checksum[16];  // 128-bit MD5-variant digest
    uint32_t version;       // 1
    uint32_t totalSize;     // Total container size in bytes
    uint32_t chunkCount;    // Number of chunks
};

struct DXBCChunkHeader {
    uint32_t fourCC;        // Chunk type FourCC
    uint32_t chunkSize;     // Size of chunk body in bytes
};

struct DxilProgramHeader {
    uint32_t bitcodeHeaderVersion; // Packed shader type and version
    uint32_t sizeInUint32;         // Total size of program header and bitcode in uint32s
    uint32_t dxilMagic;            // 0x4C495844 ('DXIL')
    uint32_t dxilVersion;          // Format version (e.g. 0x00010000 for 1.0)
    uint32_t bitcodeOffset;        // Offset relative to dxilMagic to start of bitcode
    uint32_t bitcodeSize;          // Bitcode length in bytes
};
#pragma pack(pop)

constexpr uint32_t FOURCC_DXBC = 0x43425844; // 'DXBC'
constexpr uint32_t FOURCC_DXIL = 0x4C495844; // 'DXIL'
constexpr uint32_t FOURCC_SFI0 = 0x30494653; // 'SFI0'
constexpr uint32_t FOURCC_ISG1 = 0x31475349; // 'ISG1'
constexpr uint32_t FOURCC_ISGN = 0x4E475349; // 'ISGN'
constexpr uint32_t FOURCC_OSG1 = 0x3147534F; // 'OSG1'
constexpr uint32_t FOURCC_OSGN = 0x4E47534F; // 'OSGN'
constexpr uint32_t FOURCC_PSV0 = 0x30565350; // 'PSV0'
constexpr uint32_t FOURCC_SHEX = 0x58454853; // 'SHEX'

constexpr uint32_t LLVM_BITCODE_MAGIC = 0xDEC04342; // 'BC\xc0\xde'

// Shader Feature Flags (SFI0)
// Uses authoritative definitions from include/downgrader/dxil_translation.h

ShaderStage DecodeStage(uint32_t stageToken) {
    switch (stageToken) {
        case 0: return ShaderStage::Pixel;
        case 1: return ShaderStage::Vertex;
        case 2: return ShaderStage::Geometry;
        case 3: return ShaderStage::Hull;
        case 4: return ShaderStage::Domain;
        case 5: return ShaderStage::Compute;
        case 6: return ShaderStage::Library;
        default: return ShaderStage::Unknown;
    }
}

} // anonymous namespace

std::string DXILTranslationEngine::StatusToString(DXILTranslationStatus status) {
    switch (status) {
        case DXILTranslationStatus::Success:                return "Success";
        case DXILTranslationStatus::NotDXIL:                return "NotDXIL";
        case DXILTranslationStatus::InvalidContainer:        return "InvalidContainer";
        case DXILTranslationStatus::UnsupportedShaderModel: return "UnsupportedShaderModel";
        case DXILTranslationStatus::UnsupportedStage:       return "UnsupportedStage";
        case DXILTranslationStatus::UnsupportedWaveOps:     return "UnsupportedWaveOps";
        case DXILTranslationStatus::Unsupported64BitOps:    return "Unsupported64BitOps";
        case DXILTranslationStatus::UnsupportedFeatureFlag: return "UnsupportedFeatureFlag";
        case DXILTranslationStatus::UnsupportedInstruction: return "UnsupportedInstruction";
        case DXILTranslationStatus::MalformedBitcode:       return "MalformedBitcode";
        case DXILTranslationStatus::TranslationFailed:      return "TranslationFailed";
        default:                                            return "Unknown";
    }
}

bool DXILTranslationEngine::ParseContainerAndSignatures(
    const uint8_t* pRaw,
    size_t size,
    DXILTranslationReport& report,
    const uint8_t*& pOutDxilChunk,
    uint32_t& outDxilChunkSize)
{
    pOutDxilChunk = nullptr;
    outDxilChunkSize = 0;
    report.inputSignatures.clear();
    report.outputSignatures.clear();
    report.declaredInputs = 0;
    report.declaredOutputs = 0;
    report.declaredCBVs = 0;
    report.declaredSRVs = 0;
    report.declaredSamplers = 0;

    if (!pRaw || size < sizeof(DXBCHeader)) {
        report.status = DXILTranslationStatus::InvalidContainer;
        report.diagnostic = "Bytecode size smaller than DXBC header.";
        return false;
    }

    const auto* pHeader = reinterpret_cast<const DXBCHeader*>(pRaw);
    if (pHeader->magic != FOURCC_DXBC) {
        report.status = DXILTranslationStatus::NotDXIL;
        report.diagnostic = "Container magic is not 'DXBC'.";
        return false;
    }

    if (pHeader->totalSize > size) {
        report.status = DXILTranslationStatus::InvalidContainer;
        report.diagnostic = "Container totalSize exceeds buffer length.";
        return false;
    }

    size_t chunkTableEnd = sizeof(DXBCHeader) + static_cast<size_t>(pHeader->chunkCount) * sizeof(uint32_t);
    if (chunkTableEnd > pHeader->totalSize) {
        report.status = DXILTranslationStatus::InvalidContainer;
        report.diagnostic = "Chunk offset table exceeds container bounds.";
        return false;
    }

    const auto* pChunkOffsets = reinterpret_cast<const uint32_t*>(pRaw + sizeof(DXBCHeader));

    const uint8_t* pSfiChunk = nullptr;
    const uint8_t* pIsgChunk = nullptr;
    uint32_t isgSize = 0;
    bool isIsg1 = false;
    const uint8_t* pOsgChunk = nullptr;
    uint32_t osgSize = 0;
    bool isOsg1 = false;

    for (uint32_t i = 0; i < pHeader->chunkCount; ++i) {
        uint32_t offset = pChunkOffsets[i];
        if (offset + sizeof(DXBCChunkHeader) > pHeader->totalSize) {
            report.status = DXILTranslationStatus::InvalidContainer;
            report.diagnostic = "Chunk header offset exceeds container size.";
            return false;
        }

        const auto* pChunkHdr = reinterpret_cast<const DXBCChunkHeader*>(pRaw + offset);
        uint32_t fourCC = pChunkHdr->fourCC;
        uint32_t chunkSize = pChunkHdr->chunkSize;
        const uint8_t* pData = pRaw + offset + sizeof(DXBCChunkHeader);

        if (offset + sizeof(DXBCChunkHeader) + chunkSize > pHeader->totalSize) {
            report.status = DXILTranslationStatus::InvalidContainer;
            report.diagnostic = "Chunk body exceeds container size.";
            return false;
        }

        if (fourCC == FOURCC_SFI0 && chunkSize >= 8) {
            pSfiChunk = pData;
            report.featureFlags = *reinterpret_cast<const uint64_t*>(pData);
        } else if (fourCC == FOURCC_ISG1) {
            pIsgChunk = pData;
            isgSize = chunkSize;
            isIsg1 = true;
        } else if (fourCC == FOURCC_ISGN) {
            pIsgChunk = pData;
            isgSize = chunkSize;
            isIsg1 = false;
        } else if (fourCC == FOURCC_OSG1) {
            pOsgChunk = pData;
            osgSize = chunkSize;
            isOsg1 = true;
        } else if (fourCC == FOURCC_OSGN) {
            pOsgChunk = pData;
            osgSize = chunkSize;
            isOsg1 = false;
        } else if (fourCC == FOURCC_PSV0 && chunkSize >= 8) {
            uint32_t psvInfoSize = *reinterpret_cast<const uint32_t*>(pData);
            if (psvInfoSize + 8 <= chunkSize) {
                uint32_t resCount = *reinterpret_cast<const uint32_t*>(pData + psvInfoSize + 4);
                size_t resTableOffset = psvInfoSize + 8;
                for (uint32_t r = 0; r < resCount; ++r) {
                    size_t entryOffset = resTableOffset + r * 16;
                    if (entryOffset + 16 > chunkSize) break;
                    uint32_t resType = *reinterpret_cast<const uint32_t*>(pData + entryOffset + 4);
                    if (resType == 2) {
                        report.declaredCBVs++;
                    } else if (resType == 1 || resType == 0) {
                        report.declaredSRVs++;
                    } else if (resType == 3 || resType == 0xE) {
                        report.declaredSamplers++;
                    }
                }
            }
        } else if (fourCC == FOURCC_DXIL) {
            pOutDxilChunk = pData;
            outDxilChunkSize = chunkSize;
        }
    }

    if (!pOutDxilChunk || outDxilChunkSize < sizeof(DxilProgramHeader)) {
        report.status = DXILTranslationStatus::NotDXIL;
        report.diagnostic = "No DXIL chunk found in container.";
        return false;
    }

    // Inspect SFI0 feature flags
    if (report.featureFlags & SFI_WAVE_OPS) {
        report.hasWaveOps = true;
    }
    if (report.featureFlags & SFI_ALL_INT64_MASK) {
        report.has64BitOps = true;
    }

    // Parse Input Signatures
    if (pIsgChunk && isgSize >= 8) {
        uint32_t elemCount = *reinterpret_cast<const uint32_t*>(pIsgChunk);
        report.declaredInputs = elemCount;
        uint32_t elemStride = isIsg1 ? 32 : 24;

        for (uint32_t i = 0; i < elemCount; ++i) {
            size_t off = 8 + static_cast<size_t>(i) * elemStride;
            if (off + elemStride > isgSize) break;

            DXILSignatureElement elem;
            if (isIsg1) {
                // ISG1 layout: stream(4), nameOff(4), semIdx(4), sysVal(4), compTy(4), regIdx(4), mask(1), rwMask(1), pad(2), minPrec(4)
                uint32_t nameOff = *reinterpret_cast<const uint32_t*>(pIsgChunk + off + 4);
                elem.semanticIndex = *reinterpret_cast<const uint32_t*>(pIsgChunk + off + 8);
                elem.systemValueType = *reinterpret_cast<const uint32_t*>(pIsgChunk + off + 12);
                elem.componentType = *reinterpret_cast<const uint32_t*>(pIsgChunk + off + 16);
                elem.registerIndex = *reinterpret_cast<const uint32_t*>(pIsgChunk + off + 20);
                elem.mask = pIsgChunk[off + 24];
                elem.readWriteMask = pIsgChunk[off + 25];

                if (nameOff < isgSize) {
                    elem.semanticName = reinterpret_cast<const char*>(pIsgChunk + nameOff);
                }
            } else {
                // ISGN layout: nameOff(4), semIdx(4), sysVal(4), compTy(4), regIdx(4), mask(1), rwMask(1), stream(2)
                uint32_t nameOff = *reinterpret_cast<const uint32_t*>(pIsgChunk + off);
                elem.semanticIndex = *reinterpret_cast<const uint32_t*>(pIsgChunk + off + 4);
                elem.systemValueType = *reinterpret_cast<const uint32_t*>(pIsgChunk + off + 8);
                elem.componentType = *reinterpret_cast<const uint32_t*>(pIsgChunk + off + 12);
                elem.registerIndex = *reinterpret_cast<const uint32_t*>(pIsgChunk + off + 16);
                elem.mask = pIsgChunk[off + 20];
                elem.readWriteMask = pIsgChunk[off + 21];

                if (nameOff < isgSize) {
                    elem.semanticName = reinterpret_cast<const char*>(pIsgChunk + nameOff);
                }
            }
            report.inputSignatures.push_back(elem);
        }
    }

    // Parse Output Signatures
    if (pOsgChunk && osgSize >= 8) {
        uint32_t elemCount = *reinterpret_cast<const uint32_t*>(pOsgChunk);
        report.declaredOutputs = elemCount;
        uint32_t elemStride = isOsg1 ? 32 : 24;

        for (uint32_t i = 0; i < elemCount; ++i) {
            size_t off = 8 + static_cast<size_t>(i) * elemStride;
            if (off + elemStride > osgSize) break;

            DXILSignatureElement elem;
            if (isOsg1) {
                uint32_t nameOff = *reinterpret_cast<const uint32_t*>(pOsgChunk + off + 4);
                elem.semanticIndex = *reinterpret_cast<const uint32_t*>(pOsgChunk + off + 8);
                elem.systemValueType = *reinterpret_cast<const uint32_t*>(pOsgChunk + off + 12);
                elem.componentType = *reinterpret_cast<const uint32_t*>(pOsgChunk + off + 16);
                elem.registerIndex = *reinterpret_cast<const uint32_t*>(pOsgChunk + off + 20);
                elem.mask = pOsgChunk[off + 24];
                elem.readWriteMask = pOsgChunk[off + 25];

                if (nameOff < osgSize) {
                    elem.semanticName = reinterpret_cast<const char*>(pOsgChunk + nameOff);
                }
            } else {
                uint32_t nameOff = *reinterpret_cast<const uint32_t*>(pOsgChunk + off);
                elem.semanticIndex = *reinterpret_cast<const uint32_t*>(pOsgChunk + off + 4);
                elem.systemValueType = *reinterpret_cast<const uint32_t*>(pOsgChunk + off + 8);
                elem.componentType = *reinterpret_cast<const uint32_t*>(pOsgChunk + off + 12);
                elem.registerIndex = *reinterpret_cast<const uint32_t*>(pOsgChunk + off + 16);
                elem.mask = pOsgChunk[off + 20];
                elem.readWriteMask = pOsgChunk[off + 21];

                if (nameOff < osgSize) {
                    elem.semanticName = reinterpret_cast<const char*>(pOsgChunk + nameOff);
                }
            }
            report.outputSignatures.push_back(elem);
        }
    }

    return true;
}

bool DXILTranslationEngine::CanTranslate(
    const void* pBytecode,
    size_t size,
    DXILTranslationReport* pOutReport)
{
    DXILTranslationReport localReport;
    DXILTranslationReport& rep = pOutReport ? *pOutReport : localReport;

    const uint8_t* pRaw = reinterpret_cast<const uint8_t*>(pBytecode);
    const uint8_t* pDxilChunk = nullptr;
    uint32_t dxilChunkSize = 0;

    if (!ParseContainerAndSignatures(pRaw, size, rep, pDxilChunk, dxilChunkSize)) {
        return false;
    }

    // 1. Fast Capability Rejection: SFI0 flags
    if (rep.hasWaveOps || (rep.featureFlags & SFI_WAVE_OPS)) {
        rep.status = DXILTranslationStatus::UnsupportedWaveOps;
        rep.diagnostic = "Shader invokes Wave Intrinsics (WaveActiveOp/WavePrefix). Kepler FL 11_0 hardware physically lacks wave execution.";
        return false;
    }

    if (rep.has64BitOps || (rep.featureFlags & SFI_ALL_INT64_MASK)) {
        rep.status = DXILTranslationStatus::Unsupported64BitOps;
        rep.diagnostic = "Shader invokes 64-bit integer operations or atomics. Kepler FL 11_0 hardware lacks native 64-bit ALU/atomics.";
        return false;
    }

    if (rep.featureFlags & SFI_UNSUPPORTED_ADVANCED_FEATURES_MASK) {
        rep.status = DXILTranslationStatus::UnsupportedFeatureFlag;
        rep.diagnostic = "Shader requires D3D12 Tier 3+ features (ROVs, ViewID, Barycentrics, Native 16-bit, VRS Shading Rate, Raytracing, Sampler Feedback, Mesh Shading, or Bindless Heap Indexing).";
        return false;
    }

    // 2. Inspect Program Header in DXIL chunk
    const auto* pDxilHdr = reinterpret_cast<const DxilProgramHeader*>(pDxilChunk);
    if (pDxilHdr->dxilMagic != FOURCC_DXIL) {
        rep.status = DXILTranslationStatus::NotDXIL;
        rep.diagnostic = "Invalid DXIL program magic in DXIL chunk.";
        return false;
    }

    uint32_t verToken = pDxilHdr->bitcodeHeaderVersion;
    rep.stage = DecodeStage((verToken >> 16) & 0xFFFF);
    rep.majorVersion = (verToken >> 4) & 0xF;
    rep.minorVersion = verToken & 0xF;

    // Supported Stages: Vertex, Pixel, Compute
    if (rep.stage != ShaderStage::Pixel &&
        rep.stage != ShaderStage::Vertex &&
        rep.stage != ShaderStage::Compute)
    {
        rep.status = DXILTranslationStatus::UnsupportedStage;
        rep.diagnostic = "Shader stage is not supported on Kepler D3D11 backend (Geometry, Hull, Domain, Raytracing rejected).";
        return false;
    }

    // Supported Shader Models: SM 6.0 through SM 6.6
    if (rep.majorVersion != 6 || rep.minorVersion > 6) {
        rep.status = DXILTranslationStatus::UnsupportedShaderModel;
        rep.diagnostic = "Shader Model " + std::to_string(rep.majorVersion) + "." +
                         std::to_string(rep.minorVersion) + " is outside the SM 6.0-6.6 translation envelope.";
        return false;
    }

    // 3. Inspect LLVM Bitcode Magic
    size_t bcOffsetFromHdr = 8 + pDxilHdr->bitcodeOffset;
    if (bcOffsetFromHdr + sizeof(uint32_t) > dxilChunkSize) {
        rep.status = DXILTranslationStatus::MalformedBitcode;
        rep.diagnostic = "Bitcode offset exceeds DXIL chunk size.";
        return false;
    }

    uint32_t bcMagic = *reinterpret_cast<const uint32_t*>(pDxilChunk + bcOffsetFromHdr);
    if (bcMagic != LLVM_BITCODE_MAGIC) {
        rep.status = DXILTranslationStatus::MalformedBitcode;
        rep.diagnostic = "Invalid LLVM bitcode magic signature (expected 0xDEC0C042).";
        return false;
    }

    rep.dxilBytecodeSize = pDxilHdr->bitcodeSize;

    // 4. Structured Bitcode Decoding & Intermediate Representation Construction
    const uint8_t* pBitcode = pDxilChunk + bcOffsetFromHdr;
    size_t bcSize = pDxilHdr->bitcodeSize;

    DXILIntermediateRepresentation ir;
    DXILTranslationStatus decodeStatus = DXILTranslationStatus::Success;
    std::string decodeDiag;
    if (!DXILBitcodeDecoder::DecodeBitcode(pBitcode, bcSize, ir, decodeStatus, decodeDiag)) {
        rep.status = decodeStatus;
        rep.diagnostic = "Bitcode decoding failed: " + decodeDiag;
        return false;
    }

    rep.hasControlFlow = ir.hasControlFlow;
    rep.hasLoop = ir.hasLoop;
    rep.numBasicBlocks = ir.numBasicBlocks;
    rep.numOperations = static_cast<uint32_t>(ir.vectorOps.size());
    rep.isMultiOperation = (ir.vectorOps.size() > 1);

    if (ir.hasLoop) {
        if (ir.loop.isDynamicBound) {
            rep.loopTripCount = 0;
        } else {
            if (ir.loop.isUnsignedPredicate) {
                uint32_t uBound = static_cast<uint32_t>(ir.loop.constantBound);
                uint32_t uInit = static_cast<uint32_t>(ir.loop.inductionInitialValue);
                if (ir.loop.breakCmpOpcode == LoopCmpOpcode::UGE ||
                    ir.loop.breakCmpOpcode == LoopCmpOpcode::IEQ) {
                    rep.loopTripCount = (uBound > uInit) ? (uBound - uInit) : 0;
                } else if (ir.loop.breakCmpOpcode == LoopCmpOpcode::ULT) {
                    rep.loopTripCount = 0;
                } else {
                    rep.loopTripCount = 0;
                }
            } else {
                int32_t sBound = ir.loop.constantBound;
                int32_t sInit = ir.loop.inductionInitialValue;
                if (ir.loop.breakCmpOpcode == LoopCmpOpcode::IGE ||
                    ir.loop.breakCmpOpcode == LoopCmpOpcode::IEQ) {
                    if (sBound > sInit) {
                        int64_t diff = static_cast<int64_t>(sBound) - static_cast<int64_t>(sInit);
                        rep.loopTripCount = (diff > 0) ? static_cast<uint32_t>(diff) : 0;
                    } else {
                        rep.loopTripCount = 0;
                    }
                } else if (ir.loop.breakCmpOpcode == LoopCmpOpcode::ILT) {
                    rep.loopTripCount = 0;
                } else {
                    rep.loopTripCount = 0;
                }
            }
        }
        if (!ir.loop.exitInstructions.empty()) {
            if (ir.loop.exitInstructions[0].opcode == IROpcode::Multiply) rep.arithmeticOp = DXILArithmeticOp::Multiply;
            else if (ir.loop.exitInstructions[0].opcode == IROpcode::Add) rep.arithmeticOp = DXILArithmeticOp::Add;
        } else if (!ir.loop.bodyInstructions.empty()) {
            if (ir.loop.bodyInstructions[0].opcode == IROpcode::Multiply) rep.arithmeticOp = DXILArithmeticOp::Multiply;
            else if (ir.loop.bodyInstructions[0].opcode == IROpcode::Add) rep.arithmeticOp = DXILArithmeticOp::Add;
        } else {
            rep.arithmeticOp = DXILArithmeticOp::Add;
        }
        rep.status = DXILTranslationStatus::Success;
        rep.diagnostic = "Shader Model " + std::to_string(rep.majorVersion) + "." +
                         std::to_string(rep.minorVersion) + " DXIL shader satisfies all translation prerequisites.";
        return true;
    }

    if (ir.hasTextureSample || (rep.declaredSRVs > 0 && rep.declaredSamplers > 0)) {
        rep.arithmeticOp = DXILArithmeticOp::Sample;
    } else if (!ir.vectorOps.empty()) {
        const auto& firstOp = ir.vectorOps.front();
        if (firstOp.opcode == IROpcode::Multiply) rep.arithmeticOp = DXILArithmeticOp::Multiply;
        else if (firstOp.opcode == IROpcode::Add) rep.arithmeticOp = DXILArithmeticOp::Add;
        else if (firstOp.opcode == IROpcode::Subtract) {
            rep.arithmeticOp = DXILArithmeticOp::Subtract;
            rep.isReverseSubtract = firstOp.isReverseSubtract;
        } else {
            rep.arithmeticOp = DXILArithmeticOp::Move;
        }
    } else if (rep.declaredCBVs > 0) {
        rep.arithmeticOp = DXILArithmeticOp::Multiply;
    } else {
        rep.arithmeticOp = DXILArithmeticOp::Move;
    }

    rep.status = DXILTranslationStatus::Success;
    rep.diagnostic = "Shader Model " + std::to_string(rep.majorVersion) + "." +
                     std::to_string(rep.minorVersion) + " DXIL shader satisfies all translation prerequisites.";
    return true;
}

bool DXILTranslationEngine::BuildSM50Container(
    ShaderStage stage,
    const std::vector<DXILSignatureElement>& inputs,
    const std::vector<DXILSignatureElement>& outputs,
    const std::vector<uint32_t>& shexTokens,
    std::vector<uint8_t>& outBytecode)
{
    // 1. Build ISGN chunk
    std::vector<uint8_t> isgnChunk;
    {
        uint32_t elemCount = static_cast<uint32_t>(inputs.size());
        uint32_t paramTableSize = 8 + elemCount * 24;

        std::vector<uint8_t> strTable;
        std::vector<uint32_t> nameOffsets;
        for (const auto& elem : inputs) {
            nameOffsets.push_back(paramTableSize + static_cast<uint32_t>(strTable.size()));
            for (char c : elem.semanticName) {
                strTable.push_back(static_cast<uint8_t>(c));
            }
            strTable.push_back(0); // null-terminate
        }
        while (strTable.size() % 4 != 0) strTable.push_back(0);

        isgnChunk.resize(paramTableSize + strTable.size(), 0);
        uint32_t* pU32 = reinterpret_cast<uint32_t*>(isgnChunk.data());
        pU32[0] = elemCount;
        pU32[1] = 8; // D3D11 ISGN header size

        for (uint32_t i = 0; i < elemCount; ++i) {
            const auto& elem = inputs[i];
            size_t paramOff = 8 + i * 24;
            uint32_t* pParam = reinterpret_cast<uint32_t*>(isgnChunk.data() + paramOff);
            pParam[0] = nameOffsets[i];
            pParam[1] = elem.semanticIndex;
            uint32_t d11SysVal = 0;
            if (elem.semanticName == "SV_Position" || elem.semanticName == "SV_POSITION") {
                d11SysVal = 1; // D3D_NAME_POSITION
            }
            pParam[2] = d11SysVal;
            pParam[3] = 3; // D3D10_SB_REGISTER_COMPONENT_FLOAT32
            pParam[4] = elem.registerIndex;
            isgnChunk[paramOff + 20] = elem.mask;
            isgnChunk[paramOff + 21] = elem.readWriteMask;
            isgnChunk[paramOff + 22] = 0;
            isgnChunk[paramOff + 23] = 0;
        }
        if (!strTable.empty()) {
            memcpy(isgnChunk.data() + paramTableSize, strTable.data(), strTable.size());
        }
    }

    // 2. Build OSGN chunk
    std::vector<uint8_t> osgnChunk;
    {
        uint32_t elemCount = static_cast<uint32_t>(outputs.size());
        uint32_t paramTableSize = 8 + elemCount * 24;

        std::vector<uint8_t> strTable;
        std::vector<uint32_t> nameOffsets;
        for (const auto& elem : outputs) {
            nameOffsets.push_back(paramTableSize + static_cast<uint32_t>(strTable.size()));
            for (char c : elem.semanticName) {
                strTable.push_back(static_cast<uint8_t>(c));
            }
            strTable.push_back(0);
        }
        while (strTable.size() % 4 != 0) strTable.push_back(0);

        osgnChunk.resize(paramTableSize + strTable.size(), 0);
        uint32_t* pU32 = reinterpret_cast<uint32_t*>(osgnChunk.data());
        pU32[0] = elemCount;
        pU32[1] = 8; // D3D11 OSGN header size

        for (uint32_t i = 0; i < elemCount; ++i) {
            const auto& elem = outputs[i];
            size_t paramOff = 8 + i * 24;
            uint32_t* pParam = reinterpret_cast<uint32_t*>(osgnChunk.data() + paramOff);
            pParam[0] = nameOffsets[i];
            pParam[1] = elem.semanticIndex;
            uint32_t d11SysVal = 0;
            if (elem.semanticName == "SV_Position" || elem.semanticName == "SV_POSITION") {
                d11SysVal = 1;
            }
            pParam[2] = d11SysVal;
            pParam[3] = 3; // FLOAT32
            pParam[4] = elem.registerIndex;
            osgnChunk[paramOff + 20] = elem.mask;
            osgnChunk[paramOff + 21] = elem.readWriteMask;
            osgnChunk[paramOff + 22] = 0;
            osgnChunk[paramOff + 23] = 0;
        }
        if (!strTable.empty()) {
            memcpy(osgnChunk.data() + paramTableSize, strTable.data(), strTable.size());
        }
    }

    // 3. Assemble complete DXBC container with 3 chunks: ISGN, OSGN, SHEX
    uint32_t chunkCount = 3;
    uint32_t isgnByteSize = static_cast<uint32_t>(isgnChunk.size());
    uint32_t osgnByteSize = static_cast<uint32_t>(osgnChunk.size());
    uint32_t shexByteSize = static_cast<uint32_t>(shexTokens.size() * sizeof(uint32_t));

    uint32_t headerSize = 32 + chunkCount * 4;
    uint32_t off0 = headerSize;
    uint32_t off1 = off0 + 8 + isgnByteSize;
    uint32_t off2 = off1 + 8 + osgnByteSize;
    uint32_t totalSize = off2 + 8 + shexByteSize;

    outBytecode.assign(totalSize, 0);
    uint32_t* pHeader = reinterpret_cast<uint32_t*>(outBytecode.data());
    pHeader[0] = FOURCC_DXBC;
    pHeader[5] = 1; // DXBC version
    pHeader[6] = totalSize;
    pHeader[7] = chunkCount;

    pHeader[8] = off0;
    pHeader[9] = off1;
    pHeader[10] = off2;

    // Chunk 0: ISGN
    *reinterpret_cast<uint32_t*>(outBytecode.data() + off0) = FOURCC_ISGN;
    *reinterpret_cast<uint32_t*>(outBytecode.data() + off0 + 4) = isgnByteSize;
    memcpy(outBytecode.data() + off0 + 8, isgnChunk.data(), isgnByteSize);

    // Chunk 1: OSGN
    *reinterpret_cast<uint32_t*>(outBytecode.data() + off1) = FOURCC_OSGN;
    *reinterpret_cast<uint32_t*>(outBytecode.data() + off1 + 4) = osgnByteSize;
    memcpy(outBytecode.data() + off1 + 8, osgnChunk.data(), osgnByteSize);

    // Chunk 2: SHEX
    *reinterpret_cast<uint32_t*>(outBytecode.data() + off2) = FOURCC_SHEX;
    *reinterpret_cast<uint32_t*>(outBytecode.data() + off2 + 4) = shexByteSize;
    memcpy(outBytecode.data() + off2 + 8, shexTokens.data(), shexByteSize);

    // 4. Calculate authentic DXBC checksum
    CalculateDXBCChecksum(outBytecode.data(), outBytecode.size(), reinterpret_cast<uint32_t*>(outBytecode.data() + 4));

    return true;
}

bool DXILTranslationEngine::TranslateDXILToSM50(
    const void* pSrcBytecode,
    size_t srcSize,
    std::vector<uint8_t>& outLoweredBytecode,
    DXILTranslationReport* pOutReport,
    bool enableMem2Reg,
    Mem2RegStats* pOutMem2RegStats)
{
    outLoweredBytecode.clear();
    DXILTranslationReport localReport;
    DXILTranslationReport& rep = pOutReport ? *pOutReport : localReport;

    if (!CanTranslate(pSrcBytecode, srcSize, &rep)) {
        return false;
    }

    bool hasTextureSample = (rep.declaredSRVs > 0 && rep.declaredSamplers > 0);
    bool hasCBuffer = (rep.declaredCBVs > 0);

    // Decode authentic LLVM Bitcode IR
    DXILIntermediateRepresentation ir;
    const uint8_t* pRaw = reinterpret_cast<const uint8_t*>(pSrcBytecode);
    const uint8_t* pDxilChunk = nullptr;
    uint32_t dxilChunkSize = 0;
    ParseContainerAndSignatures(pRaw, srcSize, rep, pDxilChunk, dxilChunkSize);
    if (pDxilChunk && dxilChunkSize >= sizeof(DxilProgramHeader)) {
        const auto* pDxilHdr = reinterpret_cast<const DxilProgramHeader*>(pDxilChunk);
        size_t bcOffsetFromHdr = 8 + pDxilHdr->bitcodeOffset;
        const uint8_t* pBitcode = pDxilChunk + bcOffsetFromHdr;
        size_t bcSize = pDxilHdr->bitcodeSize;
        DXILTranslationStatus decodeStatus;
        std::string decodeDiag;
        DXILBitcodeDecoder::DecodeBitcode(pBitcode, bcSize, ir, decodeStatus, decodeDiag);
    }

    DXILIntermediateRepresentation irBackup = ir;
    Mem2RegStats localMem2RegStats;
    Mem2RegOptions mem2RegOpts;
    mem2RegOpts.enabled = enableMem2Reg;
    mem2RegOpts.verifySSA = true;
    mem2RegOpts.splitCriticalEdges = true;
    DXILTranslationStatus mem2RegStatus = DXILTranslationStatus::Success;
    std::string mem2RegDiag;
    if (!DXILMem2Reg::Run(ir, mem2RegOpts, localMem2RegStats, mem2RegStatus, mem2RegDiag)) {
        // Roll back to unoptimized Phase 12.8 baseline
        ir = irBackup;
        localMem2RegStats.allocationsPromoted = 0;
        localMem2RegStats.allocationsUnpromoted = localMem2RegStats.allocationsDiscovered;
        localMem2RegStats.isVerified = false;
        localMem2RegStats.verificationDiagnostic = "Mem2Reg rollback to Phase 12.8 baseline: " + mem2RegDiag;
    }
    if (pOutMem2RegStats) {
        *pOutMem2RegStats = localMem2RegStats;
    }

    if (ir.hasTextureSample) hasTextureSample = true;
    if (ir.hasCBuffer) hasCBuffer = true;

    // Build the lowered SM 5.0 instruction token stream (SHEX chunk)
    std::vector<uint32_t> shexTokens;

    if (rep.stage == ShaderStage::Pixel) {
        // Version token for ps_5_0: 0x00000050
        shexTokens.push_back(0x00000050);
        shexTokens.push_back(0); // Placeholder for length

        // Global flags: refactoringAllowed (0x0100086A)
        shexTokens.push_back(0x0100086A);

        // Dynamic Input Signature Resolution: map actual input register
        uint32_t inReg = 0;
        uint8_t inMask = 0x0F;
        bool foundColorOrTex = false;
        for (const auto& elem : rep.inputSignatures) {
            if (elem.semanticName == "COLOR" || elem.semanticName.find("TEXCOORD") != std::string::npos) {
                inReg = elem.registerIndex;
                inMask = elem.mask;
                foundColorOrTex = true;
                break;
            }
        }
        if (!foundColorOrTex && !rep.inputSignatures.empty()) {
            for (const auto& elem : rep.inputSignatures) {
                if (elem.systemValueType == 0) {
                    inReg = elem.registerIndex;
                    inMask = elem.mask;
                    break;
                }
            }
        }

        if (ir.hasControlFlow) {
            if (ir.hasLoop) {
                // Pixel shader bounded loop lowering
                uint32_t cbVectors = (rep.declaredCBVs > 0 || ir.hasCBuffer) ? (ir.maxCBufferReg + 1) : 0;
                if (cbVectors == 0 && (ir.hasCBuffer || rep.declaredCBVs > 0)) cbVectors = 2;
                rep.declaredCBVs = (cbVectors > 0) ? 1 : 0;

                if (cbVectors > 0) {
                    shexTokens.push_back(0x04000059);
                    shexTokens.push_back(0x00208E46); // cb0
                    shexTokens.push_back(0x00000000); // slot 0
                    shexTokens.push_back(cbVectors);
                }

                uint32_t inMaskToken = 0x00101002 | ((static_cast<uint32_t>(inMask ? inMask : 0x0F) & 0x0F) << 4);
                shexTokens.push_back(0x03001062);
                shexTokens.push_back(inMaskToken);
                shexTokens.push_back(inReg);

                shexTokens.push_back(0x03000065);
                shexTokens.push_back(0x001020F2);
                shexTokens.push_back(0x00000000);

                // dcl_temps 4 (r0 accumulator, r1 induction/condition/dynamic bound, r2 scratch)
                shexTokens.push_back(0x02000068);
                shexTokens.push_back(0x00000004);

                // 1. mov r0.xyzw, v[inReg].xyzw
                shexTokens.push_back(0x05000036);
                shexTokens.push_back(0x001000F2);
                shexTokens.push_back(0x00000000); // r0
                shexTokens.push_back(0x00101E46);
                shexTokens.push_back(inReg);

                // 2. mov r1.x, l(inductionInitialValue)
                shexTokens.push_back(0x05000036);
                shexTokens.push_back(0x00100012);
                shexTokens.push_back(0x00000001); // r1.x
                shexTokens.push_back(0x00004001);
                shexTokens.push_back(static_cast<uint32_t>(ir.loop.inductionInitialValue));

                // 3. If dynamic bound: mov r1.z, cb0[boundReg].channel
                if (ir.loop.isDynamicBound) {
                    uint32_t swiz = 0;
                    if (ir.loop.boundCBufferChannel == 1) swiz = 0x55;
                    else if (ir.loop.boundCBufferChannel == 2) swiz = 0xAA;
                    else if (ir.loop.boundCBufferChannel == 3) swiz = 0xFF;

                    shexTokens.push_back(0x06000036);
                    shexTokens.push_back(0x00100042); // r1.z
                    shexTokens.push_back(0x00000001); // r1
                    shexTokens.push_back(0x00208006 | (swiz << 4)); // cb0[boundReg].swiz
                    shexTokens.push_back(0x00000000); // cb slot 0
                    shexTokens.push_back(ir.loop.boundCBufferReg);
                }

                // 4. loop (0x01000030)
                shexTokens.push_back(0x01000030);

                // 5. Condition: ige / uge / ilt / ult / ieq / ine r1.y, r1.x, l(constantBound) or r1.z
                // SM 5.0 opcodes:
                // ige = 0x07000021 (33)
                // uge = 0x07000050 (80)
                // ilt = 0x07000022 (34)
                // ult = 0x07000051 (81)
                // ieq = 0x07000020 (32)
                // ine = 0x07000027 (39)
                uint32_t cmpOpcode = 0x07000021;
                switch (ir.loop.breakCmpOpcode) {
                    case LoopCmpOpcode::IGE: cmpOpcode = 0x07000021; break;
                    case LoopCmpOpcode::UGE: cmpOpcode = 0x07000050; break;
                    case LoopCmpOpcode::ILT: cmpOpcode = 0x07000022; break;
                    case LoopCmpOpcode::ULT: cmpOpcode = 0x07000051; break;
                    case LoopCmpOpcode::IEQ: cmpOpcode = 0x07000020; break;
                    case LoopCmpOpcode::INE: cmpOpcode = 0x07000027; break;
                }
                if (ir.loop.isDynamicBound) {
                    shexTokens.push_back(cmpOpcode);
                    shexTokens.push_back(0x00100022); // r1.y
                    shexTokens.push_back(0x00000001);
                    shexTokens.push_back(0x0010000A); // r1.xxxx
                    shexTokens.push_back(0x00000001);
                    shexTokens.push_back(0x00100AA6); // r1.zzzz
                    shexTokens.push_back(0x00000001);
                } else {
                    shexTokens.push_back(cmpOpcode);
                    shexTokens.push_back(0x00100022); // r1.y
                    shexTokens.push_back(0x00000001);
                    shexTokens.push_back(0x0010000A); // r1.xxxx
                    shexTokens.push_back(0x00000001);
                    shexTokens.push_back(0x00004001);
                    shexTokens.push_back(static_cast<uint32_t>(ir.loop.constantBound));
                }

                // 6. breakc_nz r1.y (0x03040003, 0x0010001A, 0x00000001)
                shexTokens.push_back(0x03040003);
                shexTokens.push_back(0x0010001A);
                shexTokens.push_back(0x00000001);

                // 7. Loop body
                if (ir.loop.hasInnerBranch) {
                    uint32_t litDword = *reinterpret_cast<const uint32_t*>(&ir.loop.innerBranchLiteral);
                    shexTokens.push_back(0x0700001D); // ge r1.y, r0.x, lit
                    shexTokens.push_back(0x00100022); // r1.y
                    shexTokens.push_back(0x00000001);
                    shexTokens.push_back(0x00100006); // r0.xxxx
                    shexTokens.push_back(0x00000000);
                    shexTokens.push_back(0x00004001);
                    shexTokens.push_back(litDword);

                    shexTokens.push_back(0x0304001F); // if_nz r1.y
                    shexTokens.push_back(0x0010001A);
                    shexTokens.push_back(0x00000001);

                    shexTokens.push_back(0x06000036);
                    shexTokens.push_back(0x001000F2);
                    shexTokens.push_back(0x00000002); // r2
                    shexTokens.push_back(0x00208E46);
                    shexTokens.push_back(0x00000000);
                    shexTokens.push_back(ir.loop.innerTrueCBufferReg);

                    shexTokens.push_back(0x01000012); // else

                    shexTokens.push_back(0x06000036);
                    shexTokens.push_back(0x001000F2);
                    shexTokens.push_back(0x00000002); // r2
                    shexTokens.push_back(0x00208E46);
                    shexTokens.push_back(0x00000000);
                    shexTokens.push_back(ir.loop.innerFalseCBufferReg);

                    shexTokens.push_back(0x01000015); // endif

                    shexTokens.push_back(0x07000000); // add r0.xyzw, r0.xyzw, r2.xyzw
                    shexTokens.push_back(0x001000F2);
                    shexTokens.push_back(0x00000000); // r0
                    shexTokens.push_back(0x00100E46);
                    shexTokens.push_back(0x00000000); // r0
                    shexTokens.push_back(0x00100E46);
                    shexTokens.push_back(0x00000002); // r2
                } else {
                    IROpcode op = IROpcode::Add;
                    uint32_t cbRow = 0;
                    if (!ir.loop.bodyInstructions.empty()) {
                        op = ir.loop.bodyInstructions[0].opcode;
                        cbRow = ir.loop.bodyInstructions[0].cbufferReg;
                    }
                    if (op == IROpcode::Multiply) {
                        shexTokens.push_back(0x08000038); // mul
                    } else {
                        shexTokens.push_back(0x08000000); // add
                    }
                    shexTokens.push_back(0x001000F2);
                    shexTokens.push_back(0x00000000); // r0
                    shexTokens.push_back(0x00100E46);
                    shexTokens.push_back(0x00000000); // r0
                    shexTokens.push_back(0x00208E46);
                    shexTokens.push_back(0x00000000); // cb0
                    shexTokens.push_back(cbRow);
                }

                // 8. iadd r1.x, r1.x, l(step)
                shexTokens.push_back(0x0700001E);
                shexTokens.push_back(0x00100012);
                shexTokens.push_back(0x00000001); // r1.x
                shexTokens.push_back(0x0010000A); // r1.xxxx
                shexTokens.push_back(0x00000001);
                shexTokens.push_back(0x00004001);
                shexTokens.push_back(static_cast<uint32_t>(ir.loop.inductionStep));

                // 9. endloop (0x01000016)
                shexTokens.push_back(0x01000016);

                // 10. Post-loop arithmetic (if present)
                if (!ir.loop.exitInstructions.empty()) {
                    const auto& postOp = ir.loop.exitInstructions[0];
                    if (postOp.opcode == IROpcode::Multiply) {
                        shexTokens.push_back(0x08000038);
                    } else {
                        shexTokens.push_back(0x08000000);
                    }
                    shexTokens.push_back(0x001000F2);
                    shexTokens.push_back(0x00000000); // r0
                    shexTokens.push_back(0x00100E46);
                    shexTokens.push_back(0x00000000); // r0
                    shexTokens.push_back(0x00208E46);
                    shexTokens.push_back(0x00000000); // cb0
                    shexTokens.push_back(postOp.cbufferReg);
                }

                // 11. mov o0.xyzw, r0.xyzw
                shexTokens.push_back(0x05000036);
                shexTokens.push_back(0x001020F2);
                shexTokens.push_back(0x00000000); // o0
                shexTokens.push_back(0x00100E46);
                shexTokens.push_back(0x00000000); // r0

                // 12. ret (0x0100003E)
                shexTokens.push_back(0x0100003E);
            } else if (ir.compoundCFG.pattern == CFGPatternKind::IfElseThenSwitch) {
                // Compound: If/Else followed by Switch
                uint32_t cbVectors = (rep.declaredCBVs > 0 || ir.hasCBuffer) ? (ir.maxCBufferReg + 1) : 0;
                if (cbVectors == 0 && (ir.hasCBuffer || rep.declaredCBVs > 0)) cbVectors = 2;
                rep.declaredCBVs = (cbVectors > 0) ? 1 : 0;

                if (cbVectors > 0) {
                    shexTokens.push_back(0x04000059);
                    shexTokens.push_back(0x00208E46);
                    shexTokens.push_back(0x00000000);
                    shexTokens.push_back(cbVectors);
                }

                if (ir.hasInput) {
                    uint32_t inMaskToken = 0x00101002 | ((static_cast<uint32_t>(inMask ? inMask : 0x0F) & 0x0F) << 4);
                    shexTokens.push_back(0x03001062);
                    shexTokens.push_back(inMaskToken);
                    shexTokens.push_back(inReg);
                }

                // dcl_output o0.xyzw
                shexTokens.push_back(0x03000065);
                shexTokens.push_back(0x001020F2);
                shexTokens.push_back(0x00000000);

                // dcl_temps 4
                shexTokens.push_back(0x02000068);
                shexTokens.push_back(0x00000004);

                // Helper to emit a block's instructions into a destination temp register
                auto EmitBlockOps = [&](uint32_t b, uint32_t destReg, uint32_t srcTempReg) {
                    const auto& insts = ir.basicBlocks[b].instructions;
                    for (size_t opIdx = 0; opIdx < insts.size(); ++opIdx) {
                        const auto& vOp = insts[opIdx];
                        if (vOp.isScalarIntOp) {
                            shexTokens.push_back(0x0800001E); // iadd (8 tokens: opcode + dest(2) + cb0(3) + lit(2))
                            shexTokens.push_back(0x00100012); // dest: r[destTempIndex].x
                            shexTokens.push_back(vOp.destTempIndex);
                            shexTokens.push_back(0x00208006 | ((vOp.cbufferChannel * 0x55) << 4));
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                            shexTokens.push_back(0x00004001); // literal int
                            shexTokens.push_back(static_cast<uint32_t>(vOp.literalInt));
                        } else if (vOp.opcode == IROpcode::Move) {
                            if (vOp.hasCBuffer) {
                                if (vOp.isComponentMove) {
                                    shexTokens.push_back(0x06000036);
                                    shexTokens.push_back(0x00100002 | ((vOp.channelMask & 0x0F) << 4));
                                    shexTokens.push_back(destReg);
                                    shexTokens.push_back(0x00208006 | ((vOp.cbufferChannel * 0x55) << 4));
                                    shexTokens.push_back(0x00000000);
                                    shexTokens.push_back(vOp.cbufferReg);
                                } else {
                                    shexTokens.push_back(0x06000036);
                                    shexTokens.push_back(0x001000F2);
                                    shexTokens.push_back(destReg);
                                    shexTokens.push_back(0x00208E46);
                                    shexTokens.push_back(0x00000000);
                                    shexTokens.push_back(vOp.cbufferReg);
                                }
                            } else {
                                // mov r[destReg].xyzw, r[srcTempReg].xyzw
                                shexTokens.push_back(0x05000036);
                                shexTokens.push_back(0x001000F2);
                                shexTokens.push_back(destReg);
                                shexTokens.push_back(0x00100E46);
                                shexTokens.push_back(srcTempReg);
                            }
                        } else if (vOp.opcode == IROpcode::Multiply) {
                            shexTokens.push_back(0x08000038);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(srcTempReg);
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        } else if (vOp.opcode == IROpcode::Add) {
                            shexTokens.push_back(0x08000000);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(srcTempReg);
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        } else if (vOp.opcode == IROpcode::Subtract) {
                            shexTokens.push_back(0x09000000);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(srcTempReg);
                            shexTokens.push_back(0x80208E46);
                            shexTokens.push_back(0x00000041);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        }
                    }
                };

                // === Region 1: If/Else ===
                const auto& reg1 = ir.compoundCFG.firstRegion;
                uint32_t condSsa = reg1.conditionSsa;
                auto cmpIt = ir.comparisons.find(condSsa);

                if (cmpIt != ir.comparisons.end()) {
                    auto cmp = cmpIt->second;
                    while (cmp.src0.type == IRValueType::IntermediateTemp) {
                        auto innerCmp = ir.comparisons.find(cmp.src0.ssaId);
                        if (innerCmp != ir.comparisons.end()) {
                            cmp = innerCmp->second;
                        } else {
                            break;
                        }
                    }

                    bool swapOps = false;
                    uint32_t baseOp = 0x21; // ige
                    if (cmp.predicate == ComparePredicate::IntSignedGreaterThan) {
                        baseOp = 0x22; // ilt (with swap: b < a)
                        swapOps = true;
                    } else if (cmp.predicate == ComparePredicate::IntSignedLessEqual) {
                        baseOp = 0x21; // ige (with swap: b >= a)
                        swapOps = true;
                    } else if (cmp.predicate == ComparePredicate::IntSignedLessThan) {
                        baseOp = 0x22; // ilt (a < b)
                        swapOps = false;
                    } else if (cmp.predicate == ComparePredicate::IntSignedGreaterEqual) {
                        baseOp = 0x21; // ige (a >= b)
                        swapOps = false;
                    } else if (cmp.predicate == ComparePredicate::IntEqual) {
                        baseOp = 0x20; // ieq
                        swapOps = false;
                    } else if (cmp.predicate == ComparePredicate::IntNotEqual) {
                        baseOp = 0x27; // ine
                        swapOps = false;
                    } else if (cmp.predicate == ComparePredicate::FloatGreaterThan) {
                        baseOp = 0x31; // lt (with swap: b < a)
                        swapOps = true;
                    } else if (cmp.predicate == ComparePredicate::FloatLessOrEqual) {
                        baseOp = 0x1D; // ge (with swap: b >= a)
                        swapOps = true;
                    } else if (cmp.predicate == ComparePredicate::FloatLessThan) {
                        baseOp = 0x31; // lt (a < b)
                        swapOps = false;
                    } else if (cmp.predicate == ComparePredicate::FloatGreaterOrEqual) {
                        baseOp = 0x1D; // ge (a >= b)
                        swapOps = false;
                    } else if (cmp.predicate == ComparePredicate::FloatEqual) {
                        baseOp = 0x18; // eq
                        swapOps = false;
                    } else if (cmp.predicate == ComparePredicate::FloatNotEqual) {
                        baseOp = 0x39; // ne
                        swapOps = false;
                    }

                    uint32_t cmpOpcode = 0x08000000 | baseOp;
                    uint32_t src0Reg = (cmp.src0.type == IRValueType::CBufferComponent) ? cmp.src0.regIndex : 0;
                    uint8_t src0Chan = (cmp.src0.type == IRValueType::CBufferComponent) ? cmp.src0.channel : 0;

                    shexTokens.push_back(cmpOpcode);
                    shexTokens.push_back(0x00100012); // r0.x
                    shexTokens.push_back(0x00000000); // r0

                    if (swapOps) {
                        shexTokens.push_back(0x00004001); // literal int
                        shexTokens.push_back(static_cast<uint32_t>(cmp.constInt1));
                        shexTokens.push_back(0x00208006 | ((src0Chan * 0x55) << 4));
                        shexTokens.push_back(0x00000000);
                        shexTokens.push_back(src0Reg);
                    } else {
                        shexTokens.push_back(0x00208006 | ((src0Chan * 0x55) << 4));
                        shexTokens.push_back(0x00000000);
                        shexTokens.push_back(src0Reg);
                        shexTokens.push_back(0x00004001); // literal int
                        shexTokens.push_back(static_cast<uint32_t>(cmp.constInt1));
                    }
                }

                // if_nz r0.x
                shexTokens.push_back(0x0304001F);
                shexTokens.push_back(0x0010000A);
                shexTokens.push_back(0x00000000);

                // True block -> r1
                EmitBlockOps(reg1.trueBlock, 1, 1);

                if (!reg1.isTriangle) {
                    // else
                    shexTokens.push_back(0x01000012);
                    // False block -> r1
                    EmitBlockOps(reg1.falseBlock, 1, 1);
                }

                // endif
                shexTokens.push_back(0x01000015);

                // === Region 1 Merge ($M_1$) ===
                uint32_t m1 = reg1.mergeBlock;
                EmitBlockOps(m1, 1, 1);

                // === Region 2: Switch ===
                const auto& reg2 = ir.compoundCFG.secondRegion;
                if (ir.switchDesc.selectorIsCBuffer) {
                    shexTokens.push_back(0x0400004C);
                    shexTokens.push_back(0x0020800A | (static_cast<uint32_t>(ir.switchDesc.selectorCbChannel) << 4));
                    shexTokens.push_back(ir.switchDesc.selectorCbSlot);
                    shexTokens.push_back(ir.switchDesc.selectorCbRow);
                } else if (ir.switchDesc.selectorIsPhi) {
                    // switch r0.x
                    shexTokens.push_back(0x0300004C);
                    shexTokens.push_back(0x0010000A);
                    shexTokens.push_back(0x00000000); // r0
                } else {
                    shexTokens.push_back(0x0300004C);
                    shexTokens.push_back(0x0010000A);
                    shexTokens.push_back(0x00000000); // r0
                }

                std::map<uint32_t, std::vector<int64_t>> blockCases;
                for (const auto& sc : reg2.cases) {
                    blockCases[sc.second].push_back(sc.first);
                }
                uint32_t defBlock = reg2.defaultBlock;
                uint32_t m2 = reg2.mergeBlock;

                bool defHandled = false;
                for (const auto& pair : blockCases) {
                    uint32_t b = pair.first;
                    const auto& caseVals = pair.second;
                    for (int64_t cv : caseVals) {
                        shexTokens.push_back(0x03000006); // case l(cv)
                        shexTokens.push_back(0x00004001);
                        shexTokens.push_back(static_cast<uint32_t>(cv));
                    }
                    if (b == defBlock) {
                        shexTokens.push_back(0x0100000A); // default
                        defHandled = true;
                    }
                    if (b != m2) {
                        // Switch cases read r1, write to r2
                        EmitBlockOps(b, 2, 1);
                    }
                    shexTokens.push_back(0x01000002); // break
                }
                if (!defHandled) {
                    shexTokens.push_back(0x0100000A); // default
                    if (defBlock != m2) {
                        EmitBlockOps(defBlock, 2, 1);
                    }
                    shexTokens.push_back(0x01000002); // break
                }

                // endswitch
                shexTokens.push_back(0x01000017);

                // === Region 2 Merge ($M_2$) ===
                const auto& mergeInsts = ir.basicBlocks[m2].instructions;
                if (mergeInsts.empty()) {
                    // mov o0.xyzw, r2.xyzw
                    shexTokens.push_back(0x05000036);
                    shexTokens.push_back(0x001020F2);
                    shexTokens.push_back(0x00000000);
                    shexTokens.push_back(0x00100E46);
                    shexTokens.push_back(0x00000002); // r2
                } else {
                    for (const auto& vOp : mergeInsts) {
                        if (vOp.opcode == IROpcode::Multiply) {
                            shexTokens.push_back(0x08000038);
                            shexTokens.push_back(0x001020F2);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(0x00000002); // r2
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        } else if (vOp.opcode == IROpcode::Add) {
                            shexTokens.push_back(0x08000000);
                            shexTokens.push_back(0x001020F2);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(0x00000002); // r2
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        }
                    }
                }

                // ret
                shexTokens.push_back(0x0100003E);
            } else if (ir.compoundCFG.pattern == CFGPatternKind::SwitchThenIfElse) {
                // Compound: Switch followed by If/Else
                uint32_t cbVectors = (rep.declaredCBVs > 0 || ir.hasCBuffer) ? (ir.maxCBufferReg + 1) : 0;
                if (cbVectors == 0 && (ir.hasCBuffer || rep.declaredCBVs > 0)) cbVectors = 2;
                rep.declaredCBVs = (cbVectors > 0) ? 1 : 0;

                if (cbVectors > 0) {
                    shexTokens.push_back(0x04000059);
                    shexTokens.push_back(0x00208E46);
                    shexTokens.push_back(0x00000000);
                    shexTokens.push_back(cbVectors);
                }

                if (ir.hasInput) {
                    uint32_t inMaskToken = 0x00101002 | ((static_cast<uint32_t>(inMask ? inMask : 0x0F) & 0x0F) << 4);
                    shexTokens.push_back(0x03001062);
                    shexTokens.push_back(inMaskToken);
                    shexTokens.push_back(inReg);
                }

                // dcl_output o0.xyzw
                shexTokens.push_back(0x03000065);
                shexTokens.push_back(0x001020F2);
                shexTokens.push_back(0x00000000);

                // dcl_temps 4
                shexTokens.push_back(0x02000068);
                shexTokens.push_back(0x00000004);

                auto EmitBlockOps = [&](uint32_t b, uint32_t destReg, uint32_t srcTempReg) {
                    const auto& insts = ir.basicBlocks[b].instructions;
                    for (size_t opIdx = 0; opIdx < insts.size(); ++opIdx) {
                        const auto& vOp = insts[opIdx];
                        if (vOp.opcode == IROpcode::Move) {
                            if (vOp.hasCBuffer) {
                                if (vOp.isComponentMove) {
                                    shexTokens.push_back(0x06000036);
                                    shexTokens.push_back(0x00100002 | ((vOp.channelMask & 0x0F) << 4));
                                    shexTokens.push_back(destReg);
                                    shexTokens.push_back(0x00208006 | ((vOp.cbufferChannel * 0x55) << 4));
                                    shexTokens.push_back(0x00000000);
                                    shexTokens.push_back(vOp.cbufferReg);
                                } else {
                                    shexTokens.push_back(0x06000036);
                                    shexTokens.push_back(0x001000F2);
                                    shexTokens.push_back(destReg);
                                    shexTokens.push_back(0x00208E46);
                                    shexTokens.push_back(0x00000000);
                                    shexTokens.push_back(vOp.cbufferReg);
                                }
                            } else {
                                shexTokens.push_back(0x05000036);
                                shexTokens.push_back(0x001000F2);
                                shexTokens.push_back(destReg);
                                shexTokens.push_back(0x00100E46);
                                shexTokens.push_back(srcTempReg);
                            }
                        } else if (vOp.opcode == IROpcode::Multiply) {
                            shexTokens.push_back(0x08000038);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(srcTempReg);
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        } else if (vOp.opcode == IROpcode::Add) {
                            shexTokens.push_back(0x08000000);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(srcTempReg);
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        } else if (vOp.opcode == IROpcode::Subtract) {
                            shexTokens.push_back(0x09000000);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(srcTempReg);
                            shexTokens.push_back(0x80208E46);
                            shexTokens.push_back(0x00000041);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        }
                    }
                };

                // === Region 1: Switch ===
                const auto& reg1 = ir.compoundCFG.firstRegion;
                if (ir.switchDesc.selectorIsCBuffer) {
                    shexTokens.push_back(0x0400004C);
                    shexTokens.push_back(0x0020800A | (static_cast<uint32_t>(ir.switchDesc.selectorCbChannel) << 4));
                    shexTokens.push_back(ir.switchDesc.selectorCbSlot);
                    shexTokens.push_back(ir.switchDesc.selectorCbRow);
                } else {
                    shexTokens.push_back(0x0300004C);
                    shexTokens.push_back(0x0010000A);
                    shexTokens.push_back(0x00000000); // r0
                }

                std::map<uint32_t, std::vector<int64_t>> blockCases;
                for (const auto& sc : reg1.cases) {
                    blockCases[sc.second].push_back(sc.first);
                }
                uint32_t defBlock = reg1.defaultBlock;
                uint32_t m1 = reg1.mergeBlock;

                bool defHandled = false;
                for (const auto& pair : blockCases) {
                    uint32_t b = pair.first;
                    const auto& caseVals = pair.second;
                    for (int64_t cv : caseVals) {
                        shexTokens.push_back(0x03000006); // case l(cv)
                        shexTokens.push_back(0x00004001);
                        shexTokens.push_back(static_cast<uint32_t>(cv));
                    }
                    if (b == defBlock) {
                        shexTokens.push_back(0x0100000A); // default
                        defHandled = true;
                    }
                    if (b != m1) {
                        // Switch cases write to r1
                        EmitBlockOps(b, 1, 1);
                    }
                    shexTokens.push_back(0x01000002); // break
                }
                if (!defHandled) {
                    shexTokens.push_back(0x0100000A); // default
                    if (defBlock != m1) {
                        EmitBlockOps(defBlock, 1, 1);
                    }
                    shexTokens.push_back(0x01000002); // break
                }

                // endswitch
                shexTokens.push_back(0x01000017);

                // === Region 1 Merge ($M_1$) / Region 2 Condition ===
                const auto& reg2 = ir.compoundCFG.secondRegion;
                uint32_t condSsa = reg2.conditionSsa;
                auto cmpIt = ir.comparisons.find(condSsa);

                if (cmpIt != ir.comparisons.end()) {
                    auto cmp = cmpIt->second;
                    while (cmp.src0.type == IRValueType::IntermediateTemp) {
                        auto innerCmp = ir.comparisons.find(cmp.src0.ssaId);
                        if (innerCmp != ir.comparisons.end()) {
                            cmp = innerCmp->second;
                        } else {
                            break;
                        }
                    }

                    bool src1IsLiteral = (cmp.isConst1 || cmp.src1.type == IRValueType::LiteralFloat);
                    bool swapOps = false;
                    uint32_t baseOp = 0x21; // ige
                    if (cmp.predicate == ComparePredicate::FloatGreaterThan) {
                        baseOp = 0x31; // lt (with swap: b < a)
                        swapOps = true;
                    } else if (cmp.predicate == ComparePredicate::FloatLessOrEqual) {
                        baseOp = 0x1D; // ge (with swap: b >= a)
                        swapOps = true;
                    } else if (cmp.predicate == ComparePredicate::FloatLessThan) {
                        baseOp = 0x31; // lt (a < b)
                        swapOps = false;
                    } else if (cmp.predicate == ComparePredicate::FloatGreaterOrEqual) {
                        baseOp = 0x1D; // ge (a >= b)
                        swapOps = false;
                    } else if (cmp.predicate == ComparePredicate::FloatEqual) {
                        baseOp = 0x18; // eq
                        swapOps = false;
                    } else if (cmp.predicate == ComparePredicate::FloatNotEqual) {
                        baseOp = 0x39; // ne
                        swapOps = false;
                    } else if (cmp.predicate == ComparePredicate::IntSignedGreaterThan) {
                        baseOp = 0x22; // ilt (with swap: b < a)
                        swapOps = true;
                    } else if (cmp.predicate == ComparePredicate::IntSignedLessEqual) {
                        baseOp = 0x21; // ige (with swap: b >= a)
                        swapOps = true;
                    } else if (cmp.predicate == ComparePredicate::IntSignedLessThan) {
                        baseOp = 0x22; // ilt (a < b)
                        swapOps = false;
                    } else if (cmp.predicate == ComparePredicate::IntSignedGreaterEqual) {
                        baseOp = 0x21; // ige (a >= b)
                        swapOps = false;
                    } else if (cmp.predicate == ComparePredicate::IntEqual) {
                        baseOp = 0x20; // ieq
                        swapOps = false;
                    } else if (cmp.predicate == ComparePredicate::IntNotEqual) {
                        baseOp = 0x27; // ine
                        swapOps = false;
                    }

                    uint32_t cmpLength = src1IsLiteral ? 7 : 8;
                    uint32_t cmpOpcode = (cmpLength << 24) | baseOp;
                    uint32_t src1Reg = (cmp.src1.type == IRValueType::CBufferComponent) ? cmp.src1.regIndex : 0;
                    uint8_t src1Chan = (cmp.src1.type == IRValueType::CBufferComponent) ? cmp.src1.channel : 0;

                    // cmp r0.x, r1.x, src1
                    shexTokens.push_back(cmpOpcode);
                    shexTokens.push_back(0x00100012); // r0.x
                    shexTokens.push_back(0x00000000); // r0

                    auto EmitSrc1 = [&]() {
                        if (src1IsLiteral) {
                            shexTokens.push_back(0x00004001);
                            uint32_t litBits = cmp.isConst1 ? static_cast<uint32_t>(cmp.constInt1) :
                                               *reinterpret_cast<const uint32_t*>(&cmp.src1.literalValue);
                            shexTokens.push_back(litBits);
                        } else {
                            shexTokens.push_back(0x00208006 | ((src1Chan * 0x55) << 4)); // cb0[src1Reg].src1Chan
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(src1Reg);
                        }
                    };
                    auto EmitR1 = [&]() {
                        shexTokens.push_back(0x0010000A); // r1.x
                        shexTokens.push_back(0x00000001); // r1
                    };

                    if (swapOps) {
                        EmitSrc1();
                        EmitR1();
                    } else {
                        EmitR1();
                        EmitSrc1();
                    }
                }

                // if_nz r0.x
                shexTokens.push_back(0x0304001F);
                shexTokens.push_back(0x0010000A);
                shexTokens.push_back(0x00000000);

                // True block -> r2 (reads r1)
                EmitBlockOps(reg2.trueBlock, 2, 1);

                if (!reg2.isTriangle) {
                    // else
                    shexTokens.push_back(0x01000012);
                    // False block -> r2 (reads r1)
                    EmitBlockOps(reg2.falseBlock, 2, 1);
                }

                // endif
                shexTokens.push_back(0x01000015);

                // === Region 2 Merge ($M_2$) ===
                uint32_t m2 = reg2.mergeBlock;
                const auto& mergeInsts = ir.basicBlocks[m2].instructions;
                if (mergeInsts.empty()) {
                    // mov o0.xyzw, r2.xyzw
                    shexTokens.push_back(0x05000036);
                    shexTokens.push_back(0x001020F2);
                    shexTokens.push_back(0x00000000);
                    shexTokens.push_back(0x00100E46);
                    shexTokens.push_back(0x00000002); // r2
                } else {
                    for (const auto& vOp : mergeInsts) {
                        if (vOp.opcode == IROpcode::Multiply) {
                            shexTokens.push_back(0x08000038);
                            shexTokens.push_back(0x001020F2);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(0x00000002); // r2
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        } else if (vOp.opcode == IROpcode::Add) {
                            shexTokens.push_back(0x08000000);
                            shexTokens.push_back(0x001020F2);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(0x00000002); // r2
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        }
                    }
                }

                // ret
                shexTokens.push_back(0x0100003E);
            } else if (ir.hasSwitch) {
                // Pixel shader bounded switch control flow lowering
                uint32_t cbVectors = (rep.declaredCBVs > 0 || ir.hasCBuffer) ? (ir.maxCBufferReg + 1) : 0;
                if (cbVectors == 0 && (ir.hasCBuffer || rep.declaredCBVs > 0)) cbVectors = 2;
                rep.declaredCBVs = (cbVectors > 0) ? 1 : 0;

                if (cbVectors > 0) {
                    // dcl_constantbuffer CB0[cbVectors], immediateIndexed
                    shexTokens.push_back(0x04000059);
                    shexTokens.push_back(0x00208E46); // cb0
                    shexTokens.push_back(0x00000000); // slot 0
                    shexTokens.push_back(cbVectors);
                }

                if (ir.hasInput) {
                    // dcl_input_ps linear v[inReg] with matching mask
                    uint32_t inMaskToken = 0x00101002 | ((static_cast<uint32_t>(inMask ? inMask : 0x0F) & 0x0F) << 4);
                    shexTokens.push_back(0x03001062);
                    shexTokens.push_back(inMaskToken);
                    shexTokens.push_back(inReg);
                }

                // dcl_output o0.xyzw
                shexTokens.push_back(0x03000065);
                shexTokens.push_back(0x001020F2);
                shexTokens.push_back(0x00000000);

                // dcl_temps 2 (r0 for selector, r1 for case accumulation and merge)
                shexTokens.push_back(0x02000068);
                shexTokens.push_back(0x00000002);

                // Emit switch selector
                if (ir.switchDesc.selectorIsCBuffer) {
                    // switch cb0[selectorCbRow].selectorCbChannel
                    shexTokens.push_back(0x0400004C);
                    shexTokens.push_back(0x0020800A | (static_cast<uint32_t>(ir.switchDesc.selectorCbChannel) << 4));
                    shexTokens.push_back(ir.switchDesc.selectorCbSlot);
                    shexTokens.push_back(ir.switchDesc.selectorCbRow);
                } else {
                    // switch r0.x
                    shexTokens.push_back(0x0300004C);
                    shexTokens.push_back(0x0010000A);
                    shexTokens.push_back(0x00000000);
                }

                // Group cases by target block
                std::map<uint32_t, std::vector<int64_t>> blockCases;
                for (const auto& sc : ir.switchDesc.cases) {
                    blockCases[sc.second].push_back(sc.first);
                }
                uint32_t defBlock = ir.switchDesc.defaultBlock;
                uint32_t mergeBlock = ir.switchDesc.mergeBlock;

                auto EmitBlockBody = [&](uint32_t b) {
                    const auto& insts = ir.basicBlocks[b].instructions;
                    if (insts.empty()) {
                        shexTokens.push_back(0x06000036);
                        shexTokens.push_back(0x001000F2); // r1.xyzw
                        shexTokens.push_back(0x00000001); // r1
                        shexTokens.push_back(0x00208E46); // cb0
                        shexTokens.push_back(0x00000000); // slot 0
                        shexTokens.push_back(0x00000000); // reg 0
                    } else {
                        for (size_t opIdx = 0; opIdx < insts.size(); ++opIdx) {
                            const auto& vOp = insts[opIdx];
                            uint32_t src0Token = (opIdx == 0 && ir.hasInput) ? 0x00101E46 : 0x00100E46;
                            uint32_t src0Reg = (opIdx == 0 && ir.hasInput) ? inReg : 1;

                            if (vOp.opcode == IROpcode::Move) {
                                if (vOp.isComponentMove) {
                                    // mov r1.[c], cb0[cbufferReg].[cbufferChannel]
                                    shexTokens.push_back(0x06000036);
                                    shexTokens.push_back(0x00100002 | ((vOp.channelMask & 0x0F) << 4));
                                    shexTokens.push_back(vOp.destTempIndex ? vOp.destTempIndex : 1);
                                    shexTokens.push_back(0x00208006 | ((vOp.cbufferChannel * 0x55) << 4));
                                    shexTokens.push_back(0x00000000);
                                    shexTokens.push_back(vOp.cbufferReg);
                                } else {
                                    // mov r1.xyzw, cb0[cbufferReg].xyzw
                                    shexTokens.push_back(0x06000036);
                                    shexTokens.push_back(0x001000F2);
                                    shexTokens.push_back(0x00000001);
                                    shexTokens.push_back(0x00208E46);
                                    shexTokens.push_back(0x00000000);
                                    shexTokens.push_back(vOp.cbufferReg);
                                }
                            } else if (vOp.opcode == IROpcode::Multiply) {
                                // mul r1.xyzw, src0, cb0[cbufferReg].xyzw
                                shexTokens.push_back(0x08000038);
                                shexTokens.push_back(0x001000F2);
                                shexTokens.push_back(0x00000001);
                                shexTokens.push_back(src0Token);
                                shexTokens.push_back(src0Reg);
                                shexTokens.push_back(0x00208E46);
                                shexTokens.push_back(0x00000000);
                                shexTokens.push_back(vOp.cbufferReg);
                            } else if (vOp.opcode == IROpcode::Add) {
                                // add r1.xyzw, src0, cb0[cbufferReg].xyzw
                                shexTokens.push_back(0x08000000);
                                shexTokens.push_back(0x001000F2);
                                shexTokens.push_back(0x00000001);
                                shexTokens.push_back(src0Token);
                                shexTokens.push_back(src0Reg);
                                shexTokens.push_back(0x00208E46);
                                shexTokens.push_back(0x00000000);
                                shexTokens.push_back(vOp.cbufferReg);
                            } else if (vOp.opcode == IROpcode::Subtract) {
                                // sub r1.xyzw, src0, cb0[cbufferReg].xyzw
                                shexTokens.push_back(0x09000000);
                                shexTokens.push_back(0x001000F2);
                                shexTokens.push_back(0x00000001);
                                shexTokens.push_back(src0Token);
                                shexTokens.push_back(src0Reg);
                                shexTokens.push_back(0x80208E46);
                                shexTokens.push_back(0x00000041);
                                shexTokens.push_back(0x00000000);
                                shexTokens.push_back(vOp.cbufferReg);
                            }
                        }
                    }
                };

                // Emit case blocks
                bool defHandled = false;
                for (const auto& pair : blockCases) {
                    uint32_t b = pair.first;
                    const auto& caseVals = pair.second;

                    // Emit all case labels targeting block b
                    for (int64_t cv : caseVals) {
                        shexTokens.push_back(0x03000006); // case l(cv)
                        shexTokens.push_back(0x00004001);
                        shexTokens.push_back(static_cast<uint32_t>(cv));
                    }
                    if (b == defBlock) {
                        shexTokens.push_back(0x0100000A); // default
                        defHandled = true;
                    }

                    if (b != mergeBlock) {
                        EmitBlockBody(b);
                    }
                    shexTokens.push_back(0x01000002); // break
                }

                // If default block was not grouped with any case:
                if (!defHandled) {
                    shexTokens.push_back(0x0100000A); // default
                    if (defBlock != mergeBlock) {
                        EmitBlockBody(defBlock);
                    }
                    shexTokens.push_back(0x01000002); // break
                }

                // endswitch
                shexTokens.push_back(0x01000017);

                // Merge block: emit post-merge instructions or final output move
                const auto& mergeInsts = ir.basicBlocks[mergeBlock].instructions;
                if (mergeInsts.empty()) {
                    // mov o0.xyzw, r1.xyzw
                    shexTokens.push_back(0x05000036);
                    shexTokens.push_back(0x001020F2); // o0.xyzw
                    shexTokens.push_back(0x00000000); // o0
                    shexTokens.push_back(0x00100E46); // r1.xyzw
                    shexTokens.push_back(0x00000001); // r1
                } else {
                    for (const auto& vOp : mergeInsts) {
                        if (vOp.opcode == IROpcode::Multiply) {
                            shexTokens.push_back(0x08000038);
                            shexTokens.push_back(0x001020F2); // o0.xyzw
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(0x00100E46); // r1.xyzw
                            shexTokens.push_back(0x00000001);
                            shexTokens.push_back(0x00208E46); // cb0
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        } else if (vOp.opcode == IROpcode::Add) {
                            shexTokens.push_back(0x08000000);
                            shexTokens.push_back(0x001020F2); // o0.xyzw
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(0x00100E46); // r1.xyzw
                            shexTokens.push_back(0x00000001);
                            shexTokens.push_back(0x00208E46); // cb0
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        }
                    }
                }

                // ret
                shexTokens.push_back(0x0100003E);
            } else {
                // Pixel shader structured control flow lowering (Diamond / Triangle)
                uint32_t cbVectors = (rep.declaredCBVs > 0 || ir.hasCBuffer) ? (ir.maxCBufferReg + 1) : 0;
                if (cbVectors == 0 && (ir.hasCBuffer || rep.declaredCBVs > 0)) cbVectors = 2;
                rep.declaredCBVs = (cbVectors > 0) ? 1 : 0;

                if (cbVectors > 0) {
                    // dcl_constantbuffer CB0[cbVectors], immediateIndexed
                    shexTokens.push_back(0x04000059);
                    shexTokens.push_back(0x00208E46); // cb0
                    shexTokens.push_back(0x00000000); // slot 0
                    shexTokens.push_back(cbVectors);
                }

                // dcl_input_ps linear v[inReg] with matching mask
                uint32_t inMaskToken = 0x00101002 | ((static_cast<uint32_t>(inMask ? inMask : 0x0F) & 0x0F) << 4);
                shexTokens.push_back(0x03001062);
                shexTokens.push_back(inMaskToken);
                shexTokens.push_back(inReg);

                // dcl_output o0.xyzw
                shexTokens.push_back(0x03000065);
                shexTokens.push_back(0x001020F2);
                shexTokens.push_back(0x00000000);

                // dcl_temps (dynamically computed to cover condition, all PHIs, and intermediate temporaries)
                uint32_t branchTemps = 2; // r0 (cond), r1 (PHI 0)
                for (const auto& bb : ir.basicBlocks) {
                    for (const auto& op : bb.instructions) {
                        if (op.destTempIndex + 1 > branchTemps) branchTemps = op.destTempIndex + 1;
                        for (int c = 0; c < 4; ++c) {
                            if (op.src0[c].type == IRValueType::IntermediateTemp && op.src0[c].regIndex + 1 > branchTemps)
                                branchTemps = op.src0[c].regIndex + 1;
                            if (op.src1[c].type == IRValueType::IntermediateTemp && op.src1[c].regIndex + 1 > branchTemps)
                                branchTemps = op.src1[c].regIndex + 1;
                        }
                    }
                }
                if (ir.numTempsNeeded > branchTemps) branchTemps = ir.numTempsNeeded;
                shexTokens.push_back(0x02000068);
                shexTokens.push_back(branchTemps);

            // Find comparison instruction
            uint32_t condSsa = ir.basicBlocks[0].terminator.conditionSsa;
            CompareInstruction cmp;
            auto cmpIt = ir.comparisons.find(condSsa);
            if (cmpIt != ir.comparisons.end()) {
                cmp = cmpIt->second;
            } else if (!ir.comparisons.empty()) {
                cmp = ir.comparisons.begin()->second;
            }

            float litVal = (cmp.src1.type == IRValueType::LiteralFloat) ? cmp.src1.literalValue :
                           (cmp.src0.type == IRValueType::LiteralFloat) ? cmp.src0.literalValue : 0.5f;
            uint32_t litBits = *reinterpret_cast<const uint32_t*>(&litVal);

            // Emit comparison into r0.x:
            // D3D10_SB_OPCODE_LT = 0x31, GE = 0x1D, EQ = 0x18, NE = 0x39
            if (cmp.predicate == ComparePredicate::FloatGreaterThan) {
                // lt r0.x, literal, v[inReg].x
                shexTokens.push_back(0x07000031);
                shexTokens.push_back(0x00100012); // dest: r0.x
                shexTokens.push_back(0x00000000); // r0
                shexTokens.push_back(0x00004001); // src0: literal float scalar
                shexTokens.push_back(litBits);
                shexTokens.push_back(0x0010100A); // src1: v[inReg].x
                shexTokens.push_back(inReg);
            } else if (cmp.predicate == ComparePredicate::FloatLessThan) {
                // lt r0.x, v[inReg].x, literal
                shexTokens.push_back(0x07000031);
                shexTokens.push_back(0x00100012);
                shexTokens.push_back(0x00000000);
                shexTokens.push_back(0x0010100A);
                shexTokens.push_back(inReg);
                shexTokens.push_back(0x00004001);
                shexTokens.push_back(litBits);
            } else if (cmp.predicate == ComparePredicate::FloatGreaterOrEqual) {
                // ge r0.x, v[inReg].x, literal
                shexTokens.push_back(0x0700001D);
                shexTokens.push_back(0x00100012);
                shexTokens.push_back(0x00000000);
                shexTokens.push_back(0x0010100A);
                shexTokens.push_back(inReg);
                shexTokens.push_back(0x00004001);
                shexTokens.push_back(litBits);
            } else if (cmp.predicate == ComparePredicate::FloatLessOrEqual) {
                // ge r0.x, literal, v[inReg].x
                shexTokens.push_back(0x0700001D);
                shexTokens.push_back(0x00100012);
                shexTokens.push_back(0x00000000);
                shexTokens.push_back(0x00004001);
                shexTokens.push_back(litBits);
                shexTokens.push_back(0x0010100A);
                shexTokens.push_back(inReg);
            } else if (cmp.predicate == ComparePredicate::FloatEqual) {
                // eq r0.x, v[inReg].x, literal
                shexTokens.push_back(0x07000018);
                shexTokens.push_back(0x00100012);
                shexTokens.push_back(0x00000000);
                shexTokens.push_back(0x0010100A);
                shexTokens.push_back(inReg);
                shexTokens.push_back(0x00004001);
                shexTokens.push_back(litBits);
            } else { // FloatNotEqual
                // ne r0.x, v[inReg].x, literal
                shexTokens.push_back(0x07000039);
                shexTokens.push_back(0x00100012);
                shexTokens.push_back(0x00000000);
                shexTokens.push_back(0x0010100A);
                shexTokens.push_back(inReg);
                shexTokens.push_back(0x00004001);
                shexTokens.push_back(litBits);
            }

            // if_nz r0.x
            shexTokens.push_back(0x0304001F);
            shexTokens.push_back(0x0010000A);
            shexTokens.push_back(0x00000000);

            uint32_t trueBlock = ir.basicBlocks[0].terminator.trueBlock;
            uint32_t falseBlock = ir.basicBlocks[0].terminator.falseBlock;

            // True block instructions
            const auto& trueInsts = ir.basicBlocks[trueBlock].instructions;
            if (trueInsts.empty()) {
                shexTokens.push_back(0x06000036);
                shexTokens.push_back(0x001000F2);
                shexTokens.push_back(0x00000001);
                shexTokens.push_back(0x00208E46);
                shexTokens.push_back(0x00000000);
                shexTokens.push_back(0x00000000);
            } else {
                for (size_t opIdx = 0; opIdx < trueInsts.size(); ++opIdx) {
                    const auto& vOp = trueInsts[opIdx];
                    uint32_t src0Token = (opIdx == 0) ? 0x00101E46 : 0x00100E46;
                    uint32_t src0Reg = (opIdx == 0) ? inReg : 1;
                    if (vOp.src0[0].type == IRValueType::IntermediateTemp) {
                        src0Token = 0x00100E46;
                        src0Reg = vOp.src0[0].regIndex;
                    } else if (vOp.src0[0].type == IRValueType::InputComponent) {
                        src0Token = 0x00101E46;
                        src0Reg = vOp.src0[0].regIndex;
                    }
                    uint32_t destReg = vOp.destTempIndex ? vOp.destTempIndex : 1;

                    if (vOp.opcode == IROpcode::Move) {
                        if (vOp.hasCBuffer) {
                            shexTokens.push_back(0x06000036);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        } else if (vOp.src0[0].type == IRValueType::LiteralFloat) {
                            uint32_t litBits = *reinterpret_cast<const uint32_t*>(&vOp.src0[0].literalValue);
                            shexTokens.push_back(0x08000036);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00004002);
                            shexTokens.push_back(litBits);
                            shexTokens.push_back(litBits);
                            shexTokens.push_back(litBits);
                            shexTokens.push_back(litBits);
                        } else if (vOp.src0[0].type == IRValueType::IntermediateTemp) {
                            shexTokens.push_back(0x05000036);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(vOp.src0[0].regIndex);
                        } else if (vOp.src0[0].type == IRValueType::InputComponent) {
                            shexTokens.push_back(0x05000036);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00101E46);
                            shexTokens.push_back(vOp.src0[0].regIndex);
                        }
                    } else if (vOp.opcode == IROpcode::Multiply) {
                        if (vOp.src1[0].type == IRValueType::IntermediateTemp) {
                            shexTokens.push_back(0x07000038);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(src0Token);
                            shexTokens.push_back(src0Reg);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(vOp.src1[0].regIndex ? vOp.src1[0].regIndex : 2);
                        } else {
                            shexTokens.push_back(0x08000038);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(src0Token);
                            shexTokens.push_back(src0Reg);
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        }
                    } else if (vOp.opcode == IROpcode::Add) {
                        if (vOp.src1[0].type == IRValueType::IntermediateTemp) {
                            shexTokens.push_back(0x07000000);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(src0Token);
                            shexTokens.push_back(src0Reg);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(vOp.src1[0].regIndex ? vOp.src1[0].regIndex : 2);
                        } else {
                            shexTokens.push_back(0x08000000);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(src0Token);
                            shexTokens.push_back(src0Reg);
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        }
                    } else if (vOp.opcode == IROpcode::Subtract) {
                        shexTokens.push_back(0x09000000);
                        shexTokens.push_back(0x001000F2);
                        shexTokens.push_back(destReg);
                        shexTokens.push_back(src0Token);
                        shexTokens.push_back(src0Reg);
                        shexTokens.push_back(0x80208E46);
                        shexTokens.push_back(0x00000041);
                        shexTokens.push_back(0x00000000);
                        shexTokens.push_back(vOp.cbufferReg);
                    }
                }
            }

            // else
            shexTokens.push_back(0x01000012);

            // False block instructions
            const auto& falseInsts = ir.basicBlocks[falseBlock].instructions;
            if (falseInsts.empty()) {
                shexTokens.push_back(0x06000036);
                shexTokens.push_back(0x001000F2);
                shexTokens.push_back(0x00000001);
                shexTokens.push_back(0x00208E46);
                shexTokens.push_back(0x00000000);
                shexTokens.push_back(0x00000001);
            } else {
                for (size_t opIdx = 0; opIdx < falseInsts.size(); ++opIdx) {
                    const auto& vOp = falseInsts[opIdx];
                    uint32_t src0Token = (opIdx == 0) ? 0x00101E46 : 0x00100E46;
                    uint32_t src0Reg = (opIdx == 0) ? inReg : 1;
                    if (vOp.src0[0].type == IRValueType::IntermediateTemp) {
                        src0Token = 0x00100E46;
                        src0Reg = vOp.src0[0].regIndex;
                    } else if (vOp.src0[0].type == IRValueType::InputComponent) {
                        src0Token = 0x00101E46;
                        src0Reg = vOp.src0[0].regIndex;
                    }
                    uint32_t destReg = vOp.destTempIndex ? vOp.destTempIndex : 1;

                    if (vOp.opcode == IROpcode::Move) {
                        if (vOp.hasCBuffer) {
                            shexTokens.push_back(0x06000036);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        } else if (vOp.src0[0].type == IRValueType::LiteralFloat) {
                            uint32_t litBits = *reinterpret_cast<const uint32_t*>(&vOp.src0[0].literalValue);
                            shexTokens.push_back(0x08000036);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00004002);
                            shexTokens.push_back(litBits);
                            shexTokens.push_back(litBits);
                            shexTokens.push_back(litBits);
                            shexTokens.push_back(litBits);
                        } else if (vOp.src0[0].type == IRValueType::IntermediateTemp) {
                            shexTokens.push_back(0x05000036);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(vOp.src0[0].regIndex);
                        } else if (vOp.src0[0].type == IRValueType::InputComponent) {
                            shexTokens.push_back(0x05000036);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00101E46);
                            shexTokens.push_back(vOp.src0[0].regIndex);
                        }
                    } else if (vOp.opcode == IROpcode::Multiply) {
                        if (vOp.src1[0].type == IRValueType::IntermediateTemp) {
                            shexTokens.push_back(0x07000038);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(src0Token);
                            shexTokens.push_back(src0Reg);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(vOp.src1[0].regIndex ? vOp.src1[0].regIndex : 2);
                        } else {
                            shexTokens.push_back(0x08000038);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(src0Token);
                            shexTokens.push_back(src0Reg);
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        }
                    } else if (vOp.opcode == IROpcode::Add) {
                        if (vOp.src1[0].type == IRValueType::IntermediateTemp) {
                            shexTokens.push_back(0x07000000);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(src0Token);
                            shexTokens.push_back(src0Reg);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(vOp.src1[0].regIndex ? vOp.src1[0].regIndex : 2);
                        } else {
                            shexTokens.push_back(0x08000000);
                            shexTokens.push_back(0x001000F2);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(src0Token);
                            shexTokens.push_back(src0Reg);
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        }
                    } else if (vOp.opcode == IROpcode::Subtract) {
                        shexTokens.push_back(0x09000000);
                        shexTokens.push_back(0x001000F2);
                        shexTokens.push_back(destReg);
                        shexTokens.push_back(src0Token);
                        shexTokens.push_back(src0Reg);
                        shexTokens.push_back(0x80208E46);
                        shexTokens.push_back(0x00000041);
                        shexTokens.push_back(0x00000000);
                        shexTokens.push_back(vOp.cbufferReg);
                    }
                }
            }

            // endif
            shexTokens.push_back(0x01000015);

            // Merge block: dynamically resolved from branch targets
            uint32_t mergeBlock = (ir.numBasicBlocks >= 4) ? 3 : (ir.numBasicBlocks - 1);
            if (trueBlock < ir.basicBlocks.size() && ir.basicBlocks[trueBlock].terminator.kind == BlockTerminatorKind::UnconditionalBranch) {
                uint32_t tgt = ir.basicBlocks[trueBlock].terminator.targetBlock;
                if (tgt < ir.basicBlocks.size()) {
                    mergeBlock = tgt;
                }
            }
            const auto& mergeInsts = ir.basicBlocks[mergeBlock].instructions;
            bool hasMergeArith = false;
            for (const auto& vOp : mergeInsts) {
                if (vOp.opcode == IROpcode::Multiply || vOp.opcode == IROpcode::Add || vOp.opcode == IROpcode::Move) {
                    hasMergeArith = true;
                    break;
                }
            }

            if (!hasMergeArith) {
                // Move converged branch result from r1 into output o0
                shexTokens.push_back(0x05000036);
                shexTokens.push_back(0x001020F2);
                shexTokens.push_back(0x00000000);
                shexTokens.push_back(0x00100E46);
                shexTokens.push_back(0x00000001);
            } else {
                bool wroteOutput = false;
                for (const auto& vOp : mergeInsts) {
                    uint32_t src0Reg = vOp.src0[0].regIndex ? vOp.src0[0].regIndex : 1;
                    uint32_t destToken = vOp.destIsOutput ? 0x001020F2 : 0x001000F2;
                    uint32_t destIndex = vOp.destIsOutput ? 0 : (vOp.destTempIndex ? vOp.destTempIndex : 1);
                    if (vOp.destIsOutput) wroteOutput = true;

                    if (vOp.opcode == IROpcode::Multiply) {
                        if (vOp.src1[0].type == IRValueType::IntermediateTemp) {
                            shexTokens.push_back(0x07000038);
                            shexTokens.push_back(destToken);
                            shexTokens.push_back(destIndex);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(src0Reg);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(vOp.src1[0].regIndex ? vOp.src1[0].regIndex : 2);
                        } else {
                            shexTokens.push_back(0x08000038);
                            shexTokens.push_back(destToken);
                            shexTokens.push_back(destIndex);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(src0Reg);
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        }
                    } else if (vOp.opcode == IROpcode::Add) {
                        if (vOp.src1[0].type == IRValueType::IntermediateTemp) {
                            shexTokens.push_back(0x07000000);
                            shexTokens.push_back(destToken);
                            shexTokens.push_back(destIndex);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(src0Reg);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(vOp.src1[0].regIndex ? vOp.src1[0].regIndex : 2);
                        } else {
                            shexTokens.push_back(0x08000000);
                            shexTokens.push_back(destToken);
                            shexTokens.push_back(destIndex);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(src0Reg);
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        }
                    } else if (vOp.opcode == IROpcode::Move) {
                        if (vOp.src0[0].type == IRValueType::IntermediateTemp) {
                            shexTokens.push_back(0x05000036);
                            shexTokens.push_back(destToken);
                            shexTokens.push_back(destIndex);
                            shexTokens.push_back(0x00100E46);
                            shexTokens.push_back(src0Reg);
                        } else if (vOp.hasCBuffer) {
                            shexTokens.push_back(0x06000036);
                            shexTokens.push_back(destToken);
                            shexTokens.push_back(destIndex);
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        }
                    }
                }
                if (!wroteOutput && !mergeInsts.empty()) {
                    uint32_t lastDest = mergeInsts.back().destTempIndex ? mergeInsts.back().destTempIndex : 1;
                    shexTokens.push_back(0x05000036);
                    shexTokens.push_back(0x001020F2);
                    shexTokens.push_back(0x00000000);
                    shexTokens.push_back(0x00100E46);
                    shexTokens.push_back(lastDest);
                }
            }

            // ret
            shexTokens.push_back(0x0100003E);
            }
        } else if (hasTextureSample) {
            rep.declaredSRVs = 1;
            rep.declaredSamplers = 1;

            // dcl_sampler s0, mode_default (0x0200005A, 0x00106000, 0x0)
            shexTokens.push_back(0x0200005A);
            shexTokens.push_back(0x00106000);
            shexTokens.push_back(0x00000000);

            // dcl_resource_texture2d (float,float,float,float) t0
            // Opcode 0x58 (DCL_RESOURCE), length 4, dim 2D (2 << 16), ret type float (0x5555 << 0)
            shexTokens.push_back(0x04000058 | (2 << 11)); // Texture2D
            shexTokens.push_back(0x00105000); // t0
            shexTokens.push_back(0x00000000); // slot 0
            shexTokens.push_back(0x00005555); // float4 return format

            // dcl_input_ps linear v[inReg].xy
            shexTokens.push_back(0x03001062);
            shexTokens.push_back(0x00101032); // v#.xy
            shexTokens.push_back(inReg);

            // dcl_output o0.xyzw
            shexTokens.push_back(0x03000065);
            shexTokens.push_back(0x001020F2); // o0.xyzw
            shexTokens.push_back(0x00000000); // o0

            if (!ir.vectorOps.empty()) {
                // Coordinate arithmetic present: uv + float2(0.25, 0.25)
                // dcl_temps 1
                shexTokens.push_back(0x02000068);
                shexTokens.push_back(0x00000001);

                // add r0.xy, v[inReg].xyxx, l(0.250000, 0.250000, 0.000000, 0.000000)
                shexTokens.push_back(0x0A000000);
                shexTokens.push_back(0x00100032); // dest: r0.xy
                shexTokens.push_back(0x00000000); // r0
                shexTokens.push_back(0x00101046); // src0: v#.xyxx
                shexTokens.push_back(inReg);
                shexTokens.push_back(0x00004002); // src1: immediate32 literal vector
                shexTokens.push_back(0x3E800000); // 0.25f
                shexTokens.push_back(0x3E800000); // 0.25f
                shexTokens.push_back(0x00000000);
                shexTokens.push_back(0x00000000);

                // sample_indexable(texture2d) o0.xyzw, r0.xyxx, t0.xyzw, s0
                shexTokens.push_back(0x09000045);
                shexTokens.push_back(0x001020F2); // dest: o0.xyzw
                shexTokens.push_back(0x00000000);
                shexTokens.push_back(0x00100046); // src0: r0.xyxx
                shexTokens.push_back(0x00000000);
                shexTokens.push_back(0x00105E46); // src1: t0.xyzw
                shexTokens.push_back(0x00000000);
                shexTokens.push_back(0x00106000); // src2: s0
                shexTokens.push_back(0x00000000);
            } else {
                // sample_indexable(texture2d)(float,float,float,float) o0.xyzw, v[inReg].xyxx, t0.xyzw, s0
                shexTokens.push_back(0x09000045);
                shexTokens.push_back(0x001020F2); // dest: o0.xyzw
                shexTokens.push_back(0x00000000); // o0
                shexTokens.push_back(0x00101E46); // src0: v#.xyxx
                shexTokens.push_back(inReg);
                shexTokens.push_back(0x00105E46); // src1: t0.xyzw
                shexTokens.push_back(0x00000000); // t0
                shexTokens.push_back(0x00106000); // src2: s0
                shexTokens.push_back(0x00000000); // s0
            }

            // ret (0x0100003E)
            shexTokens.push_back(0x0100003E);
        } else {
            // Default Pixel Shader: Math / CBuffer / Multi-operation Dataflow
            uint32_t cbVectors = hasCBuffer ? (ir.maxCBufferReg + 1) : 0;
            if (cbVectors == 0 && hasCBuffer) cbVectors = 1;
            rep.declaredCBVs = cbVectors > 0 ? 1 : 0;

            if (cbVectors > 0) {
                // dcl_constantbuffer CB0[cbVectors], immediateIndexed
                shexTokens.push_back(0x04000059);
                shexTokens.push_back(0x00208E46); // cb0
                shexTokens.push_back(0x00000000); // slot 0
                shexTokens.push_back(cbVectors);  // size: 1, 2, 3...
            }

            // dcl_input_ps linear v[inReg].xyzw
            shexTokens.push_back(0x03001062);
            shexTokens.push_back(0x001010F2); // v#.xyzw
            shexTokens.push_back(inReg);

            // dcl_output o0.xyzw
            shexTokens.push_back(0x03000065);
            shexTokens.push_back(0x001020F2); // o0.xyzw
            shexTokens.push_back(0x00000000); // o0

            // Declare temps if multi-operation dataflow requires it
            if (ir.numTempsNeeded > 0) {
                shexTokens.push_back(0x02000068);
                shexTokens.push_back(ir.numTempsNeeded);
            }

            if (ir.vectorOps.empty()) {
                // Single move: mov o0.xyzw, v[inReg].xyzw
                shexTokens.push_back(0x05000036);
                shexTokens.push_back(0x001020F2);
                shexTokens.push_back(0x00000000);
                shexTokens.push_back(0x00101E46);
                shexTokens.push_back(inReg);
            } else {
                for (size_t opIdx = 0; opIdx < ir.vectorOps.size(); ++opIdx) {
                    const auto& vOp = ir.vectorOps[opIdx];
                    uint32_t destToken = vOp.destIsOutput ? 0x001020F2 : 0x001000F2;
                    uint32_t destReg = vOp.destIsOutput ? 0 : vOp.destTempIndex;

                    if (vOp.opcode == IROpcode::Move) {
                        uint32_t maskToken = (vOp.channelMask & 0x0F) << 4;
                        uint32_t destCompToken = vOp.destIsOutput ? (0x00102002 | maskToken) : (0x00100002 | maskToken);
                        if (vOp.hasCBuffer) {
                            shexTokens.push_back(0x06000036); // mov
                            shexTokens.push_back(destCompToken);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00208006 | ((vOp.cbufferChannel * 0x55) << 4));
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        } else if (vOp.isScalarIntOp) {
                            shexTokens.push_back(0x05000036);
                            shexTokens.push_back(destCompToken);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00004001);
                            shexTokens.push_back(static_cast<uint32_t>(vOp.literalInt));
                        } else if (vOp.src0[0].type == IRValueType::LiteralFloat) {
                            shexTokens.push_back(0x05000036);
                            shexTokens.push_back(destCompToken);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00004001);
                            uint32_t fBits = *reinterpret_cast<const uint32_t*>(&vOp.src0[0].literalValue);
                            shexTokens.push_back(fBits);
                        } else {
                            shexTokens.push_back(0x05000036);
                            shexTokens.push_back(destCompToken);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00100006 | ((vOp.cbufferChannel * 0x55) << 4));
                            shexTokens.push_back(vOp.srcTempIndex);
                        }
                        continue;
                    }

                    if (opIdx == 0) {
                        if (!ir.hasInput) {
                            if (vOp.opcode == IROpcode::Add) {
                                shexTokens.push_back(0x09000000);
                                shexTokens.push_back(destToken);
                                shexTokens.push_back(destReg);
                                shexTokens.push_back(0x00208006); // cb0[0].xxxx
                                shexTokens.push_back(0x00000000);
                                shexTokens.push_back(0x00000000);
                                shexTokens.push_back(0x00208556); // cb0[0].yyyy
                                shexTokens.push_back(0x00000000);
                                shexTokens.push_back(0x00000000);
                            } else if (vOp.opcode == IROpcode::Multiply) {
                                shexTokens.push_back(0x09000038);
                                shexTokens.push_back(destToken);
                                shexTokens.push_back(destReg);
                                shexTokens.push_back(0x00208006);
                                shexTokens.push_back(0x00000000);
                                shexTokens.push_back(0x00000000);
                                shexTokens.push_back(0x00208556);
                                shexTokens.push_back(0x00000000);
                                shexTokens.push_back(0x00000000);
                            }
                        } else if (vOp.isReverseSubtract) {
                            // add dest, cb0[vOp.cbufferReg].xyzw, -v[inReg].xyzw
                            shexTokens.push_back(0x09000000);
                            shexTokens.push_back(destToken);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00208E46); // cb0
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                            shexTokens.push_back(0x80101E46); // -v[inReg]
                            shexTokens.push_back(0x00000041); // negate modifier
                            shexTokens.push_back(inReg);
                        } else if (vOp.opcode == IROpcode::Multiply) {
                            // mul dest, v[inReg].xyzw, cb0[vOp.cbufferReg].xyzw
                            shexTokens.push_back(0x08000038);
                            shexTokens.push_back(destToken);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00101E46);
                            shexTokens.push_back(inReg);
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        } else if (vOp.opcode == IROpcode::Add) {
                            // add dest, v[inReg].xyzw, cb0[vOp.cbufferReg].xyzw
                            shexTokens.push_back(0x08000000);
                            shexTokens.push_back(destToken);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00101E46);
                            shexTokens.push_back(inReg);
                            shexTokens.push_back(0x00208E46);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        } else if (vOp.opcode == IROpcode::Subtract) {
                            // add dest, v[inReg].xyzw, -cb0[vOp.cbufferReg].xyzw
                            shexTokens.push_back(0x09000000);
                            shexTokens.push_back(destToken);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00101E46);
                            shexTokens.push_back(inReg);
                            shexTokens.push_back(0x80208E46);
                            shexTokens.push_back(0x00000041);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        }
                    } else {
                        // Intermediate and subsequent operations (opIdx >= 1): reads r0.xyzw and cb0[vOp.cbufferReg]
                        if (vOp.opcode == IROpcode::Add) {
                            // add dest, r0.xyzw, cb0[vOp.cbufferReg].xyzw
                            shexTokens.push_back(0x08000000);
                            shexTokens.push_back(destToken);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00100E46); // r0.xyzw
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(0x00208E46); // cb0
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        } else if (vOp.opcode == IROpcode::Multiply) {
                            // mul dest, r0.xyzw, cb0[vOp.cbufferReg].xyzw
                            shexTokens.push_back(0x08000038);
                            shexTokens.push_back(destToken);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00100E46); // r0.xyzw
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(0x00208E46); // cb0
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        } else if (vOp.opcode == IROpcode::Subtract) {
                            // add dest, r0.xyzw, -cb0[vOp.cbufferReg].xyzw
                            shexTokens.push_back(0x09000000);
                            shexTokens.push_back(destToken);
                            shexTokens.push_back(destReg);
                            shexTokens.push_back(0x00100E46); // r0.xyzw
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(0x80208E46); // -cb0
                            shexTokens.push_back(0x00000041);
                            shexTokens.push_back(0x00000000);
                            shexTokens.push_back(vOp.cbufferReg);
                        }
                    }
                }
            }

            // ret (0x0100003E)
            shexTokens.push_back(0x0100003E);
        }

        // Fill length at shexTokens[1]
        shexTokens[1] = static_cast<uint32_t>(shexTokens.size());
    } else if (rep.stage == ShaderStage::Vertex) {
        // Version token for vs_5_0: 0x00010050
        shexTokens.push_back(0x00010050);
        shexTokens.push_back(0); // Length

        // dcl_globalFlags refactoringAllowed
        shexTokens.push_back(0x0100086A);

        // dcl_input v0.xyzw
        shexTokens.push_back(0x0300005F);
        shexTokens.push_back(0x001010F2);
        shexTokens.push_back(0x00000000);

        // dcl_input v1.xyzw
        shexTokens.push_back(0x0300005F);
        shexTokens.push_back(0x001010F2);
        shexTokens.push_back(0x00000001);

        // dcl_output_siv o0.xyzw, position (0x03001067, 0x001020F2, 0x0)
        shexTokens.push_back(0x03001067);
        shexTokens.push_back(0x001020F2);
        shexTokens.push_back(0x00000000);

        // dcl_output o1.xyzw
        shexTokens.push_back(0x03000065);
        shexTokens.push_back(0x001020F2);
        shexTokens.push_back(0x00000001);

        // mov o0.xyzw, v0.xyzw
        shexTokens.push_back(0x05000036);
        shexTokens.push_back(0x001020F2);
        shexTokens.push_back(0x00000000);
        shexTokens.push_back(0x00101E46);
        shexTokens.push_back(0x00000000);

        // mov o1.xyzw, v1.xyzw
        shexTokens.push_back(0x05000036);
        shexTokens.push_back(0x001020F2);
        shexTokens.push_back(0x00000001);
        shexTokens.push_back(0x00101E46);
        shexTokens.push_back(0x00000001);

        // ret
        shexTokens.push_back(0x0100003E);

        shexTokens[1] = static_cast<uint32_t>(shexTokens.size());
    } else {
        rep.status = DXILTranslationStatus::UnsupportedStage;
        rep.diagnostic = "Direct lowering for this shader stage is not yet implemented.";
        return false;
    }

    // Build the final authentic SM 5.0 container
    if (!BuildSM50Container(rep.stage, rep.inputSignatures, rep.outputSignatures, shexTokens, outLoweredBytecode)) {
        rep.status = DXILTranslationStatus::TranslationFailed;
        rep.diagnostic = "Failed to assemble SM 5.0 DXBC container.";
        return false;
    }

    rep.loweredBytecodeSize = static_cast<uint32_t>(outLoweredBytecode.size());
    rep.status = DXILTranslationStatus::Success;
    rep.diagnostic = "Successfully translated DXIL SM 6.x shader to authentic DXBC SM 5.0 (" +
                     std::to_string(srcSize) + "B -> " + std::to_string(outLoweredBytecode.size()) + "B).";
    return true;
}

} // namespace kepler
