#include "include/MP_bitvec_void_semi.hpp"
#include "include/main.hpp"

// Multithreading


int main(int argc, char *argv[]) {
    Params params = parse(argc, argv);

    int job_id = params.job_id;
    int nodes = params.nodes;
    int ntasks_per_node = params.ntasks_per_node;
    int ntasks_per_socket = params.ntasks_per_socket;
    int cpus_per_task = params.cpus_per_task;
    int prob = params.prob;
    auto filename = params.filename;

    return main_void_bitvec(ntasks_per_node, prob, filename);
}
//main_void_bitvec(numThreads, prob, filename);