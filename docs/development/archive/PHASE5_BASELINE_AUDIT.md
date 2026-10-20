# KeplerBridge — Phase 5 Baseline Architecture & Method-Level Audit

**Date:** October 2026  
**Auditor:** Senior Windows Graphics Systems Architect  
**Target Hardware:** NVIDIA GeForce GT 740 (GK208, 4052 MB DDR3, Feature Level 11_0)  
**Host Environment:** Windows 10 x64 Pro (Build 19045)  
**Driver Branch:** NVIDIA 456.71 (WDDM 2.7)  
**Compiler:** WinLibs MinGW-w64 GCC 14.2.0 (POSIX UCRT)  

---

## 1. Executive Summary

This document provides a comprehensive, method-level audit of the **KeplerBridge** codebase at the commencement of Phase 5. Phase 4 successfully demonstrated the translation of a Direct3D 12 triangle rendering pipeline into Direct3D 11 draw commands, verified via 65,536-pixel staging readback and diagnostic frame presentation.

The primary objective of Phase 5 is **Compatibility Expansion and Production Hardening**, progressing from isolated diagnostic proofs to broad, robust, game-compatible execution. This audit systematically catalogs every interface method across D3D12 and DXGI, establishes their implementation status, analyzes COM lifetime and thread-safety invariants, and details the primary architectural gap blocking commercial games: **Application-Facing DXGI Swap-Chain and Back-Buffer Integration**.

---

## 2. Baseline Test Execution Results

Prior to making any modifications in Phase 5, all established test suites were executed on live hardware to record baseline compliance:

| Test Binary | Target Scope | Tests Run | Tests Passed | Baseline Status |
| :--- | :--- | :--- | :--- | :--- |
| `test_policy_engine.exe` | Feature level negotiation, spoof rules, config parsing | 6 | 6 | **PASS (100%)** |
| `test_shader_inspector.exe` | DXBC chunk bounds, DXIL container parsing, clamping | 11 | 11 | **PASS (100%)** |
| `test_translation_engine.exe` | Command batching, barrier tracking, D3D11 context replay | 5 | 5 | **PASS (100%)** |
| `test_d3d11_standalone.exe` | Direct D3D11 device creation on default & explicit GT 740 | 2 | 2 | **PASS (100%)** |
| `test_d3d12_compat.exe` | Native FL 11_0 device creation, FL 12_0 probing on GT 740 | 3 | 3 | **PASS (100%)** |

All 27 baseline regression checks passed with zero errors, zero memory leaks, and zero deadlocks.

---

## 3. Comprehensive Method-Level Compatibility Matrix

### Classification Legend:
1. **[T-TEST] Translated & Tested:** Fully recorded, translated to D3D11 backend, and verified with integration/readback tests.
2. **[T-PART] Implemented but Incompletely Tested:** Translation logic is written in the engine but lacks exhaustive coverage in test suites.
3. **[N-PASS] Delegated to Native D3D12:** Safely forwarded to the real Kepler D3D12 driver without modification.
4. **[REJECT] Unsupported & Correctly Rejected:** Unsupported feature returning appropriate `E_NOINTERFACE`, `E_NOTIMPL`, or error HRESULT.
5. **[UNVER] Unverified:** Stubbed or passthrough with unknown stability on live hardware under heavy load.

---

### 3.1 `ID3D12Device` / `ID3D12Device1` ([`src/feature_level_compat.cpp`](file:///C:/Users/win10/Documents/KeplerBridge/src/feature_level_compat.cpp))

