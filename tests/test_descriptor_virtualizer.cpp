#include "translator/descriptor_virtualizer.h"
#include <iostream>
#include <cassert>

using namespace kepler;

int main() {
    std::cout << "========================================================\n";
    std::cout << "  KeplerBridge Milestone 9: Descriptor Virtualizer Test \n";
    std::cout << "========================================================\n" << std::endl;

    auto& virt = DescriptorVirtualizer::Instance();

    // 1. Valid Bounded Ranges Mapping
    std::cout << "[TEST 1] Testing bounded descriptor ranges remapping..." << std::endl;
    std::vector<VirtualizedDescriptorRange> ranges = {
        { D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 2, 0, 0, 0, STAGE_ALL },
        { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, 0, STAGE_ALL },
        { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 0, STAGE_COMPUTE },
        { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0, 0, 0, STAGE_PIXEL }
    };

    std::vector<D3D11SlotMapping> mappings;
    std::string diag;
    bool ok = virt.VirtualizeDescriptorRanges(ranges, mappings, diag);
    assert(ok);
    assert(mappings.size() == (2 + 4 + 2 + 2));
    std::cout << "  [PASS] Remapped " << mappings.size() << " descriptors successfully to D3D11 slots." << std::endl;

    // 2. Unbounded Descriptor Table Rejection
    std::cout << "\n[TEST 2] Testing unbounded descriptor heap rejection (ResourceDescriptorHeap[i])..." << std::endl;
    std::vector<VirtualizedDescriptorRange> unboundedRanges = {
        { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 0, 0, STAGE_ALL }
    };
    bool unboundedOk = virt.VirtualizeDescriptorRanges(unboundedRanges, mappings, diag);
    assert(!unboundedOk);
    assert(diag.find("unbounded descriptor range") != std::string::npos);
    std::cout << "  [PASS] Unbounded heap indexing cleanly rejected: " << diag << std::endl;

    // 3. Kepler FL 11_0 UAV Slot Limit (max 8)
    std::cout << "\n[TEST 3] Testing Kepler FL 11_0 UAV slot limit (8 max)..." << std::endl;
    std::vector<VirtualizedDescriptorRange> tooManyUAVs = {
        { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 12, 0, 0, 0, STAGE_COMPUTE }
    };
    bool uavLimitOk = virt.VirtualizeDescriptorRanges(tooManyUAVs, mappings, diag);
    assert(!uavLimitOk);
    assert(diag.find("maximum UAV slot limit (8)") != std::string::npos);
    std::cout << "  [PASS] Excess UAV range (>8) rejected: " << diag << std::endl;

    // 4. Stage Visibility Gating
    std::cout << "\n[TEST 4] Testing stage visibility gating (UAV in Vertex stage)..." << std::endl;
    std::vector<VirtualizedDescriptorRange> vertexUAV = {
        { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0, STAGE_VERTEX }
    };
    bool vertexUAVOk = virt.VirtualizeDescriptorRanges(vertexUAV, mappings, diag);
    assert(!vertexUAVOk);
    assert(diag.find("unsupported graphics stage") != std::string::npos);
    std::cout << "  [PASS] Vertex stage UAV rejected: " << diag << std::endl;

    // 5. Access Bounds Validation
    std::cout << "\n[TEST 5] Testing descriptor access bounds validation..." << std::endl;
    D3D11SlotMapping validMapping{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 0, 10, STAGE_PIXEL };
    bool accessValid = virt.ValidateDescriptorAccess(validMapping, 0, STAGE_PIXEL, diag);
    assert(accessValid);

    bool stageInvalid = virt.ValidateDescriptorAccess(validMapping, 0, STAGE_VERTEX, diag);
    assert(!stageInvalid);
    std::cout << "  [PASS] Stage mismatch rejected: " << diag << std::endl;

    std::cout << "\n========================================================\n";
    std::cout << "   ALL DESCRIPTOR VIRTUALIZER TESTS PASSED!             \n";
    std::cout << "========================================================\n" << std::endl;
    return 0;
}
