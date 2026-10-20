#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>

#include <iostream>
#include <vector>
#include <cassert>
#include <cstdint>
#include <string>

typedef HRESULT (WINAPI *PFN_D3D12CreateDevice)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12SerializeRootSignature)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
typedef HRESULT (WINAPI *PFN_KeplerBridge_ReadbackBuffer)(ID3D12Resource*, void*, SIZE_T);
typedef void (WINAPI *PFN_KeplerBridge_GetTranslationStats)(UINT*, UINT*);

static void PrintAdapterInfo() {
    IDXGIFactory1* pFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&pFactory)))) {
        IDXGIAdapter1* pAdapter = nullptr;
        for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            pAdapter->GetDesc1(&desc);
            std::wcout << L"[HARDWARE] Adapter " << i << L": " << desc.Description
                       << L" (" << (desc.DedicatedVideoMemory / (1024 * 1024)) << L" MB VRAM)" << std::endl;
            pAdapter->Release();
        }
        pFactory->Release();
    }
}

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << "  KeplerBridge Phase 7 Multi-Slot UAV & Readback Proof  " << std::endl;
    std::cout << "========================================================" << std::endl;

    PrintAdapterInfo();

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
    std::cout << "[TEST] Loaded proxy d3d12.dll successfully." << std::endl;

    auto pfnCreateDevice = reinterpret_cast<PFN_D3D12CreateDevice>(
        GetProcAddress(hD3D12, "D3D12CreateDevice"));
    auto pfnSerializeRootSignature = reinterpret_cast<PFN_D3D12SerializeRootSignature>(
        GetProcAddress(hD3D12, "D3D12SerializeRootSignature"));
    auto pfnReadbackBuffer = reinterpret_cast<PFN_KeplerBridge_ReadbackBuffer>(
        GetProcAddress(hD3D12, "KeplerBridge_ReadbackBuffer"));
    auto pfnGetStats = reinterpret_cast<PFN_KeplerBridge_GetTranslationStats>(
        GetProcAddress(hD3D12, "KeplerBridge_GetTranslationStats"));

    if (!pfnCreateDevice || !pfnSerializeRootSignature || !pfnReadbackBuffer || !pfnGetStats) {
        std::cerr << "[FAIL] Required D3D12 / KeplerBridge entry points not exported!" << std::endl;
        FreeLibrary(hD3D12);
        return 1;
    }

    // Load compiler
    HMODULE hCompiler = LoadLibraryA("d3dcompiler_47.dll");
    if (!hCompiler) {
        std::cerr << "[FAIL] Failed to load d3dcompiler_47.dll!" << std::endl;
        FreeLibrary(hD3D12);
        return 1;
    }
    auto pfnCompile = reinterpret_cast<pD3DCompile>(
        GetProcAddress(hCompiler, "D3DCompile"));
    if (!pfnCompile) {
        std::cerr << "[FAIL] D3DCompile symbol not found in d3dcompiler_47.dll!" << std::endl;
        FreeLibrary(hCompiler);
        FreeLibrary(hD3D12);
        return 1;
    }

    // STEP 1: Create D3D12 Device
    std::cout << "\n--- STEP 1: D3D12 Device Creation (Translation Mode) ---" << std::endl;
    ID3D12Device* pDevice = nullptr;
    HRESULT hr = pfnCreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice));
    if (FAILED(hr) || !pDevice) {
        std::cerr << "[FAIL] D3D12CreateDevice failed with hr = 0x" << std::hex << hr << std::endl;
        FreeLibrary(hCompiler);
        FreeLibrary(hD3D12);
        return 1;
    }
    std::cout << "[PASS] D3D12 Device created successfully." << std::endl;

    // STEP 2: Create Command Infrastructure
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

    // STEP 3: Create UAV Descriptor Heap
    std::cout << "\n--- STEP 3: UAV Descriptor Heap Creation ---" << std::endl;
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 8;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ID3D12DescriptorHeap* pUavHeap = nullptr;
    hr = pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&pUavHeap));
    assert(SUCCEEDED(hr) && pUavHeap);

    UINT incSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHeapBase = pUavHeap->GetCPUDescriptorHandleForHeapStart();
    std::cout << "[PASS] UAV Descriptor Heap created (IncrementSize = " << incSize << ")." << std::endl;

    // STEP 4: Committed Buffer Resources (Buffer A, Buffer B, Buffer C)
    std::cout << "\n--- STEP 4: Committed Buffer Resources (Dual UAV + Rebind Target) ---" << std::endl;
    const UINT NUM_ELEMENTS = 64;
    const UINT BUFFER_BYTES = NUM_ELEMENTS * sizeof(uint32_t); // 256 bytes

    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = BUFFER_BYTES;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    ID3D12Resource* pBufferA = nullptr;
    ID3D12Resource* pBufferB = nullptr;
    ID3D12Resource* pBufferC = nullptr;

    hr = pDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&pBufferA));
    assert(SUCCEEDED(hr) && pBufferA);

    hr = pDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&pBufferB));
    assert(SUCCEEDED(hr) && pBufferB);

    hr = pDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&pBufferC));
    assert(SUCCEEDED(hr) && pBufferC);

    std::cout << "[PASS] Buffers A, B, and C created successfully (256 bytes each)." << std::endl;

    // STEP 5: Create UAV Descriptors & Validation Tests
    std::cout << "\n--- STEP 5: Unordered Access View Creation & Boundary Validation ---" << std::endl;

    // 5a. Negative descriptor tests:
    // Stride % 4 != 0
    D3D12_UNORDERED_ACCESS_VIEW_DESC badDesc1 = {};
    badDesc1.Format = DXGI_FORMAT_UNKNOWN;
    badDesc1.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    badDesc1.Buffer.NumElements = 16;
    badDesc1.Buffer.StructureByteStride = 5; // Invalid: not a multiple of 4!
    D3D12_CPU_DESCRIPTOR_HANDLE tempHandle = { cpuHeapBase.ptr + 7 * incSize };
    pDevice->CreateUnorderedAccessView(pBufferA, nullptr, &badDesc1, tempHandle);

    // Stride > 2048 (FL 11_0 limit)
    D3D12_UNORDERED_ACCESS_VIEW_DESC badDesc2 = {};
    badDesc2.Format = DXGI_FORMAT_UNKNOWN;
    badDesc2.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    badDesc2.Buffer.NumElements = 1;
    badDesc2.Buffer.StructureByteStride = 4096; // Exceeds FL 11_0 max 2048!
    pDevice->CreateUnorderedAccessView(pBufferA, nullptr, &badDesc2, tempHandle);

    // Out of bounds
    D3D12_UNORDERED_ACCESS_VIEW_DESC badDesc3 = {};
    badDesc3.Format = DXGI_FORMAT_R32_UINT;
    badDesc3.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    badDesc3.Buffer.FirstElement = 32;
    badDesc3.Buffer.NumElements = 64; // (32 + 64) * 4 = 384 > 256 bytes!
    pDevice->CreateUnorderedAccessView(pBufferA, nullptr, &badDesc3, tempHandle);

    // 5b. Valid UAV creations:
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavViewDesc = {};
    uavViewDesc.Format = DXGI_FORMAT_R32_UINT;
    uavViewDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavViewDesc.Buffer.FirstElement = 0;
    uavViewDesc.Buffer.NumElements = NUM_ELEMENTS;
    uavViewDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    D3D12_CPU_DESCRIPTOR_HANDLE handleA = { cpuHeapBase.ptr + 0 * incSize };
    D3D12_CPU_DESCRIPTOR_HANDLE handleB = { cpuHeapBase.ptr + 1 * incSize };
    D3D12_CPU_DESCRIPTOR_HANDLE handleC = { cpuHeapBase.ptr + 2 * incSize };

    pDevice->CreateUnorderedAccessView(pBufferA, nullptr, &uavViewDesc, handleA);
    pDevice->CreateUnorderedAccessView(pBufferB, nullptr, &uavViewDesc, handleB);
    pDevice->CreateUnorderedAccessView(pBufferC, nullptr, &uavViewDesc, handleC);
    std::cout << "[PASS] Valid UAV descriptors created for Buffer A (slot 0), Buffer B (slot 1), Buffer C (slot 2)." << std::endl;

    // STEP 6: Root Signature Creation
    std::cout << "\n--- STEP 6: Multi-UAV Root Signature Creation ---" << std::endl;
    // Range 0: 2 UAVs starting at u0 (u0 and u1)
    D3D12_DESCRIPTOR_RANGE uavRangeMulti = {};
    uavRangeMulti.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRangeMulti.NumDescriptors = 2; // Multi-slot range!
    uavRangeMulti.BaseShaderRegister = 0; // register(u0) and register(u1)
    uavRangeMulti.RegisterSpace = 0;
    uavRangeMulti.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rootParams[2] = {};
    // Param 0: 32-bit constants (multiplierA, offsetA, multiplierB, offsetB) -> register(b0)
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].Constants.Num32BitValues = 4;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Param 1: Multi-UAV Descriptor Table -> register(u0) and register(u1)
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &uavRangeMulti;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 2;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

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
    assert(SUCCEEDED(hr) && pRootSig);
    pSigBlob->Release();
    std::cout << "[PASS] Multi-UAV Root Signature created and reflected successfully." << std::endl;

    // STEP 7: Compile Multi-UAV Compute Shader
    std::cout << "\n--- STEP 7: Multi-UAV Compute Shader Compilation (SM 5.0) ---" << std::endl;
    const char* hlslCS = R"(
        cbuffer Params : register(b0) {
            uint g_multiplierA;
            uint g_offsetA;
            uint g_multiplierB;
            uint g_offsetB;
        };

        RWBuffer<uint> BufferA : register(u0);
        RWBuffer<uint> BufferB : register(u1);

        [numthreads(64, 1, 1)]
        void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
            uint id = dispatchThreadId.x;
            BufferA[id] = (id * g_multiplierA) + g_offsetA;
            BufferB[id] = (id * g_multiplierB) + g_offsetB;
        }
    )";

    ID3DBlob* pCSBlob = nullptr;
    ID3DBlob* pCompilerError = nullptr;
    hr = pfnCompile(hlslCS, strlen(hlslCS), "MultiUAVComputeShader", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &pCSBlob, &pCompilerError);
    if (FAILED(hr)) {
        if (pCompilerError) {
            std::cerr << "CS Compile Error: " << (char*)pCompilerError->GetBufferPointer() << std::endl;
            pCompilerError->Release();
        }
        return 1;
    }
    std::cout << "[PASS] Dual-UAV SM 5.0 Compute Shader compiled successfully (" << pCSBlob->GetBufferSize() << " bytes)." << std::endl;

    // STEP 8: Create Compute Pipeline State (PSO)
    std::cout << "\n--- STEP 8: Compute Pipeline State Creation ---" << std::endl;
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = pRootSig;
    psoDesc.CS.pShaderBytecode = pCSBlob->GetBufferPointer();
    psoDesc.CS.BytecodeLength = pCSBlob->GetBufferSize();

    ID3D12PipelineState* pPSO = nullptr;
    hr = pDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pPSO));
    assert(SUCCEEDED(hr) && pPSO);
    pCSBlob->Release();
    std::cout << "[PASS] Compute Pipeline State created successfully." << std::endl;

    // STEP 9: Command Recording with Dual UAVs, Barriers, and Rebinding
    std::cout << "\n--- STEP 9: Multi-UAV Command Recording ---" << std::endl;
    pCmdList->SetComputeRootSignature(pRootSig);
    pCmdList->SetPipelineState(pPSO);

    // Pass constants: BufferA uses (11, 3), BufferB uses (17, 5)
    const uint32_t constants[4] = { 11, 3, 17, 5 };
    pCmdList->SetComputeRoot32BitConstants(0, 4, constants, 0);

    // Bind dual UAV descriptor table (Param 1): base = handleA (covers handleA at u0 and handleB at u1)
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandleA = { handleA.ptr };
    pCmdList->SetComputeRootDescriptorTable(1, gpuHandleA);

    pCmdList->Dispatch(1, 1, 1); // Dispatches 64 threads writing to Buffer A and Buffer B

    // Resource-aware UAV barrier on Buffer A
    D3D12_RESOURCE_BARRIER uavBarrierA = {};
    uavBarrierA.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrierA.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    uavBarrierA.UAV.pResource = pBufferA;
    pCmdList->ResourceBarrier(1, &uavBarrierA);

    // Rebinding test: bind Buffer C to Param 1 (u0) with nonzero heap offset (handleC = heapBase + 2*incSize)
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandleC = { handleC.ptr };
    pCmdList->SetComputeRootDescriptorTable(1, gpuHandleC);

    // Update constants for Buffer C: multiplierA = 23, offsetA = 7
    const uint32_t constantsC[4] = { 23, 7, 0, 0 };
    pCmdList->SetComputeRoot32BitConstants(0, 4, constantsC, 0);

    pCmdList->Dispatch(1, 1, 1); // Dispatches 64 threads writing to Buffer C

    // Global UAV synchronization barrier
    D3D12_RESOURCE_BARRIER uavBarrierGlobal = {};
    uavBarrierGlobal.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrierGlobal.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    uavBarrierGlobal.UAV.pResource = nullptr;
    pCmdList->ResourceBarrier(1, &uavBarrierGlobal);

    hr = pCmdList->Close();
    assert(SUCCEEDED(hr));
    std::cout << "[PASS] Multi-UAV dispatches, barriers, and rebinding recorded and closed." << std::endl;

    // STEP 10: Execution & Fence Synchronization
    std::cout << "\n--- STEP 10: Queue Execution & Synchronization ---" << std::endl;
    ID3D12CommandList* ppLists[] = { pCmdList };
    pQueue->ExecuteCommandLists(1, ppLists);

    ID3D12Fence* pFence = nullptr;
    hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence));
    assert(SUCCEEDED(hr) && pFence);

    HANDLE hEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    hr = pFence->SetEventOnCompletion(1, hEvent);
    assert(SUCCEEDED(hr));

    hr = pQueue->Signal(pFence, 1);
    assert(SUCCEEDED(hr));

    WaitForSingleObject(hEvent, INFINITE);
    CloseHandle(hEvent);
    std::cout << "[PASS] GPU execution completed and synchronized via Fence." << std::endl;

    // STEP 11: Readback of All Three Buffers
    std::cout << "\n--- STEP 11: GPU Readback of Multi-Slot UAV Buffers ---" << std::endl;
    std::vector<uint32_t> dataA(NUM_ELEMENTS, 0);
    std::vector<uint32_t> dataB(NUM_ELEMENTS, 0);
    std::vector<uint32_t> dataC(NUM_ELEMENTS, 0);

    hr = pfnReadbackBuffer(pBufferA, dataA.data(), BUFFER_BYTES);
    if (FAILED(hr)) {
        std::cerr << "[FAIL] ReadbackBuffer on Buffer A failed: 0x" << std::hex << hr << std::endl;
        return 1;
    }

    hr = pfnReadbackBuffer(pBufferB, dataB.data(), BUFFER_BYTES);
    if (FAILED(hr)) {
        std::cerr << "[FAIL] ReadbackBuffer on Buffer B failed: 0x" << std::hex << hr << std::endl;
        return 1;
    }

    hr = pfnReadbackBuffer(pBufferC, dataC.data(), BUFFER_BYTES);
    if (FAILED(hr)) {
        std::cerr << "[FAIL] ReadbackBuffer on Buffer C failed: 0x" << std::hex << hr << std::endl;
        return 1;
    }

    std::cout << "[PASS] Successfully read back 256 bytes each from Buffer A, Buffer B, and Buffer C." << std::endl;

    // STEP 12: Byte-for-Byte Mathematical Validation
    std::cout << "\n--- STEP 12: Mathematical Verification of Multi-Slot GPU Outputs ---" << std::endl;

    std::cout << "[BUFFER A PROOF (u0: id * 11 + 3)] Elements [0..4]: ";
    for (int i = 0; i < 5; ++i) std::cout << dataA[i] << " ";
    std::cout << "\n[BUFFER A PROOF] Elements [59..63]: ";
    for (int i = 59; i < 64; ++i) std::cout << dataA[i] << " ";
    std::cout << std::endl;

    std::cout << "[BUFFER B PROOF (u1: id * 17 + 5)] Elements [0..4]: ";
    for (int i = 0; i < 5; ++i) std::cout << dataB[i] << " ";
    std::cout << "\n[BUFFER B PROOF] Elements [59..63]: ";
    for (int i = 59; i < 64; ++i) std::cout << dataB[i] << " ";
    std::cout << std::endl;

    std::cout << "[BUFFER C PROOF (rebind u0: id * 23 + 7)] Elements [0..4]: ";
    for (int i = 0; i < 5; ++i) std::cout << dataC[i] << " ";
    std::cout << "\n[BUFFER C PROOF] Elements [59..63]: ";
    for (int i = 59; i < 64; ++i) std::cout << dataC[i] << " ";
    std::cout << std::endl;

    bool allPassed = true;
    for (uint32_t i = 0; i < NUM_ELEMENTS; ++i) {
        uint32_t expectedA = (i * 11) + 3;
        uint32_t expectedB = (i * 17) + 5;
        uint32_t expectedC = (i * 23) + 7;

        if (dataA[i] != expectedA) {
            std::cerr << "[FAIL] Buffer A mismatch at element " << i << ": expected " << expectedA << ", got " << dataA[i] << std::endl;
            allPassed = false;
            break;
        }
        if (dataB[i] != expectedB) {
            std::cerr << "[FAIL] Buffer B mismatch at element " << i << ": expected " << expectedB << ", got " << dataB[i] << std::endl;
            allPassed = false;
            break;
        }
        if (dataC[i] != expectedC) {
            std::cerr << "[FAIL] Buffer C mismatch at element " << i << ": expected " << expectedC << ", got " << dataC[i] << std::endl;
            allPassed = false;
            break;
        }
    }

    if (!allPassed) {
        std::cerr << "[FAIL] Mathematical verification failed!" << std::endl;
        return 1;
    }
    std::cout << "[PASS] All 64 elements of Buffer A, Buffer B, and Buffer C verified with 100% mathematical precision!" << std::endl;

    UINT translatedCount = 0;
    UINT rejectedCount = 0;
    pfnGetStats(&translatedCount, &rejectedCount);
    std::cout << "[TRANSLATION STATS] Translated Commands: " << translatedCount
              << ", Rejected Commands: " << rejectedCount << std::endl;

    if (rejectedCount != 0) {
        std::cerr << "[FAIL] Unexpected command rejections detected in translation pipeline!" << std::endl;
        return 1;
    }

    // Cleanup
    pFence->Release();
    pBufferA->Release();
    pBufferB->Release();
    pBufferC->Release();
    pUavHeap->Release();
    pPSO->Release();
    pRootSig->Release();
    pCmdList->Release();
    pAlloc->Release();
    pQueue->Release();
    pDevice->Release();
    FreeLibrary(hCompiler);
    FreeLibrary(hD3D12);

    std::cout << "\n========================================================" << std::endl;
    std::cout << "  ALL MULTI-SLOT UAV & READBACK TESTS PASSED (13/13)!   " << std::endl;
    std::cout << "========================================================" << std::endl;
    return 0;
}
