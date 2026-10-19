#include "downgrader/texture_clamper.h"
#include "core/config.h"
#include "core/logger.h"
#include <sstream>
#include <algorithm>

namespace kepler {

TextureClamper& TextureClamper::Instance() {
    static TextureClamper s_instance;
    return s_instance;
}

ResourceValidationResult TextureClamper::ValidateAndClamp(
    const D3D12_RESOURCE_DESC* pDesc,
    const D3D12_HEAP_PROPERTIES* pHeapProps,
    D3D12_HEAP_FLAGS heapFlags)
{
    ResourceValidationResult result;
    if (!pDesc) {
        result.isValid = false;
        result.diagnostics = "Null D3D12_RESOURCE_DESC pointer.";
        return result;
    }

    result.modifiedDesc = *pDesc;

    // Feature Level 11_0 hardware limits for NVIDIA Kepler GK208/GK107
    constexpr UINT64 MAX_TEXTURE2D_DIM = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    constexpr UINT   MAX_TEXTURE3D_DIM = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
    constexpr UINT   MAX_TEXTURE_ARRAY_DIM = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;

    std::ostringstream ss;
    ss << ResourceDescToString(pDesc);

    // Validate dimensions against hardware limits
    switch (pDesc->Dimension) {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            // Standard linear buffer
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            if (pDesc->Width > MAX_TEXTURE2D_DIM) {
                result.isValid = false;
                ss << " [REJECTED: Width " << pDesc->Width << " exceeds FL 11_0 limit "
                   << MAX_TEXTURE2D_DIM << "]";
            }
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            if (pDesc->Width > MAX_TEXTURE2D_DIM ||
                pDesc->Height > MAX_TEXTURE2D_DIM ||
                pDesc->DepthOrArraySize > MAX_TEXTURE_ARRAY_DIM)
            {
                result.isValid = false;
                ss << " [REJECTED: Dimensions " << pDesc->Width << "x" << pDesc->Height
                   << "x" << pDesc->DepthOrArraySize << " exceed FL 11_0 limits]";
            }
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            if (pDesc->Width > MAX_TEXTURE3D_DIM ||
                pDesc->Height > MAX_TEXTURE3D_DIM ||
                pDesc->DepthOrArraySize > MAX_TEXTURE3D_DIM)
            {
                result.isValid = false;
                ss << " [REJECTED: 3D Dimensions " << pDesc->Width << "x" << pDesc->Height
                   << "x" << pDesc->DepthOrArraySize << " exceed FL 11_0 3D limits]";
            }
            break;

        default:
            break;
    }

    if (!result.isValid) {
        result.diagnostics = ss.str();
        return result;
    }

    // Check if Texture Clamping is enabled in configuration
    if (Config::Instance().IsTextureClampingEnabled() &&
        pDesc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
    {
        // CRITICAL SAFETY RULE: Never clamp RenderTargets, DepthStencil surfaces, or Swapchain buffers
        bool isRenderTargetOrDepth = (pDesc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                                                      D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) != 0;

        if (isRenderTargetOrDepth) {
            ss << " [CLAMP SKIPPED: RenderTarget/DepthStencil protected from dimension clamping to avoid swapchain mismatch]";
        } else {
            uint32_t maxRes = Config::Instance().GetMaxTextureResolution();
            if (maxRes > 0 && (pDesc->Width > maxRes || pDesc->Height > maxRes)) {
                UINT64 origW = pDesc->Width;
                UINT origH = pDesc->Height;

                float aspect = static_cast<float>(origW) / static_cast<float>(origH);
                UINT64 newW = maxRes;
                UINT newH = maxRes;

                if (aspect >= 1.0f) {
                    newH = std::max(1u, static_cast<UINT>(maxRes / aspect));
                } else {
                    newW = std::max(1ull, static_cast<UINT64>(maxRes * aspect));
                }

                result.modifiedDesc.Width = newW;
                result.modifiedDesc.Height = newH;
                result.isModified = true;

                ss << " [CLAMPED: " << origW << "x" << origH << " -> " << newW << "x" << newH << "]";
            }
        }
    }

    result.diagnostics = ss.str();
    return result;
}

std::string TextureClamper::ResourceDescToString(const D3D12_RESOURCE_DESC* pDesc) {
    if (!pDesc) return "nullptr";

    std::ostringstream ss;
    switch (pDesc->Dimension) {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            ss << "Buffer (" << pDesc->Width << " bytes)";
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            ss << "Texture1D (" << pDesc->Width << "x1, Mips: " << pDesc->MipLevels << ", Format: " << pDesc->Format << ")";
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            ss << "Texture2D (" << pDesc->Width << "x" << pDesc->Height << ", Array: " << pDesc->DepthOrArraySize
               << ", Mips: " << pDesc->MipLevels << ", Format: " << pDesc->Format << ")";
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            ss << "Texture3D (" << pDesc->Width << "x" << pDesc->Height << "x" << pDesc->DepthOrArraySize
               << ", Mips: " << pDesc->MipLevels << ", Format: " << pDesc->Format << ")";
            break;
        default:
            ss << "ResourceDimension(" << pDesc->Dimension << ")";
            break;
    }

    if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ss << " [RT]";
    if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) ss << " [DS]";
    if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) ss << " [UAV]";

    return ss.str();
}

} // namespace kepler
