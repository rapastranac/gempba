# v4.2.0

<small>June 16, 2026 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v4.2.0)</small>

A telemetry-on-real-clusters release. Watching a gempba run on an HPC compute node needs two things to be true at once: the per-node numbers have to be right, and you have to be able to reach them from your own machine. v4.2.0 delivers both — node telemetry now reports correct socket topology, CPU placement, and per-job memory on large multi-socket nodes, and the operator scripts can tunnel to a compute node through an MFA login node. The telemetry wire format only grows (additive cgroup-memory fields; schema version unchanged) and there are no public C++ API changes — additive on top of `v4.1.3`.

**Fixed**

- **Every node reports its real socket topology, not just the center.** Per-host topology is now gathered with a variable-length collective — each host's sentinel serializes its own layout, exchanged via `MPI_Allgatherv` and merged by hostname — so all nodes report their sockets, cores, and full per-socket CPU lists (including wide sockets such as 96-core EPYC), instead of only rank 0's (#327)
- **Workers land on their real socket, and CPUs past index 64 are no longer dropped.** The per-worker affinity bitmap grew from a single 64-bit word to a `MAX_LOGICAL_CPUS`-wide array, and each rank's primary socket is derived from its first allowed CPU through hwloc instead of being hardcoded to 0. On nodes with more than 64 logical CPUs this fixes the flat-zero per-allocation CPU% and the collapse of every rank onto socket 0 (#327)
- **Memory reflects the job, not the host.** The node frame now carries the process's memory-cgroup usage and limit, resolved at the cgroup that actually enforces the allocation (scheduler-agnostic, cgroup v1 and v2), so a shared node shows gempba's own footprint against its allocation instead of the whole machine's memory. Falls back to host totals when the process is unconstrained (#327)

**Added**

- **Reach telemetry on a compute node through an MFA login node.** `telemetry_tunnel --jump-host` nests the tunnel instead of using SSH `ProxyJump`: it forwards your machine to the login node, then runs the final hop *on* the login node so it rides the cluster's intra-cluster trust — one Duo prompt at the login node, no compute-node password, and gempba stays loopback-only. `accept-new` auto-trusts the per-job compute-node host key, and the readiness probe waits for a real byte rather than just a TCP accept. The mechanics are written up in `docs/remote-connection.md` (#325)
- **Bash ports of the operator scripts.** `scripts/telemetry_tunnel.sh` and `scripts/telemetry_view.sh` join the PowerShell versions, so the tunnel and live-watch workflows run the same from Linux and macOS shells (#325)

**Changed**

- **The telemetry operator scripts were renamed and simplified.** The helpers were renamed for clarity and the SLURM job-resolution path was dropped — pass the compute node directly (`--ssh-host` / `--jump-host`) instead of `--job <slurm-id>`. Update any wrappers that called the old names or relied on `squeue` resolution (#319)

**Docs**

- Corrected the release dates in the per-version release notes (#323)
