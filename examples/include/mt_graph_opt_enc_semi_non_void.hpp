#ifndef GEMPBA_MT_GRAPH_OPT_ENC_SEMI_NON_VOID_HPP
#define GEMPBA_MT_GRAPH_OPT_ENC_SEMI_NON_VOID_HPP

#include "DLB/DLB_Handler.hpp"
#include "VertexCover.hpp"

// TODO... The whole algorithm passes by value, which is not efficient. We should pass by reference.

namespace gempba {

    class VC_non_void : public VertexCover {
        using HolderType = gempba::ResultHolder<Graph, int, Graph>;

    private:
        std::function<Graph(int, int, Graph, void *)> _f;

    public:
        VC_non_void() {
            this->_f = std::bind(&VC_non_void::mvc, this, _1, _2, _3, _4);
        }

        ~VC_non_void() override {
        }

        bool findCover(int run) {
            string msg_center = fmt::format("run # {} ", run);
            msg_center = "!" + fmt::format("{:-^{}}", msg_center, wide - 2) + "!" + "\n";
            cout << msg_center;
            outFile(msg_center, "");

            this->branchHandler.initThreadPool(numThreads);
            preSize = graph.preprocessing();

            size_t k_mm = maximum_matching(graph);
            size_t k_uBound = graph.max_k();
            size_t k_lBound = graph.min_k();
            size_t k_prime = std::min(k_mm, k_uBound) + graph.coverSize();
            currentMVCSize = k_prime;

            begin = std::chrono::steady_clock::now();

            try {
                branchHandler.setRefValue(currentMVCSize);
                HolderType *dummyParent = new HolderType(dlb, -1); {
                    graph_res = mvc(-1, 0, graph, dummyParent);
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

            printf("refGlobal : %d \n", branchHandler.refValue());
            return true;
        }

        Graph mvc(int id, int depth, Graph graph, void *parent) {
            size_t LB = graph.min_k();
            size_t degLB = 0; // graph.DegLB();
            //            size_t UB = graph.max_k();
            size_t acLB = 0; // graph.antiColoringLB();
            //size_t mm = maximum_matching(graph);
            //size_t k = relaxation(k1, k2);

            if (graph.coverSize() + std::max({LB, degLB, acLB}) >= (size_t) branchHandler.refValue()) {
                //size_t addition = k + graph.coverSize();
                //return;
                return {};
            }

            if (graph.size() == 0) {
#ifdef GEMPBA_DEBUG_COMMENTS
                printf("Leaf reached, depth : %d \n", depth);
#endif
                return termination(graph);
            }
            int newDepth = depth + 1;

            int v = graph.id_max(false);
            HolderType holderLeft(dlb, id, parent);
            HolderType holderRight(dlb, id, parent);
            holderLeft.setDepth(depth);
            holderRight.setDepth(depth);

            int referenceValue = branchHandler.refValue();

            holderLeft.bind_branch_checkIn([&graph, &v, referenceValue, &depth, &holderLeft] {
                Graph g = graph;
                g.removeVertex(v);
                g.clean_graph();
                //g.removeZeroVertexDegree();
                int C = g.coverSize();
                if (C < referenceValue) {
                    // user's condition to see if it's worth it to make branch call
                    int newDepth = depth + 1;
                    holderLeft.holdArgs(newDepth, g);
                    return true; // it's worth it
                } else {
                    return false; // it's not worth it
                }
            });

            holderRight.bind_branch_checkIn([&graph, &v, referenceValue, &depth, &holderRight] {
                Graph g = std::move(graph);
                g.removeNv(v);
                g.clean_graph();
                //g.removeZeroVertexDegree();
                int C = g.coverSize();
                if (C < referenceValue) {
                    // user's condition to see if it's worth it to make branch call
                    int newDepth = depth + 1;
                    holderRight.holdArgs(newDepth, g);
                    return true; // it's worth it
                } else {
                    return false; // it's not worth it
                }
            });

            //*******************************************************************************************
            Graph leftGraph;
            Graph rightGraph;

            if (holderLeft.evaluate_branch_checkIn()) {
                branchHandler.try_push_MT<Graph>(_f, id, holderLeft);
            }

            if (holderRight.evaluate_branch_checkIn()) {
                rightGraph = branchHandler.forward<Graph>(_f, id, holderRight);
            }

            if (holderLeft.isFetchable()) {
                leftGraph = holderLeft.get();
            }

            return returnRes(leftGraph, rightGraph);
        }

        Graph termination(Graph &graph) {
            bool updated = branchHandler.updateRefValue(graph.size());

            if (updated) {
                return graph;
            } else {
                return {};
            }
        }

        static Graph returnRes(Graph left, Graph right) {
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

}

#endif //GEMPBA_MT_GRAPH_OPT_ENC_SEMI_NON_VOID_HPP
