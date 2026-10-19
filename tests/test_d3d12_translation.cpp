#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <iostream>
#include <cassert>
#include <vector>

typedef HRESULT(WINAPI* PFN_D3D12CreateDevice)(
    IUnknown* pAdapter,
    D3D_FEATURE_LEVEL MinimumFeatureLevel,
    REFIID riid,
    void** ppDevice);

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

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << "  KeplerBridge Phase 4 Live Translation Integration     " << std::endl;
    std::cout << "========================================================" << std::endl;

    PrintAdapterInfo();

    HMODULE hD3D12 = LoadLibraryA("d3d12.dll");
    if (!hD3D12) {
        std::cerr << "[FAIL] Could not load proxy d3d12.dll!" << std::endl;
        return 1;
    }
    std::cout << "[TEST] Loaded proxy d3d12.dll successfully." << std::endl;

    auto pfnCreateDevice = reinterpret_cast<PFN_D3D12CreateDevice>(
        GetProcAddress(hD3D12, "D3D12CreateDevice"));
    if (!pfnCreateDevice) {
        std::cerr << "[FAIL] D3D12CreateDevice not exported!" << std::endl;
        FreeLibrary(hD3D12);
        return 1;
    }

    // Acquire native D3D12 device
    ID3D12Device* pDevice = nullptr;
    HRESULT hr = pfnCreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice));
    if (FAILED(hr) || !pDevice) {
        std::cerr << "[FAIL] Failed to create D3D12 device: hr = 0x" << std::hex << hr << std::dec << std::endl;
        FreeLibrary(hD3D12);
        return 1;
    }
    std::cout << "[PASS] D3D12 device acquired successfully." << std::endl;

    // 1. Create Command Queue
    std::cout << "\n--- TEST 1: Command Queue Creation ---" << std::endl;
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    ID3D12CommandQueue* pCommandQueue = nullptr;
    hr = pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&pCommandQueue));
    std::cout << "CreateCommandQueue returned hr = 0x" << std::hex << hr << std::dec << std::endl;
    assert(SUCCEEDED(hr) && pCommandQueue != nullptr);
    std::cout << "[PASS] Command Queue created successfully." << std::endl;

    // 2. Create Command Allocator
    std::cout << "\n--- TEST 2: Command Allocator Creation ---" << std::endl;
    ID3D12CommandAllocator* pCommandAllocator = nullptr;
    hr = pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pCommandAllocator));
    std::cout << "CreateCommandAllocator returned hr = 0x" << std::hex << hr << std::dec << std::endl;
    assert(SUCCEEDED(hr) && pCommandAllocator != nullptr);
    std::cout << "[PASS] Command Allocator created successfully." << std::endl;

    // 3. Create Graphics Command List
    std::cout << "\n--- TEST 3: Graphics Command List Creation ---" << std::endl;
    ID3D12GraphicsCommandList* pCommandList = nullptr;
    hr = pDevice->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        pCommandAllocator,
        nullptr,
        IID_PPV_ARGS(&pCommandList));
    std::cout << "CreateCommandList returned hr = 0x" << std::hex << hr << std::dec << std::endl;
    assert(SUCCEEDED(hr) && pCommandList != nullptr);
    std::cout << "[PASS] Graphics Command List created successfully." << std::endl;

    // 4. Create Committed Buffer Resource
    std::cout << "\n--- TEST 4: Committed Buffer Resource Creation ---" << std::endl;
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width = 65536; // 64 KB
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ID3D12Resource* pBuffer = nullptr;
    hr = pDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&pBuffer));
    std::cout << "CreateCommittedResource returned hr = 0x" << std::hex << hr << std::dec << std::endl;
    assert(SUCCEEDED(hr) && pBuffer != nullptr);
    std::cout << "[PASS] Buffer resource created successfully." << std::endl;

    // 5. Command Recording & Resource Barriers
    std::cout << "\n--- TEST 5: Command Recording & State Transitions ---" << std::endl;
    D3D12_RESOURCE_BARRIER barrierToDest = {};
    barrierToDest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierToDest.Transition.pResource = pBuffer;
    barrierToDest.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrierToDest.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barrierToDest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    pCommandList->ResourceBarrier(1, &barrierToDest);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f };
    pCommandList->RSSetViewports(1, &vp);

    D3D12_RECT scissor = { 0, 0, 1280, 720 };
    pCommandList->RSSetScissorRects(1, &scissor);

    pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_RESOURCE_BARRIER barrierToCommon = {};
    barrierToCommon.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierToCommon.Transition.pResource = pBuffer;
    barrierToCommon.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrierToCommon.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    barrierToCommon.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    pCommandList->ResourceBarrier(1, &barrierToCommon);

    hr = pCommandList->Close();
    std::cout << "CommandList::Close returned hr = 0x" << std::hex << hr << std::dec << std::endl;
    assert(SUCCEEDED(hr));
    std::cout << "[PASS] Command List recording and Close completed." << std::endl;

    // 6. Execute Command List on Queue
    std::cout << "\n--- TEST 6: Command Execution on Queue ---" << std::endl;
    ID3D12CommandList* ppCommandLists[] = { pCommandList };
    pCommandQueue->ExecuteCommandLists(1, ppCommandLists);
    std::cout << "[PASS] Command Queue execution completed without error." << std::endl;

    // 7. Synchronization via Fence
    std::cout << "\n--- TEST 7: GPU Synchronization via Fence ---" << std::endl;
    ID3D12Fence* pFence = nullptr;
    hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence));
    assert(SUCCEEDED(hr) && pFence != nullptr);

    hr = pCommandQueue->Signal(pFence, 1);
    assert(SUCCEEDED(hr));

    HANDLE hEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (pFence->GetCompletedValue() < 1) {
        pFence->SetEventOnCompletion(1, hEvent);
        WaitForSingleObject(hEvent, 2000); // 2 second timeout
    }
    CloseHandle(hEvent);

    assert(pFence->GetCompletedValue() >= 1);
    std::cout << "[PASS] Fence signal & wait synchronization verified! Completed value = "
              << pFence->GetCompletedValue() << std::endl;

    // Cleanup
    pFence->Release();
    pBuffer->Release();
    pCommandList->Release();
    pCommandAllocator->Release();
    pCommandQueue->Release();
    pDevice->Release();
    FreeLibrary(hD3D12);

    std::cout << "\n========================================================" << std::endl;
    std::cout << "  Phase 4 Live Translation Suite Completed Successfully " << std::endl;
    std::cout << "========================================================" << std::endl;
    return 0;
}
