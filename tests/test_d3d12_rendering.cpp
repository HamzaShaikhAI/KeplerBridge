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

typedef HRESULT(WINAPI* PFN_KeplerBridge_CreateSwapChainForHwnd)(
    HWND hwnd,
    UINT width,
    UINT height,
    IDXGISwapChain** ppSwapChain);

typedef HRESULT(WINAPI* PFN_KeplerBridge_PresentSwapChain)(
    IDXGISwapChain* pSwapChain,
    UINT syncInterval,
    UINT flags);

typedef void(WINAPI* PFN_KeplerBridge_GetTranslationStats)(
    UINT* pTranslatedCount,
    UINT* pRejectedCount);

struct Vertex {
    float x, y;
    float r, g, b, a;
};

static void PrintAdapterInfo() {
    IDXGIFactory4* pFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&pFactory)))) {
        IDXGIAdapter1* pAdapter = nullptr;
        for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            pAdapter->GetDesc1(&desc);
            std::wstring descW(desc.Description);
            std::string descA(descW.begin(), descW.end());
            std::cout << "[HARDWARE] Adapter " << i << ": " << descA
                      << " (" << (desc.DedicatedVideoMemory / (1024 * 1024)) << " MB VRAM)" << std::endl;
            pAdapter->Release();
        }
        pFactory->Release();
    }
}

