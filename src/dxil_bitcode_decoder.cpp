#include "downgrader/dxil_bitcode_decoder.h"
#include "core/logger.h"

#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>

namespace kepler {

namespace {

constexpr uint32_t LLVM_BITCODE_MAGIC = 0xDEC04342; // 'BC\xc0\xde'

// Standard LLVM Block IDs
constexpr uint32_t BLOCK_BLOCKINFO       = 0;
constexpr uint32_t BLOCK_MODULE          = 8;
constexpr uint32_t BLOCK_PARAMATTR       = 9;
constexpr uint32_t BLOCK_PARAMATTR_GROUP = 10;
constexpr uint32_t BLOCK_CONSTANTS       = 11;
constexpr uint32_t BLOCK_FUNCTION        = 12;
constexpr uint32_t BLOCK_VALUE_SYMTAB    = 13;
constexpr uint32_t BLOCK_METADATA        = 14;
constexpr uint32_t BLOCK_TYPE            = 17;

// Function Block Record Codes
constexpr uint32_t FUNC_CODE_DECLAREBLOCKS = 1;
constexpr uint32_t FUNC_CODE_INST_BINOP    = 2;
constexpr uint32_t FUNC_CODE_INST_CAST     = 3;
constexpr uint32_t FUNC_CODE_INST_RET      = 10;
constexpr uint32_t FUNC_CODE_INST_BR       = 11;
constexpr uint32_t FUNC_CODE_INST_SWITCH   = 12;
constexpr uint32_t FUNC_CODE_INST_INVOKE   = 13;
constexpr uint32_t FUNC_CODE_INST_UNREACHABLE = 15;
constexpr uint32_t FUNC_CODE_INST_PHI         = 16;
constexpr uint32_t FUNC_CODE_INST_ALLOCA      = 19;
constexpr uint32_t FUNC_CODE_INST_LOAD        = 20;
constexpr uint32_t FUNC_CODE_INST_STORE_OLD   = 21;
constexpr uint32_t FUNC_CODE_INST_VAARG       = 23;
constexpr uint32_t FUNC_CODE_INST_EXTRACTVAL  = 26;
constexpr uint32_t FUNC_CODE_INST_CMP2        = 28;
constexpr uint32_t FUNC_CODE_INST_CALL        = 34;
constexpr uint32_t FUNC_CODE_INST_GEP         = 43;
constexpr uint32_t FUNC_CODE_INST_STORE       = 44;
constexpr uint32_t FUNC_CODE_INST_STOREATOMIC = 45;
constexpr uint32_t FUNC_CODE_INST_LOADATOMIC  = 46;

class BitstreamCursor {
private:
    const uint8_t* m_data;
    size_t m_totalBits;
    size_t m_bitPos;

public:
    BitstreamCursor(const uint8_t* pData, size_t sizeBytes)
        : m_data(pData), m_totalBits(sizeBytes * 8), m_bitPos(0) {}

    bool IsEOF() const { return m_bitPos >= m_totalBits; }
    size_t GetBitPosition() const { return m_bitPos; }

    uint32_t ReadBits(uint32_t n) {
        if (n == 0) return 0;
        if (m_bitPos + n > m_totalBits) {
            throw std::out_of_range("Bitstream unexpected EOF");
        }
        uint32_t val = 0;
        for (uint32_t i = 0; i < n; ++i) {
            size_t byteIdx = m_bitPos >> 3;
            uint32_t bitIdx = m_bitPos & 7;
            uint32_t bit = (m_data[byteIdx] >> bitIdx) & 1;
            val |= (bit << i);
            m_bitPos++;
        }
        return val;
    }

    uint64_t ReadVBR(uint32_t chunkWidth) {
        uint64_t val = 0;
        uint32_t shift = 0;
        uint64_t mask = (1ULL << (chunkWidth - 1)) - 1ULL;
        uint64_t flag = 1ULL << (chunkWidth - 1);
        while (true) {
            uint64_t chunk = ReadBits(chunkWidth);
            val |= (chunk & mask) << shift;
            if ((chunk & flag) == 0) break;
            shift += (chunkWidth - 1);
            if (shift > 64) {
                throw std::out_of_range("VBR overflow");
            }
        }
        return val;
    }

    void Align32() {
        m_bitPos = (m_bitPos + 31) & ~31ULL;
    }
};

struct AbbrevOp {
    bool isLiteral = false;
    uint32_t enc = 0; // 1=Fixed, 2=VBR, 3=Array, 4=Char6, 5=Blob
    uint64_t val = 0;
};

using AbbrevDefinition = std::vector<AbbrevOp>;

struct BlockState {
    uint32_t blockId = 0;
    uint32_t abbrevLen = 2;
    std::vector<AbbrevDefinition> localAbbrevs;
};

struct RawBinop {
    uint32_t opcode = 0; // 0=add, 1=sub, 2=mul
    uint32_t src0Ssa = 0;
    uint32_t src1Ssa = 0;
    uint32_t blockIdx = 0;
    bool isConst0 = false;
    int64_t constInt0 = 0;
    bool isConst1 = false;
    int64_t constInt1 = 0;
    uint32_t flags = 0;
};

struct RawExtractVal {
    uint32_t aggSsa = 0;
    uint32_t channel = 0;
};

struct BitcodeRecord {
    uint64_t code = 0;
    std::vector<uint64_t> ops;
};

} // anonymous namespace

int64_t DXILBitcodeDecoder::DecodeSignRotatedValue(uint64_t raw) {
    if ((raw & 1) == 0)
        return static_cast<int64_t>(raw >> 1);
    if (raw != 1)
        return -static_cast<int64_t>(raw >> 1);
    // Special case in LLVM bitcode: there is no negative zero (-0).
    // The value 1 encodes the minimum signed 64-bit integer (INT64_MIN: -9223372036854775808LL, 1ULL << 63).
    return static_cast<int64_t>(1ULL << 63);
}

bool DXILBitcodeDecoder::DecodeIntegerConstant(
    uint64_t raw,
    uint32_t bitWidth,
    int64_t& outVal,
    std::string& outError)
{
    if (bitWidth == 0 || bitWidth > 64) {
        outError = "Unsupported or invalid integer bit width: " + std::to_string(bitWidth);
        return false;
    }

    int64_t val = DecodeSignRotatedValue(raw);

    if (bitWidth == 64) {
        outVal = val;
        return true;
    }

    // For narrower signed integer types (1 <= bitWidth < 64):
    // Valid signed range: [-(1LL << (bitWidth - 1)), (1LL << (bitWidth - 1)) - 1]
    int64_t minVal = -(1LL << (bitWidth - 1));
    int64_t maxVal = (1LL << (bitWidth - 1)) - 1;

    // raw == 1 evaluates to INT64_MIN (-2^63), which will correctly fail this check for bitWidth < 64
    if (val < minVal || val > maxVal) {
        outError = "Integer constant " + std::to_string(val) + " overflows type bit width i" +
                   std::to_string(bitWidth) + " (valid signed range: [" + std::to_string(minVal) +
                   ", " + std::to_string(maxVal) + "]).";
        return false;
    }

    outVal = val;
    return true;
}

const char* DXILBitcodeDecoder::GetDXILOpCodeName(DXILOpCode op) {
    switch (op) {
        case DXILOpCode::TempRegLoad: return "TempRegLoad";
        case DXILOpCode::TempRegStore: return "TempRegStore";
        case DXILOpCode::MinPrecXRegLoad: return "MinPrecXRegLoad";
        case DXILOpCode::MinPrecXRegStore: return "MinPrecXRegStore";
        case DXILOpCode::LoadInput: return "LoadInput";
        case DXILOpCode::StoreOutput: return "StoreOutput";
        case DXILOpCode::FAbs: return "FAbs";
        case DXILOpCode::Saturate: return "Saturate";
        case DXILOpCode::IsFPNaN: return "IsFPNaN";
        case DXILOpCode::IsFPInfinite: return "IsFPInfinite";
        case DXILOpCode::IsFinite: return "IsFinite";
        case DXILOpCode::IsNormal: return "IsNormal";
        case DXILOpCode::Cos: return "Cos";
        case DXILOpCode::Sin: return "Sin";
        case DXILOpCode::Tan: return "Tan";
        case DXILOpCode::Acos: return "Acos";
        case DXILOpCode::Asin: return "Asin";
        case DXILOpCode::Atan: return "Atan";
        case DXILOpCode::Hcos: return "Hcos";
        case DXILOpCode::Hsin: return "Hsin";
        case DXILOpCode::Htan: return "Htan";
        case DXILOpCode::Exp: return "Exp";
        case DXILOpCode::Log: return "Log";
        case DXILOpCode::Sqrt: return "Sqrt";
        case DXILOpCode::Rsqrt: return "Rsqrt";
        case DXILOpCode::Round_ne: return "Round_ne";
        case DXILOpCode::Round_ni: return "Round_ni";
        case DXILOpCode::Round_pi: return "Round_pi";
        case DXILOpCode::Round_z: return "Round_z";
        case DXILOpCode::Bfrev: return "Bfrev";
        case DXILOpCode::Countbits: return "Countbits";
        case DXILOpCode::FirstbitLo: return "FirstbitLo";
        case DXILOpCode::FirstbitHi: return "FirstbitHi";
        case DXILOpCode::FirstbitSHi: return "FirstbitSHi";
        case DXILOpCode::FMax: return "FMax";
        case DXILOpCode::FMin: return "FMin";
        case DXILOpCode::IMax: return "IMax";
        case DXILOpCode::IMin: return "IMin";
        case DXILOpCode::UMax: return "UMax";
        case DXILOpCode::UMin: return "UMin";
        case DXILOpCode::FMA: return "FMA";
        case DXILOpCode::CreateHandle: return "CreateHandle";
        case DXILOpCode::CBufferLoad: return "CBufferLoad";
        case DXILOpCode::CBufferLoadLegacy: return "CBufferLoadLegacy";
        case DXILOpCode::Sample: return "Sample";
        case DXILOpCode::SampleBias: return "SampleBias";
        case DXILOpCode::SampleLevel: return "SampleLevel";
        case DXILOpCode::SampleGrad: return "SampleGrad";
        case DXILOpCode::SampleCmp: return "SampleCmp";
        case DXILOpCode::SampleCmpLevelZero: return "SampleCmpLevelZero";
        case DXILOpCode::TextureLoad: return "TextureLoad";
        case DXILOpCode::TextureStore: return "TextureStore";
        case DXILOpCode::BufferLoad: return "BufferLoad";
        case DXILOpCode::BufferStore: return "BufferStore";
        case DXILOpCode::StorePatchConstant: return "StorePatchConstant";
        case DXILOpCode::CreateHandleFromBinding: return "CreateHandleFromBinding";
        default: return "Unknown";
    }
}

const char* DXILBitcodeDecoder::GetDXILOpCodeName(int32_t opCode) {
    return GetDXILOpCodeName(static_cast<DXILOpCode>(opCode));
}

bool DXILBitcodeDecoder::ResolveForwarderTarget(
    const DXILIntermediateRepresentation& ir,
    uint32_t startBlock,
    uint32_t mergeBlock,
    uint32_t& outResolved,
    DXILTranslationStatus& outStatus,
    std::string& outDiagnostic)
{
    const uint32_t N = ir.numBasicBlocks;
    if (startBlock >= N) {
        outStatus = DXILTranslationStatus::MalformedBitcode;
        outDiagnostic = "Start block " + std::to_string(startBlock) + " out of bounds (N=" + std::to_string(N) + ").";
        return false;
    }

    uint32_t curr = startBlock;
    std::set<uint32_t> visited;
    uint32_t steps = 0;

    while (curr < N && curr != mergeBlock) {
        const auto& bb = ir.basicBlocks[curr];
        // An empty forwarder has no instructions, no PHIs, and terminates in UnconditionalBranch
        if (bb.instructions.empty() && bb.phiNodes.empty() &&
            bb.terminator.kind == BlockTerminatorKind::UnconditionalBranch) {

            visited.insert(curr);
            steps++;
            if (steps > N) {
                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                outDiagnostic = "Unsupported cyclic control flow: forwarder chain length (" +
                                std::to_string(steps) + ") exceeds basic block limit (N=" + std::to_string(N) + ").";
                return false;
            }

            uint32_t next = bb.terminator.targetBlock;
            if (next >= N) {
                outStatus = DXILTranslationStatus::MalformedBitcode;
                outDiagnostic = "Forwarder block " + std::to_string(curr) + " target block " +
                                std::to_string(next) + " out of bounds (N=" + std::to_string(N) + ").";
                return false;
            }

            if (visited.find(next) != visited.end()) {
                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                outDiagnostic = "Unsupported cyclic control flow: forwarder cycle detected at block " +
                                std::to_string(next) + ".";
                return false;
            }

            curr = next;
        } else {
            // curr has instructions, PHIs, or non-unconditional terminator -> reached destination
            break;
        }
    }

    outResolved = curr;
    outStatus = DXILTranslationStatus::Success;
    outDiagnostic.clear();
    return true;
}

bool DXILBitcodeDecoder::ValidateCFG(
    DXILIntermediateRepresentation& ir,
    DXILTranslationStatus& outStatus,
    std::string& outDiagnostic)
{
    if (ir.numBasicBlocks <= 1) {
        ir.hasControlFlow = false;
        ir.hasLoop = false;
        return true;
    }

    // Initial graph limits: reject graphs exceeding bounded subset (N > 6 blocks, or N > 16 if switch)
    uint32_t maxAllowedBlocks = ir.hasSwitch ? 16 : 6;
    if (ir.numBasicBlocks > maxAllowedBlocks) {
        outStatus = DXILTranslationStatus::UnsupportedInstruction;
        outDiagnostic = "CFG block count (" + std::to_string(ir.numBasicBlocks) +
                        ") exceeds bounded translator limit (max " + std::to_string(maxAllowedBlocks) + " basic blocks).";
        return false;
    }

    if (ir.basicBlocks.size() != ir.numBasicBlocks) {
        outStatus = DXILTranslationStatus::MalformedBitcode;
        outDiagnostic = "Basic block count mismatch: declared " + std::to_string(ir.numBasicBlocks) +
                        " but decoded " + std::to_string(ir.basicBlocks.size()) + ".";
        return false;
    }

    const uint32_t N = ir.numBasicBlocks;

    // 1. Validate terminators and bounds for each block
    for (uint32_t b = 0; b < N; ++b) {
        const auto& bb = ir.basicBlocks[b];
        if (!bb.hasTerminator || bb.terminator.kind == BlockTerminatorKind::None) {
            outStatus = DXILTranslationStatus::MalformedBitcode;
            outDiagnostic = "Basic block " + std::to_string(b) + " has no terminator instruction.";
            return false;
        }

        if (bb.terminator.kind == BlockTerminatorKind::Switch) {
            uint32_t defBlock = bb.terminator.defaultBlock;
            if (defBlock >= N) {
                outStatus = DXILTranslationStatus::MalformedBitcode;
                outDiagnostic = "Switch default target block " + std::to_string(defBlock) + " out of bounds.";
                return false;
            }
            if (defBlock == b) {
                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                outDiagnostic = "Switch default destination is self-edge (unsupported switch loop).";
                return false;
            }
            for (const auto& sc : bb.terminator.switchCases) {
                if (sc.second >= N) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Switch case target block " + std::to_string(sc.second) + " out of bounds.";
                    return false;
                }
                if (sc.second == b) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Switch case destination " + std::to_string(sc.second) + " is self-edge (unsupported switch loop).";
                    return false;
                }
            }
        }

        if (bb.terminator.kind == BlockTerminatorKind::Unreachable) {
            outStatus = DXILTranslationStatus::MalformedBitcode;
            outDiagnostic = "Basic block " + std::to_string(b) + " terminates with unreachable instruction.";
            return false;
        }

