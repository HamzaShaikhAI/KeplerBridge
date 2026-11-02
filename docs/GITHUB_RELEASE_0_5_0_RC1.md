# KeplerBridge 0.5.0-rc1 — First Public Release Candidate

**Release Tag:** `v0.5.0-rc1`  
**Target:** `master`  
**Release Type:** `Pre-release`  

---

## Overview

**KeplerBridge 0.5.0-rc1** is the first public release candidate of KeplerBridge, an open-source Direct3D 12-to-Direct3D 11 compatibility and translation layer.

KeplerBridge is designed to allow bounded Direct3D 12 workloads to execute on legacy graphics hardware, specifically NVIDIA Kepler architecture GPUs (**GeForce GT 740**, GTX 650, GTX 750, GK107, GK104, GK208) and native Direct3D Feature Level 11_0 hardware.

> [!WARNING]
> **Pre-release Notice:** This is an engineering Release Candidate intended for hardware preservation, graphics translation research, and running bounded Direct3D 12 applications. It does **not** upgrade older GPUs to physical Feature Level 12_0, does **not** provide a universal DirectX 12 driver, and does **not** guarantee compatibility with commercial AAA games.

---

## Key Architectural Highlights

KeplerBridge deploys as a standalone 64-bit application proxy (`d3d12.dll`) without requiring driver modifications or system file changes:

- **DXIL Bitcode Decoding & SSA Lowering:** Embedded, dependency-free LLVM 3.7 bitstream decoder parses DXIL 1.0–1.2 bitstreams, performing Mem2Reg SSA promotion (`alloca`/`load`/`store`), acyclic branch/PHI resolution, and conditional critical-edge splitting.
- **Shader Model 5.0 DXBC Generation:** Translates DXIL operations into native SM 5.0 bytecode with valid DirectX container chunks and Murmur-based bytecode checksums, bypassing runtime compiler invocations.
- **Bounded Natural Loop Lowering:** Analyzes counted induction variables and reconstructs bounded loop back-edges into SM 5.0 structured loops (`loop`/`endloop`).
- **Persistent Shader Disk Caching:** Deterministic composite hashing (`SHA256`) caches lowered DXBC shaders on disk with LRU quota management, reducing warm shader preparation time from ~1.12 ms to ~0.03 ms (>35x speedup).
- **Direct3D 11 Immediate Context Backend:** Serializes draw and dispatch commands onto the single D3D11 immediate context (`ID3D11DeviceContext`), synchronizing GPU completion via native `D3D11_QUERY_EVENT` queries.
- **Model A Feature-Level Contract:** Reports authentic physical Feature Level 11_0 to driver queries while virtualizing SM 5.1/6.0 translation capabilities.
- **Causal Runtime Telemetry:** Intercepts DXGI swap chains, falling back from `FLIP_DISCARD` to `DISCARD` with transparent tearing flag sanitization and detailed presentation/occlusion counters.

---

## Tested Hardware Baseline

All capabilities and test suites in this release are validated on physical hardware:

- **GPU:** NVIDIA GeForce GT 740 (GK107 Silicon, 384 CUDA cores, 128-bit DDR3, 21.3 GB/s)
- **PCI Identifiers:** Vendor ID `0x10DE` | Device ID `0x0FC8`
- **Native Feature Level:** `D3D_FEATURE_LEVEL_11_0` (`0xB000`)
- **Display Driver:** NVIDIA Kepler Legacy Driver 474.11 / 456.71 WHQL
- **Host System:** Windows 11 Version 24H2 (OS Build 26100) / Windows 10 x64 | Intel Core i5-2310 | 16 GB DDR3 RAM
- **Physical Verification:** Interactive visible desktop presentation verified on physical monitor with 100% exact mathematical pixel match at sampled coordinates between GPU backbuffer readbacks and independent desktop GDI captures.

---

## Validated Capabilities & Subsystems

### Stable Subsystems
- **DXIL Bitcode Decoding & SSA Lowering** (DXIL 1.0–1.2, straight-line Mem2Reg)
- **Branch & PHI Lowering** (Acyclic diamonds, multi-branch blocks, synthetic critical-edge splits)
- **Bounded Natural Loop Lowering** (Counted induction variables, safe trip-count evaluation)
- **64-Bit Integer ALU Lowering** (32-bit register pairs for addition with carry, subtraction with borrow, bitwise ops)
- **Persistent Shader Disk Cache** (Atomic write-rename, LRU quota enforcement)
- **Model A Feature-Level Contract** (Honest FL 11_0 hardware reporting with virtualized SM 5.1/6.0 translation)
- **Diagnostic Shader Autopsy** (Structured JSONL diagnostic records)
- **Memory Guardian** (D3D11 allocation tracking and soft memory budgets)

