# KeplerBridge — Phase 4 Baseline Architecture & System Audit

**Document Date:** October 2026  
**Project:** KeplerBridge (Direct3D 12 to Kepler Compatibility Layer)  
**Target Hardware:** NVIDIA GeForce GT 740 4GB DDR3 (GK208 / GK107, 384 CUDA cores, 64-bit bus, ~14.4 GB/s)  
**Host Environment:** Windows 10 x64 Version 10.0.19045  
**Current Git Commit:** `5226f6f` (*Phase 3 Complete: DXIL Inspection, PSO Interception & Resource Compatibility*)  
**Git Working Tree:** Clean (`master` branch)  

---

## 1. Executive Summary

This baseline audit evaluates the existing KeplerBridge architecture at the inception of **Phase 4: Production Translation Layer**. The project has completed Phase 1 (64-bit DLL Proxy & Telemetry Logger), Phase 2 (Feature Level Compatibility & Policy Engine), and Phase 3 (DXIL Container Inspection, PSO Interception & Texture Clamping).

All existing components have been built and tested on the physical target hardware (NVIDIA GeForce GT 740). All unit and hardware tests execute cleanly with 100% pass rates. The purpose of this audit is to verify baseline integrity, inspect COM interface coverage, identify interface and translation gaps, and establish the technical baseline for Phase 4.

---

## 2. Environment & Toolchain Verification

| Component | Verified Version / Path | Status |
| :--- | :--- | :--- |
| **Operating System** | Windows 10 Pro x64 (Build 19045.5487) | Verified |
| **Target GPU** | NVIDIA GeForce GT 740 (Device ID: 0x1287, GK208) | Verified (4052 MB DDR3, 14.4 GB/s) |
| **GPU Driver** | NVIDIA 456.71 (WDDM 2.7, DirectX 12 Feature Level 11_0) | Verified |
| **C++ Compiler** | BrechtSanders WinLibs MinGW-w64 GCC 14.2.0 (POSIX UCRT) | Verified |
| **Target Architecture** | Strictly x86_64 (`-m64`, CMAKE_SIZEOF_VOID_P == 8) | Verified |
| **C++ Standard** | C++20 (`-std=c++20`, `CMAKE_CXX_STANDARD 20`) | Verified |
| **Build Generator** | Ninja 1.13.2 + CMake 4.4.3 | Verified |
| **Direct3D Headers** | Windows SDK `<d3d12.h>`, `<d3d11.h>`, `<dxgi.h>` | Verified |
| **Runtime Linking** | Static runtime (`-static -static-libgcc -static-libstdc++`) | Verified (Zero external DLL dependencies) |

---

## 3. Baseline Test Suite Empirical Results

The full test suite was built and executed against the physical NVIDIA GeForce GT 740 GPU. Results recorded:

### 3.1 Unit Test: `test_shader_inspector.exe` (Phase 3 Inspector & Policy)
- `TestNullBytecode`: **PASS** (Zero pointer safely rejected).
- `TestTruncatedHeader`: **PASS** (Header < 32 bytes safely rejected).
- `TestNonDXBCMagic`: **PASS** (Non-DXBC container rejected).
- `TestContainerSizeMismatch`: **PASS** (Truncated container detected).
- `TestMalformedChunkOffset`: **PASS** (Out-of-bounds chunk offsets rejected).
- `TestSyntheticLegacyDXBC_VertexShader`: **PASS** (Legacy DXBC SM 5.0 VS parsed correctly).
- `TestSyntheticLegacyDXBC_PixelShader`: **PASS** (Legacy DXBC SM 5.0 PS parsed correctly).
- `TestSyntheticDXIL_VertexShader`: **PASS** (DXIL SM 6.0 VS parsed, container detected).
- `TestSyntheticDXIL_ComputeShader`: **PASS** (DXIL SM 6.0 CS parsed, container detected).
- `TestShaderPolicyEvaluation`: **PASS** (Deterministic fallback policy verified).
- `TestResourceClamping`: **PASS** (Dimension and multisample clamping verified).
- **Result:** **11 / 11 PASSED (100%)**

### 3.2 Unit Test: `test_policy_engine.exe` (Phase 2 Policy Engine)
- `TestParseFeatureLevelString`: **PASS** (Parses "11_0", "11_1", "12_0", "12_1").
- `TestFeatureLevelToShortString`: **PASS** (Converts D3D_FEATURE_LEVEL to standardized string).
- `TestPolicySpoofDisabled`: **PASS** (Preserves native behavior when disabled).
- `TestPolicySpoofEnabledAtOrBelowNative`: **PASS** (Passthrough when request <= native).
- `TestPolicySpoofEnabledAboveNative`: **PASS** (Evaluates intercept when request > native).
- `TestConfigParsing`: **PASS** (Parses `.ini` correctly).
- **Result:** **6 / 6 PASSED (100%)**

### 3.3 Hardware Integration Test: `test_d3d12_compat.exe` (Phase 2 Hardware Check)
- Capability Probing (`ppDevice = nullptr`): **PASS** (Returned `S_FALSE` / `0x1`).
- Native Feature Level 11_0 Device Creation: **PASS** (Device created on GT 740, NodeCount = 1).
- Requested Feature Level 12_0 Creation: **PASS** (Returned `0x887A0004 DXGI_ERROR_UNSUPPORTED` natively when spoofing disabled; cleanly intercepted and spoofed when enabled).
- **Result:** **ALL PASS**

