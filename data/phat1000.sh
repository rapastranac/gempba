#!/bin/bash
#SBATCH --job-name=phat1000
#SBATCH --output=report/%x-%j.out
#SBATCH --account=def-mlafond
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=2
#SBATCH --ntasks-per-socket=1
#SBATCH --cpus-per-task=24
#SBATCH --time=0-04:00:00           	# time limit (DD-HH:MM)
#SBATCH --mail-user=rapastranac@gmail.com
#SBATCH --mail-type=ALL
#SBATCH --constraint=cascade # for cedar only cascadelake 2 x Intel Platinum 8260 Cascade Lake @ 2.4Ghz, 768 nodes
# ---------------------------------------------------------------------
cd $SLURM_SUBMIT_DIR

module --force purge
module load StdEnv/2020 gcc/9.3.0
module load openmpi/4.0.3
module load boost/1.80.0
module list

#gcc --version
#mpirun --version
echo "Current working directory: $(pwd)"
echo "Starting run at: $(date)"

srun ./a.out -job_id $SLURM_JOBID -nodes $SLURM_NNODES -ntasks_per_node $SLURM_NTASKS_PER_NODE -ntasks_per_socket $SLURM_NTASKS_PER_SOCKET -cpus_per_task $SLURM_CPUS_PER_TASK -I input/p_hat1000_2

# ---------------------------------------------------------------------
echo "Finishing run at: $(date)"
# ---------------------------------------------------------------------
