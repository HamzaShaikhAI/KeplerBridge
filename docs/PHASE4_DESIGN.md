# KeplerBridge — Phase 4 Design Specification: Production Translation Layer

**Document Version:** 1.0  
**Target Hardware:** NVIDIA GeForce GT 740 4GB DDR3 (GK208 / GK107, Kepler Architecture)  
**Host Environment:** Windows 10 x64  
**Primary Language:** C++20  
**DirectX Target:** Direct3D 12 to Kepler Translation & Interception Layer  

---

## 1. Problem Statement & Mission

In Phase 1–3, KeplerBridge established:
1. 64-bit DLL proxying and telemetry logging (`d3d12.dll`).
2. Feature level spoofing and policy negotiation (spoofing `D3D_FEATURE_LEVEL_12_0` on native `11_0` hardware).
3. Bounds-checked DXIL container inspection (`DxilInspector`) and pipeline state hook (`PSOHook`).
4. Resource clamping (`TextureClamper`) to protect Kepler's 14.4 GB/s memory bandwidth.

However, when modern games or applications require execution paths not supported by the Kepler D3D12 driver, or when an application relies on D3D11-compatible commands routed through D3D12 API entry points, a **Production Translation Layer** is required.

Phase 4 implements this layer: a robust, thread-safe, modular translation engine that can record D3D12 commands, manage resource allocations, emulate barrier transitions, map descriptor bindings, and replay workloads either natively or onto a dedicated Direct3D 11 backend.

---

## 2. Evaluation of Target Backend Options for GeForce GT 740

| Backend Option | Technical Feasibility on Kepler | Architectural Strengths | Critical Bottlenecks & Limitations | Selection Verdict |
| :--- | :--- | :--- | :--- | :--- |
| **Option 1: Native D3D12 Execution** | **Native Support (FL 11_0)** | Zero translation overhead; native hardware command queues; lowest CPU latency. | Capped at FL 11_0; SM 5.1 max; Resource Binding Tier 2; driver quirks on early WDDM 2.0–2.7 code paths. | **Primary Default** (for supported workloads) |
| **Option 2: Direct3D 11 Backend (`ID3D11Device`)** | **Full Native Support (FL 11_0)** | Highly mature NVIDIA driver (`nvwgf2umx.dll`); rock-solid stability; extensive driver optimizations for GK208; deferred context support. | Requires translating D3D12 explicit barriers, command lists, and root signatures to D3D11 state machine. | **Selected Translation Engine** (for fallback/downgrade workloads) |
| **Option 3: Vulkan Backend (via vkd3d-proton)** | **Unsupported / Infeasible** | Modern open standard with low-level command buffers. | Kepler driver (472.12/456.71) is capped at Vulkan 1.2; lacks `VK_KHR_timeline_semaphore`, dynamic rendering, and descriptor indexing required by modern DX12-to-Vulkan layers. | **Rejected** (silicon & driver missing prerequisites) |
| **Option 4: Software Rasterization (WARP)** | **Available (CPU)** | 100% DX12 feature compliance (FL 12_2, SM 6.6). | CPU-bound; runs at 1–3 FPS; completely unplayable for real-time 3D games. | **Diagnostic Only** |

### Architectural Decision: Dual-Path Hybrid Architecture
1. **Direct Mode (Native D3D12 + Interception):** For workloads that operate within Kepler's native D3D12 capabilities (FL 11_0, SM 5.0/5.1, Tier 2 bindings), commands pass through directly to native hardware with spoofed FL reporting and texture clamping.
2. **Translation Mode (D3D12-to-D3D11 Translation):** When enabled via `EnableD3D11Translation=1`, KeplerBridge wraps Command Queues, Command Allocators, Command Lists, and Resources, intercepting recorded commands and translating them into `ID3D11DeviceContext` operations.

---

## 3. Translation Engine Concrete Execution Model

