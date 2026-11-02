#include "translator/fsr1_upscaler.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>
#include <cstring>

using namespace kepler;

int main() {
    std::cout << "========================================================\n";
    std::cout << "   KeplerBridge Milestone 8: FSR 1 Upscaler Test Suite  \n";
    std::cout << "========================================================\n" << std::endl;

    // 1. Quality Presets and Scale Ratios
    std::cout << "[TEST 1] Verifying AMD FSR 1 Quality Presets..." << std::endl;
    assert(std::fabs(FSR1Upscaler::GetScaleRatioForPreset(FSR1QualityPreset::UltraQuality) - 1.30f) < 0.001f);
    assert(std::fabs(FSR1Upscaler::GetScaleRatioForPreset(FSR1QualityPreset::Quality) - 1.50f) < 0.001f);
    assert(std::fabs(FSR1Upscaler::GetScaleRatioForPreset(FSR1QualityPreset::Balanced) - 1.70f) < 0.001f);
    assert(std::fabs(FSR1Upscaler::GetScaleRatioForPreset(FSR1QualityPreset::Performance) - 2.00f) < 0.001f);
    std::cout << "  [PASS] Presets verified: UQ (1.30x), Q (1.50x), B (1.70x), P (2.00x)." << std::endl;

    // 2. Constants Setup Calculation
    std::cout << "\n[TEST 2] Testing EASU and RCAS constant buffer setup..." << std::endl;
    FsrEasuConstants easuConsts{};
    FSR1Upscaler::SetupEasuConstants(1280, 720, 1280, 720, 1920, 1080, easuConsts);
    // Ratio 1280/1920 = 0.666667
    float ratioX;
    std::memcpy(&ratioX, &easuConsts.con0[0], sizeof(float));
    assert(std::fabs(ratioX - (1280.0f / 1920.0f)) < 0.001f);

    FsrRcasConstants rcasConsts{};
    FSR1Upscaler::SetupRcasConstants(0.8f, rcasConsts);
    float rcasWeight;
    std::memcpy(&rcasWeight, &rcasConsts.con0[0], sizeof(float));
    assert(rcasWeight > 0.0f);
    std::cout << "  [PASS] EASU ratioX = " << ratioX << ", RCAS weight = " << rcasWeight << std::endl;

    // 3. Mathematical Reference Image Test (CPU EASU + RCAS)
    std::cout << "\n[TEST 3] Testing mathematical CPU EASU + RCAS image processing..." << std::endl;
    uint32_t srcW = 32, srcH = 32;
    uint32_t dstW = 64, dstH = 64;
    std::vector<uint32_t> srcImg(srcW * srcH, 0);

    // Create a high-contrast pattern (cross)
    for (uint32_t y = 0; y < srcH; ++y) {
        for (uint32_t x = 0; x < srcW; ++x) {
            if (x == srcW / 2 || y == srcH / 2) {
                srcImg[y * srcW + x] = 0xFFFFFFFF; // White
            } else {
                srcImg[y * srcW + x] = 0xFF000000; // Black
            }
        }
    }

    std::vector<uint32_t> easuOutput(dstW * dstH, 0);
    FSR1Upscaler::ExecuteEasuCpuReference(srcImg.data(), srcW, srcH, easuOutput.data(), dstW, dstH);

    // Verify center pixel is reconstructed
    uint32_t centerPixel = easuOutput[(dstH / 2) * dstW + (dstW / 2)];
    assert((centerPixel & 0x00FFFFFF) != 0);

    // Apply RCAS Sharpening
    std::vector<uint32_t> rcasOutput(dstW * dstH, 0);
    FSR1Upscaler::ExecuteRcasCpuReference(easuOutput.data(), dstW, dstH, 0.8f, rcasOutput.data());
    uint32_t sharpenedCenter = rcasOutput[(dstH / 2) * dstW + (dstW / 2)];
    assert((sharpenedCenter & 0x00FFFFFF) != 0);
    std::cout << "  [PASS] CPU EASU upscaling (32x32 -> 64x64) and RCAS sharpening verified." << std::endl;

    // 4. Live D3D11 GPU Execution Test
    std::cout << "\n[TEST 4] Live D3D11 Hardware Compute Shader Dispatch..." << std::endl;
    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pContext = nullptr;
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL obtainedLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevels,
        1,
        D3D11_SDK_VERSION,
        &pDevice,
        &obtainedLevel,
        &pContext
    );

    if (SUCCEEDED(hr) && pDevice && pContext) {
        auto& upscaler = FSR1Upscaler::Instance();
        bool initOk = upscaler.InitializeD3D11(pDevice);
        assert(initOk);

        // Create Input Texture (64x64)
        D3D11_TEXTURE2D_DESC inDesc{};
        inDesc.Width = 64;
        inDesc.Height = 64;
        inDesc.MipLevels = 1;
        inDesc.ArraySize = 1;
        inDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        inDesc.SampleDesc.Count = 1;
        inDesc.Usage = D3D11_USAGE_DEFAULT;
        inDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        std::vector<uint32_t> initData(64 * 64, 0xFF00FF00); // Solid green
        D3D11_SUBRESOURCE_DATA subData{};
        subData.pSysMem = initData.data();
        subData.SysMemPitch = 64 * sizeof(uint32_t);

        ID3D11Texture2D* pInTex = nullptr;
        pDevice->CreateTexture2D(&inDesc, &subData, &pInTex);
        ID3D11ShaderResourceView* pInSRV = nullptr;
        pDevice->CreateShaderResourceView(pInTex, nullptr, &pInSRV);

        // Create Output Texture (128x128)
        D3D11_TEXTURE2D_DESC outDesc = inDesc;
        outDesc.Width = 128;
        outDesc.Height = 128;
        outDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;

        ID3D11Texture2D* pOutTex = nullptr;
        pDevice->CreateTexture2D(&outDesc, nullptr, &pOutTex);
        ID3D11UnorderedAccessView* pOutUAV = nullptr;
        pDevice->CreateUnorderedAccessView(pOutTex, nullptr, &pOutUAV);

        // Dispatch Upscale
        bool upscaleOk = upscaler.UpscaleAndSharpen(pContext, pInSRV, pOutUAV, 64, 64, 128, 128, 0.8f);
        assert(upscaleOk);

        // Create Staging Texture to read back
        D3D11_TEXTURE2D_DESC stageDesc = outDesc;
        stageDesc.BindFlags = 0;
        stageDesc.Usage = D3D11_USAGE_STAGING;
        stageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        ID3D11Texture2D* pStageTex = nullptr;
        pDevice->CreateTexture2D(&stageDesc, nullptr, &pStageTex);
        pContext->CopyResource(pStageTex, pOutTex);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT mapHr = pContext->Map(pStageTex, 0, D3D11_MAP_READ, 0, &mapped);
        assert(SUCCEEDED(mapHr));
        const uint32_t* pPixels = reinterpret_cast<const uint32_t*>(mapped.pData);
        uint32_t sample = pPixels[0];
        pContext->Unmap(pStageTex, 0);

        // Green channel should be non-zero
        assert((sample & 0x0000FF00) != 0);

        pStageTex->Release();
        pOutUAV->Release();
        pOutTex->Release();
        pInSRV->Release();
        pInTex->Release();
        upscaler.ShutdownD3D11();
        pContext->Release();
        pDevice->Release();
        std::cout << "  [PASS] Live D3D11 GPU compute shader dispatch verified on hardware.\n" << std::endl;
    } else {
        std::cout << "  [SKIP] D3D11 device creation skipped or hardware unavailable.\n" << std::endl;
    }

    std::cout << "========================================================\n";
    std::cout << "   ALL FSR 1 UPSCALER TESTS PASSED!                     \n";
    std::cout << "========================================================\n" << std::endl;
    return 0;
}
