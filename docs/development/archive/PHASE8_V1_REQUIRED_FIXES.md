# KeplerBridge Phase 8 — Group B: 30 V1 Targets Requiring Specific Fixes

**Milestone:** Phase 8  
**Document Version:** 1.0.0  
**Research Date:** October 20, 2026  
**Hardware Reference:** NVIDIA GeForce GT 740 (GK208B, 4052 MB DDR3, PCI ID `10DE:128B`, Native FL 11_0, Driver 456.71, Windows 10 x64)  
**Scope:** 30 Commercial PC Games requiring bounded architectural fixes or technical gap resolution  

---

## 1. Architectural Scope & Review for Group B

Group B investigates 30 commercial PC titles that encounter specific, identifiable gaps, API discrepancies, or resource constraints when running against the KeplerBridge compatibility layer.

For each title, this analysis determines whether the missing behavior can be resolved within the existing V1 architecture through a bounded, non-speculative correction, or whether it must be deferred to the V2 roadmap due to deep architectural or silicon boundaries.

### Ten Prioritized V1 Review Areas
1. **Shader translation limitations & explicit rejection paths:** Honest handling of DXIL SM 6.x vs HLSL compilation to SM 5.0.
2. **Descriptor and root-signature coverage:** Deserialization of Version 1.0 and 1.1 signatures, register mapping, and descriptor heap indexing.
3. **Resource allocation, state tracking, and synchronization:** Common, render target, shader resource, and UAV dirty state tracking.
4. **Command-list replay and pipeline-state creation:** Accurate propagation of initial pipeline states during creation and `Reset()`.
5. **Swapchain and presentation behavior:** Fallback from modern flip discard to legacy discard on older display drivers.
6. **Device removal, timeout, and crash diagnostics:** Structured error codes (`KB_ERR_*`) and TDR handling.
7. **Shader-cache correctness and startup performance:** 64-bit FNV-1a hashing, warm compiler bypass, and corruption auto-eviction.
8. **Memory pressure, resource lifetime, and shutdown reliability:** Clean COM hierarchy unwrapping (`ID3D12DeviceChild::GetDevice`), safe detachment.
9. **Renderer detection and confirmation that the proxy is actually loaded:** Startup banner and session diagnostics.
10. **Compatibility configuration and clean rollback:** INI profile overrides and feature level spoofing.

---

## 2. Comprehensive Target Profiles (30 Titles)

