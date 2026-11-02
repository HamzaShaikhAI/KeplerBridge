#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <iostream>
#include <cassert>

#include "core/feature_level_compat.h"
#include "core/profile_manager.h"
#include "core/logger.h"

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            return false; \
        } \
    } while (0)

using namespace kepler;

static bool TestDeviceRemovedDiagnosticsSeam() {
    std::cout << "[TEST 1] Testing Device-Removal Diagnostic Seam...\n";

    // 1. Instantiate WrappedD3D12Device in isolation without real hardware removal
    auto* dev = new WrappedD3D12Device(nullptr, D3D_FEATURE_LEVEL_11_0);
    TEST_CHECK(dev != nullptr, "Failed to instantiate WrappedD3D12Device");

    // 2. Initial state: No removal, GetDeviceRemovedReason must return S_OK
    HRESULT hr = dev->GetDeviceRemovedReason();
    TEST_CHECK(hr == S_OK, "Initial GetDeviceRemovedReason must return S_OK");
    TEST_CHECK(dev->GetSimulatedDeviceRemoval() == S_OK, "Initial simulated removal state must be S_OK");

    // 3. Set telemetry context
    RuntimeTelemetry::Instance().SetLastSubmittedWork(101, "CommandList #101: DrawIndexedInstanced(1024, 1, 0, 0, 0)");
    RuntimeTelemetry::Instance().SetLastActivePipeline(0x12345678DEADBEEFULL, "PSO [Hash: 0x12345678DEADBEEF] SM5.1 TerrainPixelShader");

    // 4. Simulate DXGI_ERROR_DEVICE_HUNG (0x887A0006)
    dev->SetSimulatedDeviceRemoval(DXGI_ERROR_DEVICE_HUNG);
    TEST_CHECK(dev->GetSimulatedDeviceRemoval() == DXGI_ERROR_DEVICE_HUNG, "Simulated removal state mismatch");

    hr = dev->GetDeviceRemovedReason();
    TEST_CHECK(hr == DXGI_ERROR_DEVICE_HUNG, "GetDeviceRemovedReason must preserve DXGI_ERROR_DEVICE_HUNG");

    TelemetrySnapshot snap = RuntimeTelemetry::Instance().GetSnapshot();
    TEST_CHECK(snap.lastDeviceRemovalReason == DXGI_ERROR_DEVICE_HUNG, "Telemetry snapshot must record DXGI_ERROR_DEVICE_HUNG");
    TEST_CHECK(RuntimeTelemetry::Instance().GetDeviceRemovalCount() >= 1, "Device removal counter must increment");

    // 5. Test repeated polling rate-limiting (simulating a game loop querying device loss)
    uint32_t countBefore = RuntimeTelemetry::Instance().GetDeviceRemovalCount();
    for (int i = 0; i < 20; ++i) {
        HRESULT polledHr = dev->GetDeviceRemovedReason();
        TEST_CHECK(polledHr == DXGI_ERROR_DEVICE_HUNG, "Polled GetDeviceRemovedReason must consistently return DXGI_ERROR_DEVICE_HUNG");
    }
    uint32_t countAfter = RuntimeTelemetry::Instance().GetDeviceRemovalCount();
    TEST_CHECK(countAfter == countBefore + 20, "Telemetry must count all occurrences while rate-limiting log output");

    // 6. Simulate DXGI_ERROR_DEVICE_REMOVED (0x887A0005)
    dev->SetSimulatedDeviceRemoval(DXGI_ERROR_DEVICE_REMOVED);
    hr = dev->GetDeviceRemovedReason();
    TEST_CHECK(hr == DXGI_ERROR_DEVICE_REMOVED, "GetDeviceRemovedReason must preserve DXGI_ERROR_DEVICE_REMOVED");
    snap = RuntimeTelemetry::Instance().GetSnapshot();
    TEST_CHECK(snap.lastDeviceRemovalReason == DXGI_ERROR_DEVICE_REMOVED, "Telemetry snapshot must record DXGI_ERROR_DEVICE_REMOVED");

    // 7. Simulate DXGI_ERROR_DEVICE_RESET (0x887A0007)
    dev->SetSimulatedDeviceRemoval(DXGI_ERROR_DEVICE_RESET);
    hr = dev->GetDeviceRemovedReason();
    TEST_CHECK(hr == DXGI_ERROR_DEVICE_RESET, "GetDeviceRemovedReason must preserve DXGI_ERROR_DEVICE_RESET");

    // 8. Recovery back to S_OK
    dev->SetSimulatedDeviceRemoval(S_OK);
    hr = dev->GetDeviceRemovedReason();
    TEST_CHECK(hr == S_OK, "Resetting simulated removal must restore S_OK");

    dev->Release();
    std::cout << "[PASS] TestDeviceRemovedDiagnosticsSeam passed.\n";
    return true;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  KeplerBridge Device-Removal Handling Regression Test  \n";
    std::cout << "========================================================\n";

    if (!TestDeviceRemovedDiagnosticsSeam()) {
        std::cerr << "[FAIL] Device-Removal regression test failed!\n";
        return 1;
    }

    std::cout << "========================================================\n";
    std::cout << "  ALL DEVICE-REMOVAL REGRESSION TESTS PASSED (1/1)!     \n";
    std::cout << "========================================================\n";
    return 0;
}
