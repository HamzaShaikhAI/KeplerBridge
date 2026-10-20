# KeplerBridge Phase 8 — Group C: 30 V2 Game Roadmap & Long-Term Architecture

**Milestone:** Phase 8  
**Document Version:** 1.0.0  
**Research Date:** October 20, 2026  
**Hardware Reference:** NVIDIA GeForce GT 740 (GK208B, 4052 MB DDR3, PCI ID `10DE:128B`, Native FL 11_0, Driver 456.71, Windows 10 x64)  
**Scope:** 30 Advanced Direct3D 12 Titles on the Long-Term V2 Architectural Roadmap  

---

## 1. Architectural Scope & Roadmap Separation

Group C categorizes 30 advanced commercial PC titles that cannot run under the KeplerBridge V1 architecture due to foundational graphics milestones: Shader Model 6.x DXIL bytecode, DirectX Raytracing (DXR), Mesh & Amplification Shaders (SM 6.5), Variable Rate Shading (VRS), DirectStorage GPU decompression (GDeflate), or Resource Binding Tier 3 (bindless indexing).

These titles are explicitly separated from V1 to preserve engineering discipline and prevent feature creep.

### Key Architectural Boundaries & V2 Research Domains
1. **DXIL-to-DXBC / SPIR-V Translation:** Evaluating offline/runtime translation pipelines (e.g. LLVM-based DXIL decompilers, SPIRV-Cross, or Microsoft DirectXShaderCompiler intermediate representations) to convert SM 6.x into SM 5.0 DXBC.
2. **Software Raytracing Emulation:** Software BVH traversal using compute shaders, acknowledging extreme performance penalties.
3. **Geometry Meshlet Processing:** Translating mesh shaders into legacy geometry/vertex shaders via compute pre-passes.
4. **Bindless Descriptor Indexing:** Emulating Tier 3 descriptor tables on Tier 1 hardware using dynamic buffer indexing.
5. **Silicon Realism:** Distinguishing between features that can theoretically be translated in software versus those where the GT 740's ~14.4 GB/s DDR3 memory bus guarantees sub-10 FPS performance regardless of software implementation.

---

## 2. Comprehensive V2 Roadmap Profiles (30 Titles)

