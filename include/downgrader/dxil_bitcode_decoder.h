#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <map>
#include <set>
#include "downgrader/dxil_translation.h"

namespace kepler {

enum class LLVMTypeKind {
    Unknown,
    Void,
    Float,      // 32-bit IEEE float
    Double,     // 64-bit IEEE double
    Half,       // 16-bit half
    Integer,    // with bitWidth
    Pointer,    // with pointeeType
    Function,   // with returnType, paramTypes
    Struct,     // with memberTypes, name, isPacked
    Vector,     // with count, elemType
    Array,      // with count, elemType
    Metadata
};

struct LLVMType {
    LLVMTypeKind kind = LLVMTypeKind::Unknown;
    uint32_t bitWidth = 0;                     // for Integer
    uint32_t pointeeType = 0;                  // for Pointer
    uint32_t returnType = 0;                   // for Function
    std::vector<uint32_t> paramTypes;          // for Function
    std::vector<uint32_t> memberTypes;         // for Struct
    std::string name;                          // for Struct / named types
    bool isPacked = false;                     // for Struct
    uint32_t count = 0;                        // for Vector / Array
    uint32_t elemType = 0;                     // for Vector / Array
};

enum class LLVMConstantKind {
    Null,
    Undef,
    Integer,
    Float,
    Other
};

struct LLVMConstant {
    LLVMConstantKind kind = LLVMConstantKind::Null;
    uint32_t typeId = 0;
    uint32_t bitWidth = 0;
    int64_t intVal = 0;
    float floatVal = 0.0f;
    bool isInvalid = false;
    std::string errorDiagnostic;
};

// Authoritative Microsoft DXIL Operation Codes (from DXC / DxilConstants.h)
enum class DXILOpCode : int32_t {
    TempRegLoad = 0,
    TempRegStore = 1,
    MinPrecXRegLoad = 2,
    MinPrecXRegStore = 3,
    LoadInput = 4,
    StoreOutput = 5,
    FAbs = 6,
    Saturate = 7,
    IsFPNaN = 8,
    IsFPInfinite = 9,
    IsFinite = 10,
    IsNormal = 11,
    Cos = 12,
    Sin = 13,
    Tan = 14,
    Acos = 15,
    Asin = 16,
    Atan = 17,
    Hcos = 18,
    Hsin = 19,
    Htan = 20,
    Exp = 21,
    Log = 22,
    Sqrt = 23,
    Rsqrt = 24,
    Round_ne = 25,
    Round_ni = 26,
    Round_pi = 27,
    Round_z = 28,
    Bfrev = 29,
    Countbits = 30,
    FirstbitLo = 31,
    FirstbitHi = 32,
    FirstbitSHi = 33,
    FMax = 34,
    FMin = 35,
    IMax = 36,
    IMin = 37,
    UMax = 38,
    UMin = 39,
    FMA = 47,
    CreateHandle = 57,
    CBufferLoad = 58,
    CBufferLoadLegacy = 59,
    Sample = 60,
    SampleBias = 61,
    SampleLevel = 62,
    SampleGrad = 63,
    SampleCmp = 64,
    SampleCmpLevelZero = 65,
    TextureLoad = 66,
    TextureStore = 67,
    BufferLoad = 68,
    BufferStore = 69,
    StorePatchConstant = 106,
    CreateHandleFromBinding = 163
};

enum class IRValueType {
    None,
    InputComponent,     // v[regIndex].[channel]
    CBufferComponent,   // cb[slot][regIndex].[channel]
    LiteralFloat,       // Immediate 32-bit float
    TextureSample,      // Sample from t0, s0
    IntermediateTemp,   // r[tempIndex].[channel]
    ResourceHandle      // Handle to CBV/SRV/Sampler
};

struct IRValue {
    IRValueType type = IRValueType::None;
    uint32_t regIndex = 0;   // Register index (input, cb offset, temp)
    uint32_t slot = 0;       // Buffer / resource slot (0)
    uint8_t channel = 0;     // 0=x, 1=y, 2=z, 3=w
    float literalValue = 0.0f;
    uint32_t ssaId = 0;      // SSA value ID in LLVM bitcode

