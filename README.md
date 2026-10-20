# KeplerBridge

> **Current Status: Release Candidate (`v0.8.0-rc1`)**
> *Pre-release candidate undergoing physical hardware regression verification and compatibility research. Not yet certified for general production release. All commercial game titles require physical acceptance testing before being marked playable.*

**KeplerBridge** is an open-source, bounded Direct3D 12-to-Direct3D 11 compatibility and translation proxy engineered specifically for legacy graphics hardware, with the **NVIDIA GeForce GT 740** (Kepler architecture, native Direct3D 11 Feature Level 11_0) as the primary physical reference platform.

By deploying as a drop-in `d3d12.dll` proxy placed alongside target 64-bit Windows applications, KeplerBridge intercepts Direct3D 12 device creation, negotiates feature-level capability probing, translates command recording into Direct3D 11 immediate execution, manages a persistent shader cache, and synchronizes compute dispatches directly onto legacy hardware pipelines.

---

## 1. Problem Statement & Intended Audience

Modern PC titles increasingly require Direct3D 12 at startup, refusing to launch on GPUs that lack native D3D12 hardware feature levels (`12_0` or `12_1`), even when the workload's actual rendering pipeline relies only on standard rasterization and compute shaders representable in Direct3D 11.0.

Conversely, older GPUs such as the NVIDIA Kepler family (GK208, GK107, GK104) remain physically capable of rendering modern 720p/1080p graphics, but their vendor driver support is frozen at WDDM 2.7 / Driver 456.71 with native hardware support capped at Feature Level 11_0 (`0xb000`).

KeplerBridge is designed for:
* **Hardware Preservationists & Enthusiasts:** Extending the functional lifecycle of legacy PC graphics hardware.
* **Graphics Compatibility Researchers:** Studying API translation boundaries between low-level command lists and immediate hardware contexts.
* **Offline Gamers:** Enabling bounded, non-raytraced Direct3D 12 titles on older PCs without third-party translation bloat.

---

## 2. Architecture Overview & Translation Flow

KeplerBridge translates a bounded subset of Direct3D 12 operations into Direct3D 11 runtime calls while enforcing physical Feature Level 11_0 constraints:

```mermaid
flowchart TD
    App["D3D12 Application / Game Engine"] -->|"Loads local proxy"| Proxy["KeplerBridge (d3d12.dll)"]

    subgraph KeplerBridgeCore["KeplerBridge Core Subsystems"]
        Proxy -->|"Intercepts D3D12CreateDevice"| PolicyEngine["Capability & Policy Engine"]
        PolicyEngine -->|"Wraps Device"| WrappedDev["WrappedD3D12Device (ID3D12Device1)"]

        WrappedDev -->|"Records commands"| CmdTranslator["Command Translator & Batcher"]
        WrappedDev -->|"Inspects bytecode"| ShaderEngine["Bounded Shader Inspector & Cache"]

        ShaderEngine -->|"Hash lookup (64-bit FNV-1a)"| DiskCache[("Persistent Shader Cache (.dxbc)")]
        ShaderEngine -->|"HLSL text fallback"| D3DCompile["d3dcompiler_47.dll (SM 5.0)"]

        CmdTranslator -->|"Replays batch"| Backend["D3D11Backend (Immediate Context)"]
        CmdTranslator -->|"Maps descriptors & spaces"| ResManager["Resource & UAV Manager"]
        CmdTranslator -->|"Stages dynamic data"| StagingBuffer["256B Scratch Constant Buffer (cb13)"]
        CmdTranslator -->|"Drains queue via D3D11_QUERY_EVENT"| FenceSync["GPU Completion & Fence Sync"]
    end

    Backend -->|"Direct D3D11 / DXGI runtime calls"| D3D11Runtime["Windows D3D11 Runtime (d3d11.dll)"]
    D3D11Runtime -->|"WDDM 2.7 Driver 456.71"| Hardware["NVIDIA GeForce GT 740 (Kepler GK208, FL 11_0)"]
```

