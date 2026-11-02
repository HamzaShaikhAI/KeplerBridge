#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d11.h>

#include "downgrader/sm51_lowering.h"
#include "downgrader/dxbc_checksum.h"
#include "downgrader/bounded_shader_translator.h"
#include "downgrader/dxil_inspector.h"

using namespace kepler;

static int g_passCount = 0;
static int g_failCount = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] FAIL: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            g_failCount++; \
            return; \
        } else { \
            std::cout << "[+] PASS: " << msg << std::endl; \
            g_passCount++; \
        } \
    } while (0)

// Helper to compile HLSL
static std::vector<uint8_t> CompileHLSL(const char* hlsl, const char* entry, const char* profile) {
    HMODULE hComp = LoadLibraryA("d3dcompiler_47.dll");
    if (!hComp) return {};
    pD3DCompile pfn = (pD3DCompile)GetProcAddress(hComp, "D3DCompile");
    if (!pfn) return {};

    ID3DBlob* pCode = nullptr;
    ID3DBlob* pErr = nullptr;
    HRESULT hr = pfn(hlsl, strlen(hlsl), nullptr, nullptr, nullptr, entry, profile, 0, 0, &pCode, &pErr);
    if (FAILED(hr) || !pCode) {
        if (pErr) pErr->Release();
        return {};
    }
    if (pErr) pErr->Release();

    std::vector<uint8_t> result((uint8_t*)pCode->GetBufferPointer(), (uint8_t*)pCode->GetBufferPointer() + pCode->GetBufferSize());
    pCode->Release();
    return result;
}

// Test 1: Authentic DXBC Checksum algorithm verification
void TestDXBCChecksum() {
    std::cout << "\n=== Test 1: Authentic DXBC Checksum Verification ===" << std::endl;

    const char* hlsl = "float4 main(float4 pos : SV_Position) : SV_Target { return pos; }";
    auto bytecode = CompileHLSL(hlsl, "main", "ps_5_0");
    TEST_ASSERT(!bytecode.empty(), "Compiled reference ps_5_0 shader");

    // Extract original checksum from header
    uint32_t origHash[4];
    memcpy(origHash, bytecode.data() + 4, 16);

    // Compute checksum over buffer
    uint32_t computedHash[4] = { 0 };
    bool ok = CalculateDXBCChecksum(bytecode.data(), bytecode.size(), computedHash);
    TEST_ASSERT(ok, "CalculateDXBCChecksum succeeded");
    TEST_ASSERT(memcmp(origHash, computedHash, 16) == 0, "Computed checksum matches Microsoft compiler bit-for-bit");
}

// Test 2: Lowering of synthetic SM 5.1 Vertex Shader
void TestLowerVertexShaderSM51() {
    std::cout << "\n=== Test 2: Lowering SM 5.1 Vertex Shader with CBV & SRV ===" << std::endl;

    const char* hlsl =
        "cbuffer CB0 : register(b0) { float4 g_offset; };\n"
        "Texture2D g_tex : register(t0);\n"
        "SamplerState g_sam : register(s0);\n"
        "struct VS_OUT { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
        "VS_OUT main(float4 pos : POSITION, float2 uv : TEXCOORD0) {\n"
        "    VS_OUT o;\n"
        "    o.pos = pos + g_offset;\n"
        "    o.uv = uv;\n"
        "    return o;\n"
        "}\n";

    auto bytecode51 = CompileHLSL(hlsl, "main", "vs_5_1");
    TEST_ASSERT(!bytecode51.empty(), "Compiled SM 5.1 vertex shader");

    SM51LoweringReport rep;
    bool canLower = SM51LoweringEngine::CanLower(bytecode51.data(), bytecode51.size(), &rep);
    TEST_ASSERT(canLower, "CanLower returned true for space0 SM 5.1 vertex shader");
    TEST_ASSERT(rep.declaredCBVs >= 1, "Detected declared constant buffer");

    std::vector<uint8_t> lowered;
    bool lowerOk = SM51LoweringEngine::LowerSM51ToSM50(bytecode51.data(), bytecode51.size(), lowered, &rep);
    TEST_ASSERT(lowerOk, "LowerSM51ToSM50 succeeded");
    TEST_ASSERT(rep.status == SM51LoweringStatus::Success, "Lowering status is Success");

    // Inspect lowered bytecode
    ShaderInspectionResult insp = DxilInspector::InspectShader(lowered.data(), lowered.size());
    TEST_ASSERT(insp.isValidContainer, "Lowered container is valid DXBC");
    TEST_ASSERT(insp.majorVersion == 5 && insp.minorVersion == 0, "Lowered version is authentic SM 5.0");
    TEST_ASSERT(insp.stage == ShaderStage::Vertex, "Shader stage preserved as Vertex");
    TEST_ASSERT(insp.isSupportedOnKepler, "Lowered shader marked supported on Kepler");

    // Verify D3D11 runtime acceptance
    ID3D11Device* pDev = nullptr;
    ID3D11DeviceContext* pCtx = nullptr;
    D3D_FEATURE_LEVEL fl;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1, D3D11_SDK_VERSION, &pDev, &fl, &pCtx);
    if (SUCCEEDED(hr) && pDev) {
        ID3D11VertexShader* pVS = nullptr;
        hr = pDev->CreateVertexShader(lowered.data(), lowered.size(), nullptr, &pVS);
        TEST_ASSERT(SUCCEEDED(hr), "D3D11 CreateVertexShader succeeded with S_OK");
        if (pVS) pVS->Release();
        pDev->Release();
        pCtx->Release();
    }
}

