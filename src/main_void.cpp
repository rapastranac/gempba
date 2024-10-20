#ifdef VC_VOID

#include "../examples/include/Graph.hpp"

#include "../examples/include/example4.hpp"

#include "BranchHandler/BranchHandler.hpp"
#include <string>

int main_void(int job_id, int nodes, int ntasks_per_node, int ntasks_per_socket, int cpus_per_task, int prob,
              std::string &filename_directory) {

    auto &handler = gempba::BranchHandler::getInstance(); // parallel GemPBA

    Graph graph;
    Graph oGraph;
    example4 cover;

    graph.readEdges(filename_directory);
    //graph.readDimacs(filename);

    cover.init(graph, ntasks_per_node, filename_directory, prob);
    cover.findCover(job_id);
    cover.printSolution();

    return 0;
}

#endif