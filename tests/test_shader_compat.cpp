#include <windows.h>
#include <d3d11.h>
#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "downgrader/bounded_shader_translator.h"
#include "downgrader/shader_disk_cache.h"
#include "downgrader/dxil_inspector.h"
#include "downgrader/shader_policy.h"
#include "core/config.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            return false; \
        } \
    } while (0)

namespace {

// Helper to create synthetic DXIL container
std::vector<uint8_t> CreateSyntheticDXIL(kepler::ShaderStage stage) {
    struct {
        uint32_t bitcodeHeaderVersion;
        uint32_t sizeInBytes;
        uint32_t dxilMagic;
        uint32_t dxilVersion;
        uint32_t bitcodeOffset;
        uint32_t bitcodeSize;
    } dxilHdr;

    uint32_t stageToken = 0;
    switch (stage) {
        case kepler::ShaderStage::Vertex: stageToken = 1; break;
        case kepler::ShaderStage::Pixel: stageToken = 0; break;
        case kepler::ShaderStage::Compute: stageToken = 5; break;
        default: stageToken = 6; break;
    }

    dxilHdr.bitcodeHeaderVersion = (stageToken << 16) | (6 << 4) | 0; // SM 6.0
    dxilHdr.sizeInBytes = sizeof(dxilHdr);
    dxilHdr.dxilMagic = 0x4C495844; // 'DXIL'
    dxilHdr.dxilVersion = 0x00000100;
    dxilHdr.bitcodeOffset = sizeof(dxilHdr);
    dxilHdr.bitcodeSize = 0;

    uint32_t chunkCount = 1;
    uint32_t headerSize = 32;
    uint32_t offsetTableSize = chunkCount * sizeof(uint32_t);
    uint32_t chunkOffset = headerSize + offsetTableSize;
    uint32_t chunkHeaderSize = 8;
    uint32_t totalSize = chunkOffset + chunkHeaderSize + sizeof(dxilHdr);

    std::vector<uint8_t> buffer(totalSize, 0);
    *reinterpret_cast<uint32_t*>(&buffer[0]) = 0x43425844; // 'DXBC'
    *reinterpret_cast<uint32_t*>(&buffer[20]) = 1;
    *reinterpret_cast<uint32_t*>(&buffer[24]) = totalSize;
    *reinterpret_cast<uint32_t*>(&buffer[28]) = chunkCount;
    *reinterpret_cast<uint32_t*>(&buffer[32]) = chunkOffset;
    *reinterpret_cast<uint32_t*>(&buffer[chunkOffset]) = 0x4C495844; // 'DXIL'
    *reinterpret_cast<uint32_t*>(&buffer[chunkOffset + 4]) = sizeof(dxilHdr);
    std::memcpy(&buffer[chunkOffset + 8], &dxilHdr, sizeof(dxilHdr));

    return buffer;
}

} // anonymous namespace

bool TestDirectCompilerCompilation() {
    using namespace kepler;
    BoundedShaderTranslator translator;
    TEST_ASSERT(translator.IsCompilerAvailable(), "Compiler must be available via d3dcompiler_47.dll");

    // 1. Vertex Shader
    const char* vsSource = "float4 VSMain(float4 pos : POSITION) : SV_Position { return pos; }";
    std::vector<uint8_t> vsCode;
    std::string err;
    bool okVS = translator.CompileHLSLToDXBC(vsSource, "VSMain", "vs_5_0", vsCode, err);
    TEST_ASSERT(okVS, "VS compilation must succeed: " + err);
    TEST_ASSERT(!vsCode.empty(), "VS bytecode must not be empty");

    auto vsInsp = DxilInspector::InspectShader(vsCode.data(), vsCode.size());
    TEST_ASSERT(vsInsp.isValidContainer, "Compiled VS must be valid container");
    TEST_ASSERT(vsInsp.stage == ShaderStage::Vertex, "Compiled VS stage must be Vertex");
    TEST_ASSERT(vsInsp.majorVersion == 5 && vsInsp.minorVersion == 0, "Compiled VS must be SM 5.0");
    TEST_ASSERT(vsInsp.isSupportedOnKepler, "Compiled VS must be marked supported on Kepler");

    // 2. Pixel Shader
    const char* psSource = "float4 PSMain() : SV_Target { return float4(1,0,0,1); }";
    std::vector<uint8_t> psCode;
    bool okPS = translator.CompileHLSLToDXBC(psSource, "PSMain", "ps_5_0", psCode, err);
    TEST_ASSERT(okPS, "PS compilation must succeed: " + err);
    auto psInsp = DxilInspector::InspectShader(psCode.data(), psCode.size());
    TEST_ASSERT(psInsp.stage == ShaderStage::Pixel, "Compiled PS stage must be Pixel");
    TEST_ASSERT(psInsp.majorVersion == 5 && psInsp.minorVersion == 0, "Compiled PS must be SM 5.0");

    // 3. Compute Shader
    const char* csSource = "[numthreads(1,1,1)] void CSMain() {}";
    std::vector<uint8_t> csCode;
    bool okCS = translator.CompileHLSLToDXBC(csSource, "CSMain", "cs_5_0", csCode, err);
    TEST_ASSERT(okCS, "CS compilation must succeed: " + err);
    auto csInsp = DxilInspector::InspectShader(csCode.data(), csCode.size());
    TEST_ASSERT(csInsp.stage == ShaderStage::Compute, "Compiled CS stage must be Compute");
    TEST_ASSERT(csInsp.majorVersion == 5 && csInsp.minorVersion == 0, "Compiled CS must be SM 5.0");

    std::cout << "[PASS] TestDirectCompilerCompilation" << std::endl;
    return true;
}

