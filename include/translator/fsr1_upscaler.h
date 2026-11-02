#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <cstdint>
#include <vector>
#include <string>

namespace kepler {

enum class FSR1QualityPreset {
    UltraQuality, // 1.30x
    Quality,      // 1.50x
    Balanced,     // 1.70x
    Performance,  // 2.00x
    Custom
};

struct FsrEasuConstants {
    uint32_t con0[4]; // Input size, inverse input size
    uint32_t con1[4]; // Output size, inverse output size
    uint32_t con2[4]; // Sampling constants
    uint32_t con3[4]; // Clipping / edge constants
};

struct FsrRcasConstants {
    uint32_t con0[4]; // Sharpness parameter
};

class FSR1Upscaler {
public:
    static FSR1Upscaler& Instance();

    // Constant calculation per AMD FidelityFX FSR 1.0 specification
    static void SetupEasuConstants(
        uint32_t inputViewportWidth,
        uint32_t inputViewportHeight,
        uint32_t inputImageWidth,
        uint32_t inputImageHeight,
        uint32_t outputWidth,
        uint32_t outputHeight,
        FsrEasuConstants& outConstants);

    static void SetupRcasConstants(
        float sharpness, // 0.0f (no sharpening) to 1.0f (maximum sharpness)
        FsrRcasConstants& outConstants);

    static float GetScaleRatioForPreset(FSR1QualityPreset preset);

    // Mathematical CPU reference implementation for correctness testing & verification
    static void ExecuteEasuCpuReference(
        const uint32_t* pSrcRgba,
        uint32_t srcW,
        uint32_t srcH,
        uint32_t* pDstRgba,
        uint32_t dstW,
        uint32_t dstH);

    static void ExecuteRcasCpuReference(
        const uint32_t* pSrcRgba,
        uint32_t width,
        uint32_t height,
        float sharpness,
        uint32_t* pDstRgba);

    // D3D11 GPU compute shader dispatch path
    bool InitializeD3D11(ID3D11Device* pDevice);
    void ShutdownD3D11();

    bool UpscaleAndSharpen(
        ID3D11DeviceContext* pContext,
        ID3D11ShaderResourceView* pInputSRV,
        ID3D11UnorderedAccessView* pOutputUAV,
        uint32_t inW,
        uint32_t inH,
        uint32_t outW,
        uint32_t outH,
        float sharpness);

    bool IsInitialized() const { return m_initialized; }

private:
    FSR1Upscaler();
    ~FSR1Upscaler();
    FSR1Upscaler(const FSR1Upscaler&) = delete;
    FSR1Upscaler& operator=(const FSR1Upscaler&) = delete;

    bool m_initialized = false;
    ID3D11ComputeShader* m_pEasuCS = nullptr;
    ID3D11ComputeShader* m_pRcasCS = nullptr;
    ID3D11Buffer* m_pEasuCB = nullptr;
    ID3D11Buffer* m_pRcasCB = nullptr;
    ID3D11Texture2D* m_pIntermediateTex = nullptr;
    ID3D11ShaderResourceView* m_pIntermediateSRV = nullptr;
    ID3D11UnorderedAccessView* m_pIntermediateUAV = nullptr;
    uint32_t m_intermediateW = 0;
    uint32_t m_intermediateH = 0;
};

} // namespace kepler
