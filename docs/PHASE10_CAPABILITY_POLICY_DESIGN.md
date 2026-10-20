# KeplerBridge Phase 10 — Capability-Driven Compatibility Architecture Design

**Milestone:** Phase 10 Architectural Specification  
**Document Version:** 1.0.0  
**Research Date:** October 20, 2026  
**Hardware Reference:** NVIDIA GeForce GT 740 (GK208B, 4052 MB DDR3, Native FL 11_0, Driver 456.71, Windows 10 x64)  
**Core Goal:** Eliminate global side effects and ensure translation, remapping, and fallback behaviors are triggered strictly by operation semantics and hardware limits, preventing regressions across unrelated applications.

---

## 1. Audit of Existing Translation Behaviors & Regression Hazards

To guarantee that fixes intended for one game (e.g. *Palworld*, *Deathloop*, *Hogwarts Legacy*) cannot regress unrelated workloads (e.g. *Elden Ring*, *Horizon Zero Dawn*, native D3D11/D3D12 benchmarks), we audit the ten critical subsystems:

| Subsystem | Current Implementation | Potential Cross-Game Regression Hazard | Capability-Driven Remedy |
| :--- | :--- | :--- | :--- |
| **1. Feature Level Reporting** | Global `TargetFeatureLevel=12_0` in `kepler_bridge.ini`. | Applications detecting FL 12_0 may query Tier 2/3 bindless or SM 6.x features that fail later at runtime. | Differentiate reported device feature level from individual capability queries (`D3D12_FEATURE_D3D12_OPTIONS`). Report authentic hardware limits (Tier 1 binding, no wave ops). |
| **2. Shader Model & DXIL/DXBC** | Evaluates container type (`DXBC` vs `DXIL`). Native DXBC passes through; DXIL is rejected unless fallback is configured. | Global rejection of DXIL may block future applications that use benign DXIL without unsupported instructions. | Per-shader capability parsing in `DxilInspector`. Pass through or translate when instructions are valid; reject only unsupported opcodes. |
| **3. Root-Signature & Space Remap** | `spaceStride = 16` (SRVs) / `4` (CBVs/Samplers/UAVs) applied to `registerSpace > 0`. | If an application legitimately uses registers spanning into the stride offset, bindings could overlap. | Zero remapping for standard `space0` (`registerSpace == 0`). Multi-space remapping strictly enabled only when `registerSpace > 0`. |
| **4. Descriptor Slot Bounds** | Enforces hard slot caps: `SRV < 128`, `Sampler < 16`, `CBV < 13`, `UAV < 8`. | High slot bindings beyond D3D11 limits could crash the native driver if passed blindly. | Explicit bounds validation with graceful diagnostic rejection instead of driver crash or memory corruption. |
| **5. Resource States & Barriers** | Resource-aware UAV barrier handling and state tracker transitions. | Emulated barriers could serialize pipelines unnecessarily on workloads with distinct, independent resources. | Resource-specific tracking: barriers are only submitted when the specific resource is genuinely dirty in an active UAV slot. |
| **6. Queue Ordering & Fence Sync** | `WaitForGpuCompletion()` drains D3D11 queue before signaling `ID3D12Fence`. | Unconditional heavy polling could degrade frame rates in lightweight applications with frequent queue signals. | Quick non-blocking `GetData()` query check: if GPU execution is already complete, the function returns `S_OK` immediately in 0 ms. |
| **7. Pipeline State Initialization** | `Reset(alloc, pInitialState)` automatically records initial PSO. | Games resetting lists with `nullptr` could inadvertently retain stale PSOs if state wasn't cleared. | Explicit reset: if `pInitialState == nullptr`, active PSO is cleared to unassigned state; if non-null, new state is recorded. |
| **8. Swapchain Fallback & Tearing** | Tries requested swapchain first; falls back to `DISCARD` and sanitizes tearing flags on failure. | If tearing flags were stripped globally on capable hardware, tearing/VRR would be needlessly disabled. | Opportunistic capability test: native flip model is requested first; sanitization and fallback occur **only** on `DXGI_ERROR_INVALID_CALL`. |
| **9. PSO Creation Throttling** | Throttles driver command queue and yields thread slices every 128 consecutive PSOs. | Interactive gameplay could stutter if throttling triggered during runtime draw call recording. | Burst counter only increments during consecutive creation loops; individual dynamic PSO creations in game loops never hit the 128 threshold. |
| **10. Shader Cache Invalidation** | 64-bit FNV-1a hash salted with compiler flags, driver version, and proxy build ID. | Cache collisions across games or stale shader bytecode after driver updates. | Cache keys incorporate input bytecode hash, compiler flags, and target profile; corruption triggers automatic eviction and recompilation. |

---

## 2. Capability Policy Architecture Design

The Capability-Driven Compatibility Layer consists of three decoupled decision levels:

