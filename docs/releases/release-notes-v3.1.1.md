# v3.1.1

<small>May 2, 2026 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v3.1.1)</small>

Post-v3.1.0 hygiene release: packaging metadata, coverage scope, CI workflow polish, a test-coverage push, cross-platform fixes, and toolchain bumps. No public API changes.

**Changed**

- `gempba::create_custom_node` now takes a `std::shared_ptr<node_core>` directly (no more wrapping at the call site)
- `wall_time` reimplemented on top of `std::chrono` (replaces `gettimeofday`) — same observable values, monotonic clock semantics
- `gempba::log_and_throw` marked `[[noreturn]]` so static analyzers and the compiler's flow analysis understand the call never returns
- `score`: cleaned up the `kind` / `to_raw` / `from_raw` paths and added explicit `unreachable` markers
- `node_core_impl`: lazy-init and result paths consolidated; redundant explicit destructor on `node` removed; excessive debug logging trimmed
- Codebase-wide `clang-tidy` identifier-naming pass (private members and locals only — no public API renames)

**Removed**

- Dead `prune()` overload in `work_stealing_load_balancer`
- Unreachable defensive checks in `get_root_level_pending_node`

**Fixed**

- `gempba::utils::get_nb_set_bits` signed-`char` overflow on MinGW
- MSYS2/MinGW build: `<windows.h>` is now included before `<psapi.h>` so `psapi.h` sees the types it needs
- `centralized_utils.hpp`: `NOMINMAX` redefinition guarded so headers that already define it don't trigger a warning
- `node.hpp`: `<stacktrace>` inclusion gated on the `__cpp_lib_stacktrace` feature test, not just `__has_include`

**Build**

- MSYS2 `PKGBUILD` `sha256` updated to match the v3.1.0 source tarball
- Codecov reporting now excludes `examples/`, `tests/`, and `external/` so coverage numbers reflect only library code
- New `clang-tidy` and `clang-format` identifier-naming and configuration rules so future changes can't reintroduce the patterns just cleaned up
- Ubuntu CI bumped to GCC 14; `<stacktrace>` and `stdc++exp` linkage gated accordingly so older toolchains still build the library
- macOS CI defaults to the `macos-26` runner image
- Self-hosted runners are now matched by label *set* (multiple labels combined) rather than a single label
- MS-MPI runtime installed on the Windows CI runner so the multiprocessing test set actually runs
- `clang-format` diagnostics surfaced with the colored summary; `lint.sh` diagnostics made readable on the terminal
- Skip the `publish-test-results` job when the upstream build was cancelled
- `runs-on` injected from `vars.RUNNER_*` repository variables so runner targets can change without editing each workflow
- New full-coverage tests for the `gempba` facade, `node_manager`, `quasi_horizontal_load_balancer`, `work_stealing_load_balancer`, `node_core_impl`, `centralized_utils`, plus branch coverage for `queue`, `utils`, `score`, and `node`
- Thread pool readiness ordering tightened in throw/discard tests so the `send()` race no longer deflakes them
- Dropped flaky peak-vs-current RSS comparisons from the memory-usage tests
- `-Wunused` warnings silenced on header-only helpers
