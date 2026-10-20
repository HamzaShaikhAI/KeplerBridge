# KeplerBridge — Real Game Validation Plan & Test Protocol

**Document Version:** 1.0.0
**Status:** ACTIVE PROTOCOL
**Target Hardware:** NVIDIA GeForce GT 740 (Kepler GK208B, 4052 MB DDR3, PCI ID `10DE:128B`, Native Feature Level 11_0, Driver 456.71, Windows 10 x64)
**Proxy Version Under Test:** KeplerBridge `v0.8.0-rc1` (`d3d12.dll` SHA-256: `03e685a1592844b0bcddb4e3e9a4512d88a27bc33359db7caa9546bb6867e8a8`)

---

## 1. Objectives & Ground Truth Principles

The purpose of this validation protocol is to conduct rigorous, empirical, and repeatable physical hardware acceptance tests of commercial PC titles using the KeplerBridge Direct3D 12-to-Direct3D 11 translation proxy.

### Strict Non-Fabrication Rules
1. **Never Conflate Native Renders with Translation:** Games capable of running natively via Direct3D 11 (`-dx11` command-line switch or in-game settings) must be tested in both modes. Native D3D11 results serve strictly as an external performance baseline and must never be reported as a KeplerBridge translation achievement.
2. **End-to-End Playability Requirement:** A title may only be designated as **VERIFIED PLAYABLE** if it successfully boots through the proxy, renders initial UI, enters interactive 3D gameplay, sustains at least 15 minutes of continuous interactive play without crashing, device removal (`0x887A0005`), or game-breaking visual artifacts, and maintains playable frame pacing.
3. **Transparent Failure Classification:** If a title encounters unsupported Shader Model 6.x instructions, root signature overflow, unhandled UAV barrier transitions, or GPU hangs, the exact failing API call, shader bytecode, and error code must be documented in the compatibility matrix.

---

## 2. Five Acceptance Pillars for Commercial Validation

Every candidate workload must be evaluated against the five standardized acceptance gates:

| Gate | Criterion | Pass Condition | Verification Method |
| :---: | :--- | :--- | :--- |
| **Gate 1** | **Proxy Injection & Hooking** | The game loads local `d3d12.dll`, executes `D3D12CreateDevice`, and queries capabilities. | Inspect `kepler_bridge.log` for successful hook and device initialization banner. |
| **Gate 2** | **Device & Swapchain Creation** | Native D3D11 device created at Feature Level 11_0; swapchain created and hooked. | Swapchain created with `DXGI_SWAP_EFFECT_DISCARD` fallback without tearing flags. |
| **Gate 3** | **Interactive Gameplay Entry** | Application passes splash screens, compile screens, main menu, and loads 3D level. | Visual confirmation and log telemetry reaching interactive game loop. |
| **Gate 4** | **Stability & Visual Fidelity** | 15 minutes of uninterrupted play with 0 WDDM watchdog TDRs (`0x887A0005`) or fatal crashes. | GPU driver stability verified; no black screen, memory leak, or geometry explosion. |
| **Gate 5** | **Resource & Bandwidth Viability** | Frame pacing, average FPS, and VRAM utilization within GT 740 DDR3 (14.4 GB/s) limits. | Overlay metrics recorded via MSI Afterburner / RTSS. |

---

## 3. Prioritized Game Test Sequence

Validation must proceed strictly one game at a time. The test queue is prioritized based on architectural alignment with KeplerBridge v0.8.0-rc1:

