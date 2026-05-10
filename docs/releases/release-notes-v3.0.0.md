# v3.0.0

<small>April 19, 2026 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v3.0.0)</small>

The largest release in the project's history: a ground-up redesign that replaces the heavy template-driven API with a single-header `gempba::` facade. `branch_handler` becomes `node_manager`, the entire pre-v3 surface (`result_holder`, `dynamic_load_balancer_handler`, `Pool`, `args_handler`, all `*2`-suffixed members) is removed, and the public surface is reorganized under `include/gempba/`.

**Breaking changes**

- `gempba::branch_handler` renamed to `gempba::node_manager` (class and header) — the main user-facing class
- `gempba::ResultHolder` / `gempba::result_holder` removed entirely; use `gempba::node` instead — function signatures no longer take a holder template
- `gempba::DLB_Handler` / `gempba::dynamic_load_balancer_handler` removed; use `gempba::load_balancer` (interface) and the new `create_load_balancer` factories
- `gempba::Pool` and `gempba::args_handler` removed
- All `*2`-suffixed members removed: `is_done2`, `wait2`, `get_balancing_policy2`, plus `*2` variants in MPI schedulers
- `node_manager::lock()` / `unlock()` removed
- `node_manager` member renames: `try_push_mp` → `try_remote_submit`, `try_push_mt` → `try_local_submit`, `force_push` → `force_local_submit`, `push_multiprocess` / `push_multithreading` → `send`, `WTime` → `get_wall_time`, `try_top_holder` → `try_push_root_level_holder_remotely`, `pass_mpi_scheduler` → `pass_scheduler`
- Identifier renames on the public surface: `load_balancing_strategy` → `balancing_policy`, `print_mpi_debug_comments` → `print_ipc_debug_comments`, `FUNCTION_ARGS` → `TASK`, `gbitset` → `G_BITSET`, "reference value" → `score` throughout MPI schedulers
- `mpi_scheduler` renamed to `mpi_semi_centralized_scheduler`
- `scheduler` members supplanted by `scheduler::worker` / `scheduler::center` views: `fetch_solution`, `fetch_result_vector`, `next_process`, `push`, `run_node`, `run_center`, `try_open_transmission_channel`, `close_transmission_channel`
- `scheduler::get_total_requests` removed (now sourced from the new stats interface)
- C++23 enforced in CMake (`CMAKE_CXX_STANDARD 23`, `CMAKE_CXX_STANDARD_REQUIRED ON`, extensions off); C++20 compilers are no longer supported
- `BS::thread_pool` is now an external dependency; consumers using CPM mirror it automatically, manual integrations must add it
- `GEMPBA_*` compile definitions on the `gempba` target are now `PRIVATE` instead of `PUBLIC`
- Public headers reorganized under `include/gempba/` (`core/`, `utils/`, `stats/`, `defaults/`, `detail/`); legacy `include/utils/...` paths are gone
- Legacy `.csv` / `.dat` raw printing removed from examples; CSV log is now opt-in and off by default

**Added**

- Single-header facade `#include <gempba/gempba.hpp>` exposing `gempba::` factories
- `gempba::node` — lightweight, copyable, template-free node handle (replaces `result_holder`)
- `gempba::shutdown()` for explicit, controlled global cleanup
- `gempba::node_manager` factories: `multithreading::create_node_manager(load_balancer*)` and `multiprocessing::create_node_manager(load_balancer*, scheduler::worker*)`
- `gempba::load_balancer` public interface with two stock implementations: `quasi_horizontal_load_balancer` (recommended) and `work_stealing_load_balancer`
- `multithreading::create_load_balancer(balancing_policy)` and `multiprocessing::create_load_balancer(balancing_policy, scheduler::worker*)` factories, plus a BYO-implementation overload
- `gempba::scheduler` public interface with `scheduler::worker` and `scheduler::center` views, `scheduler_traits`, and `multiprocessing::create_scheduler(scheduler_topology, timeout)` (`SEMI_CENTRALIZED`, `CENTRALIZED`)
- `gempba::node_core` extension point (`include/gempba/core/node_core.hpp`) plus `node_traits` and the `detail/nodes/node_core_impl.hpp` template implementation
- `gempba::stats` and `gempba::stats_visitor` interfaces; `default_mpi_stats_visitor` for the bundled MPI schedulers (visitor pattern, string-keyed metrics)
- `gempba::serial_runnable` interface with `serial_runnable_void` / `serial_runnable_non_void` impls and `runnables::return_none::create` / `runnables::return_value::create` helpers for MP task dispatch
- `gempba::serializable` interface to split serialization responsibilities out of node trace
- `gempba::task_bundle` and `gempba::transmission_guard` utilities under `include/gempba/utils/`
- `invokable` C++23 concept that enforces task signatures of the form `Ret(std::thread::id, Args..., node)`
- `gempba::score` extended with `uint32_t` and `int64_t` so it can carry `std::size_t` and other common types; spaceship `operator<=>` on `task_packet`
- `utils::log_and_throw` (replaces direct `spdlog::throw_spdlog_ex`) with C++23 `<stacktrace>` integration
- Generated `gempba/config.h` (from `config.h.in`) so IDEs see the build flags
- Multi-processing and multi-threading benchmark cases
- One-call Windows and Linux build scripts and helpers to run all graphs in a directory

**Changed**

- Module renames swept the codebase to snake_case: `BranchHandler` → `branch_handler` (then `node_manager`), `ThreadPool` → `thread_pool`, `DLB` → `dynamic_load_balancer`, `MPI_Modules` → `schedulers`, `Resultholder` → `result_holder` (then removed)
- `gempba::score` now has unsigned 32/64 specializations; `score::make(...)` accepts `std::size_t`
- Schedulers now receive their timeout at construction (`create_scheduler(topology, timeout)`)
- `default_mpi_stats_visitor` lives in the `defaults` module and is exposed via `multiprocessing::get_default_mpi_stats_visitor()`
- Internal `spdlog::info` calls demoted to `spdlog::debug`
- `#ifdef GEMPBA_DEBUG_COMMENTS` replaced with `#if GEMPBA_DEBUG_COMMENTS` (matches the new `cmakedefine01`)
- README rewritten for v3.0 (facade pattern, extensible architecture, updated requirements, Windows added to supported platforms)

**Fixed**

- Race condition that could let a worker thread throw
- Edge case in node pruning (children weren't cleared on prune)
- Critical guard fix in `quasi_horizontal_load_balancer`

**Build**

- C++23 enforced (`CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`)
- Links `stdc++exp` on non-MSVC for C++23 `<stacktrace>` support
- New `GEMPBA_DEV_MODE` toggle in the root `CMakeLists.txt` (auto-on when gempba is the root project)
- `GEMPBA_*` target flags scoped to `PRIVATE` (no longer leaks to consumers)
- `BS::thread_pool` added via CPM (`rapastranac/thread-pool`); `argparse` moved to `examples/external` since it's only used by examples
- `GIT_SHALLOW TRUE` set on CPM external clones
- Per-test discovery in CTest (`gtest_discover_tests`) and a separate test-artifact publish job in CI; `FLAKY_` test-name convention for flaky cases
- Dropped redundant `git install` step from CI
