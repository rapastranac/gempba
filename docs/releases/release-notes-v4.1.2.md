# v4.1.2

<small>May 31, 2026 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v4.1.2)</small>

A targeted bugfix release: multiprocessing now runs on macOS. MP-mode binaries aborted at startup on macOS — and only macOS — with `mutex lock failed: Invalid argument`, while the same build ran fine on Linux and Windows. If you only use MT mode, or don't run on macOS, nothing here affects you. No library or API changes.

**Fixed**

- **macOS MP no longer aborts at startup.** Both MPI schedulers passed a 128-byte buffer to `MPI_Get_processor_name`, which OpenMPI requires to be `MPI_MAX_PROCESSOR_NAME` (256) — it fills the full field, overflowing the buffer and zeroing an adjacent `std::mutex`. A zeroed mutex is a valid `PTHREAD_MUTEX_INITIALIZER` on Linux/glibc, so the corruption stayed invisible there; macOS rejects it with `EINVAL`, so the first lock threw `std::system_error`. The buffer is now sized `MPI_MAX_PROCESSOR_NAME` in both the semi-centralized and centralized schedulers (#310)

**Removed**

- Dropped an unused `m_processor_name` field from `node_manager` — declared but never read or written, and carrying the same undersized buffer (#310)

**Docs**

- Added a DeepWiki badge and callout to the README (#308)

**Build / CI**

- Split the Java binding workflow's native build from its tests, so a flaky Java test re-runs on its own without rebuilding the JNI library — the build job hands the native lib to the test job as a per-run artifact (#311)
- Extracted the duplicated gempba-examples branch resolver into a shared composite action, deduping the three C/C++ CI workflows (#311)
- Bumped the PKGBUILD `sha256` for the v4.1.1 packaging artifact
