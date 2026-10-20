# KeplerBridge — Phase 2 Verification & Test Report

**Date:** 2026-10-19  
**Target Architecture:** Windows 11 x64  
**Hardware:** NVIDIA GeForce GT 740 4GB DDR3 (Driver 27.21.14.5671, GK208/GK107)  
**Binary Tested:** `build/d3d12.dll` (Phase 2 Release Build, WinLibs MinGW GCC 16.2.0, static runtime)  
**Overall Status:** **ALL VERIFICATION TESTS PASSED (PASS WITH LIMITATIONS DOCUMENTED)**

---

## 1. Test Suite Summary

The verification suite consists of two separate, reproducible harnesses:
1. `build/test_policy_engine.exe`: Pure automated unit tests for policy decisions, feature level parsing, and edge cases (no GPU required).
2. `build/test_d3d12_compat.exe`: Direct3D 12 live hardware integration harness executing against the physical NVIDIA GeForce GT 740.

---

## 2. Unit Test Results (`test_policy_engine.exe`)

Command: `.\build\test_policy_engine.exe`  
Exit Code: `0`

| Test Case | Description | Expected Outcome | Actual Outcome | Status |
| :--- | :--- | :--- | :--- | :--- |
| **TC1** | `ParseFeatureLevelString` ("11_0", "11_1", "12_0", "12_1", "12_2", invalid fallback) | Correct `D3D_FEATURE_LEVEL` enum values | Exactly matched all enums | **PASS** |
| **TC2** | `FeatureLevelToShortString` format conversion | Short strings ("11_0", "12_0", etc.) | Exactly matched strings | **PASS** |
| **TC3** | Policy: Spoof disabled | `should_intercept = false`, level unchanged | `should_intercept = false`, level = 12_0 | **PASS** |
| **TC4** | Policy: Request <= Native (FL 11_0 <= FL 11_0) | `should_intercept = false`, level = 11_0 | Native passthrough preserved | **PASS** |
| **TC5** | Policy: Request below Native (FL 10_0 < FL 11_0) | `should_intercept = false`, level = 10_0 | Native passthrough preserved | **PASS** |
| **TC6** | Policy: Request > Native (FL 12_0 > FL 11_0) | `should_intercept = true`, creation = 11_0, target = 12_0 | Intercepted, creation = 11_0, target = 12_0 | **PASS** |
| **TC7** | Policy: Request > Native (FL 12_1 > FL 11_0) | `should_intercept = true`, creation = 11_0, target = 12_1 | Intercepted, creation = 11_0, target = 12_1 | **PASS** |
| **TC8** | Configuration missing file fallback | Return `false`, internal defaults safely used | Defaults intact, no crash | **PASS** |

Summary: **6/6 Test Functions Passed (100%)**.

---

## 3. Live Hardware Verification Results (`test_d3d12_compat.exe`)

### Configuration A: `EnableFeatureLevelSpoof=0` (Default / Native Passthrough Mode)
* **Adapter Detected:** NVIDIA GeForce GT 740 (4052 MB VRAM)
* **Test 1 (Capability Probing, ppDevice = nullptr):** Returned `0x1 (S_FALSE)`. Correct MSDN capability query response.
* **Test 2 (Request Native FL 11_0):** Returned `0x0 (S_OK)`. Native Kepler device created successfully (`NodeCount = 1`).
* **Test 3 (Request FL 12_0):** Returned `0x887A0004 (DXGI_ERROR_UNSUPPORTED)`. Hardware rejection cleanly preserved without interference.

Telemetry Log Evidence:
```text
2026-10-19 20:08:57.448 [TID 00580] [HOOK] Requested MinimumFeatureLevel: D3D_FEATURE_LEVEL_12_0 (0xc000)
2026-10-19 20:08:57.448 [TID 00580] [INFO] [COMPAT] Policy Evaluation: Compatibility spoofing is disabled in kepler_bridge.ini ([Spoof] EnableFeatureLevelSpoof=0).
2026-10-19 20:08:57.522 [TID 00580] [HOOK] Returned HRESULT: 0x887A0004 - DXGI_ERROR_UNSUPPORTED
```

