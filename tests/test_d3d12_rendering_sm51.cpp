#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>

#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>
#include <cmath>
#include <iomanip>

typedef HRESULT(WINAPI* PFN_D3D12CreateDevice)(
    IUnknown* pAdapter,
    D3D_FEATURE_LEVEL MinimumFeatureLevel,
    REFIID riid,
    void** ppDevice);

typedef HRESULT(WINAPI* PFN_D3D12SerializeRootSignature)(
    const D3D12_ROOT_SIGNATURE_DESC* pRootSignature,
    D3D_ROOT_SIGNATURE_VERSION Version,
    ID3DBlob** ppBlob,
    ID3DBlob** ppErrorBlob);

typedef HRESULT(WINAPI* PFN_D3DCompile)(
    LPCVOID pSrcData,
    SIZE_T SrcDataSize,
    LPCSTR pSourceName,
    const D3D_SHADER_MACRO* pDefines,
    ID3DInclude* pInclude,
    LPCSTR pEntrypoint,
    LPCSTR pTarget,
    UINT Flags1,
    UINT Flags2,
    ID3DBlob** ppCode,
    ID3DBlob** ppErrorMsgs);

typedef HRESULT(WINAPI* PFN_KeplerBridge_ReadbackRenderTarget)(
    ID3D12Resource* pD3D12Resource,
    void* pOutPixels,
    UINT* pOutWidth,
    UINT* pOutHeight);

typedef void(WINAPI* PFN_KeplerBridge_GetTranslationStats)(
    UINT* pTranslatedCount,
    UINT* pRejectedCount);

struct Vertex {
    float x, y;
    float r, g, b, a;
};

