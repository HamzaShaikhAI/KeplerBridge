#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <d3d11.h>

#include <vector>
#include <atomic>
#include <mutex>

namespace kepler {

class WrappedDXGISwapChain : public IDXGISwapChain3 {
public:
    WrappedDXGISwapChain(
        IDXGISwapChain* pRealSwapChain,
        ID3D12Device* pD3D12Device,
        ID3D12CommandQueue* pQueue,
        HWND hwnd,
        const DXGI_SWAP_CHAIN_DESC1* pDesc1 = nullptr,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc = nullptr);
    virtual ~WrappedDXGISwapChain();

    // IUnknown
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    virtual ULONG STDMETHODCALLTYPE AddRef() override;
    virtual ULONG STDMETHODCALLTYPE Release() override;

    // IDXGIObject
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) override;
    virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown) override;
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) override;
    virtual HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent) override;

    // IDXGIDeviceSubObject
    virtual HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppDevice) override;

    // IDXGISwapChain
    virtual HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags) override;
    virtual HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, REFIID riid, void** ppSurface) override;
    virtual HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget) override;
    virtual HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget) override;
    virtual HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc) override;
    virtual HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) override;
    virtual HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters) override;
    virtual HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** ppOutput) override;
    virtual HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats) override;
    virtual HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* pLastPresentCount) override;

    // IDXGISwapChain1
    virtual HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc) override;
    virtual HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc) override;
    virtual HRESULT STDMETHODCALLTYPE GetHwnd(HWND* pHwnd) override;
    virtual HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID refiid, void** ppUnk) override;
    virtual HRESULT STDMETHODCALLTYPE Present1(UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters) override;
    virtual BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override;
    virtual HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** ppRestrictToOutput) override;
    virtual HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* pColor) override;
    virtual HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* pColor) override;
    virtual HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION Rotation) override;
    virtual HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* pRotation) override;

    // IDXGISwapChain2
    virtual HRESULT STDMETHODCALLTYPE SetSourceSize(UINT Width, UINT Height) override;
    virtual HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* pWidth, UINT* pHeight) override;
    virtual HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT MaxLatency) override;
    virtual HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* pMaxLatency) override;
    virtual HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override;
    virtual HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix) override;
    virtual HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* pMatrix) override;

    // IDXGISwapChain3
    virtual UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override;
    virtual HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace, UINT* pColorSpaceSupport) override;
    virtual HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) override;
    virtual HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags, const UINT* pCreationNodes, IUnknown* const* ppPresentQueue) override;

    // Internal initialization & teardown
    HRESULT InitializeBackBuffers();
    void ReleaseBackBuffers();

    IDXGISwapChain* GetReal() const { return m_pRealSwapChain; }
    UINT GetBufferCount() const { return static_cast<UINT>(m_d3d12BackBuffers.size()); }
    ID3D12Resource* GetD3D12BackBuffer(UINT index) const {
        return (index < m_d3d12BackBuffers.size()) ? m_d3d12BackBuffers[index] : nullptr;
    }
    ID3D11Texture2D* GetD3D11BackBuffer(UINT index) const {
        return (index < m_d3d11BackBuffers.size()) ? m_d3d11BackBuffers[index] : nullptr;
    }

private:
    IDXGISwapChain* m_pRealSwapChain = nullptr;
    IDXGISwapChain1* m_pRealSwapChain1 = nullptr;
    IDXGISwapChain2* m_pRealSwapChain2 = nullptr;
    IDXGISwapChain3* m_pRealSwapChain3 = nullptr;

    ID3D12Device* m_pD3D12Device = nullptr;
    ID3D12CommandQueue* m_pCommandQueue = nullptr;
    HWND m_hwnd = nullptr;

    DXGI_SWAP_CHAIN_DESC1 m_desc1 = {};
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC m_fullscreenDesc = {};

    UINT m_width = 0;
    UINT m_height = 0;
    DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
    UINT m_bufferCount = 0;

    std::vector<ID3D12Resource*> m_d3d12BackBuffers;
    std::vector<ID3D11Texture2D*> m_d3d11BackBuffers;

    std::atomic<UINT> m_currentBackBufferIndex{ 0 };
    std::atomic<ULONG> m_refCount{ 1 };
    std::mutex m_mutex;
};

// Application-Facing Factory API
HRESULT CreateApplicationSwapChainForHwnd(
    ID3D12CommandQueue* pCommandQueue,
    HWND hwnd,
    const DXGI_SWAP_CHAIN_DESC1* pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
    IDXGISwapChain3** ppSwapChain);

HRESULT CreateApplicationSwapChain(
    ID3D12CommandQueue* pCommandQueue,
    const DXGI_SWAP_CHAIN_DESC* pDesc,
    IDXGISwapChain** ppSwapChain);

} // namespace kepler

// C-linkage export helpers
extern "C" {
HRESULT WINAPI KeplerBridge_CreateDXGISwapChainForHwnd(
    ID3D12CommandQueue* pCommandQueue,
    HWND hwnd,
    const DXGI_SWAP_CHAIN_DESC1* pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
    IDXGISwapChain3** ppSwapChain);

HRESULT WINAPI KeplerBridge_CreateDXGISwapChain(
    ID3D12CommandQueue* pCommandQueue,
    const DXGI_SWAP_CHAIN_DESC* pDesc,
    IDXGISwapChain** ppSwapChain);
}
