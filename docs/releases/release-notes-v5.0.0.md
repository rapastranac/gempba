# v5.0.0

<small>2026-11-14 · [tag](https://github.com/rapastranac/gempba/releases/tag/v5.0.0)</small>

<!-- TODO: tagline -->

## Changed

- #470 docs(#414): Name the gempba and gempba-mpi artifacts in the Javadoc
- #409 docs(#397): List the macOS natives as Apple Silicon only
- #409 ci(#188): Verify the Homebrew formulas by hand, without a tag, and publish nothing
- #409 ci(#274): Fail the fat-JAR job unless each JAR holds exactly the Linux, Windows and Apple Silicon natives
- #409 ci(#311): Test the macOS natives with only the JDK and, for MP, Open MPI installed
- #409 ci(#402): Run the macOS jobs only on the way into main or release, and by hand
- #409 ci(#387): Run the macOS jobs on RUNNER_MACOS_ARM64, Apple Silicon by default
- #465 feat(#45, #394): Log how the run ended
- #465 feat(#443): Stop a Java run gracefully on SIGINT and SIGTERM by default
- #465 feat(#394): Expose the stop call and the run's outcome in Java
- #465 feat(#394): Expose the stop calls and the run's outcome in the C ABI
- #465 feat(#173, #370): Let a dashboard client stop the run with the run's token
- #465 feat(#338, #389): Say how the run ended in the final telemetry line
- #465 feat(#45, #352): Wind every rank down together after a stop under MPI
- #465 fix(#45): Probe the MPI communicators in turn so score updates can't starve the termination message
- #465 feat(#394): Return how the run ended from scheduler::run
- #465 feat(#45): Stop the run gracefully on SIGINT and SIGTERM by default
- #465 feat(#45): Prune every node not yet started once a stop is requested
- #464 chore(#173): Leave the telemetry server's untestable listen failures out of coverage
- #464 fix(#173): Stop two runs on Windows from listening on one telemetry port
- #464 fix(#173): Warn when the telemetry server can't listen on its port
- #458 test(#314): Set the telemetry port with only <gempba/gempba.hpp>
- #462 docs(#45): Point the quasi-horizontal balancer's tree comments at the docs scenarios
- #462 test(#45): Mark the running branch in two root-correction tests and assert their states
- #462 test(#45): Add one test per quasi-horizontal tree scenario the docs draw
- #459 ci: Report C++ coverage to Codecov through gcovr, without exception branches
- #459 fix(#45): Submit a root that still has children without throwing
- #459 test(#45): Cover the quasi-horizontal balancer's skipped forwards and telemetry count
- #454 fix(#173): Load the hwloc topology once per node probe
- #456 docs(#45): Document that sibling branches may be forwarded and submitted in any order
- #456 fix(#45): Never hand a running node to another thread
- #456 fix(#45): Return true when a balancer already dispatched the submitted node
- #452 fix(#186): Keep a parent Node reachable while native code creates its child
- #450 ci(#45): Run every C++ test once, with no FLAKY_ retries
- #450 test(#45): Give the root-correction stress test a pool that can't fill up
- #447 fix(#173): Send a sentinel's last node frame before its final rank frame
- #446 docs(#442): Point Which MPI at build_jar_host_linux.sh
- #446 build: Add build_jar_host_linux.sh, which builds the JAR without Docker
- #446 fix(#408): Build only hwloc's library and headers in build_deps.sh
- #443 docs(#442): Drop the README's UCX_ERROR_SIGNALS note
- #443 fix(#186): Give the JVM back the signal handlers UCX takes
- #442 docs(#415): Say which MPI gempba-mpi is built for and how to use another
- #442 feat(#186): Name the MPI a bundled native library needs when it fails to load
- #442 feat(#186): Load the native library that gempba.native.path names
- #410 docs(#186): Point the README's Java section at Maven Central
- #410 ci(#187): Release the Java artifacts to Maven Central and check the bundle on every PR
- #410 build(#186): Publish the two Java variants as io.github.rapastranac:gempba and gempba-mpi
- #410 fix(#414): Drop the javadoc link to the removed MT natives class
- #436 refactor(#414, #415): Declare each component in its own directory with gempba_add_component
- #436 refactor(#414, #415): Move each component's sources and private headers into its directory
- #438 fix(#415): Install only the core in the gempba formula
- #438 fix(#414): Name the tap gempba-mpi depends on gempba from
- #415 docs(#414): Describe the MPI component as source compiled in the consumer's build
- #415 ci: Build and run an MPICH consumer against an installed gempba
- #415 build(#414): Ship the MPI packages as source
- #415 build(#414): Install the MPI component as source and compile it in the consumer's build
- #433 ci(#311): Build gempba-examples from the pull request's base branch when no same-named branch exists
- #433 test(#352): Check rank 0's best result in the termination stress test
- #433 fix: Propose the last score before a centralized MPI worker goes idle
- #414 fix(#185): Leave the scheduler to gempba_shutdown in gempba_scheduler_destroy
- #414 docs(#180): Describe the core and the MPI component instead of two flavors
- #414 ci(#180, #292, #402): Build and install gempba once in CI, with the MPI component
- #414 build(#26, #180, #188): Package the core and the MPI component from one build
- #414 feat(#186): Give Java one API, with MPI's scheduler in io.gempba.mpi
- #414 feat(#185): Give the C ABI one set of factories without mt and mp prefixes
- #414 build(#180, #184): Build a shared core and a shared MPI library instead of one flavor
- #414 refactor(#412): Log the MPI schedulers through gempba's logging functions
- #414 feat(#184): Give C++ one API and move MPI's own calls to gempba::mpi
- #414 refactor(#173, #182): Let a scheduler's creator give telemetry its transport
- #414 refactor(#173, #327): Gather the topology through the telemetry transport
- #414 refactor(#173): Stop publishing the telemetry transport interface
- #394 docs(#393): Say what a custom scheduler implements and how it reports its role
- #394 feat(#391): Have each rank report its own role in its identity
- #394 feat(#186): Run every Java rank through Scheduler.run
- #394 feat(#45, #185): Replace the scheduler's center and worker views with one run on every rank
- #373 refactor(#396): Name telemetry's transport, codec and serializer for ranks
- #373 refactor(#396): Name telemetry's frame and topology types and fields for ranks
- #373 refactor(#342): Remove the total_requested_tasks stat
- #373 refactor(#384, #396): Remove telemetry_hub's deprecated interval and frame calls
- #373 refactor(#367, #396): Drop the schema-1 interval control lines
- #428 fix(#341): Wait for every pool thread to go idle in the load balancer constructors
- #353 refactor(#352): Remove the unused scheduler timeout parameter
- Update PKGBUILD sha256 for v4.4.0

<!-- TODO: curate. Drop CI / internal items, rewrite for end-users.
     Add Added / Fixed / Removed / Build sections as applicable.
     Major releases also get a Breaking changes section. -->
