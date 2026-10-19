#include <iostream>
#include <vector>
#include <cstring>
#include <cassert>

#include "downgrader/dxil_inspector.h"
#include "downgrader/shader_policy.h"
#include "downgrader/texture_clamper.h"
#include "core/config.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Helper to construct synthetic DXBC containers for unit testing
std::vector<uint8_t> CreateSyntheticDXBC(uint32_t chunkFourCC, const void* chunkData, uint32_t chunkSize) {
    uint32_t chunkCount = 1;
    uint32_t headerSize = 32;
    uint32_t offsetTableSize = chunkCount * sizeof(uint32_t);
    uint32_t chunkOffset = headerSize + offsetTableSize;
    uint32_t chunkHeaderSize = 8;
    uint32_t totalSize = chunkOffset + chunkHeaderSize + chunkSize;

    std::vector<uint8_t> buffer(totalSize, 0);

    // Header
    *reinterpret_cast<uint32_t*>(&buffer[0]) = 0x43425844; // 'DXBC'
    *reinterpret_cast<uint32_t*>(&buffer[20]) = 1;          // version
    *reinterpret_cast<uint32_t*>(&buffer[24]) = totalSize;  // totalSize
    *reinterpret_cast<uint32_t*>(&buffer[28]) = chunkCount; // chunkCount

    // Offset table
    *reinterpret_cast<uint32_t*>(&buffer[32]) = chunkOffset;

    // Chunk Header
    *reinterpret_cast<uint32_t*>(&buffer[chunkOffset]) = chunkFourCC;
    *reinterpret_cast<uint32_t*>(&buffer[chunkOffset + 4]) = chunkSize;

    // Chunk Body
    if (chunkData && chunkSize > 0) {
        std::memcpy(&buffer[chunkOffset + 8], chunkData, chunkSize);
    }

    return buffer;
}

} // anonymous namespace

bool TestNullBytecode() {
    using namespace kepler;
    auto res = DxilInspector::InspectShader(nullptr, 100);
    TEST_ASSERT(!res.isValidContainer, "Null pointer must return invalid container");
    TEST_ASSERT(res.containerType == ShaderContainerType::Invalid, "Container type must be Invalid");
    std::cout << "[PASS] TestNullBytecode\n";
    return true;
}

bool TestTruncatedHeader() {
    using namespace kepler;
    uint8_t smallBuf[16] = { 0 };
    auto res = DxilInspector::InspectShader(smallBuf, sizeof(smallBuf));
    TEST_ASSERT(!res.isValidContainer, "Truncated buffer must return invalid container");
    TEST_ASSERT(res.containerType == ShaderContainerType::Invalid, "Container type must be Invalid");
    std::cout << "[PASS] TestTruncatedHeader\n";
    return true;
}

bool TestNonDXBCMagic() {
    using namespace kepler;
    uint8_t fakeBuf[64] = { 0 };
    *reinterpret_cast<uint32_t*>(&fakeBuf[0]) = 0x46464952; // 'RIFF'
    auto res = DxilInspector::InspectShader(fakeBuf, sizeof(fakeBuf));
    TEST_ASSERT(!res.isValidContainer, "Non-DXBC magic must return invalid container");
    TEST_ASSERT(res.containerType == ShaderContainerType::Invalid, "Container type must be Invalid");
    std::cout << "[PASS] TestNonDXBCMagic\n";
    return true;
}

bool TestContainerSizeMismatch() {
    using namespace kepler;
    uint8_t fakeBuf[64] = { 0 };
    *reinterpret_cast<uint32_t*>(&fakeBuf[0]) = 0x43425844; // 'DXBC'
    *reinterpret_cast<uint32_t*>(&fakeBuf[20]) = 1;
    *reinterpret_cast<uint32_t*>(&fakeBuf[24]) = 1024; // claims 1024 bytes, buffer is only 64!
    *reinterpret_cast<uint32_t*>(&fakeBuf[28]) = 1;

    auto res = DxilInspector::InspectShader(fakeBuf, sizeof(fakeBuf));
    TEST_ASSERT(!res.isValidContainer, "Size mismatch must return invalid container");
    TEST_ASSERT(res.containerType == ShaderContainerType::Invalid, "Container type must be Invalid");
    std::cout << "[PASS] TestContainerSizeMismatch\n";
    return true;
}

