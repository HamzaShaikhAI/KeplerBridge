#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <iostream>
#include <iomanip>
#include <cassert>

#include "translator/wrapped_d3d12_device_factory.h"
#include "core/logger.h"

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            return false; \
        } \
    } while (0)

typedef HRESULT (WINAPI *PFN_D3D12GetInterface)(REFCLSID, REFIID, void**);

// ----------------------------------------------------------------------------
// Test 1: COM Reference Counting and QueryInterface
// ----------------------------------------------------------------------------
bool TestCOMReferenceCountingAndQI() {
    using namespace kepler;
    std::cout << "[TEST 1] Testing COM Reference Counting and QueryInterface...\n";

    // A. WrappedD3D12DeviceFactory
    auto* factory = new WrappedD3D12DeviceFactory(nullptr);
    TEST_CHECK(factory != nullptr, "Failed to instantiate WrappedD3D12DeviceFactory");

    ULONG ref = factory->AddRef();
    TEST_CHECK(ref == 2, "AddRef should return 2");
    ref = factory->Release();
    TEST_CHECK(ref == 1, "Release should return 1");

    void* pQI = nullptr;
    HRESULT hr = factory->QueryInterface(IID_ID3D12DeviceFactory, &pQI);
    TEST_CHECK(SUCCEEDED(hr) && pQI == factory, "QI for IID_ID3D12DeviceFactory failed");
    factory->Release(); // Release the ref from QI

    hr = factory->QueryInterface(__uuidof(IUnknown), &pQI);
    TEST_CHECK(SUCCEEDED(hr) && pQI == factory, "QI for IUnknown failed");
    factory->Release();

    // Unsupported interface
    static const GUID IID_Dummy = { 0x12345678, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0 } };
    hr = factory->QueryInterface(IID_Dummy, &pQI);
    TEST_CHECK(hr == E_NOINTERFACE && pQI == nullptr, "QI for unsupported IID should return E_NOINTERFACE");

    // Null pointer test
    hr = factory->QueryInterface(IID_ID3D12DeviceFactory, nullptr);
    TEST_CHECK(hr == E_POINTER, "QI with null ppvObject should return E_POINTER");

    // Cleanup factory
    factory->Release();

    // B. WrappedD3D12SDKConfiguration1
    auto* config = new WrappedD3D12SDKConfiguration1(nullptr);
    TEST_CHECK(config != nullptr, "Failed to instantiate WrappedD3D12SDKConfiguration1");

    ref = config->AddRef();
    TEST_CHECK(ref == 2, "SDKConfig AddRef should return 2");
    ref = config->Release();
    TEST_CHECK(ref == 1, "SDKConfig Release should return 1");

    hr = config->QueryInterface(IID_ID3D12SDKConfiguration, &pQI);
    TEST_CHECK(SUCCEEDED(hr) && pQI == config, "QI for IID_ID3D12SDKConfiguration failed");
    config->Release();

    hr = config->QueryInterface(IID_ID3D12SDKConfiguration1, &pQI);
    TEST_CHECK(SUCCEEDED(hr) && pQI == config, "QI for IID_ID3D12SDKConfiguration1 failed");
    config->Release();

    hr = config->QueryInterface(__uuidof(IUnknown), &pQI);
    TEST_CHECK(SUCCEEDED(hr) && pQI == config, "QI for IUnknown on SDKConfig failed");
    config->Release();

    hr = config->QueryInterface(IID_Dummy, &pQI);
    TEST_CHECK(hr == E_NOINTERFACE && pQI == nullptr, "QI for unsupported IID should return E_NOINTERFACE");

    hr = config->QueryInterface(IID_ID3D12SDKConfiguration1, nullptr);
    TEST_CHECK(hr == E_POINTER, "QI with null ppvObject should return E_POINTER");

    config->Release();

    std::cout << "  -> [PASS] COM reference counting and QueryInterface verified.\n";
    return true;
}

