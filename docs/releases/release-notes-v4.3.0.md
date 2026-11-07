# v4.3.0

<small>November 6, 2026 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v4.3.0)</small>

A telemetry release about explaining scaling. When adding ranks stops making a run faster, telemetry now shows where the time went: pool threads idle because the rank has no work or because work isn't spreading, workers waiting on the coordinator, time spent moving tasks, and telemetry's own cost. Every run carries an id and describes its setup, and the dashboard can pause, snapshot and mark a run while it runs. Multiprocessing runs also end deterministically, without waiting out a timeout.

The telemetry JSON moves to schema 2, which renames and removes some keys; `docs/telemetry/wire-format.md` lists every change. C++ code built against `v4.2.0` keeps compiling, with deprecation warnings where a call was renamed, unless it reads the telemetry frame structs directly or uses `control_kind::SET_NODE_INTERVAL_MS` (see **Changed** and **Removed**).

**Added**

- **Where idle time goes.** Each rank splits its pool threads' idle time into `idle_microseconds_starved`, while no task runs anywhere in the rank, and `idle_microseconds_imbalanced`, while other threads run tasks. The two add up to `idle_microseconds_per_thread`. `load_balancer` gains `get_starved_idle_time()` and `get_imbalanced_idle_time()`, which return 0 unless a custom balancer overrides them (#337)
- **Scheduler state times.** Workers report `sched_running_microseconds` and `sched_waiting_microseconds`, which together cover the run, plus `first_task_ms` and `last_task_ms`. The coordinator reports how long it takes to assign work (`assign_latency_microseconds_total`, `assign_latency_microseconds_max`, `assign_count`) and, under the centralized scheduler, `available_microseconds_queue_empty`: available ranks × time while its queue is empty (#338)
- **The coordinator's load, by kind.** `coordinator_state_msgs_total`, `coordinator_score_proposals_total`, `coordinator_score_broadcasts_total` and, under the centralized scheduler, `coordinator_full_microseconds`. Every rank also reports `telemetry_microseconds_total`, telemetry's own time on the scheduler thread, so it isn't blamed on the scheduler (#339)
- **The cost of moving tasks.** `bytes_sent_total`, `bytes_recv_total`, `send_microseconds_total` and `recv_microseconds_total`. `telemetry_hub::record_send` and `record_recv` take how long the MPI calls took, defaulting to zero (#340)
- **Spawn and score counters.** `spawn_rejected_pool_full` and `spawn_remote_rejected` tell "work can't be handed over in time" apart from "the algorithm exposes little parallelism". Workers report `score_proposals_sent`, and the coordinator `last_score_improvement_ms`. `load_balancer` gains `get_spawn_rejected_pool_full_count()` and `get_spawn_remote_rejected_count()`, which return 0 unless overridden (#341)
- **A run id.** Every broadcast carries `run_id`, a UUID v4, so a dashboard can tell two runs on the same port apart (#343)
- **The run's setup.** The topology gains a `run` object: scheduler, load balancer, runtime mode, world size, threads per process, `gempba_version`, and annotations set with `gempba::telemetry::annotate_run(key, value)`. Each rank's identity gains a `role`, `"coordinator"` or `"worker"` (#344)
- **Schedulers declare their roles and name.** `scheduler::role_of(rank)` returns each rank's `gempba::rank_role`, and `scheduler::get_name()` names the scheduler in `run.scheduler`. The built-in schedulers make rank 0 the coordinator; a custom scheduler that declares nothing is `"custom"`, with every rank a worker. `role` and `threads_per_process` follow these answers instead of assuming rank 0 is the coordinator (#391)
- **Pause and resume from the dashboard.** `{"kind":"pause"}` stops every rank sending frames while counters keep counting, and the aggregator keeps sending a short `"paused":true` line so a paused run doesn't look lost. `{"kind":"resume"}` picks up at the same interval (#368)
- **Snapshots on demand.** `{"kind":"snapshot"}` makes every rank send one frame right away, whatever the interval and even while paused, and the aggregator broadcasts one full line with them (#369)
- **Run markers.** `gempba::telemetry::mark_run("phase 2")`, or `{"kind":"mark","label":"phase 2"}` from the dashboard, records a timestamped marker. Broadcasts list them under `run.markers` (#370)
- **A final line.** The broadcast sent at shutdown says `"final":true`, and every other line `"final":false`, so a consumer can tell a finished run from a lost connection (#389)
- **`coordinator_messages_handled`** in the MPI stats: every message the coordinator handled, 0 on workers. It reaches the C ABI and Java too (#342)

**Changed**

- **Schema 2.** `TELEMETRY_SCHEMA_VERSION` is 2. Besides the added keys, schema 2 renames keys so each word means one thing: a rank is a process, coordinator and worker are scheduler roles, the aggregator (rank 0) serves the stream, and a thread is a pool thread. Update dashboards and scripts that read the old keys; `wire-format.md` keeps schema 1's names (#396, #342):
  - `workers` → `ranks`, `worker_id` → `rank`, the topology node's `worker_ids` → `ranks`, `sentinel_worker_id` → `sentinel_rank`, `worker_local_ms` → `rank_local_ms`;
  - `idle_microseconds_per_worker` → `idle_microseconds_per_thread`;
  - `tasks_local_total` → `tasks_started_total`, which counts every subtree handed to a pool thread, received ones included, at hand-off.
- **One telemetry interval.** Node frames refresh with rank frames, and the aggregator broadcasts at that interval instead of every 500 ms, so one interval drives everything the dashboard shows. The control that sets it is `{"kind":"set_interval_ms","value":N}` (#345, #367, #396)
- **Deterministic MPI termination.** Both MPI schedulers detect termination with Dijkstra–Scholten acknowledgements instead of a quiet period. A run ends as soon as its work is done, without the 3 s timeout at the end, and its result no longer depends on every message arriving within that timeout (#352)
- **Smaller rank frames.** A rank frame carries only the destinations it sent tasks to: about 300 bytes instead of 16 KB, and destinations past rank 1,024 are recorded (#371)
- **C++ telemetry frame structs.** Code that reads `worker_frame` or `node_frame` directly, such as a custom `telemetry_transport` or a caller of `telemetry_hub::latest_rank_frame()`, needs updating: `m_tasks_local_total` is now `m_tasks_started_total`, `m_edges_out` is a `std::vector<edge_counter>` holding only the destinations sent to, and the node frame no longer has `m_net_aggregate` or `m_disk_aggregate` (#342, #371)
- **The telemetry viewer.** `telemetry_view` shows one row per rank with its role, marks paused stretches and prints run markers. The bash version now shows times on macOS too (#378)
- **The Java JARs' macOS natives are Intel only.** They are built for `osx-x86_64`, while `v4.2.0`'s were `osx-aarch64`, so on an Apple Silicon Mac the JAR finds no native to load. #409 tracks bringing Apple Silicon natives back (#387)

**Deprecated**

Each of these keeps working in `v4.3.0` and is removed in `v5.0.0` (#373, #353).

- `telemetry_hub::set_worker_interval_ms()`, `worker_interval_ms()` and `latest_worker_frame()`, and `control_kind::SET_WORKER_INTERVAL_MS`. They forward to `set_interval_ms()`, `interval_ms()`, `latest_rank_frame()` and `SET_INTERVAL_MS` (#396)
- `telemetry_hub::set_node_interval_ms()`, which does nothing, and `node_interval_ms()`, which returns the telemetry interval, since node frames follow it (#384)
- The `set_worker_interval_ms` control line, which does what `set_interval_ms` does, and the `set_node_interval_ms` control line, which is accepted and ignored (#396, #367)
- The `total_requested_tasks` stat. It means tasks sent plus received on workers, but requests handled on the coordinator. Read `coordinator_messages_handled` on the coordinator, and `sent_task_count` + `received_task_count` on workers (#342)
- The scheduler timeout. `create_scheduler(topology, timeout)` in C++, `gempba_scheduler_create` in the C ABI and `GemPBA.createScheduler` in Java still take it, and ignore it (#352)

**Removed**

- **The `telemetry_tunnel` scripts** and `docs/remote-connection.md`. The GemPBA Dashboard opens the SSH forward itself, including the jump-host hop and MFA (#378)
- **The node frame's `net_aggregate` and `disk_aggregate`.** Both were always 0, and host network counters can't see RDMA or InfiniBand traffic. Use each rank's `bytes_sent_total` and `bytes_recv_total` for task traffic (#342)
- **`control_kind::SET_NODE_INTERVAL_MS`.** Nothing outside gempba builds control messages, and the `set_node_interval_ms` control line is still accepted (#367)

**Fixed**

- **Every available rank gets work.** The centralized coordinator's `clear_buffer` handed out at most one task per call; it now hands one to each available rank, highest priority first (#334)
- **Live idle time counts sleeping threads.** A thread asleep through a starved stretch added no idle time until it woke, so live frames under-reported exactly while a rank was starved. End-of-run totals were already right (#335)
- **The last telemetry line is current.** Shutdown publishes every rank's final frame before the last broadcast; under MPI the aggregator waits up to one second for them instead of dropping late ones (#336)
- **A slow client no longer stalls the telemetry stream.** The aggregator wrote each line to every client in turn with blocking sends, so one client that stopped reading held up the other clients, the control lines and shutdown. Each client now has its own queue of up to 16 whole lines: a client that falls behind skips its oldest lines, and shutdown waits at most one second for it to take the final line (#69)
- **Workers read every coordinator notification before they exit.** A worker told to terminate could leave score updates and other notifications unread before `MPI_Finalize`. The termination message now carries how many to expect, and each worker drains them (#356)
- **`set_thread_pool_size` returns once the pool is idle**, so a `try_local_submit` right after it no longer sees a full pool (#348)

**Build**

- The pinned thread pool is `v5.1.0.3`, in `external/CMakeLists.txt` and the Homebrew formulas. It counts sleeping threads' idle time and splits it into starved and imbalanced (#335, #337)
- With `GEMPBA_MPI_INTEGRATION_TESTS=ON`, CTest runs `termination_stress` (both schedulers on 2, 4 and 8 ranks) and `telemetry_stress` (both schedulers on 4 ranks) under `mpiexec`. The Ubuntu CI job enables them (#352, #374)
- CI runs on pull requests into any branch, not only `main` and `release` (#346)
- The macOS jobs read the runner variable `RUNNER_MACOS_INTEL` instead of `RUNNER_MACOS` (#387)
- The Homebrew verify job removes leftover gempba kegs before installing a flavor, and fails if one stays installed (#398)
- CI runs fewer jobs per pull request:
  - each OS has one C/C++ test job, which publishes its own report, and one examples job that builds both flavors;
  - the `.deb` with its apt index, the MSYS2 package and the multi-platform JARs build only for pull requests into `main` or `release` and for pushes to them;
  - pull requests that change only docs or Markdown start no CI, and lint no longer runs on tags (#402)

**Docs**

- `docs/telemetry/wire-format.md` describes the telemetry stream: every key with its unit and the release that added it, every control line, the compatibility rules and both schema versions (#381)
- `docs/telemetry/custom-schedulers.md` says what a custom scheduler must do for its runs to reach the dashboard (#393)
