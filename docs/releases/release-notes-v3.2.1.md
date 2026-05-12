# v3.2.1

<small>May 6, 2026 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v3.2.1)</small>

Stability follow-up to the v3.2.0 telemetry scaffold: a critical multi-rank fix, hardening of static-init paths, and a pinned external dependency.

**Fixed**

- MPI deadlock when telemetry was installed on some ranks but not others — every rank now agrees on whether the telemetry transport is wired up, so peers don't block waiting on traffic that will never come ([#70](https://github.com/rapastranac/gempba/pull/71))
- `debug_logger_initializer` constructor marked `noexcept` so it cannot throw during static initialization (caught by `clang-tidy`'s `bugprone-throwing-static-initialization`) — a thrown exception there would have terminated the program before `main`

**Changed**

- `BS::thread_pool` pinned to `v5.1.0.1` instead of tracking `master`, so reproducible builds no longer drift when upstream changes ([#77](https://github.com/rapastranac/gempba/pull/78))

**Build**

- CI `clang-tidy` bumped from 18 to 19
