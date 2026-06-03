# v4.1.3

<small>June 2, 2026 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v4.1.3)</small>

A telemetry-reachability release. Two fixes, both about getting at telemetry from outside the C++ runtime: C++ consumers can reach it through the umbrella header again, and the `configure_port` control is finally callable from the C ABI and Java. No runtime or scheduler API changes.

**Fixed**

- **`gempba::telemetry::*` is reachable through `<gempba/gempba.hpp>` again.** The umbrella header didn't surface telemetry, so consumer code that included only `<gempba/gempba.hpp>` failed to compile `gempba::telemetry::enable()` with `no member named 'telemetry' in namespace 'gempba'` — and IDEs "fixed" it by inserting a deep, machine-specific include into the telemetry subheader. The umbrella now includes it, so the full public telemetry surface compiles from the one header consumers already use (#314)

**Added**

- **`configure_port` across the bindings.** The telemetry kill switch (`enable` / `disable` / `is_enabled`) was already mirrored in the C ABI, JNI, and Java, but `configure_port` — the call that moves the telemetry TCP port off the default `127.0.0.1:9000` — had been left behind. It is now exposed as `gempba_telemetry_configure_port` (C ABI) and `GemPBA.configureTelemetryPort(int)` (Java, both flavors, with a `0..65535` range check). As in C++, call it before the first `create_*`: the hub captures the port once, when it installs (#315)
