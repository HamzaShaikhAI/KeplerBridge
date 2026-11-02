#include "translator/descriptor_virtualizer.h"
#include "core/logger.h"
#include <sstream>

namespace kepler {

DescriptorVirtualizer& DescriptorVirtualizer::Instance() {
    static DescriptorVirtualizer s_instance;
    return s_instance;
}

bool DescriptorVirtualizer::VirtualizeDescriptorRanges(
    const std::vector<VirtualizedDescriptorRange>& ranges,
    std::vector<D3D11SlotMapping>& outMappings,
    std::string& outDiagnostic)
{
    outMappings.clear();
    outDiagnostic.clear();

    uint32_t currentSRVSlot = 0;
    uint32_t currentUAVSlot = 0;
    uint32_t currentCBVSlot = 0;
    uint32_t currentSamplerSlot = 0;

    for (size_t i = 0; i < ranges.size(); ++i) {
        const auto& r = ranges[i];

        // 1. Check for Unbounded descriptor ranges (UINT_MAX / -1)
        if (r.numDescriptors == UINT_MAX || r.numDescriptors == static_cast<uint32_t>(-1)) {
            outDiagnostic = "Unsupported unbounded descriptor range (ResourceDescriptorHeap indexing). D3D11 FL 11_0 requires statically bounded ranges.";
            LOG_WARN("[DescriptorVirtualizer] " + outDiagnostic);
            return false;
        }

        // 2. Stage-visibility restrictions (UAVs strictly forbidden in Vertex/Geometry/Hull/Domain on FL 11_0)
        if (r.rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV) {
            uint32_t forbiddenStages = (STAGE_VERTEX | STAGE_GEOMETRY | STAGE_HULL | STAGE_DOMAIN);
            if ((r.stageVisibilityMask & forbiddenStages) != 0) {
                outDiagnostic = "UAV descriptor range bound to unsupported graphics stage (FL 11_0 permits UAVs only in Compute or Pixel shader).";
                LOG_WARN("[DescriptorVirtualizer] " + outDiagnostic);
                return false;
            }
        }

        // 3. Remap each descriptor in range to fixed D3D11 slot
        for (uint32_t d = 0; d < r.numDescriptors; ++d) {
            uint32_t reg = r.baseShaderRegister + d;
            D3D11SlotMapping mapping{};
            mapping.rangeType = r.rangeType;
            mapping.d3d12Register = reg;
            mapping.d3d12Space = r.registerSpace;
            mapping.stageVisibility = r.stageVisibilityMask;

            switch (r.rangeType) {
                case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                    if (currentSRVSlot >= D3D11_MAX_SRVS_PER_STAGE) {
                        outDiagnostic = "Exceeded D3D11 maximum SRV slot limit (128).";
                        return false;
                    }
                    mapping.d3d11Slot = currentSRVSlot++;
                    break;

                case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                    if (currentUAVSlot >= D3D11_MAX_UAVS_FL11_0) {
                        outDiagnostic = "Exceeded Kepler FL 11_0 maximum UAV slot limit (8).";
                        return false;
                    }
                    mapping.d3d11Slot = currentUAVSlot++;
                    break;

                case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                    if (currentCBVSlot >= D3D11_MAX_CBVS_PER_STAGE) {
                        outDiagnostic = "Exceeded D3D11 maximum CBV slot limit (14).";
                        return false;
                    }
                    mapping.d3d11Slot = currentCBVSlot++;
                    break;

                case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
                    if (currentSamplerSlot >= D3D11_MAX_SAMPLERS_PER_STAGE) {
                        outDiagnostic = "Exceeded D3D11 maximum Sampler slot limit (16).";
                        return false;
                    }
                    mapping.d3d11Slot = currentSamplerSlot++;
                    break;

                default:
                    outDiagnostic = "Unknown descriptor range type.";
                    return false;
            }

            outMappings.push_back(mapping);
        }
    }

    return true;
}

bool DescriptorVirtualizer::ValidateDescriptorAccess(
    const D3D11SlotMapping& mapping,
    uint32_t indexOffset,
    uint32_t activeStageMask,
    std::string& outDiagnostic)
{
    // Check stage visibility
    if ((mapping.stageVisibility & activeStageMask) == 0) {
        outDiagnostic = "Descriptor access violates stage visibility permissions.";
        return false;
    }

    // Check slot bounds
    switch (mapping.rangeType) {
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
            if (mapping.d3d11Slot + indexOffset >= D3D11_MAX_SRVS_PER_STAGE) {
                outDiagnostic = "Out-of-bounds SRV slot index.";
                return false;
            }
            break;
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            if (mapping.d3d11Slot + indexOffset >= D3D11_MAX_UAVS_FL11_0) {
                outDiagnostic = "Out-of-bounds UAV slot index on Kepler FL 11_0.";
                return false;
            }
            break;
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            if (mapping.d3d11Slot + indexOffset >= D3D11_MAX_CBVS_PER_STAGE) {
                outDiagnostic = "Out-of-bounds CBV slot index.";
                return false;
            }
            break;
        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
            if (mapping.d3d11Slot + indexOffset >= D3D11_MAX_SAMPLERS_PER_STAGE) {
                outDiagnostic = "Out-of-bounds Sampler slot index.";
                return false;
            }
            break;
    }

    return true;
}

} // namespace kepler
