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

namespace kepler {

class CommandBatch;
struct RecordedCommand;

class D3D11Backend {
public:
    static D3D11Backend& Instance();

    bool Initialize(IDXGIAdapter* pAdapter = nullptr);
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }

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

    // Command replay & submission
    HRESULT ReplayCommandBatch(const CommandBatch& batch);
    HRESULT ExecuteSingleCommand(const RecordedCommand& cmd);
    void Flush();

private:
    D3D11Backend() = default;
    ~D3D11Backend();
    D3D11Backend(const D3D11Backend&) = delete;
    D3D11Backend& operator=(const D3D11Backend&) = delete;

    void ResolveBarrierHazards(const RecordedCommand& cmd);

    std::mutex m_mutex;
    bool m_initialized = false;
    ID3D11Device* m_pDevice = nullptr;
    ID3D11DeviceContext* m_pContext = nullptr;
    D3D_FEATURE_LEVEL m_featureLevel = D3D_FEATURE_LEVEL_11_0;
};

} // namespace kepler
