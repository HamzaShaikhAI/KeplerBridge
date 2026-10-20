# KeplerBridge Phase 8 — Release Candidate Checklist (v0.8.0-RC1)

**Milestone:** Phase 8  
**Document:** Pre-Publication Release Checklist & Verification Audit  
**Date:** October 20, 2026  
**Status:** ALL GATES PASSED (Locally Validated)  
**Release Candidate:** `v0.8.0-rc1`  

---

## 1. Build and Toolchain Integrity

- [x] **Compiler & Standard:** Built with MinGW-w64 GCC 16.2.0 (UCRT64) targeting `-std=c++20`.
- [x] **Build System:** Clean build generated via CMake 4.4.3 and Ninja 1.13.2.
- [x] **Static Linkage:** C/C++ runtimes statically linked (`-static -static-libgcc -static-libstdc++`).
- [x] **Zero Compiler Warnings:** Core translation headers and translation engine compile cleanly.
- [x] **Non-Destructive Recovery Point:** Git branch `backup-phase7-baseline` established prior to Phase 8 work.

---

## 2. Regression Test Suite Coverage (108 / 108 Passed)

Executed against physical NVIDIA GeForce GT 740 hardware (GK208, 4052 MB DDR3, Driver 456.71):

- [x] `test_d3d11_standalone.exe` (2/2 Passed, Exit Code: 0) — Physical device creation & adapter binding
- [x] `test_d3d12.exe` (1/1 Passed, Exit Code: 0) — Proxy loader ghost hook interception
- [x] `test_d3d12_compat.exe` (3/3 Passed, Exit Code: 0) — Feature level probing & spoofing policy
- [x] `test_d3d12_compute.exe` (12/12 Passed, Exit Code: 0) — Compute dispatch & GPU readback proof
- [x] `test_d3d12_multi_uav.exe` (13/13 Passed, Exit Code: 0) — Multi-slot UAV binding & counter atomic logic
- [x] `test_d3d12_rendering.exe` (11/11 Passed, Exit Code: 0) — Graphics pipeline rendering & texturing
- [x] `test_d3d12_translation.exe` (7/7 Passed, Exit Code: 0) — Command recording & replay engine
- [x] `test_dxgi_swapchain.exe` (6/6 Passed, Exit Code: 0) — DXGI swapchain wrapping & present pacing
- [x] `test_perf_profile.exe` (4/4 Passed, Exit Code: 0) — Microbenchmarks & memory footprint scaling
- [x] `test_policy_engine.exe` (6/6 Passed, Exit Code: 0) — Compatibility rules & clamping
- [x] `test_pso_interception.exe` (5/5 Passed, Exit Code: 0) — PSO caching & interception
- [x] `test_shader_cache_persistence.exe` (5/5 Passed, Exit Code: 0) — Multi-process cache bypass & corruption recovery
- [x] `test_shader_compat.exe` (7/7 Passed, Exit Code: 0) — Shader policy & DXIL rejection
- [x] `test_shader_inspector.exe` (11/11 Passed, Exit Code: 0) — DXBC container parsing
- [x] `test_translation_engine.exe` (15/15 Passed, Exit Code: 0) — Root constants, barrier hazards, and initial PSO state recording

---

## 3. Portable Executable (PE) & Dependency Verification

- [x] **Direct Windows Imports Only:** `d3d12.dll` directly binds only to `d3d11.dll`, `dxgi.dll`, `KERNEL32.dll`, and Windows UCRT (`api-ms-win-crt-*.dll`).
- [x] **Dynamic System Loads:** Dynamically loads `d3dcompiler_47.dll` and `C:\Windows\System32\d3d12.dll`.
- [x] **Zero Third-Party Dependencies:** No external third-party DLLs (no Vulkan runtime, no SPIRV-Cross, no vkd3d-proton).
- [x] **Canonical D3D12 Symbols:** All 16 standard Direct3D 12 export entry points verified present via `objdump -p`.
- [x] **Binary Stripping:** Production release binary stripped of debug symbols, reducing size from 3.88 MB to 1.69 MB.