    static IRValue MakeInput(uint32_t reg, uint8_t chan, uint32_t ssa) {
        IRValue v;
        v.type = IRValueType::InputComponent;
        v.regIndex = reg;
        v.channel = chan;
        v.ssaId = ssa;
        return v;
    }

    static IRValue MakeCBuffer(uint32_t slot, uint32_t reg, uint8_t chan, uint32_t ssa) {
        IRValue v;
        v.type = IRValueType::CBufferComponent;
        v.slot = slot;
        v.regIndex = reg;
        v.channel = chan;
        v.ssaId = ssa;
        return v;
    }

    static IRValue MakeLiteral(float val, uint32_t ssa) {
        IRValue v;
        v.type = IRValueType::LiteralFloat;
        v.literalValue = val;
        v.ssaId = ssa;
        return v;
    }

    static IRValue MakeTemp(uint32_t tempReg, uint8_t chan, uint32_t ssa) {
        IRValue v;
        v.type = IRValueType::IntermediateTemp;
        v.regIndex = tempReg;
        v.channel = chan;
        v.ssaId = ssa;
        return v;
    }

    static IRValue MakeSample(uint8_t chan, uint32_t ssa) {
        IRValue v;
        v.type = IRValueType::TextureSample;
        v.channel = chan;
        v.ssaId = ssa;
        return v;
    }

    static IRValue MakeHandle(uint32_t ssa) {
        IRValue v;
        v.type = IRValueType::ResourceHandle;
        v.ssaId = ssa;
        return v;
    }

    int64_t literalInt = 0;
    bool isInteger = false;

    static IRValue MakeLiteralInt(int64_t val, uint32_t ssa) {
        IRValue v;
        v.type = IRValueType::LiteralFloat;
        v.literalInt = val;
        v.isInteger = true;
        v.literalValue = static_cast<float>(val);
        v.ssaId = ssa;
        return v;
    }
};

enum class IROpcode {
    None,
    Add,
    Subtract,
    Multiply,
    Sample,
    Move
};

struct IRVectorOp {
    IROpcode opcode = IROpcode::None;
    IRValue src0[4];
    IRValue src1[4];
    uint32_t cbufferReg = 0;        // CBuffer register index (0, 1, 2...)
    bool hasCBuffer = false;        // true if an operand is a CBuffer component
    bool isReverseSubtract = false; // true if subtracting (src0 - src1) where src0 is cbuffer and src1 is input
    bool destIsOutput = false;      // true if writes to o0.xyzw
    uint32_t destTempIndex = 0;     // temp register r[destTempIndex]
    uint32_t channelMask = 0x0F;    // xyzw (0x0F) or xy (0x03)
    uint8_t cbufferChannel = 0;     // channel when single-component (0=x, 1=y, 2=z, 3=w)
    bool isComponentMove = false;   // true if single component move
    bool isScalarIntOp = false;     // true if scalar integer ALU (e.g. iadd r[dest].x, cb0[reg].chan, l(lit))
    int64_t literalInt = 0;         // immediate integer constant
    bool isLocalMemOp = false;      // true if operation involves function-local storage (alloca/load/store)
    bool isLocalStore = false;      // true if store into local slot, false if load from local slot
    uint32_t localSlot = 0;         // slot index in ir.localAllocs
    uint32_t localSubSlot = 0;      // element index within slot (0..3)
    uint32_t srcTempIndex = 0;      // source temp register index
};

enum class BlockTerminatorKind {
    None,
    Return,
    UnconditionalBranch,
    ConditionalBranch,
    Switch,
    Unreachable
};

struct BlockTerminator {
    BlockTerminatorKind kind = BlockTerminatorKind::None;
    uint32_t targetBlock = 0;       // For UnconditionalBranch
    uint32_t trueBlock = 0;         // For ConditionalBranch
    uint32_t falseBlock = 0;        // For ConditionalBranch
    uint32_t conditionSsa = 0;      // SSA value ID of branch condition
    uint32_t switchConditionSsa = 0; // For Switch
    uint32_t defaultBlock = 0;      // For Switch
    std::vector<std::pair<int64_t, uint32_t>> switchCases; // (caseVal, targetBlock)
};

struct PHIIncoming {
    uint32_t incomingSsa = 0;
    uint32_t predecessorBlock = 0;
    bool isConstant = false;
    int64_t constantInt = 0;
    IRValue incomingValue;
};

struct DXILPHINode {
    uint32_t destSsa = 0;
    uint32_t typeId = 0;
    uint8_t channel = 0; // 0=x, 1=y, 2=z, 3=w
    std::vector<PHIIncoming> incoming;
    uint32_t mergeBlock = 0;
    uint32_t slotIndex = 0;
    bool isMem2RegPhi = false;
};

enum class ComparePredicate {
    None = 0,
    FloatEqual = 1,            // OEQ (1), UEQ (9)
    FloatGreaterThan = 2,      // OGT (2), UGT (10)
    FloatGreaterOrEqual = 3,   // OGE (3), UGE (11)
    FloatLessThan = 4,         // OLT (4), ULT (12)
    FloatLessOrEqual = 5,      // OLE (5), ULE (13)
    FloatNotEqual = 6,         // ONE (6), UNE (14)

