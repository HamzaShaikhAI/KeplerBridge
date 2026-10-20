#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>

namespace kepler {

class CommandBatch;
struct RecordedCommand;
struct TranslatedPipelineState;

class D3D11Backend {
public:
    static D3D11Backend& Instance();

    bool Initialize(IDXGIAdapter* pAdapter = nullptr);
    void Shutdown(bool isProcessDetach = false);
    bool IsInitialized() const { return m_initialized; }

    static IDXGIAdapter* GetMatchingAdapter(ID3D12Device* pRealDevice);

    ID3D11Device* GetDevice() const { return m_pDevice; }
    ID3D11DeviceContext* GetContext() const { return m_pContext; }

    // Resource creation helpers
    HRESULT CreateTranslatedBuffer(
        const D3D12_RESOURCE_DESC* pDesc,
        const D3D12_HEAP_PROPERTIES* pHeapProperties,
        ID3D11Buffer** ppOutBuffer);

    HRESULT CreateTranslatedTexture2D(
        const D3D12_RESOURCE_DESC* pDesc,
        const D3D12_HEAP_PROPERTIES* pHeapProperties,
        ID3D11Texture2D** ppOutTexture);

    // Pipeline State translation
    HRESULT CreateTranslatedPSO(
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc,
        TranslatedPipelineState& outPSO);

    HRESULT CreateTranslatedComputePSO(
        const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc,
        TranslatedPipelineState& outPSO);

    // Readback & Verification
    HRESULT ReadbackRenderTarget(
        ID3D12Resource* pD3D12Resource,
        std::vector<uint32_t>& outPixels,
        UINT& outWidth,
        UINT& outHeight);

    HRESULT ReadbackBuffer(
        ID3D12Resource* pD3D12Resource,
        void* pOutData,
        SIZE_T numBytes);

    // Swapchain & Presentation Interop
    HRESULT CreateSwapChainForHwnd(
        HWND hwnd,
        UINT width,
        UINT height,
        IDXGISwapChain** ppSwapChain);
    HRESULT PresentSwapChain(
        IDXGISwapChain* pSwapChain,
        UINT syncInterval,
        UINT flags);

    // Command replay & submission
    HRESULT ReplayCommandBatch(const CommandBatch& batch);
    HRESULT ExecuteSingleCommand(const RecordedCommand& cmd);
    void Flush();
    HRESULT WaitForGpuCompletion(DWORD timeoutMs = 2000);

    // Diagnostics & Statistics
    uint64_t GetTranslatedCommandsCount() const { return m_translatedCount.load(); }
    uint64_t GetRejectedCommandsCount() const { return m_rejectedCount.load(); }
    void ResetCommandStats() { m_translatedCount = 0; m_rejectedCount = 0; }

    uint64_t GetRootConstantMapCount() const { return m_rootConstantMapCount; }
    uint64_t GetRootConstantMapSkippedCount() const { return m_rootConstantMapSkippedCount; }
    void ResetRootConstantStats() { m_rootConstantMapCount = 0; m_rootConstantMapSkippedCount = 0; }

    struct ActiveUAVBinding {
        ID3D11UnorderedAccessView* pUAV = nullptr;
        ID3D12Resource* pD3D12Resource = nullptr;
        ID3D11Resource* pD3D11Resource = nullptr;
        UINT initialCount = static_cast<UINT>(-1);
    };

    ActiveUAVBinding GetActiveComputeUAV(UINT slot) const {
        return (slot < 8) ? m_activeComputeUAVs[slot] : ActiveUAVBinding{};
    }
    ActiveUAVBinding GetActiveGraphicsUAV(UINT slot) const {
        return (slot < 8) ? m_activeGraphicsUAVs[slot] : ActiveUAVBinding{};
    }

    void SetDescriptorIncrementSize(UINT size) { m_descriptorIncrementSize = size; }
    UINT GetDescriptorIncrementSize() const { return m_descriptorIncrementSize; }

    // PSO Burst Throttling & TDR Mitigation
    void NotifyPsoCreated();
    void OnPsoBurstCheckpoint();
    uint64_t GetPsoBurstCheckpointCount() const { return m_psoBurstCheckpointCount.load(); }
    void ResetPsoBurstStats() { m_psoBurstCount = 0; m_psoBurstCheckpointCount = 0; }

private:
    D3D11Backend() = default;
    ~D3D11Backend();
    D3D11Backend(const D3D11Backend&) = delete;
    D3D11Backend& operator=(const D3D11Backend&) = delete;

    void ResolveBarrierHazards(const RecordedCommand& cmd);
    HRESULT UpdateAndBindRootConstants(UINT slot, const std::vector<uint32_t>& constants, bool isCompute, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    std::mutex m_mutex;
    bool m_initialized = false;
    ID3D11Device* m_pDevice = nullptr;
    ID3D11DeviceContext* m_pContext = nullptr;
    ID3D11Query* m_pSyncQuery = nullptr;
    D3D_FEATURE_LEVEL m_featureLevel = D3D_FEATURE_LEVEL_11_0;

    ID3D12RootSignature* m_activeGraphicsRootSig = nullptr;
    ID3D12RootSignature* m_activeComputeRootSig = nullptr;
    UINT m_descriptorIncrementSize = 32;

    std::unordered_map<UINT, ID3D11Buffer*> m_graphicsConstantBuffers;
    std::unordered_map<UINT, ID3D11Buffer*> m_computeConstantBuffers;
    std::unordered_map<UINT, std::vector<uint32_t>> m_graphicsConstantShadow;
    std::unordered_map<UINT, std::vector<uint32_t>> m_computeConstantShadow;
    uint64_t m_rootConstantMapCount = 0;
    uint64_t m_rootConstantMapSkippedCount = 0;

    ActiveUAVBinding m_activeComputeUAVs[8] = {};
    ActiveUAVBinding m_activeGraphicsUAVs[8] = {};

    ID3D11RenderTargetView* m_boundRTVs[8] = { nullptr };
    UINT m_numBoundRTVs = 0;
    ID3D11DepthStencilView* m_boundDSV = nullptr;

    std::atomic<uint64_t> m_translatedCount{ 0 };
    std::atomic<uint64_t> m_rejectedCount{ 0 };

    std::atomic<uint32_t> m_psoBurstCount{ 0 };
    std::atomic<uint64_t> m_psoBurstCheckpointCount{ 0 };
    static constexpr uint32_t PSO_BURST_THROTTLE_THRESHOLD = 128;
};

} // namespace kepler