bool TestMalformedChunkOffset() {
    using namespace kepler;
    uint8_t fakeBuf[64] = { 0 };
    *reinterpret_cast<uint32_t*>(&fakeBuf[0]) = 0x43425844; // 'DXBC'
    *reinterpret_cast<uint32_t*>(&fakeBuf[20]) = 1;
    *reinterpret_cast<uint32_t*>(&fakeBuf[24]) = 64;
    *reinterpret_cast<uint32_t*>(&fakeBuf[28]) = 1;
    *reinterpret_cast<uint32_t*>(&fakeBuf[32]) = 200; // chunk offset 200 exceeds totalSize 64!

    auto res = DxilInspector::InspectShader(fakeBuf, sizeof(fakeBuf));
    TEST_ASSERT(!res.isValidContainer, "Out-of-bounds offset must return invalid container");
    TEST_ASSERT(res.containerType == ShaderContainerType::Invalid, "Container type must be Invalid");
    std::cout << "[PASS] TestMalformedChunkOffset\n";
    return true;
}

bool TestSyntheticLegacyDXBC_VertexShader() {
    using namespace kepler;
    // SHEX version token: stage = 1 (Vertex), major = 5, minor = 1 -> 0x00010051
    uint32_t token = (1 << 16) | (5 << 4) | 1;
    auto container = CreateSyntheticDXBC(0x58454853 /* 'SHEX' */, &token, sizeof(token));

    auto res = DxilInspector::InspectShader(container.data(), container.size());
    TEST_ASSERT(res.isValidContainer, "Synthetic DXBC must be valid");
    TEST_ASSERT(res.containerType == ShaderContainerType::LegacyDXBC, "Must identify as LegacyDXBC");
    TEST_ASSERT(res.stage == ShaderStage::Vertex, "Stage must be Vertex");
    TEST_ASSERT(res.majorVersion == 5, "Major version must be 5");
    TEST_ASSERT(res.minorVersion == 1, "Minor version must be 1");
    TEST_ASSERT(res.isSupportedOnKepler, "SM 5.1 must be supported on Kepler");
    TEST_ASSERT(!res.hasDXILChunk, "Must not have DXIL chunk");
    TEST_ASSERT(res.hasLegacyBytecodeChunk, "Must have legacy bytecode chunk");
    std::cout << "[PASS] TestSyntheticLegacyDXBC_VertexShader\n";
    return true;
}

bool TestSyntheticLegacyDXBC_PixelShader() {
    using namespace kepler;
    // SHEX version token: stage = 0 (Pixel), major = 5, minor = 0 -> 0x00000050
    uint32_t token = (0 << 16) | (5 << 4) | 0;
    auto container = CreateSyntheticDXBC(0x58454853 /* 'SHEX' */, &token, sizeof(token));

    auto res = DxilInspector::InspectShader(container.data(), container.size());
    TEST_ASSERT(res.isValidContainer, "Synthetic DXBC must be valid");
    TEST_ASSERT(res.containerType == ShaderContainerType::LegacyDXBC, "Must identify as LegacyDXBC");
    TEST_ASSERT(res.stage == ShaderStage::Pixel, "Stage must be Pixel");
    TEST_ASSERT(res.majorVersion == 5, "Major version must be 5");
    TEST_ASSERT(res.minorVersion == 0, "Minor version must be 0");
    TEST_ASSERT(res.isSupportedOnKepler, "SM 5.0 must be supported on Kepler");
    std::cout << "[PASS] TestSyntheticLegacyDXBC_PixelShader\n";
    return true;
}

