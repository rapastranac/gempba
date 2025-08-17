#ifndef MT_GRAPH_OPT_ENC_SEMI_HPP
#define MT_GRAPH_OPT_ENC_SEMI_HPP


#include <spdlog/spdlog.h>
#include "VertexCover.hpp"

class MTGraphOptimizedEncodingSemiCentralized : public VertexCover {
    using HolderType = gempba::ResultHolder<void, int, Graph>;

private:
    std::function<void(int, int, Graph &, void *)> _f;

public:
    MTGraphOptimizedEncodingSemiCentralized() {
        this->_f = std::bind(&MTGraphOptimizedEncodingSemiCentralized::mvc, this, _1, _2, _3, _4);
    }

    ~MTGraphOptimizedEncodingSemiCentralized() override = default;

    bool findCover(int run) {
        string msg_center = fmt::format("run # {} ", run);
        msg_center = "!" + fmt::format("{:-^{}}", msg_center, wide - 2) + "!" + "\n";
        cout << msg_center;
        outFile(msg_center, "");

        this->branchHandler.init_thread_pool(numThreads);

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
            branchHandler.set_score(gempba::score::make(currentMVCSize));
            branchHandler.set_goal(gempba::MINIMISE, gempba::score_type::I32);
            //mvc(-1, 0, graph);
            //testing ****************************************
            HolderType initial(dlb, -1); {
                int depth = 0;
                initial.holdArgs(depth, graph);
                //branchHandler.try_push_MT<void>(_f, -1, initial);
                branchHandler.force_push<void>(_f, -1, initial);
                //mvc(-1, 0, graph, nullptr);
            }
            //************************************************

            branchHandler.wait();
            graph_res = branchHandler.fetch_solution<Graph>();
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

        printf("refGlobal : %d \n", branchHandler.get_score());
        return true;
    }

    void mvc(int id, int depth, Graph graph, void *parent) {
        size_t LB = graph.min_k();
        size_t degLB = 0; //graph.DegLB();
        size_t UB = graph.max_k();
        size_t acLB = 0; //graph.antiColoringLB();
        //size_t mm = maximum_matching(graph);
        size_t k = relaxation(LB, UB);
        //std::max({LB, degLB, acLB})

        if (k + graph.coverSize() >= (size_t) branchHandler.get_score()) {
            //size_t addition = k + graph.coverSize();
            return;
        }

        if (graph.size() == 0) {
#ifdef GEMPBA_DEBUG_COMMENTS
            printf("Leaf reached, depth : %d \n", depth);
#endif
            terminate_condition(graph, id, depth);
            return;
        }

        int v = graph.id_max(false);
        int SIZE = graph.size();

        HolderType *dummyParent = nullptr;
        HolderType hol_l(dlb, id, parent);
        HolderType hol_r(dlb, id, parent);
        hol_l.setDepth(depth);
        hol_r.setDepth(depth);
        if (branchHandler.get_load_balancing_strategy() == gempba::QUASI_HORIZONTAL) {
            dummyParent = new HolderType(dlb, id);
            dlb.linkVirtualRoot(id, dummyParent, hol_l, hol_r);
        }

        hol_l.bind_branch_checkIn([&] {
            Graph g = graph;
            g.removeVertex(v);
            g.clean_graph();
            //g.removeZeroVertexDegree();
            int C = g.coverSize();

            if (C == 0) {
                spdlog::debug("rank {}, thread {}, cover is empty\n", branchHandler.rank_me(), id);
                throw;
            }
            if (C < branchHandler.get_score()) // user's condition to see if it's worth it to make branch call
            {
                int newDepth = depth + 1;
                hol_l.holdArgs(newDepth, g);
                return true; // it's worth it
            } else
                return false; // it's not worth it
        });

        hol_r.bind_branch_checkIn([&] {
            Graph g = graph;
            if (g.empty())
                spdlog::debug("rank {}, thread {}, Graph is empty\n", branchHandler.rank_me(), id);

            g.removeNv(v);
            g.clean_graph();
            //g.removeZeroVertexDegree();
            int C = g.coverSize();
            if (C < branchHandler.get_score()) // user's condition to see if it's worth it to make branch call
            {
                int newDepth = depth + 1;
                hol_r.holdArgs(newDepth, g);
                return true; // it's worth it
            } else
                return false; // it's not worth it
        });

        if (hol_l.evaluate_branch_checkIn()) {
            //int nbVertices = std::get<1>(hol_l.getArgs()).size(); //temp
            //if (nbVertices < 50)
            //	branchHandler.forward<void>(_f, id, hol_l);
            //else
            branchHandler.try_push_mt<void>(_f, id, hol_l);
        }
        if (hol_r.evaluate_branch_checkIn()) {
            branchHandler.forward<void>(_f, id, hol_r);
        }

        if (dummyParent)
            delete dummyParent;

        return;
    }

private:
    void terminate_condition(Graph &graph, int id, int depth) {
        std::scoped_lock<std::mutex> lck(mtx);
        if (graph.coverSize() < branchHandler.get_score()) {
            int SZ = graph.coverSize(); // debuggin line
            branchHandler.try_update_result(graph, gempba::score::make(graph.coverSize()));

            foundAtDepth = depth;
            recurrent_msg(id);

            if (depth > (int) measured_Depth)
                measured_Depth = (size_t) depth;

            ++leaves;
        }

    }
};

#endif // MT_GRAPH_OPT_ENC_SEMI_HPP
