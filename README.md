# Generic Massive Parallelisation of Branching Algorithms

[![C/C++ CI (Ubuntu 24.04)](https://github.com/rapastranac/gempba/actions/workflows/c-cpp-ubuntu.yml/badge.svg)](https://github.com/rapastranac/gempba/actions/workflows/c-cpp-ubuntu.yml)
[![C/C++ CI (Windows 2025)](https://github.com/rapastranac/gempba/actions/workflows/c-cpp-windows.yml/badge.svg)](https://github.com/rapastranac/gempba/actions/workflows/c-cpp-windows.yml)
[![C/C++ CI (macOS 26)](https://github.com/rapastranac/gempba/actions/workflows/c-cpp-macos.yml/badge.svg)](https://github.com/rapastranac/gempba/actions/workflows/c-cpp-macos.yml)
[![Lint](https://github.com/rapastranac/gempba/actions/workflows/lint.yml/badge.svg)](https://github.com/rapastranac/gempba/actions/workflows/lint.yml)
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

## Requirements

| Dependency | Version | Notes |
|---|---|---|
| C++ compiler | C++23 | GCC or Clang |
| CMake | ≥ 3.28 | |
| OpenMPI | ≥ 4.0 | Multiprocessing only |
| Boost | any recent | Examples and tests only |
| GoogleTest | any recent | Tests only |

## Documentation

Installation, quick-start, full API reference, and examples are all at:

**[GemPBA-Docs/](https://rapastranac.github.io/gempba-docs/)**

## Copyright and Citing

Copyright © 2021-2025 [Andrés Pastrana](https://www.linkedin.com/in/andrepas/). Licensed under the [MIT license](https://github.com/rapastranac/gempba/blob/main/LICENSE).

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
