#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <iostream>
#include <iomanip>

typedef HRESULT (WINAPI *PFN_D3D12GetInterface)(REFCLSID, REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12CreateDevice)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

int main() {
    std::cout << "========================================================\n";
    std::cout << "  KeplerBridge Phase 11D Agility SDK & DXGI Diagnostics \n";
    std::cout << "========================================================\n";

    // 1. Inspect System32 DXGI Adapter & Driver Info
    IDXGIFactory1* pFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&pFactory)))) {
        IDXGIAdapter1* pAdapter = nullptr;
        for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc;
            pAdapter->GetDesc1(&desc);
            std::wcout << L"Adapter " << i << L": " << desc.Description 
                       << L" (Vendor: 0x" << std::hex << desc.VendorId 
                       << L", Device: 0x" << desc.DeviceId << std::dec
                       << L", VRAM: " << (desc.DedicatedVideoMemory / (1024*1024)) << L" MB)\n";

            LARGE_INTEGER driverVer = {};
            HRESULT hrCheck = pAdapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVer);
            if (SUCCEEDED(hrCheck)) {
                WORD product  = HIWORD(driverVer.HighPart);
                WORD version  = LOWORD(driverVer.HighPart);
                WORD subVersion = HIWORD(driverVer.LowPart);
                WORD build    = LOWORD(driverVer.LowPart);
                std::cout << "  UMD Driver Version: " << product << "." << version 
                          << "." << subVersion << "." << build << "\n";
            } else {
                std::cout << "  CheckInterfaceSupport(IDXGIDevice) hr = 0x" << std::hex << hrCheck << std::dec << "\n";
            }
            pAdapter->Release();
        }
        pFactory->Release();
    }

    // 2. Load System32 d3d12.dll directly
    HMODULE hSysD3D12 = LoadLibraryA("C:\\WINDOWS\\system32\\d3d12.dll");
    if (!hSysD3D12) {
        std::cerr << "[FAIL] Failed to load System32 d3d12.dll!\n";
        return 1;
    }
    std::cout << "\n--- System32 d3d12.dll Tests ---\n";
    auto pfnGetInterface = (PFN_D3D12GetInterface)GetProcAddress(hSysD3D12, "D3D12GetInterface");
    auto pfnCreateDevice = (PFN_D3D12CreateDevice)GetProcAddress(hSysD3D12, "D3D12CreateDevice");

    std::cout << "D3D12GetInterface exported: " << (pfnGetInterface ? "YES" : "NO") << "\n";
    std::cout << "D3D12CreateDevice exported: " << (pfnCreateDevice ? "YES" : "NO") << "\n";

    if (pfnGetInterface) {
        // Query CLSID_D3D12SDKConfiguration / IID_ID3D12SDKConfiguration
        ID3D12SDKConfiguration* pSDKConfig = nullptr;
        HRESULT hr1 = pfnGetInterface(CLSID_D3D12SDKConfiguration, IID_PPV_ARGS(&pSDKConfig));
        std::cout << "D3D12GetInterface(CLSID_D3D12SDKConfiguration, IID_ID3D12SDKConfiguration): hr = 0x" << std::hex << hr1 << std::dec;
        if (SUCCEEDED(hr1) && pSDKConfig) {
            std::cout << " (SUCCESS, ptr=" << pSDKConfig << ")\n";
            pSDKConfig->Release();
        } else {
            std::cout << " (FAILED)\n";
        }

        // Query CLSID_D3D12SDKConfiguration / IID_ID3D12SDKConfiguration1
        ID3D12SDKConfiguration1* pSDKConfig1 = nullptr;
        HRESULT hr1_1 = pfnGetInterface(CLSID_D3D12SDKConfiguration, IID_PPV_ARGS(&pSDKConfig1));
        std::cout << "D3D12GetInterface(CLSID_D3D12SDKConfiguration, IID_ID3D12SDKConfiguration1): hr = 0x" << std::hex << hr1_1 << std::dec;
        if (SUCCEEDED(hr1_1) && pSDKConfig1) {
            std::cout << " (SUCCESS, ptr=" << pSDKConfig1 << ")\n";

            // Test CreateDeviceFactory with Palworld's path
            const char* palPath = "D:\\Program Files\\Palworld\\Palworld\\Pal\\Binaries\\Win64\\D3D12\\";
            ID3D12DeviceFactory* pFactoryObj = nullptr;
            HRESULT hrFac = pSDKConfig1->CreateDeviceFactory(602, palPath, IID_PPV_ARGS(&pFactoryObj));
            std::cout << "  pSDKConfig1->CreateDeviceFactory(602, palPath): hr = 0x" << std::hex << hrFac << std::dec;
            if (SUCCEEDED(hrFac) && pFactoryObj) {
                std::cout << " (SUCCESS, ptr=" << pFactoryObj << ")\n";

                // Null output pointer probe on Real Microsoft Factory
                HRESULT hrProbe11 = pFactoryObj->CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr);
                std::cout << "    Factory->CreateDevice(FL 11_0, nullptr probe): hr = 0x" << std::hex << hrProbe11 << std::dec << "\n";

                HRESULT hrProbe12 = pFactoryObj->CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr);
                std::cout << "    Factory->CreateDevice(FL 12_0, nullptr probe): hr = 0x" << std::hex << hrProbe12 << std::dec << "\n";

                // Probe CreateDevice with FL 11_0 and FL 12_0
                ID3D12Device* pDev11 = nullptr;
                HRESULT hrDev11 = pFactoryObj->CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDev11));
                std::cout << "    Factory->CreateDevice(FL 11_0): hr = 0x" << std::hex << hrDev11 << std::dec;
                if (SUCCEEDED(hrDev11) && pDev11) {
                    std::cout << " (SUCCESS, dev=" << pDev11 << ")\n";
                    pDev11->Release();
                } else {
                    std::cout << " (FAILED)\n";
                }

                ID3D12Device* pDev12 = nullptr;
                HRESULT hrDev12 = pFactoryObj->CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&pDev12));
                std::cout << "    Factory->CreateDevice(FL 12_0): hr = 0x" << std::hex << hrDev12 << std::dec;
                if (SUCCEEDED(hrDev12) && pDev12) {
                    std::cout << " (SUCCESS, dev=" << pDev12 << ")\n";
                    pDev12->Release();
                } else {
                    std::cout << " (FAILED)\n";
                }

                pFactoryObj->Release();
            } else {
                std::cout << " (FAILED)\n";
            }

            pSDKConfig1->Release();
        } else {
            std::cout << " (FAILED)\n";
        }

        // Query IID_ID3D12DeviceFactory directly from System32
        ID3D12DeviceFactory* pFactoryDirect = nullptr;
        HRESULT hr2 = pfnGetInterface(IID_ID3D12DeviceFactory, IID_PPV_ARGS(&pFactoryDirect));
        std::cout << "D3D12GetInterface(IID_ID3D12DeviceFactory): hr = 0x" << std::hex << hr2 << std::dec;
        if (SUCCEEDED(hr2) && pFactoryDirect) {
            std::cout << " (SUCCESS, ptr=" << pFactoryDirect << ")\n";
            pFactoryDirect->Release();
        } else {
            std::cout << " (FAILED)\n";
        }
    }

    if (pfnCreateDevice) {
        // Native FL 11_0 probe
        HRESULT hrFL11 = pfnCreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr);
        std::cout << "System32 D3D12CreateDevice(FL 11_0, probe): hr = 0x" << std::hex << hrFL11 << std::dec << "\n";

        // Native FL 12_0 probe
        HRESULT hrFL12 = pfnCreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr);
        std::cout << "System32 D3D12CreateDevice(FL 12_0, probe): hr = 0x" << std::hex << hrFL12 << std::dec << "\n";
    }

    FreeLibrary(hSysD3D12);

    // 3. Directly inspect Palworld's shipped Agility SDK (D3D12Core.dll)
    const char* coreDllPath = "D:\\Program Files\\Palworld\\Palworld\\Pal\\Binaries\\Win64\\D3D12\\D3D12Core.dll";
    HMODULE hCore = LoadLibraryA(coreDllPath);
    std::cout << "\n--- Palworld D3D12Core.dll Direct Inspection ---\n";
    std::cout << "LoadLibraryA(Palworld D3D12Core.dll): " << (hCore ? "SUCCESS" : "FAILED") << "\n";
    if (hCore) {
        auto pfnCoreGetInterface = (PFN_D3D12GetInterface)GetProcAddress(hCore, "D3D12GetInterface");
        std::cout << "  D3D12GetInterface exported: " << (pfnCoreGetInterface ? "YES" : "NO") << "\n";
        if (pfnCoreGetInterface) {
            ID3D12DeviceFactory* pCoreFactory = nullptr;
            HRESULT hrCoreFac = pfnCoreGetInterface(IID_ID3D12DeviceFactory, IID_PPV_ARGS(&pCoreFactory));
            std::cout << "  Core->D3D12GetInterface(IID_ID3D12DeviceFactory): hr = 0x" << std::hex << hrCoreFac << std::dec;
            if (SUCCEEDED(hrCoreFac) && pCoreFactory) {
                std::cout << " (SUCCESS, ptr=" << pCoreFactory << ")\n";

                ID3D12Device* pDev11 = nullptr;
                HRESULT hrDev11 = pCoreFactory->CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDev11));
                std::cout << "    CoreFactory->CreateDevice(FL 11_0): hr = 0x" << std::hex << hrDev11 << std::dec;
                if (SUCCEEDED(hrDev11) && pDev11) {
                    std::cout << " (SUCCESS, dev=" << pDev11 << ")\n";
                    pDev11->Release();
                } else {
                    std::cout << " (FAILED)\n";
                }

                ID3D12Device* pDev12 = nullptr;
                HRESULT hrDev12 = pCoreFactory->CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&pDev12));
                std::cout << "    CoreFactory->CreateDevice(FL 12_0): hr = 0x" << std::hex << hrDev12 << std::dec;
                if (SUCCEEDED(hrDev12) && pDev12) {
                    std::cout << " (SUCCESS, dev=" << pDev12 << ")\n";
                    pDev12->Release();
                } else {
                    std::cout << " (FAILED)\n";
                }

                pCoreFactory->Release();
            } else {
                std::cout << " (FAILED)\n";
            }
        }
        FreeLibrary(hCore);
    }

    std::cout << "========================================================\n";
    return 0;
}
