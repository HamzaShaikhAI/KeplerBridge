#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>

namespace kepler {

class PSOHook {
public:
    static HRESULT InterceptCreateGraphicsPipelineState(
        ID3D12Device* pRealDevice,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc,
        REFIID riid,
        void** ppPipelineState);

    static HRESULT InterceptCreateComputePipelineState(
        ID3D12Device* pRealDevice,
        const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc,
        REFIID riid,
        void** ppPipelineState);
};

} // namespace kepler
