#!/bin/bash
cd "$(dirname "$0")/.." || exit 1
# ---------------------------------------------------------------------
echo "Current working directory: $(pwd)"
echo "Starting run at: $(date)"

## Release ---------------------------------------------------------------------
JOB_NAME="Default Job"  # SLURM
JOB_ID=-1               # SLURM
NODES=-1                # SLURM, this is the number of nodes in a super cluster
TASKS_PER_NODE=10       # SLURM, this is the number of tasks per node
TASKS_PER_SOCKET=-1     # SLURM,
CPUS_PER_TASK=1         # SLURM, this is the number of cores per process
MAX_DEPTH=10            # Maximum search depth
CSV_APPEND=true         # Append to existing CSV file
DIR_NAME=Unused

# ---------------------- Argument construction -------------------------
# NOTE:
# Only CPUS_PER_TASK is used by the project;
# the rest are passed for reporting / statistics.

args=(
    "-job_name"          "$JOB_NAME"
    "-job_id"            "$JOB_ID"
    "-max_depth"         "$MAX_DEPTH"
    "-nodes"             "$NODES"
    "-ntasks_per_node"   "$TASKS_PER_NODE"
    "-ntasks_per_socket" "$TASKS_PER_SOCKET"
    "-cpus_per_task"     "$CPUS_PER_TASK"
    "-csv"               "$CSV_APPEND"
    "-I"                 "$DIR_NAME"
)

# Here below are the scenarios to run, uncomment the one you want to run.

## Multiprocessing scenarios:

# - Benchmarking algorithm
mpirun -n "$TASKS_PER_NODE" --bind-to core --map-by slot:PE="$CPUS_PER_TASK" --report-bindings ./bin/mp_benchmark "${args[@]}"

## Multithreading scenarios:

# - Benchmarking algorithm
#./bin/mt_benchmark "${args[@]}"


# ---------------------------------------------------------------------
echo "Finishing run at: $(date)"
# ---------------------------------------------------------------------