bool TestPhysicalD3D11RuntimeVerification() {
    using namespace kepler;
    BoundedShaderTranslator translator;
    TEST_ASSERT(translator.IsCompilerAvailable(), "Compiler must be available");

    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pContext = nullptr;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &pDevice, &fl, &pContext);
    TEST_ASSERT(SUCCEEDED(hr) && pDevice, "D3D11 device creation must succeed");

    std::vector<uint8_t> vsCode;
    std::string err;
    translator.CompileHLSLToDXBC("float4 main(float4 p:POSITION):SV_Position{return p;}", "main", "vs_5_0", vsCode, err);

    ID3D11VertexShader* pVS = nullptr;
    hr = pDevice->CreateVertexShader(vsCode.data(), vsCode.size(), nullptr, &pVS);
    TEST_ASSERT(SUCCEEDED(hr) && pVS, "D3D11 driver must accept compiled vertex shader");
    pVS->Release();

    std::vector<uint8_t> psCode;
    translator.CompileHLSLToDXBC("float4 main():SV_Target{return float4(0,1,0,1);}", "main", "ps_5_0", psCode, err);

    ID3D11PixelShader* pPS = nullptr;
    hr = pDevice->CreatePixelShader(psCode.data(), psCode.size(), nullptr, &pPS);
    TEST_ASSERT(SUCCEEDED(hr) && pPS, "D3D11 driver must accept compiled pixel shader");
    pPS->Release();

    pContext->Release();
    pDevice->Release();
    std::cout << "[PASS] TestPhysicalD3D11RuntimeVerification" << std::endl;
    return true;
}

bool TestUnsupportedStagesRejection() {
    using namespace kepler;
    BoundedShaderTranslator translator;

    ShaderInspectionResult insp = {};
    insp.isValidContainer = true;

    // Geometry
    insp.stage = ShaderStage::Geometry;
    TEST_ASSERT(!translator.CanTranslate(insp), "Geometry stage must be rejected");

    // Hull
    insp.stage = ShaderStage::Hull;
    TEST_ASSERT(!translator.CanTranslate(insp), "Hull stage must be rejected");

    // Domain
    insp.stage = ShaderStage::Domain;
    TEST_ASSERT(!translator.CanTranslate(insp), "Domain stage must be rejected");

    // Mesh
    insp.stage = ShaderStage::Mesh;
    TEST_ASSERT(!translator.CanTranslate(insp), "Mesh stage must be rejected");

    // Amplification
    insp.stage = ShaderStage::Amplification;
    TEST_ASSERT(!translator.CanTranslate(insp), "Amplification stage must be rejected");

    // Raytracing
    insp.stage = ShaderStage::RayGeneration;
    TEST_ASSERT(!translator.CanTranslate(insp), "Raytracing stage must be rejected");

    std::cout << "[PASS] TestUnsupportedStagesRejection" << std::endl;
    return true;
}

