# Generic Massive Parallelisation of Branching Algorithms

[![C/C++ CI (Ubuntu 24.04)](https://github.com/rapastranac/gempba/actions/workflows/ci-cpp-ubuntu.yml/badge.svg)](https://github.com/rapastranac/gempba/actions/workflows/ci-cpp-ubuntu.yml)
[![C/C++ CI (Windows 2025)](https://github.com/rapastranac/gempba/actions/workflows/ci-cpp-windows.yml/badge.svg)](https://github.com/rapastranac/gempba/actions/workflows/ci-cpp-windows.yml)
[![C/C++ CI (macOS 26)](https://github.com/rapastranac/gempba/actions/workflows/ci-cpp-macos.yml/badge.svg)](https://github.com/rapastranac/gempba/actions/workflows/ci-cpp-macos.yml)
[![Lint](https://github.com/rapastranac/gempba/actions/workflows/ci-lint.yml/badge.svg)](https://github.com/rapastranac/gempba/actions/workflows/ci-lint.yml)
[![codecov](https://codecov.io/gh/rapastranac/gempba/branch/main/graph/badge.svg)](https://codecov.io/gh/rapastranac/gempba)
![GitHub License](https://img.shields.io/github/license/rapastranac/gempba)
![GitHub Release](https://img.shields.io/github/v/release/rapastranac/gempba)
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/rapastranac/gempba)

**[Full documentation →](https://rapastranac.github.io/gempba-docs/)**

---

## Why This Exists

If you have ever tried to parallelize a branch-and-bound algorithm by hand, you already know the pain. You start with a clean recursive function, it works great, and then someone says "can we use all these CPU cores?" Suddenly you are drowning in thread pools, work queues, mutexes protecting a shared best-solution, and recursive calls that somehow need to coordinate across threads without stomping on each other.

I built GemPBA because I kept solving the same problem from scratch for every new branching algorithm I worked on. The scheduling scaffold was always the same; only the algorithm in the middle changed. And every time I searched for an existing library, I either could not figure out how to build it (the documentation was three paragraphs and a "see the tests"), or it was so tightly coupled to one algorithm structure that adapting it meant basically rewriting it.

GemPBA's answer to this is a framework that inserts itself into your recursion through a small set of parameter additions. You keep writing your algorithm the way you always have. GemPBA handles the rest.

## What is GemPBA

GemPBA is a hybrid parallelization framework for branching algorithms. It supports:

- **Multithreading**: multiple worker threads within a single process
- **Multiprocessing**: work distributed across multiple processes (OpenMPI by default, but pluggable)
- **Hybrid**: multiple threads per process, spread across multiple nodes

The two main research contributions baked into the framework are the **Quasi-Horizontal Load Balancing** strategy (selects work near the root of the recursion tree, where each task spawns the most downstream work) and the **Semi-Centralized Scheduler** (eliminates the rejected-task bounce-back problem without creating a routing bottleneck). For the full performance analysis and formal description, see:

- [MSc. Thesis](http://hdl.handle.net/11143/18687)
- [Paper in Parallel Computing](https://doi.org/10.1016/j.parco.2023.103024)

> [!TIP]
> For a navigable, AI-assisted deep-dive into GemPBA's architecture and source, browse the **[GemPBA DeepWiki](https://deepwiki.com/rapastranac/gempba)** — auto-generated and refreshed periodically.

## Platforms

- Linux
- Windows
- macOS

Pre-built packages and the Java fat JAR target one architecture per OS:

| OS | Architecture |
|---|---|
| Linux | `x86_64` |
| Windows | `x86_64` |
| macOS | `x86_64` (Intel) |

Other architectures work via source build.

## Installing

GemPBA is one library with multithreading built in, plus an optional **MPI component**:

- **The core** (`gempba`) uses all the cores of a **single machine**. No MPI to install, nothing to configure; if you're unsure, start here.
- **The MPI component** (`gempba-mpi`, the same name in C++ and Java) adds the schedulers that distribute work across **multiple machines/nodes** over MPI. Install it on top of the core when one machine isn't enough. It installs as source: your build compiles it against the MPI on your machine (Open MPI, MPICH or MS-MPI), so one installation works with any of them.

A program uses the MPI component only when it asks for it (see [Using gempba](#using-gempba)), so multithreading and MPI programs build side by side from the same installation.

### Pre-built packages (C++)

**Ubuntu 24.04 (noble)** — the `.deb`s are built for Ubuntu 24.04 and live in a signed APT repository hosted at `https://rapastranac.github.io/gempba`. The repository serves the latest release; older `.deb`s stay attached to each [GitHub Release](https://github.com/rapastranac/gempba/releases). Register the repo once, then install:

```bash
# 1. Trust the gempba signing key (one-time)
sudo install -d -m 0755 /etc/apt/keyrings
curl -fsSL https://rapastranac.github.io/gempba/gempba-archive-keyring.gpg \
  | sudo tee /etc/apt/keyrings/gempba.gpg > /dev/null

# 2. Register the repo (one-time)
echo "deb [signed-by=/etc/apt/keyrings/gempba.gpg] https://rapastranac.github.io/gempba noble main" \
  | sudo tee /etc/apt/sources.list.d/gempba.list > /dev/null
sudo apt update

# 3. Install
sudo apt install libgempba-dev          # the core
sudo apt install libgempba-mpi-dev      # the MPI component, as source; with libopenmpi-dev or libmpich-dev
```

Ubuntu 24.04's MPICH package starts every rank of a job alone ([Launchpad bug 2072338](https://bugs.launchpad.net/ubuntu/+source/mpich/+bug/2072338)), so use Open MPI there.

**MSYS2 (UCRT64)** — packages are attached to each GitHub Release rather than served from a custom pacman repo. Download the two `.pkg.tar.zst` assets — their names carry the version (e.g. `mingw-w64-ucrt-x86_64-gempba-4.4.0-1-any.pkg.tar.zst`) — from the [latest release](https://github.com/rapastranac/gempba/releases/latest), then install them from an MSYS2 UCRT64 shell:

```bash
pacman -U mingw-w64-ucrt-x86_64-gempba-<version>-any.pkg.tar.zst       # the core
pacman -U mingw-w64-ucrt-x86_64-gempba-mpi-<version>-any.pkg.tar.zst   # the MPI component, as source; depends on the core and MS-MPI
```

If you'd rather build from `PKGBUILD`, in the same shell:

```bash
curl -LO https://raw.githubusercontent.com/rapastranac/gempba/main/packaging/msys2/PKGBUILD
MINGW_ARCH=ucrt64 makepkg-mingw -si
```

**macOS** — install from the project's Homebrew tap:

```bash
brew tap rapastranac/gempba
brew install gempba       # the core
brew install gempba-mpi   # the MPI component, as source; installs gempba too
brew install open-mpi     # or mpich: the MPI your build compiles it against
```

### Maven dependency (Java)

The JARs are on Maven Central as two artifacts that share one API: `gempba`, whose native library has no MPI, and `gempba-mpi`, which adds the `io.gempba.mpi` package for creating an MPI scheduler and requires an MPI runtime on the host. A program uses one or the other. Each JAR carries every supported OS's native binary, and the loader picks the right one at runtime. The JAR needs Java 25 or newer, and its natives need nothing beyond the OS (Linux with glibc 2.28 or newer, such as RHEL 8; Windows 10 or newer; macOS 14 or newer) and, for `gempba-mpi`, the MPI runtime. A JAR has to carry its native library compiled, so unlike the C++ packages, `gempba-mpi` is built against one MPI per OS: Open MPI on Linux and macOS, MS-MPI on Windows, and runs only with that one.

No repository or token to configure:

```xml
<dependency>
  <groupId>io.github.rapastranac</groupId>
  <artifactId>gempba</artifactId>        <!-- or gempba-mpi -->
  <version>VERSION</version>
</dependency>
```

**Available versions: `v5.0.0` and later.** `v4.1.0` to `v4.4.0` are on [GitHub Packages](https://maven.pkg.github.com/rapastranac/gempba) as `io.gempba:gempba` with the `mt` and `mp-mpi` classifiers, which needs a token with the `read:packages` scope.

Worked example: **[gempba-java-examples](https://github.com/rapastranac/gempba-java-examples)**. Full reference in the [docs site](https://rapastranac.github.io/gempba-docs/).

### From source (C++)

When pre-built packages aren't an option (custom configure flags, unsupported distro, contributing back), build the C++ library with CMake and install it:

```bash
cmake -B build                          # also compiles the MPI component, for gempba's own tests
cmake -B build -DGEMPBA_BUILD_MPI=OFF   # needs no MPI; either way, the MPI component installs as source
cmake --build build --parallel
sudo cmake --install build
```

## Using gempba

Ask for the core, and for the MPI component when a program uses MPI:

```cmake
find_package(gempba REQUIRED)                  # gempba::gempba, the core
find_package(gempba REQUIRED COMPONENTS mpi)   # adds gempba::mpi, compiled here against the MPI CMake finds

target_link_libraries(my_app PRIVATE gempba::gempba)
target_link_libraries(my_mpi_app PRIVATE gempba::mpi)   # brings gempba::gempba and MPI with it
```

libgempba is a shared library that keeps its own dependencies (spdlog, fmt, hwloc) inside, so a consumer needs only the C++ standard library, plus MPI for `gempba::mpi`. `gempba::mpi` is compiled in your build with your compiler, which must be compatible with the one that built libgempba, as for any C++ code that uses it. Each build directory compiles it against the MPI CMake finds there, so switching between MPIs takes another build directory, not a reinstall.

The API is the same for every program. An MPI program creates its scheduler from `<gempba/mpi.hpp>` and passes it to the load balancer and the node manager:

```cpp
#include <gempba/gempba.hpp>
#include <gempba/mpi.hpp>  // MPI programs only

auto* scheduler = gempba::mpi::create_scheduler(gempba::mpi::CENTRALIZED);
auto* lb = gempba::create_load_balancer(gempba::QUASI_HORIZONTAL, scheduler);
auto& nm = gempba::create_node_manager(lb, scheduler);
```

A multithreading program writes the last two lines without the scheduler. The core and the MPI component link into one program together.

## Examples

Working example programs live in the sibling repo **[gempba-examples](https://github.com/rapastranac/gempba-examples)**, where they consume gempba via `find_package(gempba)` exactly as a downstream user would. Every PR on this repo builds that example tree, the multithreading and the MPI examples, against an installed copy of the PR's gempba, so the install/`gempbaConfig.cmake`/exported-headers chain is exercised on every change.

## Requirements

| Dependency | Version | Notes |
|---|---|---|
| C++ compiler | C++23 | GCC or Clang |
| CMake | ≥ 3.28 | |
| fmt | ≥ 9.1 | Build only; compiled into the library |
| spdlog | ≥ 1.12 | Build only; compiled into the library |
| hwloc | ≥ 2.10 | Hardware topology; `GEMPBA_HWLOC`, on by default |
| pkg-config | any recent | Finds hwloc in the build |
| MPI | Open MPI ≥ 4.0, MPICH, or MS-MPI on Windows | The MPI component only, compiled against it in your build |
| Boost | any recent | Tests only |
| GoogleTest | any recent | Tests only |

## Documentation

Installation, quick-start, and full API reference are at:

**[GemPBA-Docs/](https://rapastranac.github.io/gempba-docs/)**

## Copyright and Citing

Copyright © 2021-2026 [Andrés Pastrana](https://www.linkedin.com/in/andrepas/). Licensed under the [MIT license](https://github.com/rapastranac/gempba/blob/main/LICENSE).

If you use GemPBA in software or research of any kind, please include a link to [the GitHub repository](https://github.com/rapastranac/gempba) in your source code and documentation.

If you publish results obtained with GemPBA, please also cite the paper:

```bibtex
@article{PASTRANACRUZ2023103024,
    archiveprefix = {arXiv},
    title = {A lightweight semi-centralized strategy for the massive parallelization of branching algorithms},
    journal = {Parallel Computing},
    volume = {116},
    pages = {103024},
    year = {2023},
    issn = {0167-8191},
    doi = {https://doi.org/10.1016/j.parco.2023.103024},
    url = {https://www.sciencedirect.com/science/article/pii/S0167819123000303},
    author = {Andres Pastrana-Cruz and Manuel Lafond},
    keywords = {Load balancing, Vertex cover, Parallel algorithms, Scalable parallelism, Branching algorithms}
}
```

The [paper](https://doi.org/10.1016/j.parco.2023.103024) and [arXiv preprint](https://arxiv.org/abs/2305.09117) do not reflect the most recent library updates. For the latest documentation, see the [docs site](https://rapastranac.github.io/gempba-docs/).
