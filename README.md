# KeplerBridge

Modular 64-bit DirectX 12-to-DirectX 11 Translation, Downgrade & Feature Level Compatibility Proxy DLL designed for legacy NVIDIA Kepler GPUs (e.g. GeForce GT 740 4GB DDR3).

---

## Current Status

* **Phase 1: The Ghost Hook (DLL Proxy & Telemetry Logger)**: **COMPLETE & AUDITED**
* **Phase 2: Feature Level Compatibility Engine**: **COMPLETE & VERIFIED (PASS WITH LIMITATIONS)**
* **Phase 3: DXIL Inspection, PSO Interception & Resource Compatibility**: **COMPLETE & VERIFIED (PASS WITH LIMITATIONS)**
* **Phase 4: Production Translation Layer**: **COMPLETE & EMPIRICALLY VERIFIED**

---

## Features Implemented

### Phase 1: Ghost Hook (DLL Proxy & Telemetry Logger)
* **Complete 64-bit D3D12 Proxy DLL (`d3d12.dll`)**:
  * Exports all 18 standard and modern Windows 11 / Agility SDK entry points unmangled.
  * Statically linked runtime (`-static`, `-static-libgcc`, `-static-libstdc++`) against UCRT: zero external runtime dependencies.
  * Safe dynamic loading of `C:\Windows\System32\d3d12.dll` with recursive self-load prevention.
* **Thread-Safe Telemetry Logger (`kepler_bridge.log`)**:
  * Millisecond-precision timestamps, thread IDs, adapter specifications, requested feature levels, COM REFIIDs, and HRESULT return codes.
  * Unbuffered immediate flush after every write to preserve diagnostics even across host application crashes.

### Phase 2: Feature Level Compatibility Engine
* **Cached Hardware Detection**: probes physical adapter once for maximum native Direct3D 12 feature level (`D3D_FEATURE_LEVEL_11_0` on Kepler GT 740) and caches the result.
* **Pure Policy Evaluation**: decoupled, deterministic rule engine evaluates whether interception is required.
* **ABI-Safe COM Wrapper (`WrappedD3D12Device`)**: replaces fragile vtable shadowing with standard C++ COM delegation and atomic reference counting. Supports `ID3D12Device` and `ID3D12Device1`.
* **Feature Level Spoofing**: intercepts `D3D12CreateDevice` requests for `D3D_FEATURE_LEVEL_12_0+`, creates the native device at `11_0`, wraps it, and intercepts `CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS)` to report `D3D_FEATURE_LEVEL_12_0`.
* **Native Driver Passthrough**: all other hardware feature queries (Shader Model, Resource Binding Tier, Conservative Rasterization) pass through to the real NVIDIA Kepler driver without fabricating unsupported capabilities.
* **Strict Default Safety**: `EnableFeatureLevelSpoof=0` by default; 100% untouched native passthrough when disabled.

### Phase 3: DXIL Inspection, PSO Interception & Resource Compatibility
* **Bounds-Checked DXBC Container Inspector (`DxilInspector`)**:
  * Validates container headers, chunk tables, and part-table offsets against buffer bounds.
  * Identifies chunk types: `DXIL` (Shader Model 6.x), `SHEX` (Shader Model 5.x), `SHDR` (Shader Model 4.x), `PSV0`, `ISGN`, `OSGN`, etc.
  * Decodes shader stages (Vertex, Pixel, Geometry, Hull, Domain, Compute, Mesh, Raytracing) and shader models (SM 4.0..6.7).
* **Pipeline State Interception (`PSOHook`)**:
  * Intercepts `CreateGraphicsPipelineState` and `CreateComputePipelineState`.
  * Logs detailed telemetry: pipeline type, stage, container format, shader model, bytecode length, and native hardware compilation result.
  * Preserves native hardware error propagation without fabricating fake success.
* **Modular Shader Policy Engine (`ShaderPolicy`)**:
  * Extensible `IShaderTranslator` interface for future decompilation/translation backends.
  * Controlled fallback policies disabled by default (`EnableShaderFallback=0`).
* **Resource Dimension Clamping & Validation (`TextureClamper`)**:
  * Validates resource descriptors against Direct3D 12 Feature Level 11_0 limits.
  * Enforces viewport safety: Render targets and depth-stencil surfaces are strictly protected from dimension clamping.
  * Aspect-ratio preserving texture clamping for linear textures when enabled (`EnableTextureClamping=1`).

### Phase 4: Production Translation Layer
* **Dual-Engine Hybrid Architecture**:
  * **Direct Mode**: Native D3D12 execution on Kepler hardware with zero translation overhead for supported workloads.
  * **Translation Mode**: Decoupled command recording and replay onto a Direct3D 11 backend (`ID3D11DeviceContext`).
