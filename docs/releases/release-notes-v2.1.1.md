# v2.1.1

<small>September 1, 2025 · [GitHub ↗](https://github.com/rapastranac/gempba/releases/tag/v2.1.1)</small>

Single-fix patch release for the centralized MPI scheduler.

**Fixed**

- `mpi_centralized_scheduler` worker `probe_reference_value_comm()` was probing `REFVAL_PROPOSAL_TAG` (the worker-to-center tag) instead of `REFVAL_UPDATE_TAG`, so global reference-value updates broadcast by the center were never picked up by workers — leaving them with stale bounds and exploring branches that should have been pruned ([#55](https://github.com/rapastranac/gempba/pull/55))
