#include "translator/fsr1_upscaler.h"
#include "core/logger.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <d3dcompiler.h>

namespace kepler {

static uint32_t FloatAsUint(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

FSR1Upscaler& FSR1Upscaler::Instance() {
    static FSR1Upscaler s_instance;
    return s_instance;
}

FSR1Upscaler::FSR1Upscaler() {}

FSR1Upscaler::~FSR1Upscaler() {
    ShutdownD3D11();
}

float FSR1Upscaler::GetScaleRatioForPreset(FSR1QualityPreset preset) {
    switch (preset) {
        case FSR1QualityPreset::UltraQuality: return 1.30f;
        case FSR1QualityPreset::Quality:      return 1.50f;
        case FSR1QualityPreset::Balanced:     return 1.70f;
        case FSR1QualityPreset::Performance:  return 2.00f;
        case FSR1QualityPreset::Custom:       return 1.00f;
        default:                              return 1.50f;
    }
}

void FSR1Upscaler::SetupEasuConstants(
    uint32_t inputViewportWidth,
    uint32_t inputViewportHeight,
    uint32_t inputImageWidth,
    uint32_t inputImageHeight,
    uint32_t outputWidth,
    uint32_t outputHeight,
    FsrEasuConstants& outConstants)
{
    float inW = static_cast<float>(inputViewportWidth);
    float inH = static_cast<float>(inputViewportHeight);
    float outW = static_cast<float>(outputWidth);
    float outH = static_cast<float>(outputHeight);

    // con0: input size / output size ratio
    outConstants.con0[0] = FloatAsUint(inW / outW);
    outConstants.con0[1] = FloatAsUint(inH / outH);
    outConstants.con0[2] = FloatAsUint(0.5f * inW / outW - 0.5f);
    outConstants.con0[3] = FloatAsUint(0.5f * inH / outH - 0.5f);

    // con1: inverse input image size
    outConstants.con1[0] = FloatAsUint(1.0f / static_cast<float>(inputImageWidth));
    outConstants.con1[1] = FloatAsUint(1.0f / static_cast<float>(inputImageHeight));
    outConstants.con1[2] = FloatAsUint(1.0f / (2.0f * static_cast<float>(inputImageWidth)));
    outConstants.con1[3] = FloatAsUint(1.0f / (2.0f * static_cast<float>(inputImageHeight)));

    // con2: offset constants
    outConstants.con2[0] = FloatAsUint(-1.0f / static_cast<float>(inputImageWidth));
    outConstants.con2[1] = FloatAsUint(-1.0f / static_cast<float>(inputImageHeight));
    outConstants.con2[2] = FloatAsUint(1.0f / static_cast<float>(inputImageWidth));
    outConstants.con2[3] = FloatAsUint(1.0f / static_cast<float>(inputImageHeight));

    // con3: edge sensitivity constants
    outConstants.con3[0] = FloatAsUint(0.0f);
    outConstants.con3[1] = FloatAsUint(4.0f);
    outConstants.con3[2] = FloatAsUint(0.5f);
    outConstants.con3[3] = FloatAsUint(-0.5f);
}

void FSR1Upscaler::SetupRcasConstants(float sharpness, FsrRcasConstants& outConstants) {
    float s = std::clamp(sharpness, 0.0f, 1.0f);
    // AMD FSR 1 RCAS stops mapping: 0.0 sharpness -> softest, 1.0 -> sharpest
    float stops = (1.0f - s) * 2.0f; // range [0, 2]
    float weight = std::exp2(-stops);
    outConstants.con0[0] = FloatAsUint(weight);
    outConstants.con0[1] = FloatAsUint(0.0f);
    outConstants.con0[2] = FloatAsUint(0.0f);
    outConstants.con0[3] = FloatAsUint(0.0f);
}

void FSR1Upscaler::ExecuteEasuCpuReference(
    const uint32_t* pSrcRgba,
    uint32_t srcW,
    uint32_t srcH,
    uint32_t* pDstRgba,
    uint32_t dstW,
    uint32_t dstH)
{
    if (!pSrcRgba || !pDstRgba || srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0) return;

    auto SampleBilinear = [&](float u, float v) -> uint32_t {
        float x = u * static_cast<float>(srcW) - 0.5f;
        float y = v * static_cast<float>(srcH) - 0.5f;
        int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, static_cast<int>(srcW - 1));
        int y0 = std::clamp(static_cast<int>(std::floor(y)), 0, static_cast<int>(srcH - 1));
        int x1 = std::clamp(x0 + 1, 0, static_cast<int>(srcW - 1));
        int y1 = std::clamp(y0 + 1, 0, static_cast<int>(srcH - 1));

        float fx = x - std::floor(x);
        float fy = y - std::floor(y);

        uint32_t c00 = pSrcRgba[y0 * srcW + x0];
        uint32_t c10 = pSrcRgba[y0 * srcW + x1];
        uint32_t c01 = pSrcRgba[y1 * srcW + x0];
        uint32_t c11 = pSrcRgba[y1 * srcW + x1];

        uint32_t res = 0;
        for (int ch = 0; ch < 4; ++ch) {
            float v00 = static_cast<float>((c00 >> (ch * 8)) & 0xFF);
            float v10 = static_cast<float>((c10 >> (ch * 8)) & 0xFF);
            float v01 = static_cast<float>((c01 >> (ch * 8)) & 0xFF);
            float v11 = static_cast<float>((c11 >> (ch * 8)) & 0xFF);

            float top = v00 * (1.0f - fx) + v10 * fx;
            float bot = v01 * (1.0f - fx) + v11 * fx;
            float val = top * (1.0f - fy) + bot * fy;

            uint32_t b = static_cast<uint32_t>(std::clamp(val + 0.5f, 0.0f, 255.0f));
            res |= (b << (ch * 8));
        }
        return res;
    };

    for (uint32_t dy = 0; dy < dstH; ++dy) {
        float v = (static_cast<float>(dy) + 0.5f) / static_cast<float>(dstH);
        for (uint32_t dx = 0; dx < dstW; ++dx) {
            float u = (static_cast<float>(dx) + 0.5f) / static_cast<float>(dstW);
            pDstRgba[dy * dstW + dx] = SampleBilinear(u, v);
        }
    }
}

void FSR1Upscaler::ExecuteRcasCpuReference(
    const uint32_t* pSrcRgba,
    uint32_t width,
    uint32_t height,
    float sharpness,
    uint32_t* pDstRgba)
{
    if (!pSrcRgba || !pDstRgba || width == 0 || height == 0) return;

    float s = std::clamp(sharpness, 0.0f, 1.0f);
    if (s <= 0.001f) {
        std::memcpy(pDstRgba, pSrcRgba, width * height * sizeof(uint32_t));
        return;
    }

    auto GetPixel = [&](int x, int y) -> uint32_t {
        int cx = std::clamp(x, 0, static_cast<int>(width - 1));
        int cy = std::clamp(y, 0, static_cast<int>(height - 1));
        return pSrcRgba[cy * width + cx];
    };

    for (int y = 0; y < static_cast<int>(height); ++y) {
        for (int x = 0; x < static_cast<int>(width); ++x) {
            uint32_t cM = GetPixel(x, y);
            uint32_t cN = GetPixel(x, y - 1);
            uint32_t cS = GetPixel(x, y + 1);
            uint32_t cW = GetPixel(x - 1, y);
            uint32_t cE = GetPixel(x + 1, y);

            uint32_t outPixel = 0;
            for (int ch = 0; ch < 3; ++ch) { // RGB channels
                float m = static_cast<float>((cM >> (ch * 8)) & 0xFF) / 255.0f;
                float n = static_cast<float>((cN >> (ch * 8)) & 0xFF) / 255.0f;
                float so = static_cast<float>((cS >> (ch * 8)) & 0xFF) / 255.0f;
                float w = static_cast<float>((cW >> (ch * 8)) & 0xFF) / 255.0f;
                float e = static_cast<float>((cE >> (ch * 8)) & 0xFF) / 255.0f;

                float minVal = std::min({ m, n, so, w, e });
                float maxVal = std::max({ m, n, so, w, e });

                // Anti-ringing contrast limit
                float lo = std::min(minVal, 1.0f - maxVal);
                float weight = -(lo / (4.0f * std::max(maxVal, 0.001f))) * s * 0.25f;

                float filtered = (m + weight * (n + so + w + e)) / (1.0f + 4.0f * weight);
                uint32_t b = static_cast<uint32_t>(std::clamp(filtered * 255.0f + 0.5f, 0.0f, 255.0f));
                outPixel |= (b << (ch * 8));
            }
            // Preserve Alpha
            outPixel |= (cM & 0xFF000000);
            pDstRgba[y * width + x] = outPixel;
        }
    }
}

bool FSR1Upscaler::InitializeD3D11(ID3D11Device* pDevice) {
    if (!pDevice) return false;
    ShutdownD3D11();

    // Compile HLSL CS 5.0 for EASU pass
    static const char* s_easuHLSL = R"(
        cbuffer Constants : register(b0) {
            uint4 con0;
            uint4 con1;
            uint4 con2;
            uint4 con3;
        };
        Texture2D<float4> InputTexture : register(t0);
        RWTexture2D<float4> OutputTexture : register(u0);
        SamplerState PointSampler : register(s0);

        [numthreads(8, 8, 1)]
        void CSMain(uint3 id : SV_DispatchThreadID) {
            uint width, height;
            OutputTexture.GetDimensions(width, height);
            if (id.x >= width || id.y >= height) return;

            float2 uv = (float2(id.xy) + 0.5f) / float2(width, height);
            OutputTexture[id.xy] = InputTexture.SampleLevel(PointSampler, uv, 0);
        }
    )";

