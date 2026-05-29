# v4.1.0

<small>May 28, 2026 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v4.1.0)</small>

**Java.** That's the headline.

gempba is now a Maven dependency. Drop it into a `pom.xml`, write `import io.gempba.*`, and call the same scheduler family, load balancer, and node manager you would from C++ — just from Java this time. One fat JAR per flavor (`mt`, `mp-mpi`) carries the native binaries for Linux, Windows, and macOS inside the artifact, so there is no per-platform build dance, no classifier juggling, no "first set `LD_LIBRARY_PATH`" footnote. You add the dependency, you build, you run.

That this is shippable at all is because of the second piece of `v4.1.0`: a stable C ABI underneath. The Java binding is the first consumer of it, but the same headers are what any future Python / Rust / .NET binding will sit on. If you have a JVM pipeline, a Spark job, an Airflow operator, an enterprise application — anywhere C++ was the wrong language but you still wanted gempba's scheduler — that bridge now exists.

Everything else in `v4.1.0` is the surrounding work: the publish pipeline, the README the new Java audience needs, a couple of bug fixes. No public C++ API changes — additive on top of `v4.0.0`.

**Added**

- Stable C ABI (`<gempba/cabi/gempba.h>`) — extern-C surface covering the full runtime: scheduler family, load balancer, node manager, factories, runnables, MP / MT entry points, per-node result retrieval. The same headers any non-C++ binding will reuse going forward (#185)
- Java JNI binding (`io.gempba:gempba`) — typed wrappers for `Node`, `LoadBalancer`, `NodeManager`, `RankStats`, `Score` / `ScoreType`, `Goal`, `BalancingPolicy`, the task / serializer / deserializer functional interfaces, the scheduler family (MP-only), and `GemPBA` entry points for both flavors (#186)
- Multi-platform fat JAR — each published JAR carries `natives/<os>-<arch>/` for Linux `x86_64`, Windows `x86_64`, and macOS `aarch64`. `NativeLoader` picks the right binary at runtime, so the same JAR runs unchanged on any supported platform. No per-OS classifier, no platform-specific Maven profiles (#274)
- Maven publication to GitHub Packages on every `v*` tag, with classifiers `mt` (multithreading) and `mp-mpi` (multiprocessing). Add the repo to your `settings.xml`, declare the dependency with the classifier you want, build (#187)
- README "Maven dependency (Java)" subsection — repository + `settings.xml` + dependency snippet, plus a cutoff note that Maven artifacts start at `v4.1.0` (earlier tags predate the publish flow). Platforms section gains a per-OS architecture table (#283)

**Fixed**

- `default_mpi_stats_visitor::labels()` was missing the `total_thread_requests` key — values were emitted but the label vector didn't cover them, breaking downstream consumers that zipped labels with values (#262)
- `set_thread_pool_size` could race against in-flight construction; the resize now round-trips a no-op task to fence the pool's worker threads before returning (#277)

**Build**

- CI workflows split into mutually exclusive triggers: `ci-*.yml` runs on PRs and branch pushes, `release-*.yml` runs on tags. Reusable building blocks factored out so tag-time publish reuses the exact build path CI exercises (#265)
- Workflow folder reorganized with `ci-` / `release-` prefixes, a `.github/workflows/README.md`, and shared composite actions extracted (#271)
- `prepare-release.yml` now bumps `bindings/java/pom.xml`'s `<version>` alongside the CMake / PKGBUILD bumps so the Java release version stays in lockstep with the C++ release version (#187)
- Lint workflow bumped to LLVM 22; the modernizations clang-tidy 22 flagged were applied (#261)