// Test 3: Lowering SM 5.1 Pixel Shader with typed UAV store (Night Raid transmittance pattern)
void TestLowerPixelShaderSM51() {
    std::cout << "\n=== Test 3: Lowering SM 5.1 Pixel Shader with UAV Store ===" << std::endl;

    const char* hlsl =
        "Texture3D<float3> g_tex : register(t0);\n"
        "RWTexture3D<float3> g_uav : register(u0);\n"
        "void main(float4 pos : SV_Position) {\n"
        "    float3 val = g_tex.Load(int4(pos.xy, 0, 0));\n"
        "    g_uav[int3(pos.xyz)] = val * 2.0;\n"
        "}\n";

    auto bytecode51 = CompileHLSL(hlsl, "main", "ps_5_1");
    TEST_ASSERT(!bytecode51.empty(), "Compiled SM 5.1 pixel shader with UAV store");

    std::vector<uint8_t> lowered;
    SM51LoweringReport rep;
    bool ok = SM51LoweringEngine::LowerSM51ToSM50(bytecode51.data(), bytecode51.size(), lowered, &rep);
    TEST_ASSERT(ok, "LowerSM51ToSM50 succeeded for pixel shader with typed UAV");
    std::cout << "DEBUG: declaredSRVs=" << rep.declaredSRVs << ", declaredUAVs=" << rep.declaredUAVs << std::endl;
    TEST_ASSERT(rep.declaredSRVs == 1, "Declared 1 SRV (t0)");
    TEST_ASSERT(rep.declaredUAVs >= 1, "Declared at least 1 UAV");

    // Verify D3D11 runtime acceptance
    ID3D11Device* pDev = nullptr;
    ID3D11DeviceContext* pCtx = nullptr;
    D3D_FEATURE_LEVEL fl;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1, D3D11_SDK_VERSION, &pDev, &fl, &pCtx);
    if (SUCCEEDED(hr) && pDev) {
        ID3D11PixelShader* pPS = nullptr;
        hr = pDev->CreatePixelShader(lowered.data(), lowered.size(), nullptr, &pPS);
        TEST_ASSERT(SUCCEEDED(hr), "D3D11 CreatePixelShader succeeded with S_OK for lowered UAV shader");
        if (pPS) pPS->Release();
        pDev->Release();
        pCtx->Release();
    }
}