* **Command Recording & Replay Engine (`CommandBatch`, `WrappedD3D12GraphicsCommandList`)**:
  * Records draw calls (`DrawInstanced`, `DrawIndexedInstanced`), dispatches (`Dispatch`), clear calls (`ClearRenderTargetView`, `ClearDepthStencilView`), resource copies (`CopyBufferRegion`, `CopyResource`), and state setups (`SetViewports`, `SetScissorRects`, `IASetPrimitiveTopology`, `IASetVertexBuffers`, `IASetIndexBuffer`).
* **Command Queue & Allocator Wrappers (`WrappedD3D12CommandQueue`, `WrappedD3D12CommandAllocator`)**:
  * Replays recorded command batches upon queue submission (`ExecuteCommandLists`).
  * Supports immediate context flushing and synchronization fences (`Signal`, `SetEventOnCompletion`).
* **Deterministic Resource State & Barrier Tracker (`ResourceStateTracker`)**:
  * Tracks subresource states in software, validating explicit transitions and preventing simultaneous RTV/SRV binding hazards.
* **Descriptor & View Mapping (`ResourceManager`)**:
  * Maps D3D12 CPU/GPU descriptor handles to underlying translated Direct3D 11 views (RTV, DSV, SRV, UAV).
  * Maps 32-bit root constants across graphics and compute pipeline stages.
* **Direct3D 11 Backend (`D3D11Backend`)**:
  * Translates D3D12 buffers and 2D textures to corresponding `ID3D11Buffer` and `ID3D11Texture2D` instances.
  * Replays command batches onto `ID3D11DeviceContext` with full hazard mitigation.

---

## Configuration (`kepler_bridge.ini`)

```ini
[General]
EnableLogging=1
LogVerboseCalls=1

[Spoof]
EnableFeatureLevelSpoof=0
TargetFeatureLevel=12_0
SpoofVRAMMB=4096

[Downgrader]
EnableShaderInspection=1
EnableShaderFallback=0
EnableTextureClamping=0
MaxTextureResolution=512
StripHeavyShaders=0

[Translation]
EnableD3D11Translation=0
EmulateBarriers=1
EnableStateTracking=1
ImmediateQueueFlush=1
EmulateQueries=1
```

* `EnableFeatureLevelSpoof=0` (default): Pure passthrough mode.
* `EnableD3D11Translation=0` (default): Native Direct3D 12 execution. Set to `1` to activate the D3D11 translation backend.
* `EmulateBarriers=1`: Tracks D3D12 barrier transitions and resolves binding hazards on backend.
* `EnableStateTracking=1`: Maintains software state ledger for all registered resources.

---

## Building from Source

### Prerequisites
* 64-bit Windows 10 or Windows 11.
* CMake 3.20+ (`cmake`).
* C++20 compatible compiler: MinGW-w64 (WinLibs UCRT) GCC 14+ or MSVC.
* Ninja build system.

### Build Commands:
```powershell
# Configure x64 Release
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release

# Build all binaries (d3d12.dll and all test executables)
cmake --build build --config Release
```

Build outputs in `build/`:
* `d3d12.dll`
* `kepler_bridge.ini`
* `test_shader_inspector.exe`
* `test_policy_engine.exe`
* `test_translation_engine.exe`
* `test_d3d12_compat.exe`
* `test_pso_interception.exe`
* `test_d3d12_translation.exe`
* `test_d3d12.exe`

---

## Running Verification Tests

```powershell
# 1. Shader & Resource Inspector Unit Tests (11/11 tests)
./build/test_shader_inspector.exe

# 2. Pure Policy Engine Unit Tests (6/6 tests)
./build/test_policy_engine.exe

# 3. Translation Engine Unit Tests (5/5 tests)
./build/test_translation_engine.exe

# 4. Live Hardware Feature Level Compatibility Suite
./build/test_d3d12_compat.exe

# 5. Live Hardware PSO & Resource Interception Suite
./build/test_pso_interception.exe

# 6. Live Hardware Translation & Command Execution Suite
./build/test_d3d12_translation.exe
```

---

## Documentation

* [Phase 1 Audit Report](docs/PHASE1_AUDIT.md)
* [Phase 2 Design Specification](docs/PHASE2_DESIGN.md)
* [Phase 2 Test & Verification Report](docs/PHASE2_TEST_REPORT.md)
* [Phase 3 Design Specification](docs/PHASE3_DESIGN.md)
* [Phase 3 Test & Verification Report](docs/PHASE3_TEST_REPORT.md)
* [Phase 4 Baseline Architecture Audit](docs/PHASE4_BASELINE_AUDIT.md)
* [Phase 4 Design Specification](docs/PHASE4_DESIGN.md)
* [Phase 4 Test & Verification Report](docs/PHASE4_TEST_REPORT.md)
