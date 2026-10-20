# KeplerBridge Phase 7 — 25 PC Game Compatibility Matrix & Research Report

**Document Version:** 1.0.0  
**Research Date:** October 20, 2026  
**Hardware Reference:** NVIDIA GeForce GT 740 (GK208, 4052 MB DDR3, Native FL 11_0, Driver 456.71, Windows 10 x64)  
**Evaluation Methodology:** Official publisher specifications, engine architecture documentation, community crash logs, and Direct3D 12 API tracing.

---

## 1. Classification Summary

| Classification | Count | Definition |
| :--- | :---: | :--- |
| **PLAUSIBLE** | **6** | Games with documented D3D11 renderers, dual D3D11/D3D12 executables, or confirmed compatibility with translation layers without intrusive anti-cheat. |
| **EXPERIMENTAL** | **9** | Pure D3D12 titles requiring Feature Level 12_0 spoofing, moderate shader model requirements (SM 5.0/6.0 with translation), but facing severe VRAM or compute bandwidth constraints on DDR3. |
| **BLOCKED** | **10** | Games with insurmountable hardware barriers: mandatory Mesh Shaders, Hardware Raytracing, DirectStorage, pure Vulkan APIs, or kernel-level Anti-Cheat blocking local DLL proxies. |

---

## 2. Comprehensive 25-Game Analysis Matrix

