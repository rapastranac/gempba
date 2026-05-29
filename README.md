# Generic Massive Parallelisation of Branching Algorithms

[![C/C++ CI (Ubuntu 24.04)](https://github.com/rapastranac/gempba/actions/workflows/ci-cpp-ubuntu.yml/badge.svg)](https://github.com/rapastranac/gempba/actions/workflows/ci-cpp-ubuntu.yml)
[![C/C++ CI (Windows 2025)](https://github.com/rapastranac/gempba/actions/workflows/ci-cpp-windows.yml/badge.svg)](https://github.com/rapastranac/gempba/actions/workflows/ci-cpp-windows.yml)
[![C/C++ CI (macOS 26)](https://github.com/rapastranac/gempba/actions/workflows/ci-cpp-macos.yml/badge.svg)](https://github.com/rapastranac/gempba/actions/workflows/ci-cpp-macos.yml)
[![Lint](https://github.com/rapastranac/gempba/actions/workflows/ci-lint.yml/badge.svg)](https://github.com/rapastranac/gempba/actions/workflows/ci-lint.yml)
[![codecov](https://codecov.io/gh/rapastranac/gempba/branch/main/graph/badge.svg)](https://codecov.io/gh/rapastranac/gempba)
![GitHub License](https://img.shields.io/github/license/rapastranac/gempba)
![GitHub Release](https://img.shields.io/github/v/release/rapastranac/gempba)

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

## Platforms

- Linux
- Windows
- macOS

Pre-built packages and the Java fat JAR target one architecture per OS:

| OS | Architecture |
|---|---|
| Linux | `x86_64` |
| Windows | `x86_64` |
| macOS | `aarch64` (Apple Silicon) |

Other architectures work via source build.

## Installing

GemPBA ships two distinct flavors that can coexist on a single machine. Multithreading is the default — fast local iteration, no MPI needed. Install the MPI flavor on top when you need to scale across nodes.

### Pre-built packages (C++)

**Debian / Ubuntu** — the `.deb`s live in a signed APT repository hosted at `https://rapastranac.github.io/gempba`. Register the repo once, then install:

```bash
# 1. Trust the gempba signing key (one-time)
sudo install -d -m 0755 /etc/apt/keyrings
curl -fsSL https://rapastranac.github.io/gempba/gempba-archive-keyring.gpg \
  | sudo tee /etc/apt/keyrings/gempba.gpg > /dev/null

# 2. Register the repo (one-time)
echo "deb [signed-by=/etc/apt/keyrings/gempba.gpg] https://rapastranac.github.io/gempba stable main" \
  | sudo tee /etc/apt/sources.list.d/gempba.list > /dev/null
sudo apt update

# 3. Install
sudo apt install libgempba-dev          # multithreading flavor (default)
sudo apt install libgempba-mpi-dev      # MPI flavor; depends on libgempba-dev
```

**MSYS2 / MinGW** — packages are attached to each GitHub Release rather than served from a custom pacman repo. Download the two `.pkg.tar.zst` assets — their names carry the version (e.g. `mingw-w64-x86_64-gempba-4.1.1-1-any.pkg.tar.zst`) — from the [latest release](https://github.com/rapastranac/gempba/releases/latest), then install them locally:

```bash
pacman -U mingw-w64-x86_64-gempba-<version>-any.pkg.tar.zst       # multithreading (default)
pacman -U mingw-w64-x86_64-gempba-mpi-<version>-any.pkg.tar.zst   # MPI; depends on the mt package above
```

If you'd rather build from `PKGBUILD`:

```bash
curl -LO https://raw.githubusercontent.com/rapastranac/gempba/main/packaging/msys2/PKGBUILD
makepkg -si
```

**macOS** — install from the project's Homebrew tap:

```bash
brew tap rapastranac/gempba
brew install gempba       # multithreading (default), or `brew install gempba-mpi` for MPI
```

To keep **both** flavors on one machine, install the second after unlinking the first, then point each project at the flavor it uses:

```bash
brew unlink gempba && brew install gempba-mpi
cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix gempba)       # a project built against mt
cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix gempba-mpi)   # a project built against mpi
```

### Maven dependency (Java)

JARs are published to [GitHub Packages](https://maven.pkg.github.com/rapastranac/gempba) with one classifier per flavor: `mt` (multithreading) and `mp-mpi` (multiprocessing — requires an MPI runtime on the host). One fat JAR per classifier carries every supported OS's native binary; the loader picks the right one at runtime.

**Available versions: `v4.1.0` and later.** Earlier tags predate the publish flow and have no artifact on the registry.

Add the registry to your `pom.xml`:

```xml
<repositories>
  <repository>
    <id>github-gempba</id>
    <url>https://maven.pkg.github.com/rapastranac/gempba</url>
  </repository>
</repositories>
```

GitHub Packages requires a token even for public packages. Create a PAT at [github.com/settings/tokens](https://github.com/settings/tokens) with the `read:packages` scope and add it to `~/.m2/settings.xml`:

```xml
<servers>
  <server>
    <id>github-gempba</id>
    <username>YOUR_GITHUB_USERNAME</username>
    <password>YOUR_PAT</password>
  </server>
</servers>
```

Declare the dependency with the variant classifier:

```xml
<dependency>
  <groupId>io.gempba</groupId>
  <artifactId>gempba</artifactId>
  <version>VERSION</version>
  <classifier>mt</classifier>          <!-- or mp-mpi -->
</dependency>
```

Worked example: **[gempba-java-examples](https://github.com/rapastranac/gempba-java-examples)**. Full reference in the [docs site](https://rapastranac.github.io/gempba-docs/).

### From source (C++)

When pre-built packages aren't an option (custom configure flags, unsupported distro, contributing back), build the C++ library with CMake. Pick the flavor at configure time and install:

```bash
cmake -B build -DGEMPBA_MULTIPROCESSING=ON   # MPI
cmake -B build -DGEMPBA_MULTIPROCESSING=OFF  # multithreading
cmake --build build --parallel
sudo cmake --install build
```

## Selecting a flavor

Consumer code is identical regardless of flavor. Pick at `find_package` time:

```cmake
find_package(gempba REQUIRED)                  # default: mt
find_package(gempba REQUIRED COMPONENTS mt)    # explicit mt
find_package(gempba REQUIRED COMPONENTS mpi)   # mpi (requires libgempba-mpi-dev installed)

target_link_libraries(my_app PRIVATE gempba::gempba)
```

The same target name `gempba::gempba` is exported by both flavors, so the link line never changes. The `GEMPBA_MULTIPROCESSING` macro flows through the target's interface and `<gempba/gempba.hpp>` exposes the matching API at compile time.

In code, write the short form:

```cpp
auto* lb = gempba::create_load_balancer(gempba::QUASI_HORIZONTAL /*, worker* if MP*/);
auto& nm = gempba::create_node_manager(lb /*, worker* if MP*/);
```

The explicit `gempba::multithreading::*` and `gempba::multiprocessing::*` qualifiers are also available for code that wants to be unambiguous.

The two flavors are **mutually exclusive within a single binary**: they share mode-agnostic top-level symbols (`gempba::shutdown`, `gempba::get_load_balancer`, …) and would ODR-clash at link time. `find_package(gempba COMPONENTS mt mpi)` is therefore rejected up front with a clear diagnostic. A project that genuinely needs both flavors — say, an MT debug runner and an MPI cluster runner — splits into two executables, each `find_package`-ing the flavor it needs.

## Examples

Working example programs live in the sibling repo **[gempba-examples](https://github.com/rapastranac/gempba-examples)**, where they consume gempba via `find_package(gempba)` exactly as a downstream user would. Every PR on this repo builds that example tree against an installed copy of the PR's gempba (matrix on `multiprocessing: [ON, OFF]`), so the install/`gempbaConfig.cmake`/exported-headers chain is exercised on every change.

## Requirements

| Dependency | Version | Notes |
|---|---|---|
| C++ compiler | C++23 | GCC or Clang |
| CMake | ≥ 3.28 | |
| OpenMPI | ≥ 4.0 | Multiprocessing only |
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