    // Integer predicates
    IntEqual = 32,              // IPRED_EQ (32)
    IntNotEqual = 33,           // IPRED_NE (33)
    IntUnsignedGreaterThan = 34,// IPRED_UGT (34)
    IntUnsignedGreaterEqual = 35,// IPRED_UGE (35)
    IntUnsignedLessThan = 36,   // IPRED_ULT (36)
    IntUnsignedLessEqual = 37,  // IPRED_ULE (37)
    IntSignedGreaterThan = 38,  // IPRED_SGT (38)
    IntSignedGreaterEqual = 39, // IPRED_SGE (39)
    IntSignedLessThan = 40,     // IPRED_SLT (40)
    IntSignedLessEqual = 41     // IPRED_SLE (41)
};

inline bool IsIntegerPredicate(ComparePredicate pred) {
    return static_cast<int>(pred) >= 32 && static_cast<int>(pred) <= 41;
}

struct CompareInstruction {
    uint32_t destSsa = 0;
    ComparePredicate predicate = ComparePredicate::None;
    IRValue src0;
    IRValue src1;
    bool isConst0 = false;
    int64_t constInt0 = 0;
    bool isConst1 = false;
    int64_t constInt1 = 0;
};

struct DXILBasicBlock {
    uint32_t id = 0;
    std::vector<IRVectorOp> instructions;
    std::vector<DXILPHINode> phiNodes;
    std::vector<uint32_t> predecessors;
    std::vector<uint32_t> successors;
    BlockTerminator terminator;
    bool hasTerminator = false;
};

enum class LoopCmpOpcode {
    IGE, // 0x07000021 - signed >=
    UGE, // 0x07000050 - unsigned >=
    ILT, // 0x07000022 - signed <
    ULT, // 0x07000051 - unsigned <
    IEQ, // 0x07000020 - integer ==
    INE  // 0x07000027 - integer !=
};

struct SwitchDescriptor {
    bool isSwitch = false;
    uint32_t switchBlock = 0;
    uint32_t selectorSsa = 0;
    bool isSignedSelector = false;
    uint32_t defaultBlock = 0;
    std::vector<std::pair<int64_t, uint32_t>> cases; // (caseVal, targetBlock)
    uint32_t mergeBlock = 0;
    std::vector<uint32_t> caseBlocks;
    bool selectorIsCBuffer = false;
    uint32_t selectorCbSlot = 0;
    uint32_t selectorCbRow = 0;
    uint8_t selectorCbChannel = 0; // 0=x, 1=y, 2=z, 3=w
    bool selectorIsPhi = false;
    uint32_t selectorTempReg = 1;
};

enum class CFGPatternKind {
    None,
    StraightLine,
    ConditionalBranch,
    Loop,
    Switch,
    IfElseThenSwitch,
    SwitchThenIfElse
};

enum class CFGRegionKind {
    None,
    ConditionalBranch,
    Switch
};

struct CFGRegion {
    CFGRegionKind kind = CFGRegionKind::None;
    uint32_t entryBlock = 0;
    std::vector<uint32_t> internalBlocks;
    uint32_t mergeBlock = 0;

