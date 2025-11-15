#!/bin/bash
#SBATCH --job-name=benchmark
#SBATCH --output=report/%x-%j.out
#SBATCH --error=errors/%x-%j.err
#SBATCH --account={account_name}          # replace with your group name
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --ntasks-per-socket=1
#SBATCH --cpus-per-task=8
#SBATCH --time=0-00:05:00           	    # time limit (DD-HH:MM:SS)
#SBATCH --mail-user={your.email@domain}   # replace with your email address
#SBATCH --mail-type=BEGIN
#SBATCH --mail-type=END
#SBATCH --mail-type=FAIL
#SBATCH --mail-type=REQUEUE
# ---------------------------------------------------------------------
cd $SLURM_SUBMIT_DIR

module --force purge
module load StdEnv/2023  gcc/14.3 cmake/3.31.0 googletest/1.14.0 openmpi/5.0.8 # Load required modules here
module list

# ---------------------------------------------------------------------
echo "Current working directory: `pwd`"
echo "Starting run at: `date`"

args=(
    "-job_name"          "$SLURM_JOB_NAME"
    "-job_id"            "$SLURM_JOB_ID"
    "-nodes"             "$SLURM_JOB_NUM_NODES"
    "-ntasks_per_node"   "$SLURM_NTASKS_PER_NODE"
    "-ntasks_per_socket" "$SLURM_NTASKS_PER_SOCKET"
    "-cpus_per_task"     "$SLURM_CPUS_PER_TASK"
    "-max_depth"         "20"
    "-csv"               "true"
    "-I"                 "Unused"
)

srun ./bin/mt_benchmark "${args[@]}"    # Multi-threaded run: Only 1 node or it will fail
#srun ./bin/mp_benchmark "${args[@]}"   # Multi-process run (using MPI): At least 2 nodes or it will fail


# ---------------------------------------------------------------------
echo "Finishing run at: `date`"
# ---------------------------------------------------------------------

