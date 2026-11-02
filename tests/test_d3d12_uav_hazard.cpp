#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <iostream>
#include <cassert>

#include "translator/d3d11_backend.h"
#include "core/logger.h"

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            return false; \
        } \
    } while (0)

using namespace kepler;

static bool TestUavSrvHazardResolution() {
    std::cout << "[TEST 1] Testing D3D11 UAV/SRV Hazard Resolution...\n";

    auto& backend = D3D11Backend::Instance();
    if (!backend.Initialize(nullptr)) {
        std::cerr << "[SKIP] D3D11Backend initialization failed (no compatible D3D11 device).\n";
        return true;
    }

    ID3D11Device* pDevice = backend.GetDevice();
    TEST_CHECK(pDevice != nullptr, "D3D11 Device is null");

    // 1. Create Buffer 0 (shared resource that can be both UAV and SRV)
    D3D11_BUFFER_DESC bufDesc0 = {};
    bufDesc0.ByteWidth = 1024;
    bufDesc0.Usage = D3D11_USAGE_DEFAULT;
    bufDesc0.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    bufDesc0.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufDesc0.StructureByteStride = 16;

    ID3D11Buffer* pBuffer0 = nullptr;
    HRESULT hr = pDevice->CreateBuffer(&bufDesc0, nullptr, &pBuffer0);
    TEST_CHECK(SUCCEEDED(hr) && pBuffer0 != nullptr, "Failed to create Buffer 0");

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = 64;

    ID3D11UnorderedAccessView* pUAV0 = nullptr;
    hr = pDevice->CreateUnorderedAccessView(pBuffer0, &uavDesc, &pUAV0);
    TEST_CHECK(SUCCEEDED(hr) && pUAV0 != nullptr, "Failed to create UAV 0");

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = 64;

    ID3D11ShaderResourceView* pSRV0 = nullptr;
    hr = pDevice->CreateShaderResourceView(pBuffer0, &srvDesc, &pSRV0);
    TEST_CHECK(SUCCEEDED(hr) && pSRV0 != nullptr, "Failed to create SRV 0");

    // 2. Create Buffer 1 (unrelated resource)
    ID3D11Buffer* pBuffer1 = nullptr;
    hr = pDevice->CreateBuffer(&bufDesc0, nullptr, &pBuffer1);
    TEST_CHECK(SUCCEEDED(hr) && pBuffer1 != nullptr, "Failed to create Buffer 1");

    ID3D11UnorderedAccessView* pUAV1 = nullptr;
    hr = pDevice->CreateUnorderedAccessView(pBuffer1, &uavDesc, &pUAV1);
    TEST_CHECK(SUCCEEDED(hr) && pUAV1 != nullptr, "Failed to create UAV 1");

    // 3. Bind pUAV0 to Compute slot 0 and pUAV1 to Compute slot 1
    backend.BindUAV(0, pUAV0, 0, true, D3D12_SHADER_VISIBILITY_ALL, nullptr, pBuffer0);
    backend.BindUAV(1, pUAV1, 0, true, D3D12_SHADER_VISIBILITY_ALL, nullptr, pBuffer1);

    TEST_CHECK(backend.GetActiveComputeUAV(0).pUAV == pUAV0, "Compute UAV 0 must be bound");
    TEST_CHECK(backend.GetActiveComputeUAV(0).pD3D11Resource == pBuffer0, "Compute UAV 0 resource match");
    TEST_CHECK(backend.GetActiveComputeUAV(1).pUAV == pUAV1, "Compute UAV 1 must be bound");
    TEST_CHECK(backend.GetActiveComputeUAV(1).pD3D11Resource == pBuffer1, "Compute UAV 1 resource match");

    // 4. Bind pSRV0 to Pixel SRV slot 0
    // Hazard rule: Because pSRV0 uses pBuffer0 which is currently bound as Compute UAV 0,
    // BindSRV must detect the hazard and unbind ONLY UAV slot 0, while leaving UAV slot 1 intact!
    backend.BindSRV(0, pSRV0, false, D3D12_SHADER_VISIBILITY_PIXEL);

    TEST_CHECK(backend.GetActivePixelSRV(0).pSRV == pSRV0, "Pixel SRV 0 must now be bound");
    TEST_CHECK(backend.GetActivePixelSRV(0).pD3D11Resource == pBuffer0, "Pixel SRV 0 resource match");
    TEST_CHECK(backend.GetActiveComputeUAV(0).pUAV == nullptr, "Hazard resolution must unbind conflicting Compute UAV slot 0");
    TEST_CHECK(backend.GetActiveComputeUAV(0).pD3D11Resource == nullptr, "Compute UAV slot 0 resource tracking cleared");
    TEST_CHECK(backend.GetActiveComputeUAV(1).pUAV == pUAV1, "Unrelated Compute UAV slot 1 must remain intact and bound");

    // 5. Now bind pUAV0 back to Compute UAV slot 0
    // Reverse hazard rule: Because pBuffer0 is currently bound as Pixel SRV 0,
    // BindUAV must detect the hazard and unbind ONLY Pixel SRV slot 0!
    backend.BindUAV(0, pUAV0, 0, true, D3D12_SHADER_VISIBILITY_ALL, nullptr, pBuffer0);

    TEST_CHECK(backend.GetActiveComputeUAV(0).pUAV == pUAV0, "Compute UAV 0 must be re-bound");
    TEST_CHECK(backend.GetActiveComputeUAV(1).pUAV == pUAV1, "Compute UAV 1 must still be bound");
    TEST_CHECK(backend.GetActivePixelSRV(0).pSRV == nullptr, "Hazard resolution must unbind conflicting Pixel SRV slot 0");
    TEST_CHECK(backend.GetActivePixelSRV(0).pD3D11Resource == nullptr, "Pixel SRV slot 0 resource tracking cleared");

    // 6. Clean up resources
    backend.BindUAV(0, nullptr, 0, true, D3D12_SHADER_VISIBILITY_ALL);
    backend.BindUAV(1, nullptr, 0, true, D3D12_SHADER_VISIBILITY_ALL);
    backend.BindSRV(0, nullptr, false, D3D12_SHADER_VISIBILITY_PIXEL);

    pSRV0->Release();
    pUAV0->Release();
    pBuffer0->Release();

    pUAV1->Release();
    pBuffer1->Release();

    std::cout << "[PASS] TestUavSrvHazardResolution passed cleanly.\n";
    return true;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "    KeplerBridge D3D11 UAV/SRV Hazard Regression Test   \n";
    std::cout << "========================================================\n";

    if (!TestUavSrvHazardResolution()) {
        std::cerr << "[FAIL] UAV/SRV hazard regression test failed!\n";
        return 1;
    }

    std::cout << "========================================================\n";
    std::cout << "    ALL UAV/SRV HAZARD REGRESSION TESTS PASSED (1/1)!   \n";
    std::cout << "========================================================\n";
    return 0;
}
