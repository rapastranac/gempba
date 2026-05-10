# v2.0.0

<small>August 17, 2025 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v2.0.0)</small>

Major release: new `gempba::score`, `gempba::task_packet`, and `gempba::result` public types replace string-based transport and the `int` reference value, alongside a sweeping `clang-tidy` rename of the public API (`BranchHandler` → `branch_handler`, `MPI_Scheduler` → `mpi_scheduler`, `SchedulerParent` → `scheduler_parent`, etc.).

**Breaking changes**

- `gempba::BranchHandler` → `gempba::branch_handler`; `getInstance()` → `get_instance()`; `try_push_MT` → `try_push_mt`; member methods renamed per `.clang-tidy` (e.g. `refValue()` is gone — use `get_score().get<T>()`)
- `gempba::MPI_Scheduler` → `gempba::mpi_scheduler`; `gempba::MPI_SchedulerCentralized` → `gempba::mpi_centralized_scheduler` (header `MPI_Scheduler_Centralized.hpp` → `mpi_centralized_scheduler.hpp`)
- `gempba::SchedulerParent` → `gempba::scheduler_parent`; `fetchSolution()` → `fetch_solution()` now returns `task_packet` instead of `std::string`; `fetchResVec()` → `fetch_result_vector()` returns `std::vector<gempba::result>` instead of `std::vector<std::pair<int, std::string>>`; `push()` now takes a `task_packet&&` instead of `std::string`
- `branch_handler::set_score` / `get_score` / `set_goal` and the `gempba::result` constructor: `int` reference value replaced with `gempba::score`. `set_goal` now takes a `gempba::goal` enum plus `gempba::score_type` (was a `bool`)
- `hold_solution` removed — replaced by `try_update_result(solution, score)`: type-safe, parameters reordered, default parameter dropped, returns `bool` indicating whether the update happened
- `update_reference_value` → `try_update_reference_value`; further renamed to `try_update_reference_value_and_invalidate_result` to reflect that it now clears any cached result when the score changes
- `score::get_loose` removed — use `score::to_string` (logs print the value with its real underlying type) or the typed `score::get<T>()` accessor
- `set_ref_val_strategy_lookup` → `scheduler_parent::set_goal`; `lookup_strategy` parameter → `goal` enum
- `EMPTY_RESULT` constant removed — use `result::EMPTY`
- `m_reference_value` member of `gempba::result` renamed to `m_score`; scheduler members `m_ref_value_global` → `m_global_reference_value` (and corresponding communicator member)
- Examples no longer accept the `THREADS_PER_TASK` argument on the command line
- `GEMPBA_MULTIPROCESSING` is now a value macro (`0`/`1`), no longer a bare definition — examples and downstream consumers must use `#if GEMPBA_MULTIPROCESSING` instead of `#ifdef`

**Added**

- `gempba::task_packet` (`include/utils/ipc/task_packet.hpp`) — raw-byte transport that replaces serialized `std::string` in the scheduler API; serializers now return `task_packet`
- `gempba::result` (`include/utils/ipc/result.hpp`) — bundles a `score` plus `task_packet` for shipping solutions between ranks
- `gempba::score` (`include/utils/ipc/score.hpp`) and `gempba::score_type` enum (`I32`, `I64`, `F32`, `F64`, `F128`) — multi-primitive score support, formerly the integer-only `reference_value` (suggested by @Manuel-GithubAccount in [#29](https://github.com/rapastranac/gempba/issues/29))
- `gempba::goal` enum to replace the previous boolean min/max strategy flag
- `score::make` factory and `score::to_string` for type-aware logging
- README "Concepts" section documenting `goal`, `score`, and `score_type`

**Changed**

- Schedulers now exchange raw bytes (`task_packet`) instead of serialized strings end-to-end through `branch_handler` and `scheduler_parent`
- `mpi_scheduler` and `mpi_centralized_scheduler` made structurally parallel: shared `should_broadcast` logic, `utils::diff_time` adopted in both, analogous global-reference-value checks
- `handle_full_messaging` → `monitor_and_notify_center_status`
- Most `GEMPBA_DEBUG_COMMENTS` macro sites replaced with a single utility method
- README updated for all renamed identifiers and the new `try_update_result` / `score` usage
- `batch.sh` → `run.sh`

**Fixed**

- Communicator probing in `mpi_centralized_scheduler` (matching the fix already in `mpi_scheduler`)
- `openmpi` invocation now binds processes to the intended number of cores
- A `try_update_result` path that wasn't actually updating the stored reference value
- `GEMPBA_MULTIPROCESSING` checks ([#49](https://github.com/rapastranac/gempba/issues/49)): switched from `#ifdef` to `#if`, and the macro is now injected as an explicit `0`/`1` value so non-MP examples see it defined as false rather than undefined

**Build**

- `CMakeLists.txt` project version bumped to `2.0.0`
- `examples/CMakeLists.txt`: defines `GEMPBA_MULTIPROCESSING=1` for `mp_*` examples and `GEMPBA_MULTIPROCESSING=0` for the rest (previously only the `mp_*` side was defined)
