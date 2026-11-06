# GitHub Actions workflows

All CI, release, and reusable workflows live in this directory.
GitHub requires workflow files to sit directly under `.github/workflows/`
— subdirectories are ignored — so the only organisational tool we have
is **filename prefixes**.  We use them so the alphabetical listing
groups workflows by purpose.

| Prefix              | Meaning                                                                 | Triggered by                                |
|---------------------|-------------------------------------------------------------------------|---------------------------------------------|
| `_*.yml`            | Reusable building block (`on: workflow_call`); never runs on its own    | Another workflow                            |
| `ci-*.yml`          | Continuous integration (build / test / lint on push or PR)              | `push`, `pull_request`                      |
| `release-*.yml`     | Release pipeline (build artifacts, publish, update downstream packages) | tag push, `workflow_call`, `workflow_dispatch` |
| `prepare-release.yml` | Manually-invoked release-prep helper (version bump, PR scaffold)      | `workflow_dispatch`                         |

## Current inventory

### Reusable building blocks
- `_build-apt-index.yml` — rebuild the apt repository index after new debs land
- `_build-deb.yml` — build the gempba `.deb` package
- `_build-msys2.yml` — build the gempba MSYS2 PKGBUILD artefact

### CI
- `ci-cpp-ubuntu.yml` — C/C++ test (MP=ON, coverage, MPI integration tests) on Ubuntu 24.04, gempba-examples built against both installed flavors, and the deb + apt-index reusables
- `ci-cpp-windows.yml` — C/C++ test and gempba-examples on Windows Server 2025 via MSYS2 mingw-w64, and the MSYS2 package
- `ci-cpp-macos.yml` — C/C++ test and gempba-examples on macOS 26 with Homebrew toolchain
- `ci-java.yml` — JNI shared library + Java JAR build + test across Ubuntu / Windows / macOS, MT/MP matrix
- `ci-lint.yml` — clang-format + clang-tidy

### When CI runs what

| Event | Runs |
|---|---|
| PR into any branch | Lint; per OS, the C/C++ test job, the gempba-examples job, and the Java build and test jobs |
| PR into `main` or `release`, push to them | The above, plus the deb + apt index, the MSYS2 package and the multi-platform JARs |
| PR that changes only `docs/` or Markdown | Nothing. The C/C++ workflows still run when `docs/telemetry/wire-format.md` changes, because `wire_format_doc_test` reads it |
| `v*` tag | The `release-*` workflows |

Each test job publishes its own JUnit report. The Java tests keep their own jobs so that a flaky test reruns without rebuilding the native library. Push triggers carry no path filter, so the README badges and the Codecov main graph always get a run.

### Release
- `prepare-release.yml` — bumps versions, generates PKGBUILD/deb manifests, opens the release PR
- `release-ubuntu.yml` — packages and publishes the Ubuntu deb after a tag
- `release-windows.yml` — packages and publishes the Windows MSYS2 artefacts after a tag
- `release-arch-pkgbuild.yml` — opens a PR with the post-tag sha256 update to the Arch PKGBUILD
- `release-homebrew-tap.yml` — updates the Homebrew tap formula after a tag

## When to extract a reusable

Promote a sequence to a `_*.yml` (`on: workflow_call`) when:

- The same sequence appears in **two or more** top-level workflows.
- The sequence is well-bounded: clear inputs/outputs, no leaking env or
  filesystem state.
- The shared logic is substantial enough that the indirection cost is
  outweighed by the dedup (rough threshold: > ~20 lines).

For **shorter sequences that only set up environment**, prefer a
**composite action** under `.github/actions/<name>/action.yml`.  Composite
actions inline as steps in the calling job (logs / timings stay grouped),
are cheaper to invoke than a reusable workflow, and can be sourced from
multiple jobs in the same workflow without a separate runner allocation.

Today's composite actions:

- `setup-ubuntu-deps` — apt-installs the gempba native deps; optional GTest
- `setup-windows-deps` — MSYS2 + MS-MPI; optional GTest
- `setup-macos-deps` — Homebrew toolchain; optional GTest
- `setup-java-jdk` — pinned Temurin 25 + `~/.m2` cache, for any Java workflow
- `setup-gh-cli` — installs / authenticates the `gh` CLI

## Adding a new workflow

1. Pick the right prefix from the table above.
2. Set `name:` to a human-readable title (this is what the Actions tab shows).
3. Pin the runner via `runs-on: ${{ fromJSON(vars.RUNNER_<OS> || '[...]') }}`
   so the project's self-hosted runners pick the job up automatically.
4. If a similar sequence already exists, check whether extraction into
   `_*.yml` or `.github/actions/` is warranted (see above).
5. If the new workflow is meant to gate merges, also add it to the
   required-checks list in the repo's branch protection settings — and
   when renaming an existing workflow that gates merges, update that
   list **at the same time** so a green run on the new name still counts.

## Required-check rename hazard

GitHub's branch protection rules match required status checks by
**workflow + job name**, not by filename.  Renaming a workflow file alone
keeps required checks intact.  Renaming a *job* inside a workflow (e.g.
`build` → `build-ubuntu`) silently breaks the gate: the old check stays
"required but not reported", and PRs cannot merge until the rule is
edited.  Audit the branch protection settings whenever you rename a job.
