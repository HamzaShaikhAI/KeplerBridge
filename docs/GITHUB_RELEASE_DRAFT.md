# KeplerBridge v0.8.0-rc1 — Release Candidate 1

> **Pre-Release Candidate:** This release candidate is published for hardware verification, community testing, and compatibility research on NVIDIA Kepler GPUs. It is not yet certified as a general stable release. Commercial games require physical acceptance testing before being marked playable.

---

## What is KeplerBridge?

**KeplerBridge** is an open-source Direct3D 12-to-Direct3D 11 compatibility and translation proxy engineered to enable bounded Direct3D 12 applications on legacy hardware, specifically the **NVIDIA GeForce GT 740** (Kepler GK208 architecture, native Direct3D 11 Feature Level 11_0).

Deploying as a drop-in 64-bit `d3d12.dll` alongside game executables, KeplerBridge translates D3D12 device creation, command recording, resource transitions, multi-slot compute dispatches, and swapchain presentations into Direct3D 11 immediate execution.

---

## Key Capabilities Included in v0.8.0-rc1

* **Zero External Dependencies:** Statically linked against C++20; imports solely standard Windows operating system DLLs (`d3d11.dll`, `dxgi.dll`, `KERNEL32.dll`, `d3dcompiler_47.dll`, and Windows UCRT).
* **COM Child-Object Hierarchy Preservation:** Correctly propagates wrapped device interfaces on `GetDevice()` across command allocators, command lists, and command queues while cleanly rejecting higher execution interfaces (`ID3D12GraphicsCommandList1/2` return `E_NOINTERFACE`).
* **Initial Pipeline State Preservation:** Command list `Reset(alloc, pInitialState)` automatically records and replays the initial PSO, ensuring draw/dispatch sequences execute correctly even without explicit subsequent `SetPipelineState()` calls.
* **DXGI Swapchain Fallback & Tearing Sanitization:** Transparent fallback from `DXGI_SWAP_EFFECT_FLIP_DISCARD` to `DXGI_SWAP_EFFECT_DISCARD` on D3D11.0 runtimes with automated sanitization of tearing flags (`0x200`).
* **Root Signature Version 1.1 Multi-Space Remapping:** Linear stride offset remapping for multi-space descriptor tables (`spaceStride = 16` for SRVs, `4` for CBVs/Samplers/UAVs) with 1:1 preservation of standard `space0` bindings and explicit hardware slot limit checks.
* **GPU Queue Completion Synchronization:** Authentic GPU queue draining via `D3D11Backend::WaitForGpuCompletion()` utilizing `ID3D11Query` of type `D3D11_QUERY_EVENT` polled via `GetData()`, preventing CPU worker thread races and deadlocks.
* **Monolithic PSO Compilation Throttling:** Periodic 128-PSO checkpoints flush driver command queues and yield thread execution slices (`SwitchToThread()`), preventing WDDM watchdog TDR timeouts (`0x887A0005`) during intense shader pre-warming phases.
* **Multi-Slot UAV Binding & Counters:** Up to 8 concurrent UAV slots in compute and graphics stages with full atomic append/consume counter reset and preservation semantics.
* **Persistent Shader Disk Caching:** Non-cryptographic 64-bit FNV-1a caching with warm compiler bypass (reducing compilation latency from 47.8 ms to 0.13 ms, 365x speedup) and automated disk corruption recovery.
* **Production Diagnostics:** Structured error codes (`KB_ERR_*`), user profile path redaction (`Users\[USER]\`), and 10 MB active log rotation.

---

## Hardware Regression Test Results

KeplerBridge v0.8.0-rc1 has been verified against the physical **NVIDIA GeForce GT 740** GPU (GK208B, 4052 MB DDR3, Driver 456.71, Windows 10 x64):

* **Total Test Suites:** 15 executables
* **Total Automated Tests:** 111 / 111 Passed (100% Pass Rate)
* **Regressions:** 0 detected

---

## Quick Start & Installation

1. Download the release package: `keplerbridge-v0.8.0-rc1-x64.zip`.
2. Extract `d3d12.dll` and `kepler_bridge.ini` into the directory containing your target 64-bit game executable (next to the main `.exe`).
3. Verify that the game is launched in **offline single-player mode**.
4. Launch the game.
5. Check `kepler_bridge.log` in the game directory to inspect startup capability detection and translation telemetry.

### Uninstallation
Delete `d3d12.dll`, `kepler_bridge.ini`, `kepler_bridge.log`, and the `kepler_cache/` directory from the game folder. The game immediately reverts to the system's native Direct3D 12 runtime.

---

## Commercial Game Compatibility Caveats

* **Strict Non-Fabrication Standard:** No commercial game is marked as VERIFIED PLAYABLE without confirmed in-game physical testing passing all 5 Acceptance Pillars.
* **16 Promising Candidates (Category B):** Titles with implemented targeted fixes ready for community and maintainer testing: *Stray*, *FFVII Remake Intergrade*, *Ghostwire: Tokyo*, *Lies of P*, *Hogwarts Legacy*, *Like a Dragon: Infinite Wealth*, *Resident Evil Village*, *Devil May Cry 5*, *Resident Evil 2/3 Remake*, *Deathloop*, *Atomic Heart*, *Hi-Fi RUSH*, *Little Nightmares II*, *Age of Empires IV*, *Humankind*.
* **Palworld Status (Experimental):** Palworld is **not verified playable under D3D12**. The D3D12 path fails due to Shader Model 6.0 DXIL bytecode and Niagara wave intrinsics. Players on Kepler GT 740 should launch Palworld with the native `-dx11` command-line switch, which runs smoothly on physical hardware without translation overhead.
* **Native Baselines Segregated:** Physical GT 740 runs of *God of War (2018)*, *Baldur's Gate 3*, *Days Gone*, and *Palworld -dx11* are strictly classified as native D3D11 hardware baselines, not translation successes.

---

## Known Limitations & Warning

KeplerBridge does not synthesize missing GPU hardware. The following features cannot be supported on Kepler Feature Level 11_0 silicon and will fail honestly:
* **DirectX Raytracing (DXR Tier 1.0/1.1)**
* **Mesh & Amplification Shaders (SM 6.5)**
* **Variable Rate Shading (VRS Tier 1/2)**
* **Resource Binding Tier 3 (Full Bindless)**
* **DirectStorage GDeflate GPU Decompression**
* **Arbitrary Pre-Compiled SM 6.x DXIL Bytecode (without HLSL source)**

---

## Anti-Cheat Warning

KeplerBridge is designed exclusively for offline single-player applications, legacy hardware research, and game preservation. **Do NOT use KeplerBridge in online multiplayer games protected by kernel anti-cheat systems** (Easy Anti-Cheat, BattlEye, Vanguard, Blizzard Warden). Loading proxy DLLs into protected games will trigger anti-cheat bans.

---

## Release Package Checksums

```
042565a1e018a0444b04a9a7b85376ebb942d2646f47b18d5a8dea38157a5b6a  d3d12.dll
1162c535c35cc04d7284621bab7d52aa620ba9984545423f235a9e900e3fc054  kepler_bridge.ini
2bb0f6c997569474e8d16e264f7a133218a683e68f1c62326204985aa89a93db  LICENSE
e7c99e0000cfaef8a515235953b92182b5b64760c44a50670b6ce2e29dc14ed9  README.txt
383be1c12d945d0c01368174ac63947f153080a12e8e261dc152acd181e23007  keplerbridge-v0.8.0-rc1-x64.zip
```