    // For ConditionalBranch
    uint32_t conditionSsa = 0;
    uint32_t trueBlock = 0;
    uint32_t falseBlock = 0;
    bool isTriangle = false;

    // For Switch
    uint32_t selectorSsa = 0;
    uint32_t defaultBlock = 0;
    std::vector<std::pair<int64_t, uint32_t>> cases;
};

struct CompoundCFGDescriptor {
    CFGPatternKind pattern = CFGPatternKind::None;
    CFGRegion firstRegion;
    CFGRegion secondRegion;
    uint32_t finalMergeBlock = 0;
};

struct LoopDescriptor {
    bool isLoop = false;
    uint32_t preheaderBlock = 0;
    uint32_t headerBlock = 0;
    uint32_t latchBlock = 0;
    uint32_t exitingBlock = 0;
    uint32_t exitBlock = 0;
    std::set<uint32_t> loopBlocks;

    // Induction variable
    uint32_t inductionPhiSsa = 0;
    int32_t inductionInitialValue = 0;
    int32_t inductionStep = 1;
    bool isDynamicBound = false;
    int32_t constantBound = 0;
    uint32_t boundCBufferReg = 0;
    uint8_t boundCBufferChannel = 0; // 0=x, 1=y...

    // Condition
    ComparePredicate conditionPredicate = ComparePredicate::None;
    bool isUnsignedPredicate = false;
    bool exitOnConditionTrue = true; // if true, branch takes exit when condition holds
    LoopCmpOpcode breakCmpOpcode = LoopCmpOpcode::IGE;

    // Loop-carried accumulator
    uint32_t accumulatorPhiSsa = 0;
    uint32_t accumulatorInitialSsa = 0;
    uint32_t accumulatorUpdateSsa = 0;

    // Inner branch if present
    bool hasInnerBranch = false;
    uint32_t innerBranchConditionSsa = 0;
    ComparePredicate innerBranchPredicate = ComparePredicate::None;
    float innerBranchLiteral = 0.5f;
    uint32_t innerTrueCBufferReg = 0;
    uint32_t innerFalseCBufferReg = 1;

    // Loop body operations
    std::vector<IRVectorOp> bodyInstructions;

    // Post-loop merge / exit operations
    std::vector<IRVectorOp> exitInstructions;
};

struct LocalElementState {
    bool isInitialized = false;
    uint32_t currentSsa = 0;
    int64_t constInt = 0;
    float constFloat = 0.0f;
    bool isConstant = false;
};

struct LocalAllocDescriptor {
    uint32_t slotIndex = 0;
    uint32_t ssaPointer = 0;
    uint32_t allocatedTypeId = 0;
    LLVMTypeKind elemKind = LLVMTypeKind::Float;
    uint32_t numElements = 1;
    uint32_t alignment = 4;
    bool isVector = false;
    bool isArray = false;
    uint32_t tempRegIndex = 0; // DXBC temp register assigned to this local storage
    std::vector<LocalElementState> elements;
};

struct LocalPointerRef {
    uint32_t slotIndex = 0;
    uint32_t subSlotIndex = 0;
    bool isValid = true;
    bool isDynamicIndex = false;
    bool isEscaped = false;
    uint8_t padding = 0;