| Interface Method | Classification | Description & Implementation Details |
| :--- | :--- | :--- |
| `QueryInterface` | **[T-TEST]** | Supports `IUnknown`, `ID3D12Object`, `ID3D12Device`, `ID3D12Device1`. |
| `AddRef` / `Release` | **[T-TEST]** | Atomic thread-safe reference count. Safely releases real device pointers on 0. |
| `GetPrivateData` / `SetPrivateData` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `SetPrivateDataInterface` / `SetName` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `GetNodeCount` | **[T-TEST]** | Returns native node count (1 on GT 740). |
| `CreateCommandQueue` | **[T-TEST]** | Wraps queue in `WrappedD3D12CommandQueue`, lazily initializes D3D11 backend matching adapter LUID. |
| `CreateCommandAllocator` | **[T-TEST]** | Wraps allocator in `WrappedD3D12CommandAllocator`. |
| `CreateGraphicsPipelineState` | **[T-TEST]** | Intercepts via `PSOHook`, disassembles SM 5.0 DXBC, generates D3D11 shaders and state objects. |
| `CreateComputePipelineState` | **[T-PART]** | Intercepts via `PSOHook`, inspects CS bytecode, forwards to native D3D12. |
| `CreateCommandList` | **[T-TEST]** | Wraps list in `WrappedD3D12GraphicsCommandList`, unwrapping allocator if wrapped. |
| `CheckFeatureSupport` | **[T-TEST]** | Intercepts `D3D12_FEATURE_FEATURE_LEVELS` when spoofing enabled; all other features pass through to real hardware. |
| `CreateDescriptorHeap` | **[T-TEST]** | Creates real D3D12 descriptor heap via `m_pReal`. |
| `GetDescriptorHandleIncrementSize` | **[T-TEST]** | Forwarded to native `m_pReal`. |
| `CreateRootSignature` | **[T-TEST]** | Forwarded to native `m_pReal`. |
| `CreateConstantBufferView` | **[N-PASS]** | Native pass-through. |
| `CreateShaderResourceView` | **[T-TEST]** | Queries D3D11 resource from `ResourceManager`, creates `ID3D11ShaderResourceView`, maps handle. |
| `CreateUnorderedAccessView` | **[T-PART]** | Queries D3D11 resource, creates `ID3D11UnorderedAccessView`, maps handle. |
| `CreateRenderTargetView` | **[T-TEST]** | Queries D3D11 resource, creates `ID3D11RenderTargetView`, maps handle in `ResourceManager`. |
| `CreateDepthStencilView` | **[T-PART]** | Queries D3D11 resource, creates `ID3D11DepthStencilView`, maps handle in `ResourceManager`. |
| `CreateSampler` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `CopyDescriptors` / `CopyDescriptorsSimple` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `GetResourceAllocationInfo` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `GetCustomHeapProperties` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `CreateCommittedResource` | **[T-TEST]** | Clamps texture dimensions via `TextureClamper`, creates native resource, creates matching D3D11 buffer/texture, registers GPU VA. |
| `CreateHeap` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `CreatePlacedResource` | **[T-PART]** | Validates/clamps desc, forwards to native `m_pReal`. |
| `CreateReservedResource` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `CreateSharedHandle` / `OpenSharedHandle` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `MakeResident` / `Evict` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `CreateFence` | **[T-TEST]** | Forwarded to native `m_pReal`. |
| `GetDeviceRemovedReason` | **[T-TEST]** | Forwarded to native `m_pReal`. |
| `GetCopyableFootprints` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `CreateQueryHeap` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `SetStablePowerState` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `CreateCommandSignature` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `GetResourceTiling` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `GetAdapterLuid` | **[T-TEST]** | Queries adapter LUID from real device. |
| `CreatePipelineLibrary` (Device1) | **[REJECT]** | Returns `E_NOTIMPL` (Pipeline libraries unsupported on FL 11_0 Kepler driver). |
| `SetEventOnMultipleFenceCompletion` (Device1) | **[N-PASS]** | Forwarded to native `m_pReal1` if present. |
| `SetResidencyPriority` (Device1) | **[N-PASS]** | Forwarded to native `m_pReal1` if present. |

---

### 3.2 `ID3D12CommandQueue` ([`src/wrapped_command_objects.cpp`](file:///C:/Users/win10/Documents/KeplerBridge/src/wrapped_command_objects.cpp))

| Interface Method | Classification | Description & Implementation Details |
| :--- | :--- | :--- |
| `QueryInterface` | **[T-TEST]** | Supports `IUnknown`, `ID3D12Object`, `ID3D12DeviceChild`, `ID3D12Pageable`, `ID3D12CommandQueue`. |
| `AddRef` / `Release` | **[T-TEST]** | Atomic thread-safe reference count. Safely frees wrapped real queue on 0. |
| `GetDevice` | **[T-TEST]** | Forwarded to native `m_pReal`. |
| `UpdateTileMappings` / `CopyTileMappings` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `ExecuteCommandLists` | **[T-TEST]** | Replays recorded `CommandBatch` onto `D3D11Backend` context and forwards un-wrapped list pointers to real D3D12 hardware queue. |
| `SetMarker` / `BeginEvent` / `EndEvent` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `Signal` / `Wait` | **[T-TEST]** | Native D3D12 fence signaling and waiting. |
| `GetTimestampFrequency` / `GetClockCalibration` | **[N-PASS]** | Forwarded to native `m_pReal`. |
| `GetDesc` | **[T-TEST]** | Returns cached queue description. |

