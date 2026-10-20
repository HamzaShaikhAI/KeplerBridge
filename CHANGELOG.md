# Changelog

All notable changes to the KeplerBridge project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.8.0-rc1] - 2026-10-20 (Phase 9 & Phase 10: Correctness Hardening & Capability Expansion)

### Added
- **COM Child-Object Hierarchy Continuity (P1):** Implemented `*ppvDevice = m_pDevice` with `AddRef()` in `WrappedD3D12CommandAllocator`, `WrappedD3D12GraphicsCommandList`, and `WrappedD3D12CommandQueue`. Blocked leaking higher execution list interfaces (`ID3D12GraphicsCommandList1/2` return `E_NOINTERFACE`), preventing modern engines from bypassing proxy command recording.
- **Initial Pipeline State Preservation (P2):** Automated recording of initial pipeline states during `CommandList::Reset(alloc, pInitialState)`, ensuring draw calls replayed without subsequent `SetPipelineState` retain the active PSO.
- **DXGI Swapchain Tearing Sanitization (P3):** Sanitized `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` (`0x200`) and `DXGI_PRESENT_ALLOW_TEARING` when falling back to `DXGI_SWAP_EFFECT_DISCARD` on D3D11.0 runtimes, eliminating `DXGI_ERROR_INVALID_CALL` crashes.
- **Root Signature Version 1.1 Multi-Space Remapping (P4):** Implemented deterministic linear offset remapping for multi-space descriptor ranges (`spaceStride = 16` for SRVs, `4` for CBVs/Samplers/UAVs) while keeping standard `space0` mappings 1:1. Added hardware slot boundary enforcement (`SRV < 128`, `Sampler < 16`, `CBV < 13` protecting staging buffer `cb13`, `UAV < 8`).
- **GPU Queue Completion & Fence Synchronization (P5):** Implemented authentic GPU completion synchronization via `D3D11Backend::WaitForGpuCompletion()` utilizing an `ID3D11Query` of type `D3D11_QUERY_EVENT` polled via `GetData()`. Injected GPU queue draining into `WrappedD3D12CommandQueue::Signal()` before advancing CPU fence values, eliminating asynchronous thread race conditions and deadlocks. Added `KeplerErrorCode::DeviceTimeout` (1003).
- **Monolithic PSO Burst Throttling & TDR Mitigation (P6):** Added compilation burst throttling triggering every 128 consecutive PSOs in `PSOHook` and `D3D11Backend`, flushing driver command queues and yielding thread slices (`SwitchToThread()`) to prevent WDDM watchdog timeouts (`0x887A0005`).
- **Palworld Investigation & 6-Gate Plan:** Conducted filesystem audit, engine RHI analysis, and established gates P10-A through P10-F for minimal shader substitution via cache injection.
- **Recovered 30-Game Target Plan:** Recovered all 30 Group B commercial titles into four standardized evidence categories (0 in A, 16 in B, 10 in C, 4 in D).
- **Capability-Driven Compatibility Architecture:** Audited 10 core subsystems to eliminate cross-workload regressions.

### Verified
- **111/111 Physical Hardware Regression Suite:** 100% pass rate across all 15 test executables on the physical NVIDIA GeForce GT 740 GPU with zero regressions.

---

## [0.8.0] - 2026-10-20 (Phase 8: Application Validation & Release Engineering)

### Added
- **30-Game Compatibility Matrix:** Comprehensive architectural feasibility analysis across 30 commercial PC games with strict 6-tier classification (`VERIFIED WORKING`, `PARTIALLY WORKING`, `TESTED - FAILED`, `UNTESTED - PLAUSIBLE`, `BLOCKED / INCOMPATIBLE`, `UNKNOWN`).
- **Controlled Application Validation Ladder:** Stage A micro-harness verification, Stage B commercial preflight protocols (*Palworld*, *The Witcher 3*, *Control*), and Stage C telemetry.
- **Evidence-Based Performance Profiling:** Microbenchmarks measuring persistent shader cache lookup (31.3x speedup), root constant staging suppression (2.2x speedup), command replay throughput (27.1 million commands/sec), and memory footprint (96 bytes/descriptor).
- **Comprehensive Documentation Suite:** Publication-grade `README.md` with Mermaid pipeline diagram, `docs/PHASE8_BASELINE_AUDIT.md`, `docs/PHASE8_GAME_COMPATIBILITY_MATRIX.md`, `docs/PHASE8_APPLICATION_VALIDATION.md`, `docs/PHASE8_PERFORMANCE_REPORT.md`, `docs/PHASE8_KNOWN_LIMITATIONS.md`, `docs/PHASE8_RELEASE_CHECKLIST.md`, and `docs/PHASE8_RELEASE_CANDIDATE_REPORT.md`.
- **Community Governance:** GitHub issue templates (`bug_report.md`, `game_compatibility.md`, `feature_request.md`), `CONTRIBUTING.md`, `SECURITY.md`, and `LICENSE`.
- **Local Release Package:** Stripped production proxy DLL (`d3d12.dll`, 1.69 MB), default `kepler_bridge.ini`, quick-start guide, and SHA256 checksums.

### Verified
- **107/107 Test Baseline:** 100% pass rate across all 15 regression test executables on physical NVIDIA GeForce GT 740 hardware.
- **Strict Zero Third-Party Application Dependencies:** Direct PE import inspection confirmed reliance solely on standard Windows operating system DLLs.

