# KeplerBridge — Phase 2 Design Specification: Feature Level Compatibility Engine

**Architecture:** Windows x64  
**Target Hardware:** NVIDIA Kepler (specifically GeForce GT 740 4GB DDR3, GK208/GK107)  
**DirectX Target:** D3D12 to D3D11 Feature Level Bridge & Downgrade Proxy  
**Current Revision:** 2.0 (Post-Audit ABI-Safe COM Wrapper Architecture)

---

## 1. Problem Statement

On modern Windows platforms running Direct3D 12 applications and games, executables often enforce a strict minimum requirement at startup:
```cpp
D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&pDevice));
```
Legacy Kepler-architecture GPUs (such as the GT 740) have native Direct3D 12 WDDM driver support in Windows, but physical silicon and microcode strictly support up to **`D3D_FEATURE_LEVEL_11_0`** (`0xb000`).

Consequently:
1. The Microsoft Direct3D 12 runtime rejects `D3D12CreateDevice(..., D3D_FEATURE_LEVEL_12_0, ...)` with `0x887A0004 (DXGI_ERROR_UNSUPPORTED)`.
2. The game process immediately displays an error dialog (e.g., "DirectX 12 feature level 12_0 is required to run the engine") and terminates before rendering a single frame.
3. Even if an engine initializes at a lower feature level, it subsequently calls `pDevice->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, ...)` to query maximum capability, immediately aborting if the returned level is below `12_0`.

---

## 2. Architectural Design & Evolution

### 2.1 Why Dynamic VTable Shadowing Was Rejected
An initial prototype attempted dynamic vtable shadowing (allocating a fixed 128-pointer array, copying `lpVtbl`, and replacing slots 13 and 2). An independent technical audit revealed three critical architectural flaws:
1. **VTable Size & Boundary Insecurity:** Modern Windows 11 `d3d12.dll` implements `ID3D12Device10`+ with 125+ virtual methods. Reading a fixed 128 entries risks out-of-bounds pointer reads from adjacent memory pages.
2. **`QueryInterface` Bypass:** Games frequently create an `ID3D12Device` and subsequently call `QueryInterface(IID_PPV_ARGS(&pDevice1))`. In vtable shadowing, unhooked `QueryInterface` returns the raw driver vtable, stripping all interception hooks.
3. **MSVC/MinGW ABI Brittleness:** Directly mutating `*(void***)pDevice` violates COM identity and lifetime invariants, risking crashes on different driver versions.

### 2.2 The ABI-Safe COM Wrapper Architecture (`WrappedD3D12Device`)
KeplerBridge adopts standard, fully compliant COM aggregation and delegation:

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
     │        ├─► [ Hardware Probe (Cached) ] ─── Detect Native Maximum (GT 740 = FL 11_0)
     │        │
     │        └─► [ Policy Decision ]
     │                 ├─ If Spoof Disabled / Req <= Native ──► 100% Native Passthrough
     │                 └─ If Spoof Enabled && Req > Native  ──► Create Real Device at FL 11_0
     │                                                          + Wrap in WrappedD3D12Device
     │
     ├─► [ Real System32\d3d12.dll ] ─── Real `D3D12CreateDevice(..., FL 11_0, ...)` -> S_OK!
     │
     └─► [ WrappedD3D12Device : public ID3D12Device1 ]
              │
              ├─► `CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS)`
              │        └─► Reports `MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_12_0`
              │
              ├─► `CheckFeatureSupport(Other Features)`
              │        └─► Passes through truthfully to real Kepler driver (SM 5.1, Tier 2, etc.)
              │
              ├─► `QueryInterface`
              │        ├─► Returns `this` (with AddRef) for `IUnknown`, `ID3D12Object`, `ID3D12Device`, `ID3D12Device1`
              │        └─► Forwards unrecognized interfaces to real driver
              │
              ├─► Atomic Reference Counting (`AddRef` / `Release`)
              │        └─► When refcount drops to 0: frees wrapper and releases real device cleanly
              │
              └─► Forwards all other 44+ methods directly to `m_pReal`
```

---

## 3. Core Components

### 3.1 Policy Engine (`FeatureLevelCompat::EvaluatePolicy`)
A deterministic, pure evaluation function independent of global mutable state. It evaluates:
* `spoofEnabled` (`bool`): Loaded from `kepler_bridge.ini` (default `0`).
* `targetFeatureLevelStr` (`std::string`, e.g. "12_0").
* `nativeMaxLevel` (`D3D_FEATURE_LEVEL`, e.g. `0xb000`).
* `requestedLevel` (`D3D_FEATURE_LEVEL`, e.g. `0xc000`).

Policy rules:
1. If `!spoofEnabled` -> `should_intercept = false`, `creation_level = requestedLevel`.
2. If `requestedLevel <= nativeMaxLevel` -> `should_intercept = false`, `creation_level = requestedLevel`.
3. If `requestedLevel > nativeMaxLevel` and `spoofEnabled` -> `should_intercept = true`, `creation_level = nativeMaxLevel`, `target_spoof_level = parsedTarget`.

### 3.2 Cached Native Capability Detection (`DetectNativeMaxFeatureLevel`)
Probes the physical adapter by attempting native device creation descending from `12_2` down to `11_0`. On the GT 740, `11_0` succeeds and higher levels fail. The result is cached in `m_cachedNativeLevel` protected by `std::mutex`, eliminating redundant probing overhead on subsequent calls.

### 3.3 ABI-Safe Device Method Interception (`WrappedD3D12Device`)
1. Implements standard COM interfaces `ID3D12Device` and `ID3D12Device1`.
2. Intercepts `CheckFeatureSupport`:
   - For `D3D12_FEATURE_FEATURE_LEVELS`: scans requested feature levels and reports the best match $\le$ `m_spoofedLevel` (`12_0`).
   - For all other queries (e.g. `D3D12_FEATURE_SHADER_MODEL`, `D3D12_FEATURE_D3D12_OPTIONS`), queries are forwarded to `m_pReal` so truthful hardware capabilities are returned.
3. Lifetime Management:
   - Holds an internal reference to `m_pReal`.
   - Uses `std::atomic<ULONG> m_refCount`.
   - When `Release()` decrements the count to 0, it releases `m_pReal` and deletes itself. Zero resource leaks.

---

## 4. Hardware Realism & Explicit Limitations

> [!WARNING]
> Feature level spoofing allows applications to pass initial startup gates, but does **NOT** synthesize missing hardware execution units:
> - **Hardware Resource Limits**: Kepler GPUs lack hardware support for Direct3D 12 Resource Binding Tier 3, Tiled Resources Tier 2, Conservative Rasterization, and Wave Intrinsics.
> - **Shader Model Limits**: Kepler natively supports Shader Model 5.0 (and SM 5.1 under D3D12). It does **not** support Shader Model 6.0+ (DXIL / Wave Ops).
> - **Pipeline Limitations**: If a game attempts to compile Shader Model 6.x shaders or bind unbounded descriptor tables, the driver will reject them.
> - **Roadmap**: These limitations are strictly addressed in **Phase 3 (Shader Model & Downgrader Transpiler)** and **Phase 4 (D3D11 Fallback Translation & State Emulation)**.