---

## 4. Release Packaging & Verification (`dist/`)

- [x] `dist/d3d12.dll` — Stripped production binary (1,695,758 bytes)
- [x] `dist/kepler_bridge.ini` — Documented configuration profile with sane defaults
- [x] `dist/README.txt` — Plaintext quick-start and hardware advisory
- [x] `dist/LICENSE` — MIT License text
- [x] `dist/SHA256SUMS` — Cryptographic SHA-256 hashes for all files:
  * `d3d12.dll`: `042565a1e018a0444b04a9a7b85376ebb942d2646f47b18d5a8dea38157a5b6a`
  * `kepler_bridge.ini`: `1162c535c35cc04d7284621bab7d52aa620ba9984545423f235a9e900e3fc054`
  * `LICENSE`: `2bb0f6c997569474e8d16e264f7a133218a683e68f1c62326204985aa89a93db`
  * `README.txt`: `e7c99e0000cfaef8a515235953b92182b5b64760c44a50670b6ce2e29dc14ed9`
- [x] `dist/keplerbridge-v0.8.0-rc1-x64.zip` — Complete self-contained release archive (630,489 bytes)
  * SHA-256: `383be1c12d945d0c01368174ac63947f153080a12e8e261dc152acd181e23007`

---

## 5. Architectural Scoping & Compatibility

- [x] **90 Commercial Games Evaluated:** Categorized under strict 8-state evidence taxonomy across Groups A, B, and C (`docs/PHASE8_GAME_COMPATIBILITY_MATRIX.md`).
- [x] **Silicon Boundaries Enforced:** Absolute boundaries documented for DXR, Mesh Shaders (SM 6.5), VRS, DirectStorage, and Tier 3 bindless.
- [x] **Memory Bandwidth Realism:** Documented 14.4 GB/s DDR3 bandwidth ceiling on Kepler GK208.
- [x] **Native D3D11 Fallback Documented:** Recommended `-dx11` command-line switch for dual-renderer games (*Palworld*, *The Witcher 3*, *Control*).

---

## 6. Documentation and Community Governance

- [x] `README.md` — Publication-grade overview, Mermaid architecture diagram, compatibility table, installation guide.
- [x] `CHANGELOG.md` — Full release notes from Phase 1 through Phase 8 following Keep a Changelog.
- [x] `CONTRIBUTING.md` — Contributor guidelines, coding standards, test requirements.
- [x] `SECURITY.md` — Vulnerability disclosure policy and anti-cheat advisory.
- [x] `LICENSE` — MIT License.
- [x] `.github/ISSUE_TEMPLATE/` — Bug report, compatibility report, and feature request templates.

---

## 7. Production Diagnostics & Privacy

- [x] **Stable Error Codes:** `KB_ERR_*` enumeration covering all runtime subsystems.
- [x] **Log File Rotation:** 10 MB active log file limit with automated rotation to `.old`.
- [x] **Privacy Redaction:** Regex path sanitization masking user directory names (`Users\[USER]\`).
- [x] **Session Summary:** Clean capability detection banner on startup and diagnostic summary on process exit.

---

## 8. Release Gate Decision

| Release Gate | Verification Status | Notes |
| :--- | :---: | :--- |
| **Toolchain & Build** | **APPROVED** | GCC 16.2.0 UCRT64, CMake 4.4.3, Ninja 1.13.2 |
| **Regression Tests** | **APPROVED** | 108/108 passed across 15 executables on GT 740 |
| **Packaging & Hashes**| **APPROVED** | All distribution files verified in `dist/` |
| **Documentation** | **APPROVED** | Complete publication-grade documentation suite |
| **Mandatory Hold** | **HOLD** | **STOPPING BEFORE REMOTE PUBLICATION (Awaiting User Approval)** |
