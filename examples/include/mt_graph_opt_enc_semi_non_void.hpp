#ifndef GEMPBA_MT_GRAPH_OPT_ENC_SEMI_NON_VOID_HPP
#define GEMPBA_MT_GRAPH_OPT_ENC_SEMI_NON_VOID_HPP

#include <gempba/gempba.hpp>
#include "VertexCover.hpp"

// TODO... The whole algorithm passes by value, which is not efficient. We should pass by reference.

class mt_graph_opt_enc_semi_non_void final : public VertexCover {

    gempba::node_manager &m_node_manager;
    gempba::load_balancer &m_load_balancer;
    std::function<Graph(std::thread::id, int, Graph, gempba::node)> m_function;

public:
    mt_graph_opt_enc_semi_non_void(gempba::node_manager &p_node_manager, gempba::load_balancer &p_load_balancer) :
        m_node_manager(p_node_manager), m_load_balancer(p_load_balancer) {
        this->m_function = std::bind(&mt_graph_opt_enc_semi_non_void::mvc, this, _1, _2, _3, _4);
    }

    ~mt_graph_opt_enc_semi_non_void() override {
    }

    bool findCover(int run) {
        string msg_center = fmt::format("run # {} ", run);
        msg_center = "!" + fmt::format("{:-^{}}", msg_center, wide - 2) + "!" + "\n";
        cout << msg_center;

        this->m_node_manager.set_thread_pool_size(numThreads);
        preSize = graph.preprocessing();

        size_t k_mm = maximum_matching(graph);
        size_t k_uBound = graph.max_k();
        size_t k_lBound = graph.min_k();
        size_t k_prime = std::min(k_mm, k_uBound) + graph.coverSize();
        currentMVCSize = k_prime;

        begin = std::chrono::steady_clock::now();

        try {
            m_node_manager.set_score(gempba::score::make(currentMVCSize));

            const auto dummy_node = gempba::create_dummy_node(m_load_balancer);
            const Graph result = mvc(std::this_thread::get_id(), 0, graph, dummy_node);

            graph_res = result;

            graph_res2 = graph_res;
            cover = graph_res.postProcessing();
        } catch (...) {
        }

        cout << "DONE!" << endl;
        end = std::chrono::steady_clock::now();
        elapsed_secs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();

        printf("refGlobal : %d \n", m_node_manager.get_score().get<int>());
        return true;
    }

    Graph mvc(std::thread::id p_id, int depth, Graph graph, gempba::node p_parent) {
        size_t LB = graph.min_k();
        size_t degLB = 0; // graph.DegLB();
        //            size_t UB = graph.max_k();
        size_t acLB = 0; // graph.antiColoringLB();
        //size_t mm = maximum_matching(graph);
        //size_t k = relaxation(k1, k2);

        if (graph.coverSize() + std::max({LB, degLB, acLB}) >= static_cast<size_t>(m_node_manager.get_score().get<int>())) {
            //size_t addition = k + graph.coverSize();
            //return;
            return {};
        }

        if (graph.size() == 0) {
            #if GEMPBA_DEBUG_COMMENTS
            spdlog::debug("Leaf reached, depth : %d \n", depth);
            #endif
            return termination(graph);
        }
        int newDepth = depth + 1;

        const int v = graph.id_max(false);
        const gempba::node v_parent = p_parent == nullptr ? gempba::create_dummy_node(m_load_balancer) : p_parent;

        const std::function<std::optional<std::tuple<int, Graph> >()> v_left_args_initializer = [&]()-> std::optional<std::tuple<int, Graph> > {
            Graph g = graph;
            g.removeVertex(v);
            g.clean_graph();
            //g.removeZeroVertexDegree();
            int v_cover_size = g.coverSize();
            const int v_best_val = m_node_manager.get_score().get<int>();

            if (v_cover_size < v_best_val) {
                if (v_cover_size == 0) {
                    spdlog::error("rank {}, thread {}, cover is empty", m_node_manager.rank_me(), thread_id_to_string(p_id));
                    throw;
                }
                return std::make_tuple(depth + 1, g);
            }
            return std::nullopt;
        };

        gempba::node v_left = gempba::mt::create_lazy_node<Graph>(m_load_balancer, v_parent, m_function, v_left_args_initializer);

        std::function<std::optional<std::tuple<int, Graph> >()> v_right_args_initializer = [&]() -> std::optional<std::tuple<int, Graph> > {
            Graph g = graph;
            if (g.empty()) {
                spdlog::error("rank {}, thread {}, Graph is empty\n", m_node_manager.rank_me(), thread_id_to_string(p_id));
                throw;
            }
            g.removeNv(v);
            g.clean_graph();

            const int cover_size = g.coverSize();
            const int v_best_val = m_node_manager.get_score().get<int>();

            if (cover_size < v_best_val) {
                return std::make_tuple(depth + 1, g);
            }
            return std::nullopt;
        };

        gempba::node v_right = gempba::mt::create_lazy_node<Graph>(
                m_load_balancer,
                v_parent, m_function,
                v_right_args_initializer
                );

        m_node_manager.try_local_submit(v_left);
        m_node_manager.forward(v_right);

        //*******************************************************************************************

        const auto v_any_right_result = v_right.get_any_result();
        if (!v_any_right_result.has_value()) {
            throw std::runtime_error("no right");
        }
        Graph right_graph = std::any_cast<Graph>(v_any_right_result);

        auto v_any_left_result = v_left.get_any_result();
        if (!v_any_right_result.has_value()) {
            throw std::runtime_error("no left");
        }
        Graph left_graph = std::any_cast<Graph>(v_any_right_result);

        return return_res(left_graph, right_graph);
    }

    Graph termination(Graph &graph) const {
        bool updated = m_node_manager.try_update_result(graph, gempba::score::make(graph.size()));

        if (updated) {
            return graph;
        }
        return {};
    }

    static Graph return_res(Graph &left, Graph &right) {
        if (!left.empty() && !right.empty()) {
            return left.coverSize() >= right.coverSize() ? right : left;
        }
        if (!left.empty() && right.empty()) {
            return left;
        }
        if (left.empty() && !right.empty()) {
            return right;
        }

        return {};
    }
};


#endif //GEMPBA_MT_GRAPH_OPT_ENC_SEMI_NON_VOID_HPP
