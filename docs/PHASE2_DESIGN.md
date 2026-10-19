# KeplerBridge — Phase 2 Design Specification: Feature Level Compatibility Engine

**Architecture:** Windows x64  
**Target Hardware:** NVIDIA Kepler (specifically GeForce GT 740 4GB DDR3)  
**DirectX Target:** D3D12 to D3D11 Feature Level Bridge & Downgrade Proxy

---

## 1. Problem Statement

On modern Windows platforms running Direct3D 12 applications and games, executables often enforce a hard boot requirement:
```cpp
D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&pDevice));
```
Legacy Kepler-architecture GPUs (GK107 / GK208 cores, such as the GT 740) have native Direct3D 12 WDDM driver support in Windows, but their physical silicon and microcode strictly support up to **`D3D_FEATURE_LEVEL_11_0`** (`0xb000`).

Consequently:
1. The Microsoft Direct3D 12 runtime rejects `D3D12CreateDevice(..., D3D_FEATURE_LEVEL_12_0, ...)` with `0x887A0004 (DXGI_ERROR_UNSUPPORTED)`.
2. The game process immediately displays an error dialog (e.g. "DirectX 12 feature level 12_0 is required to run the engine") and terminates before rendering a single frame.
3. Even if the application attempts to initialize at a lower feature level, it subsequently calls `pDevice->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, ...)` to query maximum capability, immediately failing if the returned level is below `12_0`.

---

## 2. Architectural Design

The Phase 2 Compatibility Engine introduces a decoupled, policy-driven compatibility interception layer:

```
[ Game / Host Application ]
           │
           ▼
[ KeplerBridge Proxy d3d12.dll ]
     │
     ├─► [ Config (`kepler_bridge.ini`) ] ─── Read `EnableFeatureLevelSpoof`, `TargetFeatureLevel`
     │
     ├─► [ FeatureLevelCompat Policy Engine ]
     │        │
     │        ├─► [ Hardware Probe ] ─── Detect Native Maximum (GT 740 = FL 11_0)
     │        │
     │        └─► [ Policy Decision ]
     │                 ├─ If Spoof Disabled / Req <= Native ──► Forward Native Level
     │                 └─ If Spoof Enabled && Req > Native  ──► Downgrade Creation to 11_0
     │                                                          + Flag for Device Hooking
     │
     ├─► [ Real System32\d3d12.dll ] ─── Native `D3D12CreateDevice(..., FL 11_0, ...)` -> S_OK!
     │
     └─► [ Device Shadow VTable Hook ]
              │
              ├─► Intercepts VTable[13]: `CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS)`
              │        └─► Reports `MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_12_0`
              │
              ├─► Intercepts VTable[2]: `Release()`
              │        └─► Tracks refcount to 0 and cleans up shadow vtable safely
              │
              └─► Forwards all other ~40+ methods directly to native NVIDIA Kepler driver
```

---

## 3. Core Components

### 3.1 Policy Engine (`FeatureLevelCompat::EvaluatePolicy`)
A deterministic, pure evaluation function independent of global mutable state. It compares:
* `spoofEnabled` (`bool`)
* `targetFeatureLevelStr` (`std::string`, e.g. "12_0")
* `nativeMaxLevel` (`D3D_FEATURE_LEVEL`, e.g. `0xb000`)
* `requestedLevel` (`D3D_FEATURE_LEVEL`, e.g. `0xc000`)

If interception is triggered, it specifies `creation_level = D3D_FEATURE_LEVEL_11_0` and `target_spoof_level = D3D_FEATURE_LEVEL_12_0`.

### 3.2 Native Capability Detection (`FeatureLevelCompat::DetectNativeMaxFeatureLevel`)
Probes the physical adapter by attempting native device creation descending from `12_2` down to `11_0`. On the GT 740, `11_0` succeeds and higher levels fail, reliably establishing ground truth without hardcoding.

### 3.3 Dynamic VTable Shadowing (`FeatureLevelCompat::InstallDeviceHooks`)
Rather than wrapping all 44+ methods of `ID3D12Device` with heavy boilerplate wrappers that risk missing newer `ID3D12Device1..14` interfaces, KeplerBridge employs **VTable Shadowing**:
1. When an `ID3D12Device` is created successfully, its original virtual function table pointer (`lpVtbl`) is copied into a dynamically allocated 128-entry shadow table.
2. Slot 13 (`CheckFeatureSupport`) is replaced with `HookedCheckFeatureSupport`.
3. Slot 2 (`Release`) is hooked with `HookedRelease` to monitor lifetime.
4. The instance's vtable pointer (`*(void***)pDevice`) is pointed to the shadow table.
5. When the game queries `D3D12_FEATURE_FEATURE_LEVELS`, slot 13 returns the spoofed feature level (`12_0`), bypassing the game's check.
6. When the game calls any other device method (e.g., `CreateCommandQueue`, `CreateGraphicsPipelineState`, `GetNodeCount`), the call dispatches directly to native NVIDIA driver functions with zero CPU overhead.

---

## 4. Hardware Realism & Explicit Limitations

> [!WARNING]
> Spoofing a feature level does **NOT** magically synthesize missing hardware silicon or shader instructions:
> - **Hardware Resource Limits**: Kepler GPUs lack hardware support for Direct3D 12 Resource Binding Tier 3, Tiled Resources Tier 2, Conservative Rasterization, and Wave Intrinsics.
> - **Shader Model Limits**: Kepler natively supports Shader Model 5.0 (and 5.1 under D3D12). It does **not** support Shader Model 6.0+ (DXIL / Wave Ops).
> - **Phase 3 Requirement**: If a game attempts to compile Shader Model 6.x shaders or bind unbounded descriptor tables, the driver will reject them. This will be addressed in **Phase 3 (Shader Model & Downgrader Transpiler)** and **Phase 4 (D3D11 Fallback Translation)**.
