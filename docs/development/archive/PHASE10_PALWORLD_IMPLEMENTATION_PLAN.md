# KeplerBridge Phase 10 — Palworld Implementation & Validation Plan

**Milestone:** Phase 10 Engineering Implementation Plan  
**Document Version:** 1.0.0  
**Target Workload:** *Palworld* (Unreal Engine 5.1 / Pocketpair)  
**Hardware Reference:** NVIDIA GeForce GT 740 (GK208B, 4052 MB DDR3, Native FL 11_0, Driver 456.71, Windows 10 x64)  
**Architecture Principle:** Evidence-backed, minimal dependencies, explicit gate progression, zero fabricated benchmarks.

---

## 1. Explicit Gating Structure

Execution proceeds through six strictly ordered gates (P10-A through P10-F). Progression to the next gate requires satisfying all exit criteria of the preceding gate.

```
+-----------------------------------------------------------------------------+
|                           PHASE 10 GATE WORKFLOW                            |
+-----------------------------------------------------------------------------+
   [P10-A: Reproduction & Telemetry Capture]
        |
        v
   [P10-B: Minimal Translation Architectural Design]
        |
        v
   [P10-C: Targeted Capability Implementation]
        |
        v
   [P10-D: Focused Unit & Correctness Tests]
        |
        v
   [P10-E: Physical Workload & Game Validation]
        |
        v
   [P10-F: Release Readiness & Playability Decision]
```

---

## 2. Gate Specifications & Acceptance Criteria

### Gate P10-A: Reproduction & Telemetry Capture

* **Objective:** Establish repeatable startup behavior on physical hardware, isolating the exact point of failure and capturing all runtime parameters.
* **Activities:**
  1. Deploy KeplerBridge proxy `d3d12.dll` and `kepler_bridge.ini` alongside `Pal-Win64-Shipping.exe`.
  2. Configure `LogVerboseCalls=1` and `EnableShaderInspection=1`.
  3. Execute standard launch (without `-dx11`).
  4. Capture and log:
     * OS loader proxy binding timestamp.
     * `D3D12CreateDevice` parameters (requested FL, adapter LUID).
     * `WrappedD3D12Device` wrapping status and COM identity.
     * First failing API call and exact native `HRESULT`.
     * Intercepted shader bytecode blobs dumped to `kepler_cache/dumps/`.
* **Exit Criteria:** Capture log artifact proving whether failure occurs at device creation, swapchain initialization, or pipeline state creation.

---

### Gate P10-B: Minimal Translation Architectural Design

* **Objective:** Determine the least invasive, highest-reliability engineering path to resolve the failing shaders without introducing massive external dependencies.
* **Architectural Trade-Off Analysis:**

| Design Approach | Dependency Footprint | Binary Size Impact | Compilation Latency | Correctness Risk | Verdict |
| :--- | :--- | :--- | :--- | :--- | :---: |
| **Option 1: Full LLVM / DXIL Toolchain** (`dxil-spirv` + `SPIRV-Cross`) | Hundreds of external LLVM headers & static libraries | +25 to 40 MB DLL size | 200–500 ms per shader on CPU | High (Complex DXIL lowering regressions) | **REJECTED (V2 Only)** |
| **Option 2: Naive String / Token Replacement** | Zero | 0 KB | Negligible | Critical (Corrupts bytecode offsets and SIMD masks) | **REJECTED (Illegal)** |
| **Option 3: Curated DXBC Cache Injection & Fallback Mapping** | Pure Win32 / standard C++ | < 100 KB | < 0.3 ms (Direct disk cache hit) | Low (Authentic SM 5.0 DXBC compiled via `d3dcompiler_47.dll`) | **SELECTED (V1 Strategy)** |
| **Option 4: Explicit Capability Rejection with Diagnostic** | Zero | 0 KB | 0 ms | None (Deterministic error propagation) | **ACTIVE SAFETY NET** |

* **Exit Criteria:** Approved technical design specification for Option 3 with documented fallback to Option 4.

---

### Gate P10-C: Targeted Capability Implementation

* **Objective:** Implement the approved minimal translation mechanism into the KeplerBridge codebase.
* **Implementation Rules:**
  1. **Strict Semantic Preservation:** Never emulate a wave operation with a flat arithmetic substitution. If wave reduction semantics cannot be accurately preserved via threadgroup reductions (`groupshared` memory, `GroupMemoryBarrierWithGroupSync()`), the shader must be honestly rejected.
  2. **Bytecode Staging & Injection:** Implement `ShaderDiskCache::InjectFallbackDXBC(uint64_t shaderHash, const void* pBytecode, size_t size)` allowing pre-compiled authentic SM 5.0 DXBC to override unsupported DXIL compute shaders.
  3. **Diagnostic Reporting:** Generate a structured `DiagnosticReport` with `KeplerErrorCode::UnsupportedShaderInstruction` (3001) whenever an unmapped SM 6.x DXIL shader is encountered.
  4. **Runtime Scope:** Ensure the workaround is capability-driven and does not alter behavior for applications submitting valid SM 5.0 DXBC.
* **Exit Criteria:** Implementation compiles cleanly under MinGW-w64 GCC 16.2.0 (UCRT64) with zero compiler warnings.

---

### Gate P10-D: Focused Unit & Correctness Tests

* **Objective:** Verify numerical correctness, boundary limits, and synchronization behavior of the new capability in isolated test executables.
* **Required New Tests:**
  1. `TestWaveIntrinsicsSemanticCorrectness`: Validate threadgroup reduction emulation against a known mathematical reference across all 32 warp lanes.
  2. `TestDxilInjectionCacheHit`: Verify pre-compiled SM 5.0 DXBC successfully replaces intercepted DXIL bytecode on physical hardware.
  3. `TestHonestRejectionOfUnmappedWaveOps`: Ensure non-curated SM 6.x DXIL shaders fail honestly with `E_NOTIMPL` and do not crash the host process.
* **Exit Criteria:** All new unit tests pass cleanly, and the full 15-executable regression suite passes 100% on physical GT 740 hardware.

---

### Gate P10-E: Physical Workload & Game Validation

* **Objective:** Execute the physical Palworld game binary on the NVIDIA GeForce GT 740 and record live telemetry.
* **Mandatory Measurements:**
  1. *Startup & Menu:* Capture time to title screen, memory footprint, and swapchain presentation.
  2. *World Generation / Save Loading:* Verify asset streaming, compute shader dispatch completion, and texture allocation.
  3. *In-Game Gameplay:* Conduct a 15-minute continuous traversal and combat session.
  4. *Stability & TDR:* Monitor for GPU device removal (`0x887A0005`) and frame pacing stalls.
  5. *Comparative Baseline:* Run an identical test session using native `-dx11` and record separate frame-time, memory, and visual fidelity baselines.
* **Exit Criteria:** Complete benchmark dossier with frame-time graphs, GPU memory logs, and visual comparison screenshots.

---

### Gate P10-F: Release Readiness & Playability Decision

* **Objective:** Determine final compatibility classification based on physical test evidence.
* **Classification Rules:**
  * **VERIFIED PLAYABLE:** Only if the game boots, loads saves, maintains stable frametimes without visual corruption, and experiences zero TDR events over a 15-minute session.
  * **PARTIALLY WORKING:** If gameplay is achievable but exhibits minor visual defects or requires manual config edits.
  * **BLOCKED / DEFERRED TO V2:** If compute shader translation fails to render critical game elements or triggers driver crashes.
* **Exit Criteria:** Formal release decision signed off and documented in `CHANGELOG.md` and `README.md`.
