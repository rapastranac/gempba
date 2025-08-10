 #ifndef MP_BITVECT_BASIC_ENC_CENTRAL_HPP
#define MP_BITVECT_BASIC_ENC_CENTRAL_HPP


#include "VertexCover.hpp"
#include <atomic>
#include <array>
#include <random>
#include <spdlog/spdlog.h>

#include <map>
#include <boost/dynamic_bitset.hpp>
#include <boost/container/set.hpp>
#include <boost/unordered_set.hpp>
#include <boost/unordered_map.hpp>
#include <boost/container/flat_map.hpp>
#include <format>
#include <functional>
#include <utils/ipc/task_packet.hpp>


#include <memory_resource>

using namespace boost;


#define gbitset dynamic_bitset<>
#define gbits boost::container::flat_map<int, gbitset>


gbits global_graphbits;


class BitGraph {
public:
    gbitset bits_in_graph;
};

namespace boost {
    namespace serialization {

        template<typename Ar, typename Block, typename Alloc>
        void save(Ar &ar, dynamic_bitset<Block, Alloc> const &bs, unsigned) {


            /*dynamic_bitset<Block, Alloc> dummy(bs);
            dummy.resize( bs.size() * 100 );

            size_t num_bits = dummy.size();
                std::vector<Block> blocks(dummy.num_blocks());
                to_block_range(dummy, blocks.begin());
                ar &num_bits &blocks;*/

            size_t num_bits = bs.size();
            std::vector<Block> blocks(bs.num_blocks());
            to_block_range(bs, blocks.begin());
            ar & num_bits & blocks;
        }

        template<typename Ar, typename Block, typename Alloc>
        void load(Ar &ar, dynamic_bitset<Block, Alloc> &bs, unsigned) {
            size_t num_bits;
            std::vector<Block> blocks;
            ar & num_bits & blocks;

            bs.resize(num_bits);
            from_block_range(blocks.begin(), blocks.end(), bs);

            bs.resize(num_bits);
            //bs.resize(num_bits / 100);
        }

        template<typename Ar, typename Block, typename Alloc>
        void serialize(Ar &ar, dynamic_bitset<Block, Alloc> &bs, unsigned version) {
            split_free(ar, bs, version);
        }

    }
}


namespace boost {
    namespace serialization {

        template<typename Ar>
        void save(Ar &ar, BitGraph const &bg, unsigned) {
            ar << bg.bits_in_graph;

            for (int i = bg.bits_in_graph.find_first(); i != gbitset::npos; i = bg.bits_in_graph.find_next(i)) {
                ar << (int32_t) i;
                ar << global_graphbits[i];
            }

        }

        template<typename Ar>
        void load(Ar &ar, BitGraph &bg, unsigned) {
            ar >> bg.bits_in_graph;

            for (int c = 0; c < bg.bits_in_graph.count(); ++c) {
                int32_t v;
                ar >> v;
                gbitset bits;
                ar >> bits;
            }
        }

        template<typename Ar>
        void serialize(Ar &ar, BitGraph &bg, unsigned version) {
            split_free(ar, bg, version);
        }

    }
}


void helper_ser(auto &archive, auto &first) {
    archive << first;
}

void helper_ser(auto &archive, auto &first, auto &... args) {
    archive << first;
    helper_ser(archive, args...);
}

auto serializer = [](auto &&... args) {
    /* here inside, user can implement its favourite serialization method given the
	arguments pack and it must return a std::string */
    std::stringstream ss;
    //cereal::BinaryOutputArchive archive(ss);
    //archive(args...);
    boost::archive::text_oarchive archive(ss);
    helper_ser(archive, args...);
    return gempba::task_packet(ss.str());
};

template<typename T>
std::function<gempba::task_packet(T&)> make_single_serializer() {
     return [&](T &p_arg) {
         gempba::task_packet v_ser = serializer(p_arg);
         return v_ser;
     };
 }

void helper_dser(auto &archive, auto &first) {
    archive >> first;
}

void helper_dser(auto &archive, auto &first, auto &... args) {
    archive >> first;
    helper_dser(archive, args...);
}

auto deserializer = [](std::stringstream &ss, auto &... args) {
    /* here inside, the user can implement its favourite deserialization method given buffer
	and the arguments pack*/
    //cereal::BinaryInputArchive archive(ss);
    boost::archive::text_iarchive archive(ss);

    helper_dser(archive, args...);
    //archive(args...);
};

class VC_void_MPI_bitvec_enc2 : public VertexCover {
    //using HolderType = gempba::ResultHolder<void, int, gbitset, int, std::vector<int>>;
    using HolderType = gempba::ResultHolder<void, int, BitGraph, int>;

private:
    std::function<void(int, int, BitGraph &, int, void *)> _f;
    //std::function<void(int, int, gbitset &, int, std::vector<int>, void *)> _f;

public:
    //vector<boost::unordered_set<pair<gbitset,int>>> seen;
    long is_skips;
    long deglb_skips;
    long seen_skips;

    gbits graphbits;
    std::atomic<size_t> passes;
    std::mutex mtx;

    VC_void_MPI_bitvec_enc2() {
        //this->_f = std::bind(&VC_void_MPI_bitvec::mvcbitset, this, _1, _2, _3, _4, _5, _6);
        this->_f = std::bind(&VC_void_MPI_bitvec_enc2::mvcbitset, this, _1, _2, _3, _4, _5);
    }

