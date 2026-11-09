# v4.4.0

<small>2026-11-09 · [tag](https://github.com/rapastranac/gempba/releases/tag/v4.4.0)</small>

<!-- TODO: tagline -->

## Changed

- #411 build(#26, #186, #188): Describe GemPBA as a hybrid parallel framework
- #411 docs(#36, #283): State the Java version and every build dependency in the README
- #411 build(#188): Drop Boost from the Homebrew formulas
- #411 build(#26): Publish the .debs in a noble suite instead of stable
- #411 fix(#26): Declare the C and C++ runtimes the .deb needs
- #411 fix(#208): Install pkg-config with every gempba package
- #408 ci(#186, #187): Test the Java natives on clean machines
- #408 build(#186): Make the JNI library depend only on the OS and MPI
- #408 build(#33): Compile gempba without MPI's C++ bindings
- #408 fix(#412): Compile with the definitions of the spdlog package gempba finds
- #408 fix(#26, #45): Link std::stacktrace's library only where the toolchain has it
- #407 build(#186): Build the Java JARs from a UCRT64 shell
- #407 build(#26, #265, #286): Build and release the MSYS2 packages for UCRT64
- #407 ci(#45, #216, #271): Build the Windows CI on UCRT64
- #412 test: Cover the public headers' logging with debug output on
- #412 build(#26, #188): Keep spdlog internal to libgempba and out of gempba's packages
- #412 refactor(#45): Keep the public headers to the standard library
- Update PKGBUILD sha256 for v4.3.0

<!-- TODO: curate. Drop CI / internal items, rewrite for end-users.
     Add Added / Fixed / Removed / Build sections as applicable.
     Major releases also get a Breaking changes section. -->
