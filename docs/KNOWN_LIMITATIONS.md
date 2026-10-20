# KeplerBridge — Known Limitations & Architectural Boundaries

**Milestone:** Phase 10 / V1 Release Engineering  
**Document Version:** 1.0.0  
**Hardware Reference:** NVIDIA GeForce GT 740 (GK208B, 4052 MB DDR3, Native FL 11_0, Driver 456.71, Windows 10 x64)  
**Engineering Principle:** Maintain complete transparency regarding physical hardware constraints and architectural boundaries. Never promise features that legacy silicon cannot execute.

---

## 1. Physical Silicon Hard Limitations (Kepler GK208)

The NVIDIA GeForce GT 740 (GK208B architecture, manufactured on 28nm process technology) was designed to support Direct3D 11.0 Feature Level 11_0 (`0xb000`) and Shader Model 5.0 (`cs_5_0`).

The following features **cannot be synthesized or emulated in software** without triggering severe, unplayable latency or driver crashes:

### 1.1 DirectX Raytracing (DXR Tier 1.0 / 1.1)
* **Silicon Boundary:** Kepler architecture GPUs possess zero dedicated ray-tracing hardware cores (RT Cores) and lack hardware-accelerated Bounding Volume Hierarchy (BVH) traversal pipelines.
* **Impact:** Games requiring mandatory hardware ray tracing (such as *Metro Exodus Enhanced Edition*, *Avatar: Frontiers of Pandora*, and *Star Wars Outlaws*) cannot run under KeplerBridge.
* **Behavior:** KeplerBridge reports `D3D12_RAYTRACING_TIER_NOT_SUPPORTED` during feature support queries. Attempts to create ray-tracing state objects fail with `E_NOTIMPL`.

### 1.2 Mesh & Amplification Shaders (Shader Model 6.5)
* **Silicon Boundary:** The geometry pipeline on Kepler silicon consists of fixed-function primitive assemblers, vertex attribute fetchers, and rasterizers. It lacks programmable task/meshlet processing engines.
* **Impact:** Modern titles requiring mandatory mesh shaders (such as *Alan Wake 2* and *Indiana Jones and the Great Circle*) cannot render geometry on Kepler hardware.
* **Behavior:** KeplerBridge reports `D3D12_MESH_SHADER_TIER_NOT_SUPPORTED`.

### 1.3 Variable Rate Shading (VRS Tier 1 / Tier 2)
* **Silicon Boundary:** Pixel shading rate on Kepler is strictly coupled 1:1 per rasterized pixel sample. Coarse pixel shading (2x2, 4x4) is physically unsupported by the hardware rasterizer.
* **Impact:** Games requiring VRS (such as *Dead Space Remake*) cannot utilize coarse shading rates.
* **Behavior:** KeplerBridge reports `D3D12_VARIABLE_RATE_SHADING_TIER_NOT_SUPPORTED`.

### 1.4 DirectStorage 1.2 GDeflate GPU Decompression
* **Silicon Boundary:** DirectStorage GPU decompression relies on specialized compute kernels executing bitstream decompression at high memory bandwidth.
* **Impact:** Titles mandating DirectStorage GDeflate (such as *Ratchet & Clank: Rift Apart*) will fail during asset streaming.
* **Behavior:** Storage decompression queries report unsupported; applications must fall back to CPU decompression.

### 1.5 Resource Binding Tier 3 (Full Bindless Heaps)
* **Silicon Boundary:** Kepler hardware enforces physical descriptor slot limits:
  * Maximum 128 Shader Resource Views (SRVs) per stage (`t0`–`t127`).
  * Maximum 16 Samplers per stage (`s0`–`s15`).
  * Maximum 14 Constant Buffer Views (CBVs) per stage (`b0`–`b13`).
  * Maximum 8 Unordered Access Views (UAVs) in compute/graphics (`u0`–`u7`).
* **Impact:** Engines that dynamically index unbounded descriptor arrays without root signature tables (such as *Cyberpunk 2077*) will exceed D3D11 hardware limits.
* **Behavior:** KeplerBridge explicitly reports `D3D12_RESOURCE_BINDING_TIER_1` in `CheckFeatureSupport()`.

### 1.6 64-Bit Atomics in Compute Shaders
* **Silicon Boundary:** Direct3D 11.0 Feature Level 11_0 does not support 64-bit integer atomics (`InterlockedCompareExchange64`) in compute shaders.
* **Impact:** Unreal Engine 5 Nanite virtualized geometry pipelines utilize 64-bit atomics for cluster visibility and depth culling. Titles requiring full Nanite (such as *Remnant II*, *Lords of the Fallen*, and *S.T.A.L.K.E.R. 2*) cannot run under D3D11 translation.
* **Behavior:** Shaders requesting 64-bit atomics fail compilation.

---

## 2. Memory Bus & Bandwidth Bottlenecks (14.4 GB/s DDR3)