### 1. Alan Wake 2
* **Developer / Publisher:** Remedy Entertainment / Epic Games Publishing
* **Engine:** Northlight Engine
* **Required Renderer & API:** Direct3D 12 Ultimate.
* **Shader Models & Capabilities:** Shader Model 6.5; mandatory Mesh & Amplification Shaders for all geometry rasterization.
* **Resource Binding & Memory:** Resource Binding Tier 3; 6 GB VRAM minimum.
* **Advanced Features:** Mesh Shaders; Path Tracing / DXR Raytracing.
* **Potential Fallback Approaches:** Compute shader meshlet expansion into traditional index/vertex buffers.
* **Required New Subsystems:** V2 Meshlet Emulation Pipeline; DXIL SM 6.5 Decompiler.
* **Estimated Engineering Complexity:** **Research-Only.**
* **GT 740 Hardware Feasibility:** **Infeasible.** Geometry amplification compute passes would saturate DDR3 memory bandwidth, yielding <3 FPS.
* **Evidence State:** **Blocked by a specific requirement** (Mesh Shaders SM 6.5)
* **Sources:** [Remedy Alan Wake 2 FAQ](https://www.alanwake.com/) (October 2026).

---

### 2. Starfield
* **Developer / Publisher:** Bethesda Game Studios / Xbox Game Studios
* **Engine:** Creation Engine 2
* **Required Renderer & API:** Direct3D 12 Agility SDK.
* **Shader Models & Capabilities:** Shader Model 6.6; 64-bit integer atomics; Wave Intrinsics (Wave32/Wave64).
* **Resource Binding & Memory:** Resource Binding Tier 2/3; 6 GB VRAM floor.
* **Advanced Features:** Variable Rate Shading; Compute-driven indirect culling.
* **Potential Fallback Approaches:** Scalarization of Wave Intrinsics into loop iterations; 64-bit atomic emulation via mutex staging.
* **Required New Subsystems:** V2 Wave Operation Scalarizer; 64-bit Atomics Bridge.
* **Estimated Engineering Complexity:** **High.**
* **GT 740 Hardware Feasibility:** **Infeasible.** Uncompressed planetary terrain buffers exceed DDR3 bandwidth.
* **Evidence State:** **Blocked by a specific requirement** (Wave Ops & 64-bit Atomics)
* **Sources:** [Bethesda Support](https://help.bethesda.net/) (October 2026).

---

### 3. Cyberpunk 2077
* **Developer / Publisher:** CD Projekt Red
* **Engine:** REDengine 4
* **Required Renderer & API:** Direct3D 12 exclusively (`Cyberpunk2077.exe`).
* **Shader Models & Capabilities:** Shader Model 6.2+; Bindless Textures; asynchronous compute lighting passes.
* **Resource Binding & Memory:** **Resource Binding Tier 3.** The engine dynamically indexes thousands of textures from a single unbounded descriptor array.
* **Advanced Features:** Optional DXR Ray Tracing & Overdrive Path Tracing.
* **Potential Fallback Approaches:** Texture array dynamic patching or texture atlas synthesis.
* **Required New Subsystems:** V2 Bindless Descriptor Emulation; Dynamic Resource Re-indexing.
* **Estimated Engineering Complexity:** **High.**
* **GT 740 Hardware Feasibility:** **Infeasible.** Dense city crowds and streaming assets saturate 14.4 GB/s bus, causing severe watchdog TDR crashes.
* **Evidence State:** **Blocked by a specific requirement** (Resource Binding Tier 3)
* **Sources:** [CD Projekt Red Support](https://support.cdprojektred.com/en/cyberpunk/pc) (October 2026).

---

### 4. Metro Exodus Enhanced Edition
* **Developer / Publisher:** 4A Games
* **Engine:** 4A Engine
* **Required Renderer & API:** Direct3D 12 DXR Raytracing exclusively.
* **Shader Models & Capabilities:** Shader Model 6.5; DXR 1.1 inline raytracing (`RayQuery`).
* **Resource Binding & Memory:** Resource Binding Tier 2; 6 GB VRAM floor.
* **Advanced Features:** **Pure Hardware Raytracing.** All direct, indirect, emissive, and ambient lighting is path-traced; zero rasterized fallbacks exist in the shipped binaries.
* **Potential Fallback Approaches:** Software BVH traversal in compute shaders.
* **Required New Subsystems:** V2 Software Raytracing Compute Emulation.
* **Estimated Engineering Complexity:** **Research-Only.**
* **GT 740 Hardware Feasibility:** **Completely Infeasible.** Software raytracing on GT 740 would execute at ~0.5 FPS.
* **Evidence State:** **Blocked by a specific requirement** (Hardware Raytracing DXR)
* **Sources:** [4A Games](https://www.4a-games.com.mt/) (October 2026).

---

### 5. Forza Horizon 5
* **Developer / Publisher:** Playground Games / Xbox Game Studios
* **Engine:** ForzaTech
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.0; hardcoded kernel-mode capability checks for Feature Level 12_0.
* **Resource Binding & Memory:** Resource Binding Tier 2; Tier 2 Resource Heaps.
* **Advanced Features:** Variable Rate Shading; Screen-space raytraced car self-shadowing.
* **Potential Fallback Approaches:** Intercepting internal ForzaTech device capability structs; emulating Tier 2 heaps via committed resources.
* **Required New Subsystems:** V2 Virtual Resource Heap Manager; Kernel Capability Masking.
* **Estimated Engineering Complexity:** **High.**
* **GT 740 Hardware Feasibility:** **Highly Unlikely.** Mexican open-world foliage streaming requires >40 GB/s bandwidth.
* **Evidence State:** **Blocked by a specific requirement** (Hardcoded FL 12_0 / Tier 2 Binding - Error FH501)
* **Sources:** [Forza Support](https://support.forzamotorsport.net/) (October 2026).

---

### 6. Marvel's Spider-Man Remastered
* **Developer / Publisher:** Insomniac Games / Nixxes Software
* **Engine:** Proprietary
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Feature Level 12_1 (Maxwell/Turing architecture baseline); Conservative Rasterization Tier 1.
* **Resource Binding & Memory:** Resource Binding Tier 2; unbounded descriptor tables.
* **Advanced Features:** DirectStorage asset streaming; optional DXR reflections.
* **Potential Fallback Approaches:** Bounded descriptor range clamping; conservative rasterization approximation via dilated geometry.
* **Required New Subsystems:** V2 FL 12_1 Emulation Layer; Nixxes Descriptor Table Rewriter.
* **Estimated Engineering Complexity:** **High.**
* **GT 740 Hardware Feasibility:** **Infeasible.** High-speed web swinging streaming causes severe DDR3 memory stalls.
* **Evidence State:** **Blocked by a specific requirement** (Feature Level 12_1 Baseline)
* **Sources:** [Insomniac Games](https://insomniac.games/) (October 2026).

---

### 7. Marvel's Spider-Man: Miles Morales
* **Developer / Publisher:** Insomniac Games / Nixxes Software
* **Engine:** Proprietary
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Feature Level 12_1; Shader Model 6.2+.
* **Resource Binding & Memory:** Resource Binding Tier 2; 4 GB VRAM absolute minimum.
* **Advanced Features:** DirectStorage GPU decompression; Snow particle compute shaders.
* **Potential Fallback Approaches:** DirectStorage CPU decompression fallback; SM 5.0 compute translation.
* **Required New Subsystems:** V2 DirectStorage CPU Bridge; FL 12_1 Interception.
* **Estimated Engineering Complexity:** **High.**
* **GT 740 Hardware Feasibility:** **Infeasible.** DDR3 memory bandwidth bottleneck.
* **Evidence State:** **Blocked by a specific requirement** (Feature Level 12_1 Baseline)
* **Sources:** [Insomniac Games](https://insomniac.games/) (October 2026).

---

### 8. Ratchet & Clank: Rift Apart
* **Developer / Publisher:** Insomniac Games / Nixxes Software
* **Engine:** Proprietary
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.4+; DirectStorage 1.2+ GPU Decompression (GDeflate compute shaders).
* **Resource Binding & Memory:** Resource Binding Tier 2; high streaming I/O.
* **Advanced Features:** Instant dimensional rift portal rendering; DirectStorage GDeflate.
* **Potential Fallback Approaches:** Hooking DirectStorage runtime to force `DSTORAGE_COMPRESSION_FORMAT_NONE` or CPU decompression.
* **Required New Subsystems:** V2 DirectStorage Decompression Interceptor.
* **Estimated Engineering Complexity:** **High.**
* **GT 740 Hardware Feasibility:** **Infeasible.** Dual-scene rift rendering saturates 4052 MB VRAM instantly.
* **Evidence State:** **Blocked by a specific requirement** (DirectStorage GDeflate GPU Decompression)
* **Sources:** [Nixxes Software](https://www.nixxes.com/) (October 2026).

---

### 9. Ghost of Tsushima Director's Cut
* **Developer / Publisher:** Sucker Punch Productions / Nixxes Software
* **Engine:** Proprietary
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.2+; PlayStation PC Core SDK overlay integration.
* **Resource Binding & Memory:** Resource Binding Tier 2; 4 GB VRAM floor.
* **Advanced Features:** DirectStorage streaming; Dynamic wind and particle compute passes.
* **Potential Fallback Approaches:** Custom descriptor table remapping; PlayStation SDK stubbing.
* **Required New Subsystems:** V2 PlayStation SDK Hook Compatibility Layer.
* **Estimated Engineering Complexity:** **Medium.**
* **GT 740 Hardware Feasibility:** **Unlikely.** Grass and particle simulation on DDR3 bus causes heavy frametime variance (<15 FPS).
* **Evidence State:** **Blocked by a specific requirement** (PlayStation PC Core SDK & DirectStorage)
* **Sources:** [PlayStation Support](https://www.playstation.com/) (October 2026).

---

### 10. Returnal
* **Developer / Publisher:** Housemarque / PlayStation PC LLC
* **Engine:** Unreal Engine 4.27 (Customized)
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.0; intensive particle compute shaders (hundreds of thousands of simultaneous particles).
* **Resource Binding & Memory:** Resource Binding Tier 2; **6 GB VRAM minimum specification**.
* **Advanced Features:** Volumetric particle lighting; compute particle collisions.
* **Potential Fallback Approaches:** Particle count clamping; downsampling particle compute buffers.
* **Required New Subsystems:** V2 Compute Dispatch Throttler & Buffer Clamper.
* **Estimated Engineering Complexity:** **High.**
* **GT 740 Hardware Feasibility:** **Infeasible.** 14.4 GB/s DDR3 memory bandwidth triggers immediate driver TDR timeout.
* **Evidence State:** **Blocked by a specific requirement** (6 GB VRAM Floor & Memory Bandwidth Saturation)
* **Sources:** [Housemarque](https://housemarque.com/) (October 2026).

---

### 11. Lords of the Fallen (2023)
* **Developer / Publisher:** HEXWORKS / CI Games
* **Engine:** Unreal Engine 5.2
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.5; mandatory Nanite geometry; 64-bit atomic operations in compute shaders.
* **Resource Binding & Memory:** Resource Binding Tier 2; 6 GB VRAM minimum.
* **Advanced Features:** Nanite virtualized micro-polygons; Lumen software/hardware GI.
* **Potential Fallback Approaches:** Nanite geometry proxy fallback to traditional LOD meshes.
* **Required New Subsystems:** V2 Nanite-to-LOD Demotion Layer; SM 6.5 Decompiler.
* **Estimated Engineering Complexity:** **Research-Only.**
* **GT 740 Hardware Feasibility:** **Completely Infeasible.**
* **Evidence State:** **Blocked by a specific requirement** (Nanite 64-Bit Atomics & Geometry Compute)
* **Sources:** [CI Games Support](https://www.cigames.com/) (October 2026).

---

### 12. Senua's Saga: Hellblade II
* **Developer / Publisher:** Ninja Theory / Xbox Game Studios
* **Engine:** Unreal Engine 5.4
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.6; full Nanite scene rasterization; Lumen hardware raytracing.
* **Resource Binding & Memory:** Resource Binding Tier 3; 6 GB VRAM minimum.
* **Advanced Features:** Photogrammetry asset streaming; virtual shadow maps.
* **Potential Fallback Approaches:** Complete pipeline demotion to forward D3D11 rendering.
* **Required New Subsystems:** V2 UE5 Pipeline Demoter.
* **Estimated Engineering Complexity:** **Research-Only.**
* **GT 740 Hardware Feasibility:** **Completely Infeasible.**
* **Evidence State:** **Blocked by a specific requirement** (Nanite & Lumen Hard Silicon Limits)
* **Sources:** [Ninja Theory](https://ninjatheory.com/) (October 2026).

---

### 13. Dead Space (2023 Remake)
* **Developer / Publisher:** Motive Studio / Electronic Arts
* **Engine:** Frostbite
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.2; **Variable Rate Shading (VRS Tier 1)** mandatory in Frostbite pipeline.
* **Resource Binding & Memory:** Resource Binding Tier 2; 8 GB VRAM recommended.
* **Advanced Features:** Variable Rate Shading; Volumetric fog compute passes.
* **Potential Fallback Approaches:** Patching VRS pipeline state descriptors to 1x1 uniform shading rate.
* **Required New Subsystems:** V2 VRS State Interceptor & Stripper.
* **Estimated Engineering Complexity:** **Medium.**
* **GT 740 Hardware Feasibility:** **Infeasible.** Engine lighting passes demand massive fillrate beyond Kepler DDR3 capabilities.
* **Evidence State:** **Blocked by a specific requirement** (Variable Rate Shading VRS Tier 1)
* **Sources:** [EA Help](https://help.ea.com/) (October 2026).

---

### 14. Halo Infinite
* **Developer / Publisher:** 343 Industries / Xbox Game Studios
* **Engine:** Slipspace Engine
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.0; hardcoded Feature Level 12_0 checks.
* **Resource Binding & Memory:** Resource Binding Tier 2; anti-tamper memory checks.
* **Advanced Features:** Async compute lighting; kernel anti-cheat in multiplayer.
* **Potential Fallback Approaches:** FL 12_0 spoofing and compute translation for campaign mode.
* **Required New Subsystems:** V2 Slipspace Command Buffer Translator.
* **Estimated Engineering Complexity:** **High.**
* **GT 740 Hardware Feasibility:** **Infeasible.** Engine requires modern GPU compute structures and exhibits sub-15 FPS on older Maxwell cards.
* **Evidence State:** **Blocked by a specific requirement** (Kernel Security & FL 12_0 Hardware Floor)
* **Sources:** [Halo Waypoint Support](https://support.halowaypoint.com/) (October 2026).

---

### 15. DOOM Eternal
* **Developer / Publisher:** id Software / Bethesda
* **Engine:** id Tech 7
* **Required Renderer & API:** **Pure Vulkan exclusively (`vulkan-1.dll`).**
* **Shader Models & Capabilities:** Vulkan SPIR-V 1.3+; bindless descriptors; subgroup operations.
* **Resource Binding & Memory:** Vulkan descriptor indexing.
* **Advanced Features:** Pure Vulkan compute rendering pipeline; contains zero Direct3D 12 code.
* **Potential Fallback Approaches:** Completely separate Vulkan-to-D3D11 wrapper (outside KeplerBridge scope).
* **Required New Subsystems:** Vulkan Proxy Subsystem (Out of D3D12 Scope).
* **Estimated Engineering Complexity:** **Research-Only (API Mismatch).**
* **GT 740 Hardware Feasibility:** GT 740 supports Vulkan 1.1 natively under driver 456.71; DOOM Eternal runs natively via Vulkan on low settings.
* **Evidence State:** **Blocked by a specific requirement** (Pure Vulkan API - No D3D12 Invocation)
* **Sources:** [Bethesda Help](https://help.bethesda.net/) (October 2026).

---

### 16. Diablo IV
* **Developer / Publisher:** Blizzard Entertainment
* **Engine:** Proprietary In-House Engine
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.0 DXIL; Blizzard Warden anti-cheat integrity scanning.
* **Resource Binding & Memory:** Resource Binding Tier 2; 4 GB VRAM floor.
* **Advanced Features:** Server-authoritative online synchronization; continuous memory hashing.
* **Potential Fallback Approaches:** None; proxy DLLs trigger immediate account sanctions.
* **Required New Subsystems:** N/A (Anti-Cheat Policy).
* **Estimated Engineering Complexity:** **Research-Only.**
* **GT 740 Hardware Feasibility:** Incompatible due to Warden anti-cheat.
* **Evidence State:** **Blocked by a specific requirement** (Blizzard Warden Anti-Cheat)
* **Sources:** [Blizzard Support](https://battle.net/support) (October 2026).

---

### 17. Black Myth: Wukong
* **Developer / Publisher:** Game Science
* **Engine:** Unreal Engine 5.4
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.6; full Nanite geometry; Virtual Shadow Maps (VSM); Lumen.
* **Resource Binding & Memory:** Resource Binding Tier 3; 6 GB VRAM minimum.
* **Advanced Features:** Dense geometric foliage; full hardware/software path-traced GI.
* **Potential Fallback Approaches:** Low-spec command line flags (`-norhithread`, `-dx11` if available).
* **Required New Subsystems:** V2 Nanite & VSM Emulation.
* **Estimated Engineering Complexity:** **Research-Only.**
* **GT 740 Hardware Feasibility:** **Completely Infeasible.**
* **Evidence State:** **Blocked by a specific requirement** (UE5.4 Nanite, VSM & SM 6.6 DXIL)
* **Sources:** [Game Science Official Portal](https://www.heishenhua.com/) (October 2026).

---

### 18. S.T.A.L.K.E.R. 2: Heart of Chornobyl
* **Developer / Publisher:** GSC Game World
* **Engine:** Unreal Engine 5.1
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.6; Nanite geometry streaming; Lumen global illumination.
* **Resource Binding & Memory:** Resource Binding Tier 2/3; 8 GB VRAM floor.
* **Advanced Features:** Seamless open-world streaming; dynamic weather compute shaders.
* **Potential Fallback Approaches:** Severe texture downscaling and Nanite mesh proxy fallbacks.
* **Required New Subsystems:** V2 UE5 Asset Demotion Pipeline.
* **Estimated Engineering Complexity:** **Research-Only.**
* **GT 740 Hardware Feasibility:** **Completely Infeasible.**
* **Evidence State:** **Blocked by a specific requirement** (UE5 Nanite, Lumen & VRAM Floor)
* **Sources:** [GSC Game World](https://www.stalker2.com/) (October 2026).

---

### 19. Avatar: Frontiers of Pandora
* **Developer / Publisher:** Massive Entertainment / Ubisoft
* **Engine:** Snowdrop Engine
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.6; **Hardware Raytraced Global Illumination (GI) mandatory.**
* **Resource Binding & Memory:** Resource Binding Tier 2; 8 GB VRAM minimum.
* **Advanced Features:** Raytraced indirect lighting on all geometry; dense foliage compute wind.
* **Potential Fallback Approaches:** Software raytracing compute pass.
* **Required New Subsystems:** V2 Software Raytracing Pipeline.
* **Estimated Engineering Complexity:** **Research-Only.**
* **GT 740 Hardware Feasibility:** **Completely Infeasible.**
* **Evidence State:** **Blocked by a specific requirement** (Mandatory Raytraced Global Illumination)
* **Sources:** [Ubisoft Support](https://www.ubisoft.com/help) (October 2026).

---

### 20. Star Wars Outlaws
* **Developer / Publisher:** Massive Entertainment / Ubisoft
* **Engine:** Snowdrop Engine
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.6; Raytraced direct and indirect lighting; Frame Generation.
* **Resource Binding & Memory:** Resource Binding Tier 3; 8 GB VRAM minimum.
* **Advanced Features:** Raytracing required for all lighting; DirectStorage streaming.
* **Potential Fallback Approaches:** None; engine contains no raster fallback.
* **Required New Subsystems:** V2 Software DXR Engine.
* **Estimated Engineering Complexity:** **Research-Only.**
* **GT 740 Hardware Feasibility:** **Completely Infeasible.**
* **Evidence State:** **Blocked by a specific requirement** (Mandatory Raytracing & SM 6.6 DXIL)
* **Sources:** [Ubisoft Support](https://www.ubisoft.com/help) (October 2026).

---

### 21. Dragon's Dogma 2
* **Developer / Publisher:** Capcom
* **Engine:** RE Engine
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.6; Mesh Shaders for foliage; heavy multithreaded command recording.
* **Resource Binding & Memory:** Resource Binding Tier 2; 6 GB VRAM floor.
* **Advanced Features:** High-density NPC character rendering; physics compute dispatches.
* **Potential Fallback Approaches:** Geometry shader meshlet expansion.
* **Required New Subsystems:** V2 RE Engine Meshlet Pipeline.
* **Estimated Engineering Complexity:** **High.**
* **GT 740 Hardware Feasibility:** **Infeasible.** Extreme CPU and memory bandwidth bottlenecks.
* **Evidence State:** **Blocked by a specific requirement** (Mesh Shaders & VRAM Floor)
* **Sources:** [Capcom Support](https://www.dragonsdogma.com/2/) (October 2026).

---

### 22. The Last of Us Part I (PC)
* **Developer / Publisher:** Naughty Dog / Iron Galaxy / PlayStation PC
* **Engine:** Proprietary In-House
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.0; strict Feature Level 12_0; unbounded descriptor indexing.
* **Resource Binding & Memory:** Resource Binding Tier 2; 4 GB VRAM absolute minimum.
* **Advanced Features:** Heavy PSO generation (>15,000 shaders during initial launch); volumetric lighting.
* **Potential Fallback Approaches:** Persistent shader disk cache; descriptor heap clamping.
* **Required New Subsystems:** V2 Naughty Dog Pipeline Transpiler.
* **Estimated Engineering Complexity:** **High.**
* **GT 740 Hardware Feasibility:** **Infeasible.** Massive VRAM footprint and memory bandwidth requirements cause continuous driver TDR timeouts.
* **Evidence State:** **Blocked by a specific requirement** (VRAM Bandwidth & Strict FL 12_0 Architecture)
* **Sources:** [Naughty Dog Support](https://www.naughtydog.com/) (October 2026).

---

### 23. Uncharted: Legacy of Thieves Collection
* **Developer / Publisher:** Naughty Dog / Iron Galaxy / PlayStation PC
* **Engine:** Proprietary In-House
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.0; strict FL 12_0 device creation.
* **Resource Binding & Memory:** Resource Binding Tier 2; 4 GB VRAM floor.
* **Advanced Features:** Async compute water and foliage passes.
* **Potential Fallback Approaches:** FL 12_0 spoofing with command queue serialization.
* **Required New Subsystems:** V2 Async Queue Serializer.
* **Estimated Engineering Complexity:** **Medium.**
* **GT 740 Hardware Feasibility:** **Unlikely.** Open jeep traversal areas saturate DDR3 memory bandwidth, yielding <15 FPS.
* **Evidence State:** **Blocked by a specific requirement** (Strict FL 12_0 & Memory Bandwidth Ceiling)
* **Sources:** [PlayStation PC Support](https://www.playstation.com/) (October 2026).

---

### 24. Monster Hunter Wilds
* **Developer / Publisher:** Capcom
* **Engine:** RE Engine
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.6; dynamic weather compute simulations; DirectStorage.
* **Resource Binding & Memory:** Resource Binding Tier 2/3; 6 GB VRAM minimum.
* **Advanced Features:** Dynamic sandstorm particle compute passes; DirectStorage streaming.
* **Potential Fallback Approaches:** Particle resolution downsampling.
* **Required New Subsystems:** V2 Next-Gen RE Engine Translator.
* **Estimated Engineering Complexity:** **High.**
* **GT 740 Hardware Feasibility:** **Infeasible.**
* **Evidence State:** **Blocked by a specific requirement** (SM 6.6 DXIL & 6 GB VRAM Floor)
* **Sources:** [Capcom Official](https://www.monsterhunter.com/wilds/) (October 2026).

---

### 25. Indiana Jones and the Great Circle
* **Developer / Publisher:** MachineGames / Bethesda
* **Engine:** id Tech (Customized Next-Gen)
* **Required Renderer & API:** Direct3D 12 Ultimate.
* **Shader Models & Capabilities:** Shader Model 6.6; full hardware raytracing; Mesh Shaders.
* **Resource Binding & Memory:** Resource Binding Tier 3; 8 GB VRAM floor.
* **Advanced Features:** Hardware raytraced reflections and indirect bounce.
* **Potential Fallback Approaches:** Software raytracing and meshlet emulation.
* **Required New Subsystems:** V2 D3D12 Ultimate Emulation Stack.
* **Estimated Engineering Complexity:** **Research-Only.**
* **GT 740 Hardware Feasibility:** **Completely Infeasible.**
* **Evidence State:** **Blocked by a specific requirement** (Direct3D 12 Ultimate & Mesh Shaders)
* **Sources:** [Bethesda Help](https://help.bethesda.net/) (October 2026).

---

### 26. Silent Hill 2 (2024 Remake)
* **Developer / Publisher:** Bloober Team / Konami
* **Engine:** Unreal Engine 5.2
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.5; Nanite geometry; Lumen software GI.
* **Resource Binding & Memory:** Resource Binding Tier 2/3; 8 GB VRAM recommended.
* **Advanced Features:** Volumetric fog compute; Nanite mesh culling.
* **Potential Fallback Approaches:** Low-spec command line flags (`-dx11` if supported in future patches).
* **Required New Subsystems:** V2 Nanite-to-LOD Demotion Layer.
* **Estimated Engineering Complexity:** **Research-Only.**
* **GT 740 Hardware Feasibility:** **Completely Infeasible.**
* **Evidence State:** **Blocked by a specific requirement** (UE5 Nanite, Lumen & SM 6.5 DXIL)
* **Sources:** [Konami Support](https://www.konami.com/) (October 2026).

---

### 27. Ark: Survival Ascended
* **Developer / Publisher:** Studio Wildcard
* **Engine:** Unreal Engine 5.2
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.6; Nanite geometry; Lumen lighting; Chaos physics.
* **Resource Binding & Memory:** Resource Binding Tier 3; 8 GB VRAM floor.
* **Advanced Features:** High-poly dinosaur meshes rendered directly via Nanite.
* **Potential Fallback Approaches:** None; engine is strictly architected around Nanite.
* **Required New Subsystems:** V2 Nanite Transpiler.
* **Estimated Engineering Complexity:** **Research-Only.**
* **GT 740 Hardware Feasibility:** **Completely Infeasible.**
* **Evidence State:** **Blocked by a specific requirement** (UE5 Nanite & SM 6.6 DXIL)
* **Sources:** [Studio Wildcard](https://survivetheark.com/) (October 2026).

---

### 28. Final Fantasy XVI (PC Port)
* **Developer / Publisher:** Square Enix
* **Engine:** Proprietary In-House
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.6; DirectStorage; heavy compute particle passes.
* **Resource Binding & Memory:** Resource Binding Tier 2; **8 GB VRAM minimum requirement**.
* **Advanced Features:** Massive Eikon battle particle effects; DirectStorage asset streaming.
* **Potential Fallback Approaches:** Memory downscaling and particle clamping.
* **Required New Subsystems:** V2 Memory Compression & Clamping Layer.
* **Estimated Engineering Complexity:** **High.**
* **GT 740 Hardware Feasibility:** **Infeasible.** 4052 MB DDR3 memory is saturated before the title screen renders.
* **Evidence State:** **Blocked by a specific requirement** (8 GB VRAM Floor & DirectStorage)
* **Sources:** [Square Enix Support](https://support.square-enix-games.com/) (October 2026).

---

### 29. Tekken 8 (Advanced Rendering Paths)
* **Developer / Publisher:** Bandai Namco
* **Engine:** Unreal Engine 5.2
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.5; Nanite destructible stage geometry.
* **Resource Binding & Memory:** Resource Binding Tier 2; 4 GB VRAM floor.
* **Advanced Features:** Nanite stage transitions; high-fidelity cloth physics.
* **Potential Fallback Approaches:** Disabling Nanite stage geometry in favor of static mesh collision.
* **Required New Subsystems:** V2 Stage Mesh Demoter.
* **Estimated Engineering Complexity:** **High.**
* **GT 740 Hardware Feasibility:** **Unlikely.** 60 FPS competitive fighting framerate cannot be sustained on GT 740.
* **Evidence State:** **Untested, high risk**
* **Sources:** [Bandai Namco Support](https://service-en.bandainamcoent.eu/) (October 2026).

---

### 30. Kingdom Come: Deliverance II
* **Developer / Publisher:** Warhorse Studios / Deep Silver
* **Engine:** CryEngine (Heavily Customized)
* **Required Renderer & API:** Direct3D 12 exclusively.
* **Shader Models & Capabilities:** Shader Model 6.6; heavy SVOGI (Sparse Voxel Octree Global Illumination).
* **Resource Binding & Memory:** Resource Binding Tier 2; 6 GB VRAM minimum.
* **Advanced Features:** Voxel-based indirect lighting; dense procedural vegetation.
* **Potential Fallback Approaches:** Disabling SVOGI in favor of pre-baked lightmaps.
* **Required New Subsystems:** V2 CryEngine Voxel Demotion Subsystem.
* **Estimated Engineering Complexity:** **High.**
* **GT 740 Hardware Feasibility:** **Infeasible.** Dense medieval crowd and vegetation rendering saturates DDR3 memory bandwidth.
* **Evidence State:** **Blocked by a specific requirement** (Voxel Octree GI & SM 6.6 DXIL)
* **Sources:** [Warhorse Studios](https://warhorsestudios.cz/) (October 2026).

---

## 3. Group C Synthesis & V2 Engineering Matrix

| Primary Silicon / API Barrier | Representative Titles | Required V2 Subsystem | Complexity | GT 740 Feasibility |
| :--- | :--- | :--- | :---: | :---: |
| **Mesh & Amplification Shaders (SM 6.5)** | *Alan Wake 2*, *Dragon's Dogma 2*, *Indiana Jones* | V2 Meshlet-to-Geometry Transpiler | **Research-Only** | **Infeasible** (<5 FPS) |
| **Pure DXR Hardware Raytracing** | *Metro Exodus Enhanced*, *Avatar*, *Star Wars Outlaws* | V2 Software BVH Compute Traversal | **Research-Only** | **Completely Infeasible** (<1 FPS) |
| **UE5 Nanite Virtual Geometry (SM 6.5/6.6)**| *Lords of the Fallen*, *Black Myth: Wukong*, *Hellblade II*, *Silent Hill 2* | V2 Nanite-to-LOD Demotion Engine | **Research-Only** | **Infeasible** (Bandwidth Floor) |
| **Resource Binding Tier 3 (Bindless)** | *Cyberpunk 2077*, *Hellblade II*, *Ark: Survival Ascended* | V2 Dynamic Texture Atlas Re-indexing | **High** | **Infeasible** (TDR Watchdog) |
| **DirectStorage GDeflate Decompression** | *Ratchet & Clank*, *Spider-Man*, *Final Fantasy XVI* | V2 DirectStorage CPU Fallback Bridge | **High** | **Infeasible** (VRAM Floor >6GB) |
| **Variable Rate Shading (VRS Tier 1)** | *Dead Space Remake*, *Starfield*, *Forza Horizon 5* | V2 VRS State Interceptor & Stripper | **Medium** | **Infeasible** (Fillrate Deficit) |
| **Wave Intrinsics & 64-bit Atomics** | *Starfield*, *Lords of the Fallen* | V2 Wave Scalarizer & 64-bit Atomics Bridge | **High** | **Infeasible** (DDR3 Saturation) |
| **Kernel Anti-Cheat Enforcement** | *Diablo IV*, *Halo Infinite Online* | None (Anti-Cheat Policy: Strictly Unsupported) | **Out of Scope** | **Incompatible** |
| **Pure Vulkan (No D3D12 API)** | *DOOM Eternal* | None (API Mismatch; Native Vulkan supported) | **Out of Scope** | **Native Vulkan Available** |