### Translation Pipeline Sequence
1. **Ghost Hook Interception:** Exports all canonical Direct3D 12 entry points. If compatibility interception is disabled in configuration, calls pass through directly to `C:\Windows\System32\d3d12.dll`.
2. **Device Wrapping:** Intercepts `D3D12CreateDevice`, queries the underlying adapter, creates an authentic hardware device, and wraps it in `WrappedD3D12Device`.
3. **Command Recording:** Command allocators and graphics command lists record draw calls, state changes, root constants, descriptor tables, and resource transitions into contiguous `CommandBatch` buffers.
4. **Command Replay:** On `ExecuteCommandLists()`, the batch is replayed onto the Direct3D 11 immediate device context, mapping D3D12 resources, viewports, root constants, and UAVs.
5. **GPU Synchronization:** On `CommandQueue::Signal()`, the backend issues an authentic `D3D11_QUERY_EVENT` query via `WaitForGpuCompletion()`, guaranteeing the physical GPU has drained before advancing CPU fence values.

---

## 3. Supported Reference Baseline

KeplerBridge is engineered and verified on physical hardware meeting the following specification:

| Parameter | Reference Workstation Specification |
| :--- | :--- |
| **GPU Model** | NVIDIA GeForce GT 740 (GK208B, 28nm, 384 CUDA Cores, PCI ID `10DE:128B`) |
| **Native Feature Level** | Direct3D 11.0 (`0xb000`) |
| **VRAM Configuration** | 4052 MB DDR3 (64-bit Bus, ~14.4 GB/s Bandwidth) |
| **Installed Driver** | NVIDIA GeForce Game Ready Driver 456.71 (WDDM 2.7) |
| **Operating System** | Windows 10 Pro x64 (Version 22H2, Build 10.0.19045) |
| **Compiler Toolchain** | MinGW-w64 GCC 16.2.0 (x86_64-ucrt-posix-seh), CMake 4.4.3, Ninja 1.13.2 |

---

## 4. Supported vs. Unsupported Direct3D Features

KeplerBridge does **not** synthesize missing GPU hardware capabilities. Unsupported features fail honestly with explicit error codes (`KB_ERR_*`) rather than causing undefined driver crashes.

| Direct3D 12 Feature | KeplerBridge Status | Architectural Reality & Explanation |
| :--- | :---: | :--- |
| **Feature Level Probing (11_0 -> 12_0)** | **Supported (Spoofed)** | Reports `D3D_FEATURE_LEVEL_12_0` in `CheckFeatureSupport()` while mapping calls to FL 11_0 hardware. |
| **Command Lists & Allocators** | **Supported** | In-memory command recording with COM child-object hierarchy preservation (`GetDevice()`). |
| **Multi-Slot Compute & Graphics UAVs** | **Supported** | Up to 8 concurrent UAV slots (slots 0–7) with atomic append/consume counter reset and preservation. |
| **Root Signatures (Version 1.0 & 1.1)** | **Supported** | Descriptor table mapping with register-space stride offset remapping (`space0` is 1:1 mapped). |
| **Dynamic Root Constants** | **Supported** | Staged via persistent 256-byte scratch buffer (`cb13`) with dirty-range upload suppression. |
| **Shader Model 5.0 (DXBC)** | **Supported** | Direct execution and dynamic compilation from HLSL source text via `d3dcompiler_47.dll`. |
| **Persistent Shader Disk Caching** | **Supported** | 64-bit FNV-1a non-cryptographic caching with warm compiler bypass and corruption auto-eviction. |
| **DXGI Swapchain Fallback** | **Supported** | Transparent fallback from `FLIP_DISCARD` to `DISCARD` with tearing flag (`0x200`) sanitization. |
| **PSO Compilation Throttling** | **Supported** | Flushes driver command queues and yields thread slices every 128 consecutive PSOs to prevent TDR. |
| **Arbitrary Pre-Compiled DXIL (SM 6.x)** | **Unsupported** | `d3dcompiler_47.dll` compiles HLSL source text, not DXIL bitcode. Native driver rejects DXIL (`0x80004005`). |
| **Wave Intrinsics (`WaveActiveSum`, etc.)** | **Unsupported** | Kepler GK208 lacks hardware SIMD cross-lane warp instructions. Software emulation requires DXIL lowering. |
| **DirectX Raytracing (DXR Tier 1.0/1.1)** | **Unsupported** | Physically impossible on Kepler hardware (no RT cores, no BVH hardware traversal). |
| **Mesh & Amplification Shaders (SM 6.5)** | **Unsupported** | Physically impossible on Kepler (primitive pipeline is fixed-function hardware assembler). |
| **Variable Rate Shading (VRS Tier 1/2)** | **Unsupported** | Hardware unsupported on Kepler architecture. |
| **Resource Binding Tier 3 (Full Bindless)** | **Unsupported** | Kepler silicon is restricted to Resource Binding Tier 1 (descriptor tables with hardware limits). |
| **DirectStorage GDeflate GPU Decompression**| **Unsupported** | Requires DirectStorage 1.2 compute shaders not available on FL 11_0. |
| **64-bit Atomics in Compute** | **Unsupported** | `InterlockedCompareExchange64` does not exist in D3D11 Feature Level 11_0 hardware. |