    ~VC_void_MPI_bitvec_enc2() {
    }

    void setGraph(Graph &graph) {
        is_skips = 0;
        deglb_skips = 0;
        seen_skips = 0;
        //this->branchHandler.setMaxThreads(numThreads);

        /*for (int i = 0; i <= numThreads; i++)
		{
			seen.push_back(boost::unordered_set<pair<gbitset,int>>());
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
        for (int i = 0; i < deg_v.size(); i++) {
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

            gbitset vnbrs(gsize);

            for (int i: it->second) {
                vnbrs[i] = true;
            }
            graphbits[v] = vnbrs;
        }

        //check for evil degree 0 vertices
        for (int i = 0; i < gsize; ++i) {
            if (!graphbits.contains(i)) {
                graphbits[i] = gbitset(gsize);
            }
        }

        global_graphbits = graphbits;
    }


    //void mvcbitset(int id, int depth, gbitset &bits_in_graph, int solsize, std::vector<int> dummy, void *parent)
    void mvcbitset(int id, int depth, BitGraph &bg, int solsize, void *parent = nullptr) {

        gbitset &bits_in_graph = bg.bits_in_graph;
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

            auto ctime = std::ctime(&time);
            auto str = std::format(
                    "WR= {} ID= {} passes={} gsize={} refvalue={} solsize={} isskips={} deglbskips={} {}",
                    branchHandler.rank_me(), id, passes.load(), bits_in_graph.count(),
                    branchHandler.reference_value(), cursol_size, is_skips, deglb_skips,
                    ctime);

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

        if (cursol_size >= branchHandler.reference_value()) {
            return;
        }

        /*if (bits_in_graph.count() <= 90 && bits_in_graph.count() >= 120 )
		{
			pair<gbitset, int> instance_key = make_pair(bits_in_graph, cursol_size);
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

            for (int i = bits_in_graph.find_first(); i != gbitset::npos; i = bits_in_graph.find_next(i)) {

                gbitset nbrs = (graphbits[i] & bits_in_graph);

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
						for (int j = nbrs.find_first(); j != gbitset::npos; j = nbrs.find_next(j))
						{
							gbitset nbrs_of_j = (graphbits[j] & bits_in_graph);
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

        if (vclb + cursol_size >= branchHandler.reference_value()) {
            is_skips++;
            return;
        }

        int degLB = 0; //getDegLB(bits_in_graph, nbEdgesDoubleCounted/2);
        degLB = (nbEdgesDoubleCounted / 2) / maxdeg;
        //cout<<"deglb="<<degLB<<" n="<<bits_in_graph.count()<<" refval="<<branchHandler.getRefValue()<<endl;
        if (degLB + cursol_size >= branchHandler.reference_value()) {
            deglb_skips++;
            return;
        }

        int newDepth = depth + 1;

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
            int bestVal = branchHandler.reference_value();
            gbitset ingraph1 = bits_in_graph;

            if (!ingraph1[maxdeg_v]) {
                cout << "ERROR : maxdeg_v already gone" << endl;
            }
            ingraph1.set(maxdeg_v, false);
            //gbitset sol1 = cur_sol;
            //sol1.set(maxdeg_v, true);
            int solsize1 = cursol_size + 1;

            if (solsize1 < bestVal) {
                //auto cpy = dummy;
                //hol_l.holdArgs(newDepth, ingraph1, solsize1, cpy);
                BitGraph bg;
                bg.bits_in_graph = ingraph1;
                hol_l.holdArgs(newDepth, bg, solsize1);
                return true;
            } else
                return false;
        });

        hol_r.bind_branch_checkIn([&] {
            int bestVal = branchHandler.reference_value();
            //right branch = take out v nbrs
            gbitset ingraph2 = bits_in_graph;

            ingraph2 = bits_in_graph & (~graphbits[maxdeg_v]);
            gbitset nbrs = (graphbits[maxdeg_v] & bits_in_graph);
            //gbitset sol2 = cur_sol | nbrs;	//add all nbrs to solution
            int solsize2 = cursol_size + nbrs.count();

            if (solsize2 < bestVal) {
                BitGraph bg;
                bg.bits_in_graph = ingraph2;
                hol_r.holdArgs(newDepth, bg, solsize2);
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

                branchHandler.try_push_mp<void>(_f, id, hol_l, serializer);
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

        if (solsize < branchHandler.reference_value()) {
            //branchHandler.setBestVal(solsize);
            std::function<gempba::task_packet(int &)> v_serializer = make_single_serializer<int>();
            branchHandler.try_update_result(solsize, solsize, v_serializer);
            branchHandler.try_update_reference_value(solsize);

            auto clock = std::chrono::system_clock::now();
            std::time_t time = std::chrono::system_clock::to_time_t(clock); //it includes a "\n"

            spdlog::debug("rank {}, MVC solution so far: {} @ depth : {}, {}", branchHandler.rank_me(), solsize, depth,
                          std::ctime(&time));
            //spdlog::debug("dummy[0,...,3] = [{}, {}, {}, {}]\n", dummy[0], dummy[1], dummy[2], dummy[3]);
        }

        return;
    }
};


#endif // MP_BITVECT_BASIC_ENC_CENTRAL_HPP