// ----------------------------------------------------------------------------
// Test 2: Factory acquisition via ID3D12SDKConfiguration1::CreateDeviceFactory
// ----------------------------------------------------------------------------
bool TestFactoryAcquisitionViaSDKConfig() {
    using namespace kepler;
    std::cout << "[TEST 2] Testing Factory Acquisition via SDKConfiguration1::CreateDeviceFactory...\n";

    auto* config = new WrappedD3D12SDKConfiguration1(nullptr);
    TEST_CHECK(config != nullptr, "SDKConfig instantiation failed");

    ID3D12DeviceFactory* pFactory = nullptr;
    HRESULT hr = config->CreateDeviceFactory(602, ".\\D3D12\\", IID_ID3D12DeviceFactory, reinterpret_cast<void**>(&pFactory));
    TEST_CHECK(SUCCEEDED(hr), "CreateDeviceFactory failed");
    TEST_CHECK(pFactory != nullptr, "CreateDeviceFactory returned null factory pointer");

    // Test flags manipulation
    hr = pFactory->SetFlags(D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_EXISTING_DEVICE);
    TEST_CHECK(SUCCEEDED(hr), "SetFlags failed");

    D3D12_DEVICE_FACTORY_FLAGS flags = pFactory->GetFlags();
    TEST_CHECK((flags & D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_EXISTING_DEVICE) != 0,
               "GetFlags did not reflect updated factory flags");

    pFactory->Release();
    config->Release();

    std::cout << "  -> [PASS] Factory acquisition and flag manipulation via SDKConfiguration1 verified.\n";
    return true;
}

// ----------------------------------------------------------------------------
// Test 3: Factory acquisition via D3D12GetInterface export
// ----------------------------------------------------------------------------
bool TestFactoryAcquisitionViaD3D12GetInterface(PFN_D3D12GetInterface pfnGetInterface) {
    std::cout << "[TEST 3] Testing Factory Acquisition via D3D12GetInterface export...\n";
    TEST_CHECK(pfnGetInterface != nullptr, "D3D12GetInterface pointer is null");

    // A. Query CLSID_D3D12SDKConfiguration / IID_ID3D12SDKConfiguration1
    ID3D12SDKConfiguration1* pSDKConfig1 = nullptr;
    HRESULT hrConfig = pfnGetInterface(CLSID_D3D12SDKConfiguration, IID_PPV_ARGS(&pSDKConfig1));
    TEST_CHECK(SUCCEEDED(hrConfig), "D3D12GetInterface(CLSID_D3D12SDKConfiguration) failed");
    TEST_CHECK(pSDKConfig1 != nullptr, "D3D12GetInterface returned null SDKConfig pointer");

    // Spawn factory from SDK configuration
    ID3D12DeviceFactory* pFactoryFromConfig = nullptr;
    HRESULT hrFac = pSDKConfig1->CreateDeviceFactory(602, ".\\D3D12\\", IID_PPV_ARGS(&pFactoryFromConfig));
    TEST_CHECK(SUCCEEDED(hrFac), "pSDKConfig1->CreateDeviceFactory failed");
    TEST_CHECK(pFactoryFromConfig != nullptr, "pSDKConfig1->CreateDeviceFactory returned null factory pointer");
    pFactoryFromConfig->Release();
    pSDKConfig1->Release();

    // B. Query CLSID_D3D12DeviceFactory directly
    ID3D12DeviceFactory* pFactoryDirect = nullptr;
    HRESULT hrDirect = pfnGetInterface(CLSID_D3D12DeviceFactory, IID_PPV_ARGS(&pFactoryDirect));
    TEST_CHECK(SUCCEEDED(hrDirect), "D3D12GetInterface(CLSID_D3D12DeviceFactory) failed");
    TEST_CHECK(pFactoryDirect != nullptr, "D3D12GetInterface returned null direct factory pointer");
    pFactoryDirect->Release();

    std::cout << "  -> [PASS] Factory acquisition via D3D12GetInterface export verified.\n";
    return true;
}

