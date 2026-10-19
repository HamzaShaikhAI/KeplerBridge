#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <iostream>

typedef HRESULT (WINAPI *PFN_D3D12CreateDevice)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

int main() {
    std::cout << "[TEST] Starting KeplerBridge Phase 1 Ghost Hook Verification...\n";

    // Enumerate default hardware adapter via DXGI
    IDXGIFactory4* pFactory = nullptr;
    IDXGIAdapter1* pAdapter = nullptr;
    HRESULT hrFactory = CreateDXGIFactory1(IID_PPV_ARGS(&pFactory));
    if (SUCCEEDED(hrFactory) && pFactory) {
        pFactory->EnumAdapters1(0, &pAdapter);
    }

    if (pAdapter) {
        DXGI_ADAPTER_DESC1 desc;
        pAdapter->GetDesc1(&desc);
        std::wcout << L"[TEST] Found Primary GPU: " << desc.Description << L"\n";
    } else {
        std::cout << "[TEST] No hardware adapter found via DXGI, will test with nullptr.\n";
    }

    // Load local d3d12.dll proxy
    HMODULE hD3D12 = LoadLibraryA("d3d12.dll");
    if (!hD3D12) {
        std::cerr << "[TEST] Failed to load local d3d12.dll! Error: " << GetLastError() << "\n";
        return 1;
    }
    std::cout << "[TEST] Successfully loaded proxy d3d12.dll.\n";

    auto pfnCreateDevice = reinterpret_cast<PFN_D3D12CreateDevice>(GetProcAddress(hD3D12, "D3D12CreateDevice"));
    if (!pfnCreateDevice) {
        std::cerr << "[TEST] Failed to resolve D3D12CreateDevice!\n";
        return 1;
    }

    std::cout << "[TEST] Invoking D3D12CreateDevice with MinimumFeatureLevel = D3D_FEATURE_LEVEL_12_0...\n";
    ID3D12Device* pDevice = nullptr;
    HRESULT hr = pfnCreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&pDevice));

    std::cout << "[TEST] D3D12CreateDevice returned hr = 0x" << std::hex << (unsigned long)hr << std::dec << "\n";

    if (pDevice) {
        pDevice->Release();
    }
    if (pAdapter) {
        pAdapter->Release();
    }
    if (pFactory) {
        pFactory->Release();
    }

    FreeLibrary(hD3D12);
    std::cout << "[TEST] Proxy test completed.\n";
    return 0;
}
