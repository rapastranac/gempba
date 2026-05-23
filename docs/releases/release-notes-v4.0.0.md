# v4.0.0

<small>May 23, 2026 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v4.0.0)</small>

This is the release where gempba becomes a real distributed library you install instead of clone. Pick your flavor (multithreading or MPI), pick your platform (Linux, macOS, or Windows), and `apt install` / `pacman -S` / `brew install` your way to a working build. Telemetry, which landed in v3.3.0, ships in every flavor, so your production runs are observable out of the box without any wiring.

## What's new

- **Packages on every platform.** `.deb` on Debian/Ubuntu, MSYS2 packages on Windows, and a brand-new Homebrew tap on macOS. Each platform ships two coexisting flavors: a default multithreading build and an MPI build that you install on top when you need it.
- **Telemetry is in the box.** The v3.3.0 telemetry hub (worker / node frames, hwloc topology, local + TCP + MPI transports) is built into every published flavor. No extra dependency to add, no extra flag to flip.
- **Same call site, both modes.** Public namespaces were reshaped. Consumer code now reads the same whether you build against the multithreading or the MPI flavor: `gempba::create_load_balancer(...)`, `gempba::create_node_manager(...)`. Mode is picked at `find_package` time, not at every call site.
- **Examples moved out.** `examples/` left the source tree for a sibling repo, [rapastranac/gempba-examples](https://github.com/rapastranac/gempba-examples), where they consume gempba via `find_package` exactly like you would. Every example PR exercises the public API.

## Breaking changes

- Public namespaces renamed: `gempba::mp` is now `gempba::multiprocessing`, `gempba::mt` is now `gempba::multithreading`.
- In a consumer build, exactly one of the two is `inline`, selected by `GEMPBA_MULTIPROCESSING`. Code that hard-codes the explicit qualifier still compiles, but mixing both qualifiers in one consumer build no longer works.
- The `gempba::multiprocessing::*` facade (schedulers, MP factories, MP `create_node_manager`, `get_default_mpi_stats_visitor`, `runnables::*`, MP node creators) is gated on `GEMPBA_MULTIPROCESSING=ON`. Code that referenced these in an MT-only build will fail to compile.
- `apt install libgempba-dev` previously gave you an MPI-enabled build. It now gives you multithreading only. MPI consumers additionally `apt install libgempba-mpi-dev`.
- `pacman -S mingw-w64-x86_64-gempba` previously gave you an MPI-enabled build. Same shape: MPI consumers additionally `pacman -S mingw-w64-x86_64-gempba-mpi`.
- The in-tree `examples/` tree is gone. The migrated tree now lives in [rapastranac/gempba-examples](https://github.com/rapastranac/gempba-examples).

## Migration

```cpp
// Before (v3.x)
auto* lb = gempba::mp::create_load_balancer(policy, worker);
auto& nm = gempba::mp::create_node_manager(lb, worker);

// After (v4.0): short form, mode picked by GEMPBA_MULTIPROCESSING
auto* lb = gempba::create_load_balancer(policy, worker);
auto& nm = gempba::create_node_manager(lb, worker);
// (or the explicit form: gempba::multiprocessing::create_load_balancer(...))
```

```cmake
# v4.0 consumer CMake
find_package(gempba REQUIRED)                  # default: mt
find_package(gempba REQUIRED COMPONENTS mt)    # explicit mt
find_package(gempba REQUIRED COMPONENTS mpi)   # mpi (requires libgempba-mpi-dev installed)
target_link_libraries(my_app PRIVATE gempba::gempba)
```

The two flavors are mutually exclusive within a single binary. They share mode-agnostic top-level symbols and would ODR-clash, so `find_package(gempba COMPONENTS mt mpi)` is rejected up front with a clear diagnostic. A project that genuinely needs both (say, an MT debug runner and an MPI cluster runner) splits into two executables, each `find_package`-ing one.

## Added

- Top-level `gempba::create_load_balancer(std::unique_ptr<load_balancer>)`. Mode-agnostic BYO factory that works identically in MT and MP builds.
- `gempbaConfig.cmake` is now a COMPONENTS-aware dispatcher. Defaults to `mt`, refuses the `mt`+`mpi` combination, pulls `find_dependency(MPI)` only on the `mpi` branch.
- Two `.deb` packages: `libgempba-dev` (mt base, ships headers and `gempbaConfig.cmake`) and `libgempba-mpi-dev` (mpi topping, `Depends:` the base plus `libopenmpi-dev`). Installable side-by-side without conflict.
- Two MSYS2 packages: `mingw-w64-x86_64-gempba` and `mingw-w64-x86_64-gempba-mpi`. Same shape, same dependency direction.
- macOS Homebrew tap: `brew tap <owner>/gempba && brew install gempba` (or `gempba-mpi`).
- [rapastranac/gempba-examples](https://github.com/rapastranac/gempba-examples) sister repo carrying the migrated example tree. Consumes gempba via `find_package(gempba)` exactly as a downstream user would.
- README sections "Installing" (apt / pacman / brew per flavor) and "Selecting a flavor" (the `COMPONENTS` API, the mutual-exclusion guard, the two-executables pattern).

## Build

- `find_package(MPI REQUIRED)` and `MPI::MPI_CXX` linkage are conditional on `GEMPBA_MULTIPROCESSING=ON`. MT-only builds no longer require an MPI installation.
- Installed library output name is flavor-tagged: `libgempba.a` for mt, `libgempba_mpi.a` for mpi. Both flavors export the same imported target name `gempba::gempba`, so your link line never changes between modes.
- Per-flavor `pkg-config` file: `gempba.pc` for mt, `gempba-mpi.pc` for mpi.