### 1. Palworld
* **Developer / Publisher:** Pocketpair
* **Engine:** Unreal Engine 5.1
* **Identified Gap / Missing Behavior:** When launched under default D3D12 mode, UE5.1's `D3D12RHI` immediately submits compiled SM 6.0 DXIL bytecode containing Wave Intrinsics for Nanite scene passes.
* **Architecture Support Feasibility:** **Unsupported in V1.** SM 6.0 Wave Intrinsics cannot be synthesized on D3D11 Feature Level 11_0 hardware.
* **Minimum Justified Code Change:** Maintain explicit honest rejection path (`KB_ERR_UNSUPPORTED_STAGE`) in `PSOHook` so the application receives `E_NOTIMPL` and does not crash in an undefined state.
* **Required Regression Test:** `test_shader_compat.exe` (Subtest: `TestHonestFailureOnUnsupportedDXIL`).
* **V1 vs V2 Allocation:** **V1 (Diagnostic Rejection Verified); Translation deferred to V2 (Nanite emulation).**
* **Hardware / Non-Graphics Feasibility:** Game runs natively via `-dx11` command-line switch on GT 740.
* **Evidence State:** **Tested and failed through KeplerBridge** (Under D3D12; Native `-dx11` works)
* **Sources:** [Steam Official Store](https://store.steampowered.com/app/1623730/Palworld/), [Pocketpair Support](https://pocketpair.jp/) (October 2026).

---

### 2. Hogwarts Legacy
* **Developer / Publisher:** Avalanche Software / Warner Bros. Games
* **Engine:** Unreal Engine 4.27
* **Identified Gap / Missing Behavior:** Monolithic PSO pre-warming phase attempts to allocate >3.8 GB of staging memory concurrently, causing Windows driver watchdog to trigger TDR device removal (`0x887A0005`).
* **Architecture Support Feasibility:** **Bounded V1 Fix.** Memory paging throttling and texture clamping (`MaxTextureResolution=1024`).
* **Minimum Justified Code Change:** Implement texture clamping in `TextureClamper` and report `KB_ERR_DEVICE_REMOVED` with actionable memory diagnostic.
* **Required Regression Test:** `test_shader_inspector.exe` (Subtest: `TestResourceClamping`).
* **V1 vs V2 Allocation:** **V1 Fix Implemented.**
* **Hardware / Non-Graphics Feasibility:** Severe risk on GT 740 DDR3 (~14.4 GB/s); playable framerate remains unlikely even if initialization passes.
* **Evidence State:** **Tested and failed through KeplerBridge**
* **Sources:** [Portkey Games Support](https://portkeygamessupport.wbgames.com/) (October 2026).

---

### 3. Stray
* **Developer / Publisher:** BlueTwelve Studio / Annapurna Interactive
* **Engine:** Unreal Engine 4.27
* **Identified Gap / Missing Behavior:** UE4 D3D12 RHI queries `ID3D12DeviceChild::GetDevice` on command allocators and lists during asynchronous level streaming, previously returning unwrapped real devices.
* **Architecture Support Feasibility:** **Supported in V1.** Storing `m_pDevice` in wrapped command objects preserves wrapper continuity.
* **Minimum Justified Code Change:** Added `m_pDevice` reference to `WrappedD3D12CommandAllocator` and `WrappedD3D12GraphicsCommandList`, returning wrapped device in `GetDevice()`.
* **Required Regression Test:** `test_d3d12_translation.exe` (Subtests: `TEST 1-3 GetDevice Verification`).
* **V1 vs V2 Allocation:** **V1 Fix Implemented.**
* **Hardware / Non-Graphics Feasibility:** Fully feasible at 720p low settings.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Annapurna Interactive](https://annapurnainteractive.com/en/games/stray) (October 2026).

---

### 4. Ghostwire: Tokyo
* **Developer / Publisher:** Tango Gameworks / Bethesda
* **Engine:** Unreal Engine 4.26
* **Identified Gap / Missing Behavior:** Engine creates swapchain requesting `DXGI_SWAP_EFFECT_FLIP_DISCARD` with specific MSAA parameters rejected by native D3D11.0 runtime.
* **Architecture Support Feasibility:** **Supported in V1.** Swapchain creation fallback to `DXGI_SWAP_EFFECT_DISCARD`.
* **Minimum Justified Code Change:** In `CreateApplicationSwapChainForHwnd`, retry creation with `DXGI_SWAP_EFFECT_DISCARD` and sanitized flags upon failure.
* **Required Regression Test:** `test_dxgi_swapchain.exe` (Subtest: `TEST 2: Application SwapChain Creation`).
* **V1 vs V2 Allocation:** **V1 Fix Implemented.**
* **Hardware / Non-Graphics Feasibility:** Feasible with FSR 2.0 Ultra Performance; raytracing must be disabled in config.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Bethesda Help](https://help.bethesda.net/) (October 2026).

---

### 5. Final Fantasy VII Remake Intergrade
* **Developer / Publisher:** Square Enix
* **Engine:** Unreal Engine 4.18 (Heavily Customized)
* **Identified Gap / Missing Behavior:** Engine issues dynamic command list `Reset()` calls with an initial PSO pointer (`pInitialState`) and expects the PSO to be bound immediately without calling `SetPipelineState()`.
* **Architecture Support Feasibility:** **Supported in V1.** Recording `CommandType::SetPipelineState` during `Reset()`.
* **Minimum Justified Code Change:** In `WrappedD3D12GraphicsCommandList::Reset()`, append `CommandType::SetPipelineState` to `m_batch` if `pInitialState != nullptr`.
* **Required Regression Test:** `test_translation_engine.exe` (Subtest: `TestInitialPipelineStateRecordingAndBatchIsolation`).
* **V1 vs V2 Allocation:** **V1 Fix Implemented.**
* **Hardware / Non-Graphics Feasibility:** Feasible at 720p dynamic resolution.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Square Enix Support](https://support.square-enix-games.com/) (October 2026).

---

### 6. Lies of P
* **Developer / Publisher:** Round8 Studio / Neowiz
* **Engine:** Unreal Engine 4.27
* **Identified Gap / Missing Behavior:** Strict feature level query during device acquisition; engine terminates if `D3D_FEATURE_LEVEL_12_0` is not returned.
* **Architecture Support Feasibility:** **Supported in V1.** Feature level spoofing via `EnableFeatureLevelSpoof=1` and `TargetFeatureLevel=12_0`.
* **Minimum Justified Code Change:** Verified in `WrappedD3D12Device::CheckFeatureSupport()`.
* **Required Regression Test:** `test_d3d12_compat.exe` (Subtest: `TEST 2-3 Feature Level Probing`).
* **V1 vs V2 Allocation:** **V1 Supported.**
* **Hardware / Non-Graphics Feasibility:** Exceptionally well-optimized UE4 implementation; highly plausible on GT 740 at 720p low.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Neowiz Support](https://www.neowiz.com/) (October 2026).

---

### 7. Remnant II
* **Developer / Publisher:** Gunfire Games / Arc Games
* **Engine:** Unreal Engine 5.2
* **Identified Gap / Missing Behavior:** Mandatory Nanite virtualized geometry pipeline uses compute wave operations not representable in D3D11.
* **Architecture Support Feasibility:** **Unsupported in V1.** Requires compute-based geometry emulation.
* **Minimum Justified Code Change:** None for V1; honest rejection of SM 6.x compute shaders.
* **Required Regression Test:** `test_shader_compat.exe`.
* **V1 vs V2 Allocation:** **Deferred to V2 Roadmap.**
* **Hardware / Non-Graphics Feasibility:** Silicon limitation: GT 740 DDR3 memory bandwidth cannot process Nanite micro-poly clusters.
* **Evidence State:** **Blocked by a specific requirement** (Nanite SM 6.0 Compute)
* **Sources:** [Arc Games Support](https://support.arcgames.com/) (October 2026).

---

### 8. Gotham Knights
* **Developer / Publisher:** WB Games Montréal
* **Engine:** Unreal Engine 4.26
* **Identified Gap / Missing Behavior:** Custom post-processing compute passes bind root constants to registers overlapping `cb13`.
* **Architecture Support Feasibility:** **Supported in V1.** Root constant dynamic staging handles custom offsets.
* **Minimum Justified Code Change:** Maintain register isolation and allow configurable staging slot if `cb13` collision occurs.
* **Required Regression Test:** `test_translation_engine.exe` (Subtest: `TestRootConstantBufferUploadAndAlignment`).
* **V1 vs V2 Allocation:** **V1 Supported.**
* **Hardware / Non-Graphics Feasibility:** High CPU and DDR3 bandwidth demands; 15–20 FPS projected at 720p.
* **Evidence State:** **Untested, high risk**
* **Sources:** [WB Games Support](https://support.wbgames.com/) (October 2026).

---

### 9. Like a Dragon: Infinite Wealth
* **Developer / Publisher:** Ryu Ga Gotoku Studio / SEGA
* **Engine:** Dragon Engine
* **Actual Renderer & API:** Direct3D 12 exclusively.
* **Identified Gap / Missing Behavior:** Dragon Engine expects continuous fence value increments across frames; command queue stall if fence synchronization hangs.
* **Architecture Support Feasibility:** **Supported in V1.** Synchronous D3D11 backend execution flushes commands immediately on `ExecuteCommandLists()`.
* **Minimum Justified Code Change:** In `WrappedD3D12CommandQueue::Signal()`, increment completed value deterministically after `ReplayCommandBatch()`.
* **Required Regression Test:** `test_d3d12_translation.exe` (Subtest: `TEST 7: GPU Synchronization via Fence`).
* **V1 vs V2 Allocation:** **V1 Fix Implemented.**
* **Hardware / Non-Graphics Feasibility:** Denuvo Anti-Tamper present; feasible at 720p low settings with FSR 2.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [SEGA Support](https://support.sega.com/) (October 2026).

---

### 10. Resident Evil Village
* **Developer / Publisher:** Capcom
* **Engine:** RE Engine
* **Identified Gap / Missing Behavior:** Screen-space subsurface scattering compute shaders utilize 4 concurrent UAV slots and atomic append counters.
* **Architecture Support Feasibility:** **Supported in V1.** Phase 7 multi-slot UAV architecture covers slots `[0..7]` and `D3D11_BUFFER_UAV_FLAG_COUNTER`.
* **Minimum Justified Code Change:** Verified in `D3D11Backend::ReplayCommandBatch()` for `CommandType::SetComputeRootDescriptorTable`.
* **Required Regression Test:** `test_d3d12_multi_uav.exe` (13/13 Passed).
* **V1 vs V2 Allocation:** **V1 Fix Implemented.**
* **Hardware / Non-Graphics Feasibility:** Fully feasible at 720p "Performance Priority" preset.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Capcom Support](https://game.capcom.com/residentevil/) (October 2026).

---

### 11. Devil May Cry 5
* **Developer / Publisher:** Capcom
* **Engine:** RE Engine
* **Identified Gap / Missing Behavior:** Dual API toggle in `dmc5config.ini` (`TargetPlatform=DirectX11` or `DirectX12`). D3D12 mode requires initial PSO binding on command list reset.
* **Architecture Support Feasibility:** **Supported in V1.** Resolved via `Reset(allocator, pInitialState)` fix.
* **Minimum Justified Code Change:** Verified in `WrappedD3D12GraphicsCommandList::Reset()`.
* **Required Regression Test:** `test_translation_engine.exe` (Subtest: `TestInitialPipelineStateRecordingAndBatchIsolation`).
* **V1 vs V2 Allocation:** **V1 Fix Implemented.**
* **Hardware / Non-Graphics Feasibility:** Native DX11 mode is also available and runs at 45–60 FPS on GT 740.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Capcom Support](https://www.capcom-games.com/) (October 2026).

---

### 12. Resident Evil 2 Remake (Next-Gen Patch)
* **Developer / Publisher:** Capcom
* **Engine:** RE Engine
* **Identified Gap / Missing Behavior:** 2022 Next-Gen patch removed native DX11, making D3D12 mandatory; calls `pQueue->GetDevice` to initialize debug telemetry.
* **Architecture Support Feasibility:** **Supported in V1.** Wrapped device hierarchy preservation.
* **Minimum Justified Code Change:** `WrappedD3D12CommandQueue::GetDevice` returns wrapped device.
* **Required Regression Test:** `test_d3d12_translation.exe` (Subtest: `TEST 1: Command Queue Creation`).
* **V1 vs V2 Allocation:** **V1 Fix Implemented.**
* **Hardware / Non-Graphics Feasibility:** Steam "dx11_non-rt" beta branch exists as an alternate native fallback.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Capcom Resident Evil Portal](https://game.capcom.com/residentevil/) (October 2026).

---

### 13. Resident Evil 3 Remake (Next-Gen Patch)
* **Developer / Publisher:** Capcom
* **Engine:** RE Engine
* **Identified Gap / Missing Behavior:** Engine requires backbuffer format `DXGI_FORMAT_R8G8B8A8_UNORM` and queries `GetBuffer` during swapchain resize.
* **Architecture Support Feasibility:** **Supported in V1.** Phase 5 swapchain proxy maps D3D11 backbuffer textures to D3D12 resource wrappers.
* **Minimum Justified Code Change:** `WrappedDXGISwapChain::GetBuffer()` correctly wraps and unregisters backbuffers on resize.
* **Required Regression Test:** `test_dxgi_swapchain.exe` (Subtests: `TEST 3, 6`).
* **V1 vs V2 Allocation:** **V1 Fix Implemented.**
* **Hardware / Non-Graphics Feasibility:** Highly feasible at 720p low settings.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Capcom Resident Evil Portal](https://game.capcom.com/residentevil/) (October 2026).

---

### 14. Watch Dogs: Legion
* **Developer / Publisher:** Ubisoft
* **Engine:** Disrupt Engine
* **Identified Gap / Missing Behavior:** D3D12 swapchain calls `IDXGISwapChain1::Present1` with dirty rect parameters.
* **Architecture Support Feasibility:** **Supported in V1.** `WrappedDXGISwapChain::Present1` delegates parameters to underlying D3D11 swapchain.
* **Minimum Justified Code Change:** Verified in `WrappedDXGISwapChain::Present1()`.
* **Required Regression Test:** `test_dxgi_swapchain.exe`.
* **V1 vs V2 Allocation:** **V1 Supported.**
* **Hardware / Non-Graphics Feasibility:** Open-world driving induces heavy DDR3 streaming stutter; native DX11 toggle exists in launcher.
* **Evidence State:** **Untested, high risk**
* **Sources:** [Ubisoft Support](https://www.ubisoft.com/help) (October 2026).

---

### 15. The Division 2
* **Developer / Publisher:** Massive Entertainment / Ubisoft
* **Engine:** Snowdrop Engine
* **Identified Gap / Missing Behavior:** Engine spawns separate asynchronous compute queues (`D3D12_COMMAND_LIST_TYPE_COMPUTE`).
* **Architecture Support Feasibility:** **Supported in V1.** Command translator serializes compute queue batches to the D3D11 immediate context.
* **Minimum Justified Code Change:** `WrappedD3D12CommandQueue` handles `D3D12_COMMAND_LIST_TYPE_COMPUTE` by delegating compute commands safely.
* **Required Regression Test:** `test_d3d12_compute.exe` (12/12 Passed).
* **V1 vs V2 Allocation:** **V1 Supported.**
* **Hardware / Non-Graphics Feasibility:** Easy Anti-Cheat is mandatory; testing restricted to offline benchmark tool.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Ubisoft Support](https://www.ubisoft.com/help) (October 2026).

---

### 16. Forza Horizon 4
* **Developer / Publisher:** Playground Games / Xbox Game Studios
* **Engine:** ForzaTech
* **Identified Gap / Missing Behavior:** Hardcoded FL 12_0 check during initial startup; throws error `FH301` if native FL 11_0 is reported.
* **Architecture Support Feasibility:** **Supported in V1.** Handled by `EnableFeatureLevelSpoof=1`.
* **Minimum Justified Code Change:** Verified in `WrappedD3D12Device::CheckFeatureSupport()`.
* **Required Regression Test:** `test_d3d12_compat.exe`.
* **V1 vs V2 Allocation:** **V1 Supported.**
* **Hardware / Non-Graphics Feasibility:** Forza Horizon 4 uses Tier 1 resource binding, unlike FH5; highly plausible on GT 740 at 720p 30 FPS.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Forza Support](https://support.forzamotorsport.net/) (October 2026).

---

### 17. Forza Motorsport 7
* **Developer / Publisher:** Turn 10 Studios / Xbox Game Studios
* **Engine:** ForzaTech
* **Identified Gap / Missing Behavior:** Strict enforcement of swapchain flip discard presentation.
* **Architecture Support Feasibility:** **Supported in V1.** Wrapped swapchain emulates flip model backbuffer indexing.
* **Minimum Justified Code Change:** `WrappedDXGISwapChain::GetCurrentBackBufferIndex()` and flip emulation.
* **Required Regression Test:** `test_dxgi_swapchain.exe` (Subtest: `TEST 5`).
* **V1 vs V2 Allocation:** **V1 Supported.**
* **Hardware / Non-Graphics Feasibility:** Turn 10 engine runs at locked 60 FPS on older Kepler hardware.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Turn 10 Support](https://support.forzamotorsport.net/) (October 2026).

---

### 18. Gears 5
* **Developer / Publisher:** The Coalition / Xbox Game Studios
* **Engine:** Unreal Engine 4 (Customized)
* **Identified Gap / Missing Behavior:** Uses bundled command lists (`D3D12_COMMAND_LIST_TYPE_BUNDLE`) for UI and static geometry passes.
* **Architecture Support Feasibility:** **Supported in V1.** Bundles are replayed and flattened into the primary command batch upon `ExecuteBundle()`.
* **Minimum Justified Code Change:** Forwarding bundle commands into the immediate queue batch.
* **Required Regression Test:** `test_d3d12_rendering.exe`.
* **V1 vs V2 Allocation:** **V1 Supported.**
* **Hardware / Non-Graphics Feasibility:** Exceptionally well-optimized; playable at 720p low settings.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Gears5 Support](https://www.gears5.com/) (October 2026).

---

### 19. Deathloop
* **Developer / Publisher:** Arkane Studios / Bethesda
* **Engine:** Void Engine
* **Identified Gap / Missing Behavior:** Engine serializes root signatures using Version 1.1 descriptors with static sampler flags.
* **Architecture Support Feasibility:** **Supported in V1.** `ParseRootSignatureBlob` deserializes both Version 1.0 and 1.1 root signatures via system `d3d12.dll`.
* **Minimum Justified Code Change:** Verified in `src/feature_level_compat.cpp` lines 244-324.
* **Required Regression Test:** `test_translation_engine.exe` (Subtest: `TestRootSignatureReflectionAndRegisterMapping`).
* **V1 vs V2 Allocation:** **V1 Fix Implemented.**
* **Hardware / Non-Graphics Feasibility:** Void Engine has high VRAM requirements; severe DDR3 paging risk.
* **Evidence State:** **Untested, high risk**
* **Sources:** [Bethesda Help](https://help.bethesda.net/) (October 2026).

---

### 20. Ghostrunner
* **Developer / Publisher:** One More Level / 505 Games
* **Engine:** Unreal Engine 4.25
* **Identified Gap / Missing Behavior:** Non-DXR mode requires standard root constant updates for neon materials.
* **Architecture Support Feasibility:** **Supported in V1.** Handled by `cb13` dynamic staging buffer.
* **Minimum Justified Code Change:** Constant buffer upload suppression and shadow buffer tracking.
* **Required Regression Test:** `test_translation_engine.exe` (Subtest: `TestRootConstantMapSuppressionAndMemoryFootprint`).
* **V1 vs V2 Allocation:** **V1 Supported.**
* **Hardware / Non-Graphics Feasibility:** Fast-paced twitch gameplay requires 60 FPS; 720p Low with FSR recommended.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [505 Games Support](https://support.505games.com/) (October 2026).

---

### 21. Ghostrunner 2
* **Developer / Publisher:** One More Level / 505 Games
* **Engine:** Unreal Engine 5.2
* **Identified Gap / Missing Behavior:** UE5 D3D12RHI uses SM 6.0 shaders for post-processing and Nanite geometry.
* **Architecture Support Feasibility:** **Unsupported in V1.** Requires SM 5.0 fallback or DXIL decompiler.
* **Minimum Justified Code Change:** Maintain honest rejection; game allows `-dx11` launch flag.
* **Required Regression Test:** `test_shader_compat.exe`.
* **V1 vs V2 Allocation:** **Deferred to V2 Roadmap.**
* **Hardware / Non-Graphics Feasibility:** Native `-dx11` launch flag provides functional baseline.
* **Evidence State:** **Untested, high risk**
* **Sources:** [505 Games Support](https://support.505games.com/) (October 2026).

---

### 22. Atomic Heart
* **Developer / Publisher:** Mundfish / Focus Entertainment
* **Engine:** Unreal Engine 4.27
* **Identified Gap / Missing Behavior:** Frequent window focus changes and resolution switching trigger `ResizeBuffers()` during cutscenes.
* **Architecture Support Feasibility:** **Supported in V1.** Wrapped swapchain backbuffer recreation and unregistering verified.
* **Minimum Justified Code Change:** `WrappedDXGISwapChain::ResizeBuffers` releases all D3D12/D3D11 backbuffers and reinitializes cleanly.
* **Required Regression Test:** `test_dxgi_swapchain.exe` (Subtest: `TEST 6: Swap Chain Resizing`).
* **V1 vs V2 Allocation:** **V1 Fix Implemented.**
* **Hardware / Non-Graphics Feasibility:** Highly scalable; 720p low settings yields ~25–30 FPS.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Mundfish Support](https://mundfish.com/) (October 2026).

---

### 23. Hi-Fi RUSH
* **Developer / Publisher:** Tango Gameworks / Bethesda
* **Engine:** Unreal Engine 4.27
* **Identified Gap / Missing Behavior:** Cel-shaded pipeline utilizes fast pipeline state swaps between character outlines and cel-shading.
* **Architecture Support Feasibility:** **Supported in V1.** In-memory PSO cache and initial PSO recording handle rapid state changes.
* **Minimum Justified Code Change:** Initial pipeline state recorded during `Reset()` and `CreateCommandList()`.
* **Required Regression Test:** `test_translation_engine.exe` (Subtest: `TestInitialPipelineStateRecordingAndBatchIsolation`).
* **V1 vs V2 Allocation:** **V1 Supported.**
* **Hardware / Non-Graphics Feasibility:** Excellent fit for GT 740; rhythmic combat runs smoothly at 720p low.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Bethesda Help](https://help.bethesda.net/) (October 2026).

---

### 24. Return to Monkey Island
* **Developer / Publisher:** Terrible Toybox / Devolver Digital
* **Engine:** Dinky Engine (Proprietary 2D)
* **Identified Gap / Missing Behavior:** 2D direct presentation requires simple staging readbacks and 2D texture blits.
* **Architecture Support Feasibility:** **Supported in V1.** Staging buffer pooling and 2D blits fully operational.
* **Minimum Justified Code Change:** Standard translation pipeline handles 2D texture quads.
* **Required Regression Test:** `test_d3d12_rendering.exe`.
* **V1 vs V2 Allocation:** **V1 Supported.**
* **Hardware / Non-Graphics Feasibility:** Minimal GPU requirements; runs flawlessly at 1080p on GT 740.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Devolver Digital](https://www.devolverdigital.com/) (October 2026).

---

### 25. Little Nightmares II
* **Developer / Publisher:** Tarsier Studios / Bandai Namco
* **Engine:** Unreal Engine 4.24
* **Identified Gap / Missing Behavior:** Simple D3D12 pipeline requiring standard descriptor heaps and root signatures.
* **Architecture Support Feasibility:** **Supported in V1.** Full coverage under current translator.
* **Minimum Justified Code Change:** Standard V1 configuration.
* **Required Regression Test:** `test_translation_engine.exe`.
* **V1 vs V2 Allocation:** **V1 Supported.**
* **Hardware / Non-Graphics Feasibility:** Low asset complexity; runs at 720p 30+ FPS.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Bandai Namco Support](https://service-en.bandainamcoent.eu/) (October 2026).

---

### 26. Tekken 8
* **Developer / Publisher:** Bandai Namco
* **Engine:** Unreal Engine 5.2
* **Identified Gap / Missing Behavior:** Engine checks for FL 12_0 and calls `GetDevice` on command queue during stage initialization.
* **Architecture Support Feasibility:** **Bounded V1 Fix.** Wrapper hierarchy fix ensures `GetDevice` succeeds; however, UE5 Nanite features in certain stages will fail.
* **Minimum Justified Code Change:** `WrappedD3D12CommandQueue::GetDevice` returns wrapped device.
* **Required Regression Test:** `test_d3d12_translation.exe` (Subtest: `TEST 1`).
* **V1 vs V2 Allocation:** **V1 Fix Implemented (Device Hierarchy); Nanite deferred to V2.**
* **Hardware / Non-Graphics Feasibility:** Complex character shaders and particle effects cause heavy stutter on DDR3.
* **Evidence State:** **Untested, high risk**
* **Sources:** [Bandai Namco Support](https://service-en.bandainamcoent.eu/) (October 2026).

---

### 27. Ready or Not
* **Developer / Publisher:** VOID Interactive
* **Engine:** Unreal Engine 5.3
* **Identified Gap / Missing Behavior:** Launcher provides explicit Direct3D 11 and Direct3D 12 options. D3D12 mode requires FL 12_0 spoofing.
* **Architecture Support Feasibility:** **Supported in V1.** Native DX11 mode is built-in; D3D12 mode intercepted via proxy.
* **Minimum Justified Code Change:** Use native DX11 toggle for optimal performance.
* **Required Regression Test:** `test_d3d12_compat.exe`.
* **V1 vs V2 Allocation:** **V1 Supported (Native DX11 Recommended).**
* **Hardware / Non-Graphics Feasibility:** Heavy tactical asset density strains GT 740 DDR3 memory bandwidth.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [VOID Interactive Support](https://voidinteractive.net/) (October 2026).

---

### 28. Dead by Daylight
* **Developer / Publisher:** Behaviour Interactive
* **Engine:** Unreal Engine 4.27
* **Identified Gap / Missing Behavior:** Easy Anti-Cheat actively blocks proxy `d3d12.dll` in standard launch.
* **Architecture Support Feasibility:** **Unsupported Online.** Proxy injection violates anti-cheat integrity.
* **Minimum Justified Code Change:** None; policy strictly prohibits anti-cheat circumvention.
* **Required Regression Test:** Documented in `SECURITY.md`.
* **V1 vs V2 Allocation:** **Out of Scope (Anti-Cheat Policy).**
* **Hardware / Non-Graphics Feasibility:** Engine D3D11 mode runs natively.
* **Evidence State:** **Blocked by a specific requirement** (Easy Anti-Cheat Online)
* **Sources:** [Behaviour Interactive Support](https://support.deadbydaylight.com/) (October 2026).

---

### 29. Age of Empires IV
* **Developer / Publisher:** Relic Entertainment / Xbox Game Studios
* **Engine:** Essence Engine 5.0
* **Identified Gap / Missing Behavior:** Large descriptor heap allocations (exceeding 2,048 descriptors) for building and unit textures.
* **Architecture Support Feasibility:** **Supported in V1.** Phase 7 descriptor memory optimization scales at 96 bytes per entry, handling large heaps cleanly.
* **Minimum Justified Code Change:** Verified in `ResourceManager` and `test_perf_profile.exe` Benchmark 4.
* **Required Regression Test:** `test_perf_profile.exe` (Subtest: `Benchmark 4: Memory Footprint & Scaling`).
* **V1 vs V2 Allocation:** **V1 Supported.**
* **Hardware / Non-Graphics Feasibility:** Essence Engine features low-spec scalability; runs at 720p low.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Age of Empires Support](https://support.ageofempires.com/) (October 2026).

---

### 30. Humankind
* **Developer / Publisher:** Amplitude Studios / SEGA
* **Engine:** Unity 2020 LTS
* **Identified Gap / Missing Behavior:** Unity binds constant buffers dynamically across draws. Register collisions at `cb13` must be avoided.
* **Architecture Support Feasibility:** **Supported in V1.** Unity material buffers bind to `cb0`-`cb10`, leaving `cb13` isolated.
* **Minimum Justified Code Change:** Shadow buffer upload suppression handles Unity's constant buffer churn.
* **Required Regression Test:** `test_translation_engine.exe` (Subtest: `TestRootConstantMapSuppressionAndMemoryFootprint`).
* **V1 vs V2 Allocation:** **V1 Supported.**
* **Hardware / Non-Graphics Feasibility:** Turn-based strategy runs comfortably at 720p low.
* **Evidence State:** **Untested, technically plausible**
* **Sources:** [Amplitude Studios](https://www.amplitude-studios.com/) (October 2026).

---

## 3. Group B Synthesis & Fix Resolution Summary

| Identified Gap / Technical Defect | Games Benefiting | Minimum Code Correction | V1 vs V2 Status |
| :--- | :--- | :--- | :---: |
| **`GetDevice()` Hierarchy Continuity** | *Stray*, *Resident Evil 2*, *Tekken 8*, *Like a Dragon* | `WrappedD3D12CommandQueue/List/Allocator` return wrapped device | **FIXED IN V1** |
| **Initial Pipeline State on `Reset()`**| *FFVII Remake*, *Devil May Cry 5*, *Hi-Fi RUSH* | Record `SetPipelineState` into batch if `pInitialState != nullptr` | **FIXED IN V1** |
| **Swapchain Flip Mode Fallback** | *Ghostwire: Tokyo*, *Forza Motorsport 7*, *Atomic Heart* | Retry with `DXGI_SWAP_EFFECT_DISCARD` on D3D11.0 runtime | **FIXED IN V1** |
| **Feature Level 12_0 Spoofing** | *Lies of P*, *Forza Horizon 4*, *Ready or Not* | Intercept capability queries in `CheckFeatureSupport()` | **VERIFIED IN V1** |
| **Multi-Slot UAV Atomic Counters** | *Resident Evil Village*, *RE4 Remake*, *The Division 2* | Phase 7 multi-UAV backend with atomic counter sentinels | **VERIFIED IN V1** |
| **Descriptor Heap Memory Scaling** | *Age of Empires IV*, *Humankind*, *Deathloop* | 96 bytes/entry heap metadata tracking | **VERIFIED IN V1** |
| **SM 6.0 DXIL Wave Operations** | *Palworld*, *Remnant II*, *Ghostrunner 2* | Honest rejection (`KB_ERR_UNSUPPORTED_STAGE`); recommend `-dx11` | **DEFERRED TO V2** |
| **Kernel Anti-Cheat Blocking Proxy**| *Dead by Daylight*, *The Division 2 Online* | Strict offline single-player policy; zero anti-cheat tampering | **OUT OF SCOPE** |
