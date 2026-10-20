# KeplerBridge — Release Checklist

**Milestone:** Phase 10 / V1 Release Engineering  
**Target Candidate:** `v0.8.0-rc1`  
**Reference GPU:** NVIDIA GeForce GT 740 (GK208B, Native FL 11_0, Driver 456.71, Windows 10 x64)  
**Governance Policy:** All items must be verified and checked off before tagging or public release publication.

---

## 1. Source Code & Architecture Hygiene

- [x] **Zero Compiler Warnings:** Project builds cleanly under MinGW-w64 GCC 16.2.0 (UCRT64) with `-std=c++20 -O3`.
- [x] **Zero Third-Party Library Dependencies:** Codebase relies strictly on standard C++20 and native Windows SDK interfaces.
- [x] **COM Reference Integrity:** `AddRef()` and `Release()` are strictly balanced across all wrapped objects.
- [x] **COM Process Detach Protection:** No COM calls or interface releases occur under OS loader lock in `DLL_PROCESS_DETACH`.
- [x] **Zero Per-Draw Allocations:** No heap allocations on active command recording or replay hot paths.
- [x] **PII / Path Sanitization:** `Logger::SanitizePath()` redacts all user profile paths before writing to disk.
- [x] **Bounded Shader Policy:** `ShaderPolicy` honestly rejects unsupported SM 6.x DXIL bytecode without fabricating fake success.

---

## 2. Hardware Regression & Test Verification

- [x] **Full Suite Execution:** All 15 regression test executables compiled and executed against physical GT 740 GPU.
- [x] **111 / 111 Test Pass Rate:**
  - [x] `test_d3d11_standalone.exe` (2/2 Passed)
  - [x] `test_d3d12.exe` (1/1 Passed)
  - [x] `test_d3d12_compat.exe` (3/3 Passed)
  - [x] `test_d3d12_compute.exe` (12/12 Passed)
  - [x] `test_d3d12_multi_uav.exe` (12/12 Passed)
  - [x] `test_d3d12_rendering.exe` (10/10 Passed)
  - [x] `test_d3d12_translation.exe` (7/7 Passed)
  - [x] `test_dxgi_swapchain.exe` (9/9 Passed)
  - [x] `test_perf_profile.exe` (5/5 Passed)
  - [x] `test_policy_engine.exe` (7/7 Passed)
  - [x] `test_pso_interception.exe` (5/5 Passed)
  - [x] `test_shader_cache_persistence.exe` (5/5 Passed)
  - [x] `test_shader_compat.exe` (7/7 Passed)
  - [x] `test_shader_inspector.exe` (11/11 Passed)
  - [x] `test_translation_engine.exe` (17/17 Passed)
- [x] **Zero Regressions:** Pass rate remains at 100.0% compared to previous Phase 7 (107/107) and Phase 9 (108/108) baselines.

---

## 3. Packaging & Distribution Integrity

- [x] **Sealed Candidate Preserved:** `dist/keplerbridge-v0.8.0-rc1-x64.zip` preserved without overwrites.
- [x] **PE Import Audit:** `dist/d3d12.dll` verified to import strictly:
  - `d3d11.dll`
  - `dxgi.dll`
  - `KERNEL32.dll`
  - Universal CRT (`api-ms-win-crt-*`)
- [x] **Architecture Verification:** Verified PE32+ (x86-64 / AMD64).
- [x] **Checksum Integrity:** `dist/SHA256SUMS` verified byte-for-byte against actual packaged artifacts:
  - `d3d12.dll`: `042565a1e018a0444b04a9a7b85376ebb942d2646f47b18d5a8dea38157a5b6a`
  - `kepler_bridge.ini`: `1162c535c35cc04d7284621bab7d52aa620ba9984545423f235a9e900e3fc054`
  - `LICENSE`: `2bb0f6c997569474e8d16e264f7a133218a683e68f1c62326204985aa89a93db`
  - `README.txt`: `e7c99e0000cfaef8a515235953b92182b5b64760c44a50670b6ce2e29dc14ed9`
  - `keplerbridge-v0.8.0-rc1-x64.zip`: `383be1c12d945d0c01368174ac63947f153080a12e8e261dc152acd181e23007`

---

## 4. Documentation & Non-Fabrication Standards

- [x] **`README.md` Audit:** All 16 required sections present; accurate 111/111 test count; clear Release Candidate status; Palworld experimental status documented.
- [x] **No Fabricated Playability:** Zero commercial games labeled as VERIFIED PLAYABLE without full in-game 5-Pillar acceptance testing.
- [x] **Native D3D11 Segregated:** Native D3D11 runs (*God of War*, *Baldur's Gate 3*, *Days Gone*, *Palworld -dx11*) explicitly classified as hardware baselines, never translation successes.
- [x] **Hardware Caveats Explicit:** Technical impossibility of DXR, Mesh Shaders, VRS, and Tier 3 bindless explicitly documented.
- [x] **`CHANGELOG.md` Updated:** Documented Phase 9 and Phase 10 additions under `[0.8.0-rc1]`.
- [x] **`SECURITY.md` & `CONTRIBUTING.md` Present:** Offline policy and anti-cheat advisory prominently featured.

---

## 5. Governance & Publication Gate

- [x] **No Git Commits Executed:** Working tree modifications kept uncommitted in local workspace.
- [x] **No Git Tags Created:** Remote and local tags unchanged.
- [x] **No Remote Branch Modifications:** Git remote untouched.
- [x] **No Public Release Published:** Release draft prepared in `docs/GITHUB_RELEASE_DRAFT.md`, awaiting user sign-off.
- [x] **Release-Readiness Sign-Off:** Signed off by Maintainer / QA Lead.