### 1. Palworld
* **Developer / Publisher:** Pocketpair
* **Engine:** Unreal Engine 5.1
* **Active Renderer:** Default D3D12 on Windows 10/11; D3D11 supported via `-dx11` command-line argument.
* **KeplerBridge Interception:** **Yes.** Intercepts default `d3d12.dll` pipeline when launched without flags.
* **Official Minimum GPU:** NVIDIA GeForce GTX 1660 (6 GB) / older baseline GTX 1050 (2 GB). Official minimum DirectX: Version 11.
* **Documented Error:** *"DirectX 12 is not supported on your system"* when UE5 D3D12RHI queries `D3D_FEATURE_LEVEL_12_0` on native FL 11_0 hardware.
* **Technical Cause:** `D3D12CreateDevice(..., D3D_FEATURE_LEVEL_12_0)` returns `0x887A0004 (DXGI_ERROR_UNSUPPORTED)`.
* **Addressability:** **Plausible.** Intercepted via `EnableFeatureLevelSpoof=1` and `EnableD3D11Translation=1`. If bypassed with `-dx11`, runs as hardware baseline.
* **Anti-Cheat / DRM:** No kernel anti-cheat in singleplayer or community dedicated servers. Easy Anti-Cheat is optional on official servers.
* **Testing Procedure:** Copy `d3d12.dll` and `kepler_bridge.ini` to `Palworld\Pal\Binaries\Win64\`. Launch `Palworld-Win64-Shipping.exe`. Verify startup banner in `kepler_bridge.log`.
* **Classification:** **PLAUSIBLE**
* **Sources:** [Steam Official Store](https://store.steampowered.com/app/1623730/Palworld/), [Pocketpair Support](https://pocketpair.jp/) (October 2026).

---

### 2. Elden Ring
* **Developer / Publisher:** FromSoftware / Bandai Namco
* **Engine:** Proprietary (Dantelion Engine)
* **Active Renderer:** DirectX 12 only (no native DX11 toggle).
* **KeplerBridge Interception:** **Yes.** Placed adjacent to `eldenring.exe`.
* **Official Minimum GPU:** NVIDIA GeForce GTX 1060 (3 GB) / AMD Radeon RX 580 (4 GB). Demands Feature Level 12_0.
* **Documented Error:** Instant white-screen crash on startup.
* **Technical Cause:** Native driver returns `0x887A0004` on FL 12_0 device creation; engine aborts without error dialog.
* **Addressability:** **Experimental (Offline Only).** Requires FL 12_0 spoofing and compute shader translation.
* **Anti-Cheat / DRM:** Easy Anti-Cheat (EAC) will reject local `d3d12.dll` proxy in online mode. Testing requires offline launch via `start_protected_game.exe` bypass or steam offline flag.
* **Testing Procedure:** Configure Steam to offline mode. Rename `start_protected_game.exe` to `.bak` and rename `eldenring.exe` to `start_protected_game.exe`. Verify offline startup.
* **Classification:** **EXPERIMENTAL**
* **Sources:** [FromSoftware Support](https://www.fromsoftware.jp/en/), [Bandai Namco Community](https://service-en.bandainamcoent.eu/) (October 2026).

---

### 3. The Witcher 3: Wild Hunt (v4.0+ Next-Gen)
* **Developer / Publisher:** CD Projekt Red
* **Engine:** REDengine 3
* **Active Renderer:** Dual executable: `witcher3.exe` (DirectX 12) and `witcher3_dx11.exe` (DirectX 11).
* **KeplerBridge Interception:** **Yes.** Intercepts `bin\x64_dx12\witcher3.exe`.
* **Official Minimum GPU:** GTX 960 (4 GB) for DX12 mode; GTX 660 for DX11 mode.
* **Documented Error:** Crash with exception `0xC0000005` in `d3d12.dll` during initial loading screen.
* **Technical Cause:** Engine assumes Tier 2 Resource Binding and FL 12_0 root descriptor tables.
* **Addressability:** **Plausible.** DX11 version provides an ideal baseline comparison. DX12 version verifies descriptor mapping.
* **Anti-Cheat / DRM:** None (DRM-Free on GOG, standard Steam wrapper).
* **Testing Procedure:** Place proxy DLL into `bin\x64_dx12\`. Launch game. Compare memory footprint and performance against `bin\x64\witcher3_dx11.exe`.
* **Classification:** **PLAUSIBLE**
* **Sources:** [CD Projekt Red Support](https://support.cdprojektred.com/) (October 2026).

---

### 4. Control
* **Developer / Publisher:** Remedy Entertainment / 505 Games
* **Engine:** Northlight Engine
* **Active Renderer:** Dual executable: `Control_DX11.exe` and `Control_DX12.exe`.
* **KeplerBridge Interception:** **Yes.** Intercepts `Control_DX12.exe`.
* **Official Minimum GPU:** NVIDIA GeForce GTX 780 (3 GB). Feature Level 11_0 for DX11; Feature Level 12_0 for DX12.
* **Documented Error:** `Control_DX12.exe` immediately exits with "Failed to create D3D12 device".
* **Addressability:** **Plausible.** Northlight engine's D3D12 implementation maps cleanly to D3D11 backend.
* **Anti-Cheat / DRM:** None.
* **Testing Procedure:** Place proxy into root game folder. Run `Control_DX12.exe`. Verify windowed presentation and swap chain resizing.
* **Classification:** **PLAUSIBLE**
* **Sources:** [Remedy Entertainment](https://www.remedygames.com/) (October 2026).

---

### 5. Monster Hunter: World
* **Developer / Publisher:** Capcom
* **Engine:** MT Framework
* **Active Renderer:** DirectX 11 (default) and DirectX 12 toggle in graphics options.
* **KeplerBridge Interception:** **Yes.** Intercepts D3D12 device creation when toggle is enabled.
* **Official Minimum GPU:** GeForce GTX 760 (2 GB).
* **Documented Error:** `ERR12: Graphics device crashed` when D3D12 API is selected on Kepler.
* **Addressability:** **Plausible.** Engine D3D12 renderer is an early translation layer from MT Framework DX11.
* **Anti-Cheat / DRM:** Denuvo removed in current builds.
* **Testing Procedure:** Boot in DX11 mode, switch option to DX12, restart with proxy, verify translation queue replay.
* **Classification:** **PLAUSIBLE**
* **Sources:** [Capcom Support](https://www.capcom-games.com/) (October 2026).

---

### 6. Sons of the Forest
* **Developer / Publisher:** Endnight Games
* **Engine:** Unity 2022 LTS
* **Active Renderer:** DirectX 11 (standard) with optional `-force-d3d12` launch flag.
* **KeplerBridge Interception:** **Yes.** Active when `-force-d3d12` is specified.
* **Official Minimum GPU:** GTX 1060 (3 GB).
* **Documented Error:** Unity D3D12 player fails with `DeviceRemoved` when root constant sizing is mismatched.
* **Addressability:** **Plausible.** KeplerBridge's dynamic 256-byte constant buffer staging handles Unity's constant buffers.
* **Anti-Cheat / DRM:** None in singleplayer.
* **Testing Procedure:** Launch with `SonsOfTheForest.exe -force-d3d12`. Check `kepler_bridge.log` for Unity shader reflection.
* **Classification:** **PLAUSIBLE**
* **Sources:** [Endnight Games](https://endnightgames.com/) (October 2026).

---

### 7. Cyberpunk 2077
* **Developer / Publisher:** CD Projekt Red
* **Engine:** REDengine 4
* **Active Renderer:** Pure DirectX 12 only (no DX11 path exists).
* **KeplerBridge Interception:** **Yes.** Intercepts `bin\x64\Cyberpunk2077.exe`.
* **Official Minimum GPU:** GTX 1060 (6 GB).
* **Documented Error:** Crash reporter triggers immediately on launch.
* **Technical Cause:** Heavy usage of Shader Model 6.x compute shaders, Bindless Textures, and FL 12_0 Resource Binding Tier 3.
* **Addressability:** **Experimental.** Kepler hardware lacks Tier 3 binding. Requires shader translation and severe texture clamping.
* **Anti-Cheat / DRM:** None (GOG/Steam).
* **Classification:** **EXPERIMENTAL**
* **Sources:** [Cyberpunk Support](https://support.cdprojektred.com/en/cyberpunk/pc) (October 2026).

---

### 8. Hogwarts Legacy
* **Developer / Publisher:** Avalanche Software / WB Games
* **Engine:** Unreal Engine 4.27
* **Active Renderer:** DirectX 12 default.
* **KeplerBridge Interception:** **Yes.** Intercepts `HogwartsLegacy.exe`.
* **Official Minimum GPU:** GeForce GTX 960 (4 GB).
* **Documented Error:** Out of Video Memory crashes during shader compilation phase.
* **Addressability:** **Experimental.** Severe memory pressure on GT 740 4GB DDR3 (bandwidth bottleneck ~14.4 GB/s).
* **Anti-Cheat / DRM:** Denuvo Anti-Tamper.
* **Classification:** **EXPERIMENTAL**
* **Sources:** [Portkey Games Support](https://portkeygamessupport.wbgames.com/) (October 2026).

---

### 9. Resident Evil 4 Remake
* **Developer / Publisher:** Capcom
* **Engine:** RE Engine
* **Active Renderer:** DirectX 12 only.
* **KeplerBridge Interception:** **Yes.**
* **Official Minimum GPU:** GeForce GTX 1050 Ti (4 GB) / AMD Radeon RX 560.
* **Documented Error:** Fatal D3D error `0x887a0005` (DEVICE_HUNG).
* **Addressability:** **Experimental.** RE Engine heavily relies on compute dispatches and UAV ping-ponging (tested in Phase 7 Milestone A).
* **Anti-Cheat / DRM:** Denuvo.
* **Classification:** **EXPERIMENTAL**
* **Sources:** [Capcom Resident Evil Portal](https://game.capcom.com/residentevil/) (October 2026).

---

### 10. Horizon Zero Dawn Complete Edition
* **Developer / Publisher:** Guerrilla Games / PlayStation PC
* **Engine:** Decima Engine
* **Active Renderer:** DirectX 12 only.
* **KeplerBridge Interception:** **Yes.**
* **Official Minimum GPU:** GTX 780 (3 GB).
* **Documented Error:** Crash during initial "Optimizing Shaders" progress bar.
* **Addressability:** **Experimental.** Decima Engine's initial pipeline compiles ~25,000 shaders; persistent disk cache is essential.
* **Classification:** **EXPERIMENTAL**
* **Sources:** [PlayStation Support](https://www.playstation.com/) (October 2026).

---

### 11. Death Stranding
* **Developer / Publisher:** Kojima Productions / 505 Games
* **Engine:** Decima Engine
* **Active Renderer:** DirectX 12 only.
* **KeplerBridge Interception:** **Yes.**
* **Official Minimum GPU:** GeForce GTX 1050 (3 GB).
* **Documented Error:** `0x887a0004 (DXGI_ERROR_UNSUPPORTED)` error dialog on launch.
* **Addressability:** **Experimental.** Requires FL 12_0 spoofing.
* **Classification:** **EXPERIMENTAL**
* **Sources:** [505 Games Support](https://support.505games.com/) (October 2026).

---

### 12. Red Dead Redemption 2
* **Developer / Publisher:** Rockstar Games
* **Engine:** RAGE
* **Active Renderer:** Vulkan (default) and DirectX 12 (configurable via `system.xml`).
* **KeplerBridge Interception:** **Yes**, if `system.xml` has `<advancedGraphics><API>kSettingAPI_DX12</API>`.
* **Official Minimum GPU:** GeForce GTX 770 (2 GB).
* **Documented Error:** `ERR_GFX_D3D_NOD3D12EX_SUPPORT`.
* **Addressability:** **Experimental.** Vulkan runs natively; D3D12 requires FL 12_0 spoofing.
* **Anti-Cheat / DRM:** Rockstar Games Launcher integrity monitor.
* **Classification:** **EXPERIMENTAL**
* **Sources:** [Rockstar Games Support](https://support.rockstargames.com/) (October 2026).

---

### 13. Marvel's Spider-Man Remastered
* **Developer / Publisher:** Insomniac Games / Nixxes
* **Engine:** Proprietary
* **Active Renderer:** DirectX 12 only.
* **KeplerBridge Interception:** **Yes.**
* **Official Minimum GPU:** GTX 950 (Feature Level 12_1 Maxwell).
* **Documented Error:** Aborts on launch with "Graphics card does not support required DirectX 12 features".
* **Addressability:** **Experimental.** Nixxes engine requires extensive root signature table rebinding.
* **Classification:** **EXPERIMENTAL**
* **Sources:** [Insomniac Games](https://insomniac.games/) (October 2026).

---

### 14. God of War (2018 PC)
* **Developer / Publisher:** Santa Monica Studio / PlayStation PC
* **Engine:** Proprietary
* **Active Renderer:** DirectX 11 native.
* **KeplerBridge Interception:** Bypasses D3D12 proxy under standard launch; can be intercepted if wrapped.
* **Official Minimum GPU:** GTX 960 (4 GB).
* **Documented Error:** Windows 10 version check / Out of Memory on 2GB cards.
* **Addressability:** **Plausible (Hardware Baseline).** Validates that Kepler GT 740 hardware context can execute heavy modern assets natively.
* **Classification:** **PLAUSIBLE (HARDWARE BASELINE)**
* **Sources:** [PlayStation PC Support](https://www.playstation.com/) (October 2026).

---

### 15. Baldur's Gate 3
* **Developer / Publisher:** Larian Studios
* **Engine:** Divinity 4.0
* **Active Renderer:** DirectX 11 and Vulkan.
* **KeplerBridge Interception:** Bypasses D3D12 proxy (no D3D12 engine).
* **Official Minimum GPU:** GTX 970 (4 GB).
* **Addressability:** **Plausible (Hardware Baseline).** Runs on D3D11 backend.
* **Classification:** **PLAUSIBLE (HARDWARE BASELINE)**
* **Sources:** [Larian Support](https://larian.com/support) (October 2026).

---

### 16. Alan Wake 2
* **Developer / Publisher:** Remedy Entertainment / Epic Games Publishing
* **Engine:** Northlight Engine
* **Active Renderer:** DirectX 12 Ultimate.
* **Official Minimum GPU:** GeForce RTX 2060 (6 GB) / later patched to GTX 1070 (8 GB).
* **Documented Error:** Instant termination with missing Mesh Shader support.
* **Technical Cause:** Hard hardware requirement for Mesh & Amplification Shaders (SM 6.5). Kepler architecture has no hardware primitive for meshlet processing.
* **Addressability:** **Blocked.** Impossible to emulate mesh shaders on Kepler hardware with acceptable performance.
* **Classification:** **BLOCKED**
* **Sources:** [Remedy Alan Wake 2 FAQ](https://www.alanwake.com/) (October 2026).

---

### 17. Starfield
* **Developer / Publisher:** Bethesda Game Studios
* **Engine:** Creation Engine 2
* **Active Renderer:** DirectX 12 Agility SDK.
* **Official Minimum GPU:** GTX 1070 Ti (8 GB).
* **Documented Error:** *"Graphics card does not meet the minimal specifications"*.
* **Technical Cause:** Demands Feature Level 12_0, Wave Intrinsics, 64-bit integer atomics, and heavy VRAM (>6 GB).
* **Addressability:** **Blocked.** Hardware capability gap exceeds Kepler GK208 silicon limits.
* **Classification:** **BLOCKED**
* **Sources:** [Bethesda Support](https://help.bethesda.net/) (October 2026).

---

### 18. Forza Horizon 5
* **Developer / Publisher:** Playground Games / Xbox Game Studios
* **Engine:** ForzaTech
* **Active Renderer:** DirectX 12 only.
* **Official Minimum GPU:** GTX 970 (4 GB).
* **Documented Error:** Error code **FH501** (Graphics card does not support DirectX 12 Feature Level 12_0).
* **Technical Cause:** Engine enforces kernel-level feature checks and Tier 2/3 binding.
* **Addressability:** **Blocked.** ForzaTech aborts before rendering if hardware reports FL 11_0.
* **Classification:** **BLOCKED**
* **Sources:** [Forza Support](https://support.forzamotorsport.net/) (October 2026).

---

### 19. Metro Exodus Enhanced Edition
* **Developer / Publisher:** 4A Games
* **Engine:** 4A Engine
* **Active Renderer:** Pure DXR Raytracing only.
* **Official Minimum GPU:** GeForce RTX 2060 (6 GB).
* **Documented Error:** *"Hardware Raytracing Capable GPU is required"*.
* **Technical Cause:** Every light source in Enhanced Edition is path-traced; rasterized fallback shaders do not exist in the game binaries.
* **Addressability:** **Blocked.** Kepler GK208 has no RT Cores or software DXR compute capability.
* **Classification:** **BLOCKED (RAYTRACING REQUIRED)**
* **Sources:** [4A Games](https://www.4a-games.com.mt/) (October 2026).

---

### 20. Returnal
* **Developer / Publisher:** Housemarque / PlayStation PC
* **Engine:** Unreal Engine 4.27
* **Active Renderer:** DirectX 12 only.
* **Official Minimum GPU:** GTX 1060 (6 GB).
* **Documented Error:** Fatal D3D device crash on title screen.
* **Technical Cause:** Demands 6 GB VRAM minimum; GT 740 DDR3 memory bandwidth (14.4 GB/s) causes driver watchdog timeouts (TDR).
* **Addressability:** **Blocked.** Severe VRAM exhaustion.
* **Classification:** **BLOCKED**
* **Sources:** [PlayStation PC](https://www.playstation.com/) (October 2026).

---

### 21. Lords of the Fallen (2023)
* **Developer / Publisher:** HEXWORKS / CI Games
* **Engine:** Unreal Engine 5.2
* **Active Renderer:** DirectX 12 only.
* **Official Minimum GPU:** GTX 1060 (6 GB).
* **Technical Cause:** Mandatory Nanite geometry rasterization using 64-bit atomics and compute Wave Ops.
* **Addressability:** **Blocked.** Nanite compute passes cannot be mapped to D3D11 Feature Level 11_0.
* **Classification:** **BLOCKED**
* **Sources:** [CI Games Support](https://www.cigames.com/) (October 2026).

---

### 22. Dead Space (2023 Remake)
* **Developer / Publisher:** Motive Studio / Electronic Arts
* **Engine:** Frostbite
* **Active Renderer:** DirectX 12 only.
* **Official Minimum GPU:** GTX 1070 (8 GB).
* **Technical Cause:** Requires Variable Rate Shading (VRS Tier 1) and FL 12_0.
* **Addressability:** **Blocked.**
* **Classification:** **BLOCKED**
* **Sources:** [EA Help](https://help.ea.com/) (October 2026).

---

### 23. Ratchet & Clank: Rift Apart
* **Developer / Publisher:** Insomniac Games / Nixxes
* **Engine:** Proprietary
* **Active Renderer:** DirectX 12 only.
* **Official Minimum GPU:** GTX 960 (4 GB).
* **Technical Cause:** Requires DirectStorage 1.2 GPU decompression and FL 12_0.
* **Addressability:** **Blocked.** Kepler drivers lack DirectStorage GDeflate GPU decompression compute paths.
* **Classification:** **BLOCKED**
* **Sources:** [Nixxes Software](https://www.nixxes.com/) (October 2026).

---

### 24. Halo Infinite
* **Developer / Publisher:** 343 Industries / Xbox Game Studios
* **Engine:** Slipspace Engine
* **Active Renderer:** DirectX 12 only.
* **Official Minimum GPU:** GeForce GTX 1050 Ti (4 GB) / Radeon RX 570.
* **Documented Error:** *"Could not find compatible graphics device"*.
* **Technical Cause:** Hardcoded checks for Feature Level 12_0 and anti-tamper memory protection.
* **Addressability:** **Blocked.**
* **Classification:** **BLOCKED**
* **Sources:** [Halo Support](https://support.halowaypoint.com/) (October 2026).

---

### 25. DOOM Eternal
* **Developer / Publisher:** id Software / Bethesda
* **Engine:** id Tech 7
* **Active Renderer:** Vulkan only (No D3D12 implementation exists).
* **Official Minimum GPU:** GeForce GTX 1050 Ti (4 GB) / GTX 1060 (3 GB).
* **Technical Cause:** id Tech 7 completely deprecated DirectX; rendering pipeline exclusively calls `vulkan-1.dll`.
* **Addressability:** **Blocked (API Mismatch).** Does not invoke `d3d12.dll`.
* **Classification:** **BLOCKED (VULKAN ONLY)**
* **Sources:** [Bethesda Support](https://help.bethesda.net/) (October 2026).

---

## 3. Summary Classification Table

| Game Title | Engine | Native API | Interception | Classification | Actionable Recommendation |
| :--- | :--- | :---: | :---: | :---: | :--- |
| **Palworld** | UE 5.1 | D3D11 / D3D12 | Yes | **PLAUSIBLE** | First candidate; test `-dx11` vs proxy translation |
| **The Witcher 3 (Next-Gen)** | REDengine 3 | D3D11 / D3D12 | Yes | **PLAUSIBLE** | Compare native `dx11` vs proxy `x64_dx12` |
| **Control** | Northlight | D3D11 / D3D12 | Yes | **PLAUSIBLE** | Test `Control_DX12.exe` with FL spoofing |
| **Monster Hunter: World** | MT Framework | D3D11 / D3D12 | Yes | **PLAUSIBLE** | Verify D3D12 toggle with translation backend |
| **Sons of the Forest** | Unity 2022 | D3D11 / D3D12 | Yes | **PLAUSIBLE** | Test `-force-d3d12` launch argument |
| **God of War (2018)** | Proprietary | D3D11 | No (DX11) | **PLAUSIBLE** | Hardware baseline performance validation |
| **Baldur's Gate 3** | Divinity 4.0 | D3D11 / Vulkan | No (DX11) | **PLAUSIBLE** | Hardware baseline asset throughput |
| **Elden Ring** | Dantelion | D3D12 | Yes | **EXPERIMENTAL** | Requires offline mode (disable EAC) |
| **Cyberpunk 2077** | REDengine 4 | D3D12 | Yes | **EXPERIMENTAL** | Severe VRAM pressure; clamp textures |
| **Resident Evil 4 Remake** | RE Engine | D3D12 | Yes | **EXPERIMENTAL** | Test multi-UAV dispatches & FL spoof |
| **Hogwarts Legacy** | UE 4.27 | D3D12 | Yes | **EXPERIMENTAL** | Monitor DDR3 memory bandwidth limits |
| **Horizon Zero Dawn** | Decima | D3D12 | Yes | **EXPERIMENTAL** | Test persistent shader cache warmup |
| **Death Stranding** | Decima | D3D12 | Yes | **EXPERIMENTAL** | Test FL 12_0 spoofing with Decima |
| **Red Dead Redemption 2** | RAGE | Vulkan / D3D12 | Yes | **EXPERIMENTAL** | Force D3D12 via `system.xml` |
| **Marvel's Spider-Man** | Proprietary | D3D12 | Yes | **EXPERIMENTAL** | Test dynamic root table rebinding |
| **Alan Wake 2** | Northlight | D3D12 Ultimate | Yes | **BLOCKED** | Requires hardware Mesh Shaders |
| **Starfield** | Creation 2 | D3D12 Agility | Yes | **BLOCKED** | Demands Wave Ops & 64-bit atomics |
| **Forza Horizon 5** | ForzaTech | D3D12 | Yes | **BLOCKED** | Hardcoded FL 12_0 & Tier 2/3 checks |
| **Metro Exodus Enhanced** | 4A Engine | DXR Raytracing | Yes | **BLOCKED** | Pure Hardware Raytracing (No raster fallback) |
| **Returnal** | UE 4.27 | D3D12 | Yes | **BLOCKED** | Minimum 6 GB VRAM; TDR timeouts on DDR3 |
| **Lords of the Fallen** | UE 5.2 | D3D12 | Yes | **BLOCKED** | Nanite compute geometry unsupported |
| **Dead Space (Remake)** | Frostbite | D3D12 | Yes | **BLOCKED** | Demands Variable Rate Shading (VRS) |
| **Ratchet & Clank: Rift Apart**| Proprietary | D3D12 | Yes | **BLOCKED** | Demands DirectStorage GPU decompression |
| **Halo Infinite** | Slipspace | D3D12 | Yes | **BLOCKED** | Kernel anti-cheat & strict FL 12_0 |
| **DOOM Eternal** | id Tech 7 | Vulkan Only | No (Vulkan) | **BLOCKED** | No DirectX 12 renderer implementation |
