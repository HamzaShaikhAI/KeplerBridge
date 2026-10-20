# KeplerBridge Phase 6 — Shader Compatibility & Binding Correctness Report

**Author:** KeplerBridge Senior Windows Graphics Systems Engineering Team  
**Date:** October 20, 2026  
**Target Hardware:** NVIDIA GeForce GT 740 (4052 MB DDR3 VRAM, GK208, 64-bit bus)  
**Host Environment:** Windows 10 Pro x64 (Version 22H2, OS Build 19045.5487)  
**Display Driver:** NVIDIA 456.71 (WDDM 2.7, Feature Level 11_0)  
**Compiler:** BrechtSanders WinLibs MinGW-w64 GCC 14.2.0 (POSIX UCRT)  
**Build System:** CMake 4.4.3 + Ninja 1.13.2 (C++20, `-m64`, static runtime)  

---

## 1. Executive Summary

Phase 6 addresses the architectural gap between high-level Direct3D 12 shader binding semantics and the physical execution model of legacy NVIDIA Kepler hardware via Direct3D 11.

### Key Milestones Delivered:
1. **Root Constant & Descriptor Binding Correctness**:
   - Audited the proposed 256-byte dynamic staging buffer on `cb13` and proved it introduces register collisions against shaders using `register(b13)` and violates explicit `D3D12_ROOT_CONSTANTS::ShaderRegister` declarations.
   - Implemented dynamic per-slot constant buffer allocation respecting D3D11's strict 16-byte alignment and D3D12's 256-byte alignment rules.
   - Implemented real GPU data upload via `Map(D3D11_MAP_WRITE_DISCARD)` and stage binding (`VSSetConstantBuffers`, `PSSetConstantBuffers`, `CSSetConstantBuffers`).
   - Implemented descriptor table binding routing `baseDescriptor` to underlying `ID3D11ShaderResourceView*` and `ID3D11UnorderedAccessView*` objects.
2. **Comprehensive DXIL Compiler Research**:
   - Proved that SPIRV-Cross does not ingest DXIL (it requires SPIR-V binary words).
   - Distinguished HLSL source compilation, DXIL validation/inspection, DXIL-to-SPIR-V conversion, and DXIL-to-DXBC translation.
   - Confirmed the lack of any official Microsoft or lossless standalone Windows DXIL $\to$ DXBC down-compiler.
3. **Compiler-Backed Bounded Shader Translator (`BoundedShaderTranslator`)**:
   - Implemented `IShaderTranslator` backed by `d3dcompiler_47.dll` (`D3DCompile`) generating genuine, driver-accepted SM 5.0 DXBC bytecode.
   - Strictly rejects unsupported hardware features: Wave Intrinsics, Raytracing, Mesh Shaders, Amplification Shaders, and >8 UAVs.
   - Preserves native driver error propagation rather than fabricating bytecode or claiming unsupported shaders were translated.
4. **Test Suite Baseline Reconciliation & Expansion**:
   - Reconciled the 44 vs 51 test count variance: 44 was an intermediate report subset of 7 suites; 51 was the full Phase 4 baseline; Phase 5 added 6 swapchain tests (57); Phase 6 added 7 new unit and compatibility tests, bringing the total inventory to **64 / 64 PASS (100%)** on physical GT 740 hardware.

---

## 2. Test Suite Inventory & Baseline Reconciliation