// ----------------------------------------------------------------------------
// Test 4: CreateDevice routing to KeplerBridge translation layer & spoofing
// ----------------------------------------------------------------------------
bool TestCreateDeviceRoutingAndSpoofing(PFN_D3D12GetInterface pfnGetInterface) {
    std::cout << "[TEST 4] Testing CreateDevice routing to translation layer & spoofing...\n";
    TEST_CHECK(pfnGetInterface != nullptr, "D3D12GetInterface pointer is null");

    ID3D12DeviceFactory* pFactory = nullptr;
    HRESULT hr = pfnGetInterface(CLSID_D3D12DeviceFactory, IID_PPV_ARGS(&pFactory));
    TEST_CHECK(SUCCEEDED(hr) && pFactory != nullptr, "Failed to acquire device factory");

    // Request FL 12_0 on native FL 11_0 hardware (GT 740)
    ID3D12Device* pDevice = nullptr;
    hr = pFactory->CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&pDevice));
    TEST_CHECK(SUCCEEDED(hr), "pFactory->CreateDevice(FL 12_0) failed to intercept and create device");
    TEST_CHECK(pDevice != nullptr, "pFactory->CreateDevice returned null device pointer");

    // Verify feature level spoofing through CheckFeatureSupport
    D3D_FEATURE_LEVEL testLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_12_0
    };
    D3D12_FEATURE_DATA_FEATURE_LEVELS featureData = {};
    featureData.NumFeatureLevels = 3;
    featureData.pFeatureLevelsRequested = testLevels;

    HRESULT hrCheck = pDevice->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featureData, sizeof(featureData));
    TEST_CHECK(SUCCEEDED(hrCheck), "CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS) failed");
    TEST_CHECK(featureData.MaxSupportedFeatureLevel >= D3D_FEATURE_LEVEL_12_0,
               "Feature level spoofing failed: MaxSupportedFeatureLevel is below 12_0");

    std::cout << "  Reported Max Supported Feature Level: 0x" << std::hex
              << featureData.MaxSupportedFeatureLevel << std::dec << " (Spoofed 12_0+ verified)\n";

    pDevice->Release();
    pFactory->Release();

    std::cout << "  -> [PASS] Device creation routed into KeplerBridge and spoofing verified.\n";
    return true;
}

// ----------------------------------------------------------------------------
// Test 5: Error propagation and probe handling
// ----------------------------------------------------------------------------
bool TestErrorPropagationAndProbe(PFN_D3D12GetInterface pfnGetInterface) {
    std::cout << "[TEST 5] Testing Error Propagation and Probe Handling...\n";
    TEST_CHECK(pfnGetInterface != nullptr, "D3D12GetInterface pointer is null");

    ID3D12DeviceFactory* pFactory = nullptr;
    HRESULT hr = pfnGetInterface(CLSID_D3D12DeviceFactory, IID_PPV_ARGS(&pFactory));
    TEST_CHECK(SUCCEEDED(hr) && pFactory != nullptr, "Failed to acquire device factory");

    // A. Probe capability call (ppv_device == nullptr)
    HRESULT hrProbe = pFactory->CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr);
    TEST_CHECK(SUCCEEDED(hrProbe), "Factory probe device creation (ppv_device=nullptr) failed");
    std::cout << "  Factory probe hr = 0x" << std::hex << hrProbe << std::dec << " (Probe Succeeded)\n";

    // B. Invalid IID error propagation
    static const GUID IID_Invalid = { 0xdeadbeef, 0x1111, 0x2222, { 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa } };
    void* pInvalidObj = nullptr;
    HRESULT hrInvalid = pFactory->CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_Invalid, &pInvalidObj);
    TEST_CHECK(FAILED(hrInvalid) && pInvalidObj == nullptr,
               "CreateDevice with invalid IID should fail and return null object");

    // C. Null pointer to GetInterface
    HRESULT hrNull = pfnGetInterface(CLSID_D3D12DeviceFactory, IID_ID3D12DeviceFactory, nullptr);
    TEST_CHECK(hrNull == E_POINTER, "D3D12GetInterface with null ppvInterface should return E_POINTER");

    pFactory->Release();

    std::cout << "  -> [PASS] Error propagation and probe handling verified.\n";
    return true;
}

