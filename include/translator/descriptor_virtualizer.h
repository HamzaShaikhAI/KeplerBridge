#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <cstdint>
#include <vector>
#include <string>
#include "core/capability_registry.h"

namespace kepler {

// Hardware D3D11 FL 11_0 limits
constexpr uint32_t D3D11_MAX_SRVS_PER_STAGE = 128;
constexpr uint32_t D3D11_MAX_UAVS_FL11_0 = 8;
constexpr uint32_t D3D11_MAX_CBVS_PER_STAGE = 14;
constexpr uint32_t D3D11_MAX_SAMPLERS_PER_STAGE = 16;

struct VirtualizedDescriptorRange {
    D3D12_DESCRIPTOR_RANGE_TYPE rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    uint32_t numDescriptors = 1;
    uint32_t baseShaderRegister = 0;
    uint32_t registerSpace = 0;
    uint32_t offsetInTableFromStart = 0;
    uint32_t stageVisibilityMask = STAGE_ALL;
};

struct D3D11SlotMapping {
    D3D12_DESCRIPTOR_RANGE_TYPE rangeType;
    uint32_t d3d12Register;
    uint32_t d3d12Space;
    uint32_t d3d11Slot;
    uint32_t stageVisibility;
};

class DescriptorVirtualizer {
public:
    static DescriptorVirtualizer& Instance();

    /**
     * @brief Remaps a list of D3D12 descriptor table ranges to fixed D3D11 resource slots.
     *        Validates bounds, checks for unsupported unbounded tables, detects aliasing,
     *        and verifies stage-specific restrictions.
     */
    bool VirtualizeDescriptorRanges(
        const std::vector<VirtualizedDescriptorRange>& ranges,
        std::vector<D3D11SlotMapping>& outMappings,
        std::string& outDiagnostic);

    /**
     * @brief Checks if a specific descriptor index access is within bounded range.
     */
    bool ValidateDescriptorAccess(
        const D3D11SlotMapping& mapping,
        uint32_t indexOffset,
        uint32_t activeStageMask,
        std::string& outDiagnostic);

private:
    DescriptorVirtualizer() = default;
    ~DescriptorVirtualizer() = default;
};

} // namespace kepler