```
+-----------------------------------------------------------------------------------+
|                           Direct3D 12 Application                                 |
+-----------------------------------------------------------------------------------+
                                         │
                                         ▼
+-----------------------------------------------------------------------------------+
|                             KeplerBridge Proxy Layer                              |
|  - D3D12CreateDevice Hook                                                         |
|  - FeatureLevelCompat (Spoofs FL 12_0 -> 11_0)                                    |
|  - WrappedD3D12Device (Intercepts Device & Object Creation)                       |
+-----------------------------------------------------------------------------------+
                                         │
        ┌────────────────────────────────┴────────────────────────────────┐
        │ Direct Mode                                                     │ Translation Mode
        ▼                                                                 ▼
+──────────────────────────────+                +─────────────────────────────────────────+
| Native Kepler D3D12 Driver   |                | KeplerBridge Translation Engine         |
|  - ID3D12CommandQueue        |                |  - WrappedD3D12CommandQueue             |
|  - ID3D12GraphicsCommandList |                |  - WrappedD3D12GraphicsCommandList      |
|  - Native Kepler Hardware    |                |  - CommandRecorder (Records D3D12 ops)  |
+──────────────────────────────+                |  - ResourceTracker (State & Barriers)   |
                                                |  - DescriptorMapper (Tables & Bindings) |
                                                |  - D3D11Backend (ID3D11DeviceContext)   |
                                                +─────────────────────────────────────────+
                                                                                  │
                                                                                  ▼
                                                +─────────────────────────────────────────+
                                                | NVIDIA Kepler D3D11 Driver (nvwgf2umx)  |
                                                |  - Solid Kepler FL 11_0 Execution       |
                                                +─────────────────────────────────────────+
```

### 3.1 Command Recording & Playback Architecture
In Direct3D 12, command recording is decoupled from execution:
1. **Command Recording (`WrappedD3D12GraphicsCommandList`):**
   - The application calls recording methods: `DrawInstanced`, `DrawIndexedInstanced`, `Dispatch`, `CopyBufferRegion`, `CopyResource`, `ResourceBarrier`, `SetRenderTargets`, `SetViewports`, `SetScissorRects`, `IASetVertexBuffers`, `IASetIndexBuffer`, etc.
   - When translation is active, each call is validated, state-tracked, and recorded into a sequential `CommandBatch` within the command list.
2. **Command Submission (`WrappedD3D12CommandQueue::ExecuteCommandLists`):**
   - The application submits command lists to the queue.
   - `ExecuteCommandLists` iterates through the submitted command lists in order and invokes `D3D11Backend::ReplayCommandBatch(batch)` on the worker/backend thread.
3. **Command Allocator Management (`WrappedD3D12CommandAllocator`):**
   - Manages memory reuse for command batches between frame cycles via `Reset()`.

### 3.2 Resource & Memory Management
In D3D12, resources are created via:
- `CreateCommittedResource`: Creates implicit heap and resource.
- `CreatePlacedResource`: Places resource at offset in explicit `ID3D12Heap`.
- `CreateReservedResource`: Tiled / virtual resource.

**Translation Mapping (`TranslatedResource`):**
- Maintains a reference to the D3D12 resource description (`D3D12_RESOURCE_DESC`), current `D3D12_RESOURCE_STATES`, and heap properties (`D3D12_HEAP_PROPERTIES`).
- On the D3D11 backend:
  - `D3D12_RESOURCE_DIMENSION_BUFFER` -> `ID3D11Buffer` (with `D3D11_BIND_VERTEX_BUFFER`, `INDEX_BUFFER`, `CONSTANT_BUFFER`, `SHADER_RESOURCE`, or `UNORDERED_ACCESS`).
  - `D3D12_RESOURCE_DIMENSION_TEXTURE2D` -> `ID3D11Texture2D` (with clamped dimensions, mapped format, and MIP levels).