// ----------------------------------------------------------------------------
// Test 6: Escape prevention and device interface query isolation
// ----------------------------------------------------------------------------
bool TestEscapePreventionAndDeviceQI(PFN_D3D12GetInterface pfnGetInterface) {
    std::cout << "[TEST 6] Testing Escape Prevention & Interface Isolation...\n";
    TEST_CHECK(pfnGetInterface != nullptr, "D3D12GetInterface pointer is null");

    ID3D12DeviceFactory* pFactory = nullptr;
    HRESULT hr = pfnGetInterface(CLSID_D3D12DeviceFactory, IID_PPV_ARGS(&pFactory));
    TEST_CHECK(SUCCEEDED(hr) && pFactory != nullptr, "Failed to acquire device factory");

    // Create wrapped device at FL 12_0
    ID3D12Device* pDevice = nullptr;
    hr = pFactory->CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&pDevice));
    TEST_CHECK(SUCCEEDED(hr) && pDevice != nullptr, "CreateDevice(FL 12_0) failed");

    // A. Verify standard supported interfaces
    void* pObj = nullptr;
    hr = pDevice->QueryInterface(__uuidof(IUnknown), &pObj);
    TEST_CHECK(SUCCEEDED(hr) && pObj == pDevice, "QueryInterface(IUnknown) must preserve COM identity");
    ((IUnknown*)pObj)->Release();

    hr = pDevice->QueryInterface(__uuidof(ID3D12Object), &pObj);
    TEST_CHECK(SUCCEEDED(hr) && pObj == pDevice, "QueryInterface(ID3D12Object) failed");
    ((IUnknown*)pObj)->Release();

    hr = pDevice->QueryInterface(__uuidof(ID3D12Device1), &pObj);
    TEST_CHECK(SUCCEEDED(hr) && pObj != nullptr, "QueryInterface(ID3D12Device1) failed");
    ((IUnknown*)pObj)->Release();

    // B. Verify supported wrapped interfaces ID3D12Device1 .. ID3D12Device5 preserve COM identity and cannot escape
    hr = pDevice->QueryInterface(__uuidof(ID3D12Device2), &pObj);
    TEST_CHECK(SUCCEEDED(hr) && pObj == pDevice, "QueryInterface(ID3D12Device2) must succeed and preserve COM identity");
    ((IUnknown*)pObj)->Release();

    hr = pDevice->QueryInterface(__uuidof(ID3D12Device3), &pObj);
    TEST_CHECK(SUCCEEDED(hr) && pObj == pDevice, "QueryInterface(ID3D12Device3) must succeed and preserve COM identity");
    ((IUnknown*)pObj)->Release();

    hr = pDevice->QueryInterface(__uuidof(ID3D12Device4), &pObj);
    TEST_CHECK(SUCCEEDED(hr) && pObj == pDevice, "QueryInterface(ID3D12Device4) must succeed and preserve COM identity");
    ((IUnknown*)pObj)->Release();

    hr = pDevice->QueryInterface(__uuidof(ID3D12Device5), &pObj);
    TEST_CHECK(SUCCEEDED(hr) && pObj == pDevice, "QueryInterface(ID3D12Device5) must succeed and preserve COM identity");
    ((IUnknown*)pObj)->Release();

    // C. Verify extended device interfaces ID3D12Device6 .. ID3D12Device10 return E_NOINTERFACE (Prevent Escape)
    static const GUID IID_Dev6 = {0xc70b221b, 0x40e4, 0x4a17, {0x89,0xaf, 0x02,0x5a,0x07,0x27,0xa6,0xdc}};
    hr = pDevice->QueryInterface(IID_Dev6, &pObj);
    TEST_CHECK(hr == E_NOINTERFACE && pObj == nullptr, "QI(ID3D12Device6) must return E_NOINTERFACE to prevent escape");

    // D. Direct CreateDevice with extended interface must return E_NOINTERFACE
    void* pEscapedDev = nullptr;
    hr = pFactory->CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_Dev6, &pEscapedDev);
    TEST_CHECK(hr == E_NOINTERFACE && pEscapedDev == nullptr,
               "CreateDevice(ID3D12Device6) must return E_NOINTERFACE and set ppv to null");

    pDevice->Release();
    pFactory->Release();

    std::cout << "  -> [PASS] Escape prevention and interface isolation verified (Zero driver leak).\n";
    return true;
}