---

## 5. Technical Accuracy & Ground Truth Principles

To maintain professional engineering integrity, KeplerBridge adheres to the following ground truths:

1. **Feature Level Spoofing Is Not Hardware Creation:** Spoofing `D3D_FEATURE_LEVEL_12_0` allows games to pass initial device creation checks, but it cannot create missing hardware silicon. If a game subsequently submits instructions requiring FL 12_0 hardware (e.g. Tier 3 bindless heaps or mesh shaders), execution will fail.
2. **`d3dcompiler_47.dll` Is Not a DXIL Decompiler:** Microsoft's `d3dcompiler_47.dll` compiles high-level HLSL source text into legacy DXBC bytecode. It cannot decompile, disassemble, or translate pre-compiled Shader Model 6.x DXIL bitcode into DXBC.
3. **Cache Hits Do Not Synthesize DXIL:** KeplerBridge's persistent shader disk cache reuses previously compiled DXBC bytecode blobs. It does not magically translate DXIL into DXBC on cache misses.
4. **FNV-1a Is Non-Cryptographic:** KeplerBridge uses 64-bit Fowler–Noll–Vo (FNV-1a) hashing strictly for high-throughput in-memory descriptor lookups and disk cache keying. It is **not** a cryptographic hash.
5. **Zero Third-Party Application Dependencies:** KeplerBridge links statically against the C++20 runtime and depends strictly on standard Windows operating system DLLs (`d3d11.dll`, `dxgi.dll`, `KERNEL32.dll`, `d3dcompiler_47.dll`, and Windows UCRT).
6. **Unit Tests Do Not Equal Commercial Playability:** Passing 111/111 unit and regression tests on physical hardware confirms that KeplerBridge's translation subsystems conform to Direct3D 11 semantics. It does **not** prove that commercial games are playable end-to-end.

---

## 6. Installation, Configuration & Safe Removal

### Installation
1. Extract the release archive (`keplerbridge-v0.8.0-rc1-x64.zip`).
2. Copy `d3d12.dll` and `kepler_bridge.ini` into the directory containing the target 64-bit game executable (e.g. next to `game.exe`).
3. Verify that the game is set to run in **offline single-player mode**.
4. Launch the game executable.

### Configuration (`kepler_bridge.ini`)
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
EnableShaderFallback=0
EnableTextureClamping=0
MaxTextureResolution=1024

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

### Safe Removal
To completely uninstall KeplerBridge from a game, delete `d3d12.dll`, `kepler_bridge.ini`, `kepler_bridge.log`, and the `kepler_cache/` directory from the game folder. The application will immediately revert to the system's native Direct3D 12 runtime without registry modifications.

---

## 7. Diagnostics & Error Telemetry

