# v4.0.0

<small>May 7, 2026 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v4.0.0)</small>

`GEMPBA_MULTIPROCESSING` is now a real build toggle: MPI is genuinely optional, and the public namespaces are reshaped so consumer code reads the same in both modes.

**Breaking changes**

- Public namespace renamed: `gempba::mp` → `gempba::multiprocessing`
- Public namespace renamed: `gempba::mt` → `gempba::multithreading`
- In a consumer build (`GEMPBA_DEV_MODE=OFF`), exactly one of `multithreading` / `multiprocessing` is `inline` — selected by `GEMPBA_MULTIPROCESSING`. Code that hard-codes the explicit qualifier still compiles, but mixing both qualifiers in one consumer build no longer works
- The `gempba::multiprocessing::*` facade (schedulers, MP factories, MP `create_node_manager`, `get_default_mpi_stats_visitor`, `runnables::*`, MP node creators) is now gated on `GEMPBA_MULTIPROCESSING=ON` — code that referenced these in an MT-only build will fail to compile
- `GEMPBA_BUILD_TESTS` now requires `GEMPBA_MULTIPROCESSING=ON` and `GEMPBA_DEV_MODE=ON`; the in-tree tests use the explicit `gempba::multiprocessing::*` form
- Example identifiers and filenames lost their `mp_` / `mt_` prefixes; multiprocessing examples moved to `examples/multiprocessing/` and multithreading examples to `examples/multithreading/`, each with its own `CMakeLists.txt`
- Shared `examples/include/benchmark.hpp` renamed to `examples/include/benchmark_io.hpp`

**Migration**

```cpp
// Before (v3.x)
auto* lb = gempba::mp::create_load_balancer(policy, worker);
auto& nm = gempba::mp::create_node_manager(lb, worker);

// After (v4.0): short form (mode picked by GEMPBA_MULTIPROCESSING)
auto* lb = gempba::create_load_balancer(policy, worker);
auto& nm = gempba::create_node_manager(lb, worker);
// Or the explicit form: gempba::multiprocessing::create_load_balancer(...)
```

**Added**

- Top-level `gempba::create_load_balancer(std::unique_ptr<load_balancer>)` — mode-agnostic BYO factory that works identically in MT and MP builds
- README "Selecting a mode" section documenting the build toggle, the short-form consumer API, and when to reach for the explicit qualifiers

**Changed**

- Shared MT/MP factory logic extracted into anonymous-namespace helpers in `src/gempba/gempba.cpp` so the two facades stay in lockstep
- Telemetry sources (`telemetry_hub`, `topology_builder`, `mpi_transport`) now gate MPI usage on `GEMPBA_MULTIPROCESSING`; `mpi_transport.cpp` is excluded from the build when the flag is off

**Build**

- `find_package(MPI REQUIRED)` and `MPI::MPI_CXX` linkage are now conditional on `GEMPBA_MULTIPROCESSING=ON` — MT-only builds no longer require an MPI installation
- `src/schedulers/mpi/**` and `src/telemetry/mpi_transport.cpp` are filtered out of the gempba library when `GEMPBA_MULTIPROCESSING=OFF`
- `GEMPBA_MULTIPROCESSING` is now defined exclusively through the generated `<gempba/config.h>` (no longer also passed via `target_compile_definitions`) to avoid redefinition hazards in consumer builds
- Per-example `GEMPBA_MULTIPROCESSING` overrides dropped; the `examples/multiprocessing` subdirectory is gated on the root flag instead
- Ubuntu, macOS, and Windows CI now exercise both `GEMPBA_MULTIPROCESSING=ON` and `=OFF`
- macOS self-hosted runners bootstrap Homebrew on `PATH` before configuring
