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
    std::cout << "  KeplerBridge Phase 6.1 Compute Shader & UAV Readback Proof" << std::endl;
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

    // Load d3dcompiler_47.dll
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

    // Step 1: Create D3D12 Device in translation mode
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

    // Step 2: Create Command Infrastructure
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

    // Step 3: Create CBV/SRV/UAV Descriptor Heap
    std::cout << "\n--- STEP 3: UAV Descriptor Heap Creation ---" << std::endl;
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ID3D12DescriptorHeap* pUavHeap = nullptr;
    hr = pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&pUavHeap));
    assert(SUCCEEDED(hr) && pUavHeap);
    D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = pUavHeap->GetCPUDescriptorHandleForHeapStart();
    std::cout << "[PASS] UAV Descriptor Heap created successfully." << std::endl;

    // Step 4: Create Committed Buffer for UAV output
    std::cout << "\n--- STEP 4: Committed Buffer Resource Creation (UAV) ---" << std::endl;
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

    ID3D12Resource* pOutputBuffer = nullptr;
    hr = pDevice->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&pOutputBuffer));
    assert(SUCCEEDED(hr) && pOutputBuffer);
    std::cout << "[PASS] Committed Buffer (256 bytes, UAV) created successfully." << std::endl;

    // Step 5: Create Unordered Access View
    std::cout << "\n--- STEP 5: Unordered Access View Creation ---" << std::endl;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavViewDesc = {};
    uavViewDesc.Format = DXGI_FORMAT_R32_UINT;
    uavViewDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavViewDesc.Buffer.FirstElement = 0;
    uavViewDesc.Buffer.NumElements = NUM_ELEMENTS;
    uavViewDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    pDevice->CreateUnorderedAccessView(pOutputBuffer, nullptr, &uavViewDesc, uavHandle);
    std::cout << "[PASS] CreateUnorderedAccessView on Buffer completed." << std::endl;

    // Step 6: Create Root Signature
    std::cout << "\n--- STEP 6: Root Signature Creation ---" << std::endl;
    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0; // register(u0)
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rootParams[2] = {};
    // Param 0: 32-bit Root Constants (multiplier, offset) -> register(b0)
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].Constants.Num32BitValues = 2;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Param 1: UAV Descriptor Table -> register(u0)
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &uavRange;
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
    std::cout << "[PASS] Root Signature created and reflected successfully." << std::endl;

    // Step 7: Compile HLSL Compute Shader
    std::cout << "\n--- STEP 7: Compute Shader Compilation (SM 5.0) ---" << std::endl;
    const char* hlslCS = R"(
        cbuffer Params : register(b0) {
            uint g_multiplier;
            uint g_offset;
        };

        RWBuffer<uint> OutputBuffer : register(u0);

        [numthreads(64, 1, 1)]
        void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
            uint id = dispatchThreadId.x;
            OutputBuffer[id] = (id * g_multiplier) + g_offset;
        }
    )";

    ID3DBlob* pCSBlob = nullptr;
    ID3DBlob* pCompilerError = nullptr;
    hr = pfnCompile(hlslCS, strlen(hlslCS), "ComputeShader", nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &pCSBlob, &pCompilerError);
    if (FAILED(hr)) {
        if (pCompilerError) {
            std::cerr << "CS Compile Error: " << (char*)pCompilerError->GetBufferPointer() << std::endl;
            pCompilerError->Release();
        }
        return 1;
    }
    std::cout << "[PASS] SM 5.0 Compute Shader compiled successfully (" << pCSBlob->GetBufferSize() << " bytes)." << std::endl;

    // Step 8: Create Compute Pipeline State (PSO)
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

    // Step 9: Record Compute Commands
    std::cout << "\n--- STEP 9: Command Recording ---" << std::endl;
    pCmdList->SetComputeRootSignature(pRootSig);
    pCmdList->SetPipelineState(pPSO);

    const uint32_t constants[2] = { 7, 13 }; // g_multiplier = 7, g_offset = 13
    pCmdList->SetComputeRoot32BitConstants(0, 2, constants, 0);

    D3D12_GPU_DESCRIPTOR_HANDLE gpuUavHandle = { uavHandle.ptr };
    pCmdList->SetComputeRootDescriptorTable(1, gpuUavHandle);

    pCmdList->Dispatch(1, 1, 1); // 1 thread group of 64 threads = 64 elements

    hr = pCmdList->Close();
    assert(SUCCEEDED(hr));
    std::cout << "[PASS] SetComputeRootSignature, SetPSO, SetConstants, SetTable, Dispatch(1,1,1) recorded and Closed." << std::endl;

    // Step 10: Execute Commands and Synchronize
    std::cout << "\n--- STEP 10: Command Execution & Synchronization ---" << std::endl;
    ID3D12CommandList* ppLists[] = { pCmdList };
    pQueue->ExecuteCommandLists(1, ppLists);

    ID3D12Fence* pFence = nullptr;
    hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence));
    assert(SUCCEEDED(hr) && pFence);
    pQueue->Signal(pFence, 1);
    while (pFence->GetCompletedValue() < 1) {
        Sleep(1);
    }
    std::cout << "[PASS] Compute queue execution completed and synchronized via Fence." << std::endl;

    // Step 11: Readback Buffer Data
    std::cout << "\n--- STEP 11: Buffer Readback via Staging Resource ---" << std::endl;
    std::vector<uint32_t> results(NUM_ELEMENTS, 0);
    hr = pfnReadbackBuffer(pOutputBuffer, results.data(), BUFFER_BYTES);
    if (FAILED(hr)) {
        std::cerr << "[FAIL] KeplerBridge_ReadbackBuffer failed: hr = 0x" << std::hex << hr << std::dec << std::endl;
        return 1;
    }
    std::cout << "[PASS] Successfully read back " << BUFFER_BYTES << " bytes (" << results.size() << " elements)." << std::endl;

    // Step 12: Empirical Verification of Mathematical Results
    std::cout << "\n--- STEP 12: Mathematical Verification of GPU Output ---" << std::endl;
    bool allCorrect = true;
    for (UINT i = 0; i < NUM_ELEMENTS; ++i) {
        uint32_t expected = (i * 7) + 13;
        if (results[i] != expected) {
            std::cerr << "[FAIL] Output mismatch at element [" << i << "]: got "
                      << results[i] << ", expected " << expected << std::endl;
            allCorrect = false;
            break;
        }
    }
    assert(allCorrect);

    // Print first 5 and last 5 elements as proof
    std::cout << "[COMPUTE PROOF] Elements [0..4]: ";
    for (UINT i = 0; i < 5; ++i) {
        std::cout << results[i] << " ";
    }
    std::cout << "\n[COMPUTE PROOF] Elements [59..63]: ";
    for (UINT i = 59; i < 64; ++i) {
        std::cout << results[i] << " ";
    }
    std::cout << std::endl;
    std::cout << "[PASS] All 64 mathematical outputs verified perfectly against (id * 7) + 13!" << std::endl;

    UINT translated = 0, rejected = 0;
    pfnGetStats(&translated, &rejected);
    std::cout << "[TRANSLATION STATS] Translated Commands: " << translated
              << ", Rejected Commands: " << rejected << std::endl;

    // Teardown
    pFence->Release();
    pPSO->Release();
    pRootSig->Release();
    pOutputBuffer->Release();
    pUavHeap->Release();
    pCmdList->Release();
    pAlloc->Release();
    pQueue->Release();
    pDevice->Release();
    FreeLibrary(hCompiler);
    FreeLibrary(hD3D12);

    std::cout << "\n========================================================" << std::endl;
    std::cout << "  ALL COMPUTE & READBACK PROOFS PASSED (12/12)!         " << std::endl;
    std::cout << "========================================================" << std::endl;
    return 0;
}
