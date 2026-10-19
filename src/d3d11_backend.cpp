#include "translator/d3d11_backend.h"
#include "translator/command_translator.h"
#include "translator/resource_manager.h"
#include "core/logger.h"
#include "core/config.h"

namespace kepler {

D3D11Backend& D3D11Backend::Instance() {
    static D3D11Backend s_instance;
    return s_instance;
}

D3D11Backend::~D3D11Backend() {
    Shutdown();
}

bool D3D11Backend::Initialize(IDXGIAdapter* pAdapter) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        return true;
    }

    D3D_DRIVER_TYPE driverType = pAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
    UINT createFlags = 0;

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    UINT numFeatureLevels = sizeof(featureLevels) / sizeof(featureLevels[0]);

    HRESULT hr = D3D11CreateDevice(
        pAdapter,
        driverType,
        nullptr,
        createFlags,
        featureLevels,
        numFeatureLevels,
        D3D11_SDK_VERSION,
        &m_pDevice,
        &m_featureLevel,
        &m_pContext);

    if (FAILED(hr) || !m_pDevice || !m_pContext) {
        LOG_ERROR("[D3D11Backend] Failed to initialize D3D11 device and context: " + Logger::HResultToString(hr));
        return false;
    }

    m_initialized = true;
    LOG_INFO("[D3D11Backend] Initialized Direct3D 11 backend successfully. Feature Level: " +
             Logger::FeatureLevelToString(static_cast<D3D_FEATURE_LEVEL>(m_featureLevel)));
    return true;
}

void D3D11Backend::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pContext) {
        m_pContext->ClearState();
        m_pContext->Flush();
        m_pContext->Release();
        m_pContext = nullptr;
    }
    if (m_pDevice) {
        m_pDevice->Release();
        m_pDevice = nullptr;
    }
    m_initialized = false;
}

HRESULT D3D11Backend::CreateTranslatedBuffer(
    const D3D12_RESOURCE_DESC* pDesc,
    const D3D12_HEAP_PROPERTIES* pHeapProperties,
    ID3D11Buffer** ppOutBuffer)
{
    if (!ppOutBuffer) return E_POINTER;
    *ppOutBuffer = nullptr;
    if (!m_initialized || !m_pDevice || !pDesc) return E_FAIL;

    D3D11_BUFFER_DESC desc11 = {};
    desc11.ByteWidth = static_cast<UINT>(pDesc->Width);

    // Map bind flags
    desc11.BindFlags = 0;
    if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) {
        desc11.BindFlags |= D3D11_BIND_RENDER_TARGET;
    }
    if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) {
        desc11.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    }
    if (!(pDesc->Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
        desc11.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }

    // Default to vertex buffer and constant buffer binding if zero
    if (desc11.BindFlags == 0) {
        desc11.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER;
    }

    // Map heap properties to usage
    if (pHeapProperties) {
        if (pHeapProperties->Type == D3D12_HEAP_TYPE_UPLOAD) {
            desc11.Usage = D3D11_USAGE_DYNAMIC;
            desc11.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            desc11.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER | D3D11_BIND_CONSTANT_BUFFER;
        } else if (pHeapProperties->Type == D3D12_HEAP_TYPE_READBACK) {
            desc11.Usage = D3D11_USAGE_STAGING;
            desc11.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc11.BindFlags = 0;
        } else {
            desc11.Usage = D3D11_USAGE_DEFAULT;
        }
    } else {
        desc11.Usage = D3D11_USAGE_DEFAULT;
    }

    return m_pDevice->CreateBuffer(&desc11, nullptr, ppOutBuffer);
}

HRESULT D3D11Backend::CreateTranslatedTexture2D(
    const D3D12_RESOURCE_DESC* pDesc,
    const D3D12_HEAP_PROPERTIES* pHeapProperties,
    ID3D11Texture2D** ppOutTexture)
{
    if (!ppOutTexture) return E_POINTER;
    *ppOutTexture = nullptr;
    if (!m_initialized || !m_pDevice || !pDesc) return E_FAIL;

    D3D11_TEXTURE2D_DESC desc11 = {};
    desc11.Width = static_cast<UINT>(pDesc->Width);
    desc11.Height = pDesc->Height;
    desc11.MipLevels = pDesc->MipLevels;
    desc11.ArraySize = pDesc->DepthOrArraySize;
    desc11.Format = pDesc->Format;
    desc11.SampleDesc.Count = pDesc->SampleDesc.Count;
    desc11.SampleDesc.Quality = pDesc->SampleDesc.Quality;

    desc11.BindFlags = 0;
    if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) {
        desc11.BindFlags |= D3D11_BIND_RENDER_TARGET;
    }
    if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
        desc11.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
    }
    if (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) {
        desc11.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    }
    if (!(pDesc->Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
        desc11.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }

    if (pHeapProperties && pHeapProperties->Type == D3D12_HEAP_TYPE_READBACK) {
        desc11.Usage = D3D11_USAGE_STAGING;
        desc11.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc11.BindFlags = 0;
    } else {
        desc11.Usage = D3D11_USAGE_DEFAULT;
    }

    return m_pDevice->CreateTexture2D(&desc11, nullptr, ppOutTexture);
}