bool TestSyntheticDXIL_VertexShader() {
    using namespace kepler;
    // DxilProgramHeader structure
    struct {
        uint32_t bitcodeHeaderVersion; // stage = 1 (VS), major = 6, minor = 0
        uint32_t sizeInBytes;
        uint32_t dxilMagic;
        uint32_t dxilVersion;
        uint32_t bitcodeOffset;
        uint32_t bitcodeSize;
    } dxilHdr;

    dxilHdr.bitcodeHeaderVersion = (1 << 16) | (6 << 4) | 0; // VS, SM 6.0
    dxilHdr.sizeInBytes = sizeof(dxilHdr);
    dxilHdr.dxilMagic = 0x4C495844; // 'DXIL'
    dxilHdr.dxilVersion = 0x00000100;
    dxilHdr.bitcodeOffset = sizeof(dxilHdr);
    dxilHdr.bitcodeSize = 0;

    auto container = CreateSyntheticDXBC(0x4C495844 /* 'DXIL' */, &dxilHdr, sizeof(dxilHdr));

    auto res = DxilInspector::InspectShader(container.data(), container.size());
    TEST_ASSERT(res.isValidContainer, "Synthetic DXIL container must be valid");
    TEST_ASSERT(res.containerType == ShaderContainerType::DXIL, "Must identify as DXIL");
    TEST_ASSERT(res.stage == ShaderStage::Vertex, "Stage must be Vertex");
    TEST_ASSERT(res.majorVersion == 6, "Major version must be 6");
    TEST_ASSERT(res.minorVersion == 0, "Minor version must be 0");
    TEST_ASSERT(!res.isSupportedOnKepler, "SM 6.0 must NOT be supported natively on Kepler");
    TEST_ASSERT(res.hasDXILChunk, "Must have DXIL chunk");
    std::cout << "[PASS] TestSyntheticDXIL_VertexShader\n";
    return true;
}

bool TestSyntheticDXIL_ComputeShader() {
    using namespace kepler;
    struct {
        uint32_t bitcodeHeaderVersion; // stage = 5 (CS), major = 6, minor = 6 (SM 6.6)
        uint32_t sizeInBytes;
        uint32_t dxilMagic;
        uint32_t dxilVersion;
        uint32_t bitcodeOffset;
        uint32_t bitcodeSize;
    } dxilHdr;

    dxilHdr.bitcodeHeaderVersion = (5 << 16) | (6 << 4) | 6; // CS, SM 6.6
    dxilHdr.sizeInBytes = sizeof(dxilHdr);
    dxilHdr.dxilMagic = 0x4C495844; // 'DXIL'
    dxilHdr.dxilVersion = 0x00000100;
    dxilHdr.bitcodeOffset = sizeof(dxilHdr);
    dxilHdr.bitcodeSize = 0;

    auto container = CreateSyntheticDXBC(0x4C495844 /* 'DXIL' */, &dxilHdr, sizeof(dxilHdr));

    auto res = DxilInspector::InspectShader(container.data(), container.size());
    TEST_ASSERT(res.isValidContainer, "Synthetic DXIL container must be valid");
    TEST_ASSERT(res.containerType == ShaderContainerType::DXIL, "Must identify as DXIL");
    TEST_ASSERT(res.stage == ShaderStage::Compute, "Stage must be Compute");
    TEST_ASSERT(res.majorVersion == 6, "Major version must be 6");
    TEST_ASSERT(res.minorVersion == 6, "Minor version must be 6");
    TEST_ASSERT(!res.isSupportedOnKepler, "SM 6.6 must NOT be supported natively on Kepler");
    std::cout << "[PASS] TestSyntheticDXIL_ComputeShader\n";
    return true;
}