### Configuration B: `EnableFeatureLevelSpoof=1` (Compatibility Mode)
* **Adapter Detected:** NVIDIA GeForce GT 740 (4052 MB VRAM)
* **Test 1 (Capability Probing, ppDevice = nullptr):** Returned `0x1 (S_FALSE)`.
* **Test 2 (Request Native FL 11_0):** Returned `0x0 (S_OK)`. Native passthrough maintained because request does not exceed hardware capabilities.
* **Test 3 (Request FL 12_0):**
  * `D3D12CreateDevice` Result: **`0x0 (S_OK)`** (Bypassed!).
  * Real Hardware Action: Real device created at native `D3D_FEATURE_LEVEL_11_0`.
  * Wrapper Action: Wrapped in `WrappedD3D12Device` with target spoof level `0xc000`.
  * `CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS)`: Reported `MaxSupportedFeatureLevel = 0xc000 (D3D_FEATURE_LEVEL_12_0)`.
  * Method Call Dispatch: `dev->GetNodeCount()` returned `1`.
  * COM `QueryInterface`: `dev->QueryInterface(ID3D12Device1)` returned `0x0 (S_OK)` and gave valid interface.
  * Lifetime Management: `Release()` destroyed wrapper and released real device cleanly without crashes or leaks.

Telemetry Log Evidence:
```text
2026-10-19 20:09:20.434 [TID 13480] [INFO] [COMPAT] Policy Evaluation: Requested feature level (12_0) exceeds native hardware capability (11_0). Intercepting device creation at native level and attaching compatibility engine.
2026-10-19 20:09:20.434 [TID 13480] [HOOK] [COMPAT] Intercepting device creation: MinimumFeatureLevel D3D_FEATURE_LEVEL_12_0 (0xc000) -> D3D_FEATURE_LEVEL_11_0 (0xb000) (Target Spoofed Level: D3D_FEATURE_LEVEL_12_0 (0xc000))
2026-10-19 20:09:20.599 [TID 13480] [HOOK] Returned HRESULT (Native Real): 0x00000000 - S_OK (Operation successful)
2026-10-19 20:09:20.599 [TID 13480] [HOOK] [COMPAT] Successfully wrapped ID3D12Device with COM compatibility wrapper.
2026-10-19 20:09:20.599 [TID 13480] [HOOK] ============================================
2026-10-19 20:09:20.599 [TID 13480] [HOOK] [COMPAT] Spoofed CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS) -> MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_12_0 (0xc000) (Native hardware is 11_0)
2026-10-19 20:09:20.599 [TID 13480] [INFO] WrappedD3D12Device: RefCount reached zero, destroying wrapper.
```

---

## 4. Hardware and Capability Boundaries

| Feature | Kepler GT 740 Hardware Support | Phase 2 Compatibility Engine Behavior | Limitation & Requirement for Phase 3/4 |
| :--- | :--- | :--- | :--- |
| **Minimum Feature Level Boot Check** | FL 11_0 only | Spoofed to FL 12_0 via COM wrapper | Bypassed. Game proceeds to initialization. |
| **`CheckFeatureSupport(FEATURE_LEVELS)`** | Reports FL 11_0 | Reports FL 12_0 | Bypassed. Engine allows rendering path. |
| **Shader Model 6.x (DXIL / Wave Ops)** | Not Supported (SM 5.1 max) | Passes through to real driver | **Hard Barrier.** Requires Phase 3 HLSL/DXBC transpiler. |
| **Resource Binding Tier 3** | Tier 2 max | Passes through to real driver | **Hard Barrier.** Engine must use bounded descriptors or translation. |
| **Ray Tracing (DXR)** | Not Supported | Passes through to real driver | **Hard Barrier.** Cannot be emulated in hardware. |
| **Conservative Rasterization** | Not Supported | Passes through to real driver | **Hard Barrier.** Requires software fallback or disabling in config. |
