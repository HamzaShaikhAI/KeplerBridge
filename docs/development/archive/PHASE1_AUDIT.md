# KeplerBridge — Phase 1 Independent Audit Report

**Date:** 2026-10-19  
**Auditor:** Senior Windows Graphics & Reverse-Engineering Engineer  
**Target Hardware:** NVIDIA GeForce GT 740 (4GB DDR3 VRAM, Kepler Architecture)  
**Host Platform:** Windows 11 Pro 64-bit (Build 26200), AMD64  
**Audit Status:** **PASS WITH LIMITATIONS**

---

## 1. Executive Summary

An independent audit was conducted on the Phase 1 codebase ("The Ghost Hook: DLL Proxy & Telemetry Logger"). The audit independently verified the toolchain, built the codebase in a fresh audit directory (`audit_build`), inspected binary exports, verified runtime dependencies, analyzed Direct3D 12 native hardware capabilities directly on the host NVIDIA GT 740 GPU, and tested proxy interception and telemetry.

The proxy architecture functions as specified: it successfully intercepts `D3D12CreateDevice`, queries adapter telemetry via DXGI, forwards calls cleanly to `C:\Windows\System32\d3d12.dll`, logs structured thread-safe telemetry with microsecond-to-millisecond precision, and records the exact hardware feature-level rejection.

Two limitations were identified during the audit:
1. **Export Surface Scope**: The Phase 1 DLL exports 8 standard D3D12 functions. The host Windows 11 system `d3d12.dll` exports 18 entry points (including Agility SDK entry points such as `D3D12CoreCreateLayeredDevice` and DRED diagnostics). Games using Agility SDK could fail to link if these exports are absent.
2. **DllMain Loader-Lock Hazard**: `ProxyLoader::Initialize` is currently invoked during `DLL_PROCESS_ATTACH` in `DllMain`, issuing `LoadLibraryW` on `System32\d3d12.dll`. While functional on Windows 11, best practices recommend lazy on-demand binding to prevent potential loader-lock deadlocks in hostile game environments.

---

## 2. Environment & Dependency Verification

| Component | Detected Specification | Status |
| :--- | :--- | :--- |
| **Operating System** | Microsoft Windows 11 Pro (10.0.26200), 64-bit | Verified |
| **Target Architecture** | x86_64 / AMD64 | Verified |
| **C++ Standard** | C++20 (`-std=c++20`) | Verified |
| **Compiler** | GCC / G++ 16.2.0 (MinGW-W64 x86_64-ucrt-posix-seh, WinLibs) | Verified |
| **Build System** | CMake 4.4.3 + Ninja 1.13.2 | Verified |
| **DirectX SDK / Headers** | MinGW-W64 native DirectX SDK (`d3d12.h`, `dxgi1_4.h`, `dxgi1_6.h`, `libdxgi.a`, `libdxguid.a`) | Verified |
| **PE Inspection Tools** | `objdump.exe`, `nm.exe`, `readelf.exe` | Verified |
| **Git Version Control** | Initialized and tracked (`C:\Program Files\Git\cmd\git.exe`) | Verified |
| **Runtime Linking** | Statically linked C/C++ runtimes (`-static -static-libgcc -static-libstdc++`) against UCRT | Verified Zero Missing DLLs |

---

## 3. Independent Clean Build Verification

A clean build was configured and compiled in an isolated directory (`audit_build`) using the verified toolchain:

### Configuration Command:
```powershell
cmake -B audit_build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
```
*Output:*
```
-- The CXX compiler identification is GNU 16.2.0
-- Detecting CXX compiler ABI info - done
-- Detecting CXX compile features - done
-- Configuring done (2.5s)
-- Generating done (0.1s)
-- Build files have been written to: C:/Users/win10/Documents/KeplerBridge/audit_build
```

### Build Command:
```powershell
cmake --build audit_build --config Release -v
```
*Output:*
```
[1/5] c++.exe -Dd3d12_EXPORTS -I.../include -O3 -DNDEBUG -std=c++20 -c src/dllmain.cpp
[2/5] c++.exe -Dd3d12_EXPORTS -I.../include -O3 -DNDEBUG -std=c++20 -c src/config.cpp
[3/5] c++.exe -Dd3d12_EXPORTS -I.../include -O3 -DNDEBUG -std=c++20 -c src/logger.cpp
[4/5] c++.exe -Dd3d12_EXPORTS -I.../include -O3 -DNDEBUG -std=c++20 -c src/proxy_loader.cpp
[5/5] c++.exe -O3 -DNDEBUG -shared src/exports.def -static -static-libgcc -static-libstdc++ -o d3d12.dll -ldxgi -ldxguid -luser32 ...
```
*Outcome:* Build succeeded with 0 compiler warnings and 0 linker errors. Output binary: `audit_build/d3d12.dll` (size ~362 KB) and `audit_build/kepler_bridge.ini`.

