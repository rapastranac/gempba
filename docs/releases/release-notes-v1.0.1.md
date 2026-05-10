# v1.0.1

<small>October 30, 2024 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v1.0.1)</small>

Restructured layout, C++23 build, typed strategy enums, and a scheduler base class so the semicentralized and centralized schedulers can coexist.

**Added**

- `gempba::SchedulerParent` (`MPI_Modules/scheduler_parent.hpp`) — common abstract base for `MPI_Scheduler` and the centralized scheduler so both can be instantiated independently
- `gempba::ResultHolderParent` (`Resultholder/ResultHolderParent.hpp`) — non-template base for `ResultHolder` so virtual interfaces no longer leak template parameters
- `gempba::LoadBalancingStrategy` enum (`QUASI_HORIZONTAL`, `WORK_STEALING`) and `gempba::LookupStrategy` enum (`MAXIMISE`, `MINIMISE`) in `utils/gempba_utils.hpp`
- `BranchHandler::setLoadBalancingStrategy` / `getLoadBalancingStrategy`
- `BranchHandler::setLookupStrategy(gempba::LookupStrategy)` — typed replacement for the string-keyword setter
- `BranchHandler::getWorldRank`
- `DLB_Handler::getRoot(int threadId)`
- `THIRD-PARTY-LICENSES` file with CPTL credits (Apache 2.0)

**Changed**

- C++ standard bumped to C++23 (`CMAKE_CXX_STANDARD 23`); minimum CMake bumped to 3.28
- Project layout reorganized: per-example executables under `bin/`, examples and tests in dedicated trees, headers under `GemPBA/utils/` (was `GemPBA/Utils/`)
- Build now produces one executable per file under `examples/`; sources matching `mp_*` get `-DMULTIPROCESSING_ENABLED` automatically
- The library is now built as a CMake target named `gempba` (was a single `a.out` executable)
- `MPI_ENABLED` compile macro renamed to `MULTIPROCESSING_ENABLED`
- `R_SEARCH` macro removed; load-balancing strategy is now a runtime enum on `BranchHandler` (so misuse becomes a compile error rather than a silent macro miss)
- Centralized scheduler class renamed from `GemPBA::MPI_Scheduler` (in `MPI_Scheduler_Centralized.hpp`) to `gempba::MPI_SchedulerCentralized`, allowing both schedulers to be linked into the same binary
- Public namespace renamed from `GemPBA` to `gempba` (all classes and free functions moved)
- `MPI_Scheduler::rank_me`, `elapsedTime`, `getWorldSize`, `tasksRecvd`, `tasksSent`, `nextProcess` are now `const`; `runNode` signature changed to take `std::function` callbacks (no longer a templated lambda accepting a `serializer`)
- `SchedulerParent::getTotalRequests` returns `size_t` (was `double` on the prior centralized scheduler)
- Input data directory renamed from `input/` to `data/`
- Logging migrated from `fmt` to `spdlog` (which uses fmt internally); debug prints go through `utils::print_mpi_debug_comments`
- README updated for the new namespace, strategy enums, and centralized-scheduler usage

**Removed**

- `BranchHandler::setRefValStrategyLookup(std::string)` — replaced by `setLookupStrategy(gempba::LookupStrategy)`
- `R_SEARCH` compile-time flag

**Build**

- Root `CMakeLists.txt` rewritten: `find_package(spdlog REQUIRED)` and `find_package(GTest REQUIRED)` added; resources globbed recursively
- `fmt` dropped as a direct FetchContent dependency (pulled in transitively via spdlog)
- `argparse` FetchContent pin bumped from v2.9 to v3.0
- Boost components `system`, `serialization`, `fiber` linked per-target via imported `Boost::*` targets instead of `CMAKE_EXE_LINKER_FLAGS`
- Tests promoted to a top-level `tests/` subdirectory with its own CMake project, linking GTest/GMock and Boost; `enable_testing()` and `add_test` registered
- New `.github/workflows/c-cpp.yml` CI pipeline triggered on pushes/merges to `main`
- Executable output directory moved to `${CMAKE_SOURCE_DIR}/bin`

**Fixed**

- CMake resource discovery now recurses into subdirectories
- OpenMP made visible to the test target
- spdlog fetch / link wiring corrected
- Several macro-guarded code paths that broke when `MPI_ENABLED` was off
