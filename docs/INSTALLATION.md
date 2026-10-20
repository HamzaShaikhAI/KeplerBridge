# KeplerBridge — Installation & Configuration Guide

**Document Version:** 1.0.0
**Target Hardware:** NVIDIA Kepler (GK104, GK107, GK208) & Native Direct3D 11 GPUs
**Operating System:** Windows 10 x64 / Windows 11 x64

---

## 1. Quick Installation (Drop-in Proxy)

KeplerBridge requires no installers, drivers, or system modifications. It deploys locally alongside target 64-bit Direct3D 12 game executables:

1. **Download Release:** Obtain the latest `keplerbridge-v0.8.0-rc1-x64.zip` from [GitHub Releases](https://github.com/HamzaShaikhAI/KeplerBridge/releases).
2. **Locate Game Binary Directory:** Open the folder containing the main 64-bit game executable.
   * *Example (Unreal Engine):* `<GameFolder>\Binaries\Win64\` (next to the main `<Game>-Win64-Shipping.exe`).
   * *Example (Standard Engine):* `<GameFolder>\` (next to `<Game>.exe`).
3. **Extract Files:** Copy the following files into the game executable folder:
   * `d3d12.dll` (The translation proxy)
   * `kepler_bridge.ini` (Configuration file)
4. **Launch Game:** Run the game normally. Windows will automatically load the local `d3d12.dll` instead of the system runtime.

---

## 2. Verification

After launching your game:
1. Check the game folder for `kepler_bridge.log`.
2. Open `kepler_bridge.log` in a text editor to verify that KeplerBridge initialized:
   ```text
   [INFO] [ProxyLoader] KeplerBridge Proxy Initialized. Target Architecture: Kepler (FL 11_0)
   [INFO] [Device] D3D12CreateDevice intercepted. Spoofing FL 12_0, authentic hardware FL: 11_0
   [INFO] [Swapchain] DXGI Swapchain intercepted and wrapped. Presentation mode: DISCARD
   ```
3. A `kepler_cache/` directory will be created to store pre-translated DXBC shaders, accelerating subsequent game launches.

---

## 3. Configuration Reference (`kepler_bridge.ini`)

You can customize proxy behavior by editing `kepler_bridge.ini` with any standard text editor:

### `[General]` Section
| Parameter | Default | Description | Recommended Setting |
| :--- | :---: | :--- | :--- |
| `EnableLogging` | `1` | Enables diagnostic logging to `kepler_bridge.log`. | `1` during initial setup; `0` for maximum framerate. |
| `LogVerboseCalls` | `0` | Logs per-frame draw, dispatch, and resource calls. | Keep `0` unless diagnosing a specific rendering glitch. |

### `[Spoof]` Section
| Parameter | Default | Description | Recommended Setting |
| :--- | :---: | :--- | :--- |
| `EnableFeatureLevelSpoof` | `1` | Spoofs Feature Level 12_0 during device creation. | `1` (required for games with strict startup FL checks). |
| `TargetFeatureLevel` | `12_0` | Feature level reported to the game during initialization. | `12_0` |
| `SpoofVRAMMB` | `4096` | VRAM size reported to game engines (in MB). | Match your card or keep `4096`. |

### `[Downgrader]` Section
| Parameter | Default | Description | Recommended Setting |
| :--- | :---: | :--- | :--- |
| `EnableShaderInspection` | `1` | Inspects DXBC/DXIL shader bytecode for capability matching. | `1` |
| `EnableShaderFallback` | `1` | Applies translation policies to unsupported instructions. | `1` |
| `EnableTextureClamping` | `1` | Clamps large textures to prevent memory bandwidth saturation. | `1` on 64-bit DDR3 GPUs (GT 730 / GT 740). |
| `MaxTextureResolution` | `1024` | Maximum texture dimension (e.g. `512`, `1024`, `2048`). | `1024` for balanced quality and performance. |
| `StripHeavyShaders` | `0` | Replaces heavy post-process shaders with lightweight stubs. | `0` |

### `[Translation]` Section
| Parameter | Default | Description | Recommended Setting |
| :--- | :---: | :--- | :--- |
| `EnableD3D11Translation` | `1` | Translates D3D12 commands into D3D11 immediate execution. | `1` (core translator). |
| `EmulateBarriers` | `1` | Issues context flushes on resource barrier transitions. | `1` |
| `EnableStateTracking` | `1` | Tracks active pipeline state, viewports, and bindings. | `1` |
| `ImmediateQueueFlush` | `1` | Replays command lists immediately upon queue submission. | `1` |
| `EmulateQueries` | `1` | Emulates D3D12 query heaps and timestamp requests. | `1` |

### `[Cache]` Section
| Parameter | Default | Description | Recommended Setting |
| :--- | :---: | :--- | :--- |
| `EnableShaderDiskCache` | `1` | Caches translated DXBC shaders to disk for warm bypass. | `1` (reduces stutter dramatically on repeat launches). |
| `CacheDirectory` | `kepler_cache/shaders` | Relative path where pre-compiled shaders are stored. | Default |

---

## 4. Uninstallation

To cleanly remove KeplerBridge:
1. Delete `d3d12.dll` from the game directory.
2. Delete `kepler_bridge.ini` and `kepler_bridge.log`.
3. *(Optional)* Delete the `kepler_cache/` folder to reclaim disk space.

The game will immediately revert to standard system DirectX 12 handling on its next launch.