bool TestShaderPolicyIntegrationWithFallback() {
    using namespace kepler;
    auto translator = std::make_shared<BoundedShaderTranslator>();
    translator->RegisterStageFallbackSource(
        ShaderStage::Pixel,
        "float4 PSMain() : SV_Target { return float4(0.25, 0.5, 0.75, 1.0); }",
        "PSMain");

    auto& policy = ShaderPolicy::Instance();
    policy.ClearTranslators();
    policy.RegisterTranslator(translator);

    auto dxilBlob = CreateSyntheticDXIL(ShaderStage::Pixel);

    // 1. With fallback disabled: must preserve native error propagation
    Config::Instance().SetShaderFallbackEnabled(false);
    auto decNoFallback = policy.EvaluateShader(dxilBlob.data(), dxilBlob.size(), ShaderStage::Pixel);
    TEST_ASSERT(!decNoFallback.isSupported, "Unsupported DXIL must be reported as not supported");
    TEST_ASSERT(!decNoFallback.fallbackApplied, "Fallback must not be applied when disabled");
    TEST_ASSERT(!decNoFallback.shouldIntercept, "Should not intercept when fallback is disabled");

    // 2. With fallback enabled: must invoke BoundedShaderTranslator and produce authentic DXBC
    Config::Instance().SetShaderFallbackEnabled(true);
    auto decWithFallback = policy.EvaluateShader(dxilBlob.data(), dxilBlob.size(), ShaderStage::Pixel);
    TEST_ASSERT(decWithFallback.isSupported, "Must report supported when fallback succeeds");
    TEST_ASSERT(decWithFallback.fallbackApplied, "Fallback must be applied");
    TEST_ASSERT(decWithFallback.shouldIntercept, "Should intercept when fallback succeeds");
    TEST_ASSERT(decWithFallback.effectiveLength > 0, "Must have non-empty effective bytecode");

    // Verify effective bytecode is authentic DXBC SM 5.0
    auto resInsp = DxilInspector::InspectShader(decWithFallback.effectiveBytecode, decWithFallback.effectiveLength);
    TEST_ASSERT(resInsp.isValidContainer, "Effective bytecode must be valid container");
    TEST_ASSERT(resInsp.majorVersion == 5 && resInsp.minorVersion == 0, "Must be SM 5.0 DXBC");
    TEST_ASSERT(resInsp.stage == ShaderStage::Pixel, "Stage must match Pixel");

    // Restore config
    Config::Instance().SetShaderFallbackEnabled(false);
    policy.ClearTranslators();

    std::cout << "[PASS] TestShaderPolicyIntegrationWithFallback" << std::endl;
    return true;
}

bool TestHonestFailureOnUnsupportedDXIL() {
    using namespace kepler;
    auto& policy = ShaderPolicy::Instance();
    policy.ClearTranslators();

    // Create synthetic DXIL compute shader
    auto dxilCS = CreateSyntheticDXIL(ShaderStage::Compute);

    Config::Instance().SetShaderFallbackEnabled(true);
    // No translator registered for Compute
    auto dec = policy.EvaluateShader(dxilCS.data(), dxilCS.size(), ShaderStage::Compute);
    TEST_ASSERT(!dec.isSupported, "Must honestly report unsupported when no translator exists");
    TEST_ASSERT(!dec.fallbackApplied, "Must not claim fallback was applied");
    TEST_ASSERT(dec.fallbackStorage.empty(), "Must not fabricate fake bytecode");

    Config::Instance().SetShaderFallbackEnabled(false);
    std::cout << "[PASS] TestHonestFailureOnUnsupportedDXIL" << std::endl;
    return true;
}

bool TestShaderDiskCacheHitMissAndValidation() {
    using namespace kepler;
    auto& cache = ShaderDiskCache::Instance();
    cache.ResetStats();

    // Use a clean custom test directory
    std::string testCacheDir = "kepler_cache\\test_shaders";
    cache.SetCacheDirectory(testCacheDir);

    BoundedShaderTranslator translator;
    TEST_ASSERT(translator.IsCompilerAvailable(), "Compiler must be available");

    const std::string hlsl = "float4 PSMain(float4 pos : SV_Position) : SV_Target { return float4(0.2, 0.4, 0.6, 1.0); }";
    std::string err;

    // Ensure cache is enabled
    Config::Instance().SetShaderDiskCacheEnabled(true);

    // 1. First compilation: Must be a Cache Miss and Cache Write
    std::vector<uint8_t> code1;
    bool ok1 = translator.CompileHLSLToDXBC(hlsl, "PSMain", "ps_5_0", code1, err);
    TEST_ASSERT(ok1 && !code1.empty(), "First compilation must succeed");
    TEST_ASSERT(cache.GetMisses() >= 1, "First compilation must register a cache miss");
    TEST_ASSERT(cache.GetWrites() >= 1, "First compilation must write to disk cache");

    uint64_t initialHits = cache.GetHits();

    // 2. Second compilation: Must be a Cache Hit
    std::vector<uint8_t> code2;
    bool ok2 = translator.CompileHLSLToDXBC(hlsl, "PSMain", "ps_5_0", code2, err);
    TEST_ASSERT(ok2 && !code2.empty(), "Second compilation must succeed");
    TEST_ASSERT(cache.GetHits() == initialHits + 1, "Second compilation must register a cache hit");
    TEST_ASSERT(code1 == code2, "Cached bytecode must match originally compiled bytecode exactly");

    // 3. Corruption test: Tamper with cache file on disk
    uint32_t compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    uint64_t key = ShaderDiskCache::ComputeShaderKey(hlsl, "PSMain", "ps_5_0", compileFlags);
    std::ostringstream ss;
    ss << testCacheDir << "\\" << std::hex << std::setw(16) << std::setfill('0') << key << ".dxbc";
    std::string filePath = ss.str();

    // Corrupt payload byte
    {
        std::fstream f(filePath, std::ios::in | std::ios::out | std::ios::binary);
        if (f.is_open()) {
            f.seekp(sizeof(ShaderCacheHeader) + 10);
            char badByte = 0xFF;
            f.write(&badByte, 1);
            f.close();
        }
    }

    // Attempt retrieval: must detect corruption, reject payload, and delete file
    std::vector<uint8_t> codeCorrupted;
    bool hitCorrupted = cache.TryGet(key, compileFlags, codeCorrupted);
    TEST_ASSERT(!hitCorrupted, "Corrupted cache file must be rejected");
    TEST_ASSERT(codeCorrupted.empty(), "Corrupted payload must not be returned");
    TEST_ASSERT(GetFileAttributesA(filePath.c_str()) == INVALID_FILE_ATTRIBUTES, "Corrupted file must be deleted");

    std::cout << "[PASS] TestShaderDiskCacheHitMissAndValidation" << std::endl;
    return true;
}

