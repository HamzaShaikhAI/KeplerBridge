#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include "downgrader/shader_policy.h"

namespace kepler {

struct Mem2RegStats;

enum class DXILTranslationStatus {
    Success,
    NotDXIL,
    InvalidContainer,
    UnsupportedShaderModel,
    UnsupportedStage,
    UnsupportedWaveOps,
    Unsupported64BitOps,
    UnsupportedFeatureFlag,
    UnsupportedInstruction,
    MalformedBitcode,
    TranslationFailed
};

enum class DXILArithmeticOp {
    Unknown,
    Multiply,
    Add,
    Subtract,
    Move,
    Sample
};

// Authoritative Microsoft DXIL Shader Feature Flags (SFI0 / DFCC_FeatureInfo)
// Reference: DirectXShaderCompiler include/dxc/DXIL/DxilConstants.h
constexpr uint64_t SFI_DOUBLES                     = (1ULL << 0);  // 0x0001
constexpr uint64_t SFI_COMPUTE_RAW_STRUCTURED_4X   = (1ULL << 1);  // 0x0002
constexpr uint64_t SFI_UAVS_AT_EVERY_STAGE         = (1ULL << 2);  // 0x0004
constexpr uint64_t SFI_64_UAVS                     = (1ULL << 3);  // 0x0008
constexpr uint64_t SFI_MINIMUM_PRECISION           = (1ULL << 4);  // 0x0010 (FL 10.0+)
constexpr uint64_t SFI_11_1_DOUBLE_EXTENSIONS      = (1ULL << 5);  // 0x0020
constexpr uint64_t SFI_11_1_SHADER_EXTENSIONS      = (1ULL << 6);  // 0x0040
constexpr uint64_t SFI_LEVEL9_COMPARISON_FILTERING = (1ULL << 7);  // 0x0080
constexpr uint64_t SFI_TILED_RESOURCES             = (1ULL << 8);  // 0x0100
constexpr uint64_t SFI_STENCIL_REF                 = (1ULL << 9);  // 0x0200
constexpr uint64_t SFI_INNER_COVERAGE              = (1ULL << 10); // 0x0400
constexpr uint64_t SFI_TYPED_UAV_ADDITIONAL_FORMATS= (1ULL << 11); // 0x0800
constexpr uint64_t SFI_ROVS                        = (1ULL << 12); // 0x1000
constexpr uint64_t SFI_VIEWPORT_AND_RT_ARRAY_INDEX = (1ULL << 13); // 0x2000
constexpr uint64_t SFI_WAVE_OPS                    = (1ULL << 14); // 0x4000 (SM 6.0+)
constexpr uint64_t SFI_INT64_OPS                   = (1ULL << 15); // 0x8000 (SM 6.0+)
constexpr uint64_t SFI_VIEW_ID                     = (1ULL << 16); // 0x10000 (SM 6.1+)
constexpr uint64_t SFI_BARYCENTRICS                = (1ULL << 17); // 0x20000 (SM 6.2+)
constexpr uint64_t SFI_NATIVE_LOW_PRECISION        = (1ULL << 18); // 0x40000 (SM 6.2+)
constexpr uint64_t SFI_SHADING_RATE                = (1ULL << 19); // 0x80000 (SM 6.5+)
constexpr uint64_t SFI_RAYTRACING_TIER_1_1         = (1ULL << 20); // 0x100000 (SM 6.5+)
constexpr uint64_t SFI_SAMPLER_FEEDBACK            = (1ULL << 21); // 0x200000 (SM 6.5+)
constexpr uint64_t SFI_ATOMIC_INT64_TYPED          = (1ULL << 22); // 0x400000 (SM 6.6+)
constexpr uint64_t SFI_ATOMIC_INT64_GROUPSHARED    = (1ULL << 23); // 0x800000 (SM 6.6+)
constexpr uint64_t SFI_DERIVATIVES_IN_MESH_AMP     = (1ULL << 24); // 0x1000000 (SM 6.6+)
constexpr uint64_t SFI_RESOURCE_DESCRIPTOR_HEAP    = (1ULL << 25); // 0x2000000 (SM 6.6+)
constexpr uint64_t SFI_SAMPLER_DESCRIPTOR_HEAP     = (1ULL << 26); // 0x4000000 (SM 6.6+)
constexpr uint64_t SFI_RESERVED                    = (1ULL << 27); // 0x8000000
constexpr uint64_t SFI_ATOMIC_INT64_HEAP           = (1ULL << 28); // 0x10000000 (SM 6.6+)

// Composite mask for all 64-bit integer ALU and atomic operations
constexpr uint64_t SFI_ALL_INT64_MASK =
    SFI_INT64_OPS | SFI_ATOMIC_INT64_TYPED | SFI_ATOMIC_INT64_GROUPSHARED | SFI_ATOMIC_INT64_HEAP;

// Composite mask for advanced D3D12 / SM 6.x features requiring hardware beyond Kepler FL 11_0
constexpr uint64_t SFI_UNSUPPORTED_ADVANCED_FEATURES_MASK =
    SFI_ROVS | SFI_VIEW_ID | SFI_BARYCENTRICS | SFI_NATIVE_LOW_PRECISION | SFI_SHADING_RATE |
    SFI_RAYTRACING_TIER_1_1 | SFI_SAMPLER_FEEDBACK | SFI_DERIVATIVES_IN_MESH_AMP |
    SFI_RESOURCE_DESCRIPTOR_HEAP | SFI_SAMPLER_DESCRIPTOR_HEAP;

struct DXILSignatureElement {
    std::string semanticName;
    uint32_t semanticIndex = 0;
    uint32_t systemValueType = 0; // 0=NONE, 1=POS, 3=TARGET
    uint32_t componentType = 4;   // 4=FLOAT, 2=UINT, 3=SINT
    uint32_t registerIndex = 0;
    uint8_t mask = 0x0F;          // xyzw
    uint8_t readWriteMask = 0x0F;
};

struct DXILTranslationReport {
    DXILTranslationStatus status = DXILTranslationStatus::InvalidContainer;
    std::string diagnostic;
    uint32_t majorVersion = 0;
    uint32_t minorVersion = 0;
    ShaderStage stage = ShaderStage::Unknown;
    uint64_t featureFlags = 0;
    uint32_t declaredInputs = 0;
    uint32_t declaredOutputs = 0;
    uint32_t declaredCBVs = 0;
    uint32_t declaredSRVs = 0;
    uint32_t declaredSamplers = 0;
    uint32_t dxilBytecodeSize = 0;
    uint32_t loweredBytecodeSize = 0;
    bool hasWaveOps = false;
    bool has64BitOps = false;
    uint32_t numOperations = 0;
    bool isMultiOperation = false;
    bool isReverseSubtract = false;
    bool hasControlFlow = false;
    bool hasLoop = false;
    uint32_t loopTripCount = 0;
    uint32_t numBasicBlocks = 1;
    DXILArithmeticOp arithmeticOp = DXILArithmeticOp::Unknown;
    std::vector<DXILSignatureElement> inputSignatures;
    std::vector<DXILSignatureElement> outputSignatures;
};

class DXILTranslationEngine {
public:
    /**
     * @brief Performs preflight analysis to check whether the DXIL shader satisfies
     *        the strict mathematical and architectural constraints for safe lowering to SM 5.0.
     */
    static bool CanTranslate(
        const void* pBytecode,
        size_t size,
        DXILTranslationReport* pOutReport = nullptr);

    /**
     * @brief Lowers a compatible Shader Model 6.x DXIL container to an authentic Shader Model 5.0
     *        DXBC container, including container parsing, instruction lowering, signature remapping,
     *        and authentic DXBC checksum recalculation.
     */
    static bool TranslateDXILToSM50(
        const void* pSrcBytecode,
        size_t srcSize,
        std::vector<uint8_t>& outLoweredBytecode,
        DXILTranslationReport* pOutReport = nullptr,
        bool enableMem2Reg = true,
        Mem2RegStats* pOutMem2RegStats = nullptr);

    static bool BuildSM50Container(
        ShaderStage stage,
        const std::vector<DXILSignatureElement>& inputs,
        const std::vector<DXILSignatureElement>& outputs,
        const std::vector<uint32_t>& shexTokens,
        std::vector<uint8_t>& outBytecode);

    static std::string StatusToString(DXILTranslationStatus status);

private:
    static bool ParseContainerAndSignatures(
        const uint8_t* pRaw,
        size_t size,
        DXILTranslationReport& report,
        const uint8_t*& pOutDxilChunk,
        uint32_t& outDxilChunkSize);
};

} // namespace kepler