---

## 4. Binary Export & Dependency Analysis

Using `objdump -p audit_build/d3d12.dll`:

### Machine Type:
* Header format: `pei-x86-64` (strictly 64-bit AMD64).

### Export Table:
```text
[0] D3D12CreateDevice
[1] D3D12CreateRootSignatureDeserializer
[2] D3D12CreateVersionedRootSignatureDeserializer
[3] D3D12EnableExperimentalFeatures
[4] D3D12GetDebugInterface
[5] D3D12GetInterface
[6] D3D12SerializeRootSignature
[7] D3D12SerializeVersionedRootSignature
```
All exports match Microsoft's official names without C++ name mangling.

### Imported DLLs:
```text
DLL Name: KERNEL32.dll
DLL Name: api-ms-win-crt-runtime-l1-1-0.dll
DLL Name: api-ms-win-crt-stdio-l1-1-0.dll
DLL Name: api-ms-win-crt-string-l1-1-0.dll
... (standard Windows Universal C Runtime only)
```
No dependencies on `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, or `libwinpthread-1.dll`. The DLL is 100% standalone and portable.

---

## 5. Hardware Capabilities & Empirical Results

We probed the host GPU hardware via both WMI and Direct3D 12 capability APIs against the real `C:\Windows\System32\d3d12.dll`:

### Hardware Identity:
* **GPU**: NVIDIA GeForce GT 740 (Kepler architecture, GK107/GK208 core)
* **Driver Version**: `27.21.14.5671` (NVIDIA Driver branch 456.71, release 9/30/2020)
* **Dedicated VRAM**:
  * DXGI Reported (`DXGI_ADAPTER_DESC1.DedicatedVideoMemory`): `4248829952` bytes (**4052 MB**)
  * WMI Reported (`Win32_VideoController.AdapterRAM`): `4293918720` bytes (~4095 MB / 4 GB)
  * Hardware Configuration: 4GB DDR3 Dedicated VRAM confirmed.

### Native Direct3D 12 Feature Level Test Matrix:
| Feature Level | Hex Code | Native `D3D12CreateDevice` Result | `CheckFeatureSupport` Result |
| :--- | :--- | :--- | :--- |
| `D3D_FEATURE_LEVEL_11_0` | `0xb000` | **`S_OK (0x00000000)`** | MaxSupportedFeatureLevel = `0xb000` |
| `D3D_FEATURE_LEVEL_11_1` | `0xb100` | **`DXGI_ERROR_UNSUPPORTED (0x887A0004)`** | N/A (Device Creation Failed) |
| `D3D_FEATURE_LEVEL_12_0` | `0xc000` | **`DXGI_ERROR_UNSUPPORTED (0x887A0004)`** | N/A (Device Creation Failed) |
| `D3D_FEATURE_LEVEL_12_1` | `0xc100` | **`DXGI_ERROR_UNSUPPORTED (0x887A0004)`** | N/A (Device Creation Failed) |
| `D3D_FEATURE_LEVEL_12_2` | `0xc200` | **`DXGI_ERROR_UNSUPPORTED (0x887A0004)`** | N/A (Device Creation Failed) |

### Key Architectural Conclusion:
The NVIDIA Kepler GT 740 has full, working Direct3D 12 driver support, but hardware capabilities are strictly bounded at **`D3D_FEATURE_LEVEL_11_0`**. Whenever modern games request `D3D_FEATURE_LEVEL_12_0` or higher, the Microsoft Direct3D 12 runtime immediately aborts device creation with `DXGI_ERROR_UNSUPPORTED`.

---

## 6. Audit Verdict & Defect Resolutions

**Status: PASS WITH LIMITATIONS**

### Actions for Phase 2:
1. **Implement Feature Level Compatibility Engine**:
   When games request `MinimumFeatureLevel = 12_0` (or configured target), create the underlying native device using `D3D_FEATURE_LEVEL_11_0` (which succeeds natively on Kepler), wrap the device interface, and intercept `CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, ...)` so games recognize the device as meeting the requested feature level.
2. **Expand Proxy Forwarding to Cover All 18 System32 Exports**:
   Add missing Agility SDK and PIX forwarded symbols to prevent entry-point missing errors in modern games.
3. **Make ProxyLoader Initialization Lazy / Thread-Safe**:
   Ensure `LoadLibraryW` on the real DLL is guarded and executed on first export call, making `DllMain` minimal and lock-safe.
