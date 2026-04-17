# ---------------------------------------------------------------------
Set-Location (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
Write-Output "Current working directory: $( Get-Location )"
Write-Output "Starting run at: $( Get-Date )"

## Release ---------------------------------------------------------------------
$JOB_NAME = "Default Job"   # SLURM job name
$JOB_ID = -1                # SLURM job id
$NODES = -1                 # SLURM nodes
$TASKS_PER_NODE = 10         # SLURM tasks per node
$TASKS_PER_SOCKET = -1      # SLURM tasks per socket
$CPUS_PER_TASK = 1          # SLURM CPUs per task
$MAX_DEPTH = 10             # Maximum search depth
$CSV_APPEND = "true"        # Append to existing CSV file
$DIR_NAME = "Unused"

# From the following args variable, only CPUS_PER_TASK is used by the project
# Put arguments in an array — each element is one argument
$arguments = @(
    "-job_name", "$JOB_NAME"
    "-job_id", "$JOB_ID"
    "-max_depth", "$MAX_DEPTH"
    "-nodes", "$NODES"
    "-ntasks_per_node", "$TASKS_PER_NODE"
    "-ntasks_per_socket", "$TASKS_PER_SOCKET"
    "-cpus_per_task", "$CPUS_PER_TASK"
    "-csv", "$CSV_APPEND"
    "-I", "$DIR_NAME"
)

# Here below are the scenarios to run, uncomment the one you want to run.

## Multiprocessing scenarios:

# - Benchmarking algorithm
#mpiexec.exe -n $TASKS_PER_NODE ./bin/mp_benchmark $arguments

## Multithreading scenarios:

# - Benchmarking algorithm
./bin/mt_benchmark $arguments


Write-Output "Finished run at: $( Get-Date )"