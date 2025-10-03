#ifndef MP_BITVECT_OPT_ENC_CENTRAL_HPP
#define MP_BITVECT_OPT_ENC_CENTRAL_HPP


#include <atomic>
#include <functional>
#include <random>
#include <boost/dynamic_bitset.hpp>
#include <spdlog/spdlog.h>

#include <utils/ipc/task_packet.hpp>
#include "VertexCover.hpp"
#include "result_holder/result_holder.hpp"
#include "optimized_encoding_utils.hpp"

class VC_void_MPI_bitvec : public VertexCover {
    using HolderType = gempba::result_holder<void, int, G_BITSET, int>;

    std::function<void(int, int, G_BITSET &, int, void *)> _f;

public:
    //vector<boost::unordered_set<pair<G_BITSET,int>>> seen;
    long is_skips;
    long deglb_skips;
    long seen_skips;

    unordered_map<int, G_BITSET > graphbits;
    std::atomic<size_t> passes;
    std::mutex mtx;

    VC_void_MPI_bitvec() {
        //this->_f = std::bind(&VC_void_MPI_bitvec::mvcbitset, this, _1, _2, _3, _4, _5, _6);
        this->_f = std::bind(&VC_void_MPI_bitvec::mvcbitset, this, _1, _2, _3, _4, _5);
    }

    ~VC_void_MPI_bitvec() {
    }

    void setGraph(Graph &graph) {
        is_skips = 0;
        deglb_skips = 0;
        seen_skips = 0;
        //this->branchHandler.setMaxThreads(numThreads);

        /*for (int i = 0; i <= numThreads; i++)
		{
			seen.push_back(boost::unordered_set<pair<G_BITSET,int>>());
		}*/

        passes = 0;
        int gsize = graph.adj.size() + 1; //+1 cuz some files use node ids from 1 to n (instead of 0 to n - 1)

        cout << "Graph has " << graph.adj.size() << " vertices and " << graph.getNumEdges() << " edges" << endl;
        vector<pair<int, int> > deg_v;
        for (auto it = graph.adj.begin(); it != graph.adj.end(); ++it) {
            deg_v.push_back(make_pair(it->second.size(), it->first));
        }

        //for some reason, I decided to sort the vertices by degree.  I don't think it is useful.
        std::sort(deg_v.begin(), deg_v.end());
        map<int, int> remap;
        for (int i = 0; i < static_cast<int>(deg_v.size()); i++) {
            remap[deg_v[i].second] = deg_v.size() - 1 - i;
        }
        map<int, set<int> > adj2;
        for (auto it = graph.adj.begin(); it != graph.adj.end(); ++it) {
            int v = it->first;
            adj2[remap[v]] = set<int>();
            for (auto w: graph.adj[v]) {
                adj2[remap[v]].insert(remap[w]);
            }
        }

        //for (auto it = graph.adj.begin(); it != graph.adj.end(); ++it)
        for (auto it = adj2.begin(); it != adj2.end(); ++it) {
            int v = it->first;

            G_BITSET vnbrs(gsize);

            for (int i: it->second) {
                vnbrs[i] = true;
            }
            graphbits[v] = vnbrs;
        }

        //check for evil degree 0 vertices
        for (int i = 0; i < gsize; ++i) {
            if (!graphbits.contains(i)) {
                graphbits[i] = G_BITSET(gsize);
            }
        }
    }