// Test 4: Rejection of Multiple Register Spaces (space1 != space0)
void TestRejectMultiSpace() {
    std::cout << "\n=== Test 4: Rejection of Multiple Register Spaces ===" << std::endl;

    const char* hlsl =
        "Texture2D g_tex0 : register(t0, space0);\n"
        "Texture2D g_tex1 : register(t0, space1);\n" // space1 collides with space0:t0
        "SamplerState g_sam : register(s0);\n"
        "float4 main(float2 uv : TEXCOORD0) : SV_Target {\n"
        "    return g_tex0.Sample(g_sam, uv) + g_tex1.Sample(g_sam, uv);\n"
        "}\n";

    auto bytecode = CompileHLSL(hlsl, "main", "ps_5_1");
    TEST_ASSERT(!bytecode.empty(), "Compiled SM 5.1 multi-space shader");

    SM51LoweringReport rep;
    bool canLower = SM51LoweringEngine::CanLower(bytecode.data(), bytecode.size(), &rep);
    TEST_ASSERT(!canLower, "CanLower correctly rejected multi-space shader");
    TEST_ASSERT(rep.status == SM51LoweringStatus::MultiSpaceCollision, "Reported MultiSpaceCollision status");

    std::vector<uint8_t> lowered;
    bool lowerOk = SM51LoweringEngine::LowerSM51ToSM50(bytecode.data(), bytecode.size(), lowered, &rep);
    TEST_ASSERT(!lowerOk, "LowerSM51ToSM50 safely refused to emit invalid flattened bytecode");
}

// Test 5: Rejection of Unbounded Descriptor Arrays
void TestRejectUnboundedDescriptorArrays() {
    std::cout << "\n=== Test 5: Rejection of Unbounded Descriptor Arrays ===" << std::endl;

    const char* hlsl =
        "Texture2D g_tex : register(t0);\n"
        "SamplerState g_sam : register(s0);\n"
        "float4 main(float2 uv : TEXCOORD0) : SV_Target {\n"
        "    return g_tex.Sample(g_sam, uv);\n"
        "}\n";

    auto bytecode = CompileHLSL(hlsl, "main", "ps_5_1");
    TEST_ASSERT(!bytecode.empty(), "Compiled base SM 5.1 shader");

    // Patch rangeUpper in dcl_resource to 0xFFFFFFFF (unbounded)
    for (size_t i = 32; i + 24 < bytecode.size(); i += 4) {
        uint32_t val = *reinterpret_cast<const uint32_t*>(bytecode.data() + i);
        if ((val & 0x7FF) == 0x58 && ((val >> 24) & 0x7F) == 7) {
            // Found dcl_resource in SM 5.1: token at i, op at i+4, regId at i+8, rangeLower at i+12, rangeUpper at i+16
            *reinterpret_cast<uint32_t*>(bytecode.data() + i + 16) = 0xFFFFFFFF;
            break;
        }
    }

    SM51LoweringReport rep;
    bool canLower = SM51LoweringEngine::CanLower(bytecode.data(), bytecode.size(), &rep);
    TEST_ASSERT(!canLower, "CanLower correctly rejected unbounded descriptor array shader");
    TEST_ASSERT(rep.status == SM51LoweringStatus::UnboundedDescriptorRange, "Reported UnboundedDescriptorRange status");
}

// Test 6: Rejection of Register Limits Exceeding D3D11 Feature Level 11_0
void TestRejectExceedingRegisterLimits() {
    std::cout << "\n=== Test 6: Rejection of UAV Register >= 8 on Feature Level 11_0 ===" << std::endl;

    const char* hlsl =
        "RWBuffer<float> g_uav : register(u8, space0);\n" // u8 exceeds FL 11_0 limit (max 8: u0..u7)
        "[numthreads(1,1,1)]\n"
        "void main(uint3 id : SV_DispatchThreadID) {\n"
        "    g_uav[id.x] = 1.0;\n"
        "}\n";

    auto bytecode = CompileHLSL(hlsl, "main", "cs_5_1");
    TEST_ASSERT(!bytecode.empty(), "Compiled compute shader with register u8");

    SM51LoweringReport rep;
    bool canLower = SM51LoweringEngine::CanLower(bytecode.data(), bytecode.size(), &rep);
    TEST_ASSERT(!canLower, "CanLower correctly rejected UAV register u8 exceeding FL 11_0 limits");
    TEST_ASSERT(rep.status == SM51LoweringStatus::ExceedsD3D11BindingLimits, "Reported ExceedsD3D11BindingLimits status");
}