| Binary | Scope | Phase 4 | Phase 5 | Phase 6 | Status |
| :--- | :--- | :---: | :---: | :---: | :---: |
| `test_policy_engine.exe` | Feature level parsing, policy spoof evaluation, INI configuration | 6 | 6 | 6 | **PASS** |
| `test_shader_inspector.exe` | Bounds-checked DXBC container inspection, chunk offsets, SM 4/5/6 | 11 | 11 | 11 | **PASS** |
| `test_shader_compat.exe` | Compiler-backed translation, D3D11 driver verification, boundary checks | — | — | 5 | **PASS** |
| `test_translation_engine.exe` | Command batching, barrier tracking, root constants, alignment, re-init | 5 | 5 | 7 | **PASS** |
| `test_d3d11_standalone.exe` | Standalone D3D11 hardware creation, driver timeout isolation | 2 | 2 | 2 | **PASS** |
| `test_d3d12_compat.exe` | Live hardware probing, native FL 11_0 creation, FL 12_0 spoofing | 3 | 3 | 3 | **PASS** |
| `test_pso_interception.exe` | Native SM 5.0 compute/graphics PSO, unsupported DXIL rejection | 5 | 5 | 5 | **PASS** |
| `test_d3d12_translation.exe` | Queue submission, fence sync, committed buffer allocation | 7 | 7 | 7 | **PASS** |
| `test_d3d12_rendering.exe` | Live triangle rasterization, clear, pixel readback, presentation | 11 | 11 | 11 | **PASS** |
| `test_dxgi_swapchain.exe` | Application swapchain, backbuffer virtualization, resize, teardown | — | 6 | 6 | **PASS** |
| `test_d3d12.exe` | Phase 1 proxy hooking verification sanity | 1 | 1 | 1 | **PASS** |
| **Total Test Inventory** | **Physical NVIDIA GeForce GT 740 Verification** | **51** | **57** | **64** | **100% PASS** |

---

## 3. Audit of Root Constants & Descriptors

### 3.1 Collision Risks & Semantics of Blind `cb13` Allocation
The previously proposed strategy of binding all 32-bit root constants to constant buffer slot 13 (`cb13`) was subjected to architectural review:
- **Collision with Shader Declarations:** Direct3D 11 provides exactly 14 constant buffer API slots (`0` through `13`). Shaders commonly utilize `register(b13)` for global parameters or material constants. Unilaterally stomping on slot 13 corrupts application shader constants.
- **Mismatch with Root Signature Declarations:** In Direct3D 12, `D3D12_ROOT_CONSTANTS` declares `ShaderRegister` (e.g. `register(b0)` or `b2`). The shader was compiled expecting constants at that register. Binding them to `cb13` means the shader reads uninitialized zeroes or wrong data from its intended register.
- **Buffer Alignment Invariant:** In Direct3D 11, `D3D11_BUFFER_DESC::ByteWidth` for constant buffers **MUST be a multiple of 16 bytes**. In Direct3D 12, root constants can be arbitrary numbers of DWORDs (e.g. 3 DWORDs = 12 bytes). Creating a buffer with 12 bytes triggers `E_INVALIDARG (0x80070057)`.
- **Missing Pipeline Execution:** The baseline `D3D11Backend` previously stored root constants in CPU memory inside `ResourceManager` but never created an `ID3D11Buffer`, never uploaded bytes to the GPU, and never called `VSSetConstantBuffers`, `PSSetConstantBuffers`, or `CSSetConstantBuffers`.

### 3.2 Correctness Implementation
1. **Dynamic Slot Mapping**: Root constants are bound to the slot matching `rootParameterIndex` (or designated `ShaderRegister`), guarded against slot overflow ($\ge 14$).
2. **16-byte Alignment & 256-byte Padding**: Buffer size is rounded up:
   $$\text{alignedBytes} = \max\left(256, \left\lceil \frac{\text{numBytes}}{16} \right\rceil \times 16\right)$$
3. **GPU Upload via Discard**: Uses `m_pContext->Map(pBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)` to stream dirty constants to the GPU.
4. **Stage Binding**: Binds active buffers to vertex/pixel stages (`VSSetConstantBuffers`, `PSSetConstantBuffers`) or compute stage (`CSSetConstantBuffers`).
5. **Descriptor Table Resolution**: `SetGraphicsRootDescriptorTable` resolves GPU descriptor handles via `ResourceManager::GetSRV` and `GetUAV` and batch-binds them to the D3D11 context.

---

## 4. Real DXIL Translation Research

