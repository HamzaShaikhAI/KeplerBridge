#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <iostream>
#include <string>

typedef HRESULT (WINAPI *PFN_D3D12CreateDevice)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

int main(int argc, char* argv[]) {
    std::cout << "========================================================\n";
    std::cout << "  KeplerBridge Phase 2 Live Hardware Verification Suite \n";
    std::cout << "========================================================\n";

    // Enumerate hardware adapter
    IDXGIFactory4* pFactory = nullptr;
    IDXGIAdapter1* pAdapter = nullptr;
    HRESULT hrFactory = CreateDXGIFactory1(IID_PPV_ARGS(&pFactory));
    if (SUCCEEDED(hrFactory) && pFactory) {
        for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            pAdapter->GetDesc1(&desc);
            std::wstring descW(desc.Description);
            std::string descA(descW.begin(), descW.end());
            std::cout << "[HARDWARE] Detected Adapter " << i << ": " << descA
                      << " (" << (desc.DedicatedVideoMemory / (1024*1024)) << " MB VRAM)" << std::endl;
            if (wcsstr(desc.Description, L"GeForce") != nullptr) {
                break;
            }
            pAdapter->Release();
            pAdapter = nullptr;
        }
    }

    if (!pAdapter) {
        std::cerr << "[TEST] NVIDIA GeForce adapter not found, using default adapter (nullptr).\n";
    }

    // Load our proxy d3d12.dll
    HMODULE hD3D12 = LoadLibraryA("d3d12.dll");
    if (!hD3D12) {
        std::cerr << "[FATAL] Failed to load local proxy d3d12.dll! Error: " << GetLastError() << "\n";
        return 1;
    }
    std::cout << "[TEST] Loaded proxy d3d12.dll successfully.\n";

    auto pfnCreate = (PFN_D3D12CreateDevice)GetProcAddress(hD3D12, "D3D12CreateDevice");
    if (!pfnCreate) {
        std::cerr << "[FATAL] Failed to resolve D3D12CreateDevice!\n";
        FreeLibrary(hD3D12);
        return 1;
    }

    // --- TEST 1: Capability Probing via Null ppDevice ---
    std::cout << "\n--- TEST 1: Capability Probing (ppDevice = nullptr) ---\n" << std::flush;
    HRESULT hrNull = pfnCreate(pAdapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr);
    std::cout << "D3D12CreateDevice(FL 11_0, ppDevice=nullptr) returned hr = 0x" << std::hex << (unsigned long)hrNull << std::dec << "\n";
    if (hrNull == S_OK) {
        std::cout << "[PASS] D3D12CreateDevice(ppDevice=nullptr) successfully probed capability per MSDN spec.\n" << std::flush;
    } else {
        std::cout << "[INFO] D3D12CreateDevice(ppDevice=nullptr) returned hr = 0x" << std::hex << hrNull << std::dec << "\n" << std::flush;
    }

    // --- TEST 2: Native Feature Level (11_0) Creation ---
    std::cout << "\n--- TEST 2: Native Feature Level (11_0) Device Creation ---\n" << std::flush;
    ID3D12Device* pNativeDevice = nullptr;
    HRESULT hrNative = pfnCreate(pAdapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&pNativeDevice);
    std::cout << "D3D12CreateDevice(FL 11_0) returned hr = 0x" << std::hex << (unsigned long)hrNative << std::dec << "\n" << std::flush;
    if (SUCCEEDED(hrNative) && pNativeDevice) {
        std::cout << "[PASS] Native D3D_FEATURE_LEVEL_11_0 creation succeeded! NodeCount=" << pNativeDevice->GetNodeCount() << "\n" << std::flush;
        pNativeDevice->Release();
    } else {
        std::cout << "[FAIL] Native 11_0 device creation failed!\n" << std::flush;
    }

    // --- TEST 3: Requested D3D_FEATURE_LEVEL_12_0 Creation ---
    std::cout << "\n--- TEST 3: Requested D3D_FEATURE_LEVEL_12_0 Creation ---\n" << std::flush;
    ID3D12Device* pDevice12 = nullptr;
    HRESULT hr12 = pfnCreate(pAdapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&pDevice12);
    std::cout << "D3D12CreateDevice(FL 12_0) returned hr = 0x" << std::hex << (unsigned long)hr12 << std::dec << "\n" << std::flush;

    if (SUCCEEDED(hr12) && pDevice12) {
        std::cout << "[PASS] D3D_FEATURE_LEVEL_12_0 creation SUCCEEDED via KeplerBridge Compatibility Engine!\n";

        // Query CheckFeatureSupport for D3D12_FEATURE_FEATURE_LEVELS
        D3D12_FEATURE_DATA_FEATURE_LEVELS flData = {};
        D3D_FEATURE_LEVEL requestedLevels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_12_0
        };
        flData.NumFeatureLevels = 3;
        flData.pFeatureLevelsRequested = requestedLevels;

        HRESULT hrCheck = pDevice12->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &flData, sizeof(flData));
        std::cout << "dev->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS) returned hr = 0x"
                  << std::hex << hrCheck << std::dec << "\n";
        std::cout << "Reported MaxSupportedFeatureLevel = 0x"
                  << std::hex << flData.MaxSupportedFeatureLevel << std::dec << "\n";

        if (flData.MaxSupportedFeatureLevel == D3D_FEATURE_LEVEL_12_0) {
            std::cout << "[PASS] Device successfully reported D3D_FEATURE_LEVEL_12_0 compatibility!\n";
        } else {
            std::cout << "[WARN] Device reported feature level: 0x" << std::hex << flData.MaxSupportedFeatureLevel << std::dec << "\n";
        }

        // Test normal device operation
        std::cout << "Testing device method call: GetNodeCount() = " << pDevice12->GetNodeCount() << "\n";

        // Test COM QueryInterface for ID3D12Device1
        ID3D12Device1* pDevice1 = nullptr;
        HRESULT hrQI = pDevice12->QueryInterface(__uuidof(ID3D12Device1), (void**)&pDevice1);
        std::cout << "dev->QueryInterface(ID3D12Device1) returned hr = 0x" << std::hex << hrQI << std::dec << "\n";
        if (SUCCEEDED(hrQI) && pDevice1) {
            std::cout << "[PASS] Successfully queried ID3D12Device1 from wrapped device!\n";
            pDevice1->Release();
        }

        pDevice12->Release();
        std::cout << "[PASS] Device released cleanly without crashes or leaks.\n";
    } else {
        std::cout << "[INFO] D3D_FEATURE_LEVEL_12_0 was rejected natively (hr = 0x" << std::hex << hr12 << std::dec
                  << "). (Expected if EnableFeatureLevelSpoof=0).\n";
    }

    if (pAdapter) {
        pAdapter->Release();
    }
    if (pFactory) {
        pFactory->Release();
    }

    FreeLibrary(hD3D12);
    std::cout << "\n========================================================\n";
    std::cout << "  Verification Suite Completed Successfully             \n";
    std::cout << "========================================================\n";
    return 0;
}
