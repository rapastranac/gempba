# v1.0.2

<small>June 7, 2025 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v1.0.2)</small>

Easier external integration: GemPBA is now a consumable CMake library with a `gempba::gempba` target, a hook to inject a custom initial process topology, and CPM-based dependency fetch.

**Added**

- `gempba::gempba` ALIAS target so consumers can `target_link_libraries(... gempba::gempba)`
- `SchedulerParent::set_custom_initial_topology(tree&&)` to inject a custom initial process topology
- New CMake options `GEMPBA_BUILD_TESTS` and `GEMPBA_BUILD_EXAMPLES` (default `OFF` when used as a subproject)
- New CMake options `GEMPBA_MULTIPROCESSING` and `GEMPBA_DEBUG_COMMENTS` to drive compile-time macros from the build
- `wall_time`, `diff_time`, `shift_left` utility functions in `utils/utils.hpp`
- `build_topology` extracted from `MPI_Scheduler` into `utils/utils.hpp`
- Public `tree` type at `include/utils/tree.hpp` (replaces `MPI_Modules/Tree.hpp`)
- README section documenting CPM-based integration into a downstream project
- Filename is now included in "file not found" exception messages

**Changed**

- GemPBA now builds as a CMake library; examples and tests are separate subprojects gated by the new options
- Public include roots are `${workspace}/include` and `${workspace}/GemPBA`, exported via `target_include_directories` (`BUILD_INTERFACE`)
- Macro renames: `MULTIPROCESSING_ENABLED` → `GEMPBA_MULTIPROCESSING`; `DEBUG_COMMENTS` → `GEMPBA_DEBUG_COMMENTS` (consumers must update guards and `target_compile_definitions`)
- `BranchHandler` API renamed to snake_case: `setLookupStrategy` → `set_lookup_strategy`, `setLoadBalancingStrategy` → `set_load_balancing_strategy`, `getLoadBalancingStrategy` → `get_load_balancing_strategy`
- Enum types renamed: `LookupStrategy` → `lookup_strategy`, `LoadBalancingStrategy` → `load_balancing_strategy` (enumerator names like `MAXIMISE`, `MINIMISE`, `QUASI_HORIZONTAL` are unchanged)
- `scheduler_parent.hpp` now includes `<mpi.h>` (was `mpi/mpi.h`); fixes builds on MPI distributions that don't expose the `mpi/` prefix
- `argparse` is now fetched via [CPM](https://github.com/cpm-cmake/CPM.cmake) instead of `FetchContent_Declare`
- Verbose `spdlog::info` calls in production code demoted to `spdlog::debug`
- Project version bumped to 1.0.2; release built against C++23 (`CMAKE_CXX_STANDARD 23`)
- Default build type is `Debug` when GemPBA is the root project, inherited from the parent otherwise
- README rewritten with shields.io markdown badges; licence and version badges fixed

**Removed**

- `Boost` (`system`, `serialization`, `fiber`) is no longer linked or required by the library target; consumers no longer need to provide it
- `GTest` is no longer required to build the library; it is now only needed when `GEMPBA_BUILD_TESTS=ON`
- Legacy headers `GemPBA/MPI_Modules/Tree.hpp`, `GemPBA/utils/Queue.hpp`, `GemPBA/utils/utils.hpp` (replaced by their counterparts under `include/utils/`)

**Fixed**

- Source-file detection in the root `CMakeLists.txt`
- Hard-coded `-O0` debug flags and `add_definitions(-DDEBUG_COMMENTS)` no longer leak into Release builds; flags are now per-configuration via generator expressions

**Build**

- `clang-tidy` and `clang-format` configurations added at the repo root
- `.vs/` and `CMakeSettings.json` added to `.gitignore`
