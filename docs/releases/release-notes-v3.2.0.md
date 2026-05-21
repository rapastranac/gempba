# v3.2.0

<small>May 21, 2026 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v3.2.0)</small>

CI hardening and release-flow automation. No library API or behavior changes.

**Fixed**

- `publish-apt-repo` no longer depends on the GitHub CLI being on the runner image — it consumes the `.deb` from the same workflow's artifact instead of `gh release download`, so self-hosted Linux runners can publish on tag pushes again

**Build**

- `BS::thread_pool` pinned to release tag `v5.1.0.1` instead of tracking `master` for reproducible builds
- CI `clang-tidy` bumped from 18 to 19; `bugprone-throwing-static-initialization` enabled — `debug_logger_initializer`'s ctor is now `noexcept` with the `spdlog` calls wrapped in `try/catch`, preserving the static-init injection contract while making it honest
- `update-pkgbuild.sh` accepts `-r / --remote` to select which git remote's tag to hash against (defaults to `origin`); useful when cutting a release pointed at the public repo
- New `Prepare release` workflow (`workflow_dispatch`): branches `prep/v<version>` from the resolved target branch, bumps `CMakeLists.txt` + `packaging/msys2/PKGBUILD`, drafts `docs/releases/release-notes-v<version>.md` from `git log`, and opens the prep PR. Auto-detects the target branch (or accepts a `target_branch` input / `vars.RELEASE_TARGET_BRANCH` override) so the same workflow runs on forks with only `main` and on forks with both `main` and `release`
- New `Post-tag PKGBUILD sha256 cleanup` workflow (`push tags: v*`): replaces the prep PR's `'SKIP'` placeholder with the real sha of the source tarball; opens a follow-up PR. Auto-resolves the target branch via tag-commit reachability when both `main` and `release` exist
- New `set-release-body` job in `c-cpp-ubuntu.yml`: applies the curated `docs/releases/release-notes-v<tag>.md` to the GitHub Release body via `gh release edit --notes-file`; fails loudly if the notes file is missing so no tag ships without curated notes
- New `setup-gh-cli` composite action: pinned `gh` on PATH for self-hosted Linux runners; no-op on github-hosted runners where `gh` is pre-installed