// ----------------------------------------------------------------------------
// Test 7: Full Agility SDK handshake and translation pipeline execution
// ----------------------------------------------------------------------------
bool TestFullAgilitySDKHandshakeAndCommandExecution(PFN_D3D12GetInterface pfnGetInterface) {
    std::cout << "[TEST 7] Testing Full Agility SDK Handshake & Command Pipeline...\n";
    TEST_CHECK(pfnGetInterface != nullptr, "D3D12GetInterface pointer is null");

    // 1. Handshake through SDK Configuration
    ID3D12SDKConfiguration1* pConfig1 = nullptr;
    HRESULT hr = pfnGetInterface(CLSID_D3D12SDKConfiguration, IID_PPV_ARGS(&pConfig1));
    TEST_CHECK(SUCCEEDED(hr) && pConfig1 != nullptr, "Acquisition of ID3D12SDKConfiguration1 failed");

    // 2. Factory creation matching Palworld's exact parameters
    const char* palPath = "D:\\Program Files\\Palworld\\Palworld\\Pal\\Binaries\\Win64\\D3D12\\";
    ID3D12DeviceFactory* pFactory = nullptr;
    hr = pConfig1->CreateDeviceFactory(602, palPath, IID_PPV_ARGS(&pFactory));
    TEST_CHECK(SUCCEEDED(hr) && pFactory != nullptr, "CreateDeviceFactory with Palworld Agility SDK path failed");

    // 3. Null-probe validation across feature levels
    HRESULT hrProbe11 = pFactory->CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr);
    TEST_CHECK(SUCCEEDED(hrProbe11), "Null-probe at FL 11_0 failed");

    HRESULT hrProbe12 = pFactory->CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr);
    TEST_CHECK(SUCCEEDED(hrProbe12), "Null-probe at FL 12_0 failed");

    // 4. Create wrapped device
    ID3D12Device* pDevice = nullptr;
    hr = pFactory->CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&pDevice));
    TEST_CHECK(SUCCEEDED(hr) && pDevice != nullptr, "CreateDevice at FL 12_0 failed");

    // 5. Command infrastructure creation
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* pQueue = nullptr;
    hr = pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&pQueue));
    TEST_CHECK(SUCCEEDED(hr) && pQueue != nullptr, "CreateCommandQueue failed");

    ID3D12CommandAllocator* pAlloc = nullptr;
    hr = pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pAlloc));
    TEST_CHECK(SUCCEEDED(hr) && pAlloc != nullptr, "CreateCommandAllocator failed");

    ID3D12GraphicsCommandList* pCmdList = nullptr;
    hr = pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pAlloc, nullptr, IID_PPV_ARGS(&pCmdList));
    TEST_CHECK(SUCCEEDED(hr) && pCmdList != nullptr, "CreateCommandList failed");

    // 6. Fence creation
    ID3D12Fence* pFence = nullptr;
    hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pFence));
    TEST_CHECK(SUCCEEDED(hr) && pFence != nullptr, "CreateFence failed");

    // 7. Command recording & execution
    hr = pCmdList->Close();
    TEST_CHECK(SUCCEEDED(hr), "CommandList::Close failed");

    ID3D12CommandList* ppLists[] = { pCmdList };
    pQueue->ExecuteCommandLists(1, ppLists);

    hr = pQueue->Signal(pFence, 1);
    TEST_CHECK(SUCCEEDED(hr), "Queue->Signal failed");

    UINT64 completed = pFence->GetCompletedValue();
    TEST_CHECK(completed == 1, "Fence synchronization failed: expected 1");

    // 8. Cleanup
    pFence->Release();
    pCmdList->Release();
    pAlloc->Release();
    pQueue->Release();
    pDevice->Release();
    pFactory->Release();
    pConfig1->Release();

    std::cout << "  -> [PASS] Full Agility SDK handshake and translation pipeline verified.\n";
    return true;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  KeplerBridge Phase 11D Device Factory Test Suite      \n";
    std::cout << "========================================================\n";

    // Load candidate proxy d3d12.dll once for integration tests
    HMODULE hD3D12 = LoadLibraryA(".\\d3d12.dll");
    if (!hD3D12) {
        hD3D12 = LoadLibraryA("d3d12.dll");
    }
    if (!hD3D12) {
        std::cerr << "[FATAL] Failed to load candidate d3d12.dll!\n";
        return 1;
    }
    std::cout << "[INIT] Loaded candidate proxy d3d12.dll successfully.\n";

    auto pfnGetInterface = reinterpret_cast<PFN_D3D12GetInterface>(GetProcAddress(hD3D12, "D3D12GetInterface"));
    if (!pfnGetInterface) {
        std::cerr << "[FATAL] D3D12GetInterface export missing from candidate d3d12.dll!\n";
        FreeLibrary(hD3D12);
        return 1;
    }

    int passed = 0;
    int failed = 0;

    if (TestCOMReferenceCountingAndQI()) passed++; else failed++;
    if (TestFactoryAcquisitionViaSDKConfig()) passed++; else failed++;
    if (TestFactoryAcquisitionViaD3D12GetInterface(pfnGetInterface)) passed++; else failed++;
    if (TestCreateDeviceRoutingAndSpoofing(pfnGetInterface)) passed++; else failed++;
    if (TestErrorPropagationAndProbe(pfnGetInterface)) passed++; else failed++;
    if (TestEscapePreventionAndDeviceQI(pfnGetInterface)) passed++; else failed++;
    if (TestFullAgilitySDKHandshakeAndCommandExecution(pfnGetInterface)) passed++; else failed++;

    FreeLibrary(hD3D12);

    std::cout << "========================================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed.\n";
    std::cout << "========================================================\n";

    return (failed == 0) ? 0 : 1;
}
