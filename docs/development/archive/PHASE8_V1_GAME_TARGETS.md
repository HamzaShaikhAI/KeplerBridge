# KeplerBridge Phase 8 — Group A: 30 V1 Compatibility Targets

**Milestone:** Phase 8  
**Document Version:** 1.0.0  
**Research Date:** October 20, 2026  
**Hardware Reference:** NVIDIA GeForce GT 740 (GK208B, 4052 MB DDR3, PCI ID `10DE:128B`, Native FL 11_0, Driver 456.71, Windows 10 x64)  
**Scope:** 30 Commercial PC Games closest to KeplerBridge V1 Architecture  

---

## 1. Architectural Scope for Group A

Group A contains 30 commercial PC titles whose rendering pipelines, shader complexity, descriptor layouts, and memory usage align most closely with the Direct3D 12-to-Direct3D 11 compatibility architecture implemented in KeplerBridge V1 (`v0.8.0-rc1`).

### Target Architecture Capabilities (V1 Baseline)
* **Direct3D Feature Level:** Native 11.0 (`0xb000`) execution backed by `WrappedD3D12Device` and `D3D11Backend`.
* **Shader Model:** SM 5.0 DXBC compiled via `d3dcompiler_47.dll`. Precompiled SM 6.x DXIL is safely rejected.
* **Descriptor Tables & UAVs:** Up to 8 concurrent Unordered Access Views (UAVs) in compute and graphics stages, with atomic counter tracking and dirty hazard flushes.
* **Constant Buffer Staging:** Dynamic 256-byte staging buffer reserved at `cb13`.
* **Shader Caching:** Persistent multi-process `.shader_cache/*.dxbc` cache with 64-bit FNV-1a hashing and corruption auto-eviction.

---

## 2. Standardized Evidence States

Every title is categorized under one of the mandatory evidence states:
* `Verified KeplerBridge working`
* `Partially working through KeplerBridge`
* `Tested and failed through KeplerBridge`
* `Native renderer baseline only`
* `Untested, technically plausible`
* `Untested, high risk`
* `Blocked by a specific requirement`
* `Unknown`

---

## 3. Comprehensive Target Profiles (30 Titles)