---

## [0.7.0] - 2026-10-20 (Phase 7: Advanced Optimization & Persistent Shader Caching)

### Added
- **Multi-Slot UAV Binding:** Full support for up to 8 concurrent UAV slots in compute and graphics pipelines.
- **Atomic Counter Handling:** Proper handling of `D3D11_BUFFER_UAV_FLAG_COUNTER` with counter preservation and explicit reset sentinels (`0xFFFFFFFF`).
- **Resource-Aware UAV Barriers:** Dirty-range tracking that issues UAV flushes only when an actual write hazard exists.
- **Persistent Shader Disk Caching:** Multi-process `.shader_cache/*.dxbc` disk caching using 64-bit non-cryptographic FNV-1a hashing, reducing compilation latency from 5.78 ms to 0.18 ms.
- **Automated Corruption Recovery:** Checksum validation detecting tampered cache files on disk, safely evicting corrupt entries and triggering recompilation without crashing.
- **Production Diagnostics Subsystem:** Structured diagnostic reports with stable error codes (`KB_ERR_*`), 10 MB active log rotation to `.old`, and user path redaction (`Users\[USER]\`).
- **Regression Suites:** Added `test_d3d12_multi_uav.exe` (13/13 Passed), `test_shader_cache_persistence.exe` (5/5 Passed), and `test_perf_profile.exe` (4/4 Passed). Total tests increased from 80 to 107.

---

## [0.6.1] - 2026-10-20 (Phase 6.1: Binding Hardening & Application Validation)

### Added
- **Compute Shader Dispatch & Readback:** End-to-end compute dispatch pipeline with CPU staging buffer readback, verified numerically via `(id * 7) + 13` mathematical proof (`test_d3d12_compute.exe`, 12/12 Passed).
- **Descriptor Table Hardening:** Preserved heap type, base handle, range offsets, shader register spaces, and visibility masks during command replay.
- **Shader Disk Cache Architecture:** Initial multi-process disk cache design with compiler flag invalidation.
- **Palworld & Commercial Preflight Analysis:** Documented Unreal Engine 5.1 D3D12RHI startup behavior on FL 11_0 hardware.

---

## [0.6.0] - 2026-10-20 (Phase 6: Shader Compatibility & Binding Correctness)

### Added
- **Bounded Shader Translator:** Dynamic compilation of HLSL source code to SM 5.0 DXBC via `d3dcompiler_47.dll`.
- **Dynamic Constant Staging Buffer (`cb13`):** 256-byte persistent scratch buffer for root constants, eliminating per-draw buffer allocations.
- **DXIL Honest Rejection:** Robust rejection of unsupported Shader Model 6.x DXIL bytecode without fabricating fake success.
- **Regression Suite:** Added `test_shader_compat.exe` (7/7 Passed). Total tests reached 64/64.

---

## [0.5.0] - 2026-10-20 (Phase 5: Production Hardening & COM Safety)

### Added
- **COM Lifecycle Safety:** Safe teardown on process detach, preventing access violations during normal DLL unload or repeated initialization.
- **DXGI Swapchain Pacing:** Validated swapchain wrapping, present pacing, and buffer resize handling (`test_dxgi_swapchain.exe`, 6/6 Passed).
- **Regression Suite:** Added `test_dxgi_swapchain.exe` and `test_d3d11_standalone.exe`. Total tests reached 51/51.

---

## [0.4.0] - 2026-10-20 (Phase 4: Translation Engine & Command Recording)

### Added
- **In-Memory Command Batcher:** Recording of draw calls, pipeline state changes, and resource barriers.
- **D3D11 Backend Replay:** High-throughput command replay translating D3D12 batches to D3D11 immediate context.
- **Resource State Tracking:** Transition hazard tracking between Common, Render Target, and Shader Resource states.
- **Regression Suite:** Added `test_d3d12_rendering.exe` (11/11 Passed) and `test_d3d12_translation.exe` (7/7 Passed).

---

## [0.3.0] - 2026-10-19 (Phase 3: DXBC Shader Inspector)

### Added
- **DXBC Container Parser:** Parsing of RDEF, ISGN, OSGN, and SHDR chunks in legacy DXBC shaders.
- **Policy Engine Clamping:** Resource clamping for buffer dimensions and texture array sizes exceeding FL 11_0 limits.
- **Regression Suite:** Added `test_shader_inspector.exe` (11/11 Passed).

---

## [0.2.0] - 2026-10-19 (Phase 2: Live Hardware Feature Level Probing)

### Added
- **Feature Level Spoofing:** Configurable spoofing returning `D3D_FEATURE_LEVEL_12_0` or `12_1` during capability queries.
- **Configuration Engine (`kepler_bridge.ini`):** INI file parser for runtime tuning.
- **Regression Suite:** Added `test_d3d12_compat.exe` (3/3 Passed) and `test_policy_engine.exe` (6/6 Passed).

---

## [0.1.0] - 2026-10-19 (Phase 1: Ghost Hook Architecture)

### Added
- **Ghost Hook Proxy:** Export of canonical D3D12 symbols and interception of `D3D12CreateDevice`.
- **Passthrough Loader:** Dynamic loading of native `C:\Windows\System32\d3d12.dll` for unhandled operations.
- **Initial Test Suite:** Added `test_d3d12.exe` (1/1 Passed) and `test_pso_interception.exe` (5/5 Passed).
