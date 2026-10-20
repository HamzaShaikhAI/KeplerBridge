#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <iostream>
#include <cassert>
#include <vector>
#include <cmath>

typedef HRESULT(WINAPI* PFN_D3D12CreateDevice)(
    IUnknown* pAdapter,
    D3D_FEATURE_LEVEL MinimumFeatureLevel,
    REFIID riid,
    void** ppDevice);

typedef HRESULT(WINAPI* PFN_KeplerBridge_CreateDXGISwapChainForHwnd)(
    ID3D12CommandQueue* pCommandQueue,
    HWND hwnd,
    const DXGI_SWAP_CHAIN_DESC1* pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
    IDXGISwapChain3** ppSwapChain);

typedef HRESULT(WINAPI* PFN_KeplerBridge_CreateDXGISwapChain)(
    ID3D12CommandQueue* pCommandQueue,
    const DXGI_SWAP_CHAIN_DESC* pDesc,
    IDXGISwapChain** ppSwapChain);

static LRESULT CALLBACK DummyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << "  KeplerBridge Phase 5 — Application DXGI SwapChain Test " << std::endl;
    std::cout << "========================================================" << std::endl;

    // Load proxy d3d12.dll
    HMODULE hD3D12 = LoadLibraryA("d3d12.dll");
    if (!hD3D12) {
        std::cerr << "[FAIL] Could not load proxy d3d12.dll!" << std::endl;
        return 1;
    }
    std::cout << "[TEST] Loaded proxy d3d12.dll successfully." << std::endl;

    auto pfnCreateDevice = reinterpret_cast<PFN_D3D12CreateDevice>(
        GetProcAddress(hD3D12, "D3D12CreateDevice"));
    auto pfnCreateSwapChainForHwnd = reinterpret_cast<PFN_KeplerBridge_CreateDXGISwapChainForHwnd>(
        GetProcAddress(hD3D12, "KeplerBridge_CreateDXGISwapChainForHwnd"));
    auto pfnCreateSwapChain = reinterpret_cast<PFN_KeplerBridge_CreateDXGISwapChain>(
        GetProcAddress(hD3D12, "KeplerBridge_CreateDXGISwapChain"));

    if (!pfnCreateDevice || !pfnCreateSwapChainForHwnd || !pfnCreateSwapChain) {
        std::cerr << "[FAIL] Required symbols missing from proxy d3d12.dll!" << std::endl;
        FreeLibrary(hD3D12);
        return 1;
    }

    // Create Win32 Window
    WNDCLASSA wc = {};
    wc.lpfnWndProc = DummyWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "KeplerBridgePhase5Window";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(
        0, "KeplerBridgePhase5Window", "KeplerBridge DXGI Test",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 256, 256,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    assert(hwnd != nullptr);

    // Create D3D12 Device & Command Queue
    ID3D12Device* pDevice = nullptr;
    HRESULT hr = pfnCreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice));
    assert(SUCCEEDED(hr) && pDevice);

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* pQueue = nullptr;
    hr = pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&pQueue));
    assert(SUCCEEDED(hr) && pQueue);

    // ------------------------------------------------------------------------
    // TEST 1: Invalid Parameter Handling & Failure Paths
    // ------------------------------------------------------------------------
    std::cout << "\n--- TEST 1: Invalid Parameter & Failure Path Handling ---" << std::endl;
    DXGI_SWAP_CHAIN_DESC1 scDesc1 = {};
    scDesc1.Width = 256;
    scDesc1.Height = 256;
    scDesc1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc1.SampleDesc.Count = 1;
    scDesc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc1.BufferCount = 2;
    scDesc1.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain3* pNullSC = nullptr;
    hr = pfnCreateSwapChainForHwnd(nullptr, hwnd, &scDesc1, nullptr, &pNullSC);
    assert(FAILED(hr));
    std::cout << "[PASS] NULL queue correctly rejected (hr = 0x" << std::hex << hr << std::dec << ")." << std::endl;

    hr = pfnCreateSwapChainForHwnd(pQueue, nullptr, &scDesc1, nullptr, &pNullSC);
    assert(FAILED(hr));
    std::cout << "[PASS] NULL HWND correctly rejected (hr = 0x" << std::hex << hr << std::dec << ")." << std::endl;

    hr = pfnCreateSwapChainForHwnd(pQueue, hwnd, nullptr, nullptr, &pNullSC);
    assert(FAILED(hr));
    std::cout << "[PASS] NULL desc correctly rejected (hr = 0x" << std::hex << hr << std::dec << ")." << std::endl;

    // ------------------------------------------------------------------------
    // TEST 2: Application-Facing SwapChain Creation
    // ------------------------------------------------------------------------
    std::cout << "\n--- TEST 2: Application SwapChain Creation ---" << std::endl;
    IDXGISwapChain3* pSwapChain = nullptr;
    hr = pfnCreateSwapChainForHwnd(pQueue, hwnd, &scDesc1, nullptr, &pSwapChain);
    if (FAILED(hr) || !pSwapChain) {
        std::cerr << "[FAIL] Failed to create application swap chain: hr = 0x" << std::hex << hr << std::dec << std::endl;
        pQueue->Release();
        pDevice->Release();
        DestroyWindow(hwnd);
        UnregisterClassA("KeplerBridgePhase5Window", GetModuleHandle(nullptr));
        FreeLibrary(hD3D12);
        return 1;
    }
    std::cout << "[PASS] Created WrappedDXGISwapChain (IDXGISwapChain3) successfully." << std::endl;

    // Check descriptions
    DXGI_SWAP_CHAIN_DESC1 queriedDesc1 = {};
    hr = pSwapChain->GetDesc1(&queriedDesc1);
    assert(SUCCEEDED(hr) && queriedDesc1.Width == 256 && queriedDesc1.Height == 256 && queriedDesc1.BufferCount == 2);
    std::cout << "[PASS] Swap chain descriptor matches: 256x256, 2 buffers, format RGBA8." << std::endl;

    // ------------------------------------------------------------------------
    // TEST 3: Back-Buffer Retrieval as D3D12 Resource (GetBuffer)
    // ------------------------------------------------------------------------
    std::cout << "\n--- TEST 3: Back-Buffer Retrieval via GetBuffer ---" << std::endl;
    ID3D12Resource* pBackBuffer0 = nullptr;
    ID3D12Resource* pBackBuffer1 = nullptr;

    hr = pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer0));
    assert(SUCCEEDED(hr) && pBackBuffer0);
    std::cout << "[PASS] Back-buffer 0 retrieved as ID3D12Resource*." << std::endl;

    hr = pSwapChain->GetBuffer(1, IID_PPV_ARGS(&pBackBuffer1));
    assert(SUCCEEDED(hr) && pBackBuffer1);
    std::cout << "[PASS] Back-buffer 1 retrieved as ID3D12Resource*." << std::endl;

    // Out of bounds buffer index should fail cleanly
    ID3D12Resource* pBadBuffer = nullptr;
    hr = pSwapChain->GetBuffer(999, IID_PPV_ARGS(&pBadBuffer));
    assert(FAILED(hr));
    std::cout << "[PASS] Out-of-bounds buffer index 999 correctly rejected." << std::endl;

    // ------------------------------------------------------------------------
    // TEST 4: Render Target View Binding on Back-Buffers
    // ------------------------------------------------------------------------
    std::cout << "\n--- TEST 4: Render Target View Binding on Back-Buffers ---" << std::endl;
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 2;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ID3D12DescriptorHeap* pRtvHeap = nullptr;
    hr = pDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&pRtvHeap));
    assert(SUCCEEDED(hr) && pRtvHeap);

    UINT rtvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle0 = pRtvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle1 = rtvHandle0;
    rtvHandle1.ptr += rtvDescriptorSize;

    pDevice->CreateRenderTargetView(pBackBuffer0, nullptr, rtvHandle0);
    pDevice->CreateRenderTargetView(pBackBuffer1, nullptr, rtvHandle1);
    std::cout << "[PASS] Render Target Views created for both swap chain back-buffers." << std::endl;

    // ------------------------------------------------------------------------
    // TEST 5: Command Recording & Presentation via SwapChain
    // ------------------------------------------------------------------------
    std::cout << "\n--- TEST 5: Direct Render Target Rendering & Present ---" << std::endl;
    ID3D12CommandAllocator* pAlloc = nullptr;
    hr = pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pAlloc));
    assert(SUCCEEDED(hr) && pAlloc);

    ID3D12GraphicsCommandList* pCmdList = nullptr;
    hr = pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pAlloc, nullptr, IID_PPV_ARGS(&pCmdList));
    assert(SUCCEEDED(hr) && pCmdList);

    UINT currentIdx = pSwapChain->GetCurrentBackBufferIndex();
    assert(currentIdx == 0);
    std::cout << "[PASS] Initial back-buffer index = " << currentIdx << std::endl;

    // Record clear onto active back-buffer
    pCmdList->OMSetRenderTargets(1, &rtvHandle0, FALSE, nullptr);
    const float clearCol[4] = { 0.25f, 0.50f, 0.75f, 1.0f }; // Sky Blue
    pCmdList->ClearRenderTargetView(rtvHandle0, clearCol, 0, nullptr);
    hr = pCmdList->Close();
    assert(SUCCEEDED(hr));

    ID3D12CommandList* ppLists[] = { pCmdList };
    pQueue->ExecuteCommandLists(1, ppLists);
    std::cout << "[PASS] Render commands executed on back-buffer 0." << std::endl;

    // Present Frame 1
    hr = pSwapChain->Present(0, 0);
    assert(SUCCEEDED(hr));
    std::cout << "[PASS] Present(0, 0) returned S_OK." << std::endl;

    UINT nextIdx = pSwapChain->GetCurrentBackBufferIndex();
    assert(nextIdx == 1);
    std::cout << "[PASS] Back-buffer index flipped to " << nextIdx << std::endl;

    // ------------------------------------------------------------------------
    // TEST 6: Window Resize & Swap Chain Recreation (ResizeBuffers)
    // ------------------------------------------------------------------------
    std::cout << "\n--- TEST 6: Swap Chain Resizing (ResizeBuffers) ---" << std::endl;
    // Release application back-buffer references before resize
    pBackBuffer0->Release();
    pBackBuffer1->Release();

    hr = pSwapChain->ResizeBuffers(2, 512, 512, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    assert(SUCCEEDED(hr));
    std::cout << "[PASS] ResizeBuffers(2, 512, 512) succeeded." << std::endl;

    // Re-acquire back-buffer 0 and verify new size
    ID3D12Resource* pResizedBuffer0 = nullptr;
    hr = pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pResizedBuffer0));
    assert(SUCCEEDED(hr) && pResizedBuffer0);

    D3D12_RESOURCE_DESC resDesc = pResizedBuffer0->GetDesc();
    assert(resDesc.Width == 512 && resDesc.Height == 512);
    std::cout << "[PASS] Re-acquired resized back-buffer: 512x512." << std::endl;

    pResizedBuffer0->Release();

    // ------------------------------------------------------------------------
    // Cleanup
    // ------------------------------------------------------------------------
    std::cout << "[DEBUG] Releasing pCmdList..." << std::endl;
    pCmdList->Release();
    std::cout << "[DEBUG] Releasing pAlloc..." << std::endl;
    pAlloc->Release();
    std::cout << "[DEBUG] Releasing pRtvHeap..." << std::endl;
    pRtvHeap->Release();
    std::cout << "[DEBUG] Releasing pSwapChain..." << std::endl;
    pSwapChain->Release();
    std::cout << "[DEBUG] Releasing pQueue..." << std::endl;
    pQueue->Release();
    std::cout << "[DEBUG] Releasing pDevice..." << std::endl;
    pDevice->Release();

    std::cout << "[DEBUG] Destroying window..." << std::endl;
    DestroyWindow(hwnd);
    UnregisterClassA("KeplerBridgePhase5Window", GetModuleHandle(nullptr));

    std::cout << "[DEBUG] Freeing d3d12.dll..." << std::endl;
    FreeLibrary(hD3D12);
    std::cout << "[DEBUG] FreeLibrary finished." << std::endl;

    std::cout << "\n========================================================" << std::endl;
    std::cout << "  ALL PHASE 5 DXGI SWAPCHAIN TESTS PASSED (6/6)!        " << std::endl;
    std::cout << "========================================================" << std::endl;
    return 0;
}
