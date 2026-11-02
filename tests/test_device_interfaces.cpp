#include <windows.h>
#include <d3d12.h>
#include <iostream>
#include <cassert>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << std::endl; \
            return 1; \
        } \
    } while(0)

typedef HRESULT (WINAPI *PFN_D3D12CreateDevice)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

int main() {
    std::cout << "========================================================\n" << std::flush;
    std::cout << "  KeplerBridge Phase 11F.1 Device Interfaces Test Suite  \n" << std::flush;
    std::cout << "========================================================\n" << std::flush;

    HMODULE hProxy = LoadLibraryA("build\\d3d12.dll");
    if (!hProxy) {
        hProxy = LoadLibraryA("d3d12.dll");
    }
    if (!hProxy) {
        std::cerr << "[FAIL] Failed to load d3d12.dll proxy (err: " << GetLastError() << ")\n" << std::flush;
        return 1;
    }
    std::cout << "[INFO] Loaded proxy d3d12.dll\n" << std::flush;

    PFN_D3D12CreateDevice pfnCreate = (PFN_D3D12CreateDevice)GetProcAddress(hProxy, "D3D12CreateDevice");
    if (!pfnCreate) {
        std::cerr << "[FAIL] Missing D3D12CreateDevice export\n";
        return 1;
    }

    // 1. Create base wrapped device
    ID3D12Device* pDev = nullptr;
    HRESULT hr = pfnCreate(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDev));
    std::cout << "[STEP 1] hr=0x" << std::hex << hr << " pDev=" << pDev << std::endl;
    if (FAILED(hr) || !pDev) {
        std::cerr << "[FAIL] D3D12CreateDevice failed with hr=0x" << std::hex << hr << "\n";
        return 1;
    }
    std::cout << "[PASS] D3D12CreateDevice returned wrapped device\n" << std::flush;

    // 2. Test COM Identity
    IUnknown* pUnk1 = nullptr;
    IUnknown* pUnk2 = nullptr;
    hr = pDev->QueryInterface(IID_IUnknown, (void**)&pUnk1);
    std::cout << "[STEP 2] QI(IUnknown) hr=0x" << std::hex << hr << " pUnk1=" << pUnk1 << std::endl;
    TEST_ASSERT(SUCCEEDED(hr) && pUnk1 != nullptr, "QI(IUnknown) must succeed");
    
    // 3. Test QueryInterface for ID3D12Device1 .. ID3D12Device5
    ID3D12Device1* pDev1 = nullptr;
    hr = pDev->QueryInterface(__uuidof(ID3D12Device1), (void**)&pDev1);
    std::cout << "[STEP 3.1] QI(Device1) hr=0x" << std::hex << hr << " pDev1=" << pDev1 << std::endl;
    TEST_ASSERT(SUCCEEDED(hr) && pDev1 != nullptr, "QI(ID3D12Device1) must succeed");
    pDev1->QueryInterface(IID_IUnknown, (void**)&pUnk2);
    TEST_ASSERT(pUnk1 == pUnk2, "COM Identity: IUnknown pointers from Device and Device1 must match");
    pUnk2->Release();

    ID3D12Device2* pDev2 = nullptr;
    hr = pDev->QueryInterface(__uuidof(ID3D12Device2), (void**)&pDev2);
    std::cout << "[STEP 3.2] QI(Device2) hr=0x" << std::hex << hr << " pDev2=" << pDev2 << std::endl;
    TEST_ASSERT(SUCCEEDED(hr) && pDev2 != nullptr, "QI(ID3D12Device2) must succeed");
    pDev2->QueryInterface(IID_IUnknown, (void**)&pUnk2);
    TEST_ASSERT(pUnk1 == pUnk2, "COM Identity: IUnknown pointers from Device and Device2 must match");
    pUnk2->Release();

    ID3D12Device3* pDev3 = nullptr;
    hr = pDev->QueryInterface(__uuidof(ID3D12Device3), (void**)&pDev3);
    std::cout << "[STEP 3.3] QI(Device3) hr=0x" << std::hex << hr << " pDev3=" << pDev3 << std::endl;
    TEST_ASSERT(SUCCEEDED(hr) && pDev3 != nullptr, "QI(ID3D12Device3) must succeed");
    pDev3->QueryInterface(IID_IUnknown, (void**)&pUnk2);
    TEST_ASSERT(pUnk1 == pUnk2, "COM Identity: IUnknown pointers from Device and Device3 must match");
    pUnk2->Release();

    ID3D12Device4* pDev4 = nullptr;
    hr = pDev->QueryInterface(__uuidof(ID3D12Device4), (void**)&pDev4);
    std::cout << "[STEP 3.4] QI(Device4) hr=0x" << std::hex << hr << " pDev4=" << pDev4 << std::endl;
    TEST_ASSERT(SUCCEEDED(hr) && pDev4 != nullptr, "QI(ID3D12Device4) must succeed");
    pDev4->QueryInterface(IID_IUnknown, (void**)&pUnk2);
    TEST_ASSERT(pUnk1 == pUnk2, "COM Identity: IUnknown pointers from Device and Device4 must match");
    pUnk2->Release();

    ID3D12Device5* pDev5 = nullptr;
    hr = pDev->QueryInterface(__uuidof(ID3D12Device5), (void**)&pDev5);
    std::cout << "[STEP 3.5] QI(Device5) hr=0x" << std::hex << hr << " pDev5=" << pDev5 << std::endl;
    TEST_ASSERT(SUCCEEDED(hr) && pDev5 != nullptr, "QI(ID3D12Device5) must succeed");
    pDev5->QueryInterface(IID_IUnknown, (void**)&pUnk2);
    TEST_ASSERT(pUnk1 == pUnk2, "COM Identity: IUnknown pointers from Device and Device5 must match");
    pUnk2->Release();

    std::cout << "[PASS] All device interfaces ID3D12Device1 through ID3D12Device5 supported with COM Identity verified\n";

    // 4. Test E_POINTER rejection on nullptr output
    hr = pDev->QueryInterface(__uuidof(ID3D12Device5), nullptr);
    TEST_ASSERT(hr == E_POINTER, "QI with null ppvObject must return E_POINTER");
    std::cout << "[PASS] Null pointer handling on QueryInterface verified (E_POINTER)\n" << std::flush;

    // 5. Test rejection of extended interfaces ID3D12Device6 .. ID3D12Device10 (preventing bypass)
    void* pOut = (void*)0x12345;
    TEST_ASSERT(pDev->QueryInterface(__uuidof(ID3D12Device6), &pOut) == E_NOINTERFACE && pOut == nullptr, "QI(ID3D12Device6) must return E_NOINTERFACE and null ppv");
    pOut = (void*)0x12345;
    TEST_ASSERT(pDev->QueryInterface(__uuidof(ID3D12Device7), &pOut) == E_NOINTERFACE && pOut == nullptr, "QI(ID3D12Device7) must return E_NOINTERFACE and null ppv");
    pOut = (void*)0x12345;
    TEST_ASSERT(pDev->QueryInterface(__uuidof(ID3D12Device8), &pOut) == E_NOINTERFACE && pOut == nullptr, "QI(ID3D12Device8) must return E_NOINTERFACE and null ppv");
    pOut = (void*)0x12345;
    TEST_ASSERT(pDev->QueryInterface(__uuidof(ID3D12Device9), &pOut) == E_NOINTERFACE && pOut == nullptr, "QI(ID3D12Device9) must return E_NOINTERFACE and null ppv");
    pOut = (void*)0x12345;
    TEST_ASSERT(pDev->QueryInterface(__uuidof(ID3D12Device10), &pOut) == E_NOINTERFACE && pOut == nullptr, "QI(ID3D12Device10) must return E_NOINTERFACE and null ppv");
    std::cout << "[PASS] Extended device interfaces (ID3D12Device6..10) strictly rejected with E_NOINTERFACE\n" << std::flush;

    // 6. Test ID3D12Device5 methods
    // Test raytracing honesty: CreateStateObject must return DXGI_ERROR_UNSUPPORTED or native failure
    D3D12_STATE_OBJECT_DESC soDesc = {};
    soDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    ID3D12StateObject* pStateObj = nullptr;
    HRESULT hrSO = pDev5->CreateStateObject(&soDesc, IID_PPV_ARGS(&pStateObj));
    TEST_ASSERT(FAILED(hrSO), "Kepler does not support hardware raytracing; CreateStateObject must fail");
    std::cout << "[PASS] DXR CreateStateObject rejected honestly: hr=0x" << std::hex << hrSO << "\n";

    // Test EnumerateMetaCommands
    UINT metaCount = 999;
    hr = pDev5->EnumerateMetaCommands(&metaCount, nullptr);
    TEST_ASSERT(SUCCEEDED(hr), "EnumerateMetaCommands with null buffer must succeed");
    std::cout << "[PASS] EnumerateMetaCommands returned count=" << std::dec << metaCount << "\n";

    // Clean up
    pDev5->Release();
    pDev4->Release();
    pDev3->Release();
    pDev2->Release();
    pDev1->Release();
    pUnk1->Release();
    pDev->Release();

    std::cout << "========================================================\n";
    std::cout << "  ALL DEVICE INTERFACE TESTS PASSED (6/6)!              \n";
    std::cout << "========================================================\n";
    return 0;
}
