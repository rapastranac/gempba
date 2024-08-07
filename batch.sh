#!/bin/bash
# ---------------------------------------------------------------------
echo "Current working directory: $(pwd)"
echo "Starting run at: $(date)"

#to debug
#xhost +node1 +manager
#mpirun -hostfile hostfile -np 2 xterm -hold -fa 'Monospace' DISPLAY=manager -fs 12 -e gdb -ex=run ./a.out

#mpirun -n 2 xterm -fa 'Monospace' -bg white -fg black -fs 12 -display :0 -e gdb -x gdb_commands --args a.out -N 10 -P 5 -I input/prob_4/400/00400_1

#mpirun -n 2 xterm -fa 'Monospace' -bg white -fg black -fs 12 -display :0 -e valgrind --leak-check=yes ./a.out -N 10 -P 5 -I input/prob_4/400/00400_1

#mpirun -n 2 xterm -fa 'Monospace' -bg white -fg black -fs 12 -e gdb -x gdb_commands --args a.out -N 1 -P 5 -I input/p_hat1000_2

## MINI CLUSTER SETUP
#mpirun -hostfile hostfile -np 3 ./a.out -N 1 -P 5 -I input/prob_4/400/00400_1
#mpirun -hostfile hostfile -np 3 ./a.out -N 1 -P 5 -I input/DSJC500_5

#mpirun -n 5 --bind-to core --map-by numa:PE=2 --report-bindings xterm -fa 'Monospace' -bg white -fg black -fs 12 -e gdb -x gdb_commands --args a.out
#mpirun --oversubscribe -n 5 -display-map --bind-to none --map-by numa:PE=2 --report-bindings xterm -fa 'Monospace' -bg white -fg black -fs 12 -e gdb -x gdb_commands --args a.out -I input/prob_4/600/00600_1

#mpirun --oversubscribe -n 10 -display-map --bind-to none --map-by core --report-bindings xterm -fa 'Monospace' -bg white -fg black -fs 12 -hold -e gdb -x gdb_commands --args a.out -N 1 -I input/prob_4/600/00600_1
#mpirun --oversubscribe -n 10 -display-map --bind-to none --map-by core --report-bindings xterm -fa 'Monospace' -bg white -fg black -fs 12 -hold -e gdb -x gdb_commands --args a.out -N 1 -I input/p_hat1000_2

#mpirun --oversubscribe -n 50 -host manager:30,node1:20 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 4 -I input/prob_4/600/0600_93
#mpirun --oversubscribe -n 10 -host manager:5,node1:5 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 1 -I input/prob_4/600/00600_1
#mpirun --oversubscribe -n 15 -host manager:10,node1:5 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 4 -I input/p_hat700_1
#mpirun --oversubscribe -n 2 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 48 -I input/prob_4/600/00600_1
#mpirun --oversubscribe -n 6 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 1 -I input/prob_4/600/00600_1

JOB_ID=9999
TASKS_PER_NODE=9999
TASKS_PER_SOCKET=9999
NODES=3         #processes
CPUS_PER_TASK=3 #threads per process (including the designated thread for MPI communication)
GRAPH_FILE=input/prob_4/600/00600_1

# From the following args variable, only CPUS_PER_TASK is used by the project, the rest is used only for stats purposes
args="-job_id $JOB_ID -nodes $NODES -ntasks_per_node $TASKS_PER_NODE -ntasks_per_socket $TASKS_PER_SOCKET -cpus_per_task $CPUS_PER_TASK -I $GRAPH_FILE"

#binding
mpirun -n $NODES -display-map --bind-to core --map-by numa:PE=1 --report-bindings ./a.out $args
#mpirun -n 3 -host manager:2,node1:1 -display-map --bind-to core --map-by numa:PE=3 --report-bindings ./a.out -N 3 -I input/prob_4/600/00600_1

#mpirun --oversubscribe -n 2 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 1 -I input/p_hat700_1
#mpirun --oversubscribe -n 5 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 4 -I input/p_hat500_3
#mpirun --oversubscribe -n 129 -host manager:1,node1:1 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 4 -I input/frb30_15_mis/frb30_15_1.mis
#mpirun --oversubscribe -n 129 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 4 -I input/frb30_15_mis/frb30_15_1.mis

#mpirun --oversubscribe -n 4 -host node1:1,manager:3 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 3 -I input/p_hat700_1
#mpirun --oversubscribe -n 5 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 4 -I input/p_hat700_1

#mpirun --oversubscribe -n 10 -host manager:5,node1:5 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 4 -I input/p_hat1000_2
#mpirun --oversubscribe -n 10 -display-map --bind-to none --map-by core --report-bindings ./a.out -N 4 -I input/p_hat1000_2

# ---------------------------------------------------------------------
echo "Finishing run at: $(date)"
# ---------------------------------------------------------------------
