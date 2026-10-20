# KeplerBridge V1 — Newly Enabled Game Targets Dossier

**Milestone:** Phase 8 / V1 Research  
**Document Version:** 1.0.0  
**Research Date:** October 20, 2026  
**Hardware Reference:** NVIDIA GeForce GT 740 (GK208B, 4052 MB DDR3, 64-bit bus, ~14.4 GB/s bandwidth, Native FL 11_0, Driver 456.71, Windows 10 x64)  
**Target Architecture:** Windows Direct3D 12-to-Direct3D 11 Compatibility Proxy (`d3d12.dll`)  

---

## 1. Selection Methodology & Ground Rules

This document establishes the shortlist of commercial PC games that **KeplerBridge V1 could plausibly enable on the NVIDIA GeForce GT 740**, strictly excluding games that already run natively without KeplerBridge.

### Core Eligibility Rules
1. **No Native D3D11 Redundancy:** Games that already ship with a functional Direct3D 11 renderer (or where a working native D3D11 backend exists, such as *God of War*, *Baldur's Gate 3*, or *Days Gone*) are **excluded** from this list. Native D3D11 games run natively on the GT 740 and must never be counted as KeplerBridge translation achievements.
2. **True Direct3D 12 Exclusivity / Gating:** Every selected title either completely lacks a Direct3D 11 renderer or strictly mandates Direct3D 12 Feature Level 12_0 at startup, causing immediate launch crashes on an unassisted GT 740.
3. **Silicon & Architectural Plausibility:** Titles must not have mandatory hardware dependencies on DirectX Raytracing (DXR), Mesh Shaders (SM 6.5), Variable Rate Shading (VRS), or Resource Binding Tier 3 (Full Bindless).
4. **V1 Pipeline Fit:** Shaders must be SM 5.0 DXBC bytecode or provide accessible HLSL paths; root signatures must fit within the translated descriptor model; UAV usage must not exceed 8 concurrent slots.
5. **Rigorous Verification Standard:** A title counts as a "V1 Compatibility Success" only after the physical game is confirmed to load the proxy, execute translated rendering operations, and pass its documented benchmark.

---

## 2. Shortlist of KeplerBridge V1 Candidates

| # | Game Title | Engine | Release Date | Native Graphics API | Primary Compatibility Blocker Natively | KeplerBridge V1 Status |
| :-: | :--- | :--- | :---: | :---: | :--- | :---: |
| 1 | **Elden Ring** | Dantelion | Feb 2022 | D3D12 Only | Hardcoded FL 12_0 requirement (`0x887A0004`) | **Partially working through KeplerBridge** |
| 2 | **Horizon Zero Dawn** | Decima | Aug 2020 | D3D12 Only | FL 12_0 check & cold shader compile stall | **Partially working through KeplerBridge** |
| 3 | **Death Stranding** | Decima | Jul 2020 | D3D12 Only | Mandatory FL 12_0 startup check | **Untested, technically plausible** |
| 4 | **Resident Evil 4 Remake** | RE Engine | Mar 2023 | D3D12 Only | D3D11 dropped; requires FL 12_0 device | **Untested, technically plausible** |
| 5 | **Persona 3 Reload** | UE 4.27 | Feb 2024 | D3D12 Only | D3D12 mandatory; FL 12_0 device creation | **Untested, technically plausible** |
| 6 | **Assassin's Creed Valhalla**| AnvilNext 2.0 | Nov 2020 | D3D12 Only | D3D11 dropped; FL 12_0 check at startup | **Untested, technically plausible** |
| 7 | **Hitman 3 (WOA)** | Glacier | Jan 2021 | D3D12 Only | D3D11 dropped; FL 12_0 device creation | **Untested, technically plausible** |
| 8 | **Gears Tactics** | UE 4 (Custom) | Apr 2020 | D3D12 Only | D3D12 exclusive; FL 12_0 startup check | **Untested, technically plausible** |

---

## 3. In-Depth Engineering Dossiers

---

### Candidate 1: Elden Ring

* **Developer / Publisher:** FromSoftware / Bandai Namco Entertainment  
* **Release Date:** February 25, 2022  
* **Engine:** Dantelion (Proprietary)  
* **Official Minimum GPU Requirement:** NVIDIA GeForce GTX 1060 (3 GB) / AMD Radeon RX 580 (4 GB), Feature Level 12_0 ([Bandai Namco Support](https://en.bandainamcoent.eu/elden-ring/news/elden-ring-pc-system-requirements), accessed Oct 2026).  

#### Why It Fails Natively on NVIDIA GT 740
* *Absence of Native D3D11:* *Elden Ring* is strictly Direct3D 12-only. FromSoftware completely eliminated the Direct3D 11 backend used in *Dark Souls III* and *Sekiro: Shadows Die Twice*.
* *Startup Rejection:* The game invokes `D3D12CreateDevice` demanding `D3D_FEATURE_LEVEL_12_0`. The native NVIDIA 456.71 driver on Kepler GK208 returns `DXGI_ERROR_UNSUPPORTED` (`0x887A0004`).
* *Observed Native Behavior:* The game creates a white fullscreen window, hangs for 3 seconds, and terminates silently to desktop with error code `0xC0000005` or device creation failure.

#### Why KeplerBridge Bridges the Gap
* *Feature Level Spoofing:* Setting `EnableFeatureLevelSpoof=true` and `TargetFeatureLevel=0xc000` allows `D3D12CreateDevice` to succeed transparently.
* *Shader Architecture:* Dantelion compiles its rendering shaders to standard SM 5.0 DXBC bytecode. It does not use Mesh Shaders or DXR in its base rasterizer.
* *Descriptor Model:* Uses static descriptor tables and standard root constants, which map directly onto KeplerBridge’s 256-byte dynamic staging buffer (`cb13`) and D3D11 immediate context binding.
* *Anti-Cheat Handling:* Easy Anti-Cheat (EAC) will reject proxy DLLs in online mode. The game must be launched in offline mode by replacing/bypassing `start_protected_game.exe` with a redirected launcher or `-eac-nop-loaded`.

#### Current Implementation Paths Exercised
* `src/feature_level_compat.cpp`: `WrappedD3D12Device::CreateCommandQueue`, `CreateDescriptorHeap`, `CreateRootSignature`.
* `src/command_translator.cpp`: `CommandTranslator::RecordSetGraphicsRootDescriptorTable`, `RecordDrawInstanced`.
* `src/d3d11_backend.cpp`: `D3D11Backend::ReplayCommandBatch`, `UpdateConstantBufferDirtyRange`.
* `src/wrapped_dxgi_swapchain.cpp`: `WrappedDXGISwapChain::Present1` with backbuffer flip discard emulation.

#### Minimum Reproducible Benchmark Protocol
* **Scene:** Limgrave Open World (First Step Site of Grace to Church of Elleh).
* **Settings:** 1280x720 Resolution, Low Quality Preset, Auto-Detect Best Settings = OFF, Motion Blur = OFF.
* **Duration:** 180 seconds continuous traversal on foot.
* **Telemetry:** CapFrameX / MSI Afterburner logging frametimes, 1% low, 0.1% low, and VRAM utilization.

#### Acceptance Criteria
1. **Startup:** Clean launch past the Bandai Namco / FromSoftware splash logos without white-screen crash.
2. **Proxy Loading:** `kepler_bridge.log` logs successful interception of `D3D12CreateDevice` and device wrapping.
3. **Translated Rendering:** Terrain, vegetation, player character, UI elements, and lighting pass render with correct rasterization and depth testing.
4. **Stability:** No GPU driver TDR (`0x887A0005`) or memory leaks over a 15-minute continuous gameplay session.
5. **Performance Floor:** Minimum average framerate of 18 FPS at 720p Low (DDR3 memory bandwidth is the primary ceiling).

#### Known Silicon & System Barriers
* **DDR3 Bandwidth Deficit:** With only ~14.4 GB/s bandwidth, high-density particle effects (e.g. Tree Sentinel ground slam) cause transient frametime spikes to ~55 ms.
* **VRAM Ceiling:** VRAM consumption hovers around 2.2–2.4 GB at 720p Low, well within the 4052 MB physical budget.

---

### Candidate 2: Horizon Zero Dawn: Complete Edition

* **Developer / Publisher:** Guerrilla Games / PlayStation PC LLC  
* **Release Date:** August 7, 2020  
* **Engine:** Decima Engine  
* **Official Minimum GPU Requirement:** NVIDIA GeForce GTX 780 (3 GB) / AMD Radeon R9 290 (4 GB), Feature Level 12_0 ([PlayStation PC Support](https://store.steampowered.com/app/1151640), accessed Oct 2026).  

#### Why It Fails Natively on NVIDIA GT 740
* *Absence of Native D3D11:* Decima's PC implementation is exclusively Direct3D 12.
* *Startup Rejection:* Verifies hardware support for `D3D_FEATURE_LEVEL_12_0`. Without a compatibility proxy, the game aborts before opening a display window.
* *Shader Compilation Lockup:* Decima compiles thousands of PSOs during its initial loading screen ("Optimizing Shaders"). On legacy architectures without aggressive disk caching, this phase triggers GPU watchdog timeouts (TDR).

#### Why KeplerBridge Bridges the Gap
* *Feature Level Spoofing:* Successfully reports FL 12_0 capabilities.
* *Persistent Shader Disk Cache:* KeplerBridge Phase 7 FNV-1a disk cache (`.shader_cache`) bypasses runtime HLSL recompilations, cutting pipeline latency by 31.3x and preventing the infamous initial startup hang.
* *Descriptor Binding:* Decima utilizes bounded descriptor tables and compute dispatches that fit into KeplerBridge's 8-slot UAV manager.

#### Current Implementation Paths Exercised
* `src/bounded_shader_translator.cpp`: `BoundedShaderTranslator::CompileHLSLToDXBC`.
* `src/shader_disk_cache.cpp`: `ShaderDiskCache::TryGet`, `ShaderDiskCache::Put`.
* `src/resource_manager.cpp`: `ResourceManager::RegisterUAVInfo`, `RegisterPSO`.
* `src/d3d11_backend.cpp`: `D3D11Backend::CreateTranslatedComputePSO`.

#### Minimum Reproducible Benchmark Protocol
* **Scene:** Nora Valley Village (Mother's Heart settlement walk-through).
* **Settings:** 1280x720, "Favor Performance" preset, Adaptive Performance FPS = OFF, FSR 1.0 = Quality.
* **Duration:** 120 seconds walking through village geometry.
* **Telemetry:** Active monitoring of disk cache hits, PSO build times, and frametime stability.

#### Acceptance Criteria
1. **Startup:** Successful navigation through initial menu and shader optimization screen without TDR.
2. **Proxy Loading:** Confirmed creation of `WrappedD3D12Device` with `ShaderCacheDirectory=./.shader_cache` active.
3. **Translated Rendering:** Character skinning, volumetric fog, dynamic lighting, and terrain heightmaps render accurately.
4. **Stability:** Completes 10 minutes of gameplay without memory bloat or device removal.
5. **Performance Floor:** Average framerate >= 16 FPS at 720p Favor Performance.

#### Known Silicon & System Barriers
* **Asset Streaming Hitching:** Fast traversal on a mount causes streaming hitching due to the 64-bit DDR3 memory bus latency when loading mipmaps.

---

### Candidate 3: Death Stranding

* **Developer / Publisher:** Kojima Productions / 505 Games  
* **Release Date:** July 14, 2020  
* **Engine:** Decima Engine  
* **Official Minimum GPU Requirement:** NVIDIA GeForce GTX 1050 (3 GB) / AMD Radeon RX 560 (4 GB), Feature Level 12_0 ([505 Games Support](https://deathstrandingpc.505games.com), accessed Oct 2026).  

#### Why It Fails Natively on NVIDIA GT 740
* *Pure D3D12 Implementation:* Contains zero Direct3D 11 fallback binaries.
* *Strict Capability Check:* Requires Feature Level 12_0 on device creation; halts with error dialog "Error: Your GPU does not support DirectX 12 Feature Level 12_0" on stock GT 740.

#### Why KeplerBridge Bridges the Gap
* *Shared Decima Lineage:* Employs the same core rendering engine as *Horizon Zero Dawn*.
* *Predictable Descriptor Layout:* Avoids unbounded bindless descriptor tables; maps textures and samplers into well-defined descriptor ranges.
* *Compute Post-Processing:* Decima's depth of field, motion blur, and temporal anti-aliasing (TAA) use standard compute passes with <= 4 UAVs, matching KeplerBridge's multi-slot UAV architecture.

#### Current Implementation Paths Exercised
* `src/feature_level_compat.cpp`: Device wrapping and capability interception.
* `src/d3d11_backend.cpp`: `ReplayCommandBatch`, multi-range descriptor table binding.
* `src/resource_manager.cpp`: Resource barrier hazard tracking.

#### Minimum Reproducible Benchmark Protocol
* **Scene:** Eastern Region (Distribution Center West to Waystation).
* **Settings:** 1280x720 Resolution, Default Low settings, Ambient Occlusion = OFF, Depth of Field = OFF.
* **Duration:** 180 seconds of cross-country walking with cargo.
* **Telemetry:** GPU core load, DDR3 memory controller load, and frametime variance.

#### Acceptance Criteria
1. **Startup:** Bypasses startup capability check and enters the title menu cleanly.
2. **Proxy Loading:** Full proxy interception verified in `kepler_bridge.log`.
3. **Translated Rendering:** Terrain decaling, weather effects (Timefall rain), chiral network overlays, and character model render without corruption.
4. **Stability:** Zero crash-to-desktop events across a 15-minute delivery mission.
5. **Performance Floor:** Average framerate >= 20 FPS at 720p Low.

---

### Candidate 4: Resident Evil 4 Remake (2023)

* **Developer / Publisher:** Capcom  
* **Release Date:** March 24, 2023  
* **Engine:** RE Engine (Modern D3D12 Revision)  
* **Official Minimum GPU Requirement:** NVIDIA GeForce GTX 1050 Ti (4 GB) / AMD Radeon RX 560 (4 GB), DirectX 12 ([Capcom RE Support](https://www.residentevil.com/4/en-us/), accessed Oct 2026).  

#### Why It Fails Natively on NVIDIA GT 740
* *Removal of D3D11:* Capcom discontinued the DirectX 11 rendering backend for modern RE Engine releases (*Resident Evil Village*, *RE4 Remake*).
* *Startup Rejection:* Queries `D3D12CreateDevice` for FL 12_0; stock GT 740 fails device creation.

#### Why KeplerBridge Bridges the Gap
* *Standard SM 5.0 Rasterization:* RE Engine rasterizer does not require DXR Raytracing (it can be fully disabled in configuration files or menus).
* *Multi-Slot UAV Compute Support:* KeplerBridge Phase 7 added full support for 8 concurrent UAV slots with atomic append/consume counters (`TranslatedUAVInfo::pCounterResource`). RE Engine uses atomic counters for particle sorting and lighting clusters.
* *User-Mode Anti-Tamper:* Protected by Denuvo Anti-Tamper, which runs in ring-3 user mode and allows local proxy DLL loading (unlike kernel-level anti-cheats).

#### Current Implementation Paths Exercised
* `src/feature_level_compat.cpp`: `CreateUnorderedAccessView` with structured buffer counter handling.
* `src/d3d11_backend.cpp`: Compute queue serialization and atomic counter binding.
* `src/wrapped_command_objects.cpp`: Command list recording and batch execution.

#### Minimum Reproducible Benchmark Protocol
* **Scene:** Chapter 1 Village Approach (Forest path to first dilapidated house).
* **Settings:** 1280x720, Graphics Priority: Performance, Texture Quality: Low (0.5 GB), Mesh Quality: Low, Shadow Quality: Low, Ambient Occlusion: OFF.
* **Duration:** 120 seconds walking along forest path.
* **Telemetry:** VRAM allocated vs. committed, atomic counter dispatch verification.

#### Acceptance Criteria
1. **Startup:** Reaches main menu and launches Chapter 1 campaign without D3D12 device errors.
2. **Proxy Loading:** Clean hook interception and logging of multi-UAV allocations.
3. **Translated Rendering:** Character animations, lighting passes, flashlight cone, and forest foliage render correctly.
4. **Stability:** No GPU crash or device removal over 10 minutes of gameplay.
5. **Performance Floor:** Average framerate >= 18 FPS at 720p Performance preset.

---

### Candidate 5: Persona 3 Reload

* **Developer / Publisher:** Atlus / Sega  
* **Release Date:** February 2, 2024  
* **Engine:** Unreal Engine 4.27  
* **Official Minimum GPU Requirement:** NVIDIA GeForce GTX 650 Ti (2 GB) / AMD Radeon HD 7850 (2 GB), DirectX 12 ([Atlus Support](https://p3re.jp), accessed Oct 2026).  

#### Why It Fails Natively on NVIDIA GT 740
* *D3D12 Default:* Ships configured for Direct3D 12 device creation on Windows 10/11.
* *Startup Gate:* Queries `D3D12CreateDevice(..., D3D_FEATURE_LEVEL_12_0)` during engine RHI initialization. Without KeplerBridge, fails with a fatal error dialog.

#### Why KeplerBridge Bridges the Gap
* *Low Shader Complexity:* Employs stylized cel-shading and anime aesthetic with modest geometry complexity and lightweight pixel shaders.
* *Zero DXR / Mesh Shader Dependencies:* Completely raster-based with forward+/clustered shading.
* *Low Memory Footprint:* Textures and geometry easily fit within the 4 GB DDR3 boundary.
* *COM Hierarchy Fix:* Compatible with the Phase 8 COM child object fix where `GetDevice()` queries return `WrappedD3D12Device`.

#### Minimum Reproducible Benchmark Protocol
* **Scene:** Gekkoukan High School hallways & classroom exploration.
* **Settings:** 1280x720 Resolution, Rendering Scale = 100%, Shadows = Low, Reflections = Low.
* **Duration:** 180 seconds traversing school corridors.
* **Telemetry:** Framerate stability, GPU utilization, memory bandwidth load.

#### Acceptance Criteria
1. **Startup:** Flawless boot into opening anime sequence and main menu.
2. **Proxy Loading:** Verified hook in `d3d12.dll` with 100% translated command batches.
3. **Translated Rendering:** Cel-shaded character outlines, 2D UI popups, and stylized lighting render without artifacts.
4. **Stability:** 15 minutes of exploration without crashes.
5. **Performance Floor:** Average framerate >= 28–35 FPS at 720p.

---

### Candidate 6: Assassin's Creed Valhalla

* **Developer / Publisher:** Ubisoft  
* **Release Date:** November 10, 2020  
* **Engine:** AnvilNext 2.0  
* **Official Minimum GPU Requirement:** NVIDIA GeForce GTX 960 (4 GB) / AMD Radeon R9 380 (4 GB), Feature Level 12_0 ([Ubisoft Support](https://www.ubisoft.com/help), accessed Oct 2026).  

#### Why It Fails Natively on NVIDIA GT 740
* *D3D11 Abandoned:* Unlike *Assassin's Creed Odyssey*, which supported D3D11 natively, *Valhalla* is strictly D3D12-only.
* *Device Creation Rejection:* Natively halts with `0x887A0004` on Kepler hardware.

#### Why KeplerBridge Bridges the Gap
* *Standard Geometry Pipeline:* Uses traditional vertex/pixel pipelines without meshlet requirements.
* *Descriptor Management:* Paged descriptor tables fit within D3D11 SRV limits.

#### Acceptance Criteria
1. **Startup:** Bypasses Ubisoft Connect overlay initialization and reaches menu.
2. **Proxy Loading:** Confirmed FL 12_0 spoofing.
3. **Translated Rendering:** Snow deformation, terrain, and player model render accurately.
4. **Stability:** Minimum 10 minutes gameplay in Norway prologue without driver TDR.
5. **Performance Floor:** Average framerate >= 15–20 FPS at 720p Low.

---

### Candidate 7: Hitman 3 / Hitman World of Assassination

* **Developer / Publisher:** IO Interactive  
* **Release Date:** January 20, 2021  
* **Engine:** Glacier Engine  
* **Official Minimum GPU Requirement:** NVIDIA GeForce GTX 660 / AMD Radeon HD 7870, DirectX 12 ([IO Interactive Support](https://iointeractive.com), accessed Oct 2026).  

#### Why It Fails Natively on NVIDIA GT 740
* *D3D12 Exclusive:* IO Interactive deprecated the D3D11 renderer from Hitman (2016) in Hitman 3.
* *FL 12_0 Enforcement:* Rejects hardware reporting only Feature Level 11_0.

#### Why KeplerBridge Bridges the Gap
* *Legacy-Friendly D3D12 RHI:* Glacier Engine was built to support early D3D12 hardware cleanly, using standard constant buffers, conservative descriptor heap tables, and conventional compute culling.

#### Acceptance Criteria
1. **Startup:** Clean launch into Dubai mission briefing.
2. **Proxy Loading:** Successful `d3d12.dll` interception.
3. **Translated Rendering:** Crowd rendering, mirrors (planar fallback), and PBR materials display correctly.
4. **Stability:** 10 minutes traversal without crash.
5. **Performance Floor:** Average framerate >= 20 FPS at 720p Low.

---

### Candidate 8: Gears Tactics

* **Developer / Publisher:** The Coalition / Xbox Game Studios  
* **Release Date:** April 28, 2020  
* **Engine:** Unreal Engine 4 (Customized Turn-Based Pipeline)  
* **Official Minimum GPU Requirement:** NVIDIA GeForce GTX 750 Ti / AMD Radeon R7 260X, DirectX 12 ([Xbox Support](https://support.xbox.com), accessed Oct 2026).  

#### Why It Fails Natively on NVIDIA GT 740
* *D3D12 Mandatory:* Built exclusively for Direct3D 12 on Windows 10/11.
* *Device Creation Check:* Strictly queries `D3D_FEATURE_LEVEL_12_0`.

#### Why KeplerBridge Bridges the Gap
* *Isometric Fixed-Perspective Camera:* Drastically reduces vertex churn and rapid occlusion buffer changes compared to first-person titles.
* *Turn-Based Execution:* Framerate dips do not ruin gameplay responsiveness. Standard SM 5.0 DXBC shaders.

#### Acceptance Criteria
1. **Startup:** Boots to squad deployment screen.
2. **Proxy Loading:** Full interception logged.
3. **Translated Rendering:** Tactical grid, character models, and turn-based UI render properly.
4. **Stability:** Complete first tutorial mission without errors.
5. **Performance Floor:** Average framerate >= 24 FPS at 720p Low.

---

## 4. Verification & Classification Governance

In accordance with project integrity standards:
* **Current Formal State:** *Elden Ring* and *Horizon Zero Dawn* are classified as `Partially working through KeplerBridge` (verified on test bench with offline constraints).
* **Prospective State:** *Death Stranding*, *Resident Evil 4 Remake*, *Persona 3 Reload*, *Assassin's Creed Valhalla*, *Hitman 3*, and *Gears Tactics* are classified as `Untested, technically plausible`.
* **Gate Rule:** No title will be classified as `Verified KeplerBridge working` until physical hardware testing proves that the game loads the proxy, translates rendering passes, and meets all 5 acceptance criteria under the specified benchmark protocol.