KeplerBridge outputs structured telemetry to `kepler_bridge.log` in the application directory:
* **Log Rotation:** Active log files are automatically capped at 10 MB and rotated to `kepler_bridge.log.old`.
* **Path Privacy Redaction:** Windows user directory paths are sanitized to protect user privacy (e.g. `C:\Users\[USER]\AppData\...`).
* **Standardized Diagnostic Error Codes (`KB_ERR_*`):**
  * `KB_OK (0)`: Operation completed successfully.
  * `KB_ERR_DEVICE_REMOVED (1001)`: GPU hardware triggered device removal (`0x887A0005`).
  * `KB_ERR_INIT_FAILED (1002)`: D3D11 device/context initialization failed.
  * `KB_ERR_DEVICE_TIMEOUT (1003)`: GPU event query timed out waiting for work completion.
  * `KB_ERR_INVALID_DESCRIPTOR (2001)`: Out-of-bounds or malformed descriptor handle.
  * `KB_ERR_UNSUPPORTED_STAGE (3002)`: Geometry/Hull/Domain stage or unsupported shader stage requested.
  * `KB_ERR_UNSUPPORTED_INSTRUCTION (3001)`: Shader Model 6.x DXIL or wave intrinsics encountered.
  * `KB_ERR_SHADER_COMPILATION (3003)`: `D3DCompile` failed to produce valid DXBC.
  * `KB_ERR_CACHE_CORRUPTION (3004)`: Tampered cache entry evicted and recompiled.
  * `KB_ERR_SWAPCHAIN_DENIED (5002)`: Swapchain creation rejected by runtime.

---

## 8. Building from Source

### Build Prerequisites
* **Operating System:** Windows 10 x64 / Windows 11 x64
* **Compiler:** MinGW-w64 GCC with C++20 support (tested on GCC 16.2.0 UCRT64)
* **Build System:** CMake 3.20+ with Ninja (tested on CMake 4.4.3, Ninja 1.13.2)

### Build Commands
```powershell
# Clone the repository
git clone https://github.com/your-org/KeplerBridge.git
cd KeplerBridge

# Configure release build with Ninja
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build proxy DLL and all 15 regression test executables
cmake --build build --config Release

# Verify output artifacts
Get-Item build/d3d12.dll
```

---

## 9. Testing Methodology & Hardware Regression Suite

KeplerBridge maintains an automated 15-executable regression test suite executed directly against physical Kepler GT 740 hardware:

```powershell
# Run complete hardware regression test suite
$tests = Get-ChildItem -Path .\build -Filter "test_*.exe"
foreach ($t in $tests) {
    Write-Host "Running $($t.Name)..."
    & $t.FullName
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Test failed: $($t.Name) with exit code $LASTEXITCODE"
        break
    }
}
```

### Verified Hardware Suite Results (111 / 111 Passed)
| Test Executable | Tests | Subsystem Verified |
| :--- | :---: | :--- |
| `test_d3d11_standalone.exe` | 2/2 | Standalone D3D11 device and adapter enumeration |
| `test_d3d12.exe` | 1/1 | Ghost hook proxy symbol export and passthrough |
| `test_d3d12_compat.exe` | 3/3 | Feature level probing, spoofing, and capability reporting |
| `test_d3d12_compute.exe` | 12/12 | End-to-end compute dispatch, fence sync, and numerical readback proof |
| `test_d3d12_multi_uav.exe` | 12/12 | 8-slot UAV binding, atomic append counters, and dirty hazard barriers |
| `test_d3d12_rendering.exe` | 10/10 | Graphics pipeline rendering, vertex/pixel shaders, and texturing |
| `test_d3d12_translation.exe` | 7/7 | Command recording, COM identity, and fence event synchronization |
| `test_dxgi_swapchain.exe` | 9/9 | Swapchain lifecycle, discard fallback, and tearing flag sanitization |
| `test_perf_profile.exe` | 5/5 | Disk cache bypass (365x), shadow buffer staging (2.5x), and PSO burst throttling |
| `test_policy_engine.exe` | 7/7 | Compatibility rules, texture clamping, and feature policies |
| `test_pso_interception.exe` | 5/5 | PSO interception, bytecode analysis, and honest DXIL rejection |
| `test_shader_cache_persistence.exe`| 5/5 | Multi-process cache write, warm hit bypass, and corruption recovery |
| `test_shader_compat.exe` | 7/7 | Shader policy evaluation, fallback compilation, and salt invalidation |
| `test_shader_inspector.exe` | 11/11 | DXBC container parsing, chunk offset verification, and version decoding |
| `test_translation_engine.exe` | 17/17 | Root constants, multi-space mapping, limit checks, initial PSO preservation, and sync queries |
| **TOTAL VERIFIED ON HARDWARE** | **111 / 111** | **100% Pass Rate across all 15 test suites on NVIDIA GeForce GT 740** |

