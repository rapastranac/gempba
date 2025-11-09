#!/usr/bin/env bash

# ---------------------------------------------------------------------
echo "Current working directory: $(pwd)"
echo "Starting batch run at: $(date)"
# ---------------------------------------------------------------------

# ---------------------- SLURM-like metadata ---------------------------
JOB_ID=-1
NODES=-1
TASKS_PER_NODE=5
TASKS_PER_SOCKET=-1
CPUS_PER_TASK=2

# ---------------------- Input directory -------------------------------
GRAPH_DIR="data/prob_4/100"

# Collect all regular files in the directory
mapfile -t GRAPH_FILES < <(find "$GRAPH_DIR" -maxdepth 1 -type f)

if [ ${#GRAPH_FILES[@]} -eq 0 ]; then
    echo "No graph files found in: $GRAPH_DIR"
    exit 1
fi

# ---------------------- Run each graph file ---------------------------
for file in "${GRAPH_FILES[@]}"; do

    echo "------------------------------------------------------"
    echo "Running file: $file"
    echo "Start time: $(date)"

    # Construct argument list
    ARGS=(
        "-job_name"          "$JOB_NAME"
        "-job_id"            "$JOB_ID"
        "-nodes"             "$NODES"
        "-ntasks_per_node"   "$TASKS_PER_NODE"
        "-ntasks_per_socket" "$TASKS_PER_SOCKET"
        "-cpus_per_task"     "$CPUS_PER_TASK"
        "-csv"               "true"
        "-I"                 "$file"
    )

    # Execute MPI program (BLOCKING)
    mpirun -n "$TASKS_PER_NODE" --bind-to core --map-by slot:PE="$CPUS_PER_TASK" --report-bindings ./bin/mp_bitvect_opt_enc_semi "${ARGS[@]}"

    echo "Finished file: $(basename "$file") at $(date)"
done

echo "------------------------------------------------------"
echo "All runs completed at: $(date)"
# ---------------------------------------------------------------------
