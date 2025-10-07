#include "include/mt_graph_opt_enc_semi.hpp"

#include <gempba/gempba.hpp>
#include "include/Graph.hpp"
#include "include/main.hpp"

int run(int job_id, int ntasks_per_node, int prob, string &filename) {
    Graph graph;
    Graph oGraph;

    gempba::load_balancer *v_load_balancer = gempba::mt::create_load_balancer(gempba::balancing_policy::QUASI_HORIZONTAL);
    gempba::branch_handler &v_branch_handler = gempba::mt::create_branch_handler(v_load_balancer);
    mt_graph_optimized_encoding_semi_centralized v_cover(v_branch_handler, *v_load_balancer);

    graph.readEdges(filename);
    //graph.readDimacs(filename);

    v_cover.init(graph, ntasks_per_node, filename, prob);
    v_cover.findCover(job_id);
    v_cover.printSolution();

    return gempba::shutdown();
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
