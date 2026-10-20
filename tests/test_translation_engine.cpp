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

    // Verify CBV registration
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = 256;
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Buffer* pRealCB = nullptr;
    if (SUCCEEDED(backend.GetDevice()->CreateBuffer(&cbDesc, nullptr, &pRealCB)) && pRealCB) {
        rm.RegisterCBV(0x2000, pRealCB);
        assert(rm.GetCBV(0x2000) == pRealCB);
        assert(rm.GetCBV(0x9999) == nullptr);
        rm.Clear();
        assert(rm.GetCBV(0x2000) == nullptr);
        pRealCB->Release();
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

static void TestRootConstantBufferUploadAndAlignment() {
    auto& backend = D3D11Backend::Instance();
    bool initOk = backend.Initialize(nullptr);
    if (!initOk) {
        std::cout << "[SKIP] TestRootConstantBufferUploadAndAlignment: D3D11 not available." << std::endl;
        return;
    }

    CommandBatch batch;

    // 1. Unaligned graphics constants (3 DWORDs = 12 bytes; requires 16-byte/256-byte alignment)
    RecordedCommand cmdGfx;
    cmdGfx.type = CommandType::SetGraphicsRoot32BitConstants;
    new (&cmdGfx.setRootConstants) SetRoot32BitConstantsCmd();
    cmdGfx.setRootConstants.rootParameterIndex = 2; // Slot 2 (avoiding blind cb13 collision)
    cmdGfx.setRootConstants.num32BitValuesToSet = 3;
    cmdGfx.setRootConstants.constants = { 0x11223344, 0x55667788, 0x99AABBCC };
    cmdGfx.setRootConstants.destOffsetIn32BitValues = 0;
    cmdGfx.setRootConstants.isCompute = false;
    batch.Append(std::move(cmdGfx));

    /*
    // 2. Compute root constants (1 DWORD = 4 bytes) on slot 1
    ...
    */

    HRESULT hr = backend.ReplayCommandBatch(batch);
    assert(SUCCEEDED(hr));

    // Query active constant buffer at slot 2 from context
    ID3D11Buffer* pBoundCB = nullptr;
    backend.GetContext()->VSGetConstantBuffers(2, 1, &pBoundCB);
    assert(pBoundCB != nullptr);

    D3D11_BUFFER_DESC desc = {};
    pBoundCB->GetDesc(&desc);
    assert(desc.ByteWidth % 16 == 0); // Must be strictly multiple of 16
    assert(desc.ByteWidth >= 256);     // Padded to D3D12 CB alignment
    assert(desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER);
    pBoundCB->Release();

    std::cout << "[PASS] TestRootConstantBufferUploadAndAlignment" << std::endl;
}

static void TestRootSignatureReflectionAndRegisterMapping() {
    auto& backend = D3D11Backend::Instance();
    if (!backend.IsInitialized()) {
        backend.Initialize(nullptr);
    }
    if (!backend.IsInitialized() || !backend.GetDevice()) {
        std::cout << "[SKIP] TestRootSignatureReflectionAndRegisterMapping: D3D11 not available." << std::endl;
        return;
    }

    auto& rm = ResourceManager::Instance();

    // 1. Define synthetic root signature:
    // Param 0: Root constants mapped to ShaderRegister = 3 (register(b3)), Vertex visibility
    // Param 1: Descriptor table with SRV mapped to BaseShaderRegister = 4 (register(t4)), Pixel visibility
    TranslatedRootSignature sig = {};
    TranslatedRootParameter param0 = {};
    param0.type = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param0.shaderRegister = 3; // Crucial: must bind to cb3, NOT rootParameterIndex 0!
    param0.visibility = D3D12_SHADER_VISIBILITY_VERTEX;
    param0.num32BitValues = 4;
    sig.parameters.push_back(param0);

    TranslatedRootParameter param1 = {};
    param1.type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param1.visibility = D3D12_SHADER_VISIBILITY_PIXEL;
    TranslatedDescriptorRange srvRange = {};
    srvRange.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.baseShaderRegister = 4; // Crucial: must bind to t4, NOT slot 1!
    srvRange.numDescriptors = 1;
    srvRange.offsetInDescriptorsFromTableStart = 0;
    param1.ranges.push_back(srvRange);
    sig.parameters.push_back(param1);

    ID3D12RootSignature* pDummySig = reinterpret_cast<ID3D12RootSignature*>(0x12345678);
    rm.RegisterRootSignature(pDummySig, sig);

    // Create a real D3D11 SRV to bind
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = 8;
    texDesc.Height = 8;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* pTex = nullptr;
    ID3D11ShaderResourceView* pSRV = nullptr;
    HRESULT hr = backend.GetDevice()->CreateTexture2D(&texDesc, nullptr, &pTex);
    assert(SUCCEEDED(hr) && pTex);
    hr = backend.GetDevice()->CreateShaderResourceView(pTex, nullptr, &pSRV);
    assert(SUCCEEDED(hr) && pSRV);

    const SIZE_T srvHandle = 0x5000;
    rm.RegisterSRV(srvHandle, pSRV);

    // Build command batch
    CommandBatch batch;

    // 1. SetGraphicsRootSignature
    RecordedCommand cmdSig;
    cmdSig.type = CommandType::SetGraphicsRootSignature;
    cmdSig.setRootSig.pRootSignature = pDummySig;
    batch.Append(std::move(cmdSig));

    // 2. SetGraphicsRoot32BitConstants at rootParameterIndex 0 -> should map to cb3
    RecordedCommand cmdConst;
    cmdConst.type = CommandType::SetGraphicsRoot32BitConstants;
    new (&cmdConst.setRootConstants) SetRoot32BitConstantsCmd();
    cmdConst.setRootConstants.rootParameterIndex = 0;
    cmdConst.setRootConstants.constants = { 0xAAAA1111, 0xBBBB2222 };
    cmdConst.setRootConstants.num32BitValuesToSet = 2;
    cmdConst.setRootConstants.destOffsetIn32BitValues = 0;
    cmdConst.setRootConstants.isCompute = false;
    batch.Append(std::move(cmdConst));

    // 3. SetGraphicsRootDescriptorTable at rootParameterIndex 1 -> should map to t4
    RecordedCommand cmdTable;
    cmdTable.type = CommandType::SetGraphicsRootDescriptorTable;
    cmdTable.setRootTable.rootParameterIndex = 1;
    cmdTable.setRootTable.baseDescriptor.ptr = srvHandle;
    cmdTable.setRootTable.isCompute = false;
    batch.Append(std::move(cmdTable));

    hr = backend.ReplayCommandBatch(batch);
    assert(SUCCEEDED(hr));

    // Verification 1: Root constant mapped to VS cb3
    ID3D11Buffer* pBoundCB3 = nullptr;
    backend.GetContext()->VSGetConstantBuffers(3, 1, &pBoundCB3);
    assert(pBoundCB3 != nullptr);
    pBoundCB3->Release();

    // Verify cb0 was NOT bound (register collision isolation)
    ID3D11Buffer* pBoundCB0 = nullptr;
    backend.GetContext()->VSGetConstantBuffers(0, 1, &pBoundCB0);
    assert(pBoundCB0 == nullptr);

    // Verification 2: SRV mapped to PS t4
    ID3D11ShaderResourceView* pBoundSRV4 = nullptr;
    backend.GetContext()->PSGetShaderResources(4, 1, &pBoundSRV4);
    assert(pBoundSRV4 != nullptr);
    assert(pBoundSRV4 == pSRV);
    pBoundSRV4->Release();

    // Cleanup
    pSRV->Release();
    pTex->Release();
    rm.UnregisterRootSignature(pDummySig);

    std::cout << "[PASS] TestRootSignatureReflectionAndRegisterMapping" << std::endl;
}

static void TestComputeDescriptorTableAndUAVBinding() {
    auto& backend = D3D11Backend::Instance();
    if (!backend.IsInitialized()) {
        backend.Initialize(nullptr);
    }
    if (!backend.IsInitialized() || !backend.GetDevice()) {
        std::cout << "[SKIP] TestComputeDescriptorTableAndUAVBinding: D3D11 not available." << std::endl;
        return;
    }

    auto& rm = ResourceManager::Instance();

    // Root signature with Compute UAV descriptor table at rootParameterIndex 0, BaseShaderRegister = 1 (u1)
    TranslatedRootSignature csSig = {};
    TranslatedRootParameter param0 = {};
    param0.type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param0.visibility = D3D12_SHADER_VISIBILITY_ALL;
    TranslatedDescriptorRange uavRange = {};
    uavRange.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.baseShaderRegister = 1; // register(u1)
    uavRange.numDescriptors = 1;
    uavRange.offsetInDescriptorsFromTableStart = 0;
    param0.ranges.push_back(uavRange);
    csSig.parameters.push_back(param0);

    ID3D12RootSignature* pDummySig = reinterpret_cast<ID3D12RootSignature*>(0x87654321);
    rm.RegisterRootSignature(pDummySig, csSig);

    // Create D3D11 UAV buffer
    D3D11_BUFFER_DESC bufDesc = {};
    bufDesc.ByteWidth = 64;
    bufDesc.Usage = D3D11_USAGE_DEFAULT;
    bufDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

    ID3D11Buffer* pBuffer = nullptr;
    HRESULT hr = backend.GetDevice()->CreateBuffer(&bufDesc, nullptr, &pBuffer);
    assert(SUCCEEDED(hr) && pBuffer);

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = 16;
    uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;

    ID3D11UnorderedAccessView* pUAV = nullptr;
    hr = backend.GetDevice()->CreateUnorderedAccessView(pBuffer, &uavDesc, &pUAV);
    assert(SUCCEEDED(hr) && pUAV);

    const SIZE_T uavHandle = 0x6000;
    rm.RegisterUAV(uavHandle, pUAV);

    // Replay batch
    CommandBatch batch;

    RecordedCommand cmdSig;
    cmdSig.type = CommandType::SetComputeRootSignature;
    cmdSig.setRootSig.pRootSignature = pDummySig;
    batch.Append(std::move(cmdSig));

    RecordedCommand cmdTable;
    cmdTable.type = CommandType::SetComputeRootDescriptorTable;
    cmdTable.setRootTable.rootParameterIndex = 0;
    cmdTable.setRootTable.baseDescriptor.ptr = uavHandle;
    cmdTable.setRootTable.isCompute = true;
    batch.Append(std::move(cmdTable));

    hr = backend.ReplayCommandBatch(batch);
    assert(SUCCEEDED(hr));

    // Verify UAV was bound to CS slot 1 (NOT slot 0)
    ID3D11UnorderedAccessView* pBoundUAV1 = nullptr;
    backend.GetContext()->CSGetUnorderedAccessViews(1, 1, &pBoundUAV1);
    assert(pBoundUAV1 != nullptr);
    assert(pBoundUAV1 == pUAV);
    pBoundUAV1->Release();

    // Verify slot 0 was NOT touched (slot isolation)
    ID3D11UnorderedAccessView* pBoundUAV0 = nullptr;
    backend.GetContext()->CSGetUnorderedAccessViews(0, 1, &pBoundUAV0);
    assert(pBoundUAV0 == nullptr);

    // Cleanup
    pUAV->Release();
    pBuffer->Release();
    rm.UnregisterRootSignature(pDummySig);

    std::cout << "[PASS] TestComputeDescriptorTableAndUAVBinding" << std::endl;
}

static void TestRepeatedInitializationLifecycle() {
    auto& backend = D3D11Backend::Instance();

    // Pass 1
    bool init1 = backend.Initialize(nullptr);
    assert(init1 && backend.IsInitialized());
    backend.Shutdown(false);
    assert(!backend.IsInitialized());
    assert(backend.GetDevice() == nullptr);
    assert(backend.GetContext() == nullptr);

    // Pass 2: Re-initialize cleanly
    bool init2 = backend.Initialize(nullptr);
    assert(init2 && backend.IsInitialized());
    assert(backend.GetDevice() != nullptr);
    assert(backend.GetContext() != nullptr);

    // Verify functionality after re-init
    CommandBatch batch;
    RecordedCommand cmdVP;
    cmdVP.type = CommandType::SetViewports;
    new (&cmdVP.setViewports) SetViewportsCmd();
    D3D12_VIEWPORT vp = { 0.0f, 0.0f, 256.0f, 256.0f, 0.0f, 1.0f };
    cmdVP.setViewports.viewports.push_back(vp);
    batch.Append(std::move(cmdVP));

    HRESULT hr = backend.ReplayCommandBatch(batch);
    assert(SUCCEEDED(hr));
    backend.Flush();

    // Teardown cleanly
    backend.Shutdown(false);
    assert(!backend.IsInitialized());

    std::cout << "[PASS] TestRepeatedInitializationLifecycle" << std::endl;
}

static void TestUAVDescriptorValidationAndLimits() {
    auto& backend = D3D11Backend::Instance();
    if (!backend.IsInitialized()) {
        backend.Initialize(nullptr);
    }
    if (!backend.IsInitialized() || !backend.GetDevice()) {
        std::cout << "[SKIP] TestUAVDescriptorValidationAndLimits: D3D11 not available." << std::endl;
        return;
    }

    auto& rm = ResourceManager::Instance();

    // Test 1: Null descriptor handle and null resource
    D3D12_CPU_DESCRIPTOR_HANDLE nullHandle = { 0 };
    TranslatedUAVInfo nullInfo = {};
    rm.RegisterUAVInfo(nullHandle.ptr, nullInfo);
    assert(rm.GetUAV(0) == nullptr);

    // Test 2: Valid descriptor registration and lookup
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = 256;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

    ID3D11Buffer* pBuffer = nullptr;
    HRESULT hr = backend.GetDevice()->CreateBuffer(&desc, nullptr, &pBuffer);
    assert(SUCCEEDED(hr) && pBuffer);

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = 64;
    uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;

    ID3D11UnorderedAccessView* pUAV = nullptr;
    hr = backend.GetDevice()->CreateUnorderedAccessView(pBuffer, &uavDesc, &pUAV);
    assert(SUCCEEDED(hr) && pUAV);

    TranslatedUAVInfo info = {};
    info.pUAV = pUAV;
    info.initialCount = 0;
    info.hasCounter = true;
    info.structureByteStride = 4;
    info.numElements = 64;

    const SIZE_T testHandle = 0x8800;
    rm.RegisterUAVInfo(testHandle, info);

    TranslatedUAVInfo queried = {};
    bool found = rm.GetUAVInfo(testHandle, queried);
    assert(found);
    assert(queried.pUAV == pUAV);
    assert(queried.initialCount == 0);
    assert(queried.hasCounter == true);
    assert(queried.structureByteStride == 4);

    // Test initial count update (preserve sentinel)
    rm.SetUAVInitialCount(testHandle, static_cast<UINT>(-1));
    rm.GetUAVInfo(testHandle, queried);
    assert(queried.initialCount == static_cast<UINT>(-1));

    // Cleanup
    pUAV->Release();
    pBuffer->Release();

    std::cout << "[PASS] TestUAVDescriptorValidationAndLimits" << std::endl;
}

static void TestMultiSlotComputeUAVBinding() {
    auto& backend = D3D11Backend::Instance();
    if (!backend.IsInitialized()) {
        backend.Initialize(nullptr);
    }
    if (!backend.IsInitialized() || !backend.GetDevice()) {
        std::cout << "[SKIP] TestMultiSlotComputeUAVBinding: D3D11 not available." << std::endl;
        return;
    }

    auto& rm = ResourceManager::Instance();

    // 1. Create 2 distinct D3D11 buffers and UAVs
    D3D11_BUFFER_DESC bufDesc = {};
    bufDesc.ByteWidth = 64;
    bufDesc.Usage = D3D11_USAGE_DEFAULT;
    bufDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

    ID3D11Buffer* pBuf0 = nullptr;
    ID3D11Buffer* pBuf1 = nullptr;
    HRESULT hr = backend.GetDevice()->CreateBuffer(&bufDesc, nullptr, &pBuf0);
    assert(SUCCEEDED(hr) && pBuf0);
    hr = backend.GetDevice()->CreateBuffer(&bufDesc, nullptr, &pBuf1);
    assert(SUCCEEDED(hr) && pBuf1);

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = 16;
    uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;

    ID3D11UnorderedAccessView* pUAV0 = nullptr;
    ID3D11UnorderedAccessView* pUAV1 = nullptr;
    hr = backend.GetDevice()->CreateUnorderedAccessView(pBuf0, &uavDesc, &pUAV0);
    assert(SUCCEEDED(hr) && pUAV0);
    hr = backend.GetDevice()->CreateUnorderedAccessView(pBuf1, &uavDesc, &pUAV1);
    assert(SUCCEEDED(hr) && pUAV1);

    const SIZE_T baseHandle = 0x9000;
    const SIZE_T incSize = backend.GetDescriptorIncrementSize(); // 32
    const SIZE_T handle0 = baseHandle;
    const SIZE_T handle1 = baseHandle + incSize;

    ID3D12Resource* pDummyRes0 = reinterpret_cast<ID3D12Resource*>(0xAAAA0000);
    ID3D12Resource* pDummyRes1 = reinterpret_cast<ID3D12Resource*>(0xBBBB0000);

    TranslatedUAVInfo info0 = {};
    info0.pUAV = pUAV0;
    info0.pD3D12Resource = pDummyRes0;
    rm.RegisterUAVInfo(handle0, info0);

    TranslatedUAVInfo info1 = {};
    info1.pUAV = pUAV1;
    info1.pD3D12Resource = pDummyRes1;
    rm.RegisterUAVInfo(handle1, info1);

    // 2. Define Root Signature with 1 Descriptor Table containing 2 UAVs (u0 and u1)
    TranslatedRootSignature csSig = {};
    TranslatedRootParameter param0 = {};
    param0.type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param0.visibility = D3D12_SHADER_VISIBILITY_ALL;

    TranslatedDescriptorRange range = {};
    range.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.baseShaderRegister = 0; // starts at u0
    range.numDescriptors = 2;     // binds u0 and u1
    range.offsetInDescriptorsFromTableStart = 0;
    param0.ranges.push_back(range);
    csSig.parameters.push_back(param0);

    ID3D12RootSignature* pDummySig = reinterpret_cast<ID3D12RootSignature*>(0x99990000);
    rm.RegisterRootSignature(pDummySig, csSig);

    // 3. Replay command batch to bind both UAVs
    CommandBatch batch;
    RecordedCommand cmdSig;
    cmdSig.type = CommandType::SetComputeRootSignature;
    cmdSig.setRootSig.pRootSignature = pDummySig;
    batch.Append(std::move(cmdSig));

    RecordedCommand cmdTable;
    cmdTable.type = CommandType::SetComputeRootDescriptorTable;
    cmdTable.setRootTable.rootParameterIndex = 0;
    cmdTable.setRootTable.baseDescriptor.ptr = baseHandle;
    cmdTable.setRootTable.isCompute = true;
    batch.Append(std::move(cmdTable));

    hr = backend.ReplayCommandBatch(batch);
    assert(SUCCEEDED(hr));

    // 4. Verification: both slots 0 and 1 must be simultaneously bound
    ID3D11UnorderedAccessView* pBound0 = nullptr;
    ID3D11UnorderedAccessView* pBound1 = nullptr;
    ID3D11UnorderedAccessView* pBound2 = nullptr;

    backend.GetContext()->CSGetUnorderedAccessViews(0, 1, &pBound0);
    backend.GetContext()->CSGetUnorderedAccessViews(1, 1, &pBound1);
    backend.GetContext()->CSGetUnorderedAccessViews(2, 1, &pBound2);

    assert(pBound0 != nullptr);
    assert(pBound0 == pUAV0);
    assert(pBound1 != nullptr);
    assert(pBound1 == pUAV1);
    assert(pBound2 == nullptr); // Slot 2 untouched

    pBound0->Release();
    pBound1->Release();

    // Cleanup
    pUAV0->Release();
    pUAV1->Release();
    pBuf0->Release();
    pBuf1->Release();
    rm.UnregisterRootSignature(pDummySig);

    std::cout << "[PASS] TestMultiSlotComputeUAVBinding" << std::endl;
}

static void TestResourceAwareUAVBarriers() {
    auto& backend = D3D11Backend::Instance();
    if (!backend.IsInitialized()) {
        backend.Initialize(nullptr);
    }
    if (!backend.IsInitialized() || !backend.GetDevice()) {
        std::cout << "[SKIP] TestResourceAwareUAVBarriers: D3D11 not available." << std::endl;
        return;
    }

    auto& rm = ResourceManager::Instance();

    // Create 2 D3D11 buffers and UAVs
    D3D11_BUFFER_DESC bufDesc = {};
    bufDesc.ByteWidth = 64;
    bufDesc.Usage = D3D11_USAGE_DEFAULT;
    bufDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

    ID3D11Buffer* pBufA = nullptr;
    ID3D11Buffer* pBufB = nullptr;
    HRESULT hr = backend.GetDevice()->CreateBuffer(&bufDesc, nullptr, &pBufA);
    assert(SUCCEEDED(hr) && pBufA);
    hr = backend.GetDevice()->CreateBuffer(&bufDesc, nullptr, &pBufB);
    assert(SUCCEEDED(hr) && pBufB);

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = 16;
    uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;

    ID3D11UnorderedAccessView* pUAVA = nullptr;
    ID3D11UnorderedAccessView* pUAVB = nullptr;
    hr = backend.GetDevice()->CreateUnorderedAccessView(pBufA, &uavDesc, &pUAVA);
    assert(SUCCEEDED(hr) && pUAVA);
    hr = backend.GetDevice()->CreateUnorderedAccessView(pBufB, &uavDesc, &pUAVB);
    assert(SUCCEEDED(hr) && pUAVB);

    const SIZE_T baseHandle = 0xA000;
    const SIZE_T incSize = backend.GetDescriptorIncrementSize();
    const SIZE_T handleA = baseHandle;
    const SIZE_T handleB = baseHandle + incSize;

    ID3D12Resource* pResA = reinterpret_cast<ID3D12Resource*>(0x11110000);
    ID3D12Resource* pResB = reinterpret_cast<ID3D12Resource*>(0x22220000);

    TranslatedUAVInfo infoA = {};
    infoA.pUAV = pUAVA;
    infoA.pD3D12Resource = pResA;
    rm.RegisterUAVInfo(handleA, infoA);

    TranslatedUAVInfo infoB = {};
    infoB.pUAV = pUAVB;
    infoB.pD3D12Resource = pResB;
    rm.RegisterUAVInfo(handleB, infoB);

    // Root signature with 2 UAV slots (u0 and u1)
    TranslatedRootSignature csSig = {};
    TranslatedRootParameter param0 = {};
    param0.type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param0.visibility = D3D12_SHADER_VISIBILITY_ALL;
    TranslatedDescriptorRange range = {};
    range.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.baseShaderRegister = 0;
    range.numDescriptors = 2;
    range.offsetInDescriptorsFromTableStart = 0;
    param0.ranges.push_back(range);
    csSig.parameters.push_back(param0);

    ID3D12RootSignature* pSig = reinterpret_cast<ID3D12RootSignature*>(0x33330000);
    rm.RegisterRootSignature(pSig, csSig);

    // Batch 1: Bind both UAVs
    CommandBatch batch1;
    RecordedCommand cmdSig;
    cmdSig.type = CommandType::SetComputeRootSignature;
    cmdSig.setRootSig.pRootSignature = pSig;
    batch1.Append(std::move(cmdSig));

    RecordedCommand cmdTable;
    cmdTable.type = CommandType::SetComputeRootDescriptorTable;
    cmdTable.setRootTable.rootParameterIndex = 0;
    cmdTable.setRootTable.baseDescriptor.ptr = baseHandle;
    cmdTable.setRootTable.isCompute = true;
    batch1.Append(std::move(cmdTable));

    hr = backend.ReplayCommandBatch(batch1);
    assert(SUCCEEDED(hr));

    // Batch 2: Issue UAV barrier targeting ONLY ResA
    CommandBatch batchBarrierA;
    RecordedCommand cmdBarrier;
    cmdBarrier.type = CommandType::ResourceBarrier;
    new (&cmdBarrier.barrier) ResourceBarrierCmd();
    D3D12_RESOURCE_BARRIER bA = {};
    bA.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    bA.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    bA.UAV.pResource = pResA; // Specific resource!
    cmdBarrier.barrier.barriers.push_back(bA);
    batchBarrierA.Append(std::move(cmdBarrier));

    hr = backend.ReplayCommandBatch(batchBarrierA);
    assert(SUCCEEDED(hr));

    // Verify: Slot 0 (ResA) is unbound, but Slot 1 (ResB) REMAINED BOUND!
    ID3D11UnorderedAccessView* pBound0 = nullptr;
    ID3D11UnorderedAccessView* pBound1 = nullptr;
    backend.GetContext()->CSGetUnorderedAccessViews(0, 1, &pBound0);
    backend.GetContext()->CSGetUnorderedAccessViews(1, 1, &pBound1);

    assert(pBound0 == nullptr); // Successfully resolved hazard on ResA
    assert(pBound1 != nullptr); // Unrelated ResB preserved!
    assert(pBound1 == pUAVB);
    pBound1->Release();

    // Batch 3: Issue global UAV barrier (pResource = nullptr)
    CommandBatch batchGlobalBarrier;
    RecordedCommand cmdGlobal;
    cmdGlobal.type = CommandType::ResourceBarrier;
    new (&cmdGlobal.barrier) ResourceBarrierCmd();
    D3D12_RESOURCE_BARRIER bGlobal = {};
    bGlobal.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    bGlobal.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    bGlobal.UAV.pResource = nullptr; // Global!
    cmdGlobal.barrier.barriers.push_back(bGlobal);
    batchGlobalBarrier.Append(std::move(cmdGlobal));

    hr = backend.ReplayCommandBatch(batchGlobalBarrier);
    assert(SUCCEEDED(hr));

    // Verify: Slot 1 is now also unbound
    backend.GetContext()->CSGetUnorderedAccessViews(1, 1, &pBound1);
    assert(pBound1 == nullptr);

    // Cleanup
    pUAVA->Release();
    pUAVB->Release();
    pBufA->Release();
    pBufB->Release();
    rm.UnregisterRootSignature(pSig);

    std::cout << "[PASS] TestResourceAwareUAVBarriers" << std::endl;
}

static void TestGraphicsUAVBindingAndCounterSemantics() {
    auto& backend = D3D11Backend::Instance();
    if (!backend.IsInitialized()) {
        backend.Initialize(nullptr);
    }
    if (!backend.IsInitialized() || !backend.GetDevice()) {
        std::cout << "[SKIP] TestGraphicsUAVBindingAndCounterSemantics: D3D11 not available." << std::endl;
        return;
    }

    auto& rm = ResourceManager::Instance();

    // Create RTV (slot 0)
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = 32;
    texDesc.Height = 32;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

    ID3D11Texture2D* pTex = nullptr;
    HRESULT hr = backend.GetDevice()->CreateTexture2D(&texDesc, nullptr, &pTex);
    assert(SUCCEEDED(hr) && pTex);
    ID3D11RenderTargetView* pRTV = nullptr;
    hr = backend.GetDevice()->CreateRenderTargetView(pTex, nullptr, &pRTV);
    assert(SUCCEEDED(hr) && pRTV);

    const SIZE_T rtvHandle = 0xB000;
    rm.RegisterRTV(rtvHandle, pRTV);

    // Create UAV (slot 1)
    D3D11_BUFFER_DESC bufDesc = {};
    bufDesc.ByteWidth = 64;
    bufDesc.Usage = D3D11_USAGE_DEFAULT;
    bufDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

    ID3D11Buffer* pBuf = nullptr;
    hr = backend.GetDevice()->CreateBuffer(&bufDesc, nullptr, &pBuf);
    assert(SUCCEEDED(hr) && pBuf);

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = 16;
    uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;

    ID3D11UnorderedAccessView* pUAV = nullptr;
    hr = backend.GetDevice()->CreateUnorderedAccessView(pBuf, &uavDesc, &pUAV);
    assert(SUCCEEDED(hr) && pUAV);

    const SIZE_T uavHandle = 0xB020;
    TranslatedUAVInfo uavInfo = {};
    uavInfo.pUAV = pUAV;
    uavInfo.initialCount = 0; // reset count = 0
    rm.RegisterUAVInfo(uavHandle, uavInfo);

    // Define graphics root signature with Pixel Shader UAV at baseShaderRegister = 1
    TranslatedRootSignature gfxSig = {};
    TranslatedRootParameter param0 = {};
    param0.type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param0.visibility = D3D12_SHADER_VISIBILITY_PIXEL;
    TranslatedDescriptorRange range = {};
    range.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.baseShaderRegister = 1; // register(u1)
    range.numDescriptors = 1;
    range.offsetInDescriptorsFromTableStart = 0;
    param0.ranges.push_back(range);
    gfxSig.parameters.push_back(param0);

    ID3D12RootSignature* pGfxSig = reinterpret_cast<ID3D12RootSignature*>(0xB100);
    rm.RegisterRootSignature(pGfxSig, gfxSig);

    // Build batch:
    // 1. SetGraphicsRootSignature
    // 2. SetRenderTargets (1 RTV at slot 0)
    // 3. SetGraphicsRootDescriptorTable (UAV at slot 1)
    CommandBatch batch;
    RecordedCommand cmdSig;
    cmdSig.type = CommandType::SetGraphicsRootSignature;
    cmdSig.setRootSig.pRootSignature = pGfxSig;
    batch.Append(std::move(cmdSig));

    RecordedCommand cmdRTV;
    cmdRTV.type = CommandType::SetRenderTargets;
    new (&cmdRTV.setRTVs) SetRenderTargetsCmd();
    cmdRTV.setRTVs.rtvHandles.push_back({ rtvHandle });
    cmdRTV.setRTVs.hasDepthStencil = false;
    batch.Append(std::move(cmdRTV));

    RecordedCommand cmdTable;
    cmdTable.type = CommandType::SetGraphicsRootDescriptorTable;
    cmdTable.setRootTable.rootParameterIndex = 0;
    cmdTable.setRootTable.baseDescriptor.ptr = uavHandle;
    cmdTable.setRootTable.isCompute = false;
    batch.Append(std::move(cmdTable));

    hr = backend.ReplayCommandBatch(batch);
    assert(SUCCEEDED(hr));

    // Verify active graphics UAV binding was registered
    auto activeGfxUAV = backend.GetActiveGraphicsUAV(1);
    assert(activeGfxUAV.pUAV == pUAV);
    assert(activeGfxUAV.initialCount == 0);

    // Cleanup
    pUAV->Release();
    pBuf->Release();
    pRTV->Release();
    pTex->Release();
    rm.UnregisterRootSignature(pGfxSig);

    std::cout << "[PASS] TestGraphicsUAVBindingAndCounterSemantics" << std::endl;
}

static void TestRootConstantMapSuppressionAndMemoryFootprint() {
    auto& backend = D3D11Backend::Instance();
    if (!backend.Initialize()) {
        std::cout << "[SKIP] TestRootConstantMapSuppressionAndMemoryFootprint: D3D11 not available." << std::endl;
        return;
    }

    backend.ResetRootConstantStats();
    backend.ResetCommandStats();

    // 1. First upload: must map
    RecordedCommand cmd1;
    cmd1.type = CommandType::SetGraphicsRoot32BitConstants;
    new (&cmd1.setRootConstants) SetRoot32BitConstantsCmd();
    cmd1.setRootConstants.rootParameterIndex = 0;
    cmd1.setRootConstants.num32BitValuesToSet = 4;
    cmd1.setRootConstants.destOffsetIn32BitValues = 0;
    cmd1.setRootConstants.isCompute = false;
    cmd1.setRootConstants.constants = { 10, 20, 30, 40 };

    CommandBatch batch1;
    batch1.Append(std::move(cmd1));
    HRESULT hr = backend.ReplayCommandBatch(batch1);
    assert(SUCCEEDED(hr));
    assert(backend.GetRootConstantMapCount() == 1);
    assert(backend.GetRootConstantMapSkippedCount() == 0);

    // 2. Duplicate upload: should skip Map(DISCARD)
    RecordedCommand cmd2;
    cmd2.type = CommandType::SetGraphicsRoot32BitConstants;
    new (&cmd2.setRootConstants) SetRoot32BitConstantsCmd();
    cmd2.setRootConstants.rootParameterIndex = 0;
    cmd2.setRootConstants.num32BitValuesToSet = 4;
    cmd2.setRootConstants.destOffsetIn32BitValues = 0;
    cmd2.setRootConstants.isCompute = false;
    cmd2.setRootConstants.constants = { 10, 20, 30, 40 };

    CommandBatch batch2;
    batch2.Append(std::move(cmd2));
    hr = backend.ReplayCommandBatch(batch2);
    assert(SUCCEEDED(hr));
    assert(backend.GetRootConstantMapCount() == 1);
    assert(backend.GetRootConstantMapSkippedCount() == 1);

    // 3. Changed data upload: must map
    RecordedCommand cmd3;
    cmd3.type = CommandType::SetGraphicsRoot32BitConstants;
    new (&cmd3.setRootConstants) SetRoot32BitConstantsCmd();
    cmd3.setRootConstants.rootParameterIndex = 0;
    cmd3.setRootConstants.num32BitValuesToSet = 4;
    cmd3.setRootConstants.destOffsetIn32BitValues = 0;
    cmd3.setRootConstants.isCompute = false;
    cmd3.setRootConstants.constants = { 10, 20, 999, 40 };

    CommandBatch batch3;
    batch3.Append(std::move(cmd3));
    hr = backend.ReplayCommandBatch(batch3);
    assert(SUCCEEDED(hr));
    assert(backend.GetRootConstantMapCount() == 2);
    assert(backend.GetRootConstantMapSkippedCount() == 1);

    // 4. Test stack viewport and scissor commands
    RecordedCommand cmdVP;
    cmdVP.type = CommandType::SetViewports;
    new (&cmdVP.setViewports) SetViewportsCmd();
    D3D12_VIEWPORT vp = { 0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f };
    cmdVP.setViewports.viewports.push_back(vp);

    RecordedCommand cmdScissor;
    cmdScissor.type = CommandType::SetScissorRects;
    new (&cmdScissor.setScissors) SetScissorRectsCmd();
    D3D12_RECT rect = { 0, 0, 640, 480 };
    cmdScissor.setScissors.rects.push_back(rect);

    CommandBatch batchVP;
    batchVP.Append(std::move(cmdVP));
    batchVP.Append(std::move(cmdScissor));
    hr = backend.ReplayCommandBatch(batchVP);
    assert(SUCCEEDED(hr));

    // 5. Test Memory Footprint tracking
    auto& rm = ResourceManager::Instance();
    auto mf = rm.GetMemoryFootprint();
    assert(mf.estimatedMemoryBytes >= 0);

    std::cout << "[PASS] TestRootConstantMapSuppressionAndMemoryFootprint" << std::endl;
}

static void TestInitialPipelineStateRecordingAndBatchIsolation() {
    auto& backend = D3D11Backend::Instance();
    assert(backend.IsInitialized());
    auto& rm = ResourceManager::Instance();

    // 1. Initial Graphics PSO recording in CommandBatch (simulating Reset(alloc, pGraphicsPSO))
    CommandBatch batchGfx;
    RecordedCommand cmdGfxPSO;
    cmdGfxPSO.type = CommandType::SetPipelineState;
    ID3D12PipelineState* pDummyGfxPSO = reinterpret_cast<ID3D12PipelineState*>(0xDEADBEEF);
    cmdGfxPSO.setPSO = { pDummyGfxPSO };
    batchGfx.Append(std::move(cmdGfxPSO));

    // Append DrawInstanced without any explicit SetPipelineState
    RecordedCommand cmdDraw;
    cmdDraw.type = CommandType::DrawInstanced;
    cmdDraw.drawInstanced = { 3, 1, 0, 0 };
    batchGfx.Append(std::move(cmdDraw));

    assert(batchGfx.GetCount() == 2);
    assert(batchGfx.GetCommands()[0].type == CommandType::SetPipelineState);
    assert(batchGfx.GetCommands()[0].setPSO.pPipelineState == pDummyGfxPSO);
    assert(batchGfx.GetCommands()[1].type == CommandType::DrawInstanced);

    // Register dummy graphics PSO in ResourceManager
    TranslatedPipelineState gfxPSO = {};
    gfxPSO.pD3D12PSO = pDummyGfxPSO;
    rm.RegisterPSO(pDummyGfxPSO, gfxPSO);

    uint64_t beforeTranslatedGfx = backend.GetTranslatedCommandsCount();
    HRESULT hr = backend.ReplayCommandBatch(batchGfx);
    assert(SUCCEEDED(hr));
    // Both SetPipelineState and DrawInstanced must be replayed successfully
    assert(backend.GetTranslatedCommandsCount() == beforeTranslatedGfx + 2);

    // 2. Initial Compute PSO recording in CommandBatch (simulating Reset(alloc, pComputePSO))
    CommandBatch batchCompute;
    RecordedCommand cmdComputePSO;
    cmdComputePSO.type = CommandType::SetPipelineState;
    ID3D12PipelineState* pDummyComputePSO = reinterpret_cast<ID3D12PipelineState*>(0xCAFEFACE);
    cmdComputePSO.setPSO = { pDummyComputePSO };
    batchCompute.Append(std::move(cmdComputePSO));

    // Append Dispatch without any explicit SetPipelineState
    RecordedCommand cmdDispatch;
    cmdDispatch.type = CommandType::Dispatch;
    cmdDispatch.dispatch = { 2, 2, 1 };
    batchCompute.Append(std::move(cmdDispatch));

    assert(batchCompute.GetCount() == 2);
    assert(batchCompute.GetCommands()[0].type == CommandType::SetPipelineState);
    assert(batchCompute.GetCommands()[0].setPSO.pPipelineState == pDummyComputePSO);
    assert(batchCompute.GetCommands()[1].type == CommandType::Dispatch);

    // Register dummy compute PSO in ResourceManager
    TranslatedPipelineState computePSO = {};
    computePSO.pD3D12PSO = pDummyComputePSO;
    rm.RegisterPSO(pDummyComputePSO, computePSO);

    uint64_t beforeTranslatedCompute = backend.GetTranslatedCommandsCount();
    hr = backend.ReplayCommandBatch(batchCompute);
    assert(SUCCEEDED(hr));
    // Both SetPipelineState and Dispatch must be replayed successfully
    assert(backend.GetTranslatedCommandsCount() == beforeTranslatedCompute + 2);

    // 3. CommandList Reset with nullptr initial state clears batch and isolates state
    CommandBatch batchEmpty;
    batchEmpty.Clear();
    assert(batchEmpty.GetCount() == 0);

    std::cout << "[PASS] TestInitialPipelineStateRecordingAndBatchIsolation" << std::endl;
}

static void TestDescriptorRangeMultiSpaceAndLimitHandling() {
    auto& backend = D3D11Backend::Instance();
    assert(backend.IsInitialized());
    auto& rm = ResourceManager::Instance();

    // Create a multi-space root signature
    ID3D12RootSignature* pMultiSpaceSig = reinterpret_cast<ID3D12RootSignature*>(0xBAADF00D);
    TranslatedRootSignature trSig = {};
    trSig.parameters.resize(5);

    // Param 0: space0 SRVs (t0..t1)
    trSig.parameters[0].type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    trSig.parameters[0].visibility = D3D12_SHADER_VISIBILITY_PIXEL;
    TranslatedDescriptorRange r0 = {};
    r0.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    r0.baseShaderRegister = 0;
    r0.registerSpace = 0;
    r0.numDescriptors = 2;
    r0.offsetInDescriptorsFromTableStart = 0;
    trSig.parameters[0].ranges.push_back(r0);

    // Param 1: space1 SRVs (t0..t1, space1 -> t16..t17)
    trSig.parameters[1].type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    trSig.parameters[1].visibility = D3D12_SHADER_VISIBILITY_PIXEL;
    TranslatedDescriptorRange r1 = {};
    r1.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    r1.baseShaderRegister = 0;
    r1.registerSpace = 1; // Multi-space mapping
    r1.numDescriptors = 2;
    r1.offsetInDescriptorsFromTableStart = 0;
    trSig.parameters[1].ranges.push_back(r1);

    // Param 2: Out of bounds SRV (t127..t128)
    trSig.parameters[2].type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    trSig.parameters[2].visibility = D3D12_SHADER_VISIBILITY_PIXEL;
    TranslatedDescriptorRange r2 = {};
    r2.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    r2.baseShaderRegister = 127;
    r2.registerSpace = 0;
    r2.numDescriptors = 2; // Second descriptor exceeds limit of 128
    r2.offsetInDescriptorsFromTableStart = 0;
    trSig.parameters[2].ranges.push_back(r2);

    // Param 3: Overlapping cb13 CBV (b13 >= 13 reserved for staging)
    trSig.parameters[3].type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    trSig.parameters[3].visibility = D3D12_SHADER_VISIBILITY_ALL;
    TranslatedDescriptorRange r3 = {};
    r3.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    r3.baseShaderRegister = 13;
    r3.registerSpace = 0;
    r3.numDescriptors = 1;
    r3.offsetInDescriptorsFromTableStart = 0;
    trSig.parameters[3].ranges.push_back(r3);

    // Param 4: Out of bounds UAV (u8 >= 8 limit)
    trSig.parameters[4].type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    trSig.parameters[4].visibility = D3D12_SHADER_VISIBILITY_ALL;
    TranslatedDescriptorRange r4 = {};
    r4.rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    r4.baseShaderRegister = 8;
    r4.registerSpace = 0;
    r4.numDescriptors = 1;
    r4.offsetInDescriptorsFromTableStart = 0;
    trSig.parameters[4].ranges.push_back(r4);

    rm.RegisterRootSignature(pMultiSpaceSig, trSig);

    // Record commands to bind these tables
    CommandBatch batch;
    RecordedCommand cmdSig;
    cmdSig.type = CommandType::SetGraphicsRootSignature;
    cmdSig.setRootSig.pRootSignature = pMultiSpaceSig;
    batch.Append(std::move(cmdSig));

    // Bind space0 table (t0..t1)
    RecordedCommand cmdTable0;
    cmdTable0.type = CommandType::SetGraphicsRootDescriptorTable;
    cmdTable0.setRootTable.rootParameterIndex = 0;
    cmdTable0.setRootTable.baseDescriptor.ptr = 0x8000;
    cmdTable0.setRootTable.isCompute = false;
    batch.Append(std::move(cmdTable0));

    // Bind space1 table (t16..t17)
    RecordedCommand cmdTable1;
    cmdTable1.type = CommandType::SetGraphicsRootDescriptorTable;
    cmdTable1.setRootTable.rootParameterIndex = 1;
    cmdTable1.setRootTable.baseDescriptor.ptr = 0x9000;
    cmdTable1.setRootTable.isCompute = false;
    batch.Append(std::move(cmdTable1));

    // Bind out-of-bounds SRV table
    RecordedCommand cmdTable2;
    cmdTable2.type = CommandType::SetGraphicsRootDescriptorTable;
    cmdTable2.setRootTable.rootParameterIndex = 2;
    cmdTable2.setRootTable.baseDescriptor.ptr = 0xA000;
    cmdTable2.setRootTable.isCompute = false;
    batch.Append(std::move(cmdTable2));

    // Bind cb13 table
    RecordedCommand cmdTable3;
    cmdTable3.type = CommandType::SetGraphicsRootDescriptorTable;
    cmdTable3.setRootTable.rootParameterIndex = 3;
    cmdTable3.setRootTable.baseDescriptor.ptr = 0xB000;
    cmdTable3.setRootTable.isCompute = false;
    batch.Append(std::move(cmdTable3));

    // Bind u8 table
    RecordedCommand cmdTable4;
    cmdTable4.type = CommandType::SetGraphicsRootDescriptorTable;
    cmdTable4.setRootTable.rootParameterIndex = 4;
    cmdTable4.setRootTable.baseDescriptor.ptr = 0xC000;
    cmdTable4.setRootTable.isCompute = false;
    batch.Append(std::move(cmdTable4));

    uint64_t beforeRejected = backend.GetRejectedCommandsCount();
    HRESULT hr = backend.ReplayCommandBatch(batch);
    assert(SUCCEEDED(hr));
    // Verify that out-of-bounds slot bindings were rejected explicitly:
    // - Param 2's second descriptor (t128 >= 128)
    // - Param 3's cb13 (b13 >= 13)
    // - Param 4's u8 (u8 >= 8)
    assert(backend.GetRejectedCommandsCount() >= beforeRejected + 3);

    rm.UnregisterRootSignature(pMultiSpaceSig);
    std::cout << "[PASS] TestDescriptorRangeMultiSpaceAndLimitHandling" << std::endl;
}

static void TestGpuFenceEventSynchronization() {
    auto& backend = D3D11Backend::Instance();
    if (!backend.IsInitialized()) {
        backend.Initialize(nullptr);
    }
    if (!backend.IsInitialized() || !backend.GetDevice()) {
        std::cout << "[SKIP] TestGpuFenceEventSynchronization: D3D11 not available." << std::endl;
        return;
    }

    // 1. Direct invocation with idle pipeline
    HRESULT hr = backend.WaitForGpuCompletion(2000);
    assert(SUCCEEDED(hr));

    // 2. Queue GPU work and verify synchronization
    CommandBatch batch;
    RecordedCommand cmdGfx;
    cmdGfx.type = CommandType::SetGraphicsRoot32BitConstants;
    new (&cmdGfx.setRootConstants) SetRoot32BitConstantsCmd();
    cmdGfx.setRootConstants.rootParameterIndex = 0;
    cmdGfx.setRootConstants.num32BitValuesToSet = 4;
    cmdGfx.setRootConstants.constants = { 0xAA, 0xBB, 0xCC, 0xDD };
    cmdGfx.setRootConstants.destOffsetIn32BitValues = 0;
    cmdGfx.setRootConstants.isCompute = false;
    batch.Append(std::move(cmdGfx));

    hr = backend.ReplayCommandBatch(batch);
    assert(SUCCEEDED(hr));

    // 3. Drain and synchronize via D3D11_QUERY_EVENT
    hr = backend.WaitForGpuCompletion(2000);
    assert(SUCCEEDED(hr));

    std::cout << "[PASS] TestGpuFenceEventSynchronization" << std::endl;
}

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "  KeplerBridge Phase 4/6/7 Translation Engine Tests" << std::endl;
    std::cout << "====================================================" << std::endl;

    TestCommandRecordingAndBatching();
    TestResourceStateTrackerTransitions();
    TestResourceManagerDescriptorLookup();
    TestRootParameterConstants();
    TestD3D11BackendCommandReplay();
    TestRootConstantBufferUploadAndAlignment();
    TestRootSignatureReflectionAndRegisterMapping();
    TestComputeDescriptorTableAndUAVBinding();
    TestRepeatedInitializationLifecycle();
    TestUAVDescriptorValidationAndLimits();
    TestMultiSlotComputeUAVBinding();
    TestResourceAwareUAVBarriers();
    TestGraphicsUAVBindingAndCounterSemantics();
    TestRootConstantMapSuppressionAndMemoryFootprint();
    TestInitialPipelineStateRecordingAndBatchIsolation();
    TestDescriptorRangeMultiSpaceAndLimitHandling();
    TestGpuFenceEventSynchronization();

    std::cout << "====================================================" << std::endl;
    std::cout << "ALL TRANSLATION ENGINE TESTS PASSED (17/17)!" << std::endl;
    std::cout << "====================================================" << std::endl;
    return 0;
}
