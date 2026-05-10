# v1.1.0

<small>August 4, 2025 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v.1.1.0)</small>

Citation metadata, a refreshed README, and dependency-management changes that consumers need to mirror.

> The git tag is `v.1.1.0` (extra dot — original spelling preserved). Everywhere else the version is referred to as `v1.1.0`.

**Added**

- `CITATION.cff` and `CITATION.bib` at the repo root for academic citation
- README sections for Requirements, Platforms, Dependency Management, Copyright and citing
- `FUNCTION_ARGS` tag for routing serialized function arguments separately from other messages
- `REFERENCE_VAL_PROPOSAL` and `REFERENCE_VAL_UPDATE` tags (split from the former single reference-value tag)

**Changed**

- Inter-process tags in `MPI_Modules/MPI_Scheduler.hpp` are now an `enum tags { ... }` (`CENTER_NODE`, `RUNNING_STATE`, `ASSIGNED_STATE`, `AVAILABLE_STATE`, `TERMINATION`, `REFERENCE_VAL_PROPOSAL`, `REFERENCE_VAL_UPDATE`, `NEXT_PROCESS`, `HAS_RESULT`, `NO_RESULT`, `FUNCTION_ARGS`) replacing the previous `#define` macros (`STATE_RUNNING`, `STATE_ASSIGNED`, `STATE_AVAILABLE`, `TERMINATION_TAG`, `REFVAL_UPDATE_TAG`, `NEXT_PROCESS_TAG`, `HAS_RESULT_TAG`, `NO_RESULT_TAG`); consumers relying on the old macro names must rename to the enum values
- README installation walkthrough moved above the description; CMake snippet now sets `GEMPBA_MULTIPROCESSING`, `GEMPBA_DEBUG_COMMENTS`, `GEMPBA_BUILD_EXAMPLES`, `GEMPBA_BUILD_TESTS` cache variables and links `gempba::gempba`

**Fixed**

- `examples/include/VertexCover.hpp` include switched from `fmt/core.h` to `<format>` (matches the C++20 toolchain)

**Build**

- CMake project version bumped to `1.1.0`
- `spdlog` removed from core CMake: `find_package(spdlog REQUIRED)` and `spdlog::spdlog` link are gone; consumers add it via CPM in `external/CMakeLists.txt` (pinned to `gabime/spdlog@1.15.1`, built static)
- `argparse` CPM entry rewritten in long form (`NAME argparse / GITHUB_REPOSITORY p-ranav/argparse / VERSION 3.0`); `external_libs` now also exports `spdlog`
- `examples/CMakeLists.txt` sets `Boost_USE_STATIC_LIBS ON` so Boost is linked statically into the example binaries
