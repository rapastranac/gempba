# v3.3.0

<small>May 21, 2026 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v3.3.0)</small>

Runtime telemetry: process-wide hub with local / TCP / MPI transports, hwloc-backed topology probe, and a small set of read-only runtime accessors. Telemetry ships ON by default; opt out at runtime with `gempba::telemetry::disable()`. No API breaks.

**Added**

- `<gempba/telemetry/telemetry_hub.hpp>` — process-wide telemetry hub that publishes worker / node frames and routes control messages. Singleton accessed via `gempba::telemetry::get()`; runtime kill switch via `disable()` / `enable()` / `is_enabled()` (process-local, sticky, must be set symmetrically across MPI ranks)
- Local in-process and TCP server transports; TCP binds `127.0.0.1:9000` by default (`configure_port()` to change before the first `create_*` call)
- MPI transport on a private communicator (`MPI_Comm_dup`) so telemetry traffic never collides with application traffic; auto-installed inside `mp::create_scheduler` so the collective install runs on every rank
- hwloc-backed topology probe: per-socket physical / logical core counts, total memory, CPU brand, cpu-id list; multi-node topology assembled via `MPI_Allgather` of the `worker_identity` POD
- JSON serializer for telemetry frames; client-pushed interval-control protocol over the TCP socket so dashboards can throttle publish rate live
- Read-only runtime accessors: `gempba::try_get_scheduler` (non-throwing variant of `get_scheduler`), `load_balancer::get_thread_pool_size`, `load_balancer::get_tasks_running_count`, `scheduler::get_pending_request_count`
- `scripts/connect_telemetry.sh` — SSH-tunnel helper for inspecting a remote rank's telemetry socket from a local dashboard

**Changed**

- Default thread-pool size when no explicit size is set is now `1` (was one-per-core via `BS::thread_pool`'s default). The concrete `quasi_horizontal_load_balancer` and `work_stealing_load_balancer` impls construct `BS::thread_pool<>{1}` explicitly. Users who relied on the implicit default should pass the size explicitly through their scheduler init

**Fixed**

- Exported `gempbaConfig.cmake` now re-discovers hwloc behind `GEMPBA_HWLOC`, so downstream `find_package(gempba)` consumers resolve `PkgConfig::HWLOC` at link time instead of failing with `target not found` (latent since hwloc became a PRIVATE link dep on a STATIC library)
- `mpi_semi_centralized_scheduler` double-counted `m_sent_task_count` — increments fired on two paths for the same dispatch; consolidated to a single ownership point so the stats visitor and telemetry's `record_send` see consistent values

**Build**

- hwloc is a new runtime dependency, gated by the `GEMPBA_HWLOC` CMake option (ON for releases, OFF for dev builds). Discovered via `pkg-config` on all three platforms; `.deb` and MSYS2 packages declare it as a runtime dep
- CI installs hwloc on Ubuntu 24.04 / macOS 26 / Windows 2025 (MSYS2) / lint runners
- `build_*.sh` scripts forward `GEMPBA_HWLOC` so packagers can disable hwloc cleanly
- Windows builds link `psapi` (process-info probe) and `ws2_32` (Winsock for the TCP server)
