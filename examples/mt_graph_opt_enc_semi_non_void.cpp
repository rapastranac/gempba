#include "include/mt_graph_opt_enc_semi_non_void.hpp"
#include "include/main.hpp"

#include <string>

int run(int numThreads, int prob, std::string &filename) {
    auto &handler = gempba::branch_handler::getInstance(); // parallel GemPBA

    Graph graph;
    Graph oGraph;
    gempba::VC_non_void cover;

    graph.readEdges(filename);

    cover.init(graph, numThreads, filename, prob);
    cover.findCover(1);
    cover.printSolution();

    return 0;
}


int main(int argc, char *argv[]) {
    Params params = parse(argc, argv);

    int thread_per_task = params.cpus_per_task;
    int prob = params.prob;
    auto filename = params.filename;

    return run(thread_per_task, prob, filename);
}
