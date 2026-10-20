# KeplerBridge — Phase 3 Design Specification: DXIL Inspection, PSO Interception & Resource Compatibility

**Architecture:** Windows x64  
**Target Hardware:** NVIDIA Kepler (GeForce GT 740 4GB DDR3, GK208/GK107)  
**DirectX Target:** Direct3D 12 Shader Model & Resource Compatibility Layer  
**Current Revision:** 1.0 (Phase 3 Release)

---

## 1. Problem Statement

Following Phase 2 (Feature Level Compatibility Engine), a Direct3D 12 game successfully passes the initial `D3D12CreateDevice(..., D3D_FEATURE_LEVEL_12_0, ...)` check and proceeds to engine initialization. However, modern engines immediately face two critical barriers:

1. **Shader Model 6.x (DXIL) Execution Barrier:**
   Modern engines compile their shaders with `dxc.exe` into **DXIL (DirectX Intermediate Language)** targeting Shader Model 6.0 through 6.7. Legacy Kepler GPUs natively support only **Shader Model 5.0 (and SM 5.1 in D3D12)** via the legacy FXC/DXBC compiler. The NVIDIA Kepler driver rejects DXIL bytecode at pipeline state creation (`CreateGraphicsPipelineState` / `CreateComputePipelineState`) with `0x80004005 (E_FAIL)` or `0x80070057 (E_INVALIDARG)`.

2. **Resource Dimension & Memory Bandwidth Constraints:**
   The GeForce GT 740 is equipped with 4 GB of DDR3 VRAM over a 64-bit bus (yielding ~14.4 GB/s memory bandwidth—roughly 1/30th the bandwidth of modern GPUs). Games requesting 4K textures or allocating massive uncompressed textures will instantly choke the PCIe/DDR3 bus or exceed available memory.

Phase 3 introduces a modular, bounds-checked shader inspection engine, pipeline state interception, shader policy management, and resource compatibility validation.

---

## 2. Direct3D Shader Bytecode Container Architecture (DXBC vs. DXIL)

Both legacy DXBC (Shader Model 4.x/5.x) and modern DXIL (Shader Model 6.x) shaders are encapsulated in the Microsoft `'DXBC'` outer container format:

```
+--------------------------------------------------------------+
| DXBC Container Header (32 bytes)                             |
|  - uint32_t magic = 0x43425844 ('DXBC')                     |
|  - uint8_t  checksum[16] (128-bit hash)                      |
|  - uint32_t version = 1                                      |
|  - uint32_t totalSize (Total container size in bytes)        |
|  - uint32_t chunkCount (Number of internal chunks)           |
+--------------------------------------------------------------+
| Chunk Offset Table: uint32_t chunkOffsets[chunkCount]        |
+--------------------------------------------------------------+
| Chunk 0: FourCC ('SHEX' / 'DXIL' / 'ISGN' / 'PSV0') + Size   |
|          Chunk Data                                          |
+--------------------------------------------------------------+
| Chunk N: ...                                                 |
+--------------------------------------------------------------+
```

### 2.1 Chunk FourCC Signatures
- **`'SHDR'` (`0x52444853`)**: Shader Model 4.0 / 4.1 legacy bytecode.
- **`'SHEX'` (`0x58454853`)**: Shader Model 5.0 / 5.1 bytecode (natively executable on Kepler).
- **`'DXIL'` (`0x4C495844`)**: Shader Model 6.0+ LLVM bitcode (unsupported on Kepler).
- **`'PSV0'` (`0x30565350`)**: Pipeline State Validation chunk.
- **`'ISGN'` / `'OSGN'`**: Input/Output reflection signatures.
- **`'RDEF'` / `'STAT'`**: Resource definitions and shader statistics.

### 2.2 Bounds-Checked Inspection Engine (`DxilInspector`)
To guarantee process safety and avoid crash vulnerabilities from untrusted or corrupted bytecode:
1. Validates that buffer length is $\ge 32$ bytes before reading header.
2. Validates `magic == 0x43425844`.
3. Validates `totalSize <= bufferLength` (detects truncated containers).
4. Validates `sizeof(DXBCHeader) + chunkCount * 4 <= totalSize`.
5. For each chunk:
   - Validates `chunkOffset + 8 <= totalSize`.
   - Validates `chunkOffset + 8 + chunkSize <= totalSize`.
6. Inspects chunk sub-headers:
   - For `DXIL`: decodes `DxilProgramHeader` (`bitcodeHeaderVersion`) to extract shader stage (VS, PS, CS, etc.) and model (SM 6.0..6.7).
   - For `SHEX`/`SHDR`: decodes `versionToken` to extract shader stage and model (SM 4.x / 5.x).

