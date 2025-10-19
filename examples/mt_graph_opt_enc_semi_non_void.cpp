#include "include/mt_graph_opt_enc_semi_non_void.hpp"

#include <string>
#include <gempba/gempba.hpp>

#include "include/main.hpp"

int run(const int p_num_threads, const int p_probability, const std::string &filename) {

    Graph graph;
    Graph oGraph;
    gempba::load_balancer *v_load_balancer = gempba::mt::create_load_balancer(gempba::balancing_policy::QUASI_HORIZONTAL);
    gempba::node_manager &v_node_manager = gempba::mt::create_node_manager(v_load_balancer);
    mt_graph_opt_enc_semi_non_void cover(v_node_manager, *v_load_balancer);

    graph.readEdges(filename);

    cover.init(graph, p_num_threads, filename, p_probability);
    cover.findCover(1);
    cover.printSolution();

    return gempba::shutdown();
}


int main(int argc, char *argv[]) {
    Params params = parse(argc, argv);

    int thread_per_task = params.cpus_per_task;
    int prob = params.prob;
    auto filename = params.filename;

    return run(thread_per_task, prob, filename);
}
