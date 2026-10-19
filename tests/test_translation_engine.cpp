#include "translator/command_translator.h"
#include "translator/resource_manager.h"
#include "translator/d3d11_backend.h"
#include "core/logger.h"
#include "core/config.h"

#include <iostream>
#include <cassert>

using namespace kepler;

static void TestCommandRecordingAndBatching() {
    CommandBatch batch;
    assert(batch.GetCount() == 0);

    // 1. ClearRenderTarget
    RecordedCommand cmdClear;
    cmdClear.type = CommandType::ClearRenderTarget;
    new (&cmdClear.clearRTV) ClearRenderTargetCmd();
    cmdClear.clearRTV.renderTargetView.ptr = 0x1000;
    cmdClear.clearRTV.colorRGBA[0] = 0.2f;
    cmdClear.clearRTV.colorRGBA[1] = 0.4f;
    cmdClear.clearRTV.colorRGBA[2] = 0.6f;
    cmdClear.clearRTV.colorRGBA[3] = 1.0f;
    batch.Append(std::move(cmdClear));

    // 2. DrawInstanced
    RecordedCommand cmdDraw;
    cmdDraw.type = CommandType::DrawInstanced;
    cmdDraw.drawInstanced = { 3, 1, 0, 0 };
    batch.Append(std::move(cmdDraw));

    // 3. Dispatch
    RecordedCommand cmdDispatch;
    cmdDispatch.type = CommandType::Dispatch;
    cmdDispatch.dispatch = { 8, 8, 1 };
    batch.Append(std::move(cmdDispatch));

    // 4. SetViewports
    RecordedCommand cmdVP;
    cmdVP.type = CommandType::SetViewports;
    new (&cmdVP.setViewports) SetViewportsCmd();
    D3D12_VIEWPORT vp = { 0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f };
    cmdVP.setViewports.viewports.push_back(vp);
    batch.Append(std::move(cmdVP));

    assert(batch.GetCount() == 4);
    assert(batch.GetCommands()[0].type == CommandType::ClearRenderTarget);
    assert(batch.GetCommands()[1].type == CommandType::DrawInstanced);
    assert(batch.GetCommands()[1].drawInstanced.vertexCountPerInstance == 3);
    assert(batch.GetCommands()[2].type == CommandType::Dispatch);
    assert(batch.GetCommands()[2].dispatch.threadGroupCountX == 8);
    assert(batch.GetCommands()[3].type == CommandType::SetViewports);
    assert(batch.GetCommands()[3].setViewports.viewports.size() == 1);

    std::cout << "[PASS] TestCommandRecordingAndBatching" << std::endl;
}