    //void mvcbitset(int id, int depth, G_BITSET &bits_in_graph, int solsize, std::vector<int> dummy, void *parent)
    void mvcbitset(int id, int depth, G_BITSET &bits_in_graph, int solsize, void *parent = nullptr) {

        //{                                                   // 1 MB, emulates heavy messaging
        //    std::random_device rd;                          // Will be used to obtain a seed for the random number engine
        //    std::mt19937 gen(rd());                         // Standard mersenne_twister_engine seeded with rd()
        //    std::uniform_int_distribution<> distrib(0, 10); // uniform distribution [closed interval]
        //    for (size_t i = 0; i < dummy.size(); i++)
        //    {
        //        dummy[i] += distrib(gen); // random value in range
        //    }
        //}

        passes++;
        //branchHandler.passes = passes;

        /*cout<<bits_in_graph.size()<<endl;
		cout<<cur_sol.size()<<endl;
		cout<<graphbits.size()<<endl;
		cout<<graphbits[0].size()<<endl;*/

        int cursol_size = solsize;

        if (passes % (size_t) 1e6 == 0) {
            auto clock = std::chrono::system_clock::now();
            std::time_t time = std::chrono::system_clock::to_time_t(clock); //it includes a "\n"

            auto str = fmt::format(
                    "WR= {} ID= {} passes={} gsize={} refvalue={} solsize={} isskips={} deglbskips={} {}",
                    branchHandler.rank_me(), id, passes.load(), bits_in_graph.count(),
                    branchHandler.get_score().get<int>(), cursol_size, is_skips, deglb_skips,
                    std::ctime(&time));

            cout << str;

            //<<" seen_skips="<<seen_skips<<" seen.size="<<seen[id].size()<<endl;
            //cout<<"ID="<<id<<" CSOL="<<cursol_size<<" REFVAL="<<branchHandler.getRefValue()<<endl;
            //branchHandler.printDebugInfo();
        }

        //cout<<"depth="<<depth<<" ref="<<branchHandler.getRefValue()<<"cursolsize="<<cursol_size<<" cnt="<<bits_in_graph.count()<<" sol="<<cur_sol<<endl;
        if (bits_in_graph.count() <= 1) {

            terminate_condition_bits(cursol_size, id, depth);
            //terminate_condition_bits(cursol_size, id, depth, dummy);
            return;
        }

        if (cursol_size >= branchHandler.get_score().get<int>()) {
            return;
        }

        /*if (bits_in_graph.count() <= 90 && bits_in_graph.count() >= 120 )
		{
			pair<G_BITSET, int> instance_key = make_pair(bits_in_graph, cursol_size);
			if (seen[id].find(instance_key) != seen[id].end())
			{
				seen_skips++;
				return;
			}

			if (seen[id].size() <= 4000000)
			{
				seen[id].insert(instance_key);
			}
		}*/

        //max degree dude
        int maxdeg = 0;
        int maxdeg_v = 0;

        int nbEdgesDoubleCounted = 0;

        bool someRuleApplies = true;

        while (someRuleApplies) {
            nbEdgesDoubleCounted = 0;
            maxdeg = -1;
            maxdeg_v = 0;
            someRuleApplies = false;

            for (size_t i = bits_in_graph.find_first(); i != G_BITSET::npos; i = bits_in_graph.find_next(i)) {

                G_BITSET nbrs = (graphbits[i] & bits_in_graph);

                int cnt = nbrs.count();
                if (cnt == 0) {
                    bits_in_graph[i] = false;
                } else if (cnt == 1) {
                    int the_nbr = nbrs.find_first();
                    //cur_sol[the_nbr] = true;
                    cursol_size++;
                    bits_in_graph[i] = false;
                    bits_in_graph[the_nbr] = false;
                    someRuleApplies = true;
                } else if (cnt > maxdeg) {
                    maxdeg = cnt;
                    maxdeg_v = i;
                } else {
                    //looking for a nbr with at least same nbrs as i
                    if (cnt == 2) {
                        int n1 = nbrs.find_first();
                        int n2 = nbrs.find_next(n1);
                        if (graphbits[n1][n2]) {
                            //cur_sol[n1] = true;
                            //cur_sol[n2] = true;
                            bits_in_graph[i] = false;
                            bits_in_graph[n1] = false;
                            bits_in_graph[n2] = false;
                            cursol_size += 2;
                            someRuleApplies = true;
                        }
                    }
                    /*
					{
						for (int j = nbrs.find_first(); j != G_BITSET::npos; j = nbrs.find_next(j))
						{
							G_BITSET nbrs_of_j = (graphbits[j] & bits_in_graph);
							nbrs_of_j.set(i, false);
							nbrs_of_j.set(j, true);
							if ((nbrs_of_j & nbrs) == nbrs)
							{
								//cout<<"we have twins at (i, j)="<<i<<","<<j<<endl<<
								//	nbrs<<endl<<(graphbits[j] & bits_in_graph)<<endl<<(nbrs_of_j & nbrs)<<endl;
								cur_sol[j] = true;
								cursol_size++;
								//bits_in_graph[i] = false;
								bits_in_graph[j] = false;
								someRuleApplies = true;
								break;
							}

						}
					}*/
                }
                nbEdgesDoubleCounted += cnt;
            }
        }

        int nbVertices = bits_in_graph.count();
        if (nbVertices <= 1) {
            //cout<<"terminating 2"<<endl;
            terminate_condition_bits(cursol_size, id, depth);
            //terminate_condition_bits(cursol_size, id, depth, dummy);

            return;
        }

        float tmp = (float) (1 - 8 * nbEdgesDoubleCounted / 2 - 4 * nbVertices + 4 * nbVertices * nbVertices);
        int indsetub = (int) (0.5f * (1.0f + sqrt(tmp)));
        int vclb = nbVertices - indsetub;

        if (vclb + cursol_size >= branchHandler.get_score().get<int>()) {
            is_skips++;
            return;
        }

        int degLB = 0; //getDegLB(bits_in_graph, nbEdgesDoubleCounted/2);
        degLB = (nbEdgesDoubleCounted / 2) / maxdeg;
        //cout<<"deglb="<<degLB<<" n="<<bits_in_graph.count()<<" refval="<<branchHandler.getRefValue()<<endl;
        if (degLB + cursol_size >= branchHandler.get_score().get<int>()) {
            deglb_skips++;
            return;
        }

        int newDepth = depth + 1;

        HolderType *dummyParent = nullptr;
        HolderType hol_l(dlb, id, parent);
        HolderType hol_r(dlb, id, parent);

        hol_l.setDepth(depth);
        hol_r.setDepth(depth);

        if (branchHandler.get_balancing_policy() == gempba::QUASI_HORIZONTAL) {
            dummyParent = new HolderType(dlb, id);
            dlb.linkVirtualRoot(id, dummyParent, hol_l, hol_r);
        }

        hol_l.bind_branch_checkIn([&] {
            int bestVal = branchHandler.get_score().get<int>();
            G_BITSET ingraph1 = bits_in_graph;

            if (!ingraph1[maxdeg_v]) {
                cout << "ERROR : maxdeg_v already gone" << endl;
            }
            ingraph1.set(maxdeg_v, false);
            //G_BITSET sol1 = cur_sol;
            //sol1.set(maxdeg_v, true);
            int solsize1 = cursol_size + 1;

            if (solsize1 < bestVal) {
                //auto cpy = dummy;
                //hol_l.holdArgs(newDepth, ingraph1, solsize1, cpy);
                hol_l.holdArgs(newDepth, ingraph1, solsize1);
                return true;
            } else
                return false;
        });

        hol_r.bind_branch_checkIn([&] {
            int bestVal = branchHandler.get_score().get<int>();
            //right branch = take out v nbrs
            G_BITSET ingraph2 = bits_in_graph;

            ingraph2 = bits_in_graph & (~graphbits[maxdeg_v]);
            G_BITSET nbrs = (graphbits[maxdeg_v] & bits_in_graph);
            //G_BITSET sol2 = cur_sol | nbrs;	//add all nbrs to solution
            int solsize2 = cursol_size + nbrs.count();

            if (solsize2 < bestVal) {
                hol_r.holdArgs(newDepth, ingraph2, solsize2);
                //auto cpy = dummy;
                //hol_r.holdArgs(newDepth, ingraph2, solsize2, cpy);
                return true;
            } else
                return false;
        });

        if (hol_l.evaluate_branch_checkIn()) {
            //if (nbVertices < 50)
            //    branchHandler.forward<void>(_f, id, hol_l);
            // else
            {

                branchHandler.try_remote_submit<void>(_f, id, hol_l, serializer);
            }
        } else {
        }

        //cout<<"ok sol1 done, going to sol2 val="<<solsize2;

        if (hol_r.evaluate_branch_checkIn()) {
            branchHandler.forward<void>(_f, id, hol_r);
        } else {
        }

        //cout<<"depth="<<depth<<" done"<<endl;

        if (dummyParent)
            delete dummyParent;

        return;
    }

private:
    //void terminate_condition_bits(int solsize, int id, int depth, auto &dummy)
    void terminate_condition_bits(int solsize, int id, int depth) {
        if (solsize == 0)
            return;

        if (solsize < branchHandler.get_score().get<int>()) {
            //branchHandler.setBestVal(solsize);
            std::function<gempba::task_packet(int &)> v_serializer = make_single_serializer<int>();
            branchHandler.try_update_result(solsize, gempba::score::make(solsize), v_serializer);

            auto clock = std::chrono::system_clock::now();
            std::time_t time = std::chrono::system_clock::to_time_t(clock); //it includes a "\n"

            spdlog::debug("rank {}, MVC solution so far: {} @ depth : {}, {}", branchHandler.rank_me(), solsize, depth,
                          std::ctime(&time));
            //spdlog::debug("dummy[0,...,3] = [{}, {}, {}, {}]\n", dummy[0], dummy[1], dummy[2], dummy[3]);
        }

        return;
    }
};

#endif // MP_BITVECT_OPT_ENC_CENTRAL_HPP
