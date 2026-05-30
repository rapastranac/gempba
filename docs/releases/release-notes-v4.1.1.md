# v4.1.1

<small>May 30, 2026 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v4.1.1)</small>

A pure install-fixes release. v4.1.0 shipped the packages, but several of the documented install paths didn't actually work on a clean machine — the Homebrew build failed in the sandbox, the apt repo's signing key was never published, the MSYS2 download link pointed at a filename that doesn't exist. v4.1.1 makes every install command in the README work end-to-end, on all three platforms.

No library or API changes. If v4.1.0 already installed and built for you, nothing here affects your code — upgrade only if you hit one of the install problems below.

**Fixed**

- **`brew install` now works.** The v4.1.0 formula died inside Homebrew's build sandbox — CPM/FetchContent tried to fetch `BS_thread_pool` over the network at build time, which the sandbox blocks. Those build deps are now vendored into the formula, so it builds offline (#288)
- **`apt install` no longer fails signature verification.** The signed APT repo never exposed its public key, so `apt update` rejected it with `NO_PUBKEY 7C5B392E…`. The release now publishes the signing key (`gempba-archive-keyring.gpg`) alongside the index, so the documented `signed-by=` setup resolves (#286)
- Dropped an unused OpenMP dependency the macOS formula pulled in for nothing (#289)

**Docs**

- Rewrote the README's pre-built-package install instructions — every platform's block was broken or misleading (#286):
    - **APT** — added the one-time key-trust + repo-registration steps before `apt install`.
    - **MSYS2** — the old `releases/latest/download/<fixed-name>` link 404'd (release asset names carry the version); now points at the Releases page + `pacman -U`.
    - **Homebrew** — fixed the literal `<owner>` placeholder, and clarified that both flavors install side by side (only one is *linked* at a time; each project points at `$(brew --prefix gempba)` / `gempba-mpi`).
    - Named the two flavors (`mt` / `mpi` / `mp-mpi`) up front so the section's recurring terms map to just two things.

**Build / CI**

- The Homebrew formula is now built and `brew test`-ed on a real macOS runner on every PR and release, so a non-installable formula blocks the release instead of shipping (#292)
- `prepare-release` bumps the Homebrew formula templates in lockstep with the other manifests — and actually stages them, so the bump lands on the prep branch (#297)
- Hardened the macOS formula-verify job: pinned Homebrew off mid-run auto-update (which broke its ephemeral tap) and serialized the two-flavor matrix so they stop colliding on the shared prefix (#303)