static LRESULT CALLBACK DummyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << " KeplerBridge Phase 11I SM 5.1 Rendering & Readback Proof" << std::endl;
    std::cout << "========================================================" << std::endl;

    // Ensure translation mode is active
    char exePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeStr(exePath);
    size_t lastSlash = exeStr.find_last_of("\\/");
    std::string exeDir = (lastSlash != std::string::npos ? exeStr.substr(0, lastSlash) : ".");
    std::string iniPath = exeDir + "\\kepler_bridge.ini";

    char origTranslation[16] = "0";
    GetPrivateProfileStringA("Translation", "EnableD3D11Translation", "0", origTranslation, sizeof(origTranslation), iniPath.c_str());
    WritePrivateProfileStringA("Translation", "EnableD3D11Translation", "1", iniPath.c_str());
    struct IniRestoreGuard {
        std::string path;
        std::string orig;
        IniRestoreGuard(const std::string& p, const char* val) : path(p), orig(val) {}
        ~IniRestoreGuard() {
            WritePrivateProfileStringA("Translation", "EnableD3D11Translation", orig.c_str(), path.c_str());
        }
    } restoreGuard(iniPath, origTranslation);

    // Load proxy d3d12.dll
    HMODULE hD3D12 = LoadLibraryA("d3d12.dll");
    if (!hD3D12) {
        std::cerr << "[FAIL] Could not load proxy d3d12.dll!" << std::endl;
        return 1;
    }

    auto pfnCreateDevice = reinterpret_cast<PFN_D3D12CreateDevice>(
        GetProcAddress(hD3D12, "D3D12CreateDevice"));
    auto pfnSerializeRootSignature = reinterpret_cast<PFN_D3D12SerializeRootSignature>(
        GetProcAddress(hD3D12, "D3D12SerializeRootSignature"));
    auto pfnReadback = reinterpret_cast<PFN_KeplerBridge_ReadbackRenderTarget>(
        GetProcAddress(hD3D12, "KeplerBridge_ReadbackRenderTarget"));
    auto pfnGetStats = reinterpret_cast<PFN_KeplerBridge_GetTranslationStats>(
        GetProcAddress(hD3D12, "KeplerBridge_GetTranslationStats"));

    if (!pfnCreateDevice || !pfnSerializeRootSignature || !pfnReadback || !pfnGetStats) {
        std::cerr << "[FAIL] Required symbols missing from d3d12.dll!" << std::endl;
        FreeLibrary(hD3D12);
        return 1;
    }

    HMODULE hCompiler = LoadLibraryA("d3dcompiler_47.dll");
    if (!hCompiler) {
        std::cerr << "[FAIL] Failed to load d3dcompiler_47.dll!" << std::endl;
        FreeLibrary(hD3D12);
        return 1;
    }
    auto pfnCompile = reinterpret_cast<PFN_D3DCompile>(
        GetProcAddress(hCompiler, "D3DCompile"));

    // Step 1: D3D12 Device
    ID3D12Device* pDevice = nullptr;
    HRESULT hr = pfnCreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice));
    if (FAILED(hr) || !pDevice) {
        std::cerr << "[FAIL] D3D12CreateDevice failed: 0x" << std::hex << hr << std::endl;
        return 1;
    }
    std::cout << "[PASS] D3D12 Device created via KeplerBridge proxy." << std::endl;

    // Step 2: Command Infrastructure
    D3D12_COMMAND_QUEUE_DESC qDesc = {};
    qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* pQueue = nullptr;
    hr = pDevice->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&pQueue));
    assert(SUCCEEDED(hr) && pQueue);

    ID3D12CommandAllocator* pAlloc = nullptr;
    hr = pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pAlloc));
    assert(SUCCEEDED(hr) && pAlloc);

    ID3D12GraphicsCommandList* pCmdList = nullptr;
    hr = pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pAlloc, nullptr, IID_PPV_ARGS(&pCmdList));
    assert(SUCCEEDED(hr) && pCmdList);

    // Step 3: Render Target
    constexpr UINT RT_WIDTH = 256;
    constexpr UINT RT_HEIGHT = 256;

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ID3D12DescriptorHeap* pRtvHeap = nullptr;
    hr = pDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&pRtvHeap));
    assert(SUCCEEDED(hr) && pRtvHeap);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = pRtvHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_RESOURCE_DESC rtDesc = {};
    rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rtDesc.Width = RT_WIDTH;
    rtDesc.Height = RT_HEIGHT;
    rtDesc.DepthOrArraySize = 1;
    rtDesc.MipLevels = 1;
    rtDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtDesc.SampleDesc.Count = 1;
    rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearVal = {};
    clearVal.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clearVal.Color[0] = 0.125f;
    clearVal.Color[1] = 0.250f;
    clearVal.Color[2] = 0.500f;
    clearVal.Color[3] = 1.000f;

    ID3D12Resource* pRenderTarget = nullptr;
    hr = pDevice->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &rtDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearVal,
        IID_PPV_ARGS(&pRenderTarget));
    assert(SUCCEEDED(hr) && pRenderTarget);
    pDevice->CreateRenderTargetView(pRenderTarget, nullptr, rtvHandle);

    // Step 4: Root Signature
    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* pSigBlob = nullptr;
    ID3DBlob* pErrorBlob = nullptr;
    hr = pfnSerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pSigBlob, &pErrorBlob);
    assert(SUCCEEDED(hr) && pSigBlob);

    ID3D12RootSignature* pRootSig = nullptr;
    hr = pDevice->CreateRootSignature(0, pSigBlob->GetBufferPointer(), pSigBlob->GetBufferSize(), IID_PPV_ARGS(&pRootSig));
    pSigBlob->Release();
    assert(SUCCEEDED(hr) && pRootSig);

    // Step 5: Compile Authentic SM 5.1 Shaders
    std::cout << "\n--- Compiling Authentic SM 5.1 Shaders ---" << std::endl;
    const char* hlslSM51 =
        "struct VS_IN { float2 pos : POSITION; float4 col : COLOR; };\n"
        "struct PS_IN { float4 pos : SV_Position; float4 col : COLOR; };\n"
        "PS_IN VSMain(VS_IN input) {\n"
        "    PS_IN output;\n"
        "    output.pos = float4(input.pos, 0.0f, 1.0f);\n"
        "    output.col = input.col;\n"
        "    return output;\n"
        "}\n"
        "float4 PSMain(PS_IN input) : SV_Target {\n"
        "    return input.col;\n"
        "}\n";

    ID3DBlob* pVsBlob = nullptr;
    ID3DBlob* pPsBlob = nullptr;
    hr = pfnCompile(hlslSM51, strlen(hlslSM51), "ShaderSM51", nullptr, nullptr, "VSMain", "vs_5_1", 0, 0, &pVsBlob, &pErrorBlob);
    if (FAILED(hr) || !pVsBlob) {
        if (pErrorBlob) {
            std::cerr << "VS Compile Error: " << (char*)pErrorBlob->GetBufferPointer() << std::endl;
            pErrorBlob->Release();
        }
        return 1;
    }
    std::cout << "[PASS] Compiled authentic vs_5_1 shader (" << pVsBlob->GetBufferSize() << " bytes)." << std::endl;

    hr = pfnCompile(hlslSM51, strlen(hlslSM51), "ShaderSM51", nullptr, nullptr, "PSMain", "ps_5_1", 0, 0, &pPsBlob, &pErrorBlob);
    if (FAILED(hr) || !pPsBlob) {
        if (pErrorBlob) {
            std::cerr << "PS Compile Error: " << (char*)pErrorBlob->GetBufferPointer() << std::endl;
            pErrorBlob->Release();
        }
        return 1;
    }
    std::cout << "[PASS] Compiled authentic ps_5_1 shader (" << pPsBlob->GetBufferSize() << " bytes)." << std::endl;

    // Step 6: Create D3D12 PSO with SM 5.1 Shaders (lowered on the fly by KeplerBridge!)
    D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = pRootSig;
    psoDesc.VS = { pVsBlob->GetBufferPointer(), pVsBlob->GetBufferSize() };
    psoDesc.PS = { pPsBlob->GetBufferPointer(), pPsBlob->GetBufferSize() };
    psoDesc.InputLayout = { inputElements, _countof(inputElements) };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    ID3D12PipelineState* pPSO = nullptr;
    hr = pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pPSO));
    if (FAILED(hr) || !pPSO) {
        std::cerr << "[FAIL] CreateGraphicsPipelineState with SM 5.1 shaders failed: 0x" << std::hex << hr << std::endl;
        return 1;
    }
    std::cout << "[PASS] D3D12 PipelineState created with lowered SM 5.1 shaders!" << std::endl;

    // Step 7: Vertex Buffer
    Vertex vertices[] = {
        {  0.0f,  0.5f,  0.0f, 1.0f, 0.0f, 1.0f }, // Green Top
        {  0.5f, -0.5f,  0.0f, 1.0f, 0.0f, 1.0f }, // Green Right
        { -0.5f, -0.5f,  0.0f, 1.0f, 0.0f, 1.0f }, // Green Left
    };

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = sizeof(vertices);
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.Format = DXGI_FORMAT_UNKNOWN;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* pVertexBuffer = nullptr;
    hr = pDevice->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&pVertexBuffer));
    assert(SUCCEEDED(hr) && pVertexBuffer);

    void* pVbData = nullptr;
    pVertexBuffer->Map(0, nullptr, &pVbData);
    memcpy(pVbData, vertices, sizeof(vertices));
    pVertexBuffer->Unmap(0, nullptr);

    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    vbView.BufferLocation = pVertexBuffer->GetGPUVirtualAddress();
    vbView.SizeInBytes = sizeof(vertices);
    vbView.StrideInBytes = sizeof(Vertex);

    // Step 8: Render
    pCmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(RT_WIDTH), static_cast<float>(RT_HEIGHT), 0.0f, 1.0f };
    pCmdList->RSSetViewports(1, &viewport);
    D3D12_RECT scissor = { 0, 0, static_cast<LONG>(RT_WIDTH), static_cast<LONG>(RT_HEIGHT) };
    pCmdList->RSSetScissorRects(1, &scissor);

    const float clearColor[4] = { 0.125f, 0.250f, 0.500f, 1.000f };
    pCmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    pCmdList->SetPipelineState(pPSO);
    pCmdList->SetGraphicsRootSignature(pRootSig);
    pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    pCmdList->IASetVertexBuffers(0, 1, &vbView);
    pCmdList->DrawInstanced(3, 1, 0, 0);

    hr = pCmdList->Close();
    assert(SUCCEEDED(hr));

    ID3D12CommandList* ppLists[] = { pCmdList };
    pQueue->ExecuteCommandLists(1, ppLists);

    // Fence Sync
    ID3D12Fence* pFence = nullptr;
    hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence));
    assert(SUCCEEDED(hr) && pFence);
    pQueue->Signal(pFence, 1);
    while (pFence->GetCompletedValue() < 1) {
        Sleep(1);
    }
    std::cout << "[PASS] GPU Command Queue execution & fence synchronization completed." << std::endl;

    // Step 9: Pixel Readback & Color Verification
    std::vector<uint32_t> pixels(RT_WIDTH * RT_HEIGHT, 0);
    UINT readbackW = 0, readbackH = 0;
    hr = pfnReadback(pRenderTarget, pixels.data(), &readbackW, &readbackH);
    if (FAILED(hr) || readbackW != RT_WIDTH || readbackH != RT_HEIGHT) {
        std::cerr << "[FAIL] ReadbackRenderTarget failed: hr = 0x" << std::hex << hr << std::endl;
        return 1;
    }

    auto getPixel = [&](UINT x, UINT y) -> uint32_t {
        return pixels[y * readbackW + x];
    };
    auto getR = [](uint32_t p) -> int { return static_cast<int>(p & 0xFF); };
    auto getG = [](uint32_t p) -> int { return static_cast<int>((p >> 8) & 0xFF); };
    auto getB = [](uint32_t p) -> int { return static_cast<int>((p >> 16) & 0xFF); };
    auto getA = [](uint32_t p) -> int { return static_cast<int>((p >> 24) & 0xFF); };

    // Clear color check (10, 10)
    uint32_t cornerPixel = getPixel(10, 10);
    assert(std::abs(getR(cornerPixel) - 32) <= 4);
    assert(std::abs(getG(cornerPixel) - 64) <= 4);
    assert(std::abs(getB(cornerPixel) - 128) <= 4);
    std::cout << "[PASS] Clear color verified at (10, 10): (32, 64, 128)." << std::endl;

    // Triangle rasterization color check (128, 128)
    uint32_t centerPixel = getPixel(128, 128);
    assert(std::abs(getR(centerPixel) - 0) <= 4);
    assert(std::abs(getG(centerPixel) - 255) <= 4);
    assert(std::abs(getB(centerPixel) - 0) <= 4);
    std::cout << "[PASS] Triangle center color verified at (128, 128): GREEN (0, 255, 0) rendered by SM 5.1 shaders!" << std::endl;

    UINT translatedCount = 0, rejectedCount = 0;
    pfnGetStats(&translatedCount, &rejectedCount);
    std::cout << "[TRANSLATION STATS] Translated Commands: " << translatedCount
              << ", Rejected Commands: " << rejectedCount << std::endl;
    assert(translatedCount > 0 && rejectedCount == 0);

    // Cleanup
    pVsBlob->Release();
    pPsBlob->Release();
    pVertexBuffer->Release();
    pRenderTarget->Release();
    pRtvHeap->Release();
    pPSO->Release();
    pRootSig->Release();
    pFence->Release();
    pCmdList->Release();
    pAlloc->Release();
    pQueue->Release();
    pDevice->Release();
    FreeLibrary(hCompiler);
    FreeLibrary(hD3D12);

    std::cout << "\n========================================================" << std::endl;
    std::cout << "  ALL SM 5.1 RENDERING & PIXEL READBACK CHECKS PASSED!" << std::endl;
    std::cout << "========================================================" << std::endl;
    return 0;
}