- Upload and Readback:
  - `D3D12_HEAP_TYPE_UPLOAD` resources are backed by dynamic/staging buffers supporting `Map` and `Unmap`.
  - `D3D12_HEAP_TYPE_READBACK` resources are backed by staging buffers with CPU read access.

### 3.3 Resource Barrier & State Tracking
Direct3D 11 has an implicit hazard-tracking driver model, whereas Direct3D 12 requires explicit resource barriers (`D3D12_RESOURCE_BARRIER`).
- `ResourceStateTracker` tracks the current state of each subresource.
- When `ResourceBarrier` is called:
  - Validates `StateBefore` matches the tracked state; logs diagnostic warnings if mismatch occurs.
  - Updates tracked state to `StateAfter`.
  - In D3D11 replay:
    - If transitioning a UAV from write to read: inserts `CSUnbind` or flushes UAV hazard.
    - If transitioning a Render Target to Shader Resource: unbinds RTV from OM stage before binding SRV to shader stages (preventing D3D11 simultaneous RTV/SRV hazard errors).

### 3.4 Descriptor Heaps & Root Signature Mapping
Direct3D 12 uses Root Signatures and Descriptor Heaps:
- `RootSignatureMapper`:
  - Parses root parameter descriptors (Constants, CBV descriptors, SRV/UAV descriptors, and Descriptor Tables).
  - Maintains a slot-to-register table:
    - Root Constant -> D3D11 Constant Buffer slot 0.
    - CBV Table -> `VSSetConstantBuffers`, `PSSetConstantBuffers`, `CSSetConstantBuffers`.
    - SRV Table -> `VSSetShaderResources`, `PSSetShaderResources`, `CSSetShaderResources`.
    - Sampler Table -> `VSSetSamplers`, `PSSetSamplers`, `CSSetSamplers`.
- `DescriptorHeapMapper`:
  - Translates CPU and GPU descriptor handles (`D3D12_CPU_DESCRIPTOR_HANDLE`, `D3D12_GPU_DESCRIPTOR_HANDLE`) to underlying translated views (`ID3D11ShaderResourceView`, `ID3D11RenderTargetView`, `ID3D11DepthStencilView`).

### 3.5 Synchronization & Fences (`WrappedD3D12Fence`)
- Applications synchronize GPU-CPU and GPU-GPU via `ID3D12Fence`.
- In native mode, forwards to native `ID3D12Fence`.
- In translation mode, wraps fences using `ID3D11Query` (`D3D11_QUERY_EVENT`) and `HANDLE` event synchronization to satisfy `SetEventOnCompletion` and `GetCompletedValue`.

### 3.6 Swapchain Presentation & Windowing Interop
- Direct3D 12 applications present via DXGI swapchain (`IDXGISwapChain3::Present`).
- Swapchains wrap backbuffer resources (`GetBuffer(0, __uuidof(ID3D12Resource), ...)`).
- When translation is active:
  - `SwapchainManager` maps the D3D12 backbuffer handle to the underlying D3D11 swapchain backbuffer texture, ensuring that rendered contents are presented to the target HWND without tearing or black screens.

---

## 4. Direct3D 12 to Kepler Capability Matrix

