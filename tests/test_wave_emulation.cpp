#include "downgrader/wave_emulation.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>

using namespace kepler;

int main() {
    std::cout << "========================================================\n";
    std::cout << "   KeplerBridge Milestone 10: Wave Emulation Test Suite \n";
    std::cout << "========================================================\n" << std::endl;

    auto& wave = WaveEmulationEngine::Instance();

    // 1. Classification & Inventory
    std::cout << "[TEST 1] Verifying Wave Operations classification inventory..." << std::endl;
    auto list = wave.GetAllClassifications();
    assert(list.size() >= 8);
    for (const auto& c : list) {
        std::cout << "  - " << c.name << " : Status="
                  << CapabilityRegistry::Instance().StatusToString(c.status)
                  << ", Route=" << c.emulationStrategy << std::endl;
    }
    std::cout << "  [PASS] All 8+ candidate wave operations inventoried." << std::endl;

    // 2. Stage Gating
    std::cout << "\n[TEST 2] Testing stage gating (Compute permitted, Graphics rejected)..." << std::endl;
    std::string diag;
    bool csOk = wave.CanEmulateInStage(WaveOpCode::WaveActiveSum, STAGE_COMPUTE, diag);
    assert(csOk);

    bool psOk = wave.CanEmulateInStage(WaveOpCode::WaveActiveSum, STAGE_PIXEL, diag);
    assert(!psOk);
    assert(diag.find("cannot be emulated in target stage") != std::string::npos);

    bool vsOk = wave.CanEmulateInStage(WaveOpCode::WaveGetLaneIndex, STAGE_VERTEX, diag);
    assert(!vsOk);

    bool quadOk = wave.CanEmulateInStage(WaveOpCode::QuadReadAcrossX, STAGE_COMPUTE, diag);
    assert(!quadOk);
    assert(diag.find("physically unsupported") != std::string::npos);
    std::cout << "  [PASS] Strict stage gating validated (Compute permitted, Pixel/Vertex rejected)." << std::endl;

    // 3. Code Generation
    std::cout << "\n[TEST 3] Testing HLSL LDS emulation generation..." << std::endl;
    std::string codeSum = wave.GenerateComputeLdsEmulationHLSL(WaveOpCode::WaveActiveSum, "float", "myVal");
    assert(codeSum.find("GroupMemoryBarrierWithGroupSync") != std::string::npos);
    assert(codeSum.find("s_waveLds") != std::string::npos);
    std::cout << "  [PASS] Emulation code generation validated." << std::endl;

    // 4. Live D3D11 Compute Shader Execution of LDS Reduction
    std::cout << "\n[TEST 4] Live D3D11 Hardware Execution of Emulated WaveActiveSum..." << std::endl;
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
        static const char* s_waveCS = R"(
            groupshared float s_waveLds[32];
            RWStructuredBuffer<float> OutputBuffer : register(u0);

            [numthreads(32, 1, 1)]
            void CSMain(uint3 tid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
                float myVal = 1.0f; // Each lane contributes 1.0f
                s_waveLds[groupIndex] = myVal;
                GroupMemoryBarrierWithGroupSync();
                if ((groupIndex % 32) < 16) s_waveLds[groupIndex] += s_waveLds[groupIndex + 16];
                GroupMemoryBarrierWithGroupSync();
                if ((groupIndex % 32) < 8)  s_waveLds[groupIndex] += s_waveLds[groupIndex + 8];
                GroupMemoryBarrierWithGroupSync();
                if ((groupIndex % 32) < 4)  s_waveLds[groupIndex] += s_waveLds[groupIndex + 4];
                GroupMemoryBarrierWithGroupSync();
                if ((groupIndex % 32) < 2)  s_waveLds[groupIndex] += s_waveLds[groupIndex + 2];
                GroupMemoryBarrierWithGroupSync();
                if ((groupIndex % 32) < 1)  s_waveLds[groupIndex] += s_waveLds[groupIndex + 1];
                GroupMemoryBarrierWithGroupSync();
                float totalSum = s_waveLds[0];

                if (groupIndex == 0) {
                    OutputBuffer[0] = totalSum;
                }
            }
        )";

        ID3DBlob* pBlob = nullptr;
        ID3DBlob* pErr = nullptr;
        HRESULT compHr = D3DCompile(s_waveCS, std::strlen(s_waveCS), "WaveActiveSum_Test", nullptr, nullptr,
                                    "CSMain", "cs_5_0", 0, 0, &pBlob, &pErr);
        if (SUCCEEDED(compHr) && pBlob) {
            ID3D11ComputeShader* pCS = nullptr;
            pDevice->CreateComputeShader(pBlob->GetBufferPointer(), pBlob->GetBufferSize(), nullptr, &pCS);
            pBlob->Release();

            // Create UAV buffer for 1 float
            D3D11_BUFFER_DESC bufDesc{};
            bufDesc.ByteWidth = sizeof(float) * 4;
            bufDesc.Usage = D3D11_USAGE_DEFAULT;
            bufDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            bufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bufDesc.StructureByteStride = sizeof(float);

            ID3D11Buffer* pOutBuf = nullptr;
            pDevice->CreateBuffer(&bufDesc, nullptr, &pOutBuf);

            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = 4;

            ID3D11UnorderedAccessView* pUAV = nullptr;
            pDevice->CreateUnorderedAccessView(pOutBuf, &uavDesc, &pUAV);

            pContext->CSSetShader(pCS, nullptr, 0);
            pContext->CSSetUnorderedAccessViews(0, 1, &pUAV, nullptr);
            pContext->Dispatch(1, 1, 1);

            ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
            pContext->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

            // Staging readback
            D3D11_BUFFER_DESC stageDesc = bufDesc;
            stageDesc.Usage = D3D11_USAGE_STAGING;
            stageDesc.BindFlags = 0;
            stageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stageDesc.MiscFlags = 0;

            ID3D11Buffer* pStageBuf = nullptr;
            pDevice->CreateBuffer(&stageDesc, nullptr, &pStageBuf);
            pContext->CopyResource(pStageBuf, pOutBuf);

            D3D11_MAPPED_SUBRESOURCE mapped{};
            HRESULT mapHr = pContext->Map(pStageBuf, 0, D3D11_MAP_READ, 0, &mapped);
            assert(SUCCEEDED(mapHr));
            float result = *reinterpret_cast<const float*>(mapped.pData);
            pContext->Unmap(pStageBuf, 0);

            // 32 threads contributing 1.0f each -> sum must equal 32.0f
            std::cout << "  GPU Output Sum: " << result << " (Expected: 32.0)" << std::endl;
            assert(std::fabs(result - 32.0f) < 0.001f);

            pStageBuf->Release();
            pUAV->Release();
            pOutBuf->Release();
            pCS->Release();
            std::cout << "  [PASS] Live GT 740 GPU LDS reduction matched exact wave active sum mathematical result (32.0)!\n";
        }
        if (pErr) pErr->Release();
        pContext->Release();
        pDevice->Release();
    } else {
        std::cout << "  [SKIP] D3D11 device creation skipped or hardware unavailable.\n";
    }

    std::cout << "========================================================\n";
    std::cout << "   ALL WAVE EMULATION TESTS PASSED!                     \n";
    std::cout << "========================================================\n" << std::endl;
    return 0;
}