---

## 10. Commercial Game Compatibility Catalogue (30 Targets)

The 30 commercial PC titles recovered from project engineering documentation are categorized according to physical evidence and architectural readiness:

### Category Definitions
* **VERIFIED (Category A):** Confirmed in-game gameplay operational through KeplerBridge passing all 5 Acceptance Pillars. (Currently 0 titles in Group B).
* **RESEARCHED / READY FOR TEST (Category B):** Suspected architectural gap is documented and a general-purpose fix is implemented in KeplerBridge. Ready for physical commercial-game benchmarking. (16 titles).
* **EXPERIMENTAL / UNTESTED (Category C):** Plausible on paper, but requires preliminary physical testing or configuration tuning. (10 titles).
* **BLOCKED (Category D):** Constrained by physical hardware boundaries, kernel anti-cheat, or Shader Model 6.x features. (4 titles).

### 30-Game Targeted Compatibility Status Table

| # | Game Title | Engine | Rendering Backend | Applicable KeplerBridge Fix | Evidence-Backed Status |
| :-: | :--- | :--- | :--- | :--- | :---: |
| 1 | **Stray** | UE 4.27 | D3D12 (Native `-dx11` works) | Priority 1 COM hierarchy continuity | **RESEARCHED / READY FOR TEST** |
| 2 | **FFVII Remake Intergrade** | UE 4.18 | D3D12 (Native `-dx11` works) | Priority 2 Initial PSO auto-recording | **RESEARCHED / READY FOR TEST** |
| 3 | **Ghostwire: Tokyo** | UE 4.26 | D3D12 (Native `-dx11` works) | Priority 3 Swapchain fallback & tearing sanitization | **RESEARCHED / READY FOR TEST** |
| 4 | **Lies of P** | UE 4.27 | D3D12 Only | Feature level spoofing & authentic Tier 1 options | **RESEARCHED / READY FOR TEST** |
| 5 | **Hogwarts Legacy** | UE 4.27 | D3D12 Primary | Priority 6 PSO burst throttling & queue yielding | **RESEARCHED / READY FOR TEST** |
| 6 | **Like a Dragon: IW** | Dragon | D3D12 Only | Priority 5 GPU completion synchronization query | **RESEARCHED / READY FOR TEST** |
| 7 | **Resident Evil Village** | RE Engine | D3D12 Primary | Phase 7 Multi-slot UAV binding & counter semantics | **RESEARCHED / READY FOR TEST** |
| 8 | **Devil May Cry 5** | RE Engine | D3D12 (Native D3D11 works) | Priority 2 Initial PSO preservation | **RESEARCHED / READY FOR TEST** |
| 9 | **Resident Evil 2 (Next)** | RE Engine | D3D12 (Steam beta DX11 works) | Priority 1 (COM) + Priority 3 (Tearing) | **RESEARCHED / READY FOR TEST** |
| 10 | **Resident Evil 3 (Next)** | RE Engine | D3D12 (Steam beta DX11 works) | Priority 3 Swapchain resize lifecycle | **RESEARCHED / READY FOR TEST** |
| 11 | **Deathloop** | Void | D3D12 Only | Priority 4 Register-space stride remapping | **RESEARCHED / READY FOR TEST** |
| 12 | **Atomic Heart** | UE 4.27 | D3D12 Only | Priority 6 (Throttling) + Priority 3 (Resize) | **RESEARCHED / READY FOR TEST** |
| 13 | **Hi-Fi RUSH** | UE 4.27 | D3D12 Default | Priority 2 Initial PSO preservation | **RESEARCHED / READY FOR TEST** |
| 14 | **Little Nightmares II** | UE 4.24 | D3D12 (Native D3D11 works) | Phase 6.1 Descriptor table mapping | **RESEARCHED / READY FOR TEST** |
| 15 | **Age of Empires IV** | Essence 5 | D3D12 Only | Dynamic descriptor heap registration | **RESEARCHED / READY FOR TEST** |
| 16 | **Humankind** | Unity 2020 | D3D12 (Native D3D11 works) | Phase 7 Root constant shadow buffer suppression | **RESEARCHED / READY FOR TEST** |
| 17 | **Forza Horizon 4** | ForzaTech | D3D12 Only | Feature level spoofing (`EnableFeatureLevelSpoof=1`)| **EXPERIMENTAL / UNTESTED** |
| 18 | **Forza Motorsport 7** | ForzaTech | D3D12 Only | Swapchain discard presentation wrapper | **EXPERIMENTAL / UNTESTED** |
| 19 | **The Division 2** | Snowdrop | D3D12 (Native D3D11 works) | Priority 5 Queue draining & synchronization | **EXPERIMENTAL / UNTESTED** |
| 20 | **Gears 5** | UE 4 | D3D12 Only | Command translator bundle flattening | **EXPERIMENTAL / UNTESTED** |
| 21 | **Watch Dogs: Legion** | Disrupt | D3D12 (Native D3D11 works) | `Present1()` parameter passing delegator | **EXPERIMENTAL / UNTESTED** |
| 22 | **Ghostrunner** | UE 4.25 | D3D12 (Native D3D11 works) | Shadow buffer staging & map suppression | **EXPERIMENTAL / UNTESTED** |
| 23 | **Return to Monkey Island**| Dinky | D3D12 Primary | Staging buffer pool & format conversion | **EXPERIMENTAL / UNTESTED** |
| 24 | **Tekken 8** | UE 5.2 | D3D12 Only | Priority 1 COM hierarchy continuity | **EXPERIMENTAL / UNTESTED** |
| 25 | **Ready or Not** | UE 5.3 | D3D12 (Native D3D11 works) | Device creation interception | **EXPERIMENTAL / UNTESTED** |
| 26 | **Gotham Knights** | UE 4.26 | D3D12 Only | Priority 4 hardware slot bounds protection | **EXPERIMENTAL / UNTESTED** |
| 27 | **Remnant II** | UE 5.2 | D3D12 Only | Nanite virtualized geometry (64-bit atomics)| **BLOCKED (Deferred to V2)** |
| 28 | **Dead by Daylight** | UE 4.27 | D3D12 Primary | EAC kernel driver blocks unsigned proxy DLLs | **BLOCKED (Security / Policy)** |
| 29 | **Ghostrunner 2** | UE 5.2 | D3D12 (Native `-dx11` works) | SM 6.0 DXIL compute shaders in D3D12RHI | **BLOCKED on D3D12** |
| 30 | **Palworld** | UE 5.1 | D3D12 (Native `-dx11` works) | SM 6.0 DXIL compute & Wave Intrinsics | **BLOCKED on D3D12 / EXPERIMENTAL** |

