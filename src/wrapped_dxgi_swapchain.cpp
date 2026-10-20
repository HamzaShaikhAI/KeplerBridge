#include "translator/wrapped_dxgi_swapchain.h"
#include "translator/d3d11_backend.h"
#include "translator/resource_manager.h"
#include "core/logger.h"
#include "core/config.h"

#include <sstream>

namespace kepler {

// ============================================================================
// WrappedDXGISwapChain Implementation
// ============================================================================

WrappedDXGISwapChain::WrappedDXGISwapChain(
    IDXGISwapChain* pRealSwapChain,
    ID3D12Device* pD3D12Device,
    ID3D12CommandQueue* pQueue,
    HWND hwnd,
    const DXGI_SWAP_CHAIN_DESC1* pDesc1,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc)
    : m_pRealSwapChain(pRealSwapChain),
      m_pD3D12Device(pD3D12Device),
      m_pCommandQueue(pQueue),
      m_hwnd(hwnd),
      m_refCount(1),
      m_currentBackBufferIndex(0)
{
    if (m_pRealSwapChain) {
        m_pRealSwapChain->AddRef();
        m_pRealSwapChain->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&m_pRealSwapChain1));
        m_pRealSwapChain->QueryInterface(__uuidof(IDXGISwapChain2), reinterpret_cast<void**>(&m_pRealSwapChain2));
        m_pRealSwapChain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&m_pRealSwapChain3));
    }
    if (m_pD3D12Device) m_pD3D12Device->AddRef();
    if (m_pCommandQueue) m_pCommandQueue->AddRef();

    if (pDesc1) m_desc1 = *pDesc1;
    if (pFullscreenDesc) m_fullscreenDesc = *pFullscreenDesc;
}

WrappedDXGISwapChain::~WrappedDXGISwapChain() {
    ReleaseBackBuffers();

    if (m_pRealSwapChain3) {
        m_pRealSwapChain3->Release();
        m_pRealSwapChain3 = nullptr;
    }
    if (m_pRealSwapChain2) {
        m_pRealSwapChain2->Release();
        m_pRealSwapChain2 = nullptr;
    }
    if (m_pRealSwapChain1) {
        m_pRealSwapChain1->Release();
        m_pRealSwapChain1 = nullptr;
    }
    if (m_pRealSwapChain) {
        m_pRealSwapChain->Release();
        m_pRealSwapChain = nullptr;
    }

    if (m_pCommandQueue) {
        m_pCommandQueue->Release();
        m_pCommandQueue = nullptr;
    }
    if (m_pD3D12Device) {
        m_pD3D12Device->Release();
        m_pD3D12Device = nullptr;
    }
}