The reference NVIDIA GeForce GT 740 configuration is equipped with 4052 MB of DDR3 memory on a 64-bit memory bus, providing approximately **14.4 GB/s of total theoretical memory bandwidth**.

* **Fillrate Saturation:** Modern deferred renderers that write to multiple high-precision render targets (G-buffer: Albedo, Normal, Roughness, Depth, Motion Vectors) at 1080p will saturate the DDR3 bus within single-digit milliseconds.
* **Practical Resolution Floor:** High-fidelity commercial games translated through KeplerBridge should be configured to run at **720p resolution (1280x720)** with low graphics presets and upscaling (AMD FSR Performance / Balanced) to avoid fillrate thrashing.
* **VRAM Overhead:** Although the card has 4 GB of addressable memory, reserving ~500 MB for Windows DWM and ~500 MB for driver staging buffers leaves an effective budget of **~3.0 GB VRAM** for game assets.

---

## 3. Shader Model & Bytecode Limitations

### 3.1 Bounded Compiler Scope (`d3dcompiler_47.dll`)
* **Core Limitation:** KeplerBridge relies on Microsoft's canonical `d3dcompiler_47.dll` for authentic bytecode compilation. `D3DCompile()` accepts **HLSL source text** and outputs legacy Shader Model 5.0 DXBC bytecode.
* **No General DXIL Decompilation:** `d3dcompiler_47.dll` is **not** an arbitrary DXIL-to-DXBC decompiler. It cannot decompile pre-compiled Shader Model 6.x DXIL bitcode into DXBC.
* **Driver Rejection:** The NVIDIA Kepler driver 456.71 accepts only legacy DXBC bytecode containers. If an application submits pre-compiled DXIL bitcode without HLSL source, the driver rejects it with `0x80004005` (`E_FAIL`).

### 3.2 Wave Intrinsics (`WaveActiveSum`, `WaveReadLaneFirst`)
* **Silicon Boundary:** Direct3D 11.0 Shader Model 5.0 does not support SIMD warp/wave instructions. Threads can only communicate via threadgroup shared memory (`groupshared`) with barrier synchronization (`GroupMemoryBarrierWithGroupSync()`).
* **Software Emulation Complexity:** Translating wave operations into threadgroup reductions requires parsing DXIL bitcode at runtime, which is beyond the bounded V1 architecture.
* **Affected Titles:** *Palworld* (Niagara compute shaders) and other UE 5.1+ titles.

---

## 4. Security & Anti-Cheat Boundaries

* **Offline Single-Player Only:** KeplerBridge is an unsigned local proxy DLL (`d3d12.dll`).
* **Kernel Anti-Cheat Enforcement:** Modern multiplayer games employ kernel-level anti-cheat drivers (Easy Anti-Cheat, BattlEye, Riot Vanguard, Blizzard Warden, Valve Anti-Cheat) that verify DLL digital signatures and monitor memory pages.
* **Zero Circumvention:** KeplerBridge incorporates zero anti-cheat evasion, driver hooks, or memory cloaking. Loading KeplerBridge into protected multiplayer games will result in immediate game launch failure or permanent account bans.

---

## 5. Operational Error Codes & Diagnostic Telemetry

When an unsupported operation or boundary condition is encountered, KeplerBridge logs a structured diagnostic report to `kepler_bridge.log`:

| Error Code | Identifier | Root Cause | Actionable User Guidance |
| :--- | :--- | :--- | :--- |
| `1001` | `KB_ERR_DEVICE_REMOVED` | GPU driver watchdog triggered TDR (`0x887A0005`). | Lower texture resolution; verify PSO burst throttling is enabled. |
| `1002` | `KB_ERR_INIT_FAILED` | D3D11 device creation failed. | Ensure DirectX 11 runtime and Driver 456.71 are installed. |
| `1003` | `KB_ERR_DEVICE_TIMEOUT` | GPU event query timed out after 2,000 ms. | Check for long-running compute shaders or infinite dispatch loops. |
| `2001` | `KB_ERR_INVALID_DESCRIPTOR` | Descriptor index exceeded hardware limits. | Application attempted to bind slots beyond D3D11 maximums. |
| `3001` | `KB_ERR_UNSUPPORTED_INSTRUCTION`| SM 6.x DXIL or wave intrinsics encountered. | Use native `-dx11` launch option if supported by the game engine. |
| `3002` | `KB_ERR_UNSUPPORTED_STAGE` | Geometry, Hull, or Domain shader stage requested. | Geometry/Tessellation shaders require valid SM 5.0 HLSL fallback. |
| `3003` | `KB_ERR_SHADER_COMPILATION` | `D3DCompile` failed to produce valid DXBC. | Inspect compiler error blob in `kepler_bridge.log`. |
| `3004` | `KB_ERR_CACHE_CORRUPTION` | On-disk cache entry payload failed validation. | Cache entry evicted automatically; recompiled on next launch. |
| `5002` | `KB_ERR_SWAPCHAIN_DENIED` | Swapchain creation rejected by DXGI runtime. | Automatic fallback to `DISCARD` engaged. |
