# Telemetry wire format

This is the reference for what gempba's telemetry stream carries: every key of a broadcast line, every control line a client can send, and every schema version, newest first. It describes **schema 2**, which gempba v4.3.0 sends. If you build an older release, find its schema under [Schema versions](#schema-versions); that schema stays valid for that release.

## The stream

- The center (rank 0, or the only process in a multithreading run) listens on `127.0.0.1` only, port 9000 unless the program calls `gempba::telemetry::configure_port` before creating the runtime. To watch a run on another machine, use the GemPBA Dashboard, which opens the SSH tunnel itself.
- Any number of clients can connect, and each receives the same lines. Each line is one JSON object followed by `\n`.
- A full line is self-contained. It carries the topology, the run setup and the latest frame from every rank and every host, so a client that connects late misses nothing but history.
- The center sends:
  - a full line every telemetry interval: 500 ms by default, never more often than every 50 ms, and changeable live with `set_worker_interval_ms`;
  - while paused, a [paused line](#paused-line) at the same cadence instead;
  - one full line as soon as a [snapshot](#control-lines) is answered;
  - a final full line at shutdown, even while paused, carrying the run's totals and marked `"final":true`. The center then closes the connection, so a connection that closes without that line ended some other way, such as a crash.
- Frame counters are cumulative since the process started telemetry. A consumer takes differences between lines to get rates.
- `first_task_ms`, `last_task_ms` and `last_score_improvement_ms` are milliseconds since the scheduler run started, and stay 0 until the event first happens. `elapsed_seconds` and a marker's `elapsed_ms` count from when telemetry came up. `worker_local_ms` and `sentinel_local_ms` are a rank's own monotonic clock.

## Example

A full line from a 2-rank centralized run on one Windows host, pretty-printed (the stream sends it on one line). The run annotation was set by the program; the marker was sent by a client.

```json
{
  "version": 2,
  "run_id": "6a5a9990-6f0f-4972-b6be-9d4bae4ead1e",
  "ts": 1793730847508,
  "elapsed_seconds": 1,
  "paused": false,
  "final": false,
  "topology": {
    "nodes": [
      {
        "hostname": "DESKTOP",
        "sentinel_worker_id": 0,
        "total_physical_cores": 24,
        "total_logical_cores": 24,
        "mem_total_bytes": 68051103744,
        "worker_ids": [0, 1],
        "sockets": [
          {
            "socket_id": 0,
            "name": "Intel(R) Core(TM) Ultra 9 285K",
            "physical_cores": 24,
            "logical_cores": 24,
            "cpu_ids": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23]
          }
        ]
      }
    ],
    "identities": [
      {
        "worker_id": 0,
        "hostname": "DESKTOP",
        "pid": 59172,
        "primary_socket": 0,
        "role": "center",
        "allowed_cpu_ids": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23]
      },
      {
        "worker_id": 1,
        "hostname": "DESKTOP",
        "pid": 14244,
        "primary_socket": 0,
        "role": "worker",
        "allowed_cpu_ids": [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23]
      }
    ],
    "run": {
      "scheduler": "centralized",
      "load_balancer": "quasi_horizontal",
      "runtime_mode": "multiprocessing",
      "world_size": 2,
      "threads_per_process": 2,
      "gempba_version": "4.3.0",
      "annotations": {
        "test": "telemetry_stress"
      },
      "markers": [
        {
          "elapsed_ms": 506,
          "label": "phase 2 started",
          "source": "dashboard"
        }
      ]
    }
  },
  "workers": [
    {
      "worker_id": 0,
      "seq_no": 4,
      "worker_local_ms": 106003119,
      "tasks_started_total": 0,
      "tasks_sent_total": 4312,
      "tasks_recv_total": 4690,
      "bytes_sent_total": 34496,
      "bytes_recv_total": 37520,
      "send_microseconds_total": 2556,
      "recv_microseconds_total": 1257,
      "spawn_rejected_pool_full": 0,
      "spawn_remote_rejected": 0,
      "tasks_running": 0,
      "scheduler_pending_count": 379,
      "idle_microseconds_per_worker": 1499714,
      "idle_microseconds_starved": 1499715,
      "idle_microseconds_imbalanced": 0,
      "sched_running_microseconds": 0,
      "sched_waiting_microseconds": 0,
      "first_task_ms": 0,
      "last_task_ms": 0,
      "assign_latency_microseconds_total": 94016,
      "assign_count": 4312,
      "assign_latency_microseconds_max": 162,
      "available_microseconds_queue_empty": 0,
      "center_state_msgs_total": 8623,
      "center_score_proposals_total": 35676,
      "center_score_broadcasts_total": 27329,
      "score_proposals_sent": 0,
      "last_score_improvement_ms": 1499,
      "center_full_microseconds": 0,
      "telemetry_microseconds_total": 10238,
      "process_cpu_pct": 59.375,
      "process_rss_bytes": 10788864,
      "process_threads": 2,
      "edges_out": [
        {
          "to": 1,
          "bytes": 34496,
          "count": 4312
        }
      ]
    },
    {
      "worker_id": 1,
      "seq_no": 4,
      "worker_local_ms": 106003126,
      "tasks_started_total": 10732,
      "tasks_sent_total": 4704,
      "tasks_recv_total": 4325,
      "bytes_sent_total": 37632,
      "bytes_recv_total": 34600,
      "send_microseconds_total": 4104,
      "recv_microseconds_total": 67324,
      "spawn_rejected_pool_full": 38532,
      "spawn_remote_rejected": 44293,
      "tasks_running": 0,
      "scheduler_pending_count": 0,
      "idle_microseconds_per_worker": 530366,
      "idle_microseconds_starved": 420743,
      "idle_microseconds_imbalanced": 109623,
      "sched_running_microseconds": 1228781,
      "sched_waiting_microseconds": 277759,
      "first_task_ms": 0,
      "last_task_ms": 1506,
      "assign_latency_microseconds_total": 0,
      "assign_count": 0,
      "assign_latency_microseconds_max": 0,
      "available_microseconds_queue_empty": 0,
      "center_state_msgs_total": 0,
      "center_score_proposals_total": 0,
      "center_score_broadcasts_total": 0,
      "score_proposals_sent": 35744,
      "last_score_improvement_ms": 0,
      "center_full_microseconds": 0,
      "telemetry_microseconds_total": 45,
      "process_cpu_pct": 156.467,
      "process_rss_bytes": 10899456,
      "process_threads": 3,
      "edges_out": [
        {
          "to": 0,
          "bytes": 37632,
          "count": 4704
        }
      ]
    }
  ],
  "nodes": [
    {
      "sentinel_worker_id": 0,
      "sentinel_local_ms": 106003119,
      "hostname": "DESKTOP",
      "socket_count": 1,
      "logical_cores": 24,
      "mem_total_bytes": 68051103744,
      "mem_avail_bytes": 27021086720,
      "cgroup_mem_used_bytes": 0,
      "cgroup_mem_limit_bytes": 0,
      "sockets": [
        {
          "socket_id": 0,
          "cpu_pct": 0.000,
          "mem_total_bytes": 27021086720,
          "mem_used_bytes": 0
        }
      ]
    }
  ]
}
```

### Paused line

While paused, the center sends only the line's header:

```json
{"version":2,"run_id":"6a5a9990-6f0f-4972-b6be-9d4bae4ead1e","ts":1793730848551,"elapsed_seconds":2,"paused":true,"final":false}
```

## Envelope

| Key | Type | Meaning | Since |
|---|---|---|---|
| `version` | integer | The schema version, `2`. See [Schema versions](#schema-versions). | v3.3.0 |
| `run_id` | string | A UUID v4 the center draws once per run, so a consumer can tell runs apart. | v4.3.0 (#343) |
| `ts` | integer | Wall-clock milliseconds since the Unix epoch when the line was built. | v3.3.0 |
| `elapsed_seconds` | integer | Seconds since telemetry came up on the center. | v3.3.0 |
| `paused` | boolean | Whether telemetry is paused. A paused line carries only the header, the keys up to `final`; a full line sent while paused, such as a snapshot or the final line, says `true` too. | v4.3.0 (#368) |
| `final` | boolean | `true` only on the line the center sends at shutdown, the last before it closes the connection; `false` on every other line, paused lines included. | v4.3.0 (#389) |
| `topology` | object | Hosts, ranks and the run setup. See [Topology](#topology). Absent from paused lines. | v3.3.0 |
| `workers` | array | The latest [worker frame](#worker-frame) of each rank that has sent one. Absent from paused lines. | v3.3.0 |
| `nodes` | array | The latest [node frame](#node-frame) of each host. Absent from paused lines. | v3.3.0 |

## Topology

`topology` has three keys: `nodes`, `identities` and `run`.

`topology.nodes` has one entry per host:

| Key | Type | Meaning | Since |
|---|---|---|---|
| `hostname` | string | The host's name. | v3.3.0 |
| `sentinel_worker_id` | integer | The rank that sends this host's node frames. | v3.3.0 |
| `total_physical_cores` | integer | Physical cores on the host; 0 without hwloc. | v3.3.0 |
| `total_logical_cores` | integer | Logical CPUs on the host; without hwloc, the count the OS reports. | v3.3.0 |
| `mem_total_bytes` | integer | The host's memory. | v3.3.0 |
| `worker_ids` | array of integers | The ranks on this host. | v3.3.0 |
| `sockets` | array | One entry per socket: `socket_id`, `name` (the CPU model), `physical_cores`, `logical_cores` and `cpu_ids` (its logical CPUs). Without hwloc, one entry for the whole host, with 0 physical cores. | v3.3.0 |

`topology.identities` has one entry per rank:

| Key | Type | Meaning | Since |
|---|---|---|---|
| `worker_id` | integer | The rank. | v3.3.0 |
| `hostname` | string | The host it runs on. | v3.3.0 |
| `pid` | integer | Its process id. | v3.3.0 |
| `primary_socket` | integer | The socket its CPU affinity places it on; 0 when unknown. | v3.3.0 |
| `role` | string | `"center"` for rank 0 of a multiprocessing run, which schedules and runs no tasks; `"worker"` otherwise, including the only rank of a multithreading run. | v4.3.0 (#344) |
| `allowed_cpu_ids` | array of integers | The logical CPUs it may run on. | v3.3.0 |

`topology.run` describes the run setup:

| Key | Type | Meaning | Since |
|---|---|---|---|
| `scheduler` | string or null | `"centralized"`, `"semi_centralized"`, or the name a custom scheduler gives; `null` without a scheduler. | v4.3.0 (#344) |
| `load_balancer` | string or null | `"quasi_horizontal"` or `"work_stealing"`; `null` until one is created. | v4.3.0 (#344) |
| `runtime_mode` | string | `"multiprocessing"` or `"multithreading"`. | v4.3.0 (#344) |
| `world_size` | integer | The number of ranks. | v4.3.0 (#344) |
| `threads_per_process` | integer | The largest thread pool a worker reports; 0 until a frame arrives. | v4.3.0 (#344) |
| `gempba_version` | string | The gempba version the center was built from. | v4.3.0 (#344) |
| `annotations` | object | Up to 8 key/value strings the program set with `telemetry::annotate_run`, keys of 1 to 32 bytes and values up to 64, in the order their keys were first set. | v4.3.0 (#344) |
| `markers` | array | Labelled moments, in the order they were recorded. Each has `elapsed_ms` (milliseconds since telemetry came up), `label`, and `source`: `"code"` for `telemetry::mark_run`, `"dashboard"` for a `mark` control line. At most 32, labels of 1 to 64 bytes. | v4.3.0 (#370) |

## Worker frame

Each entry of `workers` is one rank's latest frame. Every rank sends one per interval. A key that doesn't apply to a rank, for example a center-only counter on a worker, reads 0.

| Key | Unit | Ranks | Meaning | Since |
|---|---|---|---|---|
| `worker_id` | | all | The rank. | v3.3.0 |
| `seq_no` | count | all | Frames this rank has sent, the final one included. | v3.3.0 |
| `worker_local_ms` | ms | all | The rank's monotonic clock when it built the frame; comparable only within one rank. | v3.3.0 |
| `tasks_started_total` | count | all | Subtrees handed to a pool thread, received ones included, counted at hand-off. Branches explored inline are not counted. | v4.3.0 (#342), was `tasks_local_total` |
| `tasks_sent_total` | count | all | Tasks sent to another rank. | v3.3.0 |
| `tasks_recv_total` | count | all | Tasks received from another rank. | v3.3.0 |
| `bytes_sent_total` | bytes | all | Bytes of the tasks sent; the sum of `edges_out` bytes. | v4.3.0 (#340) |
| `bytes_recv_total` | bytes | all | Bytes of the tasks received. | v4.3.0 (#340) |
| `send_microseconds_total` | µs | all | Time in the MPI calls that send tasks: how long the sender stalled, not wire latency. | v4.3.0 (#340) |
| `recv_microseconds_total` | µs | all | Time in the MPI calls that receive tasks once a probe found them. | v4.3.0 (#340) |
| `spawn_rejected_pool_full` | count | with a load balancer | Local submissions refused because every pool thread was busy; those nodes ran inline. | v4.3.0 (#341) |
| `spawn_remote_rejected` | count | with a load balancer | Remote submissions refused because the scheduler opened no channel; those nodes were tried locally. | v4.3.0 (#341) |
| `tasks_running` | count | with a load balancer | Tasks executing on the pool right now. | v3.3.0 |
| `scheduler_pending_count` | count | all, under MPI | Outstanding requests in the scheduler's queue. | v3.3.0 |
| `idle_microseconds_per_worker` | µs per pool thread | with a load balancer | Pool idle time divided by pool size, including the idle time of threads still asleep when the frame is built (#335). | v3.3.0 |
| `idle_microseconds_starved` | µs per pool thread | with a load balancer | The part of the idle time while no task ran in the pool. | v4.3.0 (#337) |
| `idle_microseconds_imbalanced` | µs per pool thread | with a load balancer | The part of the idle time while other pool threads ran tasks. With `idle_microseconds_starved` it adds up to `idle_microseconds_per_worker`, give or take 1 µs of rounding. | v4.3.0 (#337) |
| `sched_running_microseconds` | µs | workers | Time from receiving a task to reporting available, summed over its tasks. | v4.3.0 (#338) |
| `sched_waiting_microseconds` | µs | workers | Time waiting for a task since run start; with `sched_running_microseconds` it adds up to the run time. | v4.3.0 (#338) |
| `first_task_ms` | ms since run start | workers | When its first task arrived; 0 until `tasks_recv_total` is non-zero. | v4.3.0 (#338) |
| `last_task_ms` | ms since run start | workers | When its latest task ended. | v4.3.0 (#338) |
| `assign_latency_microseconds_total` | µs | center | Time from assigning a rank to that rank reporting running, summed over assignments. The centralized center assigns by sending a task, the semi-centralized one by naming the rank a push target. | v4.3.0 (#338) |
| `assign_count` | count | center | Assignments counted in `assign_latency_microseconds_total`. | v4.3.0 (#338) |
| `assign_latency_microseconds_max` | µs | center | The longest single assignment latency. | v4.3.0 (#338) |
| `available_microseconds_queue_empty` | µs, summed over ranks | centralized center | Time ranks spent available while the center's queue was empty. | v4.3.0 (#338) |
| `center_state_msgs_total` | count | center | Running and available reports handled. | v4.3.0 (#339) |
| `center_score_proposals_total` | count | center | Score proposals received, improving or not. | v4.3.0 (#339) |
| `center_score_broadcasts_total` | count | center | Global score broadcasts, each a message to every worker. | v4.3.0 (#339) |
| `score_proposals_sent` | count | workers | Score proposals sent to the center. | v4.3.0 (#341) |
| `last_score_improvement_ms` | ms since run start | center | When the global score last improved; 0 until `center_score_broadcasts_total` is non-zero. | v4.3.0 (#341) |
| `center_full_microseconds` | µs | centralized center | Time its workers were told the center is full and must not send tasks. | v4.3.0 (#339) |
| `telemetry_microseconds_total` | µs | all | Time in telemetry ticks that sent or read a frame; under MPI, taken from the rank's scheduler thread. | v4.3.0 (#339) |
| `process_cpu_pct` | % of one CPU | all | Process CPU time over wall time since the previous frame; above 100 when several threads are busy. | v3.3.0 |
| `process_rss_bytes` | bytes | all | The process's resident memory. | v3.3.0 |
| `process_threads` | count | with a load balancer | The pool size plus the calling thread; 0 without a load balancer. | v3.3.0 |
| `edges_out` | array | all | One entry per rank this one sent tasks to, in ascending `to`: `to` (the destination rank), `bytes` and `count`. Destinations with no traffic are left out. | v3.3.0 |

## Node frame

Each entry of `nodes` is one host's latest frame, sent by its sentinel rank with each of its worker frames.

| Key | Unit | Meaning | Since |
|---|---|---|---|
| `sentinel_worker_id` | | The rank that sent it. | v3.3.0 |
| `sentinel_local_ms` | ms | The sentinel's monotonic clock when it built the frame. | v3.3.0 |
| `hostname` | | The host's name. | v3.3.0 |
| `socket_count` | count | Entries in `sockets`. | v3.3.0 |
| `logical_cores` | count | Logical CPUs the host reports. | v3.3.0 |
| `mem_total_bytes` | bytes | The host's memory. | v3.3.0 |
| `mem_avail_bytes` | bytes | The host's available memory. | v3.3.0 |
| `cgroup_mem_used_bytes` | bytes | The job's memory use in its enforcing memory cgroup on this host (v1 or v2, Linux only); 0 when unconstrained, so a consumer falls back to the host fields. | v4.2.0 |
| `cgroup_mem_limit_bytes` | bytes | That cgroup's limit; 0 when unconstrained. | v4.2.0 |
| `sockets` | array | One entry per socket: `socket_id`, `cpu_pct`, `mem_total_bytes` and `mem_used_bytes`. With hwloc, `mem_total_bytes` is the socket's NUMA memory, and `cpu_pct` and `mem_used_bytes` are not measured yet and read 0. Without hwloc there is one entry, holding host-wide memory. | v3.3.0 |

## Control lines

A client can write lines back on the same socket, one JSON object per line. The center applies each one and passes on to every rank what the ranks need. A malformed line, or one with an unknown `kind`, is dropped with a warning, and the lines after it still apply. String values accept JSON escapes. The center reads at most 4096 bytes from a client between polls and disconnects a client that sends more.

| Line | Effect | Since |
|---|---|---|
| `{"kind":"set_worker_interval_ms","value":N}` | Sets the telemetry interval to `N` ms, clamped to 50–600000, for worker frames, node frames and the broadcast alike. | v3.3.0 |
| `{"kind":"set_node_interval_ms","value":N}` | Accepted and ignored since v4.3.0, when node frames started following the worker interval (#367); to be removed in v5.0.0 (#373). | v3.3.0 |
| `{"kind":"pause"}` | Stops all ranks sending frames and the center reading them. Counters keep counting, so totals after the resume include the paused stretch. | v4.3.0 (#368) |
| `{"kind":"resume"}` | Ends a pause; frames resume at the current interval. | v4.3.0 (#368) |
| `{"kind":"snapshot"}` | Every rank sends one frame on its next tick, whatever the interval and even while paused, and the center sends one full line once all have arrived, or one second after the request. A request within 50 ms of the last accepted one is merged into it. | v4.3.0 (#369) |
| `{"kind":"mark","label":"..."}` | Records a marker with `source` `"dashboard"`, stamped by the center. Labels take 1 to 64 bytes; at most 32 markers per run, and any over a limit are dropped with a warning. | v4.3.0 (#370) |

`pause`, `resume` and `snapshot` also accept a `value`, which they ignore.

## Compatibility rules

- `version` is the schema number. Adding keys keeps it. Renaming or removing a key, or changing what a key means, bumps it.
- A consumer should ignore keys it doesn't know and treat a missing object or array as absent, so it keeps working when a release adds keys.
- The **Since** column gives the release that added each key. Within a schema, a key added by a later release is absent when an earlier one of the same schema sends the line.
- The center and every rank must run the same build. The frames between ranks are binary and carry no compatibility guarantee; only this JSON stream does.

## Schema versions

Newest first. Each version lists the releases that send it and what changed from the version before it.

### Schema 2: gempba v4.3.0

Time accounting to explain falling speedup (#333). Changes from schema 1:

- **Renamed:** `tasks_local_total` to `tasks_started_total` (#342), which now counts subtrees handed to a pool thread, received ones included, at hand-off.
- **Removed:** the node frame's `net_aggregate` and `disk_aggregate` (#342), which were always zero.
- **Added to the envelope:** `run_id` (#343), `paused` (#368) and `final` (#389). Paused lines (#368) carry only the envelope's header.
- **Added to the topology:** each identity's `role`, and the `run` object (#344), with `markers` (#370).
- **Added to the worker frame:**
  - `bytes_sent_total`, `bytes_recv_total`, `send_microseconds_total` and `recv_microseconds_total` (#340);
  - `spawn_rejected_pool_full`, `spawn_remote_rejected`, `score_proposals_sent` and `last_score_improvement_ms` (#341);
  - `idle_microseconds_starved` and `idle_microseconds_imbalanced` (#337);
  - `sched_running_microseconds`, `sched_waiting_microseconds`, `first_task_ms`, `last_task_ms`, `assign_latency_microseconds_total`, `assign_count`, `assign_latency_microseconds_max` and `available_microseconds_queue_empty` (#338);
  - `center_state_msgs_total`, `center_score_proposals_total`, `center_score_broadcasts_total`, `center_full_microseconds` and `telemetry_microseconds_total` (#339).
- **Changed meaning:** `idle_microseconds_per_worker` now includes the idle time of pool threads still asleep when the frame is built (#335).
- **Cadence:** the center broadcasts at the telemetry interval instead of every 500 ms (#345), and node frames follow the worker interval instead of their own 1000 ms one (#367).
- **Controls:** added `pause`, `resume` (#368), `snapshot` (#369) and `mark` (#370); `set_node_interval_ms` is accepted and ignored (#367).

### Schema 1: gempba v3.3.0 to v4.2.0

Sent by v3.3.0, v4.0.0, v4.1.0, v4.1.1, v4.1.2, v4.1.3 and v4.2.0. Its keys are schema 2's marked v3.3.0 or v4.2.0 above, plus:

- `tasks_local_total` in the worker frame instead of `tasks_started_total`: tasks dispatched to the rank's local thread pool;
- in the node frame, `net_aggregate` (`bytes_in`, `bytes_out`, `packets_in`, `packets_out`) and `disk_aggregate` (`read_bytes`, `write_bytes`);
- `idle_microseconds_per_worker` leaving out the idle time of threads still asleep when the frame was built.

v4.2.0 added `cgroup_mem_used_bytes` and `cgroup_mem_limit_bytes` to the node frame; earlier schema-1 releases don't send them. Schema 1 has no `run_id`, `paused`, `final`, `role` or `run`.

The center broadcast every 500 ms, whatever the interval. Worker frames defaulted to 500 ms and node frames to 1000 ms. The controls were `set_worker_interval_ms` and `set_node_interval_ms`, each clamped to 50–600000 ms.