bool TestShaderDiskCacheSaltAndCompilerFlagsInvalidation() {
    using namespace kepler;
    auto& cache = ShaderDiskCache::Instance();

    const std::string hlsl = "float4 PSMain() : SV_Target { return float4(1,1,1,1); }";

    uint32_t flagsA = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    uint32_t flagsB = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_SKIP_OPTIMIZATION;

    uint64_t keyA = ShaderDiskCache::ComputeShaderKey(hlsl, "PSMain", "ps_5_0", flagsA, 47);
    uint64_t keyB = ShaderDiskCache::ComputeShaderKey(hlsl, "PSMain", "ps_5_0", flagsB, 47);
    uint64_t keySalt = ShaderDiskCache::ComputeShaderKey(hlsl, "PSMain", "ps_5_0", flagsA, 48);

    TEST_ASSERT(keyA != keyB, "Different compiler flags must produce distinct cache keys");
    TEST_ASSERT(keyA != keySalt, "Different compiler version salts must produce distinct cache keys");

    // Test Put and TryGet directly with simulated DXBC bytecode
    std::vector<uint8_t> syntheticDXBC = { 'D', 'X', 'B', 'C', 0x01, 0x02, 0x03, 0x04 };
    bool putOk = cache.Put(keyA, flagsA, syntheticDXBC.data(), syntheticDXBC.size());
    TEST_ASSERT(putOk, "Put to cache must succeed");

    std::vector<uint8_t> retrieved;
    // Querying with wrong flags must miss
    bool getWrongFlags = cache.TryGet(keyA, flagsB, retrieved);
    TEST_ASSERT(!getWrongFlags, "TryGet with mismatched flags must fail");

    // Querying with correct flags must hit
    bool getCorrect = cache.TryGet(keyA, flagsA, retrieved);
    TEST_ASSERT(getCorrect, "TryGet with matching flags must succeed");
    TEST_ASSERT(retrieved == syntheticDXBC, "Retrieved bytecode must match stored bytecode");

    // Cleanup test file
    std::ostringstream ssCleanup;
    ssCleanup << cache.GetCacheDirectory() << "\\" << std::hex << std::setw(16) << std::setfill('0') << keyA << ".dxbc";
    DeleteFileA(ssCleanup.str().c_str());
    RemoveDirectoryA(cache.GetCacheDirectory().c_str());

    std::cout << "[PASS] TestShaderDiskCacheSaltAndCompilerFlagsInvalidation" << std::endl;
    return true;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  KeplerBridge Phase 6/6.1 Shader Compatibility & Cache \n";
    std::cout << "========================================================\n";

    bool allOk = true;
    allOk &= TestDirectCompilerCompilation();
    allOk &= TestPhysicalD3D11RuntimeVerification();
    allOk &= TestUnsupportedStagesRejection();
    allOk &= TestShaderPolicyIntegrationWithFallback();
    allOk &= TestHonestFailureOnUnsupportedDXIL();
    allOk &= TestShaderDiskCacheHitMissAndValidation();
    allOk &= TestShaderDiskCacheSaltAndCompilerFlagsInvalidation();

    if (allOk) {
        std::cout << "========================================================\n";
        std::cout << "  ALL PHASE 6/6.1 SHADER COMPATIBILITY TESTS PASSED (7/7)!\n";
        std::cout << "========================================================\n";
        return 0;
    }
    return 1;
}