        if (bb.terminator.kind == BlockTerminatorKind::UnconditionalBranch) {
            uint32_t target = bb.terminator.targetBlock;
            if (target >= N) {
                outStatus = DXILTranslationStatus::MalformedBitcode;
                outDiagnostic = "Branch target block " + std::to_string(target) + " out of bounds (N=" +
                                std::to_string(N) + ").";
                return false;
            }
        }
        else if (bb.terminator.kind == BlockTerminatorKind::ConditionalBranch) {
            uint32_t trueBb = bb.terminator.trueBlock;
            uint32_t falseBb = bb.terminator.falseBlock;
            if (trueBb >= N) {
                outStatus = DXILTranslationStatus::MalformedBitcode;
                outDiagnostic = "Conditional branch true target " + std::to_string(trueBb) + " out of bounds.";
                return false;
            }
            if (falseBb >= N) {
                outStatus = DXILTranslationStatus::MalformedBitcode;
                outDiagnostic = "Conditional branch false target " + std::to_string(falseBb) + " out of bounds.";
                return false;
            }
            // Check condition SSA
            if (ir.comparisons.find(bb.terminator.conditionSsa) == ir.comparisons.end() &&
                ir.ssaValues.find(bb.terminator.conditionSsa) == ir.ssaValues.end()) {
                outStatus = DXILTranslationStatus::MalformedBitcode;
                outDiagnostic = "Conditional branch condition SSA " + std::to_string(bb.terminator.conditionSsa) +
                                " not found in decoded values.";
                return false;
            }
        }
        else if (bb.terminator.kind == BlockTerminatorKind::Switch) {
            uint32_t def = bb.terminator.defaultBlock;
            if (def >= N) {
                outStatus = DXILTranslationStatus::MalformedBitcode;
                outDiagnostic = "Switch default target block " + std::to_string(def) + " out of bounds.";
                return false;
            }
            for (const auto& sc : bb.terminator.switchCases) {
                if (sc.second >= N) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Switch case target block " + std::to_string(sc.second) + " out of bounds.";
                    return false;
                }
            }
        }
    }

    // 2. Build verified Predecessor and Successor adjacency lists from terminators
    std::vector<std::vector<uint32_t>> succs(N);
    std::vector<std::vector<uint32_t>> preds(N);
    for (uint32_t b = 0; b < N; ++b) {
        const auto& bb = ir.basicBlocks[b];
        if (bb.terminator.kind == BlockTerminatorKind::UnconditionalBranch) {
            uint32_t t = bb.terminator.targetBlock;
            succs[b].push_back(t);
            preds[t].push_back(b);
        } else if (bb.terminator.kind == BlockTerminatorKind::ConditionalBranch) {
            uint32_t t = bb.terminator.trueBlock;
            uint32_t f = bb.terminator.falseBlock;
            succs[b].push_back(t);
            preds[t].push_back(b);
            succs[b].push_back(f);
            preds[f].push_back(b);
        } else if (bb.terminator.kind == BlockTerminatorKind::Switch) {
            uint32_t def = bb.terminator.defaultBlock;
            succs[b].push_back(def);
            if (std::find(preds[def].begin(), preds[def].end(), b) == preds[def].end()) {
                preds[def].push_back(b);
            }
            for (const auto& sc : bb.terminator.switchCases) {
                uint32_t tgt = sc.second;
                if (std::find(succs[b].begin(), succs[b].end(), tgt) == succs[b].end()) {
                    succs[b].push_back(tgt);
                }
                if (std::find(preds[tgt].begin(), preds[tgt].end(), b) == preds[tgt].end()) {
                    preds[tgt].push_back(b);
                }
            }
        }
    }

    // Update and verify basicBlocks structures
    for (uint32_t b = 0; b < N; ++b) {
        ir.basicBlocks[b].successors = succs[b];
        ir.basicBlocks[b].predecessors = preds[b];
    }

    // 3. Compute reachability from function entry (Block 0)
    std::vector<bool> reachable(N, false);
    std::vector<uint32_t> q;
    reachable[0] = true;
    q.push_back(0);
    size_t head = 0;
    while (head < q.size()) {
        uint32_t curr = q[head++];
        for (uint32_t s : succs[curr]) {
            if (!reachable[s]) {
                reachable[s] = true;
                q.push_back(s);
            }
        }
    }

    for (uint32_t b = 0; b < N; ++b) {
        if (!reachable[b]) {
            outStatus = DXILTranslationStatus::UnsupportedInstruction;
            outDiagnostic = "Unreachable basic blocks are not supported in bounded control flow lowering.";
            return false;
        }
    }

    // 4. Verify PHI incoming predecessors consistency
    for (uint32_t b = 0; b < N; ++b) {
        const auto& bb = ir.basicBlocks[b];
        for (const auto& phi : bb.phiNodes) {
            std::set<uint32_t> phiPreds;
            for (const auto& in : phi.incoming) {
                if (in.predecessorBlock >= N) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "PHI node references out-of-bounds predecessor block " +
                                    std::to_string(in.predecessorBlock) + ".";
                    return false;
                }
                if (std::find(preds[b].begin(), preds[b].end(), in.predecessorBlock) == preds[b].end()) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "PHI node in block " + std::to_string(b) +
                                    " references non-predecessor block " + std::to_string(in.predecessorBlock) + ".";
                    return false;
                }
                if (phiPreds.find(in.predecessorBlock) != phiPreds.end()) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "PHI node in block " + std::to_string(b) +
                                    " has duplicate incoming entry for predecessor block " + std::to_string(in.predecessorBlock) + ".";
                    return false;
                }
                phiPreds.insert(in.predecessorBlock);
            }
            if (phiPreds.size() != preds[b].size()) {
                outStatus = DXILTranslationStatus::MalformedBitcode;
                outDiagnostic = "PHI node in block " + std::to_string(b) +
                                " has missing or inconsistent incoming predecessor entries (expected " +
                                std::to_string(preds[b].size()) + ", got " + std::to_string(phiPreds.size()) + ").";
                return false;
            }
        }
    }

    // 5. Compute Dominance for all reachable blocks
    // Dom(0) = { 0 }
    // For n != 0: Dom(n) = all reachable blocks
    std::vector<std::set<uint32_t>> dom(N);
    dom[0] = { 0 };
    for (uint32_t b = 1; b < N; ++b) {
        for (uint32_t i = 0; i < N; ++i) {
            dom[b].insert(i);
        }
    }

    bool domChanged = true;
    while (domChanged) {
        domChanged = false;
        for (uint32_t b = 1; b < N; ++b) {
            std::set<uint32_t> newDom;
            bool first = true;
            for (uint32_t p : preds[b]) {
                if (first) {
                    newDom = dom[p];
                    first = false;
                } else {
                    std::set<uint32_t> inter;
                    std::set_intersection(newDom.begin(), newDom.end(),
                                          dom[p].begin(), dom[p].end(),
                                          std::inserter(inter, inter.begin()));
                    newDom = inter;
                }
            }
            newDom.insert(b);
            if (newDom != dom[b]) {
                dom[b] = newDom;
                domChanged = true;
            }
        }
    }

    // 6. Identify authentic backedges: an edge u -> v is a backedge iff v in Dom(u)
    struct Backedge {
        uint32_t latch = 0;
        uint32_t header = 0;
    };
    std::vector<Backedge> backedges;
    for (uint32_t u = 0; u < N; ++u) {
        for (uint32_t v : succs[u]) {
            if (dom[u].find(v) != dom[u].end()) {
                backedges.push_back({ u, v });
            }
        }
    }

    // 7. Cycle detection via transitive closure (Warshall's algorithm)
    std::vector<std::vector<bool>> canReach(N, std::vector<bool>(N, false));
    for (uint32_t u = 0; u < N; ++u) {
        for (uint32_t v : succs[u]) {
            canReach[u][v] = true;
        }
    }
    for (uint32_t k = 0; k < N; ++k) {
        for (uint32_t i = 0; i < N; ++i) {
            for (uint32_t j = 0; j < N; ++j) {
                if (canReach[i][k] && canReach[k][j]) {
                    canReach[i][j] = true;
                }
            }
        }
    }

    // Check if any cycle exists without a dominating header (irreducible cycle)
    bool hasAnyCycle = false;
    for (uint32_t b = 0; b < N; ++b) {
        if (canReach[b][b]) {
            hasAnyCycle = true;
            break;
        }
    }

    if (hasAnyCycle && backedges.empty()) {
        outStatus = DXILTranslationStatus::UnsupportedInstruction;
        outDiagnostic = "Irreducible control flow graph detected.";
        return false;
    }

    // 8. Handle Acyclic Control Flow (0 backedges, 0 cycles)
    if (backedges.empty()) {
        ir.hasLoop = false;

        auto AnalyzeSwitchRegion = [&](uint32_t swEntry, CFGRegion& outRegion, uint32_t& outMerge) -> bool {
            const auto& sbb = ir.basicBlocks[swEntry];
            if (sbb.terminator.kind != BlockTerminatorKind::Switch) return false;

            uint32_t defBlock = sbb.terminator.defaultBlock;
            const auto& cases = sbb.terminator.switchCases;
            if (cases.empty() || cases.size() > 8) return false;

            std::set<uint32_t> targetBlocks;
            targetBlocks.insert(defBlock);
            for (const auto& sc : cases) {
                targetBlocks.insert(sc.second);
            }

            // 1. Determine common merge block by resolving forwarders
            uint32_t mergeBlock = UINT32_MAX;
            for (uint32_t tb : targetBlocks) {
                if (tb >= N) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Target block " + std::to_string(tb) + " out of bounds (N=" + std::to_string(N) + ").";
                    return false;
                }

                uint32_t curr = tb;
                std::set<uint32_t> visited;
                uint32_t steps = 0;

                while (curr < N &&
                       ir.basicBlocks[curr].instructions.empty() &&
                       ir.basicBlocks[curr].phiNodes.empty() &&
                       ir.basicBlocks[curr].terminator.kind == BlockTerminatorKind::UnconditionalBranch) {

                    visited.insert(curr);
                    steps++;
                    if (steps > N) {
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Unsupported cyclic control flow: forwarder chain length exceeds basic block limit.";
                        return false;
                    }

                    uint32_t next = ir.basicBlocks[curr].terminator.targetBlock;
                    if (next >= N) {
                        outStatus = DXILTranslationStatus::MalformedBitcode;
                        outDiagnostic = "Branch target block " + std::to_string(next) + " out of bounds (N=" + std::to_string(N) + ").";
                        return false;
                    }

                    if (visited.find(next) != visited.end()) {
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Unsupported cyclic control flow: forwarder cycle detected at block " + std::to_string(next) + ".";
                        return false;
                    }

                    // Check for case fall-through: if curr is a switch target and branches to another switch target,
                    // and that target in turn branches to yet another switch target, it's a fall-through cascade.
                    if (targetBlocks.find(next) != targetBlocks.end()) {
                        const auto& nextBb = ir.basicBlocks[next];
                        if (nextBb.terminator.kind == BlockTerminatorKind::UnconditionalBranch &&
                            targetBlocks.find(nextBb.terminator.targetBlock) != targetBlocks.end()) {
                            outStatus = DXILTranslationStatus::UnsupportedInstruction;
                            outDiagnostic = "Unsupported switch CFG topology: case bodies must branch to a single common merge block without nonempty fall-through.";
                            return false;
                        }
                    }

                    // If next has instructions or PHIs or is non-unconditional, it's a body or merge block!
                    const auto& nextBb = ir.basicBlocks[next];
                    if (!nextBb.instructions.empty() || !nextBb.phiNodes.empty() ||
                        nextBb.terminator.kind != BlockTerminatorKind::UnconditionalBranch) {
                        curr = next;
                        break;
                    }

                    curr = next;
                }

                uint32_t dest = curr;
                if (ir.basicBlocks[curr].terminator.kind == BlockTerminatorKind::UnconditionalBranch) {
                    dest = ir.basicBlocks[curr].terminator.targetBlock;
                }

                if (mergeBlock == UINT32_MAX) {
                    mergeBlock = dest;
                } else if (mergeBlock != dest) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Unsupported switch CFG topology: case bodies must branch to a single common merge block without nonempty fall-through.";
                    return false;
                }
            }

            if (mergeBlock == UINT32_MAX || mergeBlock >= N) return false;

            // 2. Reject nonempty fall-through
            for (uint32_t tb : targetBlocks) {
                if (tb != mergeBlock) {
                    uint32_t dest = ir.basicBlocks[tb].terminator.targetBlock;
                    if (targetBlocks.find(dest) != targetBlocks.end() && dest != mergeBlock) {
                        if (!ir.basicBlocks[tb].instructions.empty() || !ir.basicBlocks[tb].phiNodes.empty()) {
                            outStatus = DXILTranslationStatus::UnsupportedInstruction;
                            outDiagnostic = "Unsupported switch CFG topology: case bodies must branch to a single common merge block without nonempty fall-through.";
                            return false;
                        }
                    }
                }
            }

            // 3. Resolve all case labels and default block targeting
            auto ResolveTarget = [&](uint32_t t) -> uint32_t {
                uint32_t curr = t;
                std::set<uint32_t> visited;
                uint32_t steps = 0;
                while (curr < N && curr != mergeBlock &&
                       ir.basicBlocks[curr].instructions.empty() &&
                       ir.basicBlocks[curr].phiNodes.empty() &&
                       ir.basicBlocks[curr].terminator.kind == BlockTerminatorKind::UnconditionalBranch) {

                    uint32_t next = ir.basicBlocks[curr].terminator.targetBlock;
                    if (next == mergeBlock) {
                        break; // Stop at curr: curr branches to mergeBlock
                    }

                    visited.insert(curr);
                    steps++;
                    if (steps > N || visited.find(next) != visited.end()) {
                        break;
                    }
                    if (next >= N) break;
                    curr = next;
                }
                return curr;
            };

            std::vector<std::pair<int64_t, uint32_t>> resolvedCases;
            for (const auto& sc : cases) {
                resolvedCases.push_back({ sc.first, ResolveTarget(sc.second) });
            }

            uint32_t resolvedDefBlock = ResolveTarget(defBlock);

            outRegion.kind = CFGRegionKind::Switch;
            outRegion.entryBlock = swEntry;
            outRegion.mergeBlock = mergeBlock;
            outRegion.defaultBlock = resolvedDefBlock;
            outRegion.cases = resolvedCases;
            outRegion.selectorSsa = sbb.terminator.switchConditionSsa;
            for (uint32_t tb : targetBlocks) {
                uint32_t rtb = ResolveTarget(tb);
                if (rtb != mergeBlock && std::find(outRegion.internalBlocks.begin(), outRegion.internalBlocks.end(), rtb) == outRegion.internalBlocks.end()) {
                    outRegion.internalBlocks.push_back(rtb);
                }
            }
            outMerge = mergeBlock;
            return true;
        };

        auto AnalyzeCondRegion = [&](uint32_t condEntry, CFGRegion& outRegion, uint32_t& outMerge) -> bool {
            const auto& cbb = ir.basicBlocks[condEntry];
            if (cbb.terminator.kind != BlockTerminatorKind::ConditionalBranch) return false;

            uint32_t t = cbb.terminator.trueBlock;
            uint32_t f = cbb.terminator.falseBlock;
            if (t >= N || f >= N || t == condEntry || f == condEntry || t == f) return false;

            const auto& bt = ir.basicBlocks[t];
            const auto& bf = ir.basicBlocks[f];

            // Diamond: both branch to common merge
            if (bt.terminator.kind == BlockTerminatorKind::UnconditionalBranch &&
                bf.terminator.kind == BlockTerminatorKind::UnconditionalBranch &&
                bt.terminator.targetBlock == bf.terminator.targetBlock)
            {
                uint32_t m = bt.terminator.targetBlock;
                if (m >= N || m == t || m == f) return false;
                outRegion.kind = CFGRegionKind::ConditionalBranch;
                outRegion.entryBlock = condEntry;
                outRegion.trueBlock = t;
                outRegion.falseBlock = f;
                outRegion.mergeBlock = m;
                outRegion.conditionSsa = cbb.terminator.conditionSsa;
                outRegion.isTriangle = false;
                outRegion.internalBlocks = { t, f };
                outMerge = m;
                return true;
            }

            // Triangle: one branches to merge, the other IS the merge
            if (bt.terminator.kind == BlockTerminatorKind::UnconditionalBranch && bt.terminator.targetBlock == f) {
                outRegion.kind = CFGRegionKind::ConditionalBranch;
                outRegion.entryBlock = condEntry;
                outRegion.trueBlock = t;
                outRegion.falseBlock = f;
                outRegion.mergeBlock = f;
                outRegion.conditionSsa = cbb.terminator.conditionSsa;
                outRegion.isTriangle = true;
                outRegion.internalBlocks = { t };
                outMerge = f;
                return true;
            }
            if (bf.terminator.kind == BlockTerminatorKind::UnconditionalBranch && bf.terminator.targetBlock == t) {
                outRegion.kind = CFGRegionKind::ConditionalBranch;
                outRegion.entryBlock = condEntry;
                outRegion.trueBlock = t;
                outRegion.falseBlock = f;
                outRegion.mergeBlock = t;
                outRegion.conditionSsa = cbb.terminator.conditionSsa;
                outRegion.isTriangle = true;
                outRegion.internalBlocks = { f };
                outMerge = t;
                return true;
            }

            return false;
        };

        const auto& b0 = ir.basicBlocks[0];

        if (b0.terminator.kind == BlockTerminatorKind::Switch) {
            CFGRegion swRegion;
            uint32_t swMerge = 0;
            if (!AnalyzeSwitchRegion(0, swRegion, swMerge)) {
                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                outDiagnostic = "Unsupported switch CFG topology: case bodies must branch to a single common merge block without nonempty fall-through.";
                return false;
            }

            const auto& mergeBb = ir.basicBlocks[swMerge];
            if (mergeBb.terminator.kind == BlockTerminatorKind::Return) {
                // Pure Switch
                ir.compoundCFG.pattern = CFGPatternKind::Switch;
                ir.compoundCFG.firstRegion = swRegion;
                ir.compoundCFG.finalMergeBlock = swMerge;

                ir.switchDesc.isSwitch = true;
                ir.switchDesc.switchBlock = 0;
                ir.switchDesc.selectorSsa = swRegion.selectorSsa;
                ir.switchDesc.defaultBlock = swRegion.defaultBlock;
                ir.switchDesc.cases = swRegion.cases;
                ir.switchDesc.mergeBlock = swMerge;
                ir.switchDesc.caseBlocks = swRegion.internalBlocks;
                return true;
            }
            else if (mergeBb.terminator.kind == BlockTerminatorKind::ConditionalBranch) {
                // Switch followed by If/Else!
                CFGRegion condRegion;
                uint32_t condMerge = 0;
                if (!AnalyzeCondRegion(swMerge, condRegion, condMerge)) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Unsupported compound CFG topology: switch followed by malformed conditional branch.";
                    return false;
                }

                if (ir.basicBlocks[condMerge].terminator.kind != BlockTerminatorKind::Return) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Compound CFG final merge block must terminate with Return.";
                    return false;
                }

                ir.compoundCFG.pattern = CFGPatternKind::SwitchThenIfElse;
                ir.compoundCFG.firstRegion = swRegion;
                ir.compoundCFG.secondRegion = condRegion;
                ir.compoundCFG.finalMergeBlock = condMerge;

                ir.switchDesc.isSwitch = true;
                ir.switchDesc.switchBlock = 0;
                ir.switchDesc.selectorSsa = swRegion.selectorSsa;
                ir.switchDesc.defaultBlock = swRegion.defaultBlock;
                ir.switchDesc.cases = swRegion.cases;
                ir.switchDesc.mergeBlock = swMerge;
                ir.switchDesc.caseBlocks = swRegion.internalBlocks;
                return true;
            } else {
                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                outDiagnostic = "Switch merge block must terminate with return in bounded lowering.";
                return false;
            }
        }
        else if (b0.terminator.kind == BlockTerminatorKind::ConditionalBranch) {
            CFGRegion condRegion;
            uint32_t condMerge = 0;
            if (!AnalyzeCondRegion(0, condRegion, condMerge)) {
                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                outDiagnostic = (N == 4) ? "Unsupported 4-block CFG topology: only canonical diamond graphs are supported." :
                               ((N == 3) ? "Unsupported 3-block CFG topology: only canonical triangle graphs are supported." :
                                           "Unsupported conditional branch CFG topology.");
                return false;
            }

            const auto& mergeBb = ir.basicBlocks[condMerge];
            if (mergeBb.terminator.kind == BlockTerminatorKind::Return) {
                // Pure Conditional Branch
                ir.compoundCFG.pattern = CFGPatternKind::ConditionalBranch;
                ir.compoundCFG.firstRegion = condRegion;
                ir.compoundCFG.finalMergeBlock = condMerge;
                return true;
            }
            else if (mergeBb.terminator.kind == BlockTerminatorKind::Switch) {
                // If/Else followed by Switch!
                CFGRegion swRegion;
                uint32_t swMerge = 0;
                if (!AnalyzeSwitchRegion(condMerge, swRegion, swMerge)) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Unsupported compound CFG topology: conditional branch followed by malformed switch.";
                    return false;
                }

                if (ir.basicBlocks[swMerge].terminator.kind != BlockTerminatorKind::Return) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Compound CFG final merge block must terminate with Return.";
                    return false;
                }

                ir.compoundCFG.pattern = CFGPatternKind::IfElseThenSwitch;
                ir.compoundCFG.firstRegion = condRegion;
                ir.compoundCFG.secondRegion = swRegion;
                ir.compoundCFG.finalMergeBlock = swMerge;

                ir.switchDesc.isSwitch = true;
                ir.switchDesc.switchBlock = condMerge;
                ir.switchDesc.selectorSsa = swRegion.selectorSsa;
                ir.switchDesc.defaultBlock = swRegion.defaultBlock;
                ir.switchDesc.cases = swRegion.cases;
                ir.switchDesc.mergeBlock = swMerge;
                ir.switchDesc.caseBlocks = swRegion.internalBlocks;
                return true;
            } else {
                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                outDiagnostic = "Unsupported conditional branch merge block terminator.";
                return false;
            }
        }
        else {
            outStatus = DXILTranslationStatus::UnsupportedInstruction;
            outDiagnostic = "Unsupported entry block terminator: bounded CFG lowering requires Switch or ConditionalBranch entry.";
            return false;
        }
    }

    // 9. Natural Loop Lowering (Backedges >= 1)
    if (ir.hasSwitch) {
        outStatus = DXILTranslationStatus::UnsupportedInstruction;
        outDiagnostic = "Switches nested inside or containing loops are not supported in bounded control flow lowering.";
        return false;
    }

    if (backedges.size() > 1) {
        outStatus = DXILTranslationStatus::UnsupportedInstruction;
        outDiagnostic = "Nested loops or multi-latch loops are not supported in bounded loop lowering.";
        return false;
    }

    uint32_t latch = backedges[0].latch;
    uint32_t header = backedges[0].header;

    // A canonical loop header must not be block 0 (entry node has no preheader)
    if (header == 0) {
        outStatus = DXILTranslationStatus::UnsupportedInstruction;
        outDiagnostic = "Irreducible control flow graph detected: loop header has no preheader.";
        return false;
    }

    // 10. Recover the natural loop: smallest set of nodes containing header and latch,
    // where for every node != header, all its predecessors are in the loop.
    std::set<uint32_t> loopNodes;
    loopNodes.insert(header);
    loopNodes.insert(latch);
    std::vector<uint32_t> worklist;
    if (latch != header) {
        worklist.push_back(latch);
    }
    while (!worklist.empty()) {
        uint32_t curr = worklist.back();
        worklist.pop_back();
        for (uint32_t p : preds[curr]) {
            if (loopNodes.find(p) == loopNodes.end()) {
                loopNodes.insert(p);
                worklist.push_back(p);
            }
        }
    }

    // 11. Verify single-entry property: every node in loopNodes \ {header}
    // must have all its predecessors inside loopNodes.
    for (uint32_t b : loopNodes) {
        if (b == header) continue;
        for (uint32_t p : preds[b]) {
            if (loopNodes.find(p) == loopNodes.end()) {
                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                outDiagnostic = "Irreducible control flow graph detected: loop has multiple entry edges.";
                return false;
            }
        }
    }

    // Check that every node in a cycle belongs to loopNodes
    for (uint32_t b = 0; b < N; ++b) {
        if (canReach[b][b] && loopNodes.find(b) == loopNodes.end()) {
            outStatus = DXILTranslationStatus::UnsupportedInstruction;
            outDiagnostic = "Irreducible control flow graph detected: cycle exists outside natural loop.";
            return false;
        }
    }

    // Complete cycle accounting: In the subgraph without the natural backedge (latch -> header),
    // there MUST NOT be any directed cycle. If any node can reach itself without traversing
    // the single backedge, a secondary cycle, inner cycle, or irreducible cycle exists.
    std::vector<std::vector<bool>> canReachNoBackedge(N, std::vector<bool>(N, false));
    for (uint32_t u = 0; u < N; ++u) {
        for (uint32_t v : succs[u]) {
            if (u == latch && v == header) {
                continue;
            }
            canReachNoBackedge[u][v] = true;
        }
    }
    for (uint32_t k = 0; k < N; ++k) {
        for (uint32_t i = 0; i < N; ++i) {
            for (uint32_t j = 0; j < N; ++j) {
                if (canReachNoBackedge[i][k] && canReachNoBackedge[k][j]) {
                    canReachNoBackedge[i][j] = true;
                }
            }
        }
    }
    for (uint32_t b = 0; b < N; ++b) {
        if (canReachNoBackedge[b][b]) {
            outStatus = DXILTranslationStatus::UnsupportedInstruction;
            outDiagnostic = "Irreducible control flow graph detected: secondary cycle exists alongside natural loop.";
            return false;
        }
    }

    // 12. Identify Preheader: predecessors of header outside loopNodes
    std::vector<uint32_t> outsidePreds;
    for (uint32_t p : preds[header]) {
        if (loopNodes.find(p) == loopNodes.end()) {
            outsidePreds.push_back(p);
        }
    }
    if (outsidePreds.empty()) {
        outStatus = DXILTranslationStatus::UnsupportedInstruction;
        outDiagnostic = "Irreducible control flow graph detected: loop header has no preheader.";
        return false;
    }
    if (outsidePreds.size() > 1) {
        outStatus = DXILTranslationStatus::UnsupportedInstruction;
        outDiagnostic = "Loop has multiple preheader paths.";
        return false;
    }
    uint32_t preheader = outsidePreds[0];

    // 13. Identify Exiting and Exit Blocks
    std::set<uint32_t> exitingBlocks;
    std::set<uint32_t> exitBlocks;
    for (uint32_t u : loopNodes) {
        for (uint32_t s : succs[u]) {
            if (loopNodes.find(s) == loopNodes.end()) {
                exitingBlocks.insert(u);
                exitBlocks.insert(s);
            }
        }
    }

    if (exitBlocks.empty()) {
        outStatus = DXILTranslationStatus::MalformedBitcode;
        outDiagnostic = "Loop has no exit block.";
        return false;
    }

    if (exitingBlocks.size() > 1 || exitBlocks.size() > 1) {
        outStatus = DXILTranslationStatus::UnsupportedInstruction;
        outDiagnostic = "Multi-exit loops are not supported in bounded loop lowering.";
        return false;
    }

    uint32_t exitBlock = *exitBlocks.begin();
    uint32_t exitingBlock = *exitingBlocks.begin();

    // 14. Populate IR Loop structure
    ir.hasLoop = true;
    ir.loop.isLoop = true;
    ir.loop.headerBlock = header;
    ir.loop.latchBlock = latch;
    ir.loop.preheaderBlock = preheader;
    ir.loop.exitingBlock = exitingBlock;
    ir.loop.exitBlock = exitBlock;
    ir.loop.loopBlocks = loopNodes;

    // Validate loop comparison predicate if comparison table is populated
    if (ir.basicBlocks[exitingBlock].hasTerminator &&
        ir.basicBlocks[exitingBlock].terminator.kind == BlockTerminatorKind::ConditionalBranch) {
        uint32_t condSsa = ir.basicBlocks[exitingBlock].terminator.conditionSsa;
        auto itCmp = ir.comparisons.find(condSsa);
        if (itCmp != ir.comparisons.end()) {
            auto pred = itCmp->second.predicate;
            bool isDynamic = (!itCmp->second.isConst1 && itCmp->second.src1.type == IRValueType::CBufferComponent);
            if (isDynamic) {
                if (pred == ComparePredicate::IntSignedLessEqual || pred == ComparePredicate::IntUnsignedLessEqual) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Unsupported dynamic relational loop comparison: dynamic less-or-equal continue condition requires normalization not supported in bounded lowering.";
                    return false;
                } else if (pred == ComparePredicate::IntSignedGreaterThan || pred == ComparePredicate::IntUnsignedGreaterThan) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Unsupported dynamic relational loop comparison: dynamic greater-than continue condition requires normalization not supported in bounded lowering.";
                    return false;
                }
            }
            if (itCmp->second.isConst1) {
                if ((pred == ComparePredicate::IntSignedLessEqual || pred == ComparePredicate::IntSignedGreaterThan) &&
                    itCmp->second.constInt1 == 0x7FFFFFFF) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Integer bound overflow: signed loop bound 0x7FFFFFFF cannot be incremented during normalization.";
                    return false;
                }
                if ((pred == ComparePredicate::IntUnsignedLessEqual || pred == ComparePredicate::IntUnsignedGreaterThan) &&
                    static_cast<uint32_t>(itCmp->second.constInt1) == 0xFFFFFFFFu) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Integer bound overflow: unsigned loop bound 0xFFFFFFFF cannot be incremented during normalization.";
                    return false;
                }
            }
        }
    }

    return true;
}

