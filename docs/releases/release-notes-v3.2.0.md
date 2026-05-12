# v3.2.0

<small>May 5, 2026 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v3.2.0)</small>

Runtime telemetry scaffold: a public `gempba::telemetry` surface, hwloc-backed topology, and a TCP control channel for live dashboards.

**Added**

- New public namespace `gempba::telemetry` for live runtime introspection (off by default, opt in with `GEMPBA_TELEMETRY=ON`)
- `<gempba/telemetry/telemetry_hub.hpp>` — `telemetry_hub` with hot-path `record_send` / `record_recv` / `record_task_local` hooks, `tick_if_due` pump, frame snapshots, runtime interval setters, and process-wide `install` / `uninstall` / `disable` / `enable` / `is_enabled` / `configure_port` entry points
- `<gempba/telemetry/frames.hpp>` — wire-stable `worker_frame`, `node_frame`, `worker_identity`, `edge_counter`, `socket_stats`, `net_stats`, `disk_stats`, `control_msg` (schema version 1) and the `control_kind` enum
- `<gempba/telemetry/telemetry_transport.hpp>` — `telemetry_transport` interface with `worker_frame_sink` / `node_frame_sink` callbacks and `send_control` / `try_recv_control` for control-plane messages
- `<gempba/telemetry/topology.hpp>` — `topology_snapshot`, `topology_node`, `topology_socket` for static cluster layout
- `<gempba/telemetry/runtime_mode.hpp>` — `runtime_mode` enum (`MT_ONLY`, `MPI`)
- `gempba::try_get_scheduler()` — non-throwing accessor for callers that must tolerate a missing scheduler
- `gempba::scheduler::get_pending_request_count()` exposing scheduler queue depth
- `gempba::load_balancer::get_tasks_running_count()` and `get_thread_pool_size()` accessors
- TCP control protocol: a center process accepts client-pushed `SET_WORKER_INTERVAL_MS` / `SET_NODE_INTERVAL_MS` messages and fans them out via `apply_control_from_client`
- MPI telemetry transport that builds the multi-node topology via a one-shot `MPI_Allgather` of worker identities at startup
- hwloc-backed topology probe populating physical / logical core counts, CPU sets, and per-socket runtime stats
- `scripts/connect_telemetry.ps1` — single-command SSH-tunnel helper with HPC mode (`-Login user@host -Job <slurm-id>` resolves the compute node via `squeue`) and direct mode (`-SshHost user@host`)
- `scripts/watch_telemetry.ps1` — live-tail companion for an open tunnel

**Changed**

- `gempba::multiprocessing::create_node_manager` auto-installs a `telemetry_hub` when built with `GEMPBA_TELEMETRY`; `gempba::shutdown()` uninstalls it
- `GEMPBA_DEBUG_COMMENTS` now also activates automatically in `Debug` configurations

**Build**

- New CMake option `GEMPBA_TELEMETRY` (default `OFF`) — links `psapi` and `ws2_32` on Windows when enabled
- New CMake option `GEMPBA_HWLOC` (default `ON` when gempba is the root project) — discovered via `pkg-config` (`PkgConfig::HWLOC`) and linked privately
- New runtime dependency `hwloc` — system-package consumers must install it (`libhwloc-dev` on Debian/Ubuntu, `mingw-w64-*-hwloc` on MSYS2, `hwloc` on Homebrew); `.deb` and MSYS2 `PKGBUILD` declare it automatically
- `build_linux.sh` and `build_windows.sh` forward `GEMPBA_TELEMETRY` and `GEMPBA_HWLOC` to CMake
- `gempba/config.h` exposes `GEMPBA_TELEMETRY` and `GEMPBA_HWLOC` as `0`/`1` macros
- Compile flags trimmed: dropped redundant `-pthread`, `-fexceptions`, `-g`, `-O3 -DNDEBUG` from `target_compile_options` (CMake config-driven defaults handle them)
