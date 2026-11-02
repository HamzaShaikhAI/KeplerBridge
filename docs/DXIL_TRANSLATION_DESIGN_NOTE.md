# KeplerBridge Architecture Note: DXIL Translation and Lowering Design

**Document ID**: KB-ARCH-2026-DXIL-01  
**Target Architecture**: Direct3D 12 (SM 6.0+) to Direct3D 11 (SM 5.0)  
**Reference Hardware**: NVIDIA GeForce GT 740 GK107 (4 GB DDR3, Feature Level 11_0)  
**Status**: Architecture & Design Specification  

---

## 1. Executive Summary & Forensic Retrospective

During the controlled forensic investigation of *Age of Mythology: Retold* (AoMRT), KeplerBridge extracted and analyzed **2,964 distinct shader binaries** embedded across the game's runtime packages. The empirical audit established:

1. **Predominance of Shader Model 6.x (DXIL)**: Over 98% of the game's shader suite is compiled as DirectX Intermediate Language (DXIL, container tag `DXIL`), primarily targeting **Shader Model 6.2 and Shader Model 6.5**.
2. **Advanced Hardware Dependencies**:
   - **Wave Intrinsics (`WaveActiveOp`, `WavePrefixCountBits`, `QuadReadAcross`)**: Heavily utilized in terrain tessellation, particle simulation, and post-process passes.
   - **64-bit Integer Operations (`i64` math and 64-bit atomic operations)**: Used in persistent scene graph indexing and terrain LOD hierarchies.
   - **Dynamic Descriptor Indexing (SM 6.6 style bindless tables)**: Direct indexing into unbounded descriptor heaps.
3. **Explicit Scope Disclaimer**:
   > [!IMPORTANT]
   > Implementing an initial Shader Model 6.0 subset translation engine **does not** generally translate *Age of Mythology: Retold*'s shaders. Kepler GK107 hardware physically lacks native wave warp shuffle instructions, hardware 64-bit integer units, and bindless tier 3 descriptor heaps. KeplerBridge must maintain authentic capability filtering and deterministic rejection rather than attempting unsafe emulation that risks driver hangs.

---

## 2. Five-Stage DXIL-to-DXBC Lowering Pipeline

To safely handle Direct3D 12 applications utilizing Shader Model 6.0 while preventing driver faults, KeplerBridge defines a rigorous 5-stage translation pipeline:

```
+-----------------------------------------------------------------------------------+
|                           Stage 1: Container Parsing                              |
|   Extract DXBC chunk header, validate 'DXIL' fourcc, extract bitcode & signatures |
+-----------------------------------------+-----------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
|                        Stage 2: LLVM Bitcode Decoding                             |
|   Decode LLVM 3.7 IR bitstream, parse symbol tables, types, values, and metadata  |
+-----------------------------------------+-----------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
|                   Stage 3: Semantic Lowering & Capability Gate                   |
|   Filter unsupported intrinsics (Wave/64-bit/Atomics); map SM 6.0 math to SM 5.0  |
+-----------------------------------------+-----------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
|                     Stage 4: Signature Remapping & I/O Packing                    |
|   Reconstruct ISGN/OSGN signatures, pack register semantics (v# -> r#, o# -> r#)   |
+-----------------------------------------+-----------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
|                     Stage 5: DXBC Checksum & Emission                             |
|   Emit DXBC container chunks (SHEX, ISGN, OSGN, RDEF) and calculate authentic MD5 |
+-----------------------------------------------------------------------------------+
```

### Stage 1: Container Parsing (`DXBC` / `DXIL` Chunks)
- Inspect the four-character code (FourCC). Standard DXIL containers use the outer `DXBC` wrapper containing internal `DXIL`, `ISGN`, `OSGN`, `PSV0`, and `RDAT` chunks.
- Extract the raw bitcode byte offset and length from the `DXIL` chunk.
- Verify container size and header offsets to guard against malformed or truncated inputs.

### Stage 2: LLVM Bitcode Decoding
- DXIL is an encapsulation of LLVM 3.7 bitcode.
- Parse the bitstream blocks: `BLOCKINFO`, `MODULE_BLOCK`, `PARAMATTR_BLOCK`, `TYPE_BLOCK`, `CONSTANTS_BLOCK`, `FUNCTION_BLOCK`, and `METADATA_BLOCK`.
- Reconstruct the Abstract Syntax Tree (AST) representing functions, basic blocks, SSA values, and DXIL intrinsic calls (`dx.op.*`).

### Stage 3: Semantic Lowering & Capability Filtering
- **Supported Mapping**: Arithmetic operations (`fadd`, `fmul`, `fmad`), vector construction, texture sampling (`dx.op.sample.*` without bindless indexing), and constant buffer loads (`dx.op.cbufferLoadLegacy`).
- **Capability Gate**: Immediately reject any shader invoking operations outside the FL 11_0 / SM 5.0 execution envelope:
  - Wave Intrinsics (`dx.op.waveVote`, `dx.op.waveOp`, `dx.op.quadOp`)
  - 64-bit integer arithmetic and 64-bit UAV atomic operations
  - Ray tracing intrinsics (`dx.op.ray*`)
  - Sampler feedback and mesh/amplification shader stages

### Stage 4: Signature Remapping & Register Packing
- SM 5.0 execution requires explicit hardware registers (`v[0..15]` inputs, `o[0..7]` render targets, `cb[0..13]` constant buffers).
- Reconcile DXIL parameter semantics against the compiled `ISGN` (Input Signature) and `OSGN` (Output Signature) chunks.
- Re-align component masks (`xyzw`) and ensure system-value semantics (`SV_Position`, `SV_Target`, `SV_IsFrontFace`) conform to Direct3D 11 expectations.