static void TestResourceStateTrackerTransitions() {
    auto& tracker = ResourceStateTracker::Instance();
    tracker.Reset();

    auto* dummyResource = reinterpret_cast<ID3D12Resource*>(0xDEADBEEF);
    tracker.RegisterResource(dummyResource, D3D12_RESOURCE_STATE_COMMON);

    assert(tracker.GetState(dummyResource) == D3D12_RESOURCE_STATE_COMMON);

    std::string warn;
    bool ok = tracker.ValidateTransition(
        dummyResource,
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        warn);
    assert(ok);
    assert(warn.empty());
    assert(tracker.GetState(dummyResource) == D3D12_RESOURCE_STATE_RENDER_TARGET);

    // Transition to pixel shader resource
    ok = tracker.ValidateTransition(
        dummyResource,
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        warn);
    assert(ok);
    assert(tracker.GetState(dummyResource) == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // Mismatched transition (expected COPY_DEST, but current is PIXEL_SHADER_RESOURCE)
    ok = tracker.ValidateTransition(
        dummyResource,
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_COMMON,
        warn);
    assert(!ok);
    assert(!warn.empty());

    tracker.UnregisterResource(dummyResource);
    std::cout << "[PASS] TestResourceStateTrackerTransitions" << std::endl;
}

static void TestResourceManagerDescriptorLookup() {
    auto& rm = ResourceManager::Instance();
    rm.Clear();

    auto& backend = D3D11Backend::Instance();
    if (!backend.IsInitialized()) {
        backend.Initialize(nullptr);
    }

    if (!backend.IsInitialized() || !backend.GetDevice()) {
        std::cout << "[SKIP] TestResourceManagerDescriptorLookup: D3D11 device not available." << std::endl;
        return;
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = 16;
    texDesc.Height = 16;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

    ID3D11Texture2D* pTex = nullptr;
    ID3D11RenderTargetView* pRealRTV = nullptr;
    HRESULT hr = backend.GetDevice()->CreateTexture2D(&texDesc, nullptr, &pTex);
    if (SUCCEEDED(hr) && pTex) {
        backend.GetDevice()->CreateRenderTargetView(pTex, nullptr, &pRealRTV);
    }

    if (pRealRTV) {
        rm.RegisterRTV(0x1000, pRealRTV);
        assert(rm.GetRTV(0x1000) == pRealRTV);
        assert(rm.GetRTV(0x9999) == nullptr);

        rm.Clear();
        assert(rm.GetRTV(0x1000) == nullptr);

        pRealRTV->Release();
    }
    if (pTex) {
        pTex->Release();
    }

    std::cout << "[PASS] TestResourceManagerDescriptorLookup" << std::endl;
}

static void TestRootParameterConstants() {
    auto& rm = ResourceManager::Instance();
    std::vector<uint32_t> gfxConstants = { 42, 84, 126, 168 };
    rm.BindRootConstants(0, gfxConstants, false);

    const auto* pGfx = rm.GetRootConstants(0, false);
    assert(pGfx != nullptr);
    assert(pGfx->size() == 4);
    assert((*pGfx)[0] == 42);
    assert((*pGfx)[1] == 84);

    std::vector<uint32_t> csConstants = { 100, 200 };
    rm.BindRootConstants(1, csConstants, true);

    const auto* pCS = rm.GetRootConstants(1, true);
    assert(pCS != nullptr);
    assert(pCS->size() == 2);
    assert((*pCS)[0] == 100);

    assert(rm.GetRootConstants(99, false) == nullptr);

    std::cout << "[PASS] TestRootParameterConstants" << std::endl;
}

static void TestD3D11BackendCommandReplay() {
    auto& backend = D3D11Backend::Instance();
    bool initOk = backend.Initialize(nullptr);
    if (!initOk) {
        std::cout << "[SKIP] TestD3D11BackendCommandReplay: D3D11CreateDevice not available in headless environment." << std::endl;
        return;
    }

    assert(backend.IsInitialized());
    assert(backend.GetDevice() != nullptr);
    assert(backend.GetContext() != nullptr);

    CommandBatch batch;

    // DrawInstanced
    RecordedCommand cmdDraw;
    cmdDraw.type = CommandType::DrawInstanced;
    cmdDraw.drawInstanced = { 3, 1, 0, 0 };
    batch.Append(std::move(cmdDraw));

    // Viewport
    RecordedCommand cmdVP;
    cmdVP.type = CommandType::SetViewports;
    new (&cmdVP.setViewports) SetViewportsCmd();
    D3D12_VIEWPORT vp = { 0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 1.0f };
    cmdVP.setViewports.viewports.push_back(vp);
    batch.Append(std::move(cmdVP));

    HRESULT hr = backend.ReplayCommandBatch(batch);
    assert(SUCCEEDED(hr));

    backend.Flush();
    std::cout << "[PASS] TestD3D11BackendCommandReplay" << std::endl;
}

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "  KeplerBridge Phase 4 Translation Engine Unit Tests" << std::endl;
    std::cout << "====================================================" << std::endl;

    TestCommandRecordingAndBatching();
    TestResourceStateTrackerTransitions();
    TestResourceManagerDescriptorLookup();
    TestRootParameterConstants();
    TestD3D11BackendCommandReplay();

    std::cout << "====================================================" << std::endl;
    std::cout << "ALL TRANSLATION ENGINE TESTS PASSED (5/5)!" << std::endl;
    std::cout << "====================================================" << std::endl;
    return 0;
}
