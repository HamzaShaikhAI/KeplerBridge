# KeplerBridge — Troubleshooting & Diagnostics Guide

**Document Version:** 1.0.0
**Target Architecture:** NVIDIA Kepler & Legacy Direct3D 11 GPUs

---

## 1. Quick Triage Flowchart

```
Game fails to launch or render
               │
               ▼
Does kepler_bridge.log exist in game folder?
        ├── NO  ──► Proxy is not being loaded by game (check folder / anti-cheat)
        └── YES ──► Open kepler_bridge.log and check last error entry
                      │
                      ├── "Feature level unsupported" ──► Enable FeatureLevelSpoof=1
                      ├── "DXGI_ERROR_DEVICE_REMOVED"  ──► GPU TDR (lower texture settings)
                      ├── "DXIL bytecode unsupported"   ──► Game requires SM 6.0 translation
                      └── "Missing export"             ──► Report missing D3D12 entry point
```

---

## 2. Common Issues & Solutions

### 1. Game Crashes Immediately on Startup (Zero Log Generated)
* **Probable Cause:** The game executable is loading the system `C:\Windows\System32\d3d12.dll` directly or is protected by kernel-level anti-cheat.
* **Solutions:**
  1. Verify `d3d12.dll` is placed in the exact directory of the main shipping binary (e.g. for Unreal Engine games, inside `<GameFolder>\Binaries\Win64\`, not the root game folder).
  2. Ensure the game is running in **offline single-player mode**. Online anti-cheat services (Easy Anti-Cheat, BattlEye, Vanguard) block third-party proxy DLLs by design.

### 2. Feature Level Requirement Error (e.g., "D3D12 12_0 Feature Level Required")
* **Probable Cause:** The game checks for Feature Level 12_0 before allowing initialization.
* **Solutions:**
  1. Open `kepler_bridge.ini`.
  2. Confirm the following settings:
     ```ini
     [Spoof]
     EnableFeatureLevelSpoof=1
     TargetFeatureLevel=12_0
     ```

### 3. Severe Stutter During First Few Minutes of Gameplay
* **Probable Cause:** Shader compilation and pipeline state object (PSO) caching are occurring in real time.
* **Solutions:**
  1. Allow the game to complete its title screen shader pre-warming phase.
  2. Confirm `EnableShaderDiskCache=1` is set in `kepler_bridge.ini`.
  3. Once cached into `kepler_cache/shaders/`, subsequent launches bypass compilation completely (loading in under 0.2 ms per shader).

### 4. Low Framerate on 64-bit DDR3 Cards (NVIDIA GT 730 / GT 740)
* **Probable Cause:** Memory bandwidth saturation (14.4 GB/s DDR3 bus bottlenecked by 2K/4K texture maps and 1080p framebuffers).
* **Solutions:**
  1. Set in-game resolution to **1280x720 (720p)** or **1366x768**.
  2. Enable dynamic resolution scaling, AMD FSR (Performance), or Unreal Engine TSR (50%–60%).
  3. In `kepler_bridge.ini`, configure texture clamping:
     ```ini
     [Downgrader]
     EnableTextureClamping=1
     MaxTextureResolution=1024
     ```

### 5. Black Screen with Background Audio Playing
* **Probable Cause:** Swapchain presentation format mismatch or flip-model incompatibility on older WDDM drivers.
* **Solutions:**
  1. KeplerBridge automatically converts `DXGI_SWAP_EFFECT_FLIP_DISCARD` to `DXGI_SWAP_EFFECT_DISCARD` and sanitizes tearing flags.
  2. Switch the game from Exclusive Fullscreen to **Borderless Windowed** or **Windowed** mode in the game settings or via `Alt + Enter`.
  3. Ensure your NVIDIA driver is updated to version 456.71 WHQL.

### 6. Driver Crash or Watchdog Timeout (`0x887A0005` / `DXGI_ERROR_DEVICE_REMOVED`)
* **Probable Cause:** Windows TDR (Timeout Detection and Recovery) triggered by an excessively long compute shader dispatch or massive monolithic PSO compilation burst.
* **Solutions:**
  1. KeplerBridge includes automated 128-PSO burst throttling to yield CPU execution slices (`SwitchToThread()`) and flush driver command queues.
  2. Lower in-game Shadow and Volumetric Lighting quality to reduce compute dispatch durations.

---

## 3. Diagnostic Error Codes Reference

When an error occurs, KeplerBridge logs a structured diagnostic code in `kepler_bridge.log`:

| Error Code | Identifier | Description | Remedial Action |
| :---: | :--- | :--- | :--- |
| **`KB_ERR_1001`** | `DeviceCreationFailed` | Native D3D11 device creation failed under Feature Level 11_0. | Verify GPU drivers and DirectX runtimes are properly installed. |
| **`KB_ERR_1002`** | `SwapchainCreationFailed`| Failed to wrap or create the DXGI swapchain. | Switch game to windowed/borderless mode; disable tearing flags. |
| **`KB_ERR_1003`** | `DeviceTimeout` | GPU fence event query wait timed out during queue drainage. | Reduce heavy compute shader workloads; lower graphics settings. |
| **`KB_ERR_2001`** | `UnsupportedShader` | Shader bytecode contains unsupported instructions (e.g. SM 6.x wave intrinsics). | Check `docs/GAME_COMPATIBILITY.md` for game-specific translation status. |
| **`KB_ERR_2002`** | `RootSignatureOverflow` | Root descriptor tables exceed hardware slots (SRV > 128, UAV > 8). | Title requires multi-space stride remapping or heap clamping. |
| **`KB_ERR_3001`** | `DiskCacheCorrupted` | Stored shader disk cache failed FNV-1a checksum validation. | Proxy automatically deletes corrupted cache and recompiles cleanly. |

---

## 4. Reporting Issues

If a title encounters an unresolved defect:
1. Copy `kepler_bridge.log` from the game folder.
2. Note your exact GPU model, driver version, and in-game graphics settings.
3. Submit a report using our [Game Compatibility Issue Template](https://github.com/HamzaShaikhAI/KeplerBridge/issues/new?template=game_compatibility.md).