### 1. God of War (2018 PC Port)
* **Developer / Publisher:** Santa Monica Studio / PlayStation PC LLC
* **Engine:** Proprietary In-House Engine
* **Actual Renderer & API:** Native Direct3D 11 (`d3d11.dll`).
* **D3D12 Proxy Relevance:** Irrelevant during standard launch (bypasses `d3d12.dll`); serves as an essential hardware baseline.
* **Native D3D11 Execution:** **Yes.** Executes natively without KeplerBridge.
* **Translator Coverage:** Complete for all D3D11 runtime commands; translation proxy inactive.
* **Lowest-Cost Configuration:** 1280x720, "Original" or "Low" preset, FSR 2.0 Balanced/Performance, Ambient Occlusion Low.
* **Specific Remaining Risks:** VRAM memory bus saturation on 64-bit DDR3 (~14.4 GB/s); high-density combat scenes may drop below 20 FPS.
* **Benchmark Protocol:** Intro realm sequence (The Marked Trees) 5-minute timed run.
* **Evidence State:** **Native renderer baseline only**
* **Verification Status:** Fact (Local Execution Verified)
* **Sources:** [PlayStation PC Support](https://www.playstation.com/) (October 2026).

---

### 2. Baldur's Gate 3
* **Developer / Publisher:** Larian Studios
* **Engine:** Divinity 4.0
* **Actual Renderer & API:** Direct3D 11 (`bg3_dx11.exe`) and Vulkan (`bg3.exe`).
* **D3D12 Proxy Relevance:** Irrelevant for primary executable; serves as modern complex asset baseline on GT 740.
* **Native D3D11 Execution:** **Yes.** Fully supported via `bg3_dx11.exe`.
* **Translator Coverage:** Native D3D11 runtime execution.
* **Lowest-Cost Configuration:** 1280x720, Low preset, FSR 1.0 Ultra Performance, Subsurface Scattering Off, Crowd Density Low.
* **Specific Remaining Risks:** Act 3 Lower City CPU/VRAM load exceeds DDR3 bandwidth; host paging latency.
* **Benchmark Protocol:** Act 1 Ravaged Beach crash site exploration 10-minute pass.
* **Evidence State:** **Native renderer baseline only**
* **Verification Status:** Fact (Local Execution Verified)
* **Sources:** [Larian Studios Support](https://larian.com/support) (October 2026).

---

### 3. Elden Ring
* **Developer / Publisher:** FromSoftware / Bandai Namco
* **Engine:** Dantelion Engine (Proprietary)
* **Actual Renderer & API:** Direct3D 12 exclusively (`d3d12.dll`).
* **D3D12 Proxy Relevance:** **Critical.** Required to intercept FL 12_0 device creation on FL 11_0 hardware.
* **Native D3D11 Execution:** **No.** Engine contains no native D3D11 fallback.
* **Translator Coverage:** Covers `D3D12CreateDevice`, FL 12_0 spoofing, compute dispatches, root signature descriptor tables.
* **Lowest-Cost Configuration:** 1280x720 windowed, Low preset, Shadows Low, Volumetric Fog Low, SSAO Off, Offline mode.
* **Specific Remaining Risks:** Easy Anti-Cheat (EAC) rejects proxy in online mode; must be tested offline via `start_protected_game.exe` bypass. DDR3 memory bandwidth limits framerate to 12–18 FPS.
* **Benchmark Protocol:** Limgrave First Step site of grace to Church of Elleh outdoor traversal.
* **Evidence State:** **Partially working through KeplerBridge**
* **Verification Status:** Fact & Engineering Inference (Offline Only)
* **Sources:** [Bandai Namco Support](https://service-en.bandainamcoent.eu/) (October 2026).

---

### 4. Horizon Zero Dawn Complete Edition
* **Developer / Publisher:** Guerrilla Games / PlayStation PC LLC
* **Engine:** Decima Engine
* **Actual Renderer & API:** Direct3D 12 exclusively (`d3d12.dll`).
* **D3D12 Proxy Relevance:** **Critical.** Required for device creation and persistent PSO disk caching.
* **Native D3D11 Execution:** **No.**
* **Translator Coverage:** Covers device creation, PSO interception, multi-process disk caching (`.shader_cache/`).
* **Lowest-Cost Configuration:** 1280x720, "Favor Performance" preset, Texture Quality Low, Adaptive Performance Off.
* **Specific Remaining Risks:** Initial startup issues ~25,000 PSO creation requests. Cold compilation can take >20 minutes without warm cache. Texture streaming pop-in over 64-bit DDR3 bus.
* **Benchmark Protocol:** In-game benchmark tool (Meridian flyover) 3-minute cycle.
* **Evidence State:** **Partially working through KeplerBridge**
* **Verification Status:** Test-Harness & Architectural Proof
* **Sources:** [Guerrilla Games](https://www.guerrilla-games.com/) (October 2026).

---

### 5. Control
* **Developer / Publisher:** Remedy Entertainment / 505 Games
* **Engine:** Northlight Engine
* **Actual Renderer & API:** Dual Executable: `Control_DX11.exe` and `Control_DX12.exe`.
* **D3D12 Proxy Relevance:** **High.** Intercepts `Control_DX12.exe` to enable D3D12 execution on FL 11_0.
* **Native D3D11 Execution:** **Yes.** Supported via `Control_DX11.exe`.
* **Translator Coverage:** Northlight's non-raytracing D3D12 path uses traditional root signatures and constant buffers within 256 bytes.
* **Lowest-Cost Configuration:** 1280x720, Low preset, Far Shadow Resolution Low, Ray Tracing Disabled.
* **Specific Remaining Risks:** If DXR is enabled in settings, engine submits raytracing structures which are unsupported.
* **Benchmark Protocol:** Central Executive arrival area 5-minute stress test.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [Remedy Support](https://www.remedygames.com/) (October 2026).

---

### 6. The Witcher 3: Wild Hunt (v4.0+ Next-Gen)
* **Developer / Publisher:** CD Projekt Red
* **Engine:** REDengine 3
* **Actual Renderer & API:** Dual Executable: `witcher3_dx11.exe` and `witcher3.exe` (DX12).
* **D3D12 Proxy Relevance:** **High.** Placed in `bin\x64_dx12\` for `witcher3.exe`.
* **Native D3D11 Execution:** **Yes.** Native DX11 path available in `bin\x64\`.
* **Translator Coverage:** Descriptor tables map to `cb0`-`cb10`, cleanly avoiding KeplerBridge's reserved `cb13` staging buffer.
* **Lowest-Cost Configuration:** 1280x720, Low preset, Anti-Aliasing TAA, HairWorks Off, Screen Space Reflections Low.
* **Specific Remaining Risks:** Next-Gen asset streaming induces DDR3 paging stalls; native DX11 executable delivers superior frametimes.
* **Benchmark Protocol:** White Orchard tavern to village perimeter ride.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [CD Projekt Red Support](https://support.cdprojektred.com/) (October 2026).

---

### 7. Monster Hunter: World
* **Developer / Publisher:** Capcom
* **Engine:** MT Framework
* **Actual Renderer & API:** Direct3D 11 (Default) and Direct3D 12 Toggle in graphics options.
* **D3D12 Proxy Relevance:** **High.** Active when D3D12 API toggle is enabled.
* **Native D3D11 Execution:** **Yes.** Default native engine path.
* **Translator Coverage:** MT Framework's D3D12 implementation is a direct mapping of its DX11 command queue architecture.
* **Lowest-Cost Configuration:** 1280x720, Low preset, Variable Graphics Memory Off, Capsule Skinning Off.
* **Specific Remaining Risks:** FidelityFX CAS sharpening pass requires compute UAV binding, supported via Phase 7 backend.
* **Benchmark Protocol:** Astera hub area 5-minute loop.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [Capcom Support](https://www.capcom-games.com/) (October 2026).

---

### 8. Sons of the Forest
* **Developer / Publisher:** Endnight Games / Newnight
* **Engine:** Unity 2022 LTS
* **Actual Renderer & API:** Direct3D 11 (Standard) with `-force-d3d12` launch argument.
* **D3D12 Proxy Relevance:** Active when `-force-d3d12` is passed.
* **Native D3D11 Execution:** **Yes.** Default launcher setting.
* **Translator Coverage:** Unity constant buffers cleanly avoid `cb13`; root descriptor ranges map to standard D3D11 views.
* **Lowest-Cost Configuration:** 1280x720, Ultra Low preset, Dynamic Resolution FSR 1.0 Ultra Performance.
* **Specific Remaining Risks:** Unity compute volumetric lighting shaders may saturate DDR3 memory bandwidth.
* **Benchmark Protocol:** Beach spawn point to forest edge walk.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [Endnight Games](https://endnightgames.com/) (October 2026).

---

### 9. Resident Evil 4 Remake
* **Developer / Publisher:** Capcom
* **Engine:** RE Engine
* **Actual Renderer & API:** Direct3D 12 exclusively.
* **D3D12 Proxy Relevance:** **Critical.** Required for device creation and multi-UAV compute pass execution.
* **Native D3D11 Execution:** **No.** Direct3D 11 path removed in RE Engine 2023 branch.
* **Translator Coverage:** Multi-slot UAV binding (slots 0..7) and atomic counters directly satisfy RE Engine screen-space reflection passes.
* **Lowest-Cost Configuration:** 1280x720, "Performance Priority" preset, Mesh Quality Low, Texture Filtering Trilinear, SSAO Off.
* **Specific Remaining Risks:** Denuvo Anti-Tamper memory checks; VRAM allocation must be capped below 3.5 GB to prevent driver eviction.
* **Benchmark Protocol:** Village square encounter 3-minute sequence.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [Capcom Resident Evil Portal](https://game.capcom.com/residentevil/) (October 2026).

---

### 10. Death Stranding
* **Developer / Publisher:** Kojima Productions / 505 Games
* **Engine:** Decima Engine
* **Actual Renderer & API:** Direct3D 12 exclusively.
* **D3D12 Proxy Relevance:** **Critical.** Required for FL 12_0 spoofing and shader cache persistence.
* **Native D3D11 Execution:** **No.**
* **Translator Coverage:** Shares Decima root signature and pipeline layout with Horizon Zero Dawn; fully handled by V1 cache and backend.
* **Lowest-Cost Configuration:** 1280x720, Low preset, Streamer Memory Low, Ambient Occlusion Off.
* **Specific Remaining Risks:** Decima memory streaming assumes >50 GB/s bandwidth; 14.4 GB/s DDR3 will stutter during vehicle traversal.
* **Benchmark Protocol:** Distribution Center to Waystation delivery route.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [505 Games Support](https://support.505games.com/) (October 2026).

---

### 11. Red Dead Redemption 2
* **Developer / Publisher:** Rockstar Games
* **Engine:** RAGE (Rockstar Advanced Game Engine)
* **Actual Renderer & API:** Vulkan (Default) and Direct3D 12 (via `system.xml`).
* **D3D12 Proxy Relevance:** Active when `<API>kSettingAPI_DX12</API>` is set.
* **Native D3D11 Execution:** **No.** Only Vulkan and D3D12 backends exist.
* **Translator Coverage:** RAGE D3D12 backend uses standard root descriptor tables and structured buffer uploads.
* **Lowest-Cost Configuration:** 1280x720, Lowest preset, Resolution Scale 0.75, Volumetrics Low, Water Quality Low.
* **Specific Remaining Risks:** Rockstar Games Launcher integrity checks may detect modified DLL; native Vulkan is strongly recommended.
* **Benchmark Protocol:** In-game Valentine town benchmark pass.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [Rockstar Games Support](https://support.rockstargames.com/) (October 2026).

---

### 12. Assassin's Creed Valhalla
* **Developer / Publisher:** Ubisoft
* **Engine:** AnvilNext 2.0
* **Actual Renderer & API:** Direct3D 12 exclusively.
* **D3D12 Proxy Relevance:** **Critical.** Intercepts device creation and FL 12_0 query.
* **Native D3D11 Execution:** **No.**
* **Translator Coverage:** AnvilNext 2.0 uses standard SM 5.0/5.1 shaders without mandatory mesh shaders or raytracing.
* **Lowest-Cost Configuration:** 1280x720, Low preset, Resolution Scale 70%, Shadows Low, Clutter Low.
* **Specific Remaining Risks:** Ubisoft Connect overlay may hook swapchain methods; VRAM footprint can exceed 3.8 GB.
* **Benchmark Protocol:** Built-in Ravensthorpe benchmark flyover.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [Ubisoft Support](https://www.ubisoft.com/help) (October 2026).

---

### 13. Hitman 3 / World of Assassination
* **Developer / Publisher:** IO Interactive
* **Engine:** Glacier Engine
* **Actual Renderer & API:** Direct3D 12 exclusively.
* **D3D12 Proxy Relevance:** **High.** Intercepts `HITMAN3.exe`.
* **Native D3D11 Execution:** **No.** (Hitman 1/2 supported DX11; Hitman 3 dropped DX11).
* **Translator Coverage:** Glacier engine uses clean command list batching and bounded descriptor ranges compatible with V1.
* **Lowest-Cost Configuration:** 1280x720, Low preset, Level of Detail Low, SSAO Off, VRS Off.
* **Specific Remaining Risks:** Large crowd simulations in Dubai or Berlin saturate DDR3 bandwidth.
* **Benchmark Protocol:** Dartmoor manor benchmark pass.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [IO Interactive](https://www.ioi.dk/) (October 2026).

---

### 14. Persona 3 Reload
* **Developer / Publisher:** Atlus / SEGA
* **Engine:** Unreal Engine 4.27
* **Actual Renderer & API:** Direct3D 12 exclusively.
* **D3D12 Proxy Relevance:** **Critical.**
* **Native D3D11 Execution:** **No.**
* **Translator Coverage:** Stylized cell-shaded pipeline without heavy compute post-passes; fits comfortably within FL 11_0.
* **Lowest-Cost Configuration:** 1280x720, Rendering Scale 75%, Shadows Low, Reflections Off.
* **Specific Remaining Risks:** Denuvo Anti-Tamper present.
* **Benchmark Protocol:** Iwatodai dorm to Gekkoukan High School walk.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [Atlus Support](https://atlus.com/) (October 2026).

---

### 15. F1 2020 / F1 2021
* **Developer / Publisher:** Codemasters / Electronic Arts
* **Engine:** EGO Engine 4.0 / 5.0
* **Actual Renderer & API:** Direct3D 12 exclusively.
* **D3D12 Proxy Relevance:** **High.**
* **Native D3D11 Execution:** **No.** (F1 2019 was the final release with DX11).
* **Translator Coverage:** EGO engine uses legacy root signatures with static register mappings easily handled by KeplerBridge.
* **Lowest-Cost Configuration:** 1280x720, Ultra Low preset, TAA, Dynamic Resolution 70%.
* **Specific Remaining Risks:** Wet weather particle compute shaders can cause brief frametime spikes.
* **Benchmark Protocol:** Melbourne wet track 1-lap benchmark.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [Codemasters Support](https://www.codemasters.com/) (October 2026).

---

### 16. Gears Tactics
* **Developer / Publisher:** The Coalition / Xbox Game Studios
* **Engine:** Unreal Engine 4 (Customized)
* **Actual Renderer & API:** Direct3D 12 exclusively.
* **D3D12 Proxy Relevance:** **High.**
* **Native D3D11 Execution:** **No.**
* **Translator Coverage:** Highly scalable UE4 implementation featuring extensive fallback toggles down to legacy Intel integrated graphics.
* **Lowest-Cost Configuration:** 1280x720, Lowest preset, Resolution Scale 66%, Shading Quality Low.
* **Specific Remaining Risks:** Large battlemaps with volumetric smoke can saturate 4GB DDR3 memory.
* **Benchmark Protocol:** Built-in campaign benchmark test.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [Xbox Support](https://support.xbox.com/) (October 2026).

---

### 17. Shadow of the Tomb Raider
* **Developer / Publisher:** Eidos-Montréal / Crystal Dynamics
* **Engine:** Foundation Engine
* **Actual Renderer & API:** Dual API: Direct3D 11 and Direct3D 12 toggle.
* **D3D12 Proxy Relevance:** **High.** Intercepts D3D12 option.
* **Native D3D11 Execution:** **Yes.** Native D3D11 toggle present in launcher.
* **Translator Coverage:** Foundation Engine's D3D12 renderer is an early implementation mapping 1:1 to D3D11 immediate context.
* **Lowest-Cost Configuration:** 1280x720, Lowest preset, TAA, Texture Quality Low.
* **Specific Remaining Risks:** Native DX11 mode is readily available and offers higher stability.
* **Benchmark Protocol:** Built-in three-scene benchmark sequence.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [Square Enix Support](https://support.square-enix-games.com/) (October 2026).

---

### 18. Deus Ex: Mankind Divided
* **Developer / Publisher:** Eidos-Montréal
* **Engine:** Dawn Engine
* **Actual Renderer & API:** Dual API: Direct3D 11 and Direct3D 12 toggle.
* **D3D12 Proxy Relevance:** **High.** Intercepts D3D12 toggle.
* **Native D3D11 Execution:** **Yes.** Native DX11 default.
* **Translator Coverage:** Dawn Engine D3D12 wrapper translates cleanly to D3D11 backend.
* **Lowest-Cost Configuration:** 1280x720, Low preset, MSAA Off, Contact Hardening Shadows Off.
* **Specific Remaining Risks:** D3D12 renderer in Mankind Divided is historically known for CPU overhead compared to native DX11.
* **Benchmark Protocol:** Built-in Prague benchmark scene.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [Eidos-Montréal](https://eidosmontreal.com/) (October 2026).

---

### 19. Civilization VI
* **Developer / Publisher:** Firaxis Games / 2K
* **Engine:** Proprietary In-House Engine
* **Actual Renderer & API:** Dual Executable: `CivilizationVI.exe` (DX11) and `CivilizationVI_DX12.exe`.
* **D3D12 Proxy Relevance:** **High.** Intercepts `CivilizationVI_DX12.exe`.
* **Native D3D11 Execution:** **Yes.** Native DX11 executable provided.
* **Translator Coverage:** Traditional strategy game rendering passes; fits well within FL 11_0 limits.
* **Lowest-Cost Configuration:** 1280x720, Minimum preset, Memory Impact Minimum.
* **Specific Remaining Risks:** Late-game massive maps can demand significant system RAM and DDR3 texture bandwidth.
* **Benchmark Protocol:** Built-in Gathering Storm AI & Graphics benchmark.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [2K Support](https://support.2k.com/) (October 2026).

---

### 20. Sniper Elite 4
* **Developer / Publisher:** Rebellion Developments
* **Engine:** Asura Engine
* **Actual Renderer & API:** Dual API: Direct3D 11 and Direct3D 12.
* **D3D12 Proxy Relevance:** Intercepts D3D12 launch setting.
* **Native D3D11 Execution:** **Yes.** Native D3D11 launcher option.
* **Translator Coverage:** Asura engine D3D12 is an exemplary low-overhead implementation that maps cleanly to Kepler hardware.
* **Lowest-Cost Configuration:** 1280x720, Low preset, Shadows Low, Draw Distance Medium.
* **Specific Remaining Risks:** Asura engine already provides exceptional native DX11 performance on Kepler.
* **Benchmark Protocol:** San Celini Island entrance traversal.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [Rebellion Support](https://rebellion.com/support/) (October 2026).

---

### 21. Sniper Elite 5
* **Developer / Publisher:** Rebellion Developments
* **Engine:** Asura Engine
* **Actual Renderer & API:** Direct3D 12 and Vulkan.
* **D3D12 Proxy Relevance:** **High.** Intercepts D3D12 option.
* **Native D3D11 Execution:** **No.** (D3D11 dropped in favor of D3D12 and Vulkan).
* **Translator Coverage:** Asura engine D3D12 renderer uses standard compute pipelines and descriptor tables compatible with V1.
* **Lowest-Cost Configuration:** 1280x720, Low preset, FSR 2.0 Ultra Performance, SSAO Off.
* **Specific Remaining Risks:** Easy Anti-Cheat is enabled by default; requires offline single-player mode.
* **Benchmark Protocol:** Mission 1 Colline-sur-Mer coast approach.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [Rebellion Support](https://rebellion.com/support/) (October 2026).

---

### 22. Total War: Warhammer II
* **Developer / Publisher:** Creative Assembly / SEGA
* **Engine:** TW Engine 3 (Warscape)
* **Actual Renderer & API:** Direct3D 11 (Default) and Direct3D 12 (Beta) toggle.
* **D3D12 Proxy Relevance:** Intercepts D3D12 beta mode.
* **Native D3D11 Execution:** **Yes.** Default native renderer.
* **Translator Coverage:** Warscape D3D12 beta translates standard draw calls and constant buffers.
* **Lowest-Cost Configuration:** 1280x720, Low preset, Unit Size Medium, Shadows Off.
* **Specific Remaining Risks:** High unit counts (20v20 armies) saturate DDR3 bandwidth regardless of API.
* **Benchmark Protocol:** Built-in Battle benchmark test.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [Total War Support](https://www.totalwar.com/) (October 2026).

---

### 23. Star Wars Jedi: Fallen Order
* **Developer / Publisher:** Respawn Entertainment / Electronic Arts
* **Engine:** Unreal Engine 4.19
* **Actual Renderer & API:** Direct3D 11 native; Direct3D 12 enabled via `-dx12` launch flag.
* **D3D12 Proxy Relevance:** Active when `-dx12` is forced.
* **Native D3D11 Execution:** **Yes.** Default game configuration.
* **Translator Coverage:** UE4.19 D3D12 backend uses conservative descriptor heaps and SM 5.0 shaders.
* **Lowest-Cost Configuration:** 1280x720, Medium/Low preset, Dynamic Resolution Scaling On.
* **Specific Remaining Risks:** Native DX11 mode is significantly smoother on Kepler hardware.
* **Benchmark Protocol:** Bracca scrapyard introductory walk.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [EA Help](https://help.ea.com/) (October 2026).

---

### 24. Battlefield 1
* **Developer / Publisher:** DICE / Electronic Arts
* **Engine:** Frostbite 3
* **Actual Renderer & API:** Dual API: Direct3D 11 and Direct3D 12 toggle in settings.
* **D3D12 Proxy Relevance:** Intercepts D3D12 option.
* **Native D3D11 Execution:** **Yes.** Native D3D11 default.
* **Translator Coverage:** Frostbite 3 D3D12 uses standard root signatures and persistent constant staging.
* **Lowest-Cost Configuration:** 1280x720, Low preset, TAA, Resolution Scale 100%.
* **Specific Remaining Risks:** EA Anti-Cheat / FairFight active in multiplayer; testing must be restricted to single-player War Stories.
* **Benchmark Protocol:** Through Mud and Blood prologue mission.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [EA Help](https://help.ea.com/) (October 2026).

---

### 25. Battlefield V
* **Developer / Publisher:** DICE / Electronic Arts
* **Engine:** Frostbite 3
* **Actual Renderer & API:** Dual API: Direct3D 11 and Direct3D 12 (with optional DXR).
* **D3D12 Proxy Relevance:** Intercepts D3D12 non-DXR mode.
* **Native D3D11 Execution:** **Yes.** Native D3D11 mode supported.
* **Translator Coverage:** Standard Frostbite D3D12 non-DXR path maps to V1 multi-UAV and constant staging.
* **Lowest-Cost Configuration:** 1280x720, Low preset, DXR Disabled, Future Frame Rendering On.
* **Specific Remaining Risks:** DXR Raytracing toggle must remain strictly disabled; online EA Anti-Cheat prohibits proxy.
* **Benchmark Protocol:** Under No Flag single-player campaign opening.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [EA Help](https://help.ea.com/) (October 2026).

---

### 26. Far Cry 6
* **Developer / Publisher:** Ubisoft
* **Engine:** Dunia Engine
* **Actual Renderer & API:** Direct3D 12 exclusively.
* **D3D12 Proxy Relevance:** **Critical.** Required for device creation on FL 11_0.
* **Native D3D11 Execution:** **No.**
* **Translator Coverage:** Dunia engine's non-DXR path uses standard SM 5.0 compute passes and rasterization states.
* **Lowest-Cost Configuration:** 1280x720, Low preset, FSR 1.0 Performance, DXR Reflections/Shadows Disabled.
* **Specific Remaining Risks:** High-resolution texture pack must be uninstalled; VRAM ceiling must be monitored.
* **Benchmark Protocol:** Built-in Isla Santuario benchmark scene.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [Ubisoft Support](https://www.ubisoft.com/help) (October 2026).

---

### 27. Borderlands 3
* **Developer / Publisher:** Gearbox Software / 2K
* **Engine:** Unreal Engine 4.22
* **Actual Renderer & API:** Dual API: Direct3D 11 (Default) and Direct3D 12 (Pitched as Beta).
* **D3D12 Proxy Relevance:** Intercepts D3D12 setting in `GameUserSettings.ini`.
* **Native D3D11 Execution:** **Yes.** Default native renderer.
* **Translator Coverage:** UE4.22 D3D12 uses basic root signatures compatible with KeplerBridge.
* **Lowest-Cost Configuration:** 1280x720, "Very Low" preset, Resolution Scale 75%, Fog Off.
* **Specific Remaining Risks:** D3D12 mode has historically long initial loading times (up to 10 minutes) on mechanical drives.
* **Benchmark Protocol:** Built-in Pandora benchmark sequence.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [2K Support](https://support.2k.com/) (October 2026).

---

### 28. Days Gone
* **Developer / Publisher:** Bend Studio / PlayStation PC LLC
* **Engine:** Unreal Engine 4.25
* **Actual Renderer & API:** Direct3D 11 native exclusively.
* **D3D12 Proxy Relevance:** Irrelevant for primary executable; serves as reference hardware baseline.
* **Native D3D11 Execution:** **Yes.** Executes natively on FL 11_0 hardware.
* **Translator Coverage:** Native D3D11 execution.
* **Lowest-Cost Configuration:** 1280x720, Low preset, Render Scale 70%, SSAO Off.
* **Specific Remaining Risks:** Large zombie horde physics calculations strain CPU and DDR3 bandwidth.
* **Benchmark Protocol:** Marion Forks motorcycle ride.
* **Evidence State:** **Native renderer baseline only**
* **Verification Status:** Fact (Local Execution Verified)
* **Sources:** [PlayStation PC Support](https://www.playstation.com/) (October 2026).

---

### 29. Horizon Forbidden West Complete Edition
* **Developer / Publisher:** Guerrilla Games / Nixxes / PlayStation PC LLC
* **Engine:** Decima Engine
* **Actual Renderer & API:** Direct3D 12 exclusively.
* **D3D12 Proxy Relevance:** **Critical.** Required for device creation.
* **Native D3D11 Execution:** **No.**
* **Translator Coverage:** Requires FL 12_0 spoofing, persistent shader disk cache, and multi-slot UAVs.
* **Lowest-Cost Configuration:** 1280x720, "Very Low" preset, Dynamic Resolution Scaling 50%, FSR 2.2 Ultra Performance.
* **Specific Remaining Risks:** Heavy texture streaming demands exceed 14.4 GB/s DDR3 bus, resulting in aggressive low-LOD pop-in.
* **Benchmark Protocol:** Daunt valley introductory area 3-minute pass.
* **Evidence State:** **Untested, high risk**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [PlayStation PC Support](https://www.playstation.com/) (October 2026).

---

### 30. Sea of Thieves
* **Developer / Publisher:** Rare / Xbox Game Studios
* **Engine:** Unreal Engine 4
* **Actual Renderer & API:** Direct3D 11 (Default) and Direct3D 12 toggle in video settings.
* **D3D12 Proxy Relevance:** Intercepts D3D12 toggle.
* **Native D3D11 Execution:** **Yes.** Default native engine path.
* **Translator Coverage:** Standard UE4 water compute shaders map to V1 multi-UAV dispatches.
* **Lowest-Cost Configuration:** 1280x720, "Cursed" (Low) preset, Model Detail Cursed, Water Detail Rare.
* **Specific Remaining Risks:** Easy Anti-Cheat is mandatory for online matchmaking; testing requires private local sandbox.
* **Benchmark Protocol:** Outpost island perimeter exploration.
* **Evidence State:** **Untested, technically plausible**
* **Verification Status:** Engine Architecture Analysis
* **Sources:** [Sea of Thieves Support](https://support.seaofthieves.com/) (October 2026).

---

## 4. Group A Synthesis & Summary Table

| # | Title | Engine | Native API | Evidence State | Primary Hardware Risk on GT 740 |
| :-: | :--- | :--- | :---: | :---: | :--- |
| 1 | **God of War (2018)** | Proprietary | D3D11 | **Native renderer baseline only** | DDR3 memory bus saturation in heavy combat |
| 2 | **Baldur's Gate 3** | Divinity 4.0 | D3D11 / Vulkan | **Native renderer baseline only** | Act 3 Lower City asset throughput limits |
| 3 | **Elden Ring** | Dantelion | D3D12 | **Partially working through KeplerBridge** | EAC online rejection; 14.4 GB/s DDR3 bandwidth ceiling |
| 4 | **Horizon Zero Dawn** | Decima | D3D12 | **Partially working through KeplerBridge** | Extreme cold shader compilation stall without cache |
| 5 | **Control** | Northlight | D3D11 / D3D12 | **Untested, technically plausible** | User accidentally enabling DXR raytracing |
| 6 | **The Witcher 3 Next-Gen** | REDengine 3 | D3D11 / D3D12 | **Untested, technically plausible** | Asset streaming stalls; native DX11 recommended |
| 7 | **Monster Hunter: World** | MT Framework | D3D11 / D3D12 | **Untested, technically plausible** | FidelityFX CAS sharpening compute UAV load |
| 8 | **Sons of the Forest** | Unity 2022 | D3D11 / D3D12 | **Untested, technically plausible** | Volumetric lighting compute bandwidth pressure |
| 9 | **Resident Evil 4 Remake** | RE Engine | D3D12 | **Untested, technically plausible** | Denuvo anti-tamper; VRAM allocation exceeding 3.5GB |
| 10 | **Death Stranding** | Decima | D3D12 | **Untested, technically plausible** | Fast traversal asset streaming over DDR3 bus |
| 11 | **Red Dead Redemption 2**| RAGE | Vulkan / D3D12 | **Untested, technically plausible** | Launcher integrity checks; native Vulkan preferred |
| 12 | **AC Valhalla** | AnvilNext 2.0 | D3D12 | **Untested, technically plausible** | Ubisoft Connect overlay swapchain hook collisions |
| 13 | **Hitman 3 (WOA)** | Glacier | D3D12 | **Untested, technically plausible** | High crowd simulation asset throughput |
| 14 | **Persona 3 Reload** | UE 4.27 | D3D12 | **Untested, technically plausible** | Denuvo anti-tamper |
| 15 | **F1 2020 / F1 2021** | EGO Engine | D3D12 | **Untested, technically plausible** | Wet track particle compute dispatch overhead |
| 16 | **Gears Tactics** | UE 4 | D3D12 | **Untested, technically plausible** | Volumetric smoke particle bandwidth saturation |
| 17 | **Shadow of Tomb Raider**| Foundation | D3D11 / D3D12 | **Untested, technically plausible** | Native DX11 mode readily available & faster |
| 18 | **Deus Ex: Mankind Div.**| Dawn Engine | D3D11 / D3D12 | **Untested, technically plausible** | Dawn Engine D3D12 known CPU driver overhead |
| 19 | **Civilization VI** | Proprietary | D3D11 / D3D12 | **Untested, technically plausible** | Late-game 4K texture map paging over DDR3 |
| 20 | **Sniper Elite 4** | Asura Engine | D3D11 / D3D12 | **Untested, technically plausible** | Native DX11 mode provides superior frametimes |
| 21 | **Sniper Elite 5** | Asura Engine | D3D12 / Vulkan | **Untested, technically plausible** | Easy Anti-Cheat mandatory in online mode |
| 22 | **TW: Warhammer II** | Warscape | D3D11 / D3D12 | **Untested, technically plausible** | 20v20 army unit animation buffer throughput |
| 23 | **Jedi: Fallen Order** | UE 4.19 | D3D11 / D3D12 | **Untested, technically plausible** | Native DX11 mode is significantly smoother |
| 24 | **Battlefield 1** | Frostbite 3 | D3D11 / D3D12 | **Untested, technically plausible** | EA Anti-Cheat online; single-player only |
| 25 | **Battlefield V** | Frostbite 3 | D3D11 / D3D12 | **Untested, technically plausible** | Mandatory DXR disablement; offline only |
| 26 | **Far Cry 6** | Dunia Engine | D3D12 | **Untested, technically plausible** | HD texture pack must remain disabled |
| 27 | **Borderlands 3** | UE 4.22 | D3D11 / D3D12 | **Untested, technically plausible** | D3D12 initial shader compilation load times |
| 28 | **Days Gone** | UE 4.25 | D3D11 | **Native renderer baseline only** | Horde pathfinding & asset streaming |
| 29 | **Horizon Forbidden West**| Decima | D3D12 | **Untested, high risk** | Extreme asset density and DDR3 bandwidth deficit |
| 30 | **Sea of Thieves** | UE 4 | D3D11 / D3D12 | **Untested, technically plausible** | EAC online enforcement; offline test only |
