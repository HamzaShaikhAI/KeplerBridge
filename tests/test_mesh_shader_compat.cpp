#include "downgrader/mesh_shader_compat.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>

using namespace kepler;

int main() {
    std::cout << "========================================================\n";
    std::cout << "   KeplerBridge Milestone 12: Mesh Shader Compatibility \n";
    std::cout << "========================================================\n" << std::endl;

    auto& engine = MeshShaderCompatEngine::Instance();

    // 1. Prototype Boundary & Rejection Verification
    std::cout << "[TEST 1] Testing mesh shader subset verification & rejections..." << std::endl;
    MeshShaderDesc validDesc{};
    validDesc.topology = "triangle";
    validDesc.maxVertices = 3;
    validDesc.maxPrimitives = 1;
    validDesc.hasAmplificationShader = false;

    std::string diag;
    assert(engine.CanLowerMeshShader(validDesc, diag));

    // Rejection: Amplification Shader
    MeshShaderDesc ampDesc = validDesc;
    ampDesc.hasAmplificationShader = true;
    assert(!engine.CanLowerMeshShader(ampDesc, diag));
    assert(diag.find("Amplification / Task shaders are unsupported") != std::string::npos);
    std::cout << "  [PASS] Amplification shader rejected: " << diag << std::endl;

    // Rejection: Line topology
    MeshShaderDesc lineDesc = validDesc;
    lineDesc.topology = "line";
    assert(!engine.CanLowerMeshShader(lineDesc, diag));
    assert(diag.find("Unsupported mesh primitive topology") != std::string::npos);
    std::cout << "  [PASS] Non-triangle topology rejected: " << diag << std::endl;

    // Rejection: Exceeds max vertices (e.g. 128 > 64)
    MeshShaderDesc largeDesc = validDesc;
    largeDesc.maxVertices = 128;
    assert(!engine.CanLowerMeshShader(largeDesc, diag));
    assert(diag.find("exceeds Kepler bounded prototype limit") != std::string::npos);
    std::cout << "  [PASS] Excess vertex count rejected: " << diag << std::endl;

    // 2. Transformation Pipeline Generation
    std::cout << "\n[TEST 2] Testing transformation code generation..." << std::endl;
    std::string csHlsl, vsHlsl;
    bool genOk = engine.GenerateTransformationPipeline(validDesc, csHlsl, vsHlsl, diag);
    assert(genOk);
    assert(csHlsl.find("RWStructuredBuffer<MeshVertex>") != std::string::npos);
    assert(vsHlsl.find("VSMain") != std::string::npos);
    std::cout << "  [PASS] CS 5.0 and VS 5.0 transformation pipeline generated." << std::endl;

    // 3. Live Hardware End-to-End Rendering Test (CS -> VB/IB -> IA -> Render Target Readback)
    std::cout << "\n[TEST 3] Live D3D11 End-to-End Mesh Shader Execution on Hardware..." << std::endl;
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
        // Step A: Compile CS
        ID3DBlob* pCSBlob = nullptr;
        ID3DBlob* pErr = nullptr;
        HRESULT csHr = D3DCompile(csHlsl.c_str(), csHlsl.size(), "MeshCS", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &pCSBlob, &pErr);
        assert(SUCCEEDED(csHr) && pCSBlob);
        ID3D11ComputeShader* pCS = nullptr;
        pDevice->CreateComputeShader(pCSBlob->GetBufferPointer(), pCSBlob->GetBufferSize(), nullptr, &pCS);
        pCSBlob->Release();

        // Step B: Create Compute Output Buffer (Structured UAV) and D3D11 Vertex Buffer
        D3D11_BUFFER_DESC csVbDesc{};
        csVbDesc.ByteWidth = sizeof(MeshVertex) * 3;
        csVbDesc.Usage = D3D11_USAGE_DEFAULT;
        csVbDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        csVbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        csVbDesc.StructureByteStride = sizeof(MeshVertex);

        ID3D11Buffer* pCSOutVB = nullptr;
        HRESULT hrCSOutVB = pDevice->CreateBuffer(&csVbDesc, nullptr, &pCSOutVB);
        assert(SUCCEEDED(hrCSOutVB));

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavVbDesc{};
        uavVbDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavVbDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavVbDesc.Buffer.NumElements = 3;

        ID3D11UnorderedAccessView* pVBUAV = nullptr;
        HRESULT hrVBUAV = pDevice->CreateUnorderedAccessView(pCSOutVB, &uavVbDesc, &pVBUAV);
        assert(SUCCEEDED(hrVBUAV));

        // Destination Vertex Buffer for Input Assembler
        D3D11_BUFFER_DESC vbDesc{};
        vbDesc.ByteWidth = sizeof(MeshVertex) * 3;
        vbDesc.Usage = D3D11_USAGE_DEFAULT;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        ID3D11Buffer* pVB = nullptr;
        HRESULT hrVB = pDevice->CreateBuffer(&vbDesc, nullptr, &pVB);
        assert(SUCCEEDED(hrVB));

        // Step C: Create Index Buffer (with UAV and IndexBuffer bind flags)
        D3D11_BUFFER_DESC ibDesc{};
        ibDesc.ByteWidth = sizeof(uint32_t) * 3;
        ibDesc.Usage = D3D11_USAGE_DEFAULT;
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER | D3D11_BIND_UNORDERED_ACCESS;

        ID3D11Buffer* pIB = nullptr;
        HRESULT hrIB = pDevice->CreateBuffer(&ibDesc, nullptr, &pIB);
        std::cout << "  CreateBuffer(IB) hr = 0x" << std::hex << hrIB << std::dec << std::endl;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavIbDesc{};
        uavIbDesc.Format = DXGI_FORMAT_R32_UINT;
        uavIbDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavIbDesc.Buffer.NumElements = 3;

        ID3D11UnorderedAccessView* pIBUAV = nullptr;
        HRESULT hrIBUAV = pDevice->CreateUnorderedAccessView(pIB, &uavIbDesc, &pIBUAV);
        std::cout << "  CreateUAV(IB) hr = 0x" << std::hex << hrIBUAV << std::dec << std::endl;

        // Step D: Dispatch CS to generate geometry
        pContext->CSSetShader(pCS, nullptr, 0);
        ID3D11UnorderedAccessView* uavs[2] = { pVBUAV, pIBUAV };
        pContext->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
        pContext->Dispatch(1, 1, 1);

        // Unbind UAVs before binding to IA
        ID3D11UnorderedAccessView* nullUavs[2] = { nullptr, nullptr };
        pContext->CSSetUnorderedAccessViews(0, 2, nullUavs, nullptr);
        pContext->CSSetShader(nullptr, nullptr, 0);

        // Copy generated vertices into Input Assembler Vertex Buffer
        pContext->CopyResource(pVB, pCSOutVB);

        // Rasterizer State: Cull None
        D3D11_RASTERIZER_DESC rsDesc{};
        rsDesc.FillMode = D3D11_FILL_SOLID;
        rsDesc.CullMode = D3D11_CULL_NONE;
        rsDesc.DepthClipEnable = FALSE;
        ID3D11RasterizerState* pRS = nullptr;
        pDevice->CreateRasterizerState(&rsDesc, &pRS);
        pContext->RSSetState(pRS);

        // Step E: Compile VS and PS for rasterization pass
        ID3DBlob* pVSBlob = nullptr;
        D3DCompile(vsHlsl.c_str(), vsHlsl.size(), "MeshVS", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &pVSBlob, nullptr);
        assert(pVSBlob);
        ID3D11VertexShader* pVS = nullptr;
        pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &pVS);

        // Input Layout
        D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        ID3D11InputLayout* pLayout = nullptr;
        pDevice->CreateInputLayout(layoutDesc, 2, pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &pLayout);
        pVSBlob->Release();

        static const char* s_psHLSL = R"(
            struct PSInput {
                float4 position : SV_POSITION;
                float4 color : COLOR;
            };
            float4 PSMain(PSInput input) : SV_TARGET {
                return input.color;
            }
        )";
        ID3DBlob* pPSBlob = nullptr;
        D3DCompile(s_psHLSL, std::strlen(s_psHLSL), "MeshPS", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &pPSBlob, nullptr);
        ID3D11PixelShader* pPS = nullptr;
        pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &pPS);
        pPSBlob->Release();

        // Step F: Create Render Target (64x64)
        D3D11_TEXTURE2D_DESC rtDesc{};
        rtDesc.Width = 64;
        rtDesc.Height = 64;
        rtDesc.MipLevels = 1;
        rtDesc.ArraySize = 1;
        rtDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtDesc.SampleDesc.Count = 1;
        rtDesc.Usage = D3D11_USAGE_DEFAULT;
        rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

        ID3D11Texture2D* pRTTex = nullptr;
        pDevice->CreateTexture2D(&rtDesc, nullptr, &pRTTex);
        ID3D11RenderTargetView* pRTV = nullptr;
        pDevice->CreateRenderTargetView(pRTTex, nullptr, &pRTV);

        // Step G: Bind Pipeline and Draw
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // Black clear
        pContext->ClearRenderTargetView(pRTV, clearColor);

        D3D11_VIEWPORT vp{};
        vp.Width = 64.0f;
        vp.Height = 64.0f;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        pContext->RSSetViewports(1, &vp);

        pContext->OMSetRenderTargets(1, &pRTV, nullptr);
        pContext->IASetInputLayout(pLayout);
        pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        UINT stride = sizeof(MeshVertex);
        UINT offset = 0;
        pContext->IASetVertexBuffers(0, 1, &pVB, &stride, &offset);
        pContext->IASetIndexBuffer(pIB, DXGI_FORMAT_R32_UINT, 0);

        pContext->VSSetShader(pVS, nullptr, 0);
        pContext->PSSetShader(pPS, nullptr, 0);

        pContext->DrawIndexed(3, 0, 0);

        // Step H: Read back pixel from center of triangle (32, 32)
        D3D11_TEXTURE2D_DESC stageDesc = rtDesc;
        stageDesc.Usage = D3D11_USAGE_STAGING;
        stageDesc.BindFlags = 0;
        stageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        ID3D11Texture2D* pStage = nullptr;
        pDevice->CreateTexture2D(&stageDesc, nullptr, &pStage);
        pContext->CopyResource(pStage, pRTTex);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        pContext->Map(pStage, 0, D3D11_MAP_READ, 0, &mapped);
        const uint32_t* pPixels = reinterpret_cast<const uint32_t*>(mapped.pData);
        uint32_t centerPixel = pPixels[32 * 64 + 32];
        pContext->Unmap(pStage, 0);

        std::cout << "  Rendered Center Pixel: 0x" << std::hex << centerPixel << std::dec << std::endl;
        // Verify triangle was rasterized (not background black)
        assert((centerPixel & 0x00FFFFFF) != 0);

        // Cleanup
        if (pRS) pRS->Release();
        pStage->Release();
        pRTV->Release();
        pRTTex->Release();
        pPS->Release();
        pLayout->Release();
        pVS->Release();
        pIBUAV->Release();
        pIB->Release();
        pVBUAV->Release();
        pVB->Release();
        pCSOutVB->Release();
        pCS->Release();
        pContext->Release();
        pDevice->Release();

        std::cout << "  [PASS] End-to-end mesh-to-compute transformation rasterized triangle successfully on live GPU!\n";
    } else {
        std::cout << "  [SKIP] D3D11 device creation skipped or hardware unavailable.\n";
    }

    std::cout << "========================================================\n";
    std::cout << "   ALL MESH SHADER COMPATIBILITY TESTS PASSED!          \n";
    std::cout << "========================================================\n" << std::endl;
    return 0;
}