---

### 3.3 `ID3D12GraphicsCommandList` ([`src/wrapped_command_objects.cpp`](file:///C:/Users/win10/Documents/KeplerBridge/src/wrapped_command_objects.cpp))

| Interface Method | Classification | Description & Implementation Details |
| :--- | :--- | :--- |
| `Close` | **[T-TEST]** | Marks list closed, forwards to real list. |
| `Reset` | **[T-TEST]** | Clears `CommandBatch`, updates allocator ref, resets real list. |
| `ClearState` | **[T-PART]** | Clears `CommandBatch`, resets real list. |
| `DrawInstanced` | **[T-TEST]** | Records `CommandType::DrawInstanced` in batch; replays `DrawInstanced` on D3D11 context. |
| `DrawIndexedInstanced` | **[T-PART]** | Records `CommandType::DrawIndexedInstanced` in batch; replays `DrawIndexedInstanced` on D3D11 context. |
| `Dispatch` | **[T-PART]** | Records `CommandType::Dispatch` in batch; replays `Dispatch` on D3D11 context. |
| `CopyBufferRegion` | **[T-PART]** | Records `CommandType::CopyBufferRegion`; replays `CopySubresourceRegion` on D3D11 buffers. |
| `CopyTextureRegion` | **[N-PASS]** | Native pass-through. |
| `CopyResource` | **[T-PART]** | Records `CommandType::CopyResource`; replays `CopyResource` on D3D11 textures. |
| `CopyTiles` / `ResolveSubresource` | **[N-PASS]** | Native pass-through. |
| `IASetPrimitiveTopology` | **[T-TEST]** | Records `CommandType::IASetPrimitiveTopology`; replays on D3D11 context. |
| `RSSetViewports` | **[T-TEST]** | Records `CommandType::SetViewports`; replays `RSSetViewports` on D3D11 context. |
| `RSSetScissorRects` | **[T-TEST]** | Records `CommandType::SetScissorRects`; replays `RSSetScissorRects` on D3D11 context. |
| `OMSetBlendFactor` / `OMSetStencilRef` | **[N-PASS]** | Native pass-through. |
| `SetPipelineState` | **[T-TEST]** | Records `CommandType::SetPipelineState`; binds translated VS, PS, IL, RS, BS, and DS states in D3D11. |
| `ResourceBarrier` | **[T-TEST]** | Validates state transitions via `ResourceStateTracker`; resolves simultaneous RTV/SRV binding hazards before D3D11 draw calls. |
| `ExecuteBundle` | **[N-PASS]** | Native pass-through. |
| `SetDescriptorHeaps` | **[N-PASS]** | Native pass-through. |
| `SetComputeRootSignature` / `SetGraphicsRootSignature` | **[T-TEST]** | Recorded in batch as metadata; root signatures are abstracted in D3D11. |
| `SetComputeRootDescriptorTable` / `SetGraphicsRootDescriptorTable` | **[T-PART]** | Recorded in batch; maps table slots to D3D11 shader view slots. |
| `SetComputeRoot32BitConstant` / `SetGraphicsRoot32BitConstant` | **[T-TEST]** | Unpacks and records 32-bit constant updates into root parameter store. |
| `SetComputeRoot32BitConstants` / `SetGraphicsRoot32BitConstants` | **[T-TEST]** | Records constant vector; verified via `TestRootParameterConstants`. |
| `SetComputeRootConstantBufferView` / `SetGraphicsRootConstantBufferView` | **[N-PASS]** | Native pass-through. |
| `IASetIndexBuffer` | **[T-PART]** | Resolves D3D11 buffer via GPU VA lookup; records and binds with `IASetIndexBuffer`. |
| `IASetVertexBuffers` | **[T-TEST]** | Resolves dynamic D3D11 upload buffer via GPU VA; maps and copies CPU bytes with `WRITE_DISCARD`; binds with `IASetVertexBuffers`. |
| `OMSetRenderTargets` | **[T-TEST]** | Resolves `ID3D11RenderTargetView` and `ID3D11DepthStencilView` from descriptor handles; binds via `OMSetRenderTargets`. |
| `ClearDepthStencilView` | **[T-PART]** | Resolves DSV handle; records and replays `ClearDepthStencilView` on D3D11 context. |
| `ClearRenderTargetView` | **[T-TEST]** | Resolves RTV handle; records color; replays `ClearRenderTargetView` on D3D11 context. |
| `ClearUnorderedAccessViewUint` / `ClearUnorderedAccessViewFloat` | **[N-PASS]** | Native pass-through. |
| `DiscardResource` | **[N-PASS]** | Native pass-through. |
| `BeginQuery` / `EndQuery` / `ResolveQueryData` | **[N-PASS]** | Native pass-through. |
| `SetPredication` / `SetMarker` / `BeginEvent` / `EndEvent` | **[N-PASS]** | Native pass-through. |
| `ExecuteIndirect` | **[N-PASS]** | Native pass-through. |

