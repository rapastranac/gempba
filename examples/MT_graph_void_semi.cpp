#include "include/main.hpp"
#include "include/Graph.hpp"

/**
 * Multithreaded version with Graph class, fails with big graphs due to stack limit
 */

int main(int argc, char *argv[]) {
    Params params = parse(argc, argv);

    int job_id = params.job_id;
    int nodes = params.nodes;
    int ntasks_per_node = params.ntasks_per_node;
    int ntasks_per_socket = params.ntasks_per_socket;
    int cpus_per_task = params.cpus_per_task;
    int prob = params.prob;
    auto filename = params.filename;


    Graph graph;
    Graph oGraph;
    MT_graph_void_semi cover;

    graph.readEdges(filename);
    //graph.readDimacs(filename);

    cover.init(graph, ntasks_per_node, filename, prob);
    cover.findCover(job_id);
    cover.printSolution();

    return 0;
}
