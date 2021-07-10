#ifdef VC_NON_VOID

#include "VertexCover.hpp"

class VC_non_void : public VertexCover
{
    using HolderType = library::ResultHolder<Graph, int, Graph>;

private:
    std::function<Graph(int, int, Graph, void *)> _f;

public:
    VC_non_void()
    {
        this->_f = std::bind(&VC_non_void::mvc, this, _1, _2, _3, _4);
    }
    ~VC_non_void() {}

    bool findCover(int run)
    {
        string msg_center = fmt::format("run # {} ", run);
        msg_center = "!" + fmt::format("{:-^{}}", msg_center, wide - 2) + "!" + "\n";
        cout << msg_center;
        outFile(msg_center, "");

        this->branchHandler.setMaxThreads(numThreads);
        preSize = graph.preprocessing();

        size_t k_mm = maximum_matching(graph);
        size_t k_uBound = graph.max_k();
        size_t k_lBound = graph.min_k();
        size_t k_prime = std::min(k_mm, k_uBound) + graph.coverSize();
        currentMVCSize = k_prime;

        begin = std::chrono::steady_clock::now();

        try
        {
            branchHandler.setRefValue(currentMVCSize);
            {
                int depth = 0;
                graph_res = mvc(-1, 0, graph, nullptr);
            }

            graph_res2 = graph_res;
            cover = graph_res.postProcessing();
        }
        catch (std::exception &e)
        {
            this->output.open(outPath, std::ofstream::in | std::ofstream::out | std::ofstream::app);
            if (!output.is_open())
            {
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

    Graph mvc(int id, int depth, Graph graph, void *parent)
    {
        size_t LB = graph.min_k();
        size_t degLB = 0; // graph.DegLB();
        size_t UB = graph.max_k();
        size_t acLB = 0; // graph.antiColoringLB();
        //size_t mm = maximum_matching(graph);
        //size_t k = relaxation(k1, k2);

        if (graph.coverSize() + std::max({LB, degLB, acLB}) >= (size_t)branchHandler.refValue())
        {
            //size_t addition = k + graph.coverSize();
            //return;
            return {};
        }

        if (graph.size() == 0)
        {
#ifdef DEBUG_COMMENTS
            printf("Leaf reached, depth : %d \n", depth);
#endif
            return termination(graph, id, depth);
        }
        int newDepth = depth + 1;

        int v = graph.id_max(false);
        HolderType hol_l(branchHandler, id, parent);
        HolderType hol_r(branchHandler, id, parent);
        hol_l.setDepth(depth);
        hol_r.setDepth(depth);
#ifdef DLB
        branchHandler.linkVirtualRoot(id, parent, hol_l, hol_r);
#endif
        int *referenceValue = branchHandler.refValueTest();

        hol_l.bind_branch_checkIn([&graph, &v, referenceValue, &depth, &hol_l] {
            Graph g = graph;
            g.removeVertex(v);
            g.clean_graph();
            //g.removeZeroVertexDegree();
            int C = g.coverSize();
            if (C < referenceValue[0]) // user's condition to see if it's worth it to make branch call
            {
                int newDepth = depth + 1;
                hol_l.holdArgs(newDepth, g);
                return true; // it's worth it
            }
            else
                return false; // it's not worth it
        });

        hol_r.bind_branch_checkIn([&graph, &v, referenceValue, &depth, &hol_r] {
            Graph g = std::move(graph);
            g.removeNv(v);
            g.clean_graph();
            //g.removeZeroVertexDegree();
            int C = g.coverSize();
            if (C < referenceValue[0]) // user's condition to see if it's worth it to make branch call
            {
                int newDepth = depth + 1;
                hol_r.holdArgs(newDepth, g);
                return true; // it's worth it
            }
            else
                return false; // it's not worth it
        });

        //*******************************************************************************************
        Graph r_left;
        Graph r_right;

        if (hol_l.evaluate_branch_checkIn())
        {
#ifdef DLB
            branchHandler.push_multithreading<Graph>(_f, id, hol_l, true);
#else
            branchHandler.push_multithreading<Graph>(_f, id, hol_l);
#endif
        }

        if (hol_r.evaluate_branch_checkIn())
        {
#ifdef DLB
            r_right = branchHandler.forward<Graph>(_f, id, hol_r, true);
#else
            r_right = branchHandler.forward<Graph>(_f, id, hol_r);
#endif
        }

        if (hol_l.isFetchable())
            r_left = hol_l.get();

        return returnRes(r_left, r_right);
    }

    Graph termination(Graph &graph, int id, int depth)
    {
        auto condition1 = [this](int refValGlobal, int refValLocal) {
            return (leaves == 0) && (refValLocal < refValGlobal) ? true : false;
        };

        //if condition1 complies, then ifCond1 is called
        auto ifCond1 = [&]() {
            foundAtDepth = depth;
            recurrent_msg(id);
            ++leaves;
        };

        auto condition2 = [](int refValGlobal, int refValLocal) {
            return refValLocal < refValGlobal ? true : false;
        };

        auto ifCond2 = [&]() {
            foundAtDepth = depth;
            recurrent_msg(id);
            if (depth > measured_Depth)
            {
                measured_Depth = depth;
            }

            ++leaves;
        };

        bool rcnd1 = branchHandler.replace_refValGlobal_If<Graph>(graph.coverSize(), condition1, ifCond1, graph); // thread safe
        bool rcnd2 = branchHandler.replace_refValGlobal_If<Graph>(graph.coverSize(), condition2, ifCond2, graph);

        if (rcnd1 || rcnd2)
            return graph;
        else
            return {};
    }

    Graph returnRes(Graph &left, Graph &right)
    {
        if (!left.empty() && !right.empty())
        {
            if (left.coverSize() >= right.coverSize())
                return right;
            else
                return left;
        }
        else if (!left.empty() && right.empty())
            return left;
        else if (left.empty() && !right.empty())
            return right;
        else
            return {};
    }
};

#endif