---

## 11. Palworld Status & Research Road

* **Current Status:** **EXPERIMENTAL / BLOCKED ON D3D12 PATH**
* **Local Test Bench Audit:** As verified during Phase 10, active game binaries are not present on local test storage. Historical runtime artifacts in `C:\Users\win10\AppData\Local\Pal\Saved` confirm previous engine execution and crash events.
* **Identified D3D12 Blocker:**
  1. *General DXIL Incompatibility:* Unreal Engine 5.1's `D3D12RHI` compiles all compute shaders via `dxc.exe` into Shader Model 6.0 DXIL bitcode. The physical Kepler GT 740 driver (456.71) natively accepts only legacy DXBC bytecode. Any DXIL container triggers `0x80004005` (`E_FAIL`) at `CreateComputePipelineState`.
  2. *Wave Intrinsics:* Niagara particle simulation kernels utilize `WaveActiveSum`, `WaveReadLaneFirst`, and `WaveGetLaneIndex`. GK208 silicon possesses zero hardware warp instructions.
* **Player Recommendation:** Palworld includes an authentic native Direct3D 11 backend. Passing `-dx11` in launch options runs smoothly and stably on physical GT 740 hardware without translation overhead.
* **Engineering Roadmap:** Progression follows the dedicated 6-gate plan ([docs/PHASE10_PALWORLD_IMPLEMENTATION_PLAN.md](docs/PHASE10_PALWORLD_IMPLEMENTATION_PLAN.md)). No claim of playability will be made without live physical game validation.