| Feature Area | Direct3D 12 Feature | Kepler GT 740 Status | Implementation Strategy |
| :--- | :--- | :--- | :--- |
| **Feature Levels** | `D3D_FEATURE_LEVEL_11_0` | **Native** | Hardware native execution. |
| | `D3D_FEATURE_LEVEL_11_1` | **Emulated** | Spoofed via `CheckFeatureSupport`; native driver capabilities used. |
| | `D3D_FEATURE_LEVEL_12_0` | **Spoofed / Translated** | Spoofed to pass launch checks; commands executed natively or translated to D3D11. |
| | `D3D_FEATURE_LEVEL_12_1+` | **Explicitly Unsupported** | Rejected or degraded with safe error reporting. |
| **Shader Models** | Shader Model 5.0 | **Native** | Full native execution on Kepler CUDA cores. |
| | Shader Model 5.1 | **Native** | Supported natively by Kepler WDDM 2.7 D3D12 driver. |
| | Shader Model 6.0–6.7 (DXIL) | **Explicitly Unsupported** | Inspected via `DxilInspector`; rejected gracefully (`E_FAIL`) or translated when translator available. |
| **Resource Binding** | Tier 1 & Tier 2 | **Native** | Natively supported (up to 14 CBVs, 128 SRVs, 16 Samplers, 64 UAVs). |
| | Tier 3 (Unbounded Arrays) | **Explicitly Unsupported** | Hardware cannot address unbounded descriptor arrays. |
| **Command Lists** | Direct & Compute Lists | **Translated / Native** | Recorded and executed natively or replayed on D3D11 context. |
| | Copy Lists | **Translated / Native** | Mapped to D3D11 `CopySubresourceRegion` / `CopyResource`. |
| | Bundle Lists | **Emulated** | Flattened and inlined into primary command list on replay. |
| **Barriers & States** | State Transitions | **Emulated** | Tracked in software; hazard mitigation applied to D3D11 pipeline. |
| | UAV Aliasing & Sync | **Emulated** | D3D11 UAV hazard unbind and flush. |
| **Memory Bandwidth** | 4K Textures / High MSAA | **Clamped** | Downsampled to 512/1024; MSAA clamped to 1x via `TextureClamper`. |
| **Hardware Raytracing** | DXR / Raytracing Tier 1.x | **Explicitly Unsupported** | Returns `D3D12_RAYTRACING_TIER_NOT_SUPPORTED`. |
| **Variable Rate Shading**| VRS Tier 1 / 2 | **Explicitly Unsupported** | Returns `D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED`. |
| **Mesh Shaders** | Mesh / Amplification Shaders| **Explicitly Unsupported** | Returns `D3D12_MESH_SHADER_TIER_NOT_SUPPORTED`. |

---

## 5. Configuration & Toggle Matrix

New configuration settings introduced in `config/kepler_bridge.ini`:

```ini
[Translation]
; Master switch for D3D12-to-D3D11 translation engine (0 = Direct native D3D12, 1 = Enable D3D11 Translation)
EnableD3D11Translation=0

; Software barrier emulation and hazard resolution
EmulateBarriers=1

; Track resource states and log state transitions
EnableStateTracking=1

; Flush D3D11 immediate context on CommandQueue submission
ImmediateQueueFlush=1

; Emulate unsupported query types
EmulateQueries=1
```

---

## 6. Verification & Implementation Plan

1. **Step 1:** Implement core translation architecture:
   - `include/translator/command_translator.h` & `src/command_translator.cpp`: Command recording, batch representation, and state tracking.
   - `include/translator/d3d11_backend.h` & `src/d3d11_backend.cpp`: D3D11 device/context management and command replay engine.
   - `include/translator/resource_manager.h` & `src/resource_manager.cpp`: Resource allocation, descriptor mapping, and buffer tracking.
2. **Step 2:** Integrate with COM wrappers:
   - `WrappedD3D12CommandQueue`, `WrappedD3D12GraphicsCommandList`, and `WrappedD3D12CommandAllocator` in `include/core/feature_level_compat.h` / `src/feature_level_compat.cpp`.
3. **Step 3:** Extend configuration in `config.h`, `config.cpp`, and `kepler_bridge.ini`.
4. **Step 4:** Build and test:
   - Unit tests: `test_translation_engine.cpp` verifying command recording, barrier tracking, and descriptor mapping.
   - Integration tests: `test_d3d12_translation.cpp` testing real D3D12 recording and D3D11 backend replay on GT 740.
   - Regression verification of all Phase 1–3 tests.
5. **Step 5:** Empirical documentation and test reporting in `docs/PHASE4_TEST_REPORT.md` and `README.md`.