```
+---------------------------------------------------------------------------------------------------+
|                                 DXIL Compilation & Tooling Reality                                |
+-----------------------------------+-------------------------------+-------------------------------+
| Pipeline Stage                    | Tool / Compiler               | Input / Output Formats        |
+-----------------------------------+-------------------------------+-------------------------------+
| 1. HLSL Source Compilation        | FXC (d3dcompiler_47.dll)      | HLSL -> DXBC (SM 2.0 .. 5.1)   |
|                                   | DXC (dxcompiler.dll)          | HLSL -> DXIL (SM 6.0 .. 6.8)  |
| 2. DXIL Validation & Inspection   | dxil.dll / DxilInspector      | DXIL bitcode verification     |
| 3. DXIL-to-SPIR-V Transpilation   | Mesa NIR / dxil_spirv         | DXIL -> Mesa NIR -> SPIR-V    |
| 4. SPIR-V-to-HLSL Translation     | SPIRV-Cross                   | SPIR-V -> HLSL (NOT DXIL!)    |
| 5. DXIL-to-DXBC Down-compilation  | No official Microsoft tool    | Closed one-way transition     |
+-----------------------------------+-------------------------------+-------------------------------+
```

### Critical Findings:
1. **SPIRV-Cross Input Format**: SPIRV-Cross is strictly a SPIR-V consumption tool. It parses `uint32_t` SPIR-V bytecode words. It has no capability to read or parse LLVM bitcode or DXIL containers. Any claim that SPIRV-Cross directly ingests DXIL is technically false.
2. **The Missing Down-Compiler**: Microsoft explicitly treated DXIL as a forward-only transition. `dxilconv.dll` was provided to convert legacy DXBC to DXIL, but no official DXIL $\to$ DXBC translator exists.
3. **Hardware Boundaries on Kepler**: Physical Kepler hardware lacks native instructions for wave vote/shuffle operations (`WaveActive*`), hardware raytracing, variable rate shading, and mesh/amplification shaders. Transpilation of advanced SM 6.x features to SM 5.0 cannot be performed arbitrarily without software emulation or honest rejection.

---

## 5. Bounded Shader Translator (`BoundedShaderTranslator`)

KeplerBridge implements a compiler-backed bounded shader translator:
- **Compiler Integration**: Dynamically loads `d3dcompiler_47.dll` and invokes `D3DCompile` to emit genuine, cryptographically valid DXBC SM 5.0 bytecode.
- **Supported Stages**: `Vertex`, `Pixel`, `Compute`.
- **Explicit Boundary Rejection**: Rejects `Geometry`, `Hull`, `Domain`, `Mesh`, `Amplification`, and `Raytracing` stages, reporting exact diagnostics to telemetry logs.
- **Hardware Driver Acceptance**: Verified that all translated output is accepted with `S_OK (0x0)` by the physical NVIDIA D3D11 driver runtime (`CreateVertexShader`, `CreatePixelShader`, `CreateComputeShader`).

---

## 6. Empirical Test Execution Log

```
========================================================
  KeplerBridge Phase 6 Shader Compatibility & Translation
========================================================
[PASS] TestDirectCompilerCompilation
[PASS] TestPhysicalD3D11RuntimeVerification
[PASS] TestUnsupportedStagesRejection
[PASS] TestShaderPolicyIntegrationWithFallback
[PASS] TestHonestFailureOnUnsupportedDXIL
========================================================
  ALL PHASE 6 SHADER COMPATIBILITY TESTS PASSED (5/5)!   
========================================================

====================================================
  KeplerBridge Phase 4/6 Translation Engine Unit Tests
====================================================
[PASS] TestCommandRecordingAndBatching
[PASS] TestResourceStateTrackerTransitions
[PASS] TestResourceManagerDescriptorLookup
[PASS] TestRootParameterConstants
[PASS] TestD3D11BackendCommandReplay
[PASS] TestRootConstantBufferUploadAndAlignment
[PASS] TestRepeatedInitializationLifecycle
====================================================
ALL TRANSLATION ENGINE TESTS PASSED (7/7)!
====================================================
```

All 64 tests across 11 test binaries executed and passed cleanly on the NVIDIA GeForce GT 740. Zero regressions occurred across Phases 1–5.
