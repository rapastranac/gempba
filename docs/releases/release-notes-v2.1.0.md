# v2.1.0

<small>August 23, 2025 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v2.1.0)</small>

Source-level Windows support, alongside a dedicated Windows CI pipeline.

**Added**

- Windows build support: GemPBA now compiles on Windows with MSVC
- `run.ps1` PowerShell launcher at the repo root for running multiprocessing (`mpiexec.exe`) and multithreading example binaries on Windows
- Windows CI workflow (`.github/workflows/c-cpp-windows.yml`) building on Windows Server 2022, with a matching status badge in the README

**Changed**

- `centralized_utils.hpp` now defines `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, and `RPC_NO_WINDOWS_H` before including `windows.h` to avoid `std::byte` ambiguity and `min`/`max` macro clashes against `<windows.h>`
- `score::make` factory dispatches by integral size (`int32_t`/`int64_t`) instead of exact type, so scores constructed from `long` and other platform-dependent integer widths behave the same on Windows and Linux
- Ubuntu workflow renamed from `c-cpp.yml` to `c-cpp-ubuntu.yml` (Ubuntu 24.04); README badge updated accordingly
- `args_handler.hpp` switched to angle-bracket includes and replaced `std::forward<nullptr_t>(nullptr)` with a plain `nullptr` argument for portability

**Fixed**

- Corrected the closing namespace comment in `args_handler.hpp` (`} // namespace gempba`)

**Build**

- Test executable renamed from `all_tests.out` to `all_tests` (drops the Linux-style suffix so the same target name works on Windows)
