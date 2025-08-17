#include "include/mt_graph_opt_enc_semi.hpp"
#include "include/main.hpp"
#include "include/Graph.hpp"

int run(int job_id, int ntasks_per_node, int prob, string &filename) {
    Graph graph;
    Graph oGraph;
    MTGraphOptimizedEncodingSemiCentralized cover;

    graph.readEdges(filename);
    //graph.readDimacs(filename);

    cover.init(graph, ntasks_per_node, filename, prob);
    cover.findCover(job_id);
    cover.printSolution();

    return 0;
}

/**
 * Multithreaded version with Graph class, fails with big graphs due to stack limit
 */

int main(int argc, char *argv[]) {
    Params params = parse(argc, argv);

    int job_id = params.job_id;
    int thread_per_task = params.cpus_per_task;
    int prob = params.prob;
    auto filename = params.filename;

    return run(job_id, thread_per_task, prob, filename);
}