### Experimental Subsystems
- **Bounded Descriptor Virtualization:** Static contiguous descriptor range slot remapping up to 16 slots.
- **Asynchronous Compute Queue Serialization:** Compute queues serialized over the single D3D11 immediate context.
- **AMD FSR 1.0 Spatial Upscaler:** Compute Shader 5.0 implementation of EASU + RCAS spatial upscaling (SDR UNORM/FP16).
- **Compute Wave Emulation:** Compute shader LDS parallel reduction for `WaveActiveSum`, `WaveGetLaneIndex`, and `WaveReadLaneFirst`.

---

## Known Limitations & Unsupported Scenarios

KeplerBridge translates compatible operations into Direct3D 11; it cannot fabricate missing physical silicon hardware features:

1. **Driver-Level Feature Level 12_0 Checks:** Kepler hardware physically supports Feature Level 11_0 maximum. Applications requiring strict driver-level FL 12_0 (such as those using modern Agility SDK configurations) will safely exit or fall back to their native D3D11 render path.
2. **Unbounded Dynamic Descriptor Heaps:** Tier 2 and Tier 3 full bindless indexing (`ResourceDescriptorHeap[i]` or `SamplerDescriptorHeap[i]`) is unsupported and rejected.
3. **64-Bit Shader Atomics:** Operations such as `InterlockedAdd64` are physically infeasible without hardware 64-bit atomic instructions and are strictly rejected.
4. **DirectX Raytracing (DXR 1.0 / 1.1):** Kepler GPUs lack BVH acceleration and ray-intersection hardware; DXR calls return unsupported.
5. **Mesh & Amplification Shaders (SM 6.5+):** General mesh and task shader pipelines are unsupported and rejected.
6. **Graphics Stage Wave Operations:** Wave intrinsics (`SV_Wave*`) are strictly unsupported in graphics stages (VS, PS, GS, DS, HS).
7. **Application Compatibility Status:**
   - **Palworld (Unreal Engine 5.1 / Agility SDK 602):** **Unsupported.** Strictly requires Feature Level 12_0 and Tier 3 bindless resource heaps. (Users should launch Palworld with `-dx11` to utilize its native UE D3D11 backend).
   - **3DMark Night Raid:** **Not Tested.** While its technical baseline (FL 11_0, SM 5.1, Tier 1 binding) matches supported boundaries, the benchmark executable has not been validated under translation.

---

## Installation & Removal

KeplerBridge operates as a per-application local proxy. It does **not** replace or modify Windows system files in `C:\Windows\System32`.

### Installation Steps
1. Download `KeplerBridge-0.5.0-rc1-Windows-x64.zip` from Assets below.
2. Extract `d3d12.dll` and `kepler_bridge.ini` into the directory containing the target application's main 64-bit executable.
3. (Optional) Open `kepler_bridge.ini` in a text editor to adjust shader cache directory, logging verbosity, or performance profiles.
4. Launch the application normally.

### Uninstallation Steps
Delete `d3d12.dll` and `kepler_bridge.ini` from the application executable directory. The application will immediately revert to standard system DirectX runtime behavior.

---

## Bug Reporting & Feedback

If you encounter an issue or unsupported shader instruction:
1. Ensure the application is running in offline mode.
2. Check `kepler_bridge.log` in the application directory for diagnostic error codes (`KB_ERR_*`).
3. If a crash occurs, attach the generated `.shader_autopsy.jsonl` log file to a GitHub issue.
4. Review [`TROUBLESHOOTING.md`](TROUBLESHOOTING.md) and [`SUPPORT_MATRIX.md`](SUPPORT_MATRIX.md) before submitting a bug report.

---

## Release Assets & Checksums

| Asset Name | File Size | SHA-256 Checksum |
| :--- | :--- | :--- |
| **`KeplerBridge-0.5.0-rc1-Windows-x64.zip`** | 1,344,550 bytes | `1D529CD8D3831D66E5B7487CC34E97D31C92D896BEEE5365C6F94B54A2AD866C` |
| **`d3d12.dll` (Candidate DLL)** | 4,799,805 bytes | `0926BABEA595A5A2E128B02CB55A130645D87910437FA47AA8AE2BFF61F8BD0C` |
| **`CHECKSUMS.sha256`** | 823 bytes | `16E407AF5366530FCCA4B85A8565EBC6C102B1803B110A6EC31C5D0184231A85` |

*Sealed Release Baseline: `042565A1E018A0444B04A9A7B85376EBB942D2646F47B18D5A8DEA38157A5B6A` (Preserved)*