bool DXILBitcodeDecoder::DecodeBitcode(
    const uint8_t* pBitcode,
    size_t bcSize,
    DXILIntermediateRepresentation& outIR,
    DXILTranslationStatus& outStatus,
    std::string& outDiagnostic)
{
    outIR = DXILIntermediateRepresentation();
    outStatus = DXILTranslationStatus::Success;
    outDiagnostic.clear();

    if (!pBitcode || bcSize < 8) {
        outStatus = DXILTranslationStatus::MalformedBitcode;
        outDiagnostic = "Bitcode buffer is null or shorter than 8 bytes.";
        return false;
    }

    try {
        BitstreamCursor cursor(pBitcode, bcSize);
        uint32_t magic = cursor.ReadBits(32);
        if (magic != LLVM_BITCODE_MAGIC) {
            outStatus = DXILTranslationStatus::MalformedBitcode;
            outDiagnostic = "Invalid LLVM bitcode magic (expected 0xDEC04342).";
            return false;
        }

        std::vector<BlockState> blockStack;
        uint32_t curBlockId = 0xFFFFFFFF;
        uint32_t curAbbrevLen = 2;
        std::vector<AbbrevDefinition> curAbbrevs;

        std::map<uint32_t, std::vector<AbbrevDefinition>> blockInfoAbbrevs;
        uint32_t blockInfoCurBid = 0xFFFFFFFF;

        // Captured block records
        std::vector<BitcodeRecord> typeRecords;
        std::vector<BitcodeRecord> constantRecords;
        std::vector<BitcodeRecord> moduleRecords;
        std::vector<BitcodeRecord> funcRecords;

        enum class ModuleValueKind {
            ModuleFunc,
            Constant,
            GlobalVar,
            Alias
        };

        struct ModuleValueEntry {
            ModuleValueKind kind = ModuleValueKind::ModuleFunc;
            uint32_t tableIndex = 0;
            uint32_t typeId = 0;
        };

        struct ModuleValueEvent {
            ModuleValueKind kind = ModuleValueKind::ModuleFunc;
            uint32_t recordIndex = 0;
        };
        std::vector<ModuleValueEvent> moduleValueEvents;

        while (!cursor.IsEOF()) {
            uint32_t abbrevId = cursor.ReadBits(curAbbrevLen);

            if (abbrevId == 0) { // END_BLOCK
                cursor.Align32();
                if (blockStack.empty()) {
                    break;
                }
                const auto& prev = blockStack.back();
                curBlockId = prev.blockId;
                curAbbrevLen = prev.abbrevLen;
                curAbbrevs = prev.localAbbrevs;
                blockStack.pop_back();
            }
            else if (abbrevId == 1) { // ENTER_SUBBLOCK
                uint32_t subId = static_cast<uint32_t>(cursor.ReadVBR(8));
                uint32_t newAbbrevLen = static_cast<uint32_t>(cursor.ReadVBR(4));
                cursor.Align32();
                cursor.ReadBits(32); // skip block word length

                blockStack.push_back({ curBlockId, curAbbrevLen, curAbbrevs });
                curBlockId = subId;
                curAbbrevLen = newAbbrevLen;

                auto it = blockInfoAbbrevs.find(subId);
                if (it != blockInfoAbbrevs.end()) {
                    curAbbrevs = it->second;
                } else {
                    curAbbrevs.clear();
                }
            }
            else if (abbrevId == 2) { // DEFINE_ABBREV
                uint32_t numOps = static_cast<uint32_t>(cursor.ReadVBR(5));
                AbbrevDefinition abbrev;
                for (uint32_t i = 0; i < numOps; ++i) {
                    bool isLit = (cursor.ReadBits(1) != 0);
                    if (isLit) {
                        uint64_t litVal = cursor.ReadVBR(8);
                        abbrev.push_back({ true, 0, litVal });
                    } else {
                        uint32_t enc = cursor.ReadBits(3);
                        uint64_t val = 0;
                        if (enc == 1 || enc == 2) {
                            val = cursor.ReadVBR(5);
                        }
                        abbrev.push_back({ false, enc, val });
                    }
                }
                if (curBlockId == BLOCK_BLOCKINFO) {
                    blockInfoAbbrevs[blockInfoCurBid].push_back(abbrev);
                } else {
                    curAbbrevs.push_back(abbrev);
                }
            }
            else if (abbrevId == 3) { // UNABBREV_RECORD
                uint64_t code = cursor.ReadVBR(6);
                uint32_t numOps = static_cast<uint32_t>(cursor.ReadVBR(6));
                std::vector<uint64_t> ops(numOps);
                for (uint32_t i = 0; i < numOps; ++i) {
                    ops[i] = cursor.ReadVBR(6);
                }
                if (curBlockId == BLOCK_BLOCKINFO && code == 1 && !ops.empty()) {
                    blockInfoCurBid = static_cast<uint32_t>(ops[0]);
                } else if (curBlockId == BLOCK_TYPE) {
                    typeRecords.push_back({ code, std::move(ops) });
                } else if (curBlockId == BLOCK_CONSTANTS) {
                    if (code != 1) { // not CST_CODE_SETTYPE
                        moduleValueEvents.push_back({ ModuleValueKind::Constant, static_cast<uint32_t>(constantRecords.size()) });
                    }
                    constantRecords.push_back({ code, std::move(ops) });
                } else if (curBlockId == BLOCK_MODULE) {
                    if (code == 8) { // MODULE_CODE_FUNCTION
                        moduleValueEvents.push_back({ ModuleValueKind::ModuleFunc, static_cast<uint32_t>(moduleRecords.size()) });
                    } else if (code == 7) { // MODULE_CODE_GLOBALVAR
                        moduleValueEvents.push_back({ ModuleValueKind::GlobalVar, static_cast<uint32_t>(moduleRecords.size()) });
                    } else if (code == 14) { // MODULE_CODE_ALIAS
                        moduleValueEvents.push_back({ ModuleValueKind::Alias, static_cast<uint32_t>(moduleRecords.size()) });
                    }
                    moduleRecords.push_back({ code, std::move(ops) });
                } else if (curBlockId == BLOCK_FUNCTION) {
                    funcRecords.push_back({ code, std::move(ops) });
                }
            }
            else { // ABBREVIATED RECORD
                uint32_t abbrevIdx = abbrevId - 4;
                if (abbrevIdx >= curAbbrevs.size()) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Invalid abbreviation ID: " + std::to_string(abbrevId);
                    return false;
                }
                const auto& abbrev = curAbbrevs[abbrevIdx];
                std::vector<uint64_t> recOps;

                for (size_t i = 0; i < abbrev.size(); ++i) {
                    const auto& op = abbrev[i];
                    if (op.isLiteral) {
                        recOps.push_back(op.val);
                    } else if (op.enc == 1) { // Fixed
                        recOps.push_back(cursor.ReadBits(static_cast<uint32_t>(op.val)));
                    } else if (op.enc == 2) { // VBR
                        recOps.push_back(cursor.ReadVBR(static_cast<uint32_t>(op.val)));
                    } else if (op.enc == 3) { // Array
                        if (i + 1 >= abbrev.size()) {
                            outStatus = DXILTranslationStatus::MalformedBitcode;
                            outDiagnostic = "Malformed array abbreviation.";
                            return false;
                        }
                        const auto& elemOp = abbrev[i + 1];
                        uint32_t arrLen = static_cast<uint32_t>(cursor.ReadVBR(6));
                        for (uint32_t a = 0; a < arrLen; ++a) {
                            if (elemOp.enc == 1) recOps.push_back(cursor.ReadBits(static_cast<uint32_t>(elemOp.val)));
                            else if (elemOp.enc == 2) recOps.push_back(cursor.ReadVBR(static_cast<uint32_t>(elemOp.val)));
                            else if (elemOp.enc == 4) recOps.push_back(cursor.ReadBits(6));
                            else recOps.push_back(cursor.ReadBits(8));
                        }
                        i++; // skip element op
                    } else if (op.enc == 4) { // Char6
                        recOps.push_back(cursor.ReadBits(6));
                    } else if (op.enc == 5) { // Blob
                        uint32_t blobLen = static_cast<uint32_t>(cursor.ReadVBR(6));
                        cursor.Align32();
                        for (uint32_t b = 0; b < blobLen; ++b) {
                            cursor.ReadBits(8);
                        }
                        cursor.Align32();
                    }
                }

                if (!recOps.empty()) {
                    uint64_t code = recOps[0];
                    std::vector<uint64_t> ops(recOps.begin() + 1, recOps.end());
                    if (curBlockId == BLOCK_TYPE) {
                        typeRecords.push_back({ code, std::move(ops) });
                    } else if (curBlockId == BLOCK_CONSTANTS) {
                        if (code != 1) { // not CST_CODE_SETTYPE
                            moduleValueEvents.push_back({ ModuleValueKind::Constant, static_cast<uint32_t>(constantRecords.size()) });
                        }
                        constantRecords.push_back({ code, std::move(ops) });
                    } else if (curBlockId == BLOCK_MODULE) {
                        if (code == 8) { // MODULE_CODE_FUNCTION
                            moduleValueEvents.push_back({ ModuleValueKind::ModuleFunc, static_cast<uint32_t>(moduleRecords.size()) });
                        } else if (code == 7) { // MODULE_CODE_GLOBALVAR
                            moduleValueEvents.push_back({ ModuleValueKind::GlobalVar, static_cast<uint32_t>(moduleRecords.size()) });
                        } else if (code == 14) { // MODULE_CODE_ALIAS
                            moduleValueEvents.push_back({ ModuleValueKind::Alias, static_cast<uint32_t>(moduleRecords.size()) });
                        }
                        moduleRecords.push_back({ code, std::move(ops) });
                    } else if (curBlockId == BLOCK_FUNCTION) {
                        funcRecords.push_back({ code, std::move(ops) });
                    }
                }
            }
        }

        // 1. Decode Type Table (BLOCK_TYPE)
        std::string lastStructName;
        const char* C6 = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._";
        for (const auto& rec : typeRecords) {
            uint64_t code = rec.code;
            const auto& ops = rec.ops;

            if (code == 1) { // NUMENTRY
                continue;
            } else if (code == 19) { // STRUCT_NAME
                lastStructName.clear();
                for (uint64_t ch : ops) {
                    if (ch < 64) {
                        lastStructName += C6[ch];
                    }
                }
                continue;
            }

            LLVMType t;
            if (code == 2) { // VOID
                t.kind = LLVMTypeKind::Void;
            } else if (code == 3) { // FLOAT (32-bit)
                t.kind = LLVMTypeKind::Float;
            } else if (code == 4) { // DOUBLE (64-bit)
                t.kind = LLVMTypeKind::Double;
            } else if (code == 7) { // INTEGER
                t.kind = LLVMTypeKind::Integer;
                t.bitWidth = !ops.empty() ? static_cast<uint32_t>(ops[0]) : 32;
            } else if (code == 8) { // POINTER
                t.kind = LLVMTypeKind::Pointer;
                t.pointeeType = !ops.empty() ? static_cast<uint32_t>(ops[0]) : 0;
            } else if (code == 10) { // HALF (16-bit)
                t.kind = LLVMTypeKind::Half;
            } else if (code == 11) { // ARRAY
                t.kind = LLVMTypeKind::Array;
                if (ops.size() >= 2) {
                    t.count = static_cast<uint32_t>(ops[0]);
                    t.elemType = static_cast<uint32_t>(ops[1]);
                }
            } else if (code == 12) { // VECTOR
                t.kind = LLVMTypeKind::Vector;
                if (ops.size() >= 2) {
                    t.count = static_cast<uint32_t>(ops[0]);
                    t.elemType = static_cast<uint32_t>(ops[1]);
                }
            } else if (code == 16) { // METADATA
                t.kind = LLVMTypeKind::Metadata;
            } else if (code == 20) { // STRUCT_ANON
                t.kind = LLVMTypeKind::Struct;
                t.name = lastStructName;
                lastStructName.clear();
                if (!ops.empty()) {
                    t.isPacked = (ops[0] != 0);
                    for (size_t i = 1; i < ops.size(); ++i) {
                        t.memberTypes.push_back(static_cast<uint32_t>(ops[i]));
                    }
                }
            } else if (code == 21) { // FUNCTION
                t.kind = LLVMTypeKind::Function;
                if (ops.size() >= 2) {
                    t.returnType = static_cast<uint32_t>(ops[1]);
                    for (size_t i = 2; i < ops.size(); ++i) {
                        t.paramTypes.push_back(static_cast<uint32_t>(ops[i]));
                    }
                }
            } else if (code == 22) { // STRUCT_NAMED
                t.kind = LLVMTypeKind::Struct;
                t.name = lastStructName;
                lastStructName.clear();
                if (!ops.empty()) {
                    t.isPacked = (ops[0] != 0);
                    for (size_t i = 1; i < ops.size(); ++i) {
                        t.memberTypes.push_back(static_cast<uint32_t>(ops[i]));
                    }
                }
            } else {
                t.kind = LLVMTypeKind::Unknown;
            }

            outIR.typeTable.push_back(std::move(t));
        }

        // 2. Decode Constants (BLOCK_CONSTANTS)
        uint32_t curConstType = 0;
        for (const auto& rec : constantRecords) {
            uint64_t code = rec.code;
            const auto& ops = rec.ops;

            LLVMConstant c;
            if (code == 1) { // CST_CODE_SETTYPE
                if (!ops.empty()) {
                    curConstType = static_cast<uint32_t>(ops[0]);
                }
                continue;
            } else if (code == 2) { // CST_CODE_NULL
                c.kind = LLVMConstantKind::Null;
                c.typeId = curConstType;
                c.intVal = 0;
                c.floatVal = 0.0f;
            } else if (code == 3) { // CST_CODE_UNDEF
                c.kind = LLVMConstantKind::Undef;
                c.typeId = curConstType;
                c.intVal = 0;
                c.floatVal = 0.0f;
            } else if (code == 4) { // CST_CODE_INTEGER
                c.kind = LLVMConstantKind::Integer;
                c.typeId = curConstType;
                uint32_t bitWidth = 32;
                if (curConstType < outIR.typeTable.size() &&
                    outIR.typeTable[curConstType].kind == LLVMTypeKind::Integer)
                {
                    bitWidth = outIR.typeTable[curConstType].bitWidth;
                }
                c.bitWidth = bitWidth;

                if (ops.empty()) {
                    c.isInvalid = true;
                    c.errorDiagnostic = "CST_CODE_INTEGER record has no operands.";
                } else {
                    uint64_t raw = ops[0];
                    std::string err;
                    if (!DecodeIntegerConstant(raw, bitWidth, c.intVal, err)) {
                        c.isInvalid = true;
                        c.errorDiagnostic = err;
                    }
                }
            } else if (code == 6) { // CST_CODE_FLOAT
                c.kind = LLVMConstantKind::Float;
                c.typeId = curConstType;
                if (!ops.empty()) {
                    uint32_t bits = static_cast<uint32_t>(ops[0]);
                    std::memcpy(&c.floatVal, &bits, 4);
                }
            } else {
                c.kind = LLVMConstantKind::Other;
                c.typeId = curConstType;
            }

            outIR.constantTable.push_back(c);
        }

        // 3. Module Function Declarations (BLOCK_MODULE)
        for (const auto& rec : moduleRecords) {
            if (rec.code == 8 && !rec.ops.empty()) { // MODULE_CODE_FUNCTION
                outIR.moduleFunctions.push_back(static_cast<uint32_t>(rec.ops[0]));
            }
        }

        // Value Resolver for Module Functions, Constants, and SSA references
        struct DecodedValue {
            enum class Kind { ModuleFunc, Constant, SSA } kind;
            uint32_t index = 0;
            int64_t constInt = 0;
            float constFloat = 0.0f;
            bool isConst = false;
            bool isConstValid = true;
            std::string errorDiagnostic;
        };

        // 3. Build Explicit Module Value Table from sequential ModuleValueEvents
        std::vector<ModuleValueEntry> moduleValueTable;
        uint32_t nextFuncIdx = 0;
        uint32_t nextConstIdx = 0;
        for (const auto& ev : moduleValueEvents) {
            if (ev.kind == ModuleValueKind::ModuleFunc) {
                uint32_t fType = (nextFuncIdx < outIR.moduleFunctions.size()) ? outIR.moduleFunctions[nextFuncIdx] : 0;
                moduleValueTable.push_back({ ModuleValueKind::ModuleFunc, nextFuncIdx++, fType });
            } else if (ev.kind == ModuleValueKind::Constant) {
                uint32_t cType = (nextConstIdx < outIR.constantTable.size()) ? outIR.constantTable[nextConstIdx].typeId : 0;
                moduleValueTable.push_back({ ModuleValueKind::Constant, nextConstIdx++, cType });
            } else if (ev.kind == ModuleValueKind::GlobalVar) {
                moduleValueTable.push_back({ ModuleValueKind::GlobalVar, 0, 0 });
            } else if (ev.kind == ModuleValueKind::Alias) {
                moduleValueTable.push_back({ ModuleValueKind::Alias, 0, 0 });
            }
        }

        uint32_t baseValId = static_cast<uint32_t>(moduleValueTable.size());

        auto ResolveVal = [&](uint64_t rel, uint32_t currentValId) -> DecodedValue {
            if (rel > currentValId) {
                return { DecodedValue::Kind::SSA, 0, 0, 0.0f, false, false, "Relative operand offset exceeds current value ID." };
            }
            uint32_t targetId = currentValId - static_cast<uint32_t>(rel);
            if (targetId < moduleValueTable.size()) {
                const auto& mv = moduleValueTable[targetId];
                if (mv.kind == ModuleValueKind::ModuleFunc) {
                    return { DecodedValue::Kind::ModuleFunc, mv.tableIndex, 0, 0.0f, false, true, "" };
                } else if (mv.kind == ModuleValueKind::Constant) {
                    uint32_t cIdx = mv.tableIndex;
                    if (cIdx >= outIR.constantTable.size()) {
                        return { DecodedValue::Kind::Constant, cIdx, 0, 0.0f, true, false, "Constant index out of bounds." };
                    }
                    const auto& c = outIR.constantTable[cIdx];
                    if (c.isInvalid) {
                        return { DecodedValue::Kind::Constant, cIdx, c.intVal, c.floatVal, true, false, c.errorDiagnostic };
                    }
                    return { DecodedValue::Kind::Constant, cIdx, c.intVal, c.floatVal, true, true, "" };
                } else {
                    return { DecodedValue::Kind::ModuleFunc, 0, 0, 0.0f, false, false, "Module value is neither function nor constant." };
                }
            } else {
                uint32_t ssaIdx = targetId - baseValId;
                return { DecodedValue::Kind::SSA, ssaIdx, 0, 0.0f, false, true, "" };
            }
        };

        // 4. Process Function Instructions
        uint32_t currentSsa = 0;
        uint32_t currentBlockIdx = 0;
        std::map<uint32_t, RawBinop> rawBinops;
        std::map<uint32_t, RawExtractVal> rawExtracts;
        std::map<uint32_t, uint32_t> ssaAliases;

        auto resolveSsa = [&](uint32_t ssa) -> uint32_t {
            std::set<uint32_t> visited;
            while (ssaAliases.find(ssa) != ssaAliases.end()) {
                if (visited.find(ssa) != visited.end()) break;
                visited.insert(ssa);
                ssa = ssaAliases[ssa];
            }
            return ssa;
        };
        
        struct CBufferInfo {
            uint32_t row = 0;
            uint32_t slot = 0;
            uint32_t blockIdx = 0;
        };
        std::map<uint32_t, CBufferInfo> cbufferLoads;
        std::map<uint32_t, bool> sampleLoads;

        uint32_t storeOutputSsa[4] = { 0, 0, 0, 0 };
        uint32_t storeOutputCount = 0;

        for (const auto& rec : funcRecords) {
            uint64_t code = rec.code;
            const auto& ops = rec.ops;

            // Basic block declarations and control flow instructions
            if (code == FUNC_CODE_DECLAREBLOCKS) {
                if (!ops.empty()) {
                    outIR.numBasicBlocks = static_cast<uint32_t>(ops[0]);
                    outIR.hasControlFlow = (outIR.numBasicBlocks > 1);
                    outIR.basicBlocks.resize(outIR.numBasicBlocks);
                    for (uint32_t b = 0; b < outIR.numBasicBlocks; ++b) {
                        outIR.basicBlocks[b].id = b;
                    }
                }
            }
            else if (code == FUNC_CODE_INST_SWITCH) {
                if (ops.size() < 3 || (ops.size() & 1) == 0) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Invalid switch instruction record: expected at least 3 operands and an odd operand count.";
                    return false;
                }

                uint32_t opTypeIdx = static_cast<uint32_t>(ops[0]);
                if (opTypeIdx >= outIR.typeTable.size() || 
                    outIR.typeTable[opTypeIdx].kind != LLVMTypeKind::Integer || 
                    outIR.typeTable[opTypeIdx].bitWidth != 32) 
                {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Unsupported switch selector type: bounded switch lowering only supports 32-bit integer selectors.";
                    return false;
                }

                uint32_t currentValId = baseValId + currentSsa;
                DecodedValue condVal = ResolveVal(ops[1], currentValId);
                uint32_t defaultBlock = static_cast<uint32_t>(ops[2]);
                if (defaultBlock >= outIR.numBasicBlocks) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Switch default target block " + std::to_string(defaultBlock) + 
                                    " is out of bounds (total basic blocks: " + 
                                    std::to_string(outIR.numBasicBlocks) + ").";
                    return false;
                }
                if (defaultBlock == currentBlockIdx) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Switch default destination is self-edge (unsupported switch loop).";
                    return false;
                }

                size_t numCases = (ops.size() - 3) / 2;
                if (numCases > 8) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Switch case count (" + std::to_string(numCases) + 
                                    ") exceeds bounded translator limit (max 8 cases).";
                    return false;
                }

                std::set<uint32_t> seenCaseVals;
                std::vector<std::pair<int64_t, uint32_t>> switchCases;
                for (size_t c = 0; c < numCases; ++c) {
                    uint64_t caseValAbsId = ops[3 + c * 2];
                    if (caseValAbsId >= moduleValueTable.size()) {
                        outStatus = DXILTranslationStatus::MalformedBitcode;
                        outDiagnostic = "Switch case value ID (" + std::to_string(caseValAbsId) + 
                                        ") is out of bounds (module value count: " + 
                                        std::to_string(moduleValueTable.size()) + ").";
                        return false;
                    }
                    const auto& mv = moduleValueTable[caseValAbsId];
                    if (mv.kind != ModuleValueKind::Constant) {
                        outStatus = DXILTranslationStatus::MalformedBitcode;
                        outDiagnostic = "Switch case value ID (" + std::to_string(caseValAbsId) + 
                                        ") does not reference a module constant.";
                        return false;
                    }
                    uint32_t cIdx = mv.tableIndex;
                    if (cIdx >= outIR.constantTable.size()) {
                        outStatus = DXILTranslationStatus::MalformedBitcode;
                        outDiagnostic = "Switch case constant index out of bounds.";
                        return false;
                    }
                    const auto& cConst = outIR.constantTable[cIdx];
                    if (cConst.isInvalid) {
                        outStatus = DXILTranslationStatus::MalformedBitcode;
                        outDiagnostic = "Switch case references invalid constant: " + cConst.errorDiagnostic;
                        return false;
                    }
                    if (cConst.kind != LLVMConstantKind::Integer && cConst.kind != LLVMConstantKind::Null) {
                        outStatus = DXILTranslationStatus::MalformedBitcode;
                        outDiagnostic = "Switch case value is not an integer constant.";
                        return false;
                    }
                    if (cConst.typeId >= outIR.typeTable.size()) {
                        outStatus = DXILTranslationStatus::MalformedBitcode;
                        outDiagnostic = "Switch case constant references out-of-bounds type ID.";
                        return false;
                    }
                    const auto& constType = outIR.typeTable[cConst.typeId];
                    if (constType.kind != LLVMTypeKind::Integer || constType.bitWidth != outIR.typeTable[opTypeIdx].bitWidth) {
                        outStatus = DXILTranslationStatus::MalformedBitcode;
                        outDiagnostic = "Switch case constant bitwidth (" + std::to_string(constType.bitWidth) + 
                                        ") does not match selector bitwidth (" + 
                                        std::to_string(outIR.typeTable[opTypeIdx].bitWidth) + ").";
                        return false;
                    }

                    int64_t caseVal = cConst.intVal;
                    uint32_t caseBits = static_cast<uint32_t>(caseVal);
                    if (seenCaseVals.find(caseBits) != seenCaseVals.end()) {
                        outStatus = DXILTranslationStatus::MalformedBitcode;
                        outDiagnostic = "Duplicate switch case constant (" + std::to_string(caseVal) + ") detected.";
                        return false;
                    }
                    seenCaseVals.insert(caseBits);

                    uint32_t targetBlock = static_cast<uint32_t>(ops[4 + c * 2]);
                    if (targetBlock >= outIR.numBasicBlocks) {
                        outStatus = DXILTranslationStatus::MalformedBitcode;
                        outDiagnostic = "Switch case target block " + std::to_string(targetBlock) + 
                                        " is out of bounds (total basic blocks: " + 
                                        std::to_string(outIR.numBasicBlocks) + ").";
                        return false;
                    }
                    if (targetBlock == currentBlockIdx) {
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Switch case destination " + std::to_string(targetBlock) + " is self-edge (unsupported switch loop).";
                        return false;
                    }
                    switchCases.push_back({ caseVal, targetBlock });
                }

                if (currentBlockIdx < outIR.basicBlocks.size()) {
                    auto& bb = outIR.basicBlocks[currentBlockIdx];
                    bb.terminator.kind = BlockTerminatorKind::Switch;
                    bb.terminator.switchConditionSsa = condVal.index;
                    bb.terminator.defaultBlock = defaultBlock;
                    bb.terminator.switchCases = std::move(switchCases);
                    bb.hasTerminator = true;
                    outIR.hasSwitch = true;

                    bb.successors.push_back(defaultBlock);
                    if (defaultBlock < outIR.basicBlocks.size()) {
                        outIR.basicBlocks[defaultBlock].predecessors.push_back(currentBlockIdx);
                    }
                    for (const auto& sc : bb.terminator.switchCases) {
                        uint32_t tgt = sc.second;
                        if (std::find(bb.successors.begin(), bb.successors.end(), tgt) == bb.successors.end()) {
                            bb.successors.push_back(tgt);
                            if (tgt < outIR.basicBlocks.size()) {
                                outIR.basicBlocks[tgt].predecessors.push_back(currentBlockIdx);
                            }
                        }
                    }
                }
                currentBlockIdx++;
            }
            else if (code == FUNC_CODE_INST_INVOKE) {
                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                outDiagnostic = "Shader contains unsupported invoke instruction.";
                return false;
            }
            else if (code == FUNC_CODE_INST_UNREACHABLE) {
                if (currentBlockIdx < outIR.basicBlocks.size()) {
                    outIR.basicBlocks[currentBlockIdx].terminator.kind = BlockTerminatorKind::Unreachable;
                    outIR.basicBlocks[currentBlockIdx].hasTerminator = true;
                }
                currentBlockIdx++;
            }
            else if (code == FUNC_CODE_INST_CMP2) {
                uint32_t currentValId = baseValId + currentSsa;
                if (ops.size() >= 3) {
                    DecodedValue src0Val = ResolveVal(ops[0], currentValId);
                    DecodedValue src1Val = ResolveVal(ops[1], currentValId);
                    uint32_t predCode = static_cast<uint32_t>(ops[2]);
                    ComparePredicate pred = ComparePredicate::None;
                    switch (predCode) {
                        case 1: case 9:   pred = ComparePredicate::FloatEqual; break;
                        case 2: case 10:  pred = ComparePredicate::FloatGreaterThan; break;
                        case 3: case 11:  pred = ComparePredicate::FloatGreaterOrEqual; break;
                        case 4: case 12:  pred = ComparePredicate::FloatLessThan; break;
                        case 5: case 13:  pred = ComparePredicate::FloatLessOrEqual; break;
                        case 6: case 14:  pred = ComparePredicate::FloatNotEqual; break;

                        case 32: pred = ComparePredicate::IntEqual; break;
                        case 33: pred = ComparePredicate::IntNotEqual; break;
                        case 34: pred = ComparePredicate::IntUnsignedGreaterThan; break;
                        case 35: pred = ComparePredicate::IntUnsignedGreaterEqual; break;
                        case 36: pred = ComparePredicate::IntUnsignedLessThan; break;
                        case 37: pred = ComparePredicate::IntUnsignedLessEqual; break;
                        case 38: pred = ComparePredicate::IntSignedGreaterThan; break;
                        case 39: pred = ComparePredicate::IntSignedGreaterEqual; break;
                        case 40: pred = ComparePredicate::IntSignedLessThan; break;
                        case 41: pred = ComparePredicate::IntSignedLessEqual; break;
                        default: pred = ComparePredicate::None; break;
                    }
                    CompareInstruction cmpInst;
                    cmpInst.destSsa = currentSsa;
                    cmpInst.predicate = pred;
                    if (src0Val.kind == DecodedValue::Kind::SSA) {
                        auto it = outIR.ssaValues.find(src0Val.index);
                        if (it != outIR.ssaValues.end()) cmpInst.src0 = it->second;
                    } else if (src0Val.isConst) {
                        cmpInst.src0 = IRValue::MakeLiteral(src0Val.constFloat, 0);
                        cmpInst.isConst0 = true;
                        cmpInst.constInt0 = src0Val.constInt;
                    }
                    if (src1Val.kind == DecodedValue::Kind::SSA) {
                        auto it = outIR.ssaValues.find(src1Val.index);
                        if (it != outIR.ssaValues.end()) cmpInst.src1 = it->second;
                    } else if (src1Val.isConst) {
                        cmpInst.src1 = IRValue::MakeLiteral(src1Val.constFloat, 0);
                        cmpInst.isConst1 = true;
                        cmpInst.constInt1 = src1Val.constInt;
                    }
                    outIR.comparisons[currentSsa] = cmpInst;
                    outIR.ssaValues[currentSsa] = IRValue::MakeTemp(0, 0, currentSsa);
                }
                currentSsa++;
            }
            else if (code == FUNC_CODE_INST_BR) {
                uint32_t currentValId = baseValId + currentSsa;
                if (currentBlockIdx < outIR.basicBlocks.size()) {
                    auto& bb = outIR.basicBlocks[currentBlockIdx];
                    if (ops.size() == 1) {
                        uint32_t target = static_cast<uint32_t>(ops[0]);
                        bb.terminator.kind = BlockTerminatorKind::UnconditionalBranch;
                        bb.terminator.targetBlock = target;
                        bb.successors.push_back(target);
                        bb.hasTerminator = true;
                        if (target < outIR.basicBlocks.size()) {
                            outIR.basicBlocks[target].predecessors.push_back(currentBlockIdx);
                        }
                    } else if (ops.size() >= 3) {
                        uint32_t trueBb = static_cast<uint32_t>(ops[0]);
                        uint32_t falseBb = static_cast<uint32_t>(ops[1]);
                        DecodedValue condVal = ResolveVal(ops[2], currentValId);
                        bb.terminator.kind = BlockTerminatorKind::ConditionalBranch;
                        bb.terminator.trueBlock = trueBb;
                        bb.terminator.falseBlock = falseBb;
                        bb.terminator.conditionSsa = condVal.index;
                        bb.successors.push_back(trueBb);
                        bb.successors.push_back(falseBb);
                        bb.hasTerminator = true;
                        if (trueBb < outIR.basicBlocks.size()) {
                            outIR.basicBlocks[trueBb].predecessors.push_back(currentBlockIdx);
                        }
                        if (falseBb < outIR.basicBlocks.size()) {
                            outIR.basicBlocks[falseBb].predecessors.push_back(currentBlockIdx);
                        }
                    }
                }
                currentBlockIdx++;
            }
            else if (code == FUNC_CODE_INST_PHI) {
                uint32_t currentValId = baseValId + currentSsa;
                if (ops.size() >= 1 && currentBlockIdx < outIR.basicBlocks.size()) {
                    DXILPHINode phi;
                    phi.destSsa = currentSsa;
                    phi.typeId = static_cast<uint32_t>(ops[0]);
                    phi.channel = static_cast<uint8_t>(outIR.basicBlocks[currentBlockIdx].phiNodes.size() % 4);
                    for (size_t k = 1; k + 1 < ops.size(); k += 2) {
                        int64_t relVal = DecodeSignRotatedValue(ops[k]);
                        uint32_t predBlock = static_cast<uint32_t>(ops[k + 1]);
                        uint32_t incomingSsa = 0;
                        bool isConst = false;
                        int64_t constInt = 0;
                        if (relVal < 0) {
                            uint32_t forwardOffset = static_cast<uint32_t>(-relVal);
                            uint32_t targetValId = currentValId + forwardOffset;
                            if (targetValId >= baseValId) {
                                incomingSsa = targetValId - baseValId;
                            }
                        } else if (relVal > 0 && static_cast<uint32_t>(relVal) <= currentValId) {
                            uint32_t targetValId = currentValId - static_cast<uint32_t>(relVal);
                            if (targetValId >= baseValId) {
                                incomingSsa = targetValId - baseValId;
                            } else if (targetValId < moduleValueTable.size()) {
                                const auto& mv = moduleValueTable[targetValId];
                                if (mv.kind == ModuleValueKind::Constant && mv.tableIndex < outIR.constantTable.size()) {
                                    isConst = true;
                                    constInt = outIR.constantTable[mv.tableIndex].intVal;
                                }
                            }
                        }
                        phi.incoming.push_back({ incomingSsa, predBlock, isConst, constInt });
                    }
                    outIR.basicBlocks[currentBlockIdx].phiNodes.push_back(phi);
                    outIR.ssaValues[currentSsa] = IRValue::MakeTemp(1, phi.channel, currentSsa);
                }
                currentSsa++;
            }
            else if (code == FUNC_CODE_INST_BINOP) {
                uint32_t currentValId = baseValId + currentSsa;
                if (ops.size() >= 3) {
                    DecodedValue src0Val = ResolveVal(ops[0], currentValId);
                    DecodedValue src1Val = ResolveVal(ops[1], currentValId);
                    uint32_t opcode = static_cast<uint32_t>(ops[2]);
                    RawBinop raw;
                    raw.opcode = opcode;
                    raw.src0Ssa = (src0Val.kind == DecodedValue::Kind::SSA) ? resolveSsa(src0Val.index) : src0Val.index;
                    raw.src1Ssa = (src1Val.kind == DecodedValue::Kind::SSA) ? resolveSsa(src1Val.index) : src1Val.index;
                    raw.blockIdx = currentBlockIdx;
                    if (src0Val.isConst) {
                        raw.isConst0 = true;
                        raw.constInt0 = src0Val.constInt;
                    }
                    if (src1Val.isConst) {
                        raw.isConst1 = true;
                        raw.constInt1 = src1Val.constInt;
                    }
                    if (ops.size() >= 4) {
                        raw.flags = static_cast<uint32_t>(ops[3]);
                    }
                    rawBinops[currentSsa] = raw;
                }
                currentSsa++;
            }
            else if (code == FUNC_CODE_INST_EXTRACTVAL) {
                uint32_t currentValId = baseValId + currentSsa;
                if (ops.size() >= 2) {
                    DecodedValue aggVal = ResolveVal(ops[0], currentValId);
                    uint32_t channel = static_cast<uint32_t>(ops[1]);
                    if (aggVal.kind == DecodedValue::Kind::SSA) {
                        uint32_t aggSsa = aggVal.index;
                        rawExtracts[currentSsa] = { aggSsa, channel };

                        auto cbIt = cbufferLoads.find(aggSsa);
                        if (cbIt != cbufferLoads.end()) {
                            uint32_t cbRow = cbIt->second.row;
                            uint32_t cbSlot = cbIt->second.slot;
                            outIR.ssaValues[currentSsa] = IRValue::MakeCBuffer(cbSlot, cbRow, static_cast<uint8_t>(channel), currentSsa);
                            outIR.hasCBuffer = true;
                            if (cbRow > outIR.maxCBufferReg) {
                                outIR.maxCBufferReg = cbRow;
                            }
                        } else if (sampleLoads.find(aggSsa) != sampleLoads.end()) {
                            outIR.ssaValues[currentSsa] = IRValue::MakeSample(static_cast<uint8_t>(channel), currentSsa);
                        }
                    }
                }
                currentSsa++;
            }
            else if (code == FUNC_CODE_INST_ALLOCA) {
                if (ops.empty()) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Truncated alloca instruction record (0 operands).";
                    return false;
                }
                uint32_t allocatedTypeIdx = static_cast<uint32_t>(ops[0]);
                if (allocatedTypeIdx >= outIR.typeTable.size()) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Invalid type ID " + std::to_string(allocatedTypeIdx) + 
                                    " in alloca instruction (type table size: " + 
                                    std::to_string(outIR.typeTable.size()) + ").";
                    return false;
                }

                const auto& allocType = outIR.typeTable[allocatedTypeIdx];
                LLVMTypeKind elemKind = LLVMTypeKind::Unknown;
                uint32_t numElements = 1;
                bool isArray = false;
                bool isVector = false;

                if (allocType.kind == LLVMTypeKind::Float) {
                    elemKind = LLVMTypeKind::Float;
                    numElements = 1;
                } else if (allocType.kind == LLVMTypeKind::Integer && allocType.bitWidth == 32) {
                    elemKind = LLVMTypeKind::Integer;
                    numElements = 1;
                } else if (allocType.kind == LLVMTypeKind::Array) {
                    if (allocType.count > 4) {
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Unsupported array size (" + std::to_string(allocType.count) +
                                        ") in local alloca: bounded translator supports maximum 4 elements.";
                        return false;
                    }
                    if (allocType.elemType >= outIR.typeTable.size()) {
                        outStatus = DXILTranslationStatus::MalformedBitcode;
                        outDiagnostic = "Invalid array element type ID in alloca.";
                        return false;
                    }
                    const auto& elemType = outIR.typeTable[allocType.elemType];
                    if (elemType.kind == LLVMTypeKind::Float) {
                        elemKind = LLVMTypeKind::Float;
                    } else if (elemType.kind == LLVMTypeKind::Integer && elemType.bitWidth == 32) {
                        elemKind = LLVMTypeKind::Integer;
                    } else {
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Unsupported array element type in local alloca (only 32-bit float or integer supported).";
                        return false;
                    }
                    numElements = allocType.count;
                    isArray = true;
                } else if (allocType.kind == LLVMTypeKind::Vector) {
                    if (allocType.count > 4) {
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Unsupported vector size in local alloca: bounded translator supports up to 4 lanes.";
                        return false;
                    }
                    if (allocType.elemType >= outIR.typeTable.size()) {
                        outStatus = DXILTranslationStatus::MalformedBitcode;
                        outDiagnostic = "Invalid vector element type ID in alloca.";
                        return false;
                    }
                    const auto& elemType = outIR.typeTable[allocType.elemType];
                    if (elemType.kind == LLVMTypeKind::Float) {
                        elemKind = LLVMTypeKind::Float;
                    } else if (elemType.kind == LLVMTypeKind::Integer && elemType.bitWidth == 32) {
                        elemKind = LLVMTypeKind::Integer;
                    } else {
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Unsupported vector element type in local alloca.";
                        return false;
                    }
                    numElements = allocType.count;
                    isVector = true;
                } else {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Unsupported type for function-local alloca: only 32-bit scalar, <=4-element array, or <=4-lane vector supported.";
                    return false;
                }

                uint32_t slotIdx = static_cast<uint32_t>(outIR.localAllocs.size());
                LocalAllocDescriptor desc;
                desc.slotIndex = slotIdx;
                desc.ssaPointer = currentSsa;
                desc.allocatedTypeId = allocatedTypeIdx;
                desc.elemKind = elemKind;
                desc.numElements = numElements;
                desc.isArray = isArray;
                desc.isVector = isVector;
                desc.alignment = (ops.size() >= 4) ? static_cast<uint32_t>(ops[3] & 0x1F) : 4;
                desc.elements.resize(numElements);
                outIR.localAllocs.push_back(desc);

                outIR.localPointers[currentSsa] = { slotIdx, 0, true };
                currentSsa++;
            }
            else if (code == FUNC_CODE_INST_GEP) {
                uint32_t currentValId = baseValId + currentSsa;
                if (ops.size() < 3) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Truncated GEP instruction record (" + std::to_string(ops.size()) + " operands, expected >= 3).";
                    return false;
                }

                // ops[0] = inbounds, ops[1] = pointeeType, ops[2] = ptrRel
                DecodedValue ptrVal = ResolveVal(ops[2], currentValId);
                if (ptrVal.kind != DecodedValue::Kind::SSA) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "GEP base pointer is not a local SSA value (global or constant pointer GEP is unsupported).";
                    return false;
                }

                auto itPtr = outIR.localPointers.find(ptrVal.index);
                if (itPtr == outIR.localPointers.end() || !itPtr->second.isValid) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "GEP on non-local, global, or escaping pointer is unsupported.";
                    return false;
                }

                uint32_t slotIdx = itPtr->second.slotIndex;
                if (slotIdx >= outIR.localAllocs.size()) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "GEP base pointer references invalid local slot " + std::to_string(slotIdx) + ".";
                    return false;
                }
                const auto& alloc = outIR.localAllocs[slotIdx];

                // Outer index (ops[3]) must be constant 0 (dereferencing the pointer)
                if (ops.size() >= 4) {
                    DecodedValue idx0Val = ResolveVal(ops[3], currentValId);
                    if (!idx0Val.isConst || idx0Val.constInt != 0) {
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Non-zero outer index in GEP is unsupported in bounded translator.";
                        return false;
                    }
                }

                uint32_t subSlot = 0;
                if (ops.size() >= 5) {
                    DecodedValue idx1Val = ResolveVal(ops[4], currentValId);
                    if (!idx1Val.isConst) {
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Dynamic-index GEP is unsupported in bounded translator.";
                        return false;
                    }
                    int64_t cIdx = idx1Val.constInt;
                    if (cIdx < 0 || cIdx >= static_cast<int64_t>(alloc.numElements)) {
                        outStatus = DXILTranslationStatus::MalformedBitcode;
                        outDiagnostic = "GEP constant index " + std::to_string(cIdx) + 
                                        " out of bounds for allocation of size " + 
                                        std::to_string(alloc.numElements) + ".";
                        return false;
                    }
                    subSlot = static_cast<uint32_t>(cIdx);
                }

                outIR.localPointers[currentSsa] = { slotIdx, subSlot, true };
                currentSsa++;
            }
            else if (code == FUNC_CODE_INST_STORE || code == FUNC_CODE_INST_STORE_OLD) {
                uint32_t currentValId = baseValId + currentSsa;
                if (ops.size() < 4) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Truncated store instruction record (" + std::to_string(ops.size()) + " operands, expected >= 4).";
                    return false;
                }

                // ops[0] = ptrRel, ops[1] = valRel, ops[2] = align, ops[3] = isVolatile
                if (ops[3] != 0) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Volatile store is unsupported.";
                    return false;
                }

                DecodedValue ptrVal = ResolveVal(ops[0], currentValId);
                if (ptrVal.kind != DecodedValue::Kind::SSA) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Store to non-SSA pointer is unsupported.";
                    return false;
                }

                auto itPtr = outIR.localPointers.find(ptrVal.index);
                if (itPtr == outIR.localPointers.end() || !itPtr->second.isValid) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Store to non-local or escaping pointer is unsupported.";
                    return false;
                }

                uint32_t slotIdx = itPtr->second.slotIndex;
                uint32_t subSlot = itPtr->second.subSlotIndex;
                if (slotIdx >= outIR.localAllocs.size()) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Store target slot out of bounds.";
                    return false;
                }

                auto& alloc = outIR.localAllocs[slotIdx];
                if (subSlot >= alloc.numElements) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Store target sub-slot " + std::to_string(subSlot) + 
                                    " out of bounds for allocation of size " + 
                                    std::to_string(alloc.numElements) + ".";
                    return false;
                }

                DecodedValue val = ResolveVal(ops[1], currentValId);
                if (val.isConst && !val.isConstValid) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Invalid constant in store instruction: " + val.errorDiagnostic;
                    return false;
                }

                // Update element state (read-after-write tracking)
                alloc.elements[subSlot].isInitialized = true;
                alloc.elements[subSlot].isConstant = val.isConst;
                alloc.elements[subSlot].constInt = val.constInt;
                alloc.elements[subSlot].constFloat = val.constFloat;
                alloc.elements[subSlot].currentSsa = val.isConst ? 0 : resolveSsa(val.index);

                // Record local store operation in the current basic block
                if (currentBlockIdx < outIR.basicBlocks.size()) {
                    IRVectorOp movOp;
                    movOp.opcode = IROpcode::Move;
                    movOp.isLocalMemOp = true;
                    movOp.isLocalStore = true;
                    movOp.localSlot = slotIdx;
                    movOp.localSubSlot = subSlot;
                    movOp.channelMask = (1u << subSlot);
                    movOp.destTempIndex = alloc.tempRegIndex;
                    if (val.isConst) {
                        if (alloc.elemKind == LLVMTypeKind::Integer) {
                            movOp.isScalarIntOp = true;
                            movOp.literalInt = val.constInt;
                        } else {
                            movOp.src0[0] = IRValue::MakeLiteral(val.constFloat, 0);
                        }
                    } else {
                        auto itSsa = outIR.ssaValues.find(val.index);
                        if (itSsa != outIR.ssaValues.end()) {
                            movOp.src0[0] = itSsa->second;
                            if (itSsa->second.type == IRValueType::CBufferComponent) {
                                movOp.hasCBuffer = true;
                                movOp.cbufferReg = itSsa->second.regIndex;
                                movOp.cbufferChannel = itSsa->second.channel;
                                movOp.isComponentMove = true;
                            }
                        }
                    }
                    outIR.basicBlocks[currentBlockIdx].instructions.push_back(movOp);
                }
                // Store returns void in LLVM, so currentSsa does not advance
            }
            else if (code == FUNC_CODE_INST_LOAD) {
                uint32_t currentValId = baseValId + currentSsa;
                if (ops.size() < 2) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Truncated load instruction record (" + std::to_string(ops.size()) + " operands, expected >= 2).";
                    return false;
                }

                // ops[0] = ptrRel, ops[1] = typeId, ops[2] = align, ops[3] = isVolatile
                if (ops.size() >= 4 && ops[3] != 0) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Volatile load is unsupported.";
                    return false;
                }

                DecodedValue ptrVal = ResolveVal(ops[0], currentValId);

                // Handle compiler debug dummy loads (e.g. from @dx.nothing.a)
                if (ptrVal.kind == DecodedValue::Kind::Constant || ptrVal.kind == DecodedValue::Kind::ModuleFunc) {
                    outIR.ssaValues[currentSsa] = IRValue::MakeLiteral(0.0f, currentSsa);
                    currentSsa++;
                } else if (ptrVal.kind == DecodedValue::Kind::SSA) {
                    auto itPtr = outIR.localPointers.find(ptrVal.index);
                    if (itPtr == outIR.localPointers.end() || !itPtr->second.isValid) {
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Load from non-local, escaping, or unresolvable pointer is unsupported.";
                        return false;
                    }

                    uint32_t slotIdx = itPtr->second.slotIndex;
                    uint32_t subSlot = itPtr->second.subSlotIndex;
                    if (slotIdx >= outIR.localAllocs.size()) {
                        outStatus = DXILTranslationStatus::MalformedBitcode;
                        outDiagnostic = "Load target slot out of bounds.";
                        return false;
                    }

                    const auto& alloc = outIR.localAllocs[slotIdx];
                    if (subSlot >= alloc.numElements) {
                        outStatus = DXILTranslationStatus::MalformedBitcode;
                        outDiagnostic = "Load target sub-slot " + std::to_string(subSlot) + 
                                        " out of bounds for allocation of size " + 
                                        std::to_string(alloc.numElements) + ".";
                        return false;
                    }

                    const auto& elem = alloc.elements[subSlot];
                    if (!elem.isInitialized) {
                        // In LLVM IR, reading from uninitialized storage is valid bitcode (yielding undef),
                        // but loading uninitialized storage is unsupported in the bounded translator.
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Load from uninitialized function-local storage location (slot " + 
                                        std::to_string(slotIdx) + ", sub-slot " + std::to_string(subSlot) + ") is unsupported in bounded translator.";
                        return false;
                    }

                    // Read-after-write dataflow propagation
                    if (elem.isConstant) {
                        if (alloc.elemKind == LLVMTypeKind::Integer) {
                            outIR.ssaValues[currentSsa] = IRValue::MakeLiteralInt(elem.constInt, currentSsa);
                        } else {
                            outIR.ssaValues[currentSsa] = IRValue::MakeLiteral(elem.constFloat, currentSsa);
                        }
                    } else {
                        uint32_t srcSsa = resolveSsa(elem.currentSsa);
                        ssaAliases[currentSsa] = srcSsa;
                        auto itVal = outIR.ssaValues.find(srcSsa);
                        if (itVal != outIR.ssaValues.end()) {
                            outIR.ssaValues[currentSsa] = itVal->second;
                            outIR.ssaValues[currentSsa].ssaId = currentSsa;
                        } else {
                            outIR.ssaValues[currentSsa] = IRValue::MakeTemp(alloc.tempRegIndex, static_cast<uint8_t>(subSlot), currentSsa);
                        }
                    }

                    // Record load move in basic block
                    if (currentBlockIdx < outIR.basicBlocks.size()) {
                        IRVectorOp movOp;
                        movOp.opcode = IROpcode::Move;
                        movOp.isLocalMemOp = true;
                        movOp.isLocalStore = false;
                        movOp.localSlot = slotIdx;
                        movOp.localSubSlot = subSlot;
                        movOp.channelMask = (1u << subSlot);
                        movOp.destTempIndex = currentSsa;
                        outIR.basicBlocks[currentBlockIdx].instructions.push_back(movOp);
                    }
                    currentSsa++;
                } else {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Load from non-SSA pointer is unsupported.";
                    return false;
                }
            }
            else if (code == FUNC_CODE_INST_STOREATOMIC || code == FUNC_CODE_INST_LOADATOMIC) {
                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                outDiagnostic = "Atomic memory instruction is unsupported.";
                return false;
            }
            else if (code == FUNC_CODE_INST_CAST) {
                uint32_t currentValId = baseValId + currentSsa;
                if (ops.size() >= 3) {
                    uint32_t castOp = static_cast<uint32_t>(ops[2]);
                    // 8 = inttoptr, 9 = ptrtoint, 10 = bitcast
                    if (castOp == 8 || castOp == 9) {
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Pointer cast or ptr-to-int conversion is unsupported.";
                        return false;
                    }
                    if (castOp == 10) { // bitcast
                        DecodedValue srcVal = ResolveVal(ops[0], currentValId);
                        if (srcVal.kind == DecodedValue::Kind::SSA && 
                            outIR.localPointers.find(srcVal.index) != outIR.localPointers.end()) {
                            outStatus = DXILTranslationStatus::UnsupportedInstruction;
                            outDiagnostic = "Pointer bitcast conversion is unsupported.";
                            return false;
                        }
                    }
                }
                currentSsa++;
            }
            else if (code == FUNC_CODE_INST_CALL) {
                uint32_t currentValId = baseValId + currentSsa;
                if (ops.size() < 5) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Call instruction record too short (" + std::to_string(ops.size()) + " operands).";
                    return false;
                }

                uint32_t fnty = static_cast<uint32_t>(ops[2]);
                if (fnty >= outIR.typeTable.size()) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Invalid function type index " + std::to_string(fnty) +
                                    " (type table size is " + std::to_string(outIR.typeTable.size()) + ").";
                    return false;
                }

                const auto& fnType = outIR.typeTable[fnty];
                if (fnType.kind != LLVMTypeKind::Function) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Type at index " + std::to_string(fnty) + " is not a Function type.";
                    return false;
                }

                uint32_t retTyIdx = fnType.returnType;
                const auto& retType = (retTyIdx < outIR.typeTable.size()) ? outIR.typeTable[retTyIdx] : LLVMType();
                bool returnsVoid = (retType.kind == LLVMTypeKind::Void);

                // Audit escaping pointers: reject if any call argument is an SSA pointer to local storage
                for (size_t a = 4; a < ops.size(); ++a) {
                    DecodedValue argVal = ResolveVal(ops[a], currentValId);
                    if (argVal.kind == DecodedValue::Kind::SSA &&
                        outIR.localPointers.find(argVal.index) != outIR.localPointers.end())
                    {
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Escaping pointer to local storage in function call is unsupported.";
                        return false;
                    }
                }

                DecodedValue arg0 = ResolveVal(ops[4], currentValId);
                if (arg0.isConst && !arg0.isConstValid) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Invalid DXIL opcode constant referenced: " + arg0.errorDiagnostic;
                    return false;
                }
                int64_t dxilOpCode = arg0.isConst ? arg0.constInt : -1;

                // 1. CreateHandle (57) or CreateHandleFromBinding (163)
                if (dxilOpCode == static_cast<int32_t>(DXILOpCode::CreateHandle) ||
                    dxilOpCode == static_cast<int32_t>(DXILOpCode::CreateHandleFromBinding) ||
                    (retType.kind == LLVMTypeKind::Struct && retType.name.find("Handle") != std::string::npos))
                {
                    outIR.ssaValues[currentSsa] = IRValue::MakeHandle(currentSsa);
                    currentSsa++;
                }
                // 2. LoadInput (4)
                else if (dxilOpCode == static_cast<int32_t>(DXILOpCode::LoadInput) ||
                         (fnType.paramTypes.size() == 5 &&
                          (retType.kind == LLVMTypeKind::Float || retType.kind == LLVMTypeKind::Integer)))
                {
                    uint32_t rowIdx = 0;
                    uint8_t chan = static_cast<uint8_t>(currentSsa % 4);
                    if (ops.size() >= 7) {
                        DecodedValue rowVal = ResolveVal(ops[6], currentValId);
                        if (rowVal.isConst && !rowVal.isConstValid) {
                            outStatus = DXILTranslationStatus::MalformedBitcode;
                            outDiagnostic = "Invalid row constant: " + rowVal.errorDiagnostic;
                            return false;
                        }
                        if (rowVal.isConst) rowIdx = static_cast<uint32_t>(rowVal.constInt);
                    }
                    if (ops.size() >= 8) {
                        DecodedValue colVal = ResolveVal(ops[7], currentValId);
                        if (colVal.isConst && !colVal.isConstValid) {
                            outStatus = DXILTranslationStatus::MalformedBitcode;
                            outDiagnostic = "Invalid col constant: " + colVal.errorDiagnostic;
                            return false;
                        }
                        if (colVal.isConst) chan = static_cast<uint8_t>(colVal.constInt);
                    }
                    outIR.inputRegisterIndex = rowIdx;
                    outIR.hasInput = true;
                    outIR.ssaValues[currentSsa] = IRValue::MakeInput(rowIdx, chan, currentSsa);
                    currentSsa++;
                }
                // 3. CBufferLoad (58 with byte offset)
                else if (dxilOpCode == static_cast<int32_t>(DXILOpCode::CBufferLoad))
                {
                    uint32_t byteOffset = 0;
                    uint32_t alignment = 4;
                    if (ops.size() >= 7) {
                        DecodedValue byteOffVal = ResolveVal(ops[6], currentValId);
                        if (byteOffVal.isConst && !byteOffVal.isConstValid) {
                            outStatus = DXILTranslationStatus::MalformedBitcode;
                            outDiagnostic = "Invalid byte offset constant: " + byteOffVal.errorDiagnostic;
                            return false;
                        }
                        if (byteOffVal.isConst) byteOffset = static_cast<uint32_t>(byteOffVal.constInt);
                    }
                    if (ops.size() >= 8) {
                        DecodedValue alignVal = ResolveVal(ops[7], currentValId);
                        if (alignVal.isConst && !alignVal.isConstValid) {
                            outStatus = DXILTranslationStatus::MalformedBitcode;
                            outDiagnostic = "Invalid alignment constant: " + alignVal.errorDiagnostic;
                            return false;
                        }
                        if (alignVal.isConst) alignment = static_cast<uint32_t>(alignVal.constInt);
                    }

                    // Strict byte alignment checks
                    if (byteOffset % 4 != 0) {
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Unaligned constant buffer byte offset (" + std::to_string(byteOffset) +
                                        ") is not a multiple of 4 bytes. D3D11 cbuffer components must be 4-byte aligned.";
                        return false;
                    }
                    if (alignment > 0 && (byteOffset % alignment != 0)) {
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Constant buffer byte offset (" + std::to_string(byteOffset) +
                                        ") violates declared alignment of " + std::to_string(alignment) + " bytes.";
                        return false;
                    }

                    uint32_t cbRow = byteOffset / 16;
                    uint32_t chan = (byteOffset % 16) / 4;
                    if (cbRow >= 4096) {
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Constant buffer row (" + std::to_string(cbRow) +
                                        ") exceeds D3D11 hardware limit of 4096 rows.";
                        return false;
                    }

                    outIR.ssaValues[currentSsa] = IRValue::MakeCBuffer(0, cbRow, static_cast<uint8_t>(chan), currentSsa);
                    outIR.hasCBuffer = true;
                    if (cbRow > outIR.maxCBufferReg) {
                        outIR.maxCBufferReg = cbRow;
                    }
                    currentSsa++;
                }
                // 4. CBufferLoadLegacy (59 with row index)
                else if (dxilOpCode == static_cast<int32_t>(DXILOpCode::CBufferLoadLegacy) ||
                         (retType.kind == LLVMTypeKind::Struct && retType.name.find("CBufRet") != std::string::npos))
                {
                    uint32_t cbRow = 0;
                    if (ops.size() >= 7) {
                        DecodedValue rowVal = ResolveVal(ops[6], currentValId);
                        if (rowVal.isConst && !rowVal.isConstValid) {
                            outStatus = DXILTranslationStatus::MalformedBitcode;
                            outDiagnostic = "Invalid cbuffer row constant: " + rowVal.errorDiagnostic;
                            return false;
                        }
                        if (rowVal.isConst) cbRow = static_cast<uint32_t>(rowVal.constInt);
                    }
                    if (cbRow >= 4096) {
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Legacy constant buffer row (" + std::to_string(cbRow) +
                                        ") exceeds D3D11 hardware limit of 4096 rows.";
                        return false;
                    }
                    cbufferLoads[currentSsa] = { cbRow, 0, currentBlockIdx };
                    currentSsa++;
                }
                // 5. Standard Texture Sample (60)
                else if (dxilOpCode == static_cast<int32_t>(DXILOpCode::Sample) ||
                         (retType.kind == LLVMTypeKind::Struct && retType.name.find("ResRet") != std::string::npos && dxilOpCode == 60) ||
                         (ops.size() >= 14 && dxilOpCode == 60))
                {
                    outIR.hasTextureSample = true;
                    sampleLoads[currentSsa] = true;
                    currentSsa++;
                }
                // 6. Unsupported Texture Sampling Variants (61..65: SampleBias, SampleLevel, SampleGrad, SampleCmp, SampleCmpLevelZero)
                else if (dxilOpCode >= static_cast<int32_t>(DXILOpCode::SampleBias) &&
                         dxilOpCode <= static_cast<int32_t>(DXILOpCode::SampleCmpLevelZero))
                {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    const char* opName = GetDXILOpCodeName(static_cast<DXILOpCode>(dxilOpCode));
                    outDiagnostic = "Unsupported texture operation: " + std::string(opName) +
                                    " (DXIL OpCode " + std::to_string(dxilOpCode) +
                                    "). Only standard Sample (OpCode 60) is supported in bounded translator.";
                    return false;
                }
                // 7. Unsupported TextureLoad (66)
                else if (dxilOpCode == static_cast<int32_t>(DXILOpCode::TextureLoad))
                {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Unsupported texture operation: TextureLoad (DXIL OpCode 66). TextureLoad is not supported in straight-line vector translator.";
                    return false;
                }
                // 8. Other Unsupported Resource Operations (67..69: TextureStore, BufferLoad, BufferStore)
                else if (dxilOpCode >= static_cast<int32_t>(DXILOpCode::TextureStore) &&
                         dxilOpCode <= static_cast<int32_t>(DXILOpCode::BufferStore))
                {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    const char* opName = GetDXILOpCodeName(static_cast<DXILOpCode>(dxilOpCode));
                    outDiagnostic = "Unsupported resource operation: " + std::string(opName) +
                                    " (DXIL OpCode " + std::to_string(dxilOpCode) + ").";
                    return false;
                }
                // 9. Unhandled ResRet intrinsics
                else if (retType.kind == LLVMTypeKind::Struct && retType.name.find("ResRet") != std::string::npos)
                {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    const char* opName = GetDXILOpCodeName(static_cast<DXILOpCode>(dxilOpCode));
                    outDiagnostic = "Unsupported texture or resource operation: " + std::string(opName) +
                                    " (DXIL OpCode " + std::to_string(dxilOpCode) + ").";
                    return false;
                }
                // 10. StoreOutput (5)
                else if (dxilOpCode == static_cast<int32_t>(DXILOpCode::StoreOutput) || (returnsVoid && fnType.paramTypes.size() == 5)) {
                    uint32_t chan = storeOutputCount;
                    if (ops.size() >= 8) {
                        DecodedValue colVal = ResolveVal(ops[7], currentValId);
                        if (colVal.isConst) chan = static_cast<uint32_t>(colVal.constInt);
                    }
                    if (ops.size() >= 9) {
                        DecodedValue valVal = ResolveVal(ops[8], currentValId);
                        if (chan < 4 && valVal.kind == DecodedValue::Kind::SSA) {
                            storeOutputSsa[chan] = resolveSsa(valVal.index);
                            storeOutputCount++;
                        }
                    } else if (!ops.empty()) {
                        DecodedValue valVal = ResolveVal(ops.back(), currentValId);
                        if (chan < 4 && valVal.kind == DecodedValue::Kind::SSA) {
                            storeOutputSsa[chan] = resolveSsa(valVal.index);
                            storeOutputCount++;
                        }
                    }
                    // Void return does not advance SSA counter
                }
                else {
                    if (!returnsVoid) {
                        currentSsa++;
                    }
                }
            }
            else if (code == FUNC_CODE_INST_RET) {
                uint32_t currentValId = baseValId + currentSsa;
                if (!ops.empty()) {
                    DecodedValue retVal = ResolveVal(ops[0], currentValId);
                    if (retVal.kind == DecodedValue::Kind::SSA &&
                        outIR.localPointers.find(retVal.index) != outIR.localPointers.end())
                    {
                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                        outDiagnostic = "Escaping pointer to local storage in return instruction is unsupported.";
                        return false;
                    }
                }
                if (currentBlockIdx < outIR.basicBlocks.size()) {
                    outIR.basicBlocks[currentBlockIdx].terminator.kind = BlockTerminatorKind::Return;
                    outIR.basicBlocks[currentBlockIdx].hasTerminator = true;
                }
                currentBlockIdx++;
            }
            else {
                currentSsa++;
            }
        }

        // Ensure at least 1 basic block if numBasicBlocks is 0 or 1
        if (outIR.basicBlocks.empty()) {
            outIR.numBasicBlocks = 1;
            outIR.basicBlocks.resize(1);
            outIR.basicBlocks[0].id = 0;
            outIR.basicBlocks[0].terminator.kind = BlockTerminatorKind::Return;
            outIR.basicBlocks[0].hasTerminator = true;
        }

        if (outIR.hasControlFlow) {
            if (!ValidateCFG(outIR, outStatus, outDiagnostic)) {
                return false;
            }

            if (outIR.hasLoop) {
                // Populate loop descriptor
                uint32_t hdr = outIR.loop.headerBlock;
                uint32_t latch = outIR.loop.latchBlock;
                uint32_t preheader = outIR.loop.preheaderBlock;
                uint32_t exitBlk = outIR.loop.exitBlock;

                // Find induction variable and accumulator PHIs in header
                for (const auto& phi : outIR.basicBlocks[hdr].phiNodes) {
                    bool isInteger = false;
                    if (phi.typeId < outIR.typeTable.size()) {
                        isInteger = (outIR.typeTable[phi.typeId].kind == LLVMTypeKind::Integer);
                    }
                    if (isInteger || phi.typeId == 4) { // induction variable
                        outIR.loop.inductionPhiSsa = phi.destSsa;
                        for (const auto& in : phi.incoming) {
                            if (in.predecessorBlock == preheader) {
                                if (in.isConstant) {
                                    outIR.loop.inductionInitialValue = static_cast<int32_t>(in.constantInt);
                                } else {
                                    outIR.loop.inductionInitialValue = 0;
                                }
                            } else if (in.predecessorBlock == latch) {
                                // Check induction step
                                auto itBin = rawBinops.find(in.incomingSsa);
                                if (itBin != rawBinops.end()) {
                                    if (itBin->second.opcode == 0) { // iadd
                                        if (itBin->second.isConst1) {
                                            outIR.loop.inductionStep = static_cast<int32_t>(itBin->second.constInt1);
                                        } else if (itBin->second.isConst0) {
                                            outIR.loop.inductionStep = static_cast<int32_t>(itBin->second.constInt0);
                                        } else {
                                            outIR.loop.inductionStep = 1;
                                        }
                                    } else if (itBin->second.opcode == 1) { // isub
                                        if (itBin->second.isConst1) {
                                            outIR.loop.inductionStep = -static_cast<int32_t>(itBin->second.constInt1);
                                        } else {
                                            outIR.loop.inductionStep = -1;
                                        }
                                    } else {
                                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                        outDiagnostic = "Unsupported loop induction update: bounded loop lowering only supports addition/subtraction.";
                                        return false;
                                    }
                                }
                            }
                        }
                    } else { // accumulator component
                        if (outIR.loop.accumulatorPhiSsa == 0) {
                            outIR.loop.accumulatorPhiSsa = phi.destSsa;
                        }
                    }
                }

                // Verify induction step: bounded lowering strictly supports canonical unit-step (+1)
                if (outIR.loop.inductionStep != 1) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Unsupported loop induction step: bounded loop lowering currently only supports canonical unit-step (+1) induction.";
                    return false;
                }

                // Check condition in exitingBlock (or latch if exitingBlock not set)
                uint32_t exitCondBlock = outIR.loop.exitingBlock != 0 ? outIR.loop.exitingBlock : latch;
                if (!outIR.basicBlocks[exitCondBlock].hasTerminator ||
                    outIR.basicBlocks[exitCondBlock].terminator.kind != BlockTerminatorKind::ConditionalBranch) {
                    outStatus = DXILTranslationStatus::MalformedBitcode;
                    outDiagnostic = "Loop exiting block lacks conditional branch terminator.";
                    return false;
                }
                uint32_t condSsa = outIR.basicBlocks[exitCondBlock].terminator.conditionSsa;
                auto itCmp = outIR.comparisons.find(condSsa);
                if (itCmp == outIR.comparisons.end()) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Unsupported loop condition: condition must be an integer comparison.";
                    return false;
                }

                auto cmpInst = itCmp->second;
                auto pred = cmpInst.predicate;
                if (!IsIntegerPredicate(pred)) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Unsupported loop comparison predicate: bounded loop lowering only supports signed and unsigned integer comparisons.";
                    return false;
                }

                // If src1 is the induction variable and src0 is the bound, normalize operands
                bool src0IsInduction = (cmpInst.src0.type == IRValueType::IntermediateTemp && cmpInst.src0.ssaId == outIR.loop.inductionPhiSsa) ||
                                       (cmpInst.isConst1 || cmpInst.src1.type == IRValueType::CBufferComponent);
                if (!src0IsInduction) {
                    switch (pred) {
                        case ComparePredicate::IntSignedLessThan: pred = ComparePredicate::IntSignedGreaterThan; break;
                        case ComparePredicate::IntSignedLessEqual: pred = ComparePredicate::IntSignedGreaterEqual; break;
                        case ComparePredicate::IntSignedGreaterThan: pred = ComparePredicate::IntSignedLessThan; break;
                        case ComparePredicate::IntSignedGreaterEqual: pred = ComparePredicate::IntSignedLessEqual; break;
                        case ComparePredicate::IntUnsignedLessThan: pred = ComparePredicate::IntUnsignedGreaterThan; break;
                        case ComparePredicate::IntUnsignedLessEqual: pred = ComparePredicate::IntUnsignedGreaterEqual; break;
                        case ComparePredicate::IntUnsignedGreaterThan: pred = ComparePredicate::IntUnsignedLessThan; break;
                        case ComparePredicate::IntUnsignedGreaterEqual: pred = ComparePredicate::IntUnsignedLessEqual; break;
                        default: break;
                    }
                    std::swap(cmpInst.src0, cmpInst.src1);
                    std::swap(cmpInst.isConst0, cmpInst.isConst1);
                    std::swap(cmpInst.constInt0, cmpInst.constInt1);
                }

                outIR.loop.conditionPredicate = pred;

                // Extract bound from src1
                if (cmpInst.isConst1) {
                    outIR.loop.isDynamicBound = false;
                    outIR.loop.constantBound = static_cast<int32_t>(cmpInst.constInt1);
                } else if (cmpInst.src1.type == IRValueType::CBufferComponent) {
                    outIR.loop.isDynamicBound = true;
                    outIR.loop.boundCBufferReg = cmpInst.src1.regIndex;
                    outIR.loop.boundCBufferChannel = cmpInst.src1.channel;
                    if (outIR.loop.boundCBufferReg > outIR.maxCBufferReg) outIR.maxCBufferReg = outIR.loop.boundCBufferReg;
                    outIR.hasCBuffer = true;
                } else if (cmpInst.src1.type == IRValueType::LiteralFloat || cmpInst.src0.type == IRValueType::LiteralFloat) {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Type mismatch: floating-point literal used in integer loop bound comparison without explicit conversion.";
                    return false;
                } else {
                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                    outDiagnostic = "Unsupported loop bound operand: bounded loop lowering requires a constant integer or constant buffer component.";
                    return false;
                }

                bool exitOnConditionTrue = (outIR.basicBlocks[exitCondBlock].terminator.trueBlock == exitBlk);
                outIR.loop.exitOnConditionTrue = exitOnConditionTrue;

                // Determine signedness without numerical guesswork
                bool isUnsigned = false;
                bool isSigned = false;
                if (pred == ComparePredicate::IntUnsignedLessThan ||
                    pred == ComparePredicate::IntUnsignedLessEqual ||
                    pred == ComparePredicate::IntUnsignedGreaterThan ||
                    pred == ComparePredicate::IntUnsignedGreaterEqual) {
                    isUnsigned = true;
                } else if (pred == ComparePredicate::IntSignedLessThan ||
                           pred == ComparePredicate::IntSignedLessEqual ||
                           pred == ComparePredicate::IntSignedGreaterThan ||
                           pred == ComparePredicate::IntSignedGreaterEqual) {
                    isSigned = true;
                } else if (pred == ComparePredicate::IntEqual || pred == ComparePredicate::IntNotEqual) {
                    if (!outIR.loop.isDynamicBound) {
                        bool hasNsw = false;
                        bool hasNuw = false;
                        for (const auto& phi : outIR.basicBlocks[hdr].phiNodes) {
                            if (phi.destSsa == outIR.loop.inductionPhiSsa) {
                                for (const auto& inPhi : phi.incoming) {
                                    if (inPhi.predecessorBlock == latch) {
                                        auto itB = rawBinops.find(inPhi.incomingSsa);
                                        if (itB != rawBinops.end() && itB->second.opcode == 0) { // iadd
                                            if (itB->second.flags & 2) hasNsw = true;
                                            if (itB->second.flags & 1) hasNuw = true;
                                        }
                                    }
                                }
                            }
                        }
                        if (hasNuw && !hasNsw) {
                            isUnsigned = true;
                            isSigned = false;
                        } else if (hasNsw && !hasNuw) {
                            isSigned = true;
                            isUnsigned = false;
                        } else if (outIR.loop.inductionInitialValue < 0 || outIR.loop.constantBound < 0) {
                            isSigned = true;
                            isUnsigned = false;
                        } else {
                            isSigned = true;
                            isUnsigned = false;
                        }
                    } else {
                        isSigned = true;
                    }
                }

                outIR.loop.isUnsignedPredicate = isUnsigned;

                if (exitOnConditionTrue) {
                    switch (pred) {
                        case ComparePredicate::IntSignedGreaterEqual:
                            outIR.loop.breakCmpOpcode = LoopCmpOpcode::IGE;
                            break;
                        case ComparePredicate::IntSignedLessThan:
                            outIR.loop.breakCmpOpcode = LoopCmpOpcode::ILT;
                            break;
                        case ComparePredicate::IntUnsignedGreaterEqual:
                            outIR.loop.breakCmpOpcode = LoopCmpOpcode::UGE;
                            break;
                        case ComparePredicate::IntUnsignedLessThan:
                            outIR.loop.breakCmpOpcode = LoopCmpOpcode::ULT;
                            break;
                        case ComparePredicate::IntSignedGreaterThan:
                            if (!outIR.loop.isDynamicBound) {
                                if (outIR.loop.constantBound == 0x7FFFFFFF) {
                                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                    outDiagnostic = "Integer bound overflow: signed loop bound 0x7FFFFFFF cannot be incremented during normalization.";
                                    return false;
                                }
                                outIR.loop.constantBound += 1;
                                outIR.loop.breakCmpOpcode = LoopCmpOpcode::IGE;
                            } else {
                                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                outDiagnostic = "Unsupported dynamic relational loop comparison: dynamic greater-than exit condition requires normalization not supported in bounded lowering.";
                                return false;
                            }
                            break;
                        case ComparePredicate::IntSignedLessEqual:
                            if (!outIR.loop.isDynamicBound) {
                                if (outIR.loop.constantBound == 0x7FFFFFFF) {
                                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                    outDiagnostic = "Integer bound overflow: signed loop bound 0x7FFFFFFF cannot be incremented during normalization.";
                                    return false;
                                }
                                outIR.loop.constantBound += 1;
                                outIR.loop.breakCmpOpcode = LoopCmpOpcode::ILT;
                            } else {
                                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                outDiagnostic = "Unsupported dynamic relational loop comparison: dynamic less-or-equal exit condition requires normalization not supported in bounded lowering.";
                                return false;
                            }
                            break;
                        case ComparePredicate::IntUnsignedGreaterThan:
                            if (!outIR.loop.isDynamicBound) {
                                if (static_cast<uint32_t>(outIR.loop.constantBound) == 0xFFFFFFFFu) {
                                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                    outDiagnostic = "Integer bound overflow: unsigned loop bound 0xFFFFFFFF cannot be incremented during normalization.";
                                    return false;
                                }
                                outIR.loop.constantBound += 1;
                                outIR.loop.breakCmpOpcode = LoopCmpOpcode::UGE;
                            } else {
                                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                outDiagnostic = "Unsupported dynamic relational loop comparison: dynamic greater-than exit condition requires normalization not supported in bounded lowering.";
                                return false;
                            }
                            break;
                        case ComparePredicate::IntUnsignedLessEqual:
                            if (!outIR.loop.isDynamicBound) {
                                if (static_cast<uint32_t>(outIR.loop.constantBound) == 0xFFFFFFFFu) {
                                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                    outDiagnostic = "Integer bound overflow: unsigned loop bound 0xFFFFFFFF cannot be incremented during normalization.";
                                    return false;
                                }
                                outIR.loop.constantBound += 1;
                                outIR.loop.breakCmpOpcode = LoopCmpOpcode::ULT;
                            } else {
                                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                outDiagnostic = "Unsupported dynamic relational loop comparison: dynamic less-or-equal exit condition requires normalization not supported in bounded lowering.";
                                return false;
                            }
                            break;
                        case ComparePredicate::IntEqual:
                            if (!outIR.loop.isDynamicBound) {
                                if (isUnsigned) {
                                    if (static_cast<uint32_t>(outIR.loop.inductionInitialValue) > static_cast<uint32_t>(outIR.loop.constantBound)) {
                                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                        outDiagnostic = "Unsupported loop: increasing == exit loop with initial value exceeding bound cannot reach termination bound under unit-step without wrap-around.";
                                        return false;
                                    }
                                } else {
                                    if (outIR.loop.inductionInitialValue > outIR.loop.constantBound) {
                                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                        outDiagnostic = "Unsupported loop: increasing == exit loop with initial value exceeding bound cannot reach termination bound under unit-step without wrap-around.";
                                        return false;
                                    }
                                }
                                outIR.loop.breakCmpOpcode = isUnsigned ? LoopCmpOpcode::UGE : LoopCmpOpcode::IGE;
                            } else {
                                outIR.loop.breakCmpOpcode = LoopCmpOpcode::IEQ;
                            }
                            break;
                        case ComparePredicate::IntNotEqual:
                            outIR.loop.breakCmpOpcode = LoopCmpOpcode::INE;
                            break;
                        default:
                            outStatus = DXILTranslationStatus::UnsupportedInstruction;
                            outDiagnostic = "Unsupported loop comparison predicate.";
                            return false;
                    }
                } else {
                    switch (pred) {
                        case ComparePredicate::IntSignedLessThan:
                            outIR.loop.breakCmpOpcode = LoopCmpOpcode::IGE;
                            break;
                        case ComparePredicate::IntSignedGreaterEqual:
                            outIR.loop.breakCmpOpcode = LoopCmpOpcode::ILT;
                            break;
                        case ComparePredicate::IntUnsignedLessThan:
                            outIR.loop.breakCmpOpcode = LoopCmpOpcode::UGE;
                            break;
                        case ComparePredicate::IntUnsignedGreaterEqual:
                            outIR.loop.breakCmpOpcode = LoopCmpOpcode::ULT;
                            break;
                        case ComparePredicate::IntSignedLessEqual:
                            if (!outIR.loop.isDynamicBound) {
                                if (outIR.loop.constantBound == 0x7FFFFFFF) {
                                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                    outDiagnostic = "Integer bound overflow: signed loop bound 0x7FFFFFFF cannot be incremented during normalization.";
                                    return false;
                                }
                                outIR.loop.constantBound += 1;
                                outIR.loop.breakCmpOpcode = LoopCmpOpcode::IGE;
                            } else {
                                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                outDiagnostic = "Unsupported dynamic relational loop comparison: dynamic less-or-equal continue condition requires normalization not supported in bounded lowering.";
                                return false;
                            }
                            break;
                        case ComparePredicate::IntSignedGreaterThan:
                            if (!outIR.loop.isDynamicBound) {
                                if (outIR.loop.constantBound == 0x7FFFFFFF) {
                                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                    outDiagnostic = "Integer bound overflow: signed loop bound 0x7FFFFFFF cannot be incremented during normalization.";
                                    return false;
                                }
                                outIR.loop.constantBound += 1;
                                outIR.loop.breakCmpOpcode = LoopCmpOpcode::ILT;
                            } else {
                                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                outDiagnostic = "Unsupported dynamic relational loop comparison: dynamic greater-than continue condition requires normalization not supported in bounded lowering.";
                                return false;
                            }
                            break;
                        case ComparePredicate::IntUnsignedLessEqual:
                            if (!outIR.loop.isDynamicBound) {
                                if (static_cast<uint32_t>(outIR.loop.constantBound) == 0xFFFFFFFFu) {
                                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                    outDiagnostic = "Integer bound overflow: unsigned loop bound 0xFFFFFFFF cannot be incremented during normalization.";
                                    return false;
                                }
                                outIR.loop.constantBound += 1;
                                outIR.loop.breakCmpOpcode = LoopCmpOpcode::UGE;
                            } else {
                                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                outDiagnostic = "Unsupported dynamic relational loop comparison: dynamic less-or-equal continue condition requires normalization not supported in bounded lowering.";
                                return false;
                            }
                            break;
                        case ComparePredicate::IntUnsignedGreaterThan:
                            if (!outIR.loop.isDynamicBound) {
                                if (static_cast<uint32_t>(outIR.loop.constantBound) == 0xFFFFFFFFu) {
                                    outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                    outDiagnostic = "Integer bound overflow: unsigned loop bound 0xFFFFFFFF cannot be incremented during normalization.";
                                    return false;
                                }
                                outIR.loop.constantBound += 1;
                                outIR.loop.breakCmpOpcode = LoopCmpOpcode::ULT;
                            } else {
                                outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                outDiagnostic = "Unsupported dynamic relational loop comparison: dynamic greater-than continue condition requires normalization not supported in bounded lowering.";
                                return false;
                            }
                            break;
                        case ComparePredicate::IntEqual:
                            outIR.loop.breakCmpOpcode = LoopCmpOpcode::INE;
                            break;
                        case ComparePredicate::IntNotEqual:
                            if (!outIR.loop.isDynamicBound) {
                                if (isUnsigned) {
                                    if (static_cast<uint32_t>(outIR.loop.inductionInitialValue) > static_cast<uint32_t>(outIR.loop.constantBound)) {
                                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                        outDiagnostic = "Unsupported loop: increasing != loop with initial value exceeding bound cannot reach termination bound under unit-step without wrap-around.";
                                        return false;
                                    }
                                } else {
                                    if (outIR.loop.inductionInitialValue > outIR.loop.constantBound) {
                                        outStatus = DXILTranslationStatus::UnsupportedInstruction;
                                        outDiagnostic = "Unsupported loop: increasing != loop with initial value exceeding bound cannot reach termination bound under unit-step without wrap-around.";
                                        return false;
                                    }
                                }
                                outIR.loop.breakCmpOpcode = isUnsigned ? LoopCmpOpcode::UGE : LoopCmpOpcode::IGE;
                            } else {
                                outIR.loop.breakCmpOpcode = LoopCmpOpcode::IEQ;
                            }
                            break;
                        default:
                            outStatus = DXILTranslationStatus::UnsupportedInstruction;
                            outDiagnostic = "Unsupported loop comparison predicate.";
                            return false;
                    }
                }

                // Verify termination reachability for < break comparisons under unit-step +1
                if (!outIR.loop.isDynamicBound) {
                    if (outIR.loop.breakCmpOpcode == LoopCmpOpcode::ILT) {
                        if (outIR.loop.inductionInitialValue >= outIR.loop.constantBound) {
                            outStatus = DXILTranslationStatus::UnsupportedInstruction;
                            outDiagnostic = "Unsupported loop: increasing loop with < break condition where initial value >= bound cannot reach termination bound under unit-step without wrap-around.";
                            return false;
                        }
                    } else if (outIR.loop.breakCmpOpcode == LoopCmpOpcode::ULT) {
                        if (static_cast<uint32_t>(outIR.loop.inductionInitialValue) >= static_cast<uint32_t>(outIR.loop.constantBound)) {
                            outStatus = DXILTranslationStatus::UnsupportedInstruction;
                            outDiagnostic = "Unsupported loop: increasing loop with < break condition where initial value >= bound cannot reach termination bound under unit-step without wrap-around.";
                            return false;
                        }
                    }
                }

                // Inner branch
                if (outIR.loop.loopBlocks.size() > 2 && outIR.basicBlocks[hdr].terminator.kind == BlockTerminatorKind::ConditionalBranch) {
                    outIR.loop.hasInnerBranch = true;
                    uint32_t innerCondSsa = outIR.basicBlocks[hdr].terminator.conditionSsa;
                    auto itInnerCmp = outIR.comparisons.find(innerCondSsa);
                    if (itInnerCmp != outIR.comparisons.end()) {
                        outIR.loop.innerBranchPredicate = itInnerCmp->second.predicate;
                        outIR.loop.innerBranchLiteral = itInnerCmp->second.src1.literalValue;
                    }
                    uint32_t trueBlk = outIR.basicBlocks[hdr].terminator.trueBlock;
                    uint32_t falseBlk = outIR.basicBlocks[hdr].terminator.falseBlock;
                    for (const auto& cb : cbufferLoads) {
                        if (cb.second.blockIdx == trueBlk) {
                            outIR.loop.innerTrueCBufferReg = cb.second.row;
                        }
                        if (cb.second.blockIdx == falseBlk) {
                            outIR.loop.innerFalseCBufferReg = cb.second.row;
                        }
                    }
                    if (outIR.loop.innerTrueCBufferReg > outIR.maxCBufferReg) outIR.maxCBufferReg = outIR.loop.innerTrueCBufferReg;
                    if (outIR.loop.innerFalseCBufferReg > outIR.maxCBufferReg) outIR.maxCBufferReg = outIR.loop.innerFalseCBufferReg;
                    outIR.hasCBuffer = true;
                }

                // Body operations
                uint32_t bodyCbRow = 0;
                for (const auto& cb : cbufferLoads) {
                    if (outIR.loop.loopBlocks.find(cb.second.blockIdx) != outIR.loop.loopBlocks.end()) {
                        bodyCbRow = cb.second.row;
                        break;
                    }
                }
                IROpcode bodyOpCode = IROpcode::Add;
                for (const auto& pair : rawBinops) {
                    if (outIR.loop.loopBlocks.find(pair.second.blockIdx) != outIR.loop.loopBlocks.end()) {
                        if (pair.second.opcode == 2 || pair.second.opcode == 10) {
                            bodyOpCode = IROpcode::Multiply;
                            break;
                        }
                    }
                }
                IRVectorOp bodyOp;
                bodyOp.opcode = bodyOpCode;
                bodyOp.cbufferReg = bodyCbRow;
                bodyOp.hasCBuffer = true;
                if (bodyCbRow > outIR.maxCBufferReg) outIR.maxCBufferReg = bodyCbRow;
                outIR.hasCBuffer = true;
                outIR.loop.bodyInstructions.push_back(bodyOp);

                // Exit operations
                for (const auto& pair : rawBinops) {
                    if (pair.second.blockIdx == exitBlk) {
                        IRVectorOp exitOp;
                        if (pair.second.opcode == 2 || pair.second.opcode == 10) exitOp.opcode = IROpcode::Multiply;
                        else exitOp.opcode = IROpcode::Add;
                        for (const auto& cb : cbufferLoads) {
                            if (cb.second.blockIdx == exitBlk) {
                                exitOp.cbufferReg = cb.second.row;
                                exitOp.hasCBuffer = true;
                                if (cb.second.row > outIR.maxCBufferReg) outIR.maxCBufferReg = cb.second.row;
                                outIR.hasCBuffer = true;
                                break;
                            }
                        }
                        outIR.loop.exitInstructions.push_back(exitOp);
                        break;
                    }
                }

                outIR.numTempsNeeded = 4;
            } else {
                // Determine compound CFG roles
                uint32_t finalMerge = (outIR.compoundCFG.finalMergeBlock != 0) ? outIR.compoundCFG.finalMergeBlock :
                                      (outIR.hasSwitch ? outIR.switchDesc.mergeBlock :
                                      (outIR.numBasicBlocks >= 4 ? 3 : (outIR.numBasicBlocks - 1)));

                if (outIR.hasSwitch) {
                    auto extIt = rawExtracts.find(outIR.switchDesc.selectorSsa);
                    if (extIt != rawExtracts.end()) {
                        uint32_t aggSsa = extIt->second.aggSsa;
                        uint32_t chan = extIt->second.channel;
                        auto cbIt = cbufferLoads.find(aggSsa);
                        if (cbIt != cbufferLoads.end()) {
                            outIR.switchDesc.selectorIsCBuffer = true;
                            outIR.switchDesc.selectorCbSlot = cbIt->second.slot;
                            outIR.switchDesc.selectorCbRow = cbIt->second.row;
                            outIR.switchDesc.selectorCbChannel = static_cast<uint8_t>(chan);
                            if (cbIt->second.row > outIR.maxCBufferReg) {
                                outIR.maxCBufferReg = cbIt->second.row;
                            }
                            outIR.hasCBuffer = true;
                        }
                    } else {
                        // Check if selector originates from a PHI node at entry of switch
                        auto ssaIt = outIR.ssaValues.find(outIR.switchDesc.selectorSsa);
                        if (ssaIt != outIR.ssaValues.end() && ssaIt->second.type == IRValueType::IntermediateTemp) {
                            outIR.switchDesc.selectorIsCBuffer = false;
                            outIR.switchDesc.selectorIsPhi = true;
                            outIR.switchDesc.selectorTempReg = 0;
                        }
                    }
                }

                // Lower block vector operations
                for (uint32_t b = 0; b < outIR.numBasicBlocks; ++b) {
                    bool isFinalMerge = (b == finalMerge);
                    uint32_t curDestTemp = 1;
                    if (outIR.compoundCFG.pattern == CFGPatternKind::IfElseThenSwitch) {
                        if (b == outIR.compoundCFG.firstRegion.trueBlock || b == outIR.compoundCFG.firstRegion.falseBlock) {
                            curDestTemp = 1;
                        } else if (b == outIR.compoundCFG.firstRegion.mergeBlock) {
                            curDestTemp = 1;
                        } else if (b != finalMerge) {
                            curDestTemp = 2; // Switch cases write to r2
                        } else {
                            curDestTemp = 2;
                        }
                    } else if (outIR.compoundCFG.pattern == CFGPatternKind::SwitchThenIfElse) {
                        if (b == outIR.compoundCFG.firstRegion.mergeBlock) {
                            curDestTemp = 1;
                        } else if (b == outIR.compoundCFG.secondRegion.trueBlock || b == outIR.compoundCFG.secondRegion.falseBlock) {
                            curDestTemp = 2; // If/else branches write to r2
                        } else if (b != finalMerge) {
                            curDestTemp = 1; // Switch cases write to r1
                        } else {
                            curDestTemp = 2;
                        }
                    }

                    std::vector<uint32_t> blockSsas;
                    for (const auto& pair : rawBinops) {
                        if (pair.second.blockIdx == b) {
                            blockSsas.push_back(pair.first);
                        }
                    }

                    bool isBranchFeedingPhiSelector = (outIR.switchDesc.selectorIsPhi &&
                        outIR.compoundCFG.pattern == CFGPatternKind::IfElseThenSwitch &&
                        (b == outIR.compoundCFG.firstRegion.trueBlock || b == outIR.compoundCFG.firstRegion.falseBlock));

                    if (!blockSsas.empty() && !isBranchFeedingPhiSelector) {
                        size_t numGroups = (blockSsas.size() + 3) / 4;
                        for (size_t g = 0; g < numGroups; ++g) {
                            uint32_t repSsa = blockSsas[g * 4];
                            const auto& raw = rawBinops[repSsa];

                            IRVectorOp vOp;
                            if (raw.opcode == 0) vOp.opcode = IROpcode::Add;
                            else if (raw.opcode == 1) vOp.opcode = IROpcode::Subtract;
                            else if (raw.opcode == 2) vOp.opcode = IROpcode::Multiply;
                            else vOp.opcode = IROpcode::None;

                            uint32_t s0 = resolveSsa(raw.src0Ssa);
                            uint32_t s1 = resolveSsa(raw.src1Ssa);
                            auto itSrc0 = outIR.ssaValues.find(s0);
                            auto itSrc1 = outIR.ssaValues.find(s1);
                            bool src0IsCB = (itSrc0 != outIR.ssaValues.end() && itSrc0->second.type == IRValueType::CBufferComponent);
                            bool src1IsCB = (itSrc1 != outIR.ssaValues.end() && itSrc1->second.type == IRValueType::CBufferComponent);

                            if (src0IsCB && src1IsCB) {
                                uint32_t cbReg0 = itSrc0->second.regIndex;
                                uint32_t cbReg1 = itSrc1->second.regIndex;

                                // First move src0 into curDestTemp
                                IRVectorOp movOp;
                                movOp.opcode = IROpcode::Move;
                                movOp.hasCBuffer = true;
                                movOp.cbufferReg = cbReg0;
                                movOp.destTempIndex = curDestTemp;
                                if (cbReg0 > outIR.maxCBufferReg) outIR.maxCBufferReg = cbReg0;
                                outIR.hasCBuffer = true;
                                outIR.basicBlocks[b].instructions.push_back(movOp);

                                // Then execute binary operation with src1 as cbuffer operand
                                vOp.hasCBuffer = true;
                                vOp.cbufferReg = cbReg1;
                                vOp.destTempIndex = curDestTemp;
                                vOp.isReverseSubtract = false;
                                if (cbReg1 > outIR.maxCBufferReg) outIR.maxCBufferReg = cbReg1;
                                outIR.hasCBuffer = true;
                                if (isFinalMerge && (g == numGroups - 1)) {
                                    vOp.destIsOutput = true;
                                }
                                outIR.basicBlocks[b].instructions.push_back(vOp);
                            } else {
                                if (vOp.opcode == IROpcode::Subtract) {
                                    if (src0IsCB) {
                                        vOp.isReverseSubtract = true;
                                    }
                                }

                                if (src0IsCB) {
                                    vOp.cbufferReg = itSrc0->second.regIndex;
                                    vOp.hasCBuffer = true;
                                } else if (src1IsCB) {
                                    vOp.cbufferReg = itSrc1->second.regIndex;
                                    vOp.hasCBuffer = true;
                                }

                                if (vOp.hasCBuffer && vOp.cbufferReg > outIR.maxCBufferReg) {
                                    outIR.maxCBufferReg = vOp.cbufferReg;
                                    outIR.hasCBuffer = true;
                                }

                                vOp.destTempIndex = curDestTemp;
                                if (isFinalMerge && (g == numGroups - 1)) {
                                    vOp.destIsOutput = true;
                                }

                                outIR.basicBlocks[b].instructions.push_back(vOp);
                            }
                        }
                    } else {
                        // Check if this block feeds a successor merge block via PHI nodes
                        bool handledByPhi = false;
                        uint32_t tgtMerge = UINT32_MAX;
                        if (outIR.basicBlocks[b].terminator.kind == BlockTerminatorKind::UnconditionalBranch) {
                            tgtMerge = outIR.basicBlocks[b].terminator.targetBlock;
                        } else if (outIR.hasSwitch && b != outIR.switchDesc.mergeBlock) {
                            tgtMerge = outIR.switchDesc.mergeBlock;
                        }

                        if (tgtMerge < outIR.numBasicBlocks && !outIR.basicBlocks[tgtMerge].phiNodes.empty() && b != tgtMerge) {
                            uint32_t phiDestTemp = 1;
                            if (outIR.compoundCFG.pattern == CFGPatternKind::IfElseThenSwitch) {
                                phiDestTemp = (tgtMerge == outIR.compoundCFG.firstRegion.mergeBlock) ? 1 : 2;
                            } else if (outIR.compoundCFG.pattern == CFGPatternKind::SwitchThenIfElse) {
                                phiDestTemp = (tgtMerge == outIR.compoundCFG.firstRegion.mergeBlock) ? 1 : 2;
                            }

                            const auto& phiList = outIR.basicBlocks[tgtMerge].phiNodes;
                            if (phiList.size() >= 4) {
                                uint32_t rows[4] = { 0 };
                                uint8_t chans[4] = { 0 };
                                bool allFound = true;
                                for (uint32_t c = 0; c < 4; ++c) {
                                    bool foundInPhi = false;
                                    for (const auto& in : phiList[c].incoming) {
                                        if (in.predecessorBlock == b) {
                                            auto itSsa = outIR.ssaValues.find(in.incomingSsa);
                                            if (itSsa != outIR.ssaValues.end() && itSsa->second.type == IRValueType::CBufferComponent) {
                                                rows[c] = itSsa->second.regIndex;
                                                chans[c] = static_cast<uint8_t>(itSsa->second.channel);
                                                foundInPhi = true;
                                                break;
                                            }
                                        }
                                    }
                                    if (!foundInPhi) {
                                        allFound = false;
                                        break;
                                    }
                                }

                                if (allFound) {
                                    handledByPhi = true;
                                    bool sameRow = (rows[0] == rows[1] && rows[1] == rows[2] && rows[2] == rows[3]);
                                    bool identityChannels = (chans[0] == 0 && chans[1] == 1 && chans[2] == 2 && chans[3] == 3);
                                    if (sameRow && identityChannels) {
                                        IRVectorOp vOp;
                                        vOp.opcode = IROpcode::Move;
                                        vOp.hasCBuffer = true;
                                        vOp.cbufferReg = rows[0];
                                        vOp.destTempIndex = phiDestTemp;
                                        if (vOp.cbufferReg > outIR.maxCBufferReg) {
                                            outIR.maxCBufferReg = vOp.cbufferReg;
                                        }
                                        outIR.hasCBuffer = true;
                                        outIR.basicBlocks[b].instructions.push_back(vOp);
                                    } else {
                                        // Emit 4 component moves
                                        for (uint32_t c = 0; c < 4; ++c) {
                                            IRVectorOp compOp;
                                            compOp.opcode = IROpcode::Move;
                                            compOp.isComponentMove = true;
                                            compOp.hasCBuffer = true;
                                            compOp.channelMask = (1 << c);
                                            compOp.cbufferReg = rows[c];
                                            compOp.cbufferChannel = chans[c];
                                            compOp.destTempIndex = phiDestTemp;
                                            if (rows[c] > outIR.maxCBufferReg) {
                                                outIR.maxCBufferReg = rows[c];
                                            }
                                            outIR.hasCBuffer = true;
                                            outIR.basicBlocks[b].instructions.push_back(compOp);
                                        }
                                    }
                                }
                            }
                        }

                        if (!handledByPhi &&
                            outIR.basicBlocks[b].terminator.kind != BlockTerminatorKind::Switch &&
                            outIR.basicBlocks[b].terminator.kind != BlockTerminatorKind::ConditionalBranch) {
                            // Check if this block loaded a constant buffer directly
                            for (const auto& cb : cbufferLoads) {
                                if (cb.second.blockIdx == b) {
                                    IRVectorOp vOp;
                                    vOp.opcode = IROpcode::Move;
                                    vOp.hasCBuffer = true;
                                    vOp.cbufferReg = cb.second.row;
                                    vOp.destTempIndex = curDestTemp;
                                    if (vOp.cbufferReg > outIR.maxCBufferReg) {
                                        outIR.maxCBufferReg = vOp.cbufferReg;
                                        outIR.hasCBuffer = true;
                                    }
                                    outIR.basicBlocks[b].instructions.push_back(vOp);
                                    break;
                                }
                            }
                        }

                        if (isBranchFeedingPhiSelector && !blockSsas.empty()) {
                            for (uint32_t repSsa : blockSsas) {
                                const auto& raw = rawBinops[repSsa];
                                IRVectorOp vOp;
                                vOp.opcode = IROpcode::Add;
                                vOp.isScalarIntOp = true;
                                vOp.destTempIndex = 0;
                                auto itSrc0 = outIR.ssaValues.find(raw.src0Ssa);
                                if (itSrc0 != outIR.ssaValues.end() && itSrc0->second.type == IRValueType::CBufferComponent) {
                                    vOp.cbufferReg = itSrc0->second.regIndex;
                                    vOp.cbufferChannel = itSrc0->second.channel;
                                    vOp.hasCBuffer = true;
                                    if (vOp.cbufferReg > outIR.maxCBufferReg) {
                                        outIR.maxCBufferReg = vOp.cbufferReg;
                                    }
                                }
                                if (raw.isConst1) {
                                    vOp.literalInt = raw.constInt1;
                                }
                                outIR.basicBlocks[b].instructions.push_back(vOp);
                            }
                        }
                    }
                }
                outIR.numTempsNeeded = (outIR.compoundCFG.pattern != CFGPatternKind::None &&
                                        outIR.compoundCFG.pattern != CFGPatternKind::StraightLine &&
                                        outIR.compoundCFG.pattern != CFGPatternKind::Switch &&
                                        outIR.compoundCFG.pattern != CFGPatternKind::ConditionalBranch) ? 4 : 2;
            }
        } else {
            // Trace instruction dataflow from storeOutput backwards
            if (storeOutputCount > 0) {
                std::vector<std::vector<uint32_t>> binopStages;

                std::vector<uint32_t> currentWave;
                for (uint32_t i = 0; i < storeOutputCount; ++i) {
                    currentWave.push_back(resolveSsa(storeOutputSsa[i]));
                }

                while (!currentWave.empty()) {
                    bool hasBinop = false;
                    std::vector<uint32_t> stageSsas;
                    std::vector<uint32_t> nextWave;

                    for (uint32_t ssa : currentWave) {
                        ssa = resolveSsa(ssa);
                        auto it = rawBinops.find(ssa);
                        if (it != rawBinops.end()) {
                            hasBinop = true;
                            stageSsas.push_back(ssa);
                            nextWave.push_back(resolveSsa(it->second.src0Ssa));
                            nextWave.push_back(resolveSsa(it->second.src1Ssa));
                        }
                    }

                    if (hasBinop && !stageSsas.empty()) {
                        binopStages.push_back(stageSsas);
                    }
                    currentWave = nextWave;
                }

                // Reverse so earliest operations come first
                std::reverse(binopStages.begin(), binopStages.end());

                uint32_t tempIndex = 0;
                for (size_t s = 0; s < binopStages.size(); ++s) {
                    const auto& stageSsas = binopStages[s];
                    if (stageSsas.empty()) continue;

                    uint32_t representativeSsa = stageSsas[0];
                    const auto& raw = rawBinops[representativeSsa];

                    IRVectorOp vOp;
                    if (raw.opcode == 0) vOp.opcode = IROpcode::Add;
                    else if (raw.opcode == 1) vOp.opcode = IROpcode::Subtract;
                    else if (raw.opcode == 2) vOp.opcode = IROpcode::Multiply;
                    else vOp.opcode = IROpcode::None;

                    uint32_t s0 = resolveSsa(raw.src0Ssa);
                    uint32_t s1 = resolveSsa(raw.src1Ssa);
                    auto itSrc0 = outIR.ssaValues.find(s0);
                    auto itSrc1 = outIR.ssaValues.find(s1);

                    if (vOp.opcode == IROpcode::Subtract) {
                        if (itSrc0 != outIR.ssaValues.end() && itSrc0->second.type == IRValueType::CBufferComponent) {
                            vOp.isReverseSubtract = true;
                        }
                    }

                    if (itSrc0 != outIR.ssaValues.end() && itSrc0->second.type == IRValueType::CBufferComponent) {
                        vOp.cbufferReg = itSrc0->second.regIndex;
                        vOp.hasCBuffer = true;
                    } else if (itSrc1 != outIR.ssaValues.end() && itSrc1->second.type == IRValueType::CBufferComponent) {
                        vOp.cbufferReg = itSrc1->second.regIndex;
                        vOp.hasCBuffer = true;
                    }

                    bool isLastStage = (s == binopStages.size() - 1);
                    vOp.destIsOutput = isLastStage;
                    if (!isLastStage) {
                        vOp.destTempIndex = 0; // Sequential SSA accumulation into r0
                        tempIndex = 1;
                    }

                    outIR.vectorOps.push_back(vOp);
                }

                outIR.numTempsNeeded = tempIndex;

                if (binopStages.empty() && storeOutputCount > 0) {
                    for (uint32_t i = 0; i < storeOutputCount; ++i) {
                        uint32_t ssa = resolveSsa(storeOutputSsa[i]);
                        auto it = outIR.ssaValues.find(ssa);
                        if (it != outIR.ssaValues.end()) {
                            IRVectorOp movOp;
                            movOp.opcode = IROpcode::Move;
                            movOp.destIsOutput = true;
                            movOp.channelMask = (1u << i);
                            movOp.isComponentMove = true;
                            if (it->second.type == IRValueType::CBufferComponent) {
                                movOp.hasCBuffer = true;
                                movOp.cbufferReg = it->second.regIndex;
                                movOp.cbufferChannel = it->second.channel;
                                if (it->second.regIndex > outIR.maxCBufferReg) {
                                    outIR.maxCBufferReg = it->second.regIndex;
                                }
                                outIR.hasCBuffer = true;
                            } else if (it->second.type == IRValueType::IntermediateTemp) {
                                movOp.srcTempIndex = it->second.regIndex;
                                movOp.cbufferChannel = it->second.channel;
                            } else if (it->second.type == IRValueType::LiteralFloat) {
                                if (it->second.isInteger) {
                                    movOp.isScalarIntOp = true;
                                    movOp.literalInt = it->second.literalInt;
                                } else {
                                    movOp.src0[0] = it->second;
                                }
                            }
                            outIR.vectorOps.push_back(movOp);
                        }
                    }
                }
            }
        }

        // Assign DXBC temporary registers to all function-local storage allocations
        if (!outIR.localAllocs.empty()) {
            uint32_t baseLocalTemp = outIR.numTempsNeeded;
            for (size_t s = 0; s < outIR.localAllocs.size(); ++s) {
                outIR.localAllocs[s].tempRegIndex = baseLocalTemp + static_cast<uint32_t>(s);
            }
            outIR.numTempsNeeded += static_cast<uint32_t>(outIR.localAllocs.size());
        }

        outStatus = DXILTranslationStatus::Success;
        outDiagnostic = "Successfully decoded LLVM bitstream into Intermediate Representation (types: " +
                        std::to_string(outIR.typeTable.size()) + ", consts: " +
                        std::to_string(outIR.constantTable.size()) + ").";
        return true;
    }
    catch (const std::exception& ex) {
        outStatus = DXILTranslationStatus::MalformedBitcode;
        outDiagnostic = std::string("Decoder exception: ") + ex.what();
        return false;
    }
}

} // namespace kepler
