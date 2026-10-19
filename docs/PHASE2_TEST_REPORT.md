# KeplerBridge — Phase 2 Verification & Test Report

**Date:** 2026-10-19  
**Target Architecture:** Windows 11 x64  
**Hardware:** NVIDIA GeForce GT 740 4GB DDR3 (Driver 27.21.14.5671)  
**Binary Tested:** `build/d3d12.dll` (Phase 2 Release Build)  
**Overall Status:** **ALL TESTS PASSED**

---

## 1. Test Suite Summary

Two dedicated test executables were developed and executed to validate Phase 2:
1. `build/test_policy_engine.exe`: Pure automated unit tests for policy decisions, feature level parsing, and edge cases.
2. `build/test_d3d12_compat.exe`: Integration test harness executing real Direct3D 12 calls against the proxy DLL and the physical NVIDIA GT 740 GPU.

---

## 2. Unit Test Results (`test_policy_engine.exe`)

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

---

## 3. Live Hardware Test Results (`test_d3d12_compat.exe`)

### Configuration A: `EnableFeatureLevelSpoof=0` (Passthrough / Phase 1 Mode)
* **Adapter Detected:** NVIDIA GeForce GT 740 (4052 MB VRAM)
* **Test 1 (ppDevice = nullptr):** Returned `S_FALSE (0x00000001)`. Correct Microsoft D3D12 capability query response.
* **Test 2 (Request FL 11_0):** Returned `S_OK (0x00000000)`. Native Kepler device created successfully.
* **Test 3 (Request FL 12_0):** Returned `0x887A0004 (DXGI_ERROR_UNSUPPORTED)`. Real hardware capability barrier reproduced.

### Configuration B: `EnableFeatureLevelSpoof=1` (Phase 2 Compatibility Mode)
* **Adapter Detected:** NVIDIA GeForce GT 740 (4052 MB VRAM)
* **Test 1 (ppDevice = nullptr):** Returned `S_FALSE (0x00000001)`. Correct behavior.
* **Test 2 (Request FL 11_0):** Returned `S_OK (0x00000000)`. Native creation untouched.
* **Test 3 (Request FL 12_0):**
  * `D3D12CreateDevice` Result: **`S_OK (0x00000000)`** (Bypassed!)
  * Policy Action: Downgraded native creation to `D3D_FEATURE_LEVEL_11_0`, succeeded, attached dynamic shadow vtable.
  * `dev->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS)` Result: Returned `S_OK`, `MaxSupportedFeatureLevel = 0xc000 (D3D_FEATURE_LEVEL_12_0)`.
  * Method Call Verification: `dev->GetNodeCount()` returned `1`. Native calls dispatch seamlessly.
  * Clean Shutdown: `dev->Release()` tracked refcount to 0, detached vtable, and freed resources without leaks.

---

## 4. Verification Evidence (Extract from `build/kepler_bridge.log`)

```text
[HOOK] Requested MinimumFeatureLevel: D3D_FEATURE_LEVEL_12_0 (0xc000)
[INFO] Hardware probe detected native maximum D3D12 feature level: D3D_FEATURE_LEVEL_11_0 (0xb000)
[INFO] [COMPAT] Policy Evaluation: Requested feature level (12_0) exceeds native hardware capability (11_0). Intercepting device creation at native level and attaching compatibility engine.
[HOOK] [COMPAT] Modifying device creation target: MinimumFeatureLevel D3D_FEATURE_LEVEL_12_0 (0xc000) -> D3D_FEATURE_LEVEL_11_0 (0xb000) (Target Spoofed Level: D3D_FEATURE_LEVEL_12_0 (0xc000))
[HOOK] Returned HRESULT: 0x00000000 - S_OK (Operation successful)
[INFO] Real D3D12CreateDevice succeeded. Device pointer: Non-null
[INFO] FeatureLevelCompat: Installed compatibility hooks on ID3D12Device (2485961049536), Target Spoof Level: D3D_FEATURE_LEVEL_12_0 (0xc000)
[HOOK] [COMPAT] Compatibility engine successfully attached to created ID3D12Device.
[HOOK] ============================================
[HOOK] [COMPAT] Spoofed CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS) -> MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_12_0 (0xc000) (Native hardware is 11_0)
[INFO] FeatureLevelCompat: Hooked ID3D12Device released to refcount 0 and cleaned up.
```