// Test 7: Rejection of Truncated/Corrupted Bytecode
void TestRejectMalformedBytecode() {
    std::cout << "\n=== Test 7: Rejection of Truncated and Malformed Bytecode ===" << std::endl;

    uint8_t garbage[16] = { 0x43, 0x42, 0x58, 0x44, 0 }; // DXBC magic but truncated header
    SM51LoweringReport rep;
    bool canLower = SM51LoweringEngine::CanLower(garbage, sizeof(garbage), &rep);
    TEST_ASSERT(!canLower, "CanLower rejected truncated header");
    TEST_ASSERT(rep.status == SM51LoweringStatus::InvalidContainer, "Reported InvalidContainer");

    bool nullLower = SM51LoweringEngine::CanLower(nullptr, 0, &rep);
    TEST_ASSERT(!nullLower, "CanLower rejected null bytecode pointer");
}

// Test 8: Non-SM 5.1 Bytecode Passthrough / NotSM51
void TestNonSM51Passthrough() {
    std::cout << "\n=== Test 8: Non-SM 5.1 Detection ===" << std::endl;

    const char* hlsl = "float4 main(float4 pos : SV_Position) : SV_Target { return pos; }";
    auto bytecode50 = CompileHLSL(hlsl, "main", "ps_5_0");
    TEST_ASSERT(!bytecode50.empty(), "Compiled SM 5.0 shader");

    SM51LoweringReport rep;
    bool canLower = SM51LoweringEngine::CanLower(bytecode50.data(), bytecode50.size(), &rep);
    TEST_ASSERT(!canLower, "CanLower returned false for SM 5.0 (only lowers SM 5.1)");
    TEST_ASSERT(rep.status == SM51LoweringStatus::NotSM51, "Reported NotSM51 status");
}

// Test 9: Integration with BoundedShaderTranslator
void TestBoundedShaderTranslatorIntegration() {
    std::cout << "\n=== Test 9: BoundedShaderTranslator SM 5.1 Integration ===" << std::endl;

    BoundedShaderTranslator translator;
    const char* hlsl =
        "cbuffer CB0 : register(b0) { float4 g_color; };\n"
        "float4 main() : SV_Target { return g_color; }\n";

    auto bytecode51 = CompileHLSL(hlsl, "main", "ps_5_1");
    TEST_ASSERT(!bytecode51.empty(), "Compiled SM 5.1 pixel shader");

    ShaderInspectionResult insp = DxilInspector::InspectShader(bytecode51.data(), bytecode51.size());
    TEST_ASSERT(!insp.isSupportedOnKepler, "Raw SM 5.1 is not natively supported on Kepler D3D11");
    TEST_ASSERT(translator.CanTranslate(insp), "BoundedShaderTranslator::CanTranslate reports true for SM 5.1");

    std::vector<uint8_t> translated;
    std::string err;
    bool ok = translator.Translate(bytecode51.data(), bytecode51.size(), insp, translated, err);
    TEST_ASSERT(ok, "BoundedShaderTranslator::Translate succeeded");
    TEST_ASSERT(!translated.empty(), "Output translated bytecode is not empty");

    ShaderInspectionResult loweredInsp = DxilInspector::InspectShader(translated.data(), translated.size());
    TEST_ASSERT(loweredInsp.isSupportedOnKepler, "Translated shader is now marked supported on Kepler");
    TEST_ASSERT(loweredInsp.majorVersion == 5 && loweredInsp.minorVersion == 0, "Translated shader is SM 5.0");
}

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << " KeplerBridge Phase 11H — Shader Model 5.1 Lowering Test Suite" << std::endl;
    std::cout << "============================================================" << std::endl;

    TestDXBCChecksum();
    TestLowerVertexShaderSM51();
    TestLowerPixelShaderSM51();
    TestRejectMultiSpace();
    TestRejectUnboundedDescriptorArrays();
    TestRejectExceedingRegisterLimits();
    TestRejectMalformedBytecode();
    TestNonSM51Passthrough();
    TestBoundedShaderTranslatorIntegration();

    std::cout << "\n============================================================" << std::endl;
    std::cout << " SUMMARY: Passed: " << g_passCount << ", Failed: " << g_failCount << std::endl;
    std::cout << "============================================================" << std::endl;

    return (g_failCount == 0) ? 0 : 1;
}
