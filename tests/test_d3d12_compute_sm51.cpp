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

typedef HRESULT(WINAPI* PFN_KeplerBridge_ReadbackBuffer)(
    ID3D12Resource* pD3D12Resource,
    void* pOutData,
    UINT byteSize);

typedef void(WINAPI* PFN_KeplerBridge_GetTranslationStats)(
    UINT* pTranslatedCount,
    UINT* pRejectedCount);

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << " KeplerBridge Phase 11I SM 5.1 Compute & Math Readback Proof" << std::endl;
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
    auto pfnReadbackBuffer = reinterpret_cast<PFN_KeplerBridge_ReadbackBuffer>(
        GetProcAddress(hD3D12, "KeplerBridge_ReadbackBuffer"));
    auto pfnGetStats = reinterpret_cast<PFN_KeplerBridge_GetTranslationStats>(
        GetProcAddress(hD3D12, "KeplerBridge_GetTranslationStats"));

    if (!pfnCreateDevice || !pfnSerializeRootSignature || !pfnReadbackBuffer || !pfnGetStats) {
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
    auto pfnCompile = reinterpret_cast<pD3DCompile>(
        GetProcAddress(hCompiler, "D3DCompile"));

    // Step 1: Device
    ID3D12Device* pDevice = nullptr;
    HRESULT hr = pfnCreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice));
    assert(SUCCEEDED(hr) && pDevice);
    std::cout << "[PASS] D3D12 Device created via KeplerBridge." << std::endl;

    // Step 2: Command Infrastructure
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

    // Step 3: UAV Descriptor Heap
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ID3D12DescriptorHeap* pUavHeap = nullptr;
    hr = pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&pUavHeap));
    assert(SUCCEEDED(hr) && pUavHeap);
    D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = pUavHeap->GetCPUDescriptorHandleForHeapStart();

    // Step 4: Committed Buffer for UAV output
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

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavViewDesc = {};
    uavViewDesc.Format = DXGI_FORMAT_R32_UINT;
    uavViewDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavViewDesc.Buffer.FirstElement = 0;
    uavViewDesc.Buffer.NumElements = NUM_ELEMENTS;
    uavViewDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    pDevice->CreateUnorderedAccessView(pOutputBuffer, nullptr, &uavViewDesc, uavHandle);

    // Step 5: Root Signature
    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0; // register(u0)
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rootParams[2] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].Constants.Num32BitValues = 2;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

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
    assert(SUCCEEDED(hr) && pSigBlob);

    ID3D12RootSignature* pRootSig = nullptr;
    hr = pDevice->CreateRootSignature(0, pSigBlob->GetBufferPointer(), pSigBlob->GetBufferSize(), IID_PPV_ARGS(&pRootSig));
    assert(SUCCEEDED(hr) && pRootSig);
    pSigBlob->Release();

    // Step 6: Compile Authentic SM 5.1 Compute Shader
    std::cout << "\n--- Compiling Authentic SM 5.1 Compute Shader ---" << std::endl;
    const char* hlslCS = R"(
        cbuffer Params : register(b0, space0) {
            uint g_multiplier;
            uint g_offset;
        };

        RWBuffer<uint> OutputBuffer : register(u0, space0);

        [numthreads(64, 1, 1)]
        void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
            uint id = dispatchThreadId.x;
            OutputBuffer[id] = (id * g_multiplier) + g_offset;
        }
    )";

    ID3DBlob* pCSBlob = nullptr;
    ID3DBlob* pCompilerError = nullptr;
    hr = pfnCompile(hlslCS, strlen(hlslCS), "ComputeShaderSM51", nullptr, nullptr, "CSMain", "cs_5_1", 0, 0, &pCSBlob, &pCompilerError);
    if (FAILED(hr) || !pCSBlob) {
        if (pCompilerError) {
            std::cerr << "CS Compile Error: " << (char*)pCompilerError->GetBufferPointer() << std::endl;
            pCompilerError->Release();
        }
        return 1;
    }
    std::cout << "[PASS] Compiled authentic cs_5_1 shader (" << pCSBlob->GetBufferSize() << " bytes)." << std::endl;

    // Step 7: Create Compute Pipeline State (lowered automatically by KeplerBridge!)
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = pRootSig;
    psoDesc.CS = { pCSBlob->GetBufferPointer(), pCSBlob->GetBufferSize() };

    ID3D12PipelineState* pPSO = nullptr;
    hr = pDevice->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pPSO));
    if (FAILED(hr) || !pPSO) {
        std::cerr << "[FAIL] CreateComputePipelineState with SM 5.1 CS failed: 0x" << std::hex << hr << std::endl;
        return 1;
    }
    std::cout << "[PASS] Compute Pipeline State created with lowered SM 5.1 shader!" << std::endl;

    // Step 8: Dispatch Commands
    pCmdList->SetPipelineState(pPSO);
    pCmdList->SetComputeRootSignature(pRootSig);

    const uint32_t MULTIPLIER = 11;
    const uint32_t OFFSET = 17;
    uint32_t rootConstants[2] = { MULTIPLIER, OFFSET };
    pCmdList->SetComputeRoot32BitConstants(0, 2, rootConstants, 0);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuUavHandle = { uavHandle.ptr };
    pCmdList->SetComputeRootDescriptorTable(1, gpuUavHandle);
    pCmdList->Dispatch(1, 1, 1);

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
    std::cout << "[PASS] Compute Queue execution & fence synchronization completed." << std::endl;

    // Step 9: Readback & Verify 64 Mathematical Results
    std::vector<uint32_t> results(NUM_ELEMENTS, 0);
    hr = pfnReadbackBuffer(pOutputBuffer, results.data(), BUFFER_BYTES);
    if (FAILED(hr)) {
        std::cerr << "[FAIL] KeplerBridge_ReadbackBuffer failed: hr = 0x" << std::hex << hr << std::endl;
        return 1;
    }

    bool allMatch = true;
    for (UINT i = 0; i < NUM_ELEMENTS; ++i) {
        uint32_t expected = (i * MULTIPLIER) + OFFSET;
        if (results[i] != expected) {
            std::cerr << "[FAIL] Mismatch at index " << i << ": got " << results[i] << ", expected " << expected << std::endl;
            allMatch = false;
        }
    }

    if (allMatch) {
        std::cout << "[PASS] All 64 mathematical outputs match expected: (id * 11) + 17 computed by SM 5.1 shader!" << std::endl;
    } else {
        return 1;
    }

    UINT translatedCount = 0, rejectedCount = 0;
    pfnGetStats(&translatedCount, &rejectedCount);
    std::cout << "[TRANSLATION STATS] Translated Commands: " << translatedCount
              << ", Rejected Commands: " << rejectedCount << std::endl;
    assert(translatedCount > 0 && rejectedCount == 0);

    // Cleanup
    pCSBlob->Release();
    pOutputBuffer->Release();
    pUavHeap->Release();
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
    std::cout << "  ALL SM 5.1 COMPUTE & MATH READBACK CHECKS PASSED!" << std::endl;
    std::cout << "========================================================" << std::endl;
    return 0;
}
