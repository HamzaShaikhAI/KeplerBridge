# Contributing to KeplerBridge

Thank you for your interest in contributing to KeplerBridge! We welcome issues, bug reports, and pull requests from the graphics development and hardware preservation communities.

To maintain architectural integrity, determinism, and compatibility with physical Kepler hardware, all contributions must adhere to the following standards.

---

## 1. Architectural Invariants (Non-Negotiable)

1. **Zero Third-Party Application Dependencies:**
   KeplerBridge must remain a self-contained, statically linked binary. Pull requests introducing external dependencies (such as Vulkan runtimes, SPIRV-Cross, vkd3d-proton, LLVM/DXC, or third-party utility libraries) will not be accepted.
2. **Honest Architectural Boundaries:**
   Never claim arbitrary DXIL-to-DXBC translation, fake Raytracing support, or synthesized Mesh Shaders. Unsupported Direct3D 12 features must be handled through honest policy rejection or graceful fallbacks.
3. **Hardware Ground Truth:**
   All translation paths, resource limits, and optimizations must remain strictly compatible with physical **Feature Level 11_0** silicon (specifically the NVIDIA Kepler GK208 target architecture).
4. **No Anti-Cheat Circumvention:**
   KeplerBridge is designed exclusively for offline single-player applications and hardware preservation. Code attempting to hide from, disable, or tamper with anti-cheat software (EAC, BattlEye, Vanguard, etc.) is strictly prohibited.

---

## 2. Development Environment & Toolchain

Contributions should be built and validated using the established project toolchain:
* **Operating System:** Windows 10 x64 (Version 19041+) or Windows 11 x64
* **Compiler:** MinGW-w64 GCC with C++20 standard support (GCC 13+ or UCRT64 GCC 16.2.0 recommended)
* **Build System:** CMake 3.20+ with Ninja
* **Language Standard:** Modern C++20 (`-std=c++20`)

### Building the Project
```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
```

---

## 3. Code Quality & Engineering Standards

* **C++20 & Modern Practices:** Prefer RAII, smart pointers (`std::unique_ptr`), and `std::string_view` where applicable. Avoid raw pointers for resource ownership.
* **COM Lifecycle Safety:** COM reference counting (`AddRef`/`Release`) must be handled carefully. Never invoke virtual COM methods or `Release()` during static destruction or process detach (`DLL_PROCESS_DETACH`).
* **Zero Per-Draw Allocations:** Hot paths—including command recording, constant buffer updates, and descriptor translation—must not perform heap allocations (`new`, `malloc`, `std::vector::resize`). Utilize persistent scratch buffers and object pooling.
* **Structured Diagnostics:** Errors in core translation paths must be reported via `Logger::ReportDiagnostic()` with an appropriate `KeplerErrorCode`, severity, and actionable hint.
* **Path & PII Privacy:** Never log raw user home directories or sensitive personal paths. Always pass file paths through `Logger::SanitizePath()`.

---

## 4. Testing & Verification Requirements

Every pull request must pass the entire 15-executable regression suite without regressions, memory leaks, or execution hangs:

```powershell
Get-ChildItem -Path build -Filter "test_*.exe" | ForEach-Object {
    $proc = Start-Process -FilePath $_.FullName -Wait -PassThru -NoNewWindow
    if ($proc.ExitCode -ne 0) {
        Write-Error "Test failed: $($_.Name)"
        exit 1
    }
}
```

If your contribution adds a new feature or fixes a bug, you must include corresponding test cases verifying both positive and negative/boundary conditions.

---

## 5. Pull Request Guidelines

1. Fork the repository and create a feature branch (`git checkout -b feature/my-enhancement`).
2. Keep commits atomic, descriptive, and well-documented.
3. Reference any relevant GitHub issues or bug reports in your commit message and PR description.
4. Run the full test suite and include the test output in your PR description.
5. Ensure documentation (`README.md`, `CHANGELOG.md`, relevant `docs/*.md`) is updated to reflect your changes.