    LocalPointerRef(uint32_t s = 0, uint32_t sub = 0, bool valid = true, bool dyn = false, bool esc = false)
        : slotIndex(s), subSlotIndex(sub), isValid(valid), isDynamicIndex(dyn), isEscaped(esc), padding(0) {}
};

struct DXILIntermediateRepresentation {
    ShaderStage stage = ShaderStage::Pixel;
    uint32_t numBasicBlocks = 0;
    
    // Ordered vector operations to execute
    std::vector<IRVectorOp> vectorOps;

    // Function-local memory & pointer storage
    std::vector<LocalAllocDescriptor> localAllocs;
    std::map<uint32_t, LocalPointerRef> localPointers; // SSA pointer -> local allocation reference

    // Control Flow Graph (CFG)
    bool hasControlFlow = false;
    bool hasLoop = false;
    bool hasSwitch = false;
    SwitchDescriptor switchDesc;
    CompoundCFGDescriptor compoundCFG;
    LoopDescriptor loop;
    std::vector<DXILBasicBlock> basicBlocks;
    std::map<uint32_t, CompareInstruction> comparisons;

    // Resource & register usage
    uint32_t maxCBufferReg = 0;
    bool hasCBuffer = false;
    bool hasTextureSample = false;
    bool hasSampleCoordArithmetic = false;
    float sampleCoordOffset = 0.0f;
    uint32_t numTempsNeeded = 0;
    uint32_t inputRegisterIndex = 0;
    bool hasInput = false;

    // SSA storage
    std::map<uint32_t, IRValue> ssaValues;

    // Decoded Module Metadata & Tables
    std::vector<LLVMType> typeTable;
    std::vector<LLVMConstant> constantTable;
    std::vector<uint32_t> moduleFunctions; // Function type indices for module functions
};

class DXILBitcodeDecoder {
public:
    /**
     * @brief Decodes an LLVM sign-rotated integer value according to LLVM bitcode specification.
     *        Handles LSB sign bit rotation and the raw == 1 special case for INT64_MIN.
     */
    static int64_t DecodeSignRotatedValue(uint64_t raw);

    /**
     * @brief Decodes an integer constant for a given bit width, validating range and detecting overflow.
     *        Returns false with an error diagnostic if the value overflows or width is invalid.
     */
    static bool DecodeIntegerConstant(
        uint64_t raw,
        uint32_t bitWidth,
        int64_t& outVal,
        std::string& outError);

    /**
     * @brief Decodes an LLVM 3.7 bitstream from a DXIL chunk into a dataflow-preserving IR.
     *        Deterministically verifies block boundaries, abbreviations, SSA values, and instructions.
     */
    static bool DecodeBitcode(
        const uint8_t* pBitcode,
        size_t bcSize,
        DXILIntermediateRepresentation& outIR,
        DXILTranslationStatus& outStatus,
        std::string& outDiagnostic);

    /**
     * @brief Validates a decoded Control Flow Graph against structural consistency rules:
     *        verifies block boundaries, terminators, predecessor/successor duality, PHI references.
     */
    static bool ValidateCFG(
        DXILIntermediateRepresentation& ir,
        DXILTranslationStatus& outStatus,
        std::string& outDiagnostic);

    /**
     * @brief Resolves a chain of empty forwarder blocks to its ultimate destination block.
     *        Detects and rejects cycles (UnsupportedInstruction) and out-of-bounds targets (MalformedBitcode).
     */
    static bool ResolveForwarderTarget(
        const DXILIntermediateRepresentation& ir,
        uint32_t startBlock,
        uint32_t mergeBlock,
        uint32_t& outResolved,
        DXILTranslationStatus& outStatus,
        std::string& outDiagnostic);

    /**
     * @brief Returns the authoritative canonical name of a DXIL operation code.
     */
    static const char* GetDXILOpCodeName(DXILOpCode op);
    static const char* GetDXILOpCodeName(int32_t opCode);
};

} // namespace kepler
