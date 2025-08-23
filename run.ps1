# ---------------------------------------------------------------------
Write-Output "Current working directory: $( Get-Location )"
Write-Output "Starting run at: $( Get-Date )"

## Release ---------------------------------------------------------------------
$JOB_ID = 9999            # SLURM job id
$NODES = 9999             # SLURM nodes
$TASKS_PER_NODE = 4       # SLURM tasks per node
$TASKS_PER_SOCKET = 9999  # SLURM tasks per socket
$CPUS_PER_TASK = 4        # SLURM CPUs per task
$GRAPH_FILE = "data/prob_4/400/00400_1"

# From the following args variable, only CPUS_PER_TASK is used by the project
# Put arguments in an array — each element is one argument
$arguments = @(
    "-job_id", "$JOB_ID"
    "-nodes", "$NODES"
    "-ntasks_per_node", "$TASKS_PER_NODE"
    "-ntasks_per_socket", "$TASKS_PER_SOCKET"
    "-cpus_per_task", "$CPUS_PER_TASK"
    "-I", "$GRAPH_FILE"
)

# Here below are the scenarios to run, uncomment the one you want to run.

## Multiprocessing scenarios:

#  - Bitvector Optimized Encoding Semi-Centralized
mpiexec.exe -n $TASKS_PER_NODE ./bin/mp_bitvect_opt_enc_semi $arguments

#  - Bitvector Optimized Encoding Centralized
#mpiexec.exe -n $TASKS_PER_NODE ./bin/mp_bitvect_opt_enc_central $arguments

#  - Bitvector Basic Encoding Semi-Centralized
#mpiexec.exe -n $TASKS_PER_NODE ./bin/mp_bitvect_basic_enc_semi $arguments

#  - Bitvector Basic Encoding Centralized
#mpiexec.exe -n $TASKS_PER_NODE ./bin/mp_bitvect_basic_enc_central $arguments


## Multithreading scenarios:

#  - Bitvector Optimized Encoding Semi-Centralized
#./bin/mt_bitvect_opt_enc_semi $arguments

#  - Graph Optimized Encoding Semi-Centralized
#./bin/mt_graph_opt_enc_semi $arguments

#  - Graph Optimized Encoding Semi-Centralized for non-void algorithms
#./bin/mt_graph_opt_enc_semi_non_void $arguments