### Test Target 1: *Stray* (BlueTwelve Studio / Annapurna Interactive)
* **Engine:** Unreal Engine 4.27 (Modified)
* **Default Render Path:** Direct3D 12 (DirectX 11 available via `-dx11`)
* **Architectural Rationale:** Pure UE4 forward/deferred hybrid renderer. Uses standard DXBC SM 5.0/5.1 shaders without mandatory SM 6.x wave intrinsics or raytracing. Ideal first commercial test of KeplerBridge PSO extraction and swapchain presentation.
* **Target Settings:** 1280x720, Low Presets, Resolution Scale 70%, Motion Blur OFF.
* **Test Procedure:**
  1. Record Native Baseline: Launch with `-dx11` parameter without proxy. Record baseline FPS, VRAM, and frametime stability.
  2. Deploy KeplerBridge: Place `d3d12.dll` and `kepler_bridge.ini` in `Stray\Hk_project\Binaries\Win64\`.
  3. Remove `-dx11` parameter and launch game.
  4. Verify proxy hook in `kepler_bridge.log`.
  5. Traverse Prologue alley sequence (15 minutes).

### Test Target 2: *Devil May Cry 5* (Capcom)
* **Engine:** RE Engine
* **Default Render Path:** Direct3D 12 (DirectX 11 fallback historically supported)
* **Architectural Rationale:** RE Engine features clean root signatures, deterministic compute workloads, and lightweight descriptor heap layouts. Exercises multi-slot UAVs (slots 0–7) and compute dispatch fences.
* **Target Settings:** 1280x720, Lowest Preset, Texture Quality Low (0.5 GB), FXAA only.
* **Test Procedure:**
  1. Deploy `d3d12.dll` and `kepler_bridge.ini` in `Devil May Cry 5\` root directory.
  2. Launch game in offline mode.
  3. Monitor Mission 02 combat sequence for compute shader particle effects and PSO pre-warming timeouts.

### Test Target 3: *Resident Evil 2 Remake* (Capcom)
* **Engine:** RE Engine
* **Default Render Path:** Direct3D 12
* **Architectural Rationale:** Direct companion to DMC5; tests heavy streaming buffers, tone-mapping shaders, and compute-driven screen space ambient occlusion.
* **Target Settings:** 1280x720, Preset "Prioritize Performance", Mesh Quality Low, Shadow Quality Min.
* **Test Procedure:**
  1. Deploy proxy and configuration into game directory.
  2. Execute Raccoon Police Department introductory hallway sequence.
  3. Verify memory usage against 4052 MB physical VRAM limit.

### Test Target 4: *Palworld* (Pocketpair) — Experimental Target
* **Engine:** Unreal Engine 5.1
* **Default Render Path:** Direct3D 12 (Mandatory Shader Model 6.0 DXIL, Niagara wave intrinsics)
* **Investigation Gate:** Gate P10-A (Validation Protocol).
* **Target Settings:** 1280x720, Lowest Preset, Resolution Scale 50%.
* **Test Procedure:**
  1. Record Native Baseline: Launch with `-dx11` command-line switch without proxy. Document native Kepler GT 740 performance.
  2. Deploy KeplerBridge v0.8.0-rc1 into `Pal\Binaries\Win64\`.
  3. Launch without `-dx11` to inspect exact shader bytecode rejection or compilation failure in `kepler_bridge.log`.
  4. Do NOT claim playability unless DXIL emulation or replacement policy successfully overcomes Niagara particle barriers.

---

## 4. Telemetry Collection & Environment Setup

### Required Instrumentation
1. **KeplerBridge Diagnostics:**
   Set `kepler_bridge.ini` to:
   ```ini
   [General]
   EnableLogging=1
   LogVerboseCalls=0

   [Spoof]
   EnableFeatureLevelSpoof=1
   TargetFeatureLevel=12_0
   SpoofVRAMMB=4096

   [Downgrader]
   EnableShaderInspection=1
   EnableShaderFallback=1
   EnableTextureClamping=1
   MaxTextureResolution=1024
   StripHeavyShaders=0

   [Translation]
   EnableD3D11Translation=1
   EmulateBarriers=1
   EnableStateTracking=1
   ImmediateQueueFlush=1
   EmulateQueries=1

   [Cache]
   EnableShaderDiskCache=1
   CacheDirectory=kepler_cache/shaders
   ```
2. **Hardware Metric Logging:**
   Use MSI Afterburner / RTSS to log:
   - Average FPS, 1% Low FPS, 0.1% Low FPS.
   - Frametime graph variance (ms).
   - Dedicated VRAM allocated and committed (MB).
   - Core clock frequency (MHz) and GPU temperature (°C).

---

## 5. Standard Test Report Template

For each evaluated title, maintain a test entry matching this specification:

```markdown
### Game Validation Entry: [Title]
* **Date & Time:** [ISO 8601 Timestamp]
* **Target Executable:** `[Path\To\Game.exe]`
* **Engine / API:** [e.g., Unreal Engine 4.27 / D3D12]
* **Driver Version:** 456.71 WHQL
* **Proxy Build:** v0.8.0-rc1 (`03e685a1...`)

#### Baseline Native D3D11 Comparison (if supported)
* **Average FPS:** [e.g., 34.2 FPS]
* **1% Lows:** [e.g., 22.8 FPS]
* **VRAM Utilization:** [e.g., 1850 MB]

#### KeplerBridge D3D12 Translation Results
* **Gate 1 (Hooking):** [PASS / FAIL] — [Log notes]
* **Gate 2 (Device/Swapchain):** [PASS / FAIL] — [Format/Mode]
* **Gate 3 (Gameplay Entry):** [PASS / FAIL] — [Level loaded]
* **Gate 4 (Stability 15 min):** [PASS / FAIL] — [0 TDRs / Crash info]
* **Gate 5 (Frametime & VRAM):**
  * **Average FPS:** [FPS]
  * **1% Lows:** [FPS]
  * **VRAM Utilization:** [MB]
* **Artifacts Observed:** [None / Texture flickering / Missing compute lighting]
* **Final Verdict:** [VERIFIED PLAYABLE / PARTIALLY WORKING / BLOCKED]
```
