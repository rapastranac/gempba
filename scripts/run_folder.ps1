# ---------------------------------------------------------------------
Set-Location (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
Write-Output "Current working directory: $( Get-Location )"
Write-Output "Starting batch run at: $( Get-Date )"
# ---------------------------------------------------------------------

# ---------------------- SLURM-like metadata ---------------------------
$JOB_ID            = -1
$NODES             = -1
$TASKS_PER_NODE    = 6
$TASKS_PER_SOCKET  = -1
$CPUS_PER_TASK     = 4

# ---------------------- Input directory -------------------------------
# Directory where all graph files are located
$GRAPH_DIR = "data/prob_4/100"

# Match files with no extension or any extension you need
# Example: "*.graph", "*.txt", or "*" for all files
$GRAPH_FILES = Get-ChildItem -Path $GRAPH_DIR -File

if ($GRAPH_FILES.Count -eq 0) {
    Write-Output "No graph files found in: $GRAPH_DIR"
    exit 1
}

# ---------------------- Run each graph file ---------------------------
foreach ($file in $GRAPH_FILES) {

    Write-Output "------------------------------------------------------"
    Write-Output "Running file: $($file.FullName)"
    Write-Output "Start time: $( Get-Date )"

    # Construct argument list
    $arguments = @(
        "-job_name", "$JOB_NAME"
        "-job_id", "$JOB_ID"
        "-nodes", "$NODES"
        "-ntasks_per_node", "$TASKS_PER_NODE"
        "-ntasks_per_socket", "$TASKS_PER_SOCKET"
        "-cpus_per_task", "$CPUS_PER_TASK"
        "-csv", "true"
        "-I", "$($file.FullName)"
    )

    # Execute MPI program (BLOCKING)
    mpiexec.exe -n $TASKS_PER_NODE ./bin/mp_bitvect_opt_enc_semi @arguments
    # For non-MPI execution, uncomment the line below:
#    ./bin/mt_bitvect_opt_enc_semi @arguments

    Write-Output "Finished file: $($file.Name) at $( Get-Date )"
}

Write-Output "------------------------------------------------------"
Write-Output "All runs completed at: $( Get-Date )"
# ---------------------------------------------------------------------