void D3D11Backend::ResolveBarrierHazards(const RecordedCommand& cmd) {
    if (!m_pContext) return;
    for (const auto& b : cmd.barrier.barriers) {
        if (b.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
            // If transitioning to Shader Resource, unbind RTV to avoid D3D11 simultaneous RTV/SRV binding hazard
            if (b.Transition.StateAfter & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE ||
                b.Transition.StateAfter & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
            {
                ID3D11RenderTargetView* nullRTV[1] = { nullptr };
                m_pContext->OMSetRenderTargets(1, nullRTV, nullptr);
            }
            // If transitioning from UAV, unbind UAVs
            if (b.Transition.StateBefore & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
                m_pContext->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
            }
        }
    }
}

HRESULT D3D11Backend::ExecuteSingleCommand(const RecordedCommand& cmd) {
    if (!m_initialized || !m_pContext) return E_FAIL;

    switch (cmd.type) {
        case CommandType::ClearRenderTarget: {
            ID3D11RenderTargetView* pRTV = ResourceManager::Instance().GetRTV(cmd.clearRTV.renderTargetView.ptr);
            if (pRTV) {
                m_pContext->ClearRenderTargetView(pRTV, cmd.clearRTV.colorRGBA);
            }
            break;
        }

        case CommandType::ClearDepthStencil: {
            ID3D11DepthStencilView* pDSV = ResourceManager::Instance().GetDSV(cmd.clearDSV.depthStencilView.ptr);
            if (pDSV) {
                UINT flags11 = 0;
                if (cmd.clearDSV.clearFlags & D3D12_CLEAR_FLAG_DEPTH) flags11 |= D3D11_CLEAR_DEPTH;
                if (cmd.clearDSV.clearFlags & D3D12_CLEAR_FLAG_STENCIL) flags11 |= D3D11_CLEAR_STENCIL;
                if (flags11 != 0) {
                    m_pContext->ClearDepthStencilView(pDSV, flags11, cmd.clearDSV.depth, cmd.clearDSV.stencil);
                }
            }
            break;
        }

        case CommandType::DrawInstanced: {
            m_pContext->DrawInstanced(
                cmd.drawInstanced.vertexCountPerInstance,
                cmd.drawInstanced.instanceCount,
                cmd.drawInstanced.startVertexLocation,
                cmd.drawInstanced.startInstanceLocation);
            break;
        }

        case CommandType::DrawIndexedInstanced: {
            m_pContext->DrawIndexedInstanced(
                cmd.drawIndexed.indexCountPerInstance,
                cmd.drawIndexed.instanceCount,
                cmd.drawIndexed.startIndexLocation,
                cmd.drawIndexed.baseVertexLocation,
                cmd.drawIndexed.startInstanceLocation);
            break;
        }

        case CommandType::Dispatch: {
            m_pContext->Dispatch(
                cmd.dispatch.threadGroupCountX,
                cmd.dispatch.threadGroupCountY,
                cmd.dispatch.threadGroupCountZ);
            break;
        }

        case CommandType::CopyBufferRegion: {
            TranslatedResourceInfo dstInfo, srcInfo;
            if (ResourceManager::Instance().GetResourceInfo(cmd.copyBuffer.pDstBuffer, dstInfo) &&
                ResourceManager::Instance().GetResourceInfo(cmd.copyBuffer.pSrcBuffer, srcInfo))
            {
                if (dstInfo.pD3D11Buffer && srcInfo.pD3D11Buffer) {
                    D3D11_BOX srcBox = {};
                    srcBox.left = static_cast<UINT>(cmd.copyBuffer.srcOffset);
                    srcBox.right = static_cast<UINT>(cmd.copyBuffer.srcOffset + cmd.copyBuffer.numBytes);
                    srcBox.top = 0; srcBox.bottom = 1;
                    srcBox.front = 0; srcBox.back = 1;

                    m_pContext->CopySubresourceRegion(
                        dstInfo.pD3D11Buffer, 0,
                        static_cast<UINT>(cmd.copyBuffer.dstOffset), 0, 0,
                        srcInfo.pD3D11Buffer, 0, &srcBox);
                }
            }
            break;
        }

        case CommandType::CopyResource: {
            TranslatedResourceInfo dstInfo, srcInfo;
            if (ResourceManager::Instance().GetResourceInfo(cmd.copyResource.pDstResource, dstInfo) &&
                ResourceManager::Instance().GetResourceInfo(cmd.copyResource.pSrcResource, srcInfo))
            {
                if (dstInfo.pD3D11Resource && srcInfo.pD3D11Resource) {
                    m_pContext->CopyResource(dstInfo.pD3D11Resource, srcInfo.pD3D11Resource);
                }
            }
            break;
        }

        case CommandType::ResourceBarrier: {
            if (Config::Instance().IsBarrierEmulationEnabled()) {
                ResolveBarrierHazards(cmd);
            }
            break;
        }

        case CommandType::SetRenderTargets: {
            std::vector<ID3D11RenderTargetView*> rtvs;
            for (const auto& h : cmd.setRTVs.rtvHandles) {
                ID3D11RenderTargetView* pRTV = ResourceManager::Instance().GetRTV(h.ptr);
                if (pRTV) rtvs.push_back(pRTV);
            }
            ID3D11DepthStencilView* pDSV = cmd.setRTVs.hasDepthStencil ?
                ResourceManager::Instance().GetDSV(cmd.setRTVs.dsvHandle.ptr) : nullptr;

            m_pContext->OMSetRenderTargets(
                static_cast<UINT>(rtvs.size()),
                rtvs.empty() ? nullptr : rtvs.data(),
                pDSV);
            break;
        }

        case CommandType::SetViewports: {
            std::vector<D3D11_VIEWPORT> vps;
            for (const auto& vp : cmd.setViewports.viewports) {
                D3D11_VIEWPORT vp11;
                vp11.TopLeftX = vp.TopLeftX;
                vp11.TopLeftY = vp.TopLeftY;
                vp11.Width = vp.Width;
                vp11.Height = vp.Height;
                vp11.MinDepth = vp.MinDepth;
                vp11.MaxDepth = vp.MaxDepth;
                vps.push_back(vp11);
            }
            if (!vps.empty()) {
                m_pContext->RSSetViewports(static_cast<UINT>(vps.size()), vps.data());
            }
            break;
        }

        case CommandType::SetScissorRects: {
            std::vector<D3D11_RECT> rects;
            for (const auto& r : cmd.setScissors.rects) {
                D3D11_RECT r11;
                r11.left = r.left;
                r11.top = r.top;
                r11.right = r.right;
                r11.bottom = r.bottom;
                rects.push_back(r11);
            }
            if (!rects.empty()) {
                m_pContext->RSSetScissorRects(static_cast<UINT>(rects.size()), rects.data());
            }
            break;
        }

        case CommandType::IASetPrimitiveTopology: {
            m_pContext->IASetPrimitiveTopology(
                static_cast<D3D11_PRIMITIVE_TOPOLOGY>(cmd.setTopology.topology));
            break;
        }

        case CommandType::IASetVertexBuffers: {
            for (size_t i = 0; i < cmd.setVBs.views.size(); ++i) {
                const auto& vb = cmd.setVBs.views[i];
                TranslatedResourceInfo info;
                ID3D11Buffer* pBuf = nullptr;
                if (ResourceManager::Instance().GetResourceInfo(vb.pBuffer, info)) {
                    pBuf = info.pD3D11Buffer;
                }
                UINT stride = vb.strideInBytes;
                UINT offset = static_cast<UINT>(vb.offset);
                UINT slot = cmd.setVBs.startSlot + static_cast<UINT>(i);
                m_pContext->IASetVertexBuffers(slot, 1, &pBuf, &stride, &offset);
            }
            break;
        }

        case CommandType::IASetIndexBuffer: {
            TranslatedResourceInfo info;
            ID3D11Buffer* pBuf = nullptr;
            if (ResourceManager::Instance().GetResourceInfo(cmd.setIB.pBuffer, info)) {
                pBuf = info.pD3D11Buffer;
            }
            m_pContext->IASetIndexBuffer(pBuf, cmd.setIB.format, static_cast<UINT>(cmd.setIB.offset));
            break;
        }

        case CommandType::SetGraphicsRoot32BitConstants:
        case CommandType::SetComputeRoot32BitConstants: {
            ResourceManager::Instance().BindRootConstants(
                cmd.setRootConstants.rootParameterIndex,
                cmd.setRootConstants.constants,
                cmd.setRootConstants.isCompute);
            break;
        }

        default:
            break;
    }

    return S_OK;
}

HRESULT D3D11Backend::ReplayCommandBatch(const CommandBatch& batch) {
    if (!m_initialized || !m_pContext) return E_FAIL;
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& cmd : batch.GetCommands()) {
        ExecuteSingleCommand(cmd);
    }

    if (Config::Instance().IsImmediateQueueFlushEnabled()) {
        m_pContext->Flush();
    }
    return S_OK;
}

void D3D11Backend::Flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pContext) {
        m_pContext->Flush();
    }
}

} // namespace kepler