```
+-----------------------------------------------------------------------------+
|                     CAPABILITY-DRIVEN DECISION LAYERS                       |
+-----------------------------------------------------------------------------+

  [LEVEL 1: Physical Hardware & Driver Baseline]
   - Evaluates native adapter capability (Kepler GK208B, FL 11_0, Driver 456.71)
   - Discovers native limits: Max SRVs=128, Samplers=16, CBVs=14, UAVs=8
   - Never fabricated, never spoofed internally
                                 |
                                 v
  [LEVEL 2: Operation-Level Semantic Evaluation]
   - Evaluates the specific operation (e.g. Draw, Dispatch, Present, Barrier)
   - Determines if the requested operation can be executed natively
   - Identifies if translation, remapping, or sanitization is required
   - Preserves 1:1 identity when no transformation is needed
                                 |
                                 v
  [LEVEL 3: Isolated Workload / Workaround Scoping]
   - Narrowly scoped workarounds applied ONLY when triggered by exact API state
   - Zero guessing based on executable process names
   - Strict diagnostic logging explaining why a transformation occurred
```

---

## 3. Detailed Subsystem Specifications

### 3.1 Device & Feature Support Subsystem
* **Principle:** `CheckFeatureSupport()` must report authentic D3D12 capability tiers even when `TargetFeatureLevel=12_0` is spoofed to pass initial engine checks:
  * `D3D12_RESOURCE_BINDING_TIER`: Must return `D3D12_RESOURCE_BINDING_TIER_1` (Descriptor tables with hardware limits).
  * `D3D12_CONSERVATIVE_RASTERIZATION_TIER`: Must return `D3D12_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED`.
  * `D3D12_WAVE_MMA_TIER`: Must return `D3D12_WAVE_MMA_TIER_NOT_SUPPORTED`.
  * `D3D12_CROSS_NODE_SHARING_TIER`: Must return `D3D12_CROSS_NODE_SHARING_TIER_NOT_SUPPORTED`.
* **Benefit:** Modern game engines (e.g. UE 4.27, RE Engine) inspect these flags and automatically disable advanced paths (e.g. Bindless textures, Tier 3 heaps) and fall back to standard descriptor tables, preventing runtime crashes.

### 3.2 Root Signature & Descriptor Mapping Subsystem
* **Semantic Rule:**
  ```cpp
  UINT d3d11Slot = range.baseShaderRegister;
  if (range.registerSpace > 0) {
      d3d11Slot += (range.registerSpace * spaceStride);
  }
  ```
* **Guarantee:** 
  * If `registerSpace == 0`, `d3d11Slot == baseShaderRegister`. Zero translation overhead or displacement for standard shaders.
  * If `registerSpace > 0`, mapped into disjoint offset ranges.
  * If `d3d11Slot >= HardwareLimit`, the command is safely rejected with `KB_ERR_INVALID_DESCRIPTOR` and logged.

### 3.3 Swapchain Presentation & Lifecycle Subsystem
* **Semantic Rule:**
  1. Attempt `IDXGIFactory2::CreateSwapChainForHwnd()` with user's exact parameters.
  2. If return is `S_OK`, store and return. Zero modification applied.
  3. If return is `DXGI_ERROR_INVALID_CALL` or `E_INVALIDARG`:
     * Strip `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` (`0x200`).
     * Replace `DXGI_SWAP_EFFECT_FLIP_DISCARD` / `FLIP_SEQUENTIAL` with `DXGI_SWAP_EFFECT_DISCARD`.
     * Retry creation.
     * Log: `[SWAPCHAIN] Capability fallback applied: Tearing sanitized and Discard swap effect engaged.`
* **Guarantee:** Flip-model swapchains run natively on platforms that support them; fallback is engaged strictly on platforms where the OS rejects the modern configuration.

### 3.4 Asynchronous Queue & Fence Synchronization Subsystem
* **Semantic Rule:**
  * In `WrappedD3D12CommandQueue::Signal(ID3D12Fence* pFence, UINT64 Value)`:
  * Invoke `D3D11Backend::Instance().WaitForGpuCompletion(2000)`.
  * Implementation:
    1. Check if GPU queue has pending work (`m_pContext->GetData(m_pSyncQuery, ...)`).
    2. If query returns `S_OK` immediately (pipeline idle), return immediately in 0 ms.
    3. If query returns `S_FALSE` (work pending), yield CPU thread (`SwitchToThread()`) and poll until GPU completes.
    4. If timeout exceeds 2,000 ms, report `KB_ERR_DEVICE_TIMEOUT` (1003) to prevent process hang.
* **Guarantee:** Ensures CPU worker threads never access unwritten GPU buffers, while adding zero latency to already completed command streams.

---

## 4. Verification and Regression Defense Plan

To prove that capability-driven compatibility does not regress existing or unrelated workloads:

1. **Synthetic Isolation Tests:**
   * Unit test verifying `space0` mappings produce identical slot indices.
   * Unit test verifying multi-space mappings trigger stride remapping.
   * Unit test verifying out-of-bounds slots are rejected without driver faults.
2. **Multi-Stage Hardware Regression Suite:**
   * Run the complete 15-executable regression suite across all 111 tests.
   * Verify standalone D3D11 initialization (`test_d3d11_standalone.exe`).
   * Verify native D3D12 hardware passthrough (`test_d3d12.exe`, `test_d3d12_compat.exe`).
   * Verify compute and multi-UAV translation (`test_d3d12_compute.exe`, `test_d3d12_multi_uav.exe`).
   * Verify swapchain presentation (`test_dxgi_swapchain.exe`).
3. **Acceptance Threshold:** 100% pass rate across all 111 tests on physical GT 740 hardware before signing off on any capability-policy modification.