---

## 3. Shader Translation Strategy Feasibility Research

A rigorous engineering evaluation of available open-source and proprietary tools was conducted:

| Tool | Capabilities | Input Format | Output Format | Suitability for Kepler DXIL->DXBC |
| :--- | :--- | :--- | :--- | :--- |
| **DirectX Shader Compiler (DXC)** | Official LLVM HLSL compiler | HLSL Source | DXIL Bitcode / SPIR-V | **Unsuitable.** DXC cannot decompile DXIL binary back to HLSL or legacy DXBC. The legacy backend was deprecated. |
| **SPIRV-Cross** | Shader language transpiler | SPIR-V Binary | GLSL, MSL, HLSL Source | **Unsuitable.** Ingests SPIR-V, not DXIL; outputs HLSL/GLSL, not binary DXBC. |
| **LLVM-Dis (`llvm-dis`)** | Bitcode disassembler | LLVM Bitcode | LLVM IR Assembly (.ll) | **Unsuitable.** Produces human-readable LLVM IR; does not produce GPU microcode. |
| **vkd3d-shader** | Wine/Proton shader compiler | DXBC / SPIR-V / DXIL (partial) | SPIR-V / GLSL / HLSL | **Emerging / Partial.** Translates DXIL to SPIR-V, but converting back to SM 5.1 DXBC fails on modern shader features. |

### Fundamental Hardware Barriers for Translation
Even if an intermediate compiler toolchain (`DXIL -> SPIR-V -> HLSL -> FXC -> DXBC`) were embedded into the DLL:
- **Wave Intrinsics / Quad Operations:** Kepler hardware (GK208) has no wave/subgroup execution hardware (WaveGetLaneIndex, WaveActiveBallot, etc.).
- **16-bit Float / Integer Arithmetic:** Kepler lacks native FP16/INT16 arithmetic units (emulated as 32-bit with performance penalty).
- **Bindless Resources (SM 6.6):** Unbounded descriptor arrays (`Texture2D textures[]`) exceed Kepler's Resource Binding Tier 2 hardware limits.
- **Mesh Shaders & Raytracing:** Kepler has no mesh/amplification shader hardware or RT cores.

### KeplerBridge Shader Architecture
Rather than fabricating fake success or substituting broken dummy shaders:
1. `ShaderPolicy` defines a clean `IShaderTranslator` interface for future modular translators.
2. By default (`EnableShaderFallback=0`), unsupported DXIL shaders are passed directly to the real driver, preserving native error codes (`E_FAIL` / `E_INVALIDARG`) without corrupting rendering state.
3. Native SM 5.0 and SM 5.1 shaders pass through untouched and execute natively at full performance.

---

## 4. Pipeline State Interception (`PSOHook`)

`WrappedD3D12Device` intercepts:
- `CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc, ...)`
- `CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc, ...)`

### Interception Workflow:
1. Inspects all attached shader stages (`VS`, `PS`, `DS`, `HS`, `GS`, `CS`).
2. Logs shader stage, container type, shader model, and bytecode size.
3. If `EnableShaderFallback=1` and a registered translator succeeds, swaps the bytecode with the compatible SM 5.x blob.
4. Otherwise, dispatches the original descriptor to `m_pReal->CreateGraphicsPipelineState` / `CreateComputePipelineState`.
5. Logs the resulting HRESULT and warns on native driver failure.

---

## 5. Texture & Resource Compatibility (`TextureClamper`)

`WrappedD3D12Device` intercepts resource creation:
- `CreateCommittedResource`
- `CreatePlacedResource`
- `CreateReservedResource`

### Architectural Rules & Safeguards:
1. **Never Clamp Render Targets or Depth Buffers:**
   Render targets (`ALLOW_RENDER_TARGET`) and depth buffers (`ALLOW_DEPTH_STENCIL`) are tightly coupled to the swapchain and viewport dimensions. Clamping them causes immediate viewport clipping, depth buffer mismatch, and crash.
2. **Preserve Subresource Row Pitch Alignment:**
   D3D12 requires 256-byte alignment (`D3D12_TEXTURE_DATA_PITCH_ALIGNMENT`) for texture copy and upload layouts.
3. **Hardware Limit Enforcement:**
   Validates texture dimensions against Direct3D 12 Feature Level 11_0 limits (Max 2D: 16384, Max 3D: 2048, Max Array: 2048).
4. **Safe Default:**
   `EnableTextureClamping=0` by default. Resources pass through unmodified unless explicitly enabled.
