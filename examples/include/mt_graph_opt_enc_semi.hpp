#ifndef MT_GRAPH_OPT_ENC_SEMI_HPP
#define MT_GRAPH_OPT_ENC_SEMI_HPP


#include <gempba/gempba.hpp>
#include <spdlog/spdlog.h>
#include "VertexCover.hpp"

class mt_graph_optimized_encoding_semi_centralized final : public VertexCover {

    gempba::node_manager &m_branch_handler;
    gempba::load_balancer &m_load_balancer;
    std::function<void(std::thread::id, int, Graph, gempba::node)> m_function;

public:
    explicit mt_graph_optimized_encoding_semi_centralized(gempba::node_manager &p_branch_handler, gempba::load_balancer &p_load_balancer) :
        m_branch_handler(p_branch_handler), m_load_balancer(p_load_balancer) {
        this->m_function = std::bind(&mt_graph_optimized_encoding_semi_centralized::mvc, this, _1, _2, _3, _4);
    }

    ~mt_graph_optimized_encoding_semi_centralized() override = default;

    bool findCover(int run) {
        string msg_center = fmt::format("run # {} ", run);
        msg_center = "!" + fmt::format("{:-^{}}", msg_center, wide - 2) + "!" + "\n";
        cout << msg_center;
        outFile(msg_center, "");

        this->m_branch_handler.set_thread_pool_size(numThreads);

        //size_t _k_mm = maximum_matching(graph);
        //size_t _k_uBound = graph.max_k();
        //size_t _k_lBound = graph.min_k();
        //size_t k_prime = std::min(k_mm, k_uBound);
        //currentMVCSize = k_prime;
        //preSize = graph.size();

        preSize = graph.preprocessing();

        auto degLB = graph.DegLB();

        size_t k_mm = maximum_matching(graph);
        size_t k_uBound = graph.max_k();
        size_t k_lBound = graph.min_k();
        size_t k_prime = std::min(k_mm, k_uBound) + graph.coverSize();
        currentMVCSize = k_prime;

        begin = std::chrono::steady_clock::now();

        try {
            m_branch_handler.set_score(gempba::score::make(currentMVCSize));
            m_branch_handler.set_goal(gempba::MINIMISE, gempba::score_type::I32);
            //testing ****************************************
            gempba::node seed_node = gempba::create_seed_node<void>(m_load_balancer, m_function, std::make_tuple(0, graph));
            {
                m_branch_handler.try_local_submit(seed_node);
            }
            //************************************************
            m_branch_handler.wait();
            const optional<Graph> v_result_opt = m_branch_handler.get_result<Graph>();
            if (v_result_opt) {
                graph_res = v_result_opt.value();
            }
            graph_res2 = graph_res;
            cover = graph_res.postProcessing();
        } catch (std::exception &e) {
            this->output.open(outPath, std::ofstream::in | std::ofstream::out | std::ofstream::app);
            if (!output.is_open()) {
                printf("Error, output file not found ! \n");
            }
            std::cout << "Exception caught : " << e.what() << '\n';
            output << "Exception caught : " << e.what() << '\n';
            output.close();
        }

        cout << "DONE!" << endl;
        end = std::chrono::steady_clock::now();
        elapsed_secs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();

        printf("refGlobal : %d \n", m_branch_handler.get_score().get<int>());
        return true;
    }

    void mvc(std::thread::id p_id, int depth, Graph graph, gempba::node p_parent) {
        size_t LB = graph.min_k();
        size_t degLB = 0; //graph.DegLB();
        size_t UB = graph.max_k();
        size_t acLB = 0; //graph.antiColoringLB();
        //size_t mm = maximum_matching(graph);
        size_t k = relaxation(LB, UB);
        //std::max({LB, degLB, acLB})

        if (k + graph.coverSize() >= static_cast<size_t>(m_branch_handler.get_score().get<int>())) {
            //size_t addition = k + graph.coverSize();
            return;
        }

        if (graph.size() == 0) {
            #if GEMPBA_DEBUG_COMMENTS
            spdlog::debug("Leaf reached, depth : %d \n", depth);
            #endif
            terminate_condition(graph, p_id, depth);
            return;
        }

        int v = graph.id_max(false);
        int SIZE = graph.size();

        const gempba::node v_parent = p_parent == nullptr ? gempba::create_dummy_node(m_load_balancer) : p_parent;

        const std::function<std::optional<std::tuple<int, Graph> >()> v_left_args_initializer = [&]() -> std::optional<std::tuple<int, Graph> > {
            Graph g = graph;
            g.removeVertex(v);
            g.clean_graph();
            //g.removeZeroVertexDegree();
            int v_cover_size = g.coverSize();
            const int v_best_val = m_branch_handler.get_score().get<int>();

            if (v_cover_size == 0) {
                spdlog::error("rank {}, thread {}, cover is empty", m_branch_handler.rank_me(), thread_id_to_string(p_id));
                throw;
            }
            if (v_cover_size < v_best_val) {
                return std::make_tuple(depth + 1, g);
            }
            return std::nullopt;
        };

        gempba::node v_left = gempba::mt::create_lazy_node<void>(m_load_balancer, v_parent, m_function, v_left_args_initializer);

        std::function<std::optional<std::tuple<int, Graph> >()> v_right_args_initializer = [&]() -> std::optional<std::tuple<int, Graph> > {
            Graph g = graph;
            if (g.empty()) {
                spdlog::error("rank {}, thread {}, Graph is empty\n", m_branch_handler.rank_me(), thread_id_to_string(p_id));
                throw;
            }
            g.removeNv(v);
            g.clean_graph();

            const int v_best_val = m_branch_handler.get_score().get<int>();
            int cover_size = g.coverSize();

            if (cover_size < v_best_val) {
                return std::make_tuple(depth + 1, g);
            }
            return std::nullopt;
        };

        gempba::node v_right = gempba::mt::create_lazy_node<void>(
                m_load_balancer,
                v_parent, m_function,
                v_right_args_initializer
                );

        branchHandler.try_local_submit(v_left);
        branchHandler.forward(v_right);

    }

private:
    void terminate_condition(Graph &p_graph, const std::thread::id p_id, const int p_depth) {
        std::scoped_lock<std::mutex> lck(mtx);
        if (p_graph.coverSize() < m_branch_handler.get_score().get<int>()) {
            int SZ = p_graph.coverSize(); // debuggin line
            m_branch_handler.try_update_result(p_graph, gempba::score::make(p_graph.coverSize()));

            foundAtDepth = p_depth;
            recurrent_msg(p_id);

            if (p_depth > (int) measured_Depth)
                measured_Depth = (size_t) p_depth;

            ++leaves;
        }

    }
};

#endif // MT_GRAPH_OPT_ENC_SEMI_HPP