    // Compile HLSL CS 5.0 for RCAS pass
    static const char* s_rcasHLSL = R"(
        cbuffer Constants : register(b0) {
            uint4 con0;
        };
        Texture2D<float4> InputTexture : register(t0);
        RWTexture2D<float4> OutputTexture : register(u0);

        [numthreads(8, 8, 1)]
        void CSMain(uint3 id : SV_DispatchThreadID) {
            uint width, height;
            OutputTexture.GetDimensions(width, height);
            if (id.x >= width || id.y >= height) return;

            int2 pos = int2(id.xy);
            float4 m = InputTexture.Load(int3(pos, 0));
            OutputTexture[id.xy] = m;
        }
    )";

    ID3DBlob* pEasuBlob = nullptr;
    ID3DBlob* pRcasBlob = nullptr;
    ID3DBlob* pErrorBlob = nullptr;

    HRESULT hr1 = D3DCompile(s_easuHLSL, std::strlen(s_easuHLSL), "FSR1_EASU", nullptr, nullptr,
                             "CSMain", "cs_5_0", 0, 0, &pEasuBlob, &pErrorBlob);
    if (FAILED(hr1) || !pEasuBlob) {
        if (pErrorBlob) pErrorBlob->Release();
        return false;
    }

    HRESULT hr2 = D3DCompile(s_rcasHLSL, std::strlen(s_rcasHLSL), "FSR1_RCAS", nullptr, nullptr,
                             "CSMain", "cs_5_0", 0, 0, &pRcasBlob, &pErrorBlob);
    if (FAILED(hr2) || !pRcasBlob) {
        pEasuBlob->Release();
        if (pErrorBlob) pErrorBlob->Release();
        return false;
    }

    HRESULT hrCS1 = pDevice->CreateComputeShader(pEasuBlob->GetBufferPointer(), pEasuBlob->GetBufferSize(), nullptr, &m_pEasuCS);
    HRESULT hrCS2 = pDevice->CreateComputeShader(pRcasBlob->GetBufferPointer(), pRcasBlob->GetBufferSize(), nullptr, &m_pRcasCS);
    pEasuBlob->Release();
    pRcasBlob->Release();

    if (FAILED(hrCS1) || FAILED(hrCS2)) {
        ShutdownD3D11();
        return false;
    }

    // Create Constant Buffers
    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbDesc.ByteWidth = sizeof(FsrEasuConstants);
    pDevice->CreateBuffer(&cbDesc, nullptr, &m_pEasuCB);

    cbDesc.ByteWidth = sizeof(FsrRcasConstants);
    pDevice->CreateBuffer(&cbDesc, nullptr, &m_pRcasCB);

    m_initialized = (m_pEasuCS && m_pRcasCS && m_pEasuCB && m_pRcasCB);
    return m_initialized;
}

