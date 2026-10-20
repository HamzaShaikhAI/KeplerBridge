#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <iostream>
#include <string>

int main() {
    std::cout << "========================================================\n";
    std::cout << "  KeplerBridge Test B — Standalone D3D11 Initialization  \n";
    std::cout << "========================================================\n" << std::flush;

    IDXGIFactory4* pFactory = nullptr;
    HRESULT hrFactory = CreateDXGIFactory1(IID_PPV_ARGS(&pFactory));
    if (FAILED(hrFactory) || !pFactory) {
        std::cerr << "[FAIL] Failed to create DXGI factory! hr = 0x" << std::hex << hrFactory << std::dec << "\n";
        return 1;
    }

    IDXGIAdapter1* pAdapter = nullptr;
    for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        pAdapter->GetDesc1(&desc);
        std::wstring descW(desc.Description);
        std::string descA(descW.begin(), descW.end());
        std::cout << "[HARDWARE] Adapter " << i << ": " << descA
                  << " (" << (desc.DedicatedVideoMemory / (1024 * 1024)) << " MB VRAM)\n" << std::flush;
        if (wcsstr(desc.Description, L"GeForce") != nullptr) {
            break;
        }
        pAdapter->Release();
        pAdapter = nullptr;
    }

    // 1. D3D11 with default adapter (nullptr)
    std::cout << "\n--- Subtest B1: D3D11CreateDevice with nullptr adapter ---\n" << std::flush;
    ID3D11Device* pDeviceDefault = nullptr;
    ID3D11DeviceContext* pContextDefault = nullptr;
    D3D_FEATURE_LEVEL flOutDefault = D3D_FEATURE_LEVEL_9_1;
    D3D_FEATURE_LEVEL flRequested[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    HRESULT hrDef = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        flRequested,
        _countof(flRequested),
        D3D11_SDK_VERSION,
        &pDeviceDefault,
        &flOutDefault,
        &pContextDefault);

    std::cout << "D3D11CreateDevice(nullptr, HARDWARE) returned hr = 0x" << std::hex << hrDef << std::dec
              << ", Feature Level = 0x" << std::hex << flOutDefault << std::dec << "\n" << std::flush;
    if (SUCCEEDED(hrDef) && pDeviceDefault) {
        std::cout << "[PASS] Subtest B1 succeeded!\n" << std::flush;
        pContextDefault->Release();
        pDeviceDefault->Release();
    } else {
        std::cout << "[FAIL] Subtest B1 failed!\n" << std::flush;
    }

    // 2. D3D11 with explicit adapter pointer
    if (pAdapter) {
        std::cout << "\n--- Subtest B2: D3D11CreateDevice with explicit adapter pointer ---\n" << std::flush;
        ID3D11Device* pDeviceExplicit = nullptr;
        ID3D11DeviceContext* pContextExplicit = nullptr;
        D3D_FEATURE_LEVEL flOutExplicit = D3D_FEATURE_LEVEL_9_1;

        // When passing an explicit adapter, DriverType MUST be D3D_DRIVER_TYPE_UNKNOWN per MSDN
        HRESULT hrExp = D3D11CreateDevice(
            pAdapter,
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            0,
            flRequested,
            _countof(flRequested),
            D3D11_SDK_VERSION,
            &pDeviceExplicit,
            &flOutExplicit,
            &pContextExplicit);

        std::cout << "D3D11CreateDevice(pAdapter, UNKNOWN) returned hr = 0x" << std::hex << hrExp << std::dec
                  << ", Feature Level = 0x" << std::hex << flOutExplicit << std::dec << "\n" << std::flush;
        if (SUCCEEDED(hrExp) && pDeviceExplicit) {
            std::cout << "[PASS] Subtest B2 succeeded!\n" << std::flush;
            pContextExplicit->Release();
            pDeviceExplicit->Release();
        } else {
            std::cout << "[FAIL] Subtest B2 failed!\n" << std::flush;
        }

        pAdapter->Release();
    }

    pFactory->Release();
    std::cout << "\n========================================================\n";
    std::cout << "  Test B Completed Successfully                         \n";
    std::cout << "========================================================\n" << std::flush;
    return 0;
}