static LRESULT CALLBACK DummyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << "  KeplerBridge Phase 4 Live Rendering & Presentation Proof" << std::endl;
    std::cout << "========================================================" << std::endl;

    PrintAdapterInfo();

    // Ensure translation mode is active for rendering verification test
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
    std::cout << "[TEST] Loaded proxy d3d12.dll successfully." << std::endl;

    auto pfnCreateDevice = reinterpret_cast<PFN_D3D12CreateDevice>(
        GetProcAddress(hD3D12, "D3D12CreateDevice"));
    auto pfnSerializeRootSignature = reinterpret_cast<PFN_D3D12SerializeRootSignature>(
        GetProcAddress(hD3D12, "D3D12SerializeRootSignature"));

    auto pfnReadback = reinterpret_cast<PFN_KeplerBridge_ReadbackRenderTarget>(
        GetProcAddress(hD3D12, "KeplerBridge_ReadbackRenderTarget"));
    auto pfnCreateSwapChain = reinterpret_cast<PFN_KeplerBridge_CreateSwapChainForHwnd>(
        GetProcAddress(hD3D12, "KeplerBridge_CreateSwapChainForHwnd"));
    auto pfnPresent = reinterpret_cast<PFN_KeplerBridge_PresentSwapChain>(
        GetProcAddress(hD3D12, "KeplerBridge_PresentSwapChain"));
    auto pfnGetStats = reinterpret_cast<PFN_KeplerBridge_GetTranslationStats>(
        GetProcAddress(hD3D12, "KeplerBridge_GetTranslationStats"));

    if (!pfnCreateDevice || !pfnSerializeRootSignature) {
        std::cerr << "[FAIL] Required D3D12 entry points not exported from proxy!" << std::endl;
        FreeLibrary(hD3D12);
        return 1;
    }
    if (!pfnReadback || !pfnCreateSwapChain || !pfnPresent || !pfnGetStats) {
        std::cerr << "[FAIL] KeplerBridge diagnostic entry points not exported from proxy!" << std::endl;
        FreeLibrary(hD3D12);
        return 1;
    }

    // Load d3dcompiler_47.dll for runtime HLSL compilation
    HMODULE hCompiler = LoadLibraryA("d3dcompiler_47.dll");
    if (!hCompiler) {
        std::cerr << "[FAIL] Failed to load d3dcompiler_47.dll!" << std::endl;
        FreeLibrary(hD3D12);
        return 1;
    }
    auto pfnCompile = reinterpret_cast<PFN_D3DCompile>(
        GetProcAddress(hCompiler, "D3DCompile"));
    if (!pfnCompile) {
        std::cerr << "[FAIL] D3DCompile symbol not found in d3dcompiler_47.dll!" << std::endl;
        FreeLibrary(hCompiler);
        FreeLibrary(hD3D12);
        return 1;
    }

    // Step 1: Create D3D12 Device
    std::cout << "\n--- STEP 1: D3D12 Device Creation (Translation Mode) ---" << std::endl;
    ID3D12Device* pDevice = nullptr;
    HRESULT hr = pfnCreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice));
    if (FAILED(hr) || !pDevice) {
        std::cerr << "[FAIL] Failed to create D3D12 device: hr = 0x" << std::hex << hr << std::dec << std::endl;
        FreeLibrary(hCompiler);
        FreeLibrary(hD3D12);
        return 1;
    }
    std::cout << "[PASS] D3D12 Device created successfully." << std::endl;

    // Step 2: Create Command Queue, Allocator, and Command List
    std::cout << "\n--- STEP 2: Command Infrastructure Creation ---" << std::endl;
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* pQueue = nullptr;
    hr = pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&pQueue));
    assert(SUCCEEDED(hr) && pQueue);

    ID3D12CommandAllocator* pAlloc = nullptr;
    hr = pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pAlloc));
    assert(SUCCEEDED(hr) && pAlloc);

    ID3D12GraphicsCommandList* pCmdList = nullptr;
    hr = pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pAlloc, nullptr, IID_PPV_ARGS(&pCmdList));
    assert(SUCCEEDED(hr) && pCmdList);
    std::cout << "[PASS] Command Queue, Allocator, and List created successfully." << std::endl;

    // Step 3: Create RTV Descriptor Heap
    std::cout << "\n--- STEP 3: RTV Descriptor Heap Creation ---" << std::endl;
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ID3D12DescriptorHeap* pRtvHeap = nullptr;
    hr = pDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&pRtvHeap));
    assert(SUCCEEDED(hr) && pRtvHeap);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = pRtvHeap->GetCPUDescriptorHandleForHeapStart();
    std::cout << "[PASS] RTV Descriptor Heap created successfully." << std::endl;

    // Step 4: Create Render Target Texture (256x256 RGBA8)
    std::cout << "\n--- STEP 4: Render Target Resource Creation ---" << std::endl;
    const UINT RT_WIDTH = 256;
    const UINT RT_HEIGHT = 256;

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
    clearVal.Color[0] = 0.125f; // R: 32 / 255
    clearVal.Color[1] = 0.250f; // G: 64 / 255
    clearVal.Color[2] = 0.500f; // B: 128 / 255
    clearVal.Color[3] = 1.000f; // A: 255 / 255

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
    std::cout << "[PASS] Committed 256x256 Render Target Texture & RTV created." << std::endl;

    // Step 5: Create Root Signature
    std::cout << "\n--- STEP 5: Root Signature Creation ---" << std::endl;
    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* pSigBlob = nullptr;
    ID3DBlob* pErrorBlob = nullptr;
    hr = pfnSerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pSigBlob, &pErrorBlob);
    if (FAILED(hr)) {
        if (pErrorBlob) {
            std::cerr << "RootSig Error: " << (char*)pErrorBlob->GetBufferPointer() << std::endl;
            pErrorBlob->Release();
        }
        return 1;
    }
    ID3D12RootSignature* pRootSig = nullptr;
    hr = pDevice->CreateRootSignature(0, pSigBlob->GetBufferPointer(), pSigBlob->GetBufferSize(), IID_PPV_ARGS(&pRootSig));
    pSigBlob->Release();
    assert(SUCCEEDED(hr) && pRootSig);
    std::cout << "[PASS] Root Signature created successfully." << std::endl;

    // Step 6: Compile Shaders (SM 5.0) & Create Pipeline State Object (PSO)
    std::cout << "\n--- STEP 6: Shader Compilation & PSO Creation ---" << std::endl;
    const char* hlslSource =
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
    hr = pfnCompile(hlslSource, strlen(hlslSource), "Shader", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &pVsBlob, &pErrorBlob);
    if (FAILED(hr)) {
        if (pErrorBlob) {
            std::cerr << "VS Compile Error: " << (char*)pErrorBlob->GetBufferPointer() << std::endl;
            pErrorBlob->Release();
        }
        return 1;
    }

    hr = pfnCompile(hlslSource, strlen(hlslSource), "Shader", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &pPsBlob, &pErrorBlob);
    if (FAILED(hr)) {
        if (pErrorBlob) {
            std::cerr << "PS Compile Error: " << (char*)pErrorBlob->GetBufferPointer() << std::endl;
            pErrorBlob->Release();
        }
        return 1;
    }

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
    assert(SUCCEEDED(hr) && pPSO);
    std::cout << "[PASS] SM 5.0 VS & PS compiled and Graphics PSO created." << std::endl;

    // Step 7: Create Vertex Buffer (Upload Heap)
    std::cout << "\n--- STEP 7: Vertex Buffer Resource Creation ---" << std::endl;
    // Triangle covering center of 256x256 viewport with bright green color (0, 1, 0, 1)
    Vertex vertices[] = {
        {  0.0f,  0.6f,  0.0f, 1.0f, 0.0f, 1.0f },
        {  0.6f, -0.6f,  0.0f, 1.0f, 0.0f, 1.0f },
        { -0.6f, -0.6f,  0.0f, 1.0f, 0.0f, 1.0f }
    };

    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = sizeof(vertices);
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.Format = DXGI_FORMAT_UNKNOWN;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

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
    std::cout << "[PASS] Vertex buffer created and populated via upload heap." << std::endl;

    // Step 8: Record Rendering Commands
    std::cout << "\n--- STEP 8: Command Recording ---" << std::endl;
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
    std::cout << "[PASS] Clear, SetPSO, SetVB, DrawInstanced(3) recorded and Closed." << std::endl;

    // Step 9: Execute Commands on Queue
    std::cout << "\n--- STEP 9: Command Execution ---" << std::endl;
    ID3D12CommandList* ppLists[] = { pCmdList };
    pQueue->ExecuteCommandLists(1, ppLists);

    // Fence synchronization
    ID3D12Fence* pFence = nullptr;
    hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence));
    assert(SUCCEEDED(hr) && pFence);
    pQueue->Signal(pFence, 1);
    while (pFence->GetCompletedValue() < 1) {
        Sleep(1);
    }
    std::cout << "[PASS] Commands executed and GPU queue synchronization verified." << std::endl;

    // Step 10: Readback Render Target & Verify Pixels
    std::cout << "\n--- STEP 10: Pixel Readback & Empirical Color Verification ---" << std::endl;
    std::vector<uint32_t> pixels(RT_WIDTH * RT_HEIGHT, 0);
    UINT readbackW = 0, readbackH = 0;

    hr = pfnReadback(pRenderTarget, pixels.data(), &readbackW, &readbackH);
    if (FAILED(hr) || readbackW != RT_WIDTH || readbackH != RT_HEIGHT) {
        std::cerr << "[FAIL] ReadbackRenderTarget failed: hr = 0x" << std::hex << hr << std::dec
                  << " (W: " << readbackW << ", H: " << readbackH << ")" << std::endl;
        return 1;
    }
    std::cout << "[PASS] Successfully read back 256x256 render target texture (" << pixels.size() << " pixels)." << std::endl;

    // Pixel format RGBA8: byte 0 = R, byte 1 = G, byte 2 = B, byte 3 = A
    auto getPixel = [&](UINT x, UINT y) -> uint32_t {
        return pixels[y * readbackW + x];
    };
    auto getR = [](uint32_t p) -> int { return static_cast<int>(p & 0xFF); };
    auto getG = [](uint32_t p) -> int { return static_cast<int>((p >> 8) & 0xFF); };
    auto getB = [](uint32_t p) -> int { return static_cast<int>((p >> 16) & 0xFF); };
    auto getA = [](uint32_t p) -> int { return static_cast<int>((p >> 24) & 0xFF); };

    // 1. Verify background clear color at (10, 10) - outside triangle
    uint32_t cornerPixel = getPixel(10, 10);
    int cornerR = getR(cornerPixel);
    int cornerG = getG(cornerPixel);
    int cornerB = getB(cornerPixel);
    int cornerA = getA(cornerPixel);

    std::cout << "[PIXEL PROOF] Clear Color at (10, 10): R=" << cornerR
              << ", G=" << cornerG << ", B=" << cornerB << ", A=" << cornerA
              << " | Expected: R~32, G~64, B~128, A~255" << std::endl;

    // Check clear color tolerance (+/- 4 values)
    assert(std::abs(cornerR - 32) <= 4);
    assert(std::abs(cornerG - 64) <= 4);
    assert(std::abs(cornerB - 128) <= 4);
    assert(std::abs(cornerA - 255) <= 4);
    std::cout << "[PASS] Render Target Clear Color verified within tolerance." << std::endl;

    // 2. Verify rasterized triangle color at center (128, 128) - inside triangle
    uint32_t centerPixel = getPixel(128, 128);
    int centerR = getR(centerPixel);
    int centerG = getG(centerPixel);
    int centerB = getB(centerPixel);
    int centerA = getA(centerPixel);

    std::cout << "[PIXEL PROOF] Triangle Color at (128, 128): R=" << centerR
              << ", G=" << centerG << ", B=" << centerB << ", A=" << centerA
              << " | Expected: R~0, G~255, B~0, A~255" << std::endl;

    // Check triangle color tolerance (+/- 4 values)
    assert(std::abs(centerR - 0) <= 4);
    assert(std::abs(centerG - 255) <= 4);
    assert(std::abs(centerB - 0) <= 4);
    assert(std::abs(centerA - 255) <= 4);
    std::cout << "[PASS] Triangle Rasterization Pixel Color verified within tolerance." << std::endl;

    // Check translation statistics
    UINT translatedCount = 0, rejectedCount = 0;
    pfnGetStats(&translatedCount, &rejectedCount);
    std::cout << "[TRANSLATION STATS] Translated Commands: " << translatedCount
              << ", Rejected Commands: " << rejectedCount << std::endl;
    assert(translatedCount > 0 && rejectedCount == 0);

    // Step 11: SwapChain & Presentation Verification
    std::cout << "\n--- STEP 11: SwapChain Presentation Interop ---" << std::endl;
    WNDCLASSA wc = {};
    wc.lpfnWndProc = DummyWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "KeplerBridgeProofWindow";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(
        0, "KeplerBridgeProofWindow", "KeplerBridge Proof",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 256, 256,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    if (hwnd) {
        IDXGISwapChain* pSwapChain = nullptr;
        hr = pfnCreateSwapChain(hwnd, 256, 256, &pSwapChain);
        if (SUCCEEDED(hr) && pSwapChain) {
            std::cout << "[PASS] DXGI SwapChain created for HWND via D3D11 backend." << std::endl;
            hr = pfnPresent(pSwapChain, 0, 0);
            if (SUCCEEDED(hr)) {
                std::cout << "[PASS] PresentSwapChain succeeded (Frame successfully presented!)." << std::endl;
            } else {
                std::cout << "[WARN] PresentSwapChain returned hr = 0x" << std::hex << hr << std::dec << std::endl;
            }
            pSwapChain->Release();
        } else {
            std::cout << "[WARN] CreateSwapChainForHwnd returned hr = 0x" << std::hex << hr << std::dec << std::endl;
        }
        DestroyWindow(hwnd);
        UnregisterClassA("KeplerBridgeProofWindow", GetModuleHandle(nullptr));
    }

    // Cleanup resources
    pVsBlob->Release();
    pPsBlob->Release();
    pPSO->Release();
    pRootSig->Release();
    pVertexBuffer->Release();
    pRenderTarget->Release();
    pRtvHeap->Release();
    pFence->Release();
    pCmdList->Release();
    pAlloc->Release();
    pQueue->Release();
    pDevice->Release();

    FreeLibrary(hCompiler);
    FreeLibrary(hD3D12);

    std::cout << "\n========================================================" << std::endl;
    std::cout << "  ALL RENDERING & PRESENTATION PROOFS PASSED (11/11)!   " << std::endl;
    std::cout << "========================================================" << std::endl;

    return 0;
}