// IUnknown
HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = nullptr;

    if (InlineIsEqualGUID(riid, IID_IUnknown) ||
        InlineIsEqualGUID(riid, __uuidof(IDXGIObject)) ||
        InlineIsEqualGUID(riid, __uuidof(IDXGIDeviceSubObject)) ||
        InlineIsEqualGUID(riid, __uuidof(IDXGISwapChain)))
    {
        *ppvObject = static_cast<IDXGISwapChain*>(this);
        AddRef();
        return S_OK;
    }

    if (InlineIsEqualGUID(riid, __uuidof(IDXGISwapChain1))) {
        *ppvObject = static_cast<IDXGISwapChain1*>(this);
        AddRef();
        return S_OK;
    }

    if (InlineIsEqualGUID(riid, __uuidof(IDXGISwapChain2))) {
        *ppvObject = static_cast<IDXGISwapChain2*>(this);
        AddRef();
        return S_OK;
    }

    if (InlineIsEqualGUID(riid, __uuidof(IDXGISwapChain3))) {
        *ppvObject = static_cast<IDXGISwapChain3*>(this);
        AddRef();
        return S_OK;
    }

    if (m_pRealSwapChain) {
        return m_pRealSwapChain->QueryInterface(riid, ppvObject);
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WrappedDXGISwapChain::AddRef() {
    return ++m_refCount;
}

ULONG STDMETHODCALLTYPE WrappedDXGISwapChain::Release() {
    ULONG count = --m_refCount;
    if (count == 0) {
        delete this;
    }
    return count;
}

// IDXGIObject
HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) {
    return m_pRealSwapChain ? m_pRealSwapChain->SetPrivateData(Name, DataSize, pData) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown) {
    return m_pRealSwapChain ? m_pRealSwapChain->SetPrivateDataInterface(Name, pUnknown) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) {
    return m_pRealSwapChain ? m_pRealSwapChain->GetPrivateData(Name, pDataSize, pData) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetParent(REFIID riid, void** ppParent) {
    return m_pRealSwapChain ? m_pRealSwapChain->GetParent(riid, ppParent) : E_FAIL;
}

// IDXGIDeviceSubObject
HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetDevice(REFIID riid, void** ppDevice) {
    if (!ppDevice) return E_POINTER;
    if (m_pD3D12Device) {
        return m_pD3D12Device->QueryInterface(riid, ppDevice);
    }
    return m_pRealSwapChain ? m_pRealSwapChain->GetDevice(riid, ppDevice) : E_FAIL;
}

// IDXGISwapChain
HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::Present(UINT SyncInterval, UINT Flags) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Flush any pending D3D11 commands to hardware before presenting
    D3D11Backend::Instance().Flush();

    HRESULT hr = m_pRealSwapChain ? m_pRealSwapChain->Present(SyncInterval, Flags) : E_FAIL;

    if (SUCCEEDED(hr)) {
        if (!m_d3d12BackBuffers.empty()) {
            m_currentBackBufferIndex = (m_currentBackBufferIndex + 1) % static_cast<UINT>(m_d3d12BackBuffers.size());
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetBuffer(UINT Buffer, REFIID riid, void** ppSurface) {
    if (!ppSurface) return E_POINTER;
    *ppSurface = nullptr;
    std::lock_guard<std::mutex> lock(m_mutex);

    if (Buffer >= m_d3d12BackBuffers.size()) {
        return DXGI_ERROR_INVALID_CALL;
    }

    if (InlineIsEqualGUID(riid, __uuidof(ID3D12Resource)) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D12Pageable)) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D12DeviceChild)) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D12Object)) ||
        InlineIsEqualGUID(riid, IID_IUnknown))
    {
        return m_d3d12BackBuffers[Buffer]->QueryInterface(riid, ppSurface);
    }

    if (InlineIsEqualGUID(riid, __uuidof(ID3D11Texture2D)) ||
        InlineIsEqualGUID(riid, __uuidof(ID3D11Resource)))
    {
        return m_d3d11BackBuffers[Buffer]->QueryInterface(riid, ppSurface);
    }

    return m_pRealSwapChain ? m_pRealSwapChain->GetBuffer(Buffer, riid, ppSurface) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget) {
    return m_pRealSwapChain ? m_pRealSwapChain->SetFullscreenState(Fullscreen, pTarget) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget) {
    return m_pRealSwapChain ? m_pRealSwapChain->GetFullscreenState(pFullscreen, ppTarget) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc) {
    return m_pRealSwapChain ? m_pRealSwapChain->GetDesc(pDesc) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::ResizeBuffers(
    UINT BufferCount,
    UINT Width,
    UINT Height,
    DXGI_FORMAT NewFormat,
    UINT SwapChainFlags)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    ReleaseBackBuffers();
    D3D11Backend::Instance().Flush();

    HRESULT hr = m_pRealSwapChain ?
        m_pRealSwapChain->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags) : E_FAIL;

    if (FAILED(hr) && (SwapChainFlags & 0x200 /* DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING */) && m_pRealSwapChain) {
        UINT sanitizedFlags = SwapChainFlags & ~0x200;
        hr = m_pRealSwapChain->ResizeBuffers(BufferCount, Width, Height, NewFormat, sanitizedFlags);
        if (SUCCEEDED(hr)) {
            LOG_WARN("[SWAPCHAIN] ResizeBuffers succeeded after sanitizing unsupported DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING.");
        }
    }

    if (FAILED(hr)) {
        LOG_ERROR("[SWAPCHAIN] ResizeBuffers failed: " + Logger::HResultToString(hr));
        return hr;
    }

    hr = InitializeBackBuffers();
    if (FAILED(hr)) {
        LOG_ERROR("[SWAPCHAIN] Re-initializing backbuffers after resize failed: " + Logger::HResultToString(hr));
        return hr;
    }

    LOG_INFO("[SWAPCHAIN] ResizeBuffers succeeded: " + std::to_string(m_width) + "x" + std::to_string(m_height) +
             " (" + std::to_string(m_bufferCount) + " buffers)");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters) {
    return m_pRealSwapChain ? m_pRealSwapChain->ResizeTarget(pNewTargetParameters) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetContainingOutput(IDXGIOutput** ppOutput) {
    return m_pRealSwapChain ? m_pRealSwapChain->GetContainingOutput(ppOutput) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats) {
    return m_pRealSwapChain ? m_pRealSwapChain->GetFrameStatistics(pStats) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetLastPresentCount(UINT* pLastPresentCount) {
    return m_pRealSwapChain ? m_pRealSwapChain->GetLastPresentCount(pLastPresentCount) : E_FAIL;
}

// IDXGISwapChain1
HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc) {
    if (!pDesc) return E_POINTER;
    if (m_pRealSwapChain1) {
        return m_pRealSwapChain1->GetDesc1(pDesc);
    }
    *pDesc = m_desc1;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc) {
    if (!pDesc) return E_POINTER;
    if (m_pRealSwapChain1) {
        return m_pRealSwapChain1->GetFullscreenDesc(pDesc);
    }
    *pDesc = m_fullscreenDesc;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetHwnd(HWND* pHwnd) {
    if (!pHwnd) return E_POINTER;
    *pHwnd = m_hwnd;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetCoreWindow(REFIID refiid, void** ppUnk) {
    return m_pRealSwapChain1 ? m_pRealSwapChain1->GetCoreWindow(refiid, ppUnk) : DXGI_ERROR_INVALID_CALL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::Present1(
    UINT SyncInterval,
    UINT PresentFlags,
    const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    D3D11Backend::Instance().Flush();

    HRESULT hr = E_FAIL;
    if (m_pRealSwapChain1) {
        hr = m_pRealSwapChain1->Present1(SyncInterval, PresentFlags, pPresentParameters);
        if (FAILED(hr) && (PresentFlags & 0x200 /* DXGI_PRESENT_ALLOW_TEARING */)) {
            hr = m_pRealSwapChain1->Present1(SyncInterval, PresentFlags & ~0x200, pPresentParameters);
        }
    } else if (m_pRealSwapChain) {
        hr = m_pRealSwapChain->Present(SyncInterval, PresentFlags);
        if (FAILED(hr) && (PresentFlags & 0x200 /* DXGI_PRESENT_ALLOW_TEARING */)) {
            hr = m_pRealSwapChain->Present(SyncInterval, PresentFlags & ~0x200);
        }
    }

    if (SUCCEEDED(hr)) {
        if (!m_d3d12BackBuffers.empty()) {
            m_currentBackBufferIndex = (m_currentBackBufferIndex + 1) % static_cast<UINT>(m_d3d12BackBuffers.size());
        }
    }
    return hr;
}

BOOL STDMETHODCALLTYPE WrappedDXGISwapChain::IsTemporaryMonoSupported() {
    return m_pRealSwapChain1 ? m_pRealSwapChain1->IsTemporaryMonoSupported() : FALSE;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetRestrictToOutput(IDXGIOutput** ppRestrictToOutput) {
    return m_pRealSwapChain1 ? m_pRealSwapChain1->GetRestrictToOutput(ppRestrictToOutput) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::SetBackgroundColor(const DXGI_RGBA* pColor) {
    return m_pRealSwapChain1 ? m_pRealSwapChain1->SetBackgroundColor(pColor) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetBackgroundColor(DXGI_RGBA* pColor) {
    return m_pRealSwapChain1 ? m_pRealSwapChain1->GetBackgroundColor(pColor) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::SetRotation(DXGI_MODE_ROTATION Rotation) {
    return m_pRealSwapChain1 ? m_pRealSwapChain1->SetRotation(Rotation) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetRotation(DXGI_MODE_ROTATION* pRotation) {
    return m_pRealSwapChain1 ? m_pRealSwapChain1->GetRotation(pRotation) : E_FAIL;
}

// IDXGISwapChain2
HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::SetSourceSize(UINT Width, UINT Height) {
    return m_pRealSwapChain2 ? m_pRealSwapChain2->SetSourceSize(Width, Height) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetSourceSize(UINT* pWidth, UINT* pHeight) {
    return m_pRealSwapChain2 ? m_pRealSwapChain2->GetSourceSize(pWidth, pHeight) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::SetMaximumFrameLatency(UINT MaxLatency) {
    return m_pRealSwapChain2 ? m_pRealSwapChain2->SetMaximumFrameLatency(MaxLatency) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetMaximumFrameLatency(UINT* pMaxLatency) {
    return m_pRealSwapChain2 ? m_pRealSwapChain2->GetMaximumFrameLatency(pMaxLatency) : E_FAIL;
}

HANDLE STDMETHODCALLTYPE WrappedDXGISwapChain::GetFrameLatencyWaitableObject() {
    return m_pRealSwapChain2 ? m_pRealSwapChain2->GetFrameLatencyWaitableObject() : NULL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix) {
    return m_pRealSwapChain2 ? m_pRealSwapChain2->SetMatrixTransform(pMatrix) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::GetMatrixTransform(DXGI_MATRIX_3X2_F* pMatrix) {
    return m_pRealSwapChain2 ? m_pRealSwapChain2->GetMatrixTransform(pMatrix) : E_FAIL;
}

// IDXGISwapChain3
UINT STDMETHODCALLTYPE WrappedDXGISwapChain::GetCurrentBackBufferIndex() {
    return m_currentBackBufferIndex.load();
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::CheckColorSpaceSupport(
    DXGI_COLOR_SPACE_TYPE ColorSpace,
    UINT* pColorSpaceSupport)
{
    return m_pRealSwapChain3 ?
        m_pRealSwapChain3->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) {
    return m_pRealSwapChain3 ? m_pRealSwapChain3->SetColorSpace1(ColorSpace) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE WrappedDXGISwapChain::ResizeBuffers1(
    UINT BufferCount,
    UINT Width,
    UINT Height,
    DXGI_FORMAT Format,
    UINT SwapChainFlags,
    const UINT* pCreationNodes,
    IUnknown* const* ppPresentQueue)
{
    return ResizeBuffers(BufferCount, Width, Height, Format, SwapChainFlags);
}

// Internal backbuffer lifecycle
HRESULT WrappedDXGISwapChain::InitializeBackBuffers() {
    if (!m_pRealSwapChain || !m_pD3D12Device) return E_FAIL;

    DXGI_SWAP_CHAIN_DESC desc = {};
    m_pRealSwapChain->GetDesc(&desc);

    m_width = desc.BufferDesc.Width;
    m_height = desc.BufferDesc.Height;
    m_format = desc.BufferDesc.Format;
    m_bufferCount = desc.BufferCount;
    m_currentBackBufferIndex = 0;

    m_d3d12BackBuffers.reserve(m_bufferCount);
    m_d3d11BackBuffers.reserve(m_bufferCount);

    for (UINT i = 0; i < m_bufferCount; ++i) {
        ID3D11Texture2D* pD3D11Tex = nullptr;
        HRESULT hr = m_pRealSwapChain->GetBuffer(i, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pD3D11Tex));
        if (FAILED(hr) || !pD3D11Tex) {
            // For legacy bitblt swapchains (DXGI_SWAP_EFFECT_DISCARD), DXGI only exposes index 0.
            // If i > 0 and buffer 0 succeeded, share buffer 0's D3D11 texture
            if (i > 0 && !m_d3d11BackBuffers.empty() && m_d3d11BackBuffers[0] != nullptr) {
                pD3D11Tex = m_d3d11BackBuffers[0];
                pD3D11Tex->AddRef();
            } else {
                LOG_ERROR("[SWAPCHAIN] Failed to retrieve D3D11 backbuffer texture index " + std::to_string(i));
                return hr;
            }
        }

        D3D11_TEXTURE2D_DESC texDesc = {};
        pD3D11Tex->GetDesc(&texDesc);

        D3D12_HEAP_PROPERTIES defaultHeap = {};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rDesc = {};
        rDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rDesc.Width = texDesc.Width;
        rDesc.Height = texDesc.Height;
        rDesc.DepthOrArraySize = 1;
        rDesc.MipLevels = 1;
        rDesc.Format = texDesc.Format;
        rDesc.SampleDesc = texDesc.SampleDesc;
        rDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        ID3D12Resource* pD3D12Res = nullptr;
        hr = m_pD3D12Device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &rDesc,
            D3D12_RESOURCE_STATE_PRESENT,
            nullptr,
            __uuidof(ID3D12Resource),
            reinterpret_cast<void**>(&pD3D12Res));

        if (FAILED(hr) || !pD3D12Res) {
            LOG_ERROR("[SWAPCHAIN] Failed to create D3D12 backbuffer resource proxy index " + std::to_string(i));
            pD3D11Tex->Release();
            return hr;
        }

        // Register in ResourceManager: maps pD3D12Res -> pD3D11Tex
        ResourceManager::Instance().RegisterResource(pD3D12Res, rDesc, &defaultHeap, pD3D11Tex);

        m_d3d11BackBuffers.push_back(pD3D11Tex);
        m_d3d12BackBuffers.push_back(pD3D12Res);
    }

    LOG_INFO("[SWAPCHAIN] Initialized " + std::to_string(m_bufferCount) + " backbuffers (" +
             std::to_string(m_width) + "x" + std::to_string(m_height) + ") with D3D11-to-D3D12 mapping.");
    return S_OK;
}

void WrappedDXGISwapChain::ReleaseBackBuffers() {
    for (size_t i = 0; i < m_d3d12BackBuffers.size(); ++i) {
        if (m_d3d12BackBuffers[i]) {
            ResourceManager::Instance().UnregisterResource(m_d3d12BackBuffers[i]);
            m_d3d12BackBuffers[i]->Release();
        }
    }
    m_d3d12BackBuffers.clear();

    for (size_t i = 0; i < m_d3d11BackBuffers.size(); ++i) {
        if (m_d3d11BackBuffers[i]) {
            m_d3d11BackBuffers[i]->Release();
        }
    }
    m_d3d11BackBuffers.clear();
}

// ============================================================================
// Application-Facing Factory Implementation
// ============================================================================

HRESULT CreateApplicationSwapChainForHwnd(
    ID3D12CommandQueue* pCommandQueue,
    HWND hwnd,
    const DXGI_SWAP_CHAIN_DESC1* pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
    IDXGISwapChain3** ppSwapChain)
{
    if (!pCommandQueue || !hwnd || !pDesc || !ppSwapChain) return E_POINTER;
    *ppSwapChain = nullptr;

    ID3D12Device* pDevice = nullptr;
    HRESULT hr = pCommandQueue->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&pDevice));
    if (FAILED(hr) || !pDevice) {
        LOG_ERROR("[SWAPCHAIN] Failed to obtain D3D12 device from command queue.");
        return E_FAIL;
    }

    if (!D3D11Backend::Instance().IsInitialized()) {
        IDXGIAdapter* pAdapter = D3D11Backend::GetMatchingAdapter(pDevice);
        D3D11Backend::Instance().Initialize(pAdapter);
        if (pAdapter) pAdapter->Release();
    }

    ID3D11Device* pD3D11Dev = D3D11Backend::Instance().GetDevice();
    if (!pD3D11Dev) {
        pDevice->Release();
        return E_FAIL;
    }

    IDXGIDevice* pDXGIDevice = nullptr;
    hr = pD3D11Dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&pDXGIDevice));
    if (FAILED(hr) || !pDXGIDevice) {
        pDevice->Release();
        return hr;
    }

    IDXGIAdapter* pAdapter = nullptr;
    hr = pDXGIDevice->GetAdapter(&pAdapter);
    pDXGIDevice->Release();
    if (FAILED(hr) || !pAdapter) {
        pDevice->Release();
        return hr;
    }

    IDXGIFactory2* pFactory2 = nullptr;
    hr = pAdapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&pFactory2));
    pAdapter->Release();

    IDXGISwapChain1* pRealSwapChain1 = nullptr;
    if (SUCCEEDED(hr) && pFactory2) {
        hr = pFactory2->CreateSwapChainForHwnd(pD3D11Dev, hwnd, pDesc, pFullscreenDesc, nullptr, &pRealSwapChain1);
        if (FAILED(hr) || !pRealSwapChain1) {
            // Attempt fallback with DXGI_SWAP_EFFECT_DISCARD if FLIP modes fail on older D3D11 configurations
            DXGI_SWAP_CHAIN_DESC1 fallbackDesc = *pDesc;
            fallbackDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            fallbackDesc.Flags &= ~(DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | 0x200 /* DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING */);
            hr = pFactory2->CreateSwapChainForHwnd(pD3D11Dev, hwnd, &fallbackDesc, pFullscreenDesc, nullptr, &pRealSwapChain1);
            if (SUCCEEDED(hr) && pRealSwapChain1) {
                LOG_WARN("[SWAPCHAIN] Modern flip swapchain creation failed; successfully fell back to DXGI_SWAP_EFFECT_DISCARD.");
            }
        }
        pFactory2->Release();
    }

    if (!pRealSwapChain1) {
        // Fallback to DXGIFactory1
        IDXGIFactory1* pFactory1 = nullptr;
        CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&pFactory1));
        if (pFactory1) {
            DXGI_SWAP_CHAIN_DESC scDesc = {};
            scDesc.BufferCount = pDesc->BufferCount;
            scDesc.BufferDesc.Width = pDesc->Width;
            scDesc.BufferDesc.Height = pDesc->Height;
            scDesc.BufferDesc.Format = pDesc->Format;
            scDesc.BufferDesc.RefreshRate.Numerator = 60;
            scDesc.BufferDesc.RefreshRate.Denominator = 1;
            scDesc.BufferUsage = pDesc->BufferUsage;
            scDesc.OutputWindow = hwnd;
            scDesc.SampleDesc = pDesc->SampleDesc;
            scDesc.Windowed = pFullscreenDesc ? pFullscreenDesc->Windowed : TRUE;
            scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

            IDXGISwapChain* pRealSC = nullptr;
            hr = pFactory1->CreateSwapChain(pD3D11Dev, &scDesc, &pRealSC);
            pFactory1->Release();
            if (SUCCEEDED(hr) && pRealSC) {
                pRealSC->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&pRealSwapChain1));
                pRealSC->Release();
            }
        }
    }

    if (FAILED(hr) || !pRealSwapChain1) {
        LOG_ERROR("[SWAPCHAIN] CreateSwapChainForHwnd on D3D11 backend failed: " + Logger::HResultToString(hr));
        pDevice->Release();
        return hr;
    }

    auto* wrapped = new WrappedDXGISwapChain(pRealSwapChain1, pDevice, pCommandQueue, hwnd, pDesc, pFullscreenDesc);
    pRealSwapChain1->Release();
    pDevice->Release();

    hr = wrapped->InitializeBackBuffers();
    if (FAILED(hr)) {
        wrapped->Release();
        return hr;
    }

    hr = wrapped->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(ppSwapChain));
    wrapped->Release();
    return hr;
}

HRESULT CreateApplicationSwapChain(
    ID3D12CommandQueue* pCommandQueue,
    const DXGI_SWAP_CHAIN_DESC* pDesc,
    IDXGISwapChain** ppSwapChain)
{
    if (!pCommandQueue || !pDesc || !ppSwapChain) return E_POINTER;
    *ppSwapChain = nullptr;

    DXGI_SWAP_CHAIN_DESC1 desc1 = {};
    desc1.Width = pDesc->BufferDesc.Width;
    desc1.Height = pDesc->BufferDesc.Height;
    desc1.Format = pDesc->BufferDesc.Format;
    desc1.Stereo = FALSE;
    desc1.SampleDesc = pDesc->SampleDesc;
    desc1.BufferUsage = pDesc->BufferUsage;
    desc1.BufferCount = pDesc->BufferCount;
    desc1.Scaling = DXGI_SCALING_STRETCH;
    desc1.SwapEffect = pDesc->SwapEffect;
    desc1.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    desc1.Flags = pDesc->Flags;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsDesc = {};
    fsDesc.RefreshRate = pDesc->BufferDesc.RefreshRate;
    fsDesc.ScanlineOrdering = pDesc->BufferDesc.ScanlineOrdering;
    fsDesc.Scaling = pDesc->BufferDesc.Scaling;
    fsDesc.Windowed = pDesc->Windowed;

    IDXGISwapChain3* pSC3 = nullptr;
    HRESULT hr = CreateApplicationSwapChainForHwnd(
        pCommandQueue,
        pDesc->OutputWindow,
        &desc1,
        &fsDesc,
        &pSC3);

    if (SUCCEEDED(hr) && pSC3) {
        hr = pSC3->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(ppSwapChain));
        pSC3->Release();
    }
    return hr;
}

} // namespace kepler

// ============================================================================
// C-Linkage Export Implementations
// ============================================================================

extern "C" {

HRESULT WINAPI KeplerBridge_CreateDXGISwapChainForHwnd(
    ID3D12CommandQueue* pCommandQueue,
    HWND hwnd,
    const DXGI_SWAP_CHAIN_DESC1* pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
    IDXGISwapChain3** ppSwapChain)
{
    return kepler::CreateApplicationSwapChainForHwnd(
        pCommandQueue, hwnd, pDesc, pFullscreenDesc, ppSwapChain);
}

HRESULT WINAPI KeplerBridge_CreateDXGISwapChain(
    ID3D12CommandQueue* pCommandQueue,
    const DXGI_SWAP_CHAIN_DESC* pDesc,
    IDXGISwapChain** ppSwapChain)
{
    return kepler::CreateApplicationSwapChain(
        pCommandQueue, pDesc, ppSwapChain);
}

} // extern "C"
