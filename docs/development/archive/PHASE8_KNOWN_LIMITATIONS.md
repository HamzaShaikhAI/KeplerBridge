# KeplerBridge Phase 8 — Known Limitations & Architectural Boundaries

**Milestone:** Phase 8  
**Document:** Technical Boundaries, Hardware Constraints & Scope Definition  
**Date:** October 20, 2026  
**Hardware Reference:** NVIDIA GeForce GT 740 (GK208B Architecture, Native FL 11_0)  

---

## 1. Introduction

KeplerBridge is an engineered compatibility layer designed to translate a well-defined subset of Direct3D 12 API operations into Direct3D 11 for legacy graphics hardware, specifically NVIDIA Kepler-generation GPUs (such as the GeForce GT 740).

To ensure transparency, prevent unrealistic expectations, and preserve system stability, this document enumerates all known architectural, silicon, driver, and API boundaries.

---

## 2. Silicon and Hardware Boundaries (Kepler GK208)

The NVIDIA GeForce GT 740 (GK208B) is a Kepler-architecture GPU fabricated on a 28nm process. The following hardware limitations are permanent silicon constraints and cannot be circumvented by software emulation:

### 2.1 Native Feature Level 11_0 Ceiling
The GK208 hardware context supports a maximum native Direct3D Feature Level of **11_0 (`0xb000`)**. While KeplerBridge can spoof `D3D_FEATURE_LEVEL_12_0` or `12_1` during `D3D12CreateDevice` to satisfy application startup checks, underlying operations must execute within Feature Level 11_0 capabilities.

### 2.2 Lack of DirectX Raytracing (DXR)
Kepler GPUs possess no hardware Ray Tracing Cores (RT Cores), BVH traversal accelerators, or compute-based DXR emulation paths. Titles requiring hardware raytracing (e.g., *Metro Exodus Enhanced Edition*, *Cyberpunk 2077 Ray Tracing Overdrive*) are strictly incompatible.

### 2.3 Absence of Mesh & Amplification Shaders (SM 6.5)
Kepler hardware utilizes traditional fixed-function Input Assemblers and Primitive Setup hardware. It contains no compute-driven geometry pipelines or meshlet processors. Titles requiring Mesh Shaders (e.g., *Alan Wake 2*) will fail during pipeline creation.

### 2.4 No Variable Rate Shading (VRS)
Kepler GPUs lack coarse-pixel shading or variable rate rasterization hardware (VRS Tier 1 or Tier 2).

### 2.5 No DirectStorage GPU Decompression (GDeflate)
The Kepler NVIDIA display driver (456.71) does not support the compute pipelines required for DirectStorage 1.2+ GDeflate GPU asset decompression. Titles mandating GPU decompression (e.g., *Ratchet & Clank: Rift Apart*) will fail.

### 2.6 Resource Binding Tier 1 Ceiling
Kepler GK208 hardware is physically restricted to **Resource Binding Tier 1**. It cannot support Tier 2 or Tier 3 bindless descriptor tables, where shaders dynamically index unbounded texture or buffer arrays using runtime registers.

### 2.7 DDR3 Memory Bandwidth Bottleneck
The GT 740 utilizes a 64-bit memory bus with DDR3 VRAM, providing a maximum theoretical bandwidth of **~14.4 GB/s** (compared to >200 GB/s on modern GDDR6 architectures). High-bandwidth operations—such as 4K render targets, uncompressed volumetric render passes, or aggressive post-processing pipelines—will saturate the memory bus, leading to severe frame stutter or driver Timeout Detection and Recovery (TDR) crashes.

---

## 3. Translation and Direct3D API Boundaries

### 3.1 Maximum 8 Concurrent Unordered Access Views (UAVs)
In accordance with Direct3D 11.0 hardware specifications, the translation engine supports a maximum of **8 concurrent UAV slots** (`[0..7]`) for compute and graphics stages. Direct3D 12 applications requesting access to slots 8 through 63 will have those descriptors safely rejected or clamped, logging a `KB_ERR_INVALID_DESCRIPTOR` diagnostic warning.

### 3.2 Bounded Shader Translation Model
KeplerBridge operates a bounded shader translation model:
* **Supported:** HLSL source code or pre-compiled legacy DXBC bytecode targeting Shader Model 5.0 (`vs_5_0`, `ps_5_0`, `cs_5_0`), compiled via `d3dcompiler_47.dll`.
* **Unsupported:** Pre-compiled Direct3D 12 DXIL container bytecode targeting Shader Model 6.0+ that requires DXC features (Wave Intrinsics, 64-bit atomics, RayQuery, Mesh Shaders). KeplerBridge does not incorporate an arbitrary DXIL-to-DXBC decompiler and will honestly reject unsupported DXIL shaders (`KB_ERR_UNSUPPORTED_STAGE`).

### 3.3 Dynamic Constant Staging Buffer Collision (`cb13`)
KeplerBridge reserves constant buffer slot **`cb13`** in all active shader stages for dynamic 256-byte root constant staging. If an application's root signature binds a user constant buffer specifically to slot 13, a register collision will occur.

### 3.4 Command List Serialization
Direct3D 12 supports multithreaded command recording across multiple command lists and bundles. When replayed in KeplerBridge, all recorded batches are serialized and submitted to the Direct3D 11 Immediate Device Context. High-volume parallel recording will not see multithreaded CPU scaling.

---

## 4. Software, System, and Security Constraints

### 4.1 Anti-Cheat Incompatibility
Third-party kernel-level anti-cheat engines (including Easy Anti-Cheat, BattlEye, Vanguard, Ricochet, and Blizzard Warden) scan the application directory and process address space. These tools actively detect, reject, or terminate games loading proxy `d3d12.dll` binaries, and may flag user accounts for tampering.
* **Rule:** KeplerBridge must **only** be utilized in offline, disconnected, or single-player environments.

### 4.2 Operating System Requirements
KeplerBridge targets **Windows 10 x64 (Version 19041+)** or **Windows 11 x64**. Windows 7 and 32-bit (x86) operating systems are not supported.

### 4.3 Native D3D11 Fallback Preference
For titles offering both Direct3D 11 and Direct3D 12 executables or launch flags (e.g. *Palworld* with `-dx11`, *The Witcher 3* with `witcher3_dx11.exe`, or *Control* with `Control_DX11.exe`), running the **native Direct3D 11 renderer is always recommended**. Native D3D11 avoids translation overhead and guarantees full visual fidelity.