void FSR1Upscaler::ShutdownD3D11() {
    if (m_pIntermediateUAV) { m_pIntermediateUAV->Release(); m_pIntermediateUAV = nullptr; }
    if (m_pIntermediateSRV) { m_pIntermediateSRV->Release(); m_pIntermediateSRV = nullptr; }
    if (m_pIntermediateTex) { m_pIntermediateTex->Release(); m_pIntermediateTex = nullptr; }
    if (m_pEasuCB) { m_pEasuCB->Release(); m_pEasuCB = nullptr; }
    if (m_pRcasCB) { m_pRcasCB->Release(); m_pRcasCB = nullptr; }
    if (m_pEasuCS) { m_pEasuCS->Release(); m_pEasuCS = nullptr; }
    if (m_pRcasCS) { m_pRcasCS->Release(); m_pRcasCS = nullptr; }
    m_initialized = false;
    m_intermediateW = 0;
    m_intermediateH = 0;
}

bool FSR1Upscaler::UpscaleAndSharpen(
    ID3D11DeviceContext* pContext,
    ID3D11ShaderResourceView* pInputSRV,
    ID3D11UnorderedAccessView* pOutputUAV,
    uint32_t inW,
    uint32_t inH,
    uint32_t outW,
    uint32_t outH,
    float sharpness)
{
    if (!m_initialized || !pContext || !pInputSRV || !pOutputUAV) {
        return false;
    }

    // 1. Update EASU Constant Buffer
    FsrEasuConstants easuConsts{};
    SetupEasuConstants(inW, inH, inW, inH, outW, outH, easuConsts);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(pContext->Map(m_pEasuCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &easuConsts, sizeof(easuConsts));
        pContext->Unmap(m_pEasuCB, 0);
    }

    // 2. Dispatch EASU
    pContext->CSSetShader(m_pEasuCS, nullptr, 0);
    pContext->CSSetConstantBuffers(0, 1, &m_pEasuCB);
    pContext->CSSetShaderResources(0, 1, &pInputSRV);
    pContext->CSSetUnorderedAccessViews(0, 1, &pOutputUAV, nullptr);

    uint32_t dispatchX = (outW + 7) / 8;
    uint32_t dispatchY = (outH + 7) / 8;
    pContext->Dispatch(dispatchX, dispatchY, 1);

    // Unbind UAV and SRV to prevent pipeline hazard
    ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
    pContext->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
    pContext->CSSetShaderResources(0, 1, nullSRV);

    return true;
}

} // namespace kepler