---

### 3.4 DXGI Swap-Chain & Presentation Interoperation

| Interface / Operation | Classification | Description & Implementation Details |
| :--- | :--- | :--- |
| `KeplerBridge_CreateSwapChainForHwnd` | **[T-TEST]** | Creates a raw D3D11 swap chain owned by `m_pDevice`. Used for diagnostic proofs. |
| `KeplerBridge_PresentSwapChain` | **[T-TEST]** | Calls `pSwapChain->Present(syncInterval, flags)`. Returned `S_OK`. |
| `IDXGIFactory::CreateSwapChain` (App-facing) | **[UNVER]** | When an application passes `WrappedD3D12CommandQueue*` to standard DXGI, DXGI queries private interfaces (`INDXGICommandQueue`) and creates a native D3D12 swapchain. The D3D11 translation backend is bypassed! |
| `IDXGIFactory2::CreateSwapChainForHwnd` (App-facing) | **[UNVER]** | Same as above. Commercial titles cannot present translated frames without a dedicated application-facing swap-chain bridge. |
| `IDXGISwapChain::GetBuffer` (Back-buffer retrieval) | **[UNVER]** | D3D12 titles call `pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer))` expecting an `ID3D12Resource*`. KeplerBridge previously had no mapping between swap-chain back buffers and D3D12 resources. |
| `IDXGISwapChain::ResizeBuffers` | **[UNVER]** | Window resize operations require recreating/resizing both D3D11 and D3D12 back-buffer associations. |
| `IDXGISwapChain3::GetCurrentBackBufferIndex` | **[UNVER]** | Modern titles rely on this index to select the target render target descriptor. |

---

## 4. COM Invariant, Lifetime & Concurrency Analysis

1. **Reference Counting Invariants:**
   - All wrapper objects (`WrappedD3D12Device`, `WrappedD3D12CommandQueue`, `WrappedD3D12GraphicsCommandList`, `WrappedD3D12CommandAllocator`) implement thread-safe reference counting via `std::atomic<ULONG> m_refCount`.
   - Wrapping retains the underlying native pointer with `AddRef()` on initialization and `Release()` on destruction.
2. **Interface Inheritance & `QueryInterface`:**
   - `WrappedD3D12Device` inherits directly from `ID3D12Device1` (and thus `ID3D12Device`, `ID3D12Object`, `IUnknown`).
   - `QueryInterface` strictly evaluates interface GUIDs (`IID_IUnknown`, `__uuidof(ID3D12Device)`, `__uuidof(ID3D12Device1)`) returning the typed pointer with incremented reference count.
3. **Concurrency & Thread Safety:**
   - `WrappedD3D12CommandQueue::ExecuteCommandLists` synchronizes queue submission via `std::mutex m_mutex`.
   - `WrappedD3D12GraphicsCommandList` synchronizes command recording and batch mutation via `std::mutex m_mutex`.
   - `ResourceManager` guards its internal translation ledgers (`m_resources`, `m_rtvs`, `m_gpuVaToBuffer`) with `std::mutex m_mutex`.
   - `D3D11Backend` guards replay execution and swap-chain presentation with `std::mutex m_mutex`.
4. **Adapter Matching & Loader Lock Safety:**
   - Confirmed: Eager initialization removed. The D3D11 backend is initialized lazily and matched to the exact physical adapter LUID via `IDXGIFactory1::EnumAdapters1`.
   - Confirmed: `ProxyLoader::Shutdown(bool isProcessDetach)` skips `FreeLibrary` during process detach, eliminating OS loader lock deadlocks with NVIDIA's driver threads (`nvwgf2umx.dll`).

---

## 5. Architectural Gap Identification: The Swap-Chain Problem