### 3.4 Hardware Integration Test: `test_pso_interception.exe` (Phase 3 PSO & Resource)
- Resource Creation & Validation: **PASS** (Committed buffer created and validated).
- Root Signature Creation: **PASS** (Native root signature serialized and created).
- Valid Native Compute PSO Creation (SM 5.0): **PASS** (Created on hardware with `0x0`).
- Valid Native Graphics PSO Creation (SM 5.0 VS+PS): **PASS** (Created on hardware with `0x0`).
- Compute PSO Interception & Unsupported DXIL: **PASS** (DXIL SM 6.0 cleanly rejected by real hardware with `0x80004005 E_FAIL`, without crash or corruption).
- **Result:** **ALL PASS**

### 3.5 Regression Test: `test_d3d12.exe` (Phase 1 Proxy)
- Loaded proxy `d3d12.dll` and invoked `D3D12CreateDevice`: **PASS**

---

## 4. COM Wrapper Interface Coverage & Architecture Audit

The core device wrapper is implemented as `kepler::WrappedD3D12Device` in `include/core/feature_level_compat.h` and `src/feature_level_compat.cpp`.

### 4.1 Interface Hierarchy
```
IUnknown
   └── ID3D12Object
         └── ID3D12Device
               └── ID3D12Device1  <-- WrappedD3D12Device
```

### 4.2 QueryInterface Behavior
- Handles `IID_IUnknown`, `__uuidof(ID3D12Object)`, `__uuidof(ID3D12Device)` -> Returns `static_cast<ID3D12Device*>(this)`.
- Handles `__uuidof(ID3D12Device1)` -> Returns `static_cast<ID3D12Device1*>(this)`.
- Interfaces `ID3D12Device2` through `ID3D12Device14` are queried directly on the underlying `m_pReal`. Because the Kepler driver only natively implements `ID3D12Device` and `ID3D12Device1`, queries for `ID3D12Device2+` return `E_NOINTERFACE (0x80004002)`.
- Lifetime management is thread-safe using `std::atomic<ULONG> m_refCount`.

### 4.3 Currently Intercepted Methods
1. `CheckFeatureSupport`:
   - Intercepts `D3D12_FEATURE_FEATURE_LEVELS` to return spoofed feature level (e.g. `12_0`).
   - Forwards all other feature queries (`D3D12_FEATURE_D3D12_OPTIONS`, etc.) to native hardware driver.
2. `CreateGraphicsPipelineState`:
   - Intercepts descriptor, inspects shader bytecode containers via `DxilInspector`, validates against `ShaderPolicy`.
3. `CreateComputePipelineState`:
   - Intercepts descriptor, inspects compute bytecode, validates against `ShaderPolicy`.
4. `CreateCommittedResource`, `CreatePlacedResource`, `CreateReservedResource`:
   - Intercepts resource descriptors via `TextureClamper`, clamps dimensions and multisample parameters if enabled.

---

## 5. Identification of Gaps for Phase 4

While Phase 1–3 provide a solid hooking, inspection, and policy foundation, the following architectural gaps exist before games requiring full D3D12 execution or D3D11 translation can render:

| Subsystem | Current State | Phase 4 Requirement |
| :--- | :--- | :--- |
| **Command Queues** | Raw forwarding to native `m_pReal->CreateCommandQueue`. | Wrapped `WrappedD3D12CommandQueue` capable of intercepting `ExecuteCommandLists` and dispatching translated workloads. |
| **Command Allocators & Lists** | Raw forwarding to native `CreateCommandAllocator` and `CreateCommandList`. | Wrapped `WrappedD3D12GraphicsCommandList` implementing command recording, state tracking, and translation. |
| **Resource Translation** | Resources are raw native `ID3D12Resource` pointers. | Resource tracking structure (`TranslatedResource`) managing GPU/CPU backing, D3D11 buffer/texture representations, subresources, and mapping. |
| **Descriptor Heaps & Tables** | Raw forwarding to native driver. | CPU/GPU descriptor table mapping for CBVs, SRVs, UAVs, and Samplers. |
| **Root Signatures** | Raw forwarding to native driver. | Root parameter index-to-slot binding mapper. |
| **Resource Barriers** | Passed directly to native command lists. | Software state tracker emulating D3D12 barrier transitions on backends without native barriers (e.g., D3D11). |
| **Swapchain & Presentation** | Unhooked (DXGI handles swapchains directly). | Swapchain backbuffer tracking, format validation, and interop synchronization. |
| **Translation Backend** | Only native D3D12 passthrough exists. | Direct3D 11 backend (`D3D11Backend`) allowing D3D12 command lists to replay onto `ID3D11DeviceContext` on Kepler hardware. |

---

## 6. Target Hardware Realities & Backend Viability

The NVIDIA GeForce GT 740 (GK208, Kepler architecture) has specific hardware constraints that dictate the Phase 4 architecture:

1. **WDDM Driver Support:** NVIDIA driver 456.71 / 472.12 provides full WDDM 2.7 support, but limits D3D12 to Feature Level 11_0.
2. **Resource Binding Limits:** Kepler is hardware Resource Binding **Tier 2** (max 14 CBVs, 128 SRVs, 64 UAVs, 16 Samplers per shader stage). It does not support Tier 3 bindless descriptor indexing.
3. **Execution Units:** Kepler does not possess Wave Intrinsics, Subgroup operations, or FP16 ALU execution units.
4. **Memory Bandwidth:** ~14.4 GB/s on DDR3. Heavy over-allocation or uncompressed render targets can cause major pipeline stalls.
5. **D3D11 Maturity:** The NVIDIA Kepler D3D11 driver (`nvwgf2umx.dll`) is exceptionally mature and heavily optimized for GK208/GK107. A translation path from D3D12 to D3D11 provides an effective, highly compatible execution route for workloads that the Kepler D3D12 driver rejects.

---

## 7. Baseline Audit Verdict

**VERDICT: APPROVED.**  
The baseline codebase is stable, all unit and live hardware tests pass without error, and the architecture is clean and ready for Phase 4 implementation.
