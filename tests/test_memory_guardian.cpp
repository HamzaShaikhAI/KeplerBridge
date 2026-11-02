#include "translator/memory_guardian.h"
#include <iostream>
#include <cassert>

using namespace kepler;

int main() {
    std::cout << "========================================================\n";
    std::cout << "  KeplerBridge Milestone 5: Memory Guardian Test Suite  \n";
    std::cout << "========================================================\n" << std::endl;

    auto& mg = MemoryGuardian::Instance();
    mg.Reset();
    mg.SetSoftBudgetMB(100); // 100 MB test budget

    // 1. Allocation registration and accounting
    std::cout << "[TEST 1] Testing resource allocation tracking & peaks..." << std::endl;
    mg.RegisterAllocation(101, "Texture_Albedo", 50 * 1024 * 1024, true, false, 1);
    assert(mg.GetCurrentTrackedBytes() == 50 * 1024 * 1024);
    assert(mg.GetPeakTrackedBytes() == 50 * 1024 * 1024);
    assert(mg.GetActiveResourceCount() == 1);
    assert(mg.GetPressureLevel() == MemoryPressureLevel::Normal);

    mg.RegisterAllocation(102, "VertexBuffer", 35 * 1024 * 1024, false, true, 1);
    assert(mg.GetCurrentTrackedBytes() == 85 * 1024 * 1024);
    assert(mg.GetActiveResourceCount() == 2);
    // 85 MB / 100 MB = 85% -> Warning
    assert(mg.GetPressureLevel() == MemoryPressureLevel::Warning);
    std::cout << "  [PASS] Allocation tracking and Warning pressure level (85%) verified." << std::endl;

    // 2. Critical pressure and unregistration
    std::cout << "\n[TEST 2] Testing Critical pressure and release..." << std::endl;
    mg.RegisterAllocation(103, "ShadowMap", 12 * 1024 * 1024, true, false, 1);
    // 97 MB / 100 MB = 97% -> Critical
    assert(mg.GetPressureLevel() == MemoryPressureLevel::Critical);

    mg.UnregisterAllocation(102); // Free 35 MB -> 62 MB remaining
    assert(mg.GetCurrentTrackedBytes() == 62 * 1024 * 1024);
    assert(mg.GetActiveResourceCount() == 2);
    assert(mg.GetPressureLevel() == MemoryPressureLevel::Normal);
    assert(mg.GetPeakTrackedBytes() == 97 * 1024 * 1024); // Peak retained
    std::cout << "  [PASS] Critical pressure triggered and cleared upon unregistration." << std::endl;

    // 3. Leak detection
    std::cout << "\n[TEST 3] Testing stale resource / leak detection..." << std::endl;
    // Resource 101 was created at frame 1 and never touched
    // Resource 103 was touched at frame 500
    mg.TouchAllocation(103, 500);
    auto leaks = mg.DetectPotentialLeaks(600, 300); // threshold 300 frames
    assert(leaks.size() == 1);
    assert(leaks[0].id == 101);
    assert(leaks[0].name == "Texture_Albedo");
    std::cout << "  [PASS] Stale resource 101 (idle for 599 frames) identified as leak candidate." << std::endl;

    // 4. Failure and Diagnostic Report
    std::cout << "\n[TEST 4] Testing diagnostic report and failure recording..." << std::endl;
    mg.RecordAllocationFailure(200 * 1024 * 1024, "Out of VRAM test");
    assert(mg.GetAllocationFailureCount() == 1);
    assert(mg.GetPressureLevel() == MemoryPressureLevel::Critical);

    mg.RecordDeviceRemoved(static_cast<HRESULT>(0x887A0005)); // DXGI_ERROR_DEVICE_REMOVED
    std::string report = mg.GetDiagnosticsReport();
    assert(!report.empty());
    assert(report.find("Current Tracked") != std::string::npos);
    assert(report.find("887a0005") != std::string::npos);
    std::cout << "  [PASS] Diagnostic report generated correctly:\n" << report << std::endl;

    std::cout << "========================================================\n";
    std::cout << "   ALL MEMORY GUARDIAN TESTS PASSED!                    \n";
    std::cout << "========================================================\n" << std::endl;
    return 0;
}