### Problem Statement:
In Phase 4, presentation was verified using diagnostic helper functions (`KeplerBridge_CreateSwapChainForHwnd` and `KeplerBridge_PresentSwapChain`). 
However, **real-world Direct3D 12 game titles do not use diagnostic helper functions**. Real titles follow this exact lifecycle:

```
1. App creates D3D12 Device & CommandQueue:
   D3D12CreateDevice(pAdapter, FL 11_0, &pDevice);
   pDevice->CreateCommandQueue(&queueDesc, &pQueue);

2. App creates standard DXGI Factory:
   CreateDXGIFactory1(&pFactory);

3. App creates SwapChain passing the D3D12 CommandQueue:
   pFactory->CreateSwapChainForHwnd(pQueue, hwnd, &desc, ... &pSwapChain);

4. App retrieves SwapChain back-buffers as D3D12 resources:
   pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pD3D12BackBuffer0));
   pSwapChain->GetBuffer(1, IID_PPV_ARGS(&pD3D12BackBuffer1));

5. App creates RTVs for back-buffers:
   pDevice->CreateRenderTargetView(pD3D12BackBuffer0, nullptr, rtvHandle0);
   pDevice->CreateRenderTargetView(pD3D12BackBuffer1, nullptr, rtvHandle1);

6. Every frame:
   UINT bbIdx = pSwapChain3->GetCurrentBackBufferIndex();
   pCmdList->OMSetRenderTargets(1, &rtvHandle[bbIdx], ...);
   pCmdList->ClearRenderTargetView(rtvHandle[bbIdx], ...);
   pCmdList->DrawInstanced(...);
   pCmdList->Close();
   pQueue->ExecuteCommandLists(1, &pCmdList);
   pSwapChain->Present(1, 0);

7. On window resize:
   pSwapChain->ResizeBuffers(2, newWidth, newHeight, ...);
```

### The Failure Modes in Current KeplerBridge:
1. **Queue Passing Failure:** Passing `WrappedD3D12CommandQueue*` directly into standard Windows `dxgi.dll` causes DXGI to query internal undocumented interfaces. If DXGI unwraps `m_pReal`, it creates a *native D3D12 swapchain*. In translation mode, all rendering happens on D3D11, leaving the native D3D12 swapchain displaying a black or uninitialized screen.
2. **Back-Buffer Disconnection:** In native D3D12, `pSwapChain->GetBuffer` returns an `ID3D12Resource*` created by DXGI. `ResourceManager` has no knowledge of this resource and no corresponding `ID3D11Texture2D` or `ID3D11RenderTargetView`. Any commands targeting it are rejected or dropped.
3. **Present Call Disconnect:** Calling `pSwapChain->Present` on a native D3D12 swapchain does not flush or present the D3D11 translation context.

---

## 6. Phase 5 Implementation Priority

To resolve this critical blocker and progress KeplerBridge toward genuine game compatibility, **Task 3 must implement an Application-Facing DXGI Swap-Chain Integration Layer**:

1. **`WrappedDXGISwapChain` Implementation:**
   - Implements `IDXGISwapChain3`, `IDXGISwapChain2`, `IDXGISwapChain1`, `IDXGISwapChain`.
   - Wraps the physical D3D11 swap chain created on the Kepler GT 740.
2. **Back-Buffer Virtualization & Mapping:**
   - For each swap chain buffer (e.g. buffers 0 and 1), creates a corresponding D3D12-facing `ID3D12Resource` proxy.
   - Automatically registers each D3D12 back buffer with its underlying `ID3D11Texture2D` in `ResourceManager`.
   - Ensures `pDevice->CreateRenderTargetView(pBackBuffer, ...)` binds directly to the D3D11 swap chain's real back buffer view.
3. **Application-Facing Swap-Chain Creation Bridge:**
   - Intercepts or provides the application-facing swap chain creation (`KeplerBridge_CreateDXGISwapChainForHwnd` and `KeplerBridge_CreateDXGISwapChain`), binding the `WrappedD3D12CommandQueue` to the D3D11 backend.
4. **Lifecycle & State Management:**
   - Supports `Present(syncInterval, flags)` with immediate D3D11 context flush and GPU frame synchronization.
   - Supports `GetCurrentBackBufferIndex()` tracking flip index across frame boundaries.
   - Supports `ResizeBuffers(...)` cleanly unbinding and recreating D3D11 views and D3D12 back-buffer proxies.
