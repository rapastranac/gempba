# v3.1.0

<small>April 19, 2026 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v3.1.0)</small>

macOS support, system packaging (`.deb`, MSYS2, signed APT repo), and cross-platform portability fixes.

**Breaking changes**

- `gempba::Queue` renamed to `gempba::queue` (header moved to `include/gempba/utils/queue.hpp`) — update any direct uses
- Bundled `spdlog` removed; consumers must now provide a system-installed `spdlog` (and `fmt`) via `find_package`
- `GEMPBA_*` build options (`GEMPBA_MULTIPROCESSING`, `GEMPBA_BUILD_TESTS`, `GEMPBA_BUILD_EXAMPLES`, `GEMPBA_DEBUG_COMMENTS`, `GEMPBA_DEV_MODE`) now honor `-D` overrides when used as a subproject — previously hard-coded values shadowed user input

**Added**

- macOS officially supported (Apple Silicon, AppleClang/libc++, Homebrew Boost 1.90)
- Debian/Ubuntu `.deb` packages (`libgempba-dev`) published on tagged releases
- Signed APT repository at `apt-repo/` with `pubkey.gpg` for `apt-get install libgempba-dev`
- MSYS2 `PKGBUILD` at `packaging/msys2/` for `mingw64` / `ucrt64` / `clang64` builds, with a Windows CI publish job producing `.pkg.tar.zst` artifacts
- CMake install rules and `gempbaConfig.cmake` — consumers can now `find_package(gempba 3.1.0 REQUIRED)` and link `gempba::gempba`
- pkg-config file (`gempba.pc`) for non-CMake build systems
- `scripts/update-pkgbuild.sh` to regenerate `pkgver` and `sha256` for MSYS2 packagers

**Changed**

- README rewritten and trimmed; full reference moved to the [docs site](https://rapastranac.github.io/gempba-docs/)
- Build/run scripts relocated under `scripts/` (`build_linux.sh`, `build_windows.sh`, `run.sh`, etc.); old top-level `linux_build.sh` / `win_build.sh` removed
- `-rdynamic` is now applied only on Linux Debug builds (no longer leaks into Release or non-Linux targets)
- CPM.cmake download hardened against silent corruption

**Fixed**

- `mpi_centralized_scheduler`: misplaced parenthesis in the `MPI_Wtime`/`diff_time` comparison broke the rate-limit on "center queue full" notifications, causing repeated worker contacts every loop iteration instead of at most once per second
- `<bits/stdc++.h>` removed from `node_manager.hpp` — header now compiles on libc++ / AppleClang / MSVC
- C++23 `<stacktrace>` gated on `__has_include` so libc++ targets without it still build
- `gempba::score` type-dispatch, comparison, and `to_string` made portable across `long double` ABIs
- Dropped `-fconcepts` (legacy GCC flag) and gated `stdc++exp` for non-Linux toolchains
- `BS_thread_pool` include directory now propagated so private headers compile against installed packages
- `<gempba/config.h>` resolves correctly in all build layouts (now generated into `build/gempba/`)

**Removed**

- Private `node_manager` method that always returned zero (worker view returns zero directly)