### Stage 5: Authentic DXBC Emission & Checksum Recalculation
- Assemble the lowered token stream into a valid `SHEX` / `SHDR` bytecode chunk.
- Compute the authentic Microsoft DXBC checksum using the reverse-engineered MD5 variant implemented in `src/dxbc_checksum.cpp`.
- Deliver the binary to `D3D11Backend::CreateVertexShader`, `CreatePixelShader`, or `CreateComputeShader`.

---

## 3. Comparison of Architectural Approaches

We evaluate three potential architectural paths for DXIL translation:

| Criterion | Option A: In-House Bounded Bitcode Lowering | Option B: Microsoft DXC (`dxil.dll` / `dxcompiler.dll`) | Option C: Mesa `dxil-spirv` + `SPIRV-Cross` |
| :--- | :--- | :--- | :--- |
| **Dependencies** | **Zero external dependencies** (integrated directly into `d3d12.dll`) | Heavy (`dxcompiler.dll`, `dxil.dll`, MSVC runtimes) | External libraries (`dxil-spirv`, `spirv-cross`, `vulkan-headers`) |
| **Footprint** | Extremely compact (< 200 KB code additions) | Very large (> 35 MB bundled DLLs) | Moderate (~ 4 MB compiled static libs) |
| **SM 6.0 Basic Support** | High (focused strictly on bounded SM 6.0 compute/pixel subset) | N/A (DXC does **not** support DXIL-to-DXBC lowering; only HLSL-to-DXIL) | Complete (full LLVM IR to SPIR-V translation) |
| **Wave / 64-bit Emulation** | Deterministic explicit rejection (prevents driver crashes) | None | Attempts software polyfills (often fails on Kepler FL 11_0) |
| **Maintenance Burden** | Moderate (custom bitcode decoder maintenance) | High (dealing with Microsoft licensing, runtime hooking) | High (keeping up with Mesa releases, SPIR-V dialect mismatches) |
| **Architectural Fit** | **Recommended for KeplerBridge Phase 12** | Infeasible (architecturally inappropriate) | Deferred for future non-Kepler projects |

### Rationale
- **Option B is rejected**: Microsoft designed DXC as a forward-compiler (HLSL -> DXIL). It has no built-in facility to decompile or lower DXIL back down to Direct3D 11 DXBC bytecode.
- **Option C is deferred**: While powerful, pulling Mesa and SPIR-V translation infrastructure into a lightweight proxy wrapper introduces immense build complexity and does not solve the fundamental physical hardware absence of Wave Intrinsics on Kepler GK107.
- **Option A is selected**: A lightweight, bounded in-house bitcode decoder that identifies SM 6.0 features, translates basic shaders, and strictly rejects unsupported constructs.

---

## 4. Semantic Testing & Explicit Rejection Architecture

To ensure system stability and eliminate GPU hangs or TDRs, KeplerBridge strictly adheres to the following failure handling contract:

### 1. Deterministic Rejection
When an unsupported DXIL feature is encountered (e.g., Wave Intrinsics or 64-bit integer ops):
- The shader translator **MUST NOT** substitute dummy NOPs or pass partial bytecode to the Direct3D 11 driver.
- It **MUST** return `DXGI_ERROR_UNSUPPORTED` (0x887A0004) or `E_INVALIDARG`.
- It **MUST** log a structured diagnostic report via `LOG_DIAGNOSTIC_REPORT`.

### 2. Diagnostic Telemetry Schema
```cpp
DiagnosticReport rep;
rep.errorCode = KeplerErrorCode::UnsupportedShaderInstruction;
rep.severity = ErrorSeverity::Error;
rep.subsystem = "DXILLoweringEngine";
rep.apiOperation = "TranslateDXILShader";
rep.nativeHResult = DXGI_ERROR_UNSUPPORTED;
rep.shaderProfile = "cs_6_5";
rep.disposition = ExecutionDisposition::Rejected;
rep.explanation = "Shader uses unsupported DXIL Wave Intrinsics (dx.op.waveActiveOp). Kepler FL 11_0 hardware lacks warp-level communication.";
rep.actionableHint = "Game requires native Shader Model 6.0+ wave execution support not physically present on GK107.";
LOG_DIAGNOSTIC_REPORT(rep);
```

### 3. Pipeline State Object (PSO) Failure Propagation
When a shader within a `D3D12_GRAPHICS_PIPELINE_STATE_DESC` or `D3D12_COMPUTE_PIPELINE_STATE_DESC` is rejected:
- `CreateGraphicsPipelineState` / `CreateComputePipelineState` returns `E_INVALIDARG`.
- The application receives clean notification of pipeline failure, allowing graceful software fallback or informative error presentation rather than a whole-machine BugCheck 0x116 restart.

---

## 5. Engineering Roadmap & Milestones

1. **Phase 12A — DXIL Container & Header Inspector**: Implement header parsing to extract `DXIL` chunk offsets and validate LLVM bitcode magic (`0xDEB01C04`).
2. **Phase 12B — Capability Rejection Gate**: Verify shader model version (`6.0` through `6.7`) and fast-reject unsupported SM 6.2+ intrinsics with structured diagnostics.
3. **Phase 12C — Bounded SM 6.0 Math Lowering**: Implement LLVM bitcode parsing for basic compute shaders (arithmetic, buffer indexing, thread group shared memory).
4. **Phase 12D — Regression & Isolation Testing**: Author synthetic tests verifying both clean translation of standard SM 6.0 shaders and safe rejection of Wave Intrinsic shaders.