bool TestShaderPolicyEvaluation() {
    using namespace kepler;
    auto& policy = ShaderPolicy::Instance();

    // 1. Valid SM 5.1 shader -> supported, no fallback needed
    uint32_t token = (1 << 16) | (5 << 4) | 1;
    auto validShader = CreateSyntheticDXBC(0x58454853, &token, sizeof(token));
    auto dec1 = policy.EvaluateShader(validShader.data(), validShader.size(), ShaderStage::Vertex);
    TEST_ASSERT(dec1.isSupported, "SM 5.1 must be supported");
    TEST_ASSERT(!dec1.shouldIntercept, "Should not intercept natively supported shader");
    TEST_ASSERT(!dec1.fallbackApplied, "No fallback should be applied");

    // 2. DXIL SM 6.0 shader -> unsupported, fallback disabled by default
    struct {
        uint32_t bitcodeHeaderVersion;
        uint32_t sizeInBytes;
        uint32_t dxilMagic;
        uint32_t dxilVersion;
        uint32_t bitcodeOffset;
        uint32_t bitcodeSize;
    } dxilHdr;
    dxilHdr.bitcodeHeaderVersion = (1 << 16) | (6 << 4) | 0;
    dxilHdr.sizeInBytes = sizeof(dxilHdr);
    dxilHdr.dxilMagic = 0x4C495844;
    dxilHdr.dxilVersion = 0x00000100;
    dxilHdr.bitcodeOffset = sizeof(dxilHdr);
    dxilHdr.bitcodeSize = 0;
    auto dxilShader = CreateSyntheticDXBC(0x4C495844, &dxilHdr, sizeof(dxilHdr));

    auto dec2 = policy.EvaluateShader(dxilShader.data(), dxilShader.size(), ShaderStage::Vertex);
    TEST_ASSERT(!dec2.isSupported, "DXIL SM 6.0 must be reported unsupported");
    TEST_ASSERT(!dec2.shouldIntercept, "Should not intercept when fallback is disabled (preserve native error)");
    TEST_ASSERT(!dec2.fallbackApplied, "Fallback must not be applied without explicit translator");

    std::cout << "[PASS] TestShaderPolicyEvaluation\n";
    return true;
}

bool TestResourceClamping() {
    using namespace kepler;
    auto& clamper = TextureClamper::Instance();

    // 1. Standard Buffer
    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = 65536;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    auto res1 = clamper.ValidateAndClamp(&bufDesc, nullptr, D3D12_HEAP_FLAG_NONE);
    TEST_ASSERT(res1.isValid, "Buffer must be valid");
    TEST_ASSERT(!res1.isModified, "Buffer must not be clamped");

    // 2. Texture2D exceeding 16384 limit
    D3D12_RESOURCE_DESC oversizedDesc = {};
    oversizedDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    oversizedDesc.Width = 32768; // Exceeds FL 11_0 limit (16384)
    oversizedDesc.Height = 1024;
    oversizedDesc.DepthOrArraySize = 1;
    oversizedDesc.MipLevels = 1;
    auto res2 = clamper.ValidateAndClamp(&oversizedDesc, nullptr, D3D12_HEAP_FLAG_NONE);
    TEST_ASSERT(!res2.isValid, "Oversized texture must be rejected as invalid per FL 11_0 spec");

    // 3. RenderTarget Texture2D (should never be clamped even if configured)
    D3D12_RESOURCE_DESC rtDesc = {};
    rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rtDesc.Width = 1920;
    rtDesc.Height = 1080;
    rtDesc.DepthOrArraySize = 1;
    rtDesc.MipLevels = 1;
    rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    auto res3 = clamper.ValidateAndClamp(&rtDesc, nullptr, D3D12_HEAP_FLAG_NONE);
    TEST_ASSERT(res3.isValid, "RT texture must be valid");
    TEST_ASSERT(!res3.isModified, "RT texture must NEVER be modified (viewport safety)");

    std::cout << "[PASS] TestResourceClamping\n";
    return true;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  KeplerBridge Phase 3 Shader & Resource Inspector Tests\n";
    std::cout << "========================================================\n";

    int failures = 0;
    if (!TestNullBytecode()) failures++;
    if (!TestTruncatedHeader()) failures++;
    if (!TestNonDXBCMagic()) failures++;
    if (!TestContainerSizeMismatch()) failures++;
    if (!TestMalformedChunkOffset()) failures++;
    if (!TestSyntheticLegacyDXBC_VertexShader()) failures++;
    if (!TestSyntheticLegacyDXBC_PixelShader()) failures++;
    if (!TestSyntheticDXIL_VertexShader()) failures++;
    if (!TestSyntheticDXIL_ComputeShader()) failures++;
    if (!TestShaderPolicyEvaluation()) failures++;
    if (!TestResourceClamping()) failures++;

    std::cout << "========================================================\n";
    if (failures == 0) {
        std::cout << "ALL SHADER & RESOURCE INSPECTOR TESTS PASSED (11/11)!\n";
        return 0;
    } else {
        std::cerr << failures << " TESTS FAILED!\n";
        return 1;
    }
}
