# v4.4.0

<small>November 9, 2026 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v4.4.0)</small>

A packaging release. gempba's packages and Java JARs now install and build on a clean machine of each platform they support, instead of only on machines set up like the ones that built them. The Java natives depend only on the operating system and MPI, the public headers include only the C++ standard library, Windows moves from MSYS2's MINGW64 to UCRT64, and each package declares what it needs.

No public API is removed. Upgrading needs action in three cases: apt users switch the repository's suite from `stable` to `noble`, MSYS2 users install the UCRT64 packages, and a program that got spdlog or fmt through gempba's headers now finds them itself (see **Changed**).

**Changed**

- **The apt suite is `noble`.** The `.deb`s are built for Ubuntu 24.04, and the repository now publishes them in the `noble` suite instead of `stable`. In `/etc/apt/sources.list.d/gempba.list`, change `stable main` to `noble main`, then run `sudo apt update`; until then, `apt update` reports that the repository has no `stable` release. The repository serves the latest release, and older `.deb`s stay attached to each GitHub Release (#411)
- **Windows uses MSYS2's UCRT64.** MSYS2 is retiring MINGW64, so the packages are now `mingw-w64-ucrt-x86_64-gempba` and `mingw-w64-ucrt-x86_64-gempba-mpi`, installed from an MSYS2 UCRT64 shell. To build from the PKGBUILD, run `MINGW_ARCH=ucrt64 makepkg-mingw -si`. The Java DLL is built on UCRT64 too (#407)
- **The public headers include only the C++ standard library.** gempba logs through functions compiled into the library and formats with `std::format`, so its headers no longer include spdlog or need fmt (#412):
  - spdlog is compiled into `libgempba.a`. The packages list it only as a build dependency, and `gempba.pc` no longer defines `SPDLOG_HEADER_ONLY`;
  - fmt stays a private link dependency, which `find_package(gempba)` still finds;
  - a program that used spdlog or fmt through gempba's headers now finds them itself, as gempba-examples does. If it also uses spdlog, it should use it header-only and at the same version, since `libgempba.a` holds its own copy of spdlog's inline code;
  - `utils::log_and_throw` and `utils::print_ipc_debug_comments` take a `std::format_string`. Calls with a literal format string compile unchanged.
- **The Java natives depend only on the OS and MPI.** The C++ runtime, hwloc and fmt are linked into them, so they need glibc 2.28 or newer on Linux (such as RHEL 8), Windows 10 or newer, or macOS 14 or newer, plus an MPI runtime for `mp-mpi`. Before, they loaded only where the build machine's libraries were installed: MSYS2's DLLs on Windows, glibc 2.38 and Ubuntu 24.04's libraries on Linux, and Homebrew's fmt and hwloc and macOS 15 on Macs (#408)
- **Package descriptions** call GemPBA a hybrid parallel framework (#411)

**Fixed**

- **`find_package(gempba)` works right after installing a package.** `gempbaConfig.cmake` finds hwloc through pkg-config, but no package installed it, so on a clean Ubuntu 24.04 `find_package(gempba)` failed with "Could NOT find PkgConfig". `libgempba-dev`, the MSYS2 package and both Homebrew formulas now depend on pkg-config (#411)
- **The `.deb` declares the runtimes it needs:** `libc6 (>= 2.38)` and `libstdc++6 (>= 13.1)`, instead of `libc6 (>= 2.17)`, which let it install on systems it can't link on. It no longer depends on `libgomp1`, which nothing used (#411)
- **The Homebrew formulas no longer install Boost**, which gempba doesn't use (#411)
- **gempba builds with toolchains that lack `std::stacktrace`'s library,** such as Red Hat's gcc-toolset and macOS's clang. CMake links `stdc++exp` only where a `std::stacktrace` program links and otherwise builds without stack traces, and `gempba.pc` no longer names it (#408)
- **gempba compiles with the definitions of the spdlog package it finds,** so an spdlog built against an external fmt no longer reaches for its bundled copy (#408)

**Build**

- gempba compiles without MPI's C++ bindings (`MPI_CXX_SKIP_MPICXX`), since it uses only MPI's C API (#408)
- The Linux JARs build in a manylinux_2_28 container (AlmaLinux 8, GCC 14) from `bindings/jni/Dockerfile`, so building them needs Docker and is x86_64 only. `bindings/jni/build_deps.sh` builds the static hwloc and the fmt and spdlog the natives link, from pinned, checksummed sources (#408)
- `scripts/check_jni_deps.sh` fails a JAR build whose natives need anything beyond the OS and, for `mp-mpi`, MPI. On Linux it also fails a glibc floor above 2.28 (#408)
- CI runs the Java tests on clean machines: on Linux in a stock Maven and JDK 25 container, and on Windows from PowerShell with no MSYS2 on PATH (#408)
- Windows CI, the MSYS2 packages and `build_jar_windows.sh` build on UCRT64. The PKGBUILD builds for `ucrt64` only, and `build_jar_windows.sh` refuses to run from any other MSYS2 shell (#407)

**Docs**

- The README says what each package needs and where it runs: the `.deb`s are for Ubuntu 24.04, the JAR needs Java 25, its macOS natives are Intel only, and the Requirements table lists every build dependency with its version (#411)

**Not in this release**

- **The Java JARs' macOS natives are still Intel only.** Apple Silicon natives (#409), publishing to Maven Central (#410) and gempba in MSYS2's own repository (#413) move to `v5.0.0`.
