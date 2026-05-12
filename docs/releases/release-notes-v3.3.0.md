# v3.3.0

<small>May 6, 2026 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v3.3.0)</small>

Telemetry is now always built and switched at runtime. The kill switch is reachable through `<gempba/gempba.hpp>` and the runtime-mode taxonomy is simplified.

**Breaking changes**

- `GEMPBA_TELEMETRY` CMake option **removed** — telemetry is unconditionally compiled into `libgempba`. Existing `-DGEMPBA_TELEMETRY=ON|OFF` invocations and the `GEMPBA_TELEMETRY` preprocessor macro no longer exist; gate behavior at runtime via the new disable/enable API instead
- `gempba::telemetry::runtime_mode::MPI` renamed to `runtime_mode::MP_MPI` — any consumer that names the enumerator directly must be updated
- `gempba::telemetry::is_disabled()` renamed to `gempba::telemetry::is_enabled()` (boolean meaning inverted) — match the `disable()`/`enable()` pair and avoid the double-negative read at the call site
- `gempba::telemetry::runtime_mode::OTHER_IPC` removed (had no transport behind it and no production callers)
- `scripts/build_linux.sh` and `scripts/build_windows.sh` no longer accept the `[TELEMETRY]` positional argument; the second positional argument is now `[HWLOC]`

**Added**

- Runtime telemetry kill switch — exposed through `<gempba/gempba.hpp>` so consumers don't have to include the telemetry headers directly:
  - `gempba::telemetry::disable()` — uninstalls any current hub and blocks future `install()` calls (including the implicit auto-install performed by `multiprocessing::create_scheduler` and `multithreading::create_node_manager`)
  - `gempba::telemetry::enable()` — clears the disabled flag so `install()` works again
  - `gempba::telemetry::is_enabled()` — observe the current state
- `disable()` / `enable()` are idempotent and safe to call before or after the hub is installed
- README documents the MPI-symmetry contract on telemetry toggles inline next to the API

**Changed**

- MPI-symmetry contract on telemetry toggles: telemetry is always linked in and `disable()` simply prevents `install()` from taking effect, so any `disable()` / `enable()` call must be issued on every rank symmetrically. Disabling on a subset of ranks would leave some ranks expecting MPI telemetry traffic from peers that no longer send any, deadlocking on the absent transport
- Telemetry auto-install in `multiprocessing::create_scheduler` / `multithreading::create_node_manager` / `gempba::shutdown` is no longer guarded by `#if GEMPBA_TELEMETRY`; the same code path runs in every build and short-circuits when telemetry is disabled

**Build**

- `libgempba` no longer varies by telemetry flag — a single artifact ships with telemetry compiled in, so packagers (`.deb`, MSYS2) no longer need a telemetry/non-telemetry matrix