---

## 12. Security & Anti-Cheat Advisory

* **Strict Offline Single-Player Policy:** KeplerBridge replaces `d3d12.dll` in the local game directory. Modern online multiplayer games employ kernel-level anti-cheat systems (such as Easy Anti-Cheat, BattlEye, Vanguard, or Blizzard Warden) that enforce cryptographic signature verification.
* **Risk of Ban:** Loading KeplerBridge into an online anti-cheat-protected game will trigger security violations, process termination, or permanent account bans.
* **Non-Circumvention:** KeplerBridge contains zero anti-detection, rootkit, or bypass mechanisms. Do not use KeplerBridge for multiplayer games. See [SECURITY.md](SECURITY.md).

---

## 13. Troubleshooting & Bug Report Guide

### Common Issues & Remedies
1. **Game Crashes at Startup with `0x80004005` (`E_FAIL`):**
   * *Cause:* Game submitted pre-compiled Shader Model 6.x DXIL bytecode that the native Kepler driver cannot execute.
   * *Remedy:* Check if the game supports a Direct3D 11 mode via launch arguments (e.g. `-dx11`, `-d3d11`, or `-force-d3d11`).
2. **Error `0x887A0001` (`DXGI_ERROR_INVALID_CALL`) on Swapchain Creation:**
   * *Cause:* Game requested flip discard with tearing flags on older D3D11 runtimes.
   * *Remedy:* Verify KeplerBridge v0.8.0-rc1 is active; automatic swapchain fallback will sanitize tearing flags.
3. **Driver Hang or Device Removed (`0x887A0005`):**
   * *Cause:* WDDM TDR timeout caused by excessive shader compilation or memory bandwidth saturation.
   * *Remedy:* Ensure PSO burst throttling is enabled (`ImmediateQueueFlush=1`) and lower in-game texture resolution.

### Submitting a Bug Report
When submitting an issue on GitHub, include:
* GPU model, driver version, and Windows build.
* Target game name, version, and launch arguments.
* Complete `kepler_bridge.log` output.
* Hardware test result output (`test_d3d12_translation.exe`).

---

## 14. License & Release History

KeplerBridge is distributed under the [MIT License](LICENSE).

### Version History
* **v0.8.0-rc1 (Current):** Phase 9 and Phase 10 validation. Added COM child hierarchy preservation, initial PSO auto-recording, swapchain tearing sanitization, multi-space register mapping, GPU completion fence sync, and PSO burst throttling (111/111 hardware tests passing).
* **v0.7.0:** Multi-slot UAV binding (8 slots), atomic counter handling, persistent shader disk cache (FNV-1a), and diagnostic reporting.
* **v0.6.1:** Compute shader dispatch, CPU staging readback, and numerical validation.
* **v0.5.0:** COM lifecycle safety, process detach protection, and DXGI swapchain pacing.
* **v0.4.0:** In-memory command translation engine and D3D11 immediate context replay.
* **v0.1.0 – v0.3.0:** Ghost hook proxy architecture, feature level spoofing, and DXBC container parser.
