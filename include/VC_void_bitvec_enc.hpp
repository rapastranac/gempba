#ifdef BITVECTOR_VC


#include "VertexCover.hpp"
#include <atomic>
#include <array>
#include <random>

#include <chrono>
#include <map>
#include <boost/dynamic_bitset.hpp>
#include <boost/container/set.hpp>
#include <boost/unordered_set.hpp>
#include <boost/unordered_map.hpp>
#include <boost/container/flat_map.hpp>

#include <memory_resource>

using namespace boost;


#define gbitset dynamic_bitset<>
#define gbits boost::container::flat_map<int, gbitset>


struct BitGraph {
    vector<int> in;
    gbits *graphbits_ptr = nullptr;
    gbits graphbits;
};



//SERIALIZE A DYNAMIC_BITSET
namespace boost {
    namespace serialization {

        template<typename Ar, typename Block, typename Alloc>
        void save(Ar &ar, dynamic_bitset<Block, Alloc> const &bs, unsigned) {

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









//SERIALIZE A GRAPHBITS OBJECT
namespace boost {
    namespace serialization {

        template<typename Ar>
        void save(Ar &ar, gbits const &graphbits, unsigned) {


            if (true) {
                ar << graphbits.size();
                for (const auto &keyvaluepair: graphbits) {
                    ar << keyvaluepair.first;
                    ar << keyvaluepair.second;
                }
                return;
            }

            //encoding format of each vertex is [vertex no] [nb neighbors] [list of nb neighbors ints]

            int32_t nbbits_per_bitset = 0;
            vector<int32_t> encoding;
            encoding.reserve(graphbits.size() * graphbits.size() /
                             10);    //heuristic way of reserving edges, because I don't want to count them


            for (const auto &keyvaluepair: graphbits) {
                encoding.push_back((int32_t) keyvaluepair.first);

                const gbitset &nbrs = keyvaluepair.second;
                nbbits_per_bitset = (int32_t) nbrs.size();

                encoding.push_back(nbrs.count());
                int poscount = encoding.size() - 1;

                int nbfwd = 0;
                for (int j = nbrs.find_first(); j != gbitset::npos; j = nbrs.find_next(j))
                    //for (int j = nbrs.find_next(keyvaluepair.first); j != gbitset::npos; j = nbrs.find_next(j))
                {
                    if (graphbits.find(j) != graphbits.end()) {
                        encoding.push_back((int32_t) j);
                        ++nbfwd;
                    }
                }
                encoding[poscount] = nbfwd;


            }
            size_t num_entries = encoding.size();


            ar << num_entries << nbbits_per_bitset;

            ar << boost::serialization::make_array(encoding.data(), encoding.size());
        }

        template<typename Ar>
        void load(Ar &ar, gbits &graphbits, unsigned) {

            if (true) {
                size_t num_entries;
                ar >> num_entries;


                for (int i = 0; i < num_entries; ++i) {
                    int v;
                    gbitset bits;
                    ar >> v;
                    ar >> bits;
                    graphbits[v] = bits;
                }
                return;
            }

            size_t num_entries;
            int32_t nbbits_per_bitset;
            ar >> num_entries;

            ar >> nbbits_per_bitset;

            std::vector<int32_t> encoding(num_entries);
            ar >> boost::serialization::make_array(encoding.data(), encoding.size());

            //map<int, vector<int>> future_edges;

            size_t cpt = 0;
            while (cpt < encoding.size()) {
                int32_t v = encoding[cpt];
                ++cpt;
                int32_t nbnbrs = encoding[cpt];
                ++cpt;

                gbitset vvec(nbbits_per_bitset);

                for (size_t i = 0; i < nbnbrs; ++i) {
                    int32_t w = encoding[cpt];
                    vvec[w] = true;

                    //future_edges[w].push_back(v);
                    ++cpt;
                }

                /*if (future_edges.find(v) != future_edges.end())
                {
                    for (auto z : future_edges[v])
                    {
                        vvec[z] = true;
                    }
                }*/

                graphbits[(int) v] = vvec;
            }

        }

        template<typename Ar>
        void serialize(Ar &ar, gbits &graphbits, unsigned version) {
            split_free(ar, graphbits, version);
        }

    }
}

void helper_ser(auto &archive, auto &first) {
    archive << first;
}

void helper_ser(auto &archive, auto &first, auto &...args) {
    archive << first;
    helper_ser(archive, args...);
}

auto serializer = [](auto &&...args) {
    /* here inside, user can implement its favourite serialization method given the
	arguments pack and it must return a std::string */
    std::stringstream ss;
    //cereal::BinaryOutputArchive archive(ss);
    //archive(args...);
    boost::archive::text_oarchive archive(ss);
    helper_ser(archive, args...);
    return ss.str();
};

void helper_dser(auto &archive, auto &first) {
    archive >> first;
}

void helper_dser(auto &archive, auto &first, auto &...args) {
    archive >> first;
    helper_dser(archive, args...);
}

auto deserializer = [](std::stringstream &ss, auto &...args) {
    /* here inside, the user can implement its favourite deserialization method given buffer
	and the arguments pack*/
    //cereal::BinaryInputArchive archive(ss);
    boost::archive::text_iarchive archive(ss);

    helper_dser(archive, args...);
    //archive(args...);
};

class VC_void_MPI_bitvec_enc : public VertexCover {
    //using HolderType = gempba::ResultHolder<void, int, gbitset, int, std::vector<int>>;
    using HolderType = gempba::ResultHolder<void, int, gbits, int>;

private:
    std::function<void(int, int, gbits &, int, void *)> _f;
    //std::function<void(int, int, gbitset &, int, std::vector<int>, void *)> _f;

public:
    //vector<boost::unordered_set<pair<gbitset,int>>> seen;
    long is_skips;
    long deglb_skips;
    long seen_skips;

    long timecopying;

    gbits init_graphbits;
    size_t passes;
    std::mutex mtx;

    VC_void_MPI_bitvec_enc() {

        this->_f = std::bind(&VC_void_MPI_bitvec_enc::mvcbitset, this, _1, _2, _3, _4, _5);
    }

    ~VC_void_MPI_bitvec_enc() {}

    void setGraph(Graph &graph) {
        timecopying = 0;
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
        vector<pair<int, int>> deg_v;
        for (auto it = graph.adj.begin(); it != graph.adj.end(); ++it) {
            deg_v.push_back(make_pair(it->second.size(), it->first));
        }

        //for some reason, I decided to sort the vertices by degree.  I don't think it is useful.
        std::sort(deg_v.begin(), deg_v.end());
        map<int, int> remap;
        for (int i = 0; i < deg_v.size(); i++) {
            remap[deg_v[i].second] = deg_v.size() - 1 - i;
        }
        map<int, set<int>> adj2;
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
            init_graphbits[v] = vnbrs;
        }

        //check for evil degree 0 vertices
        for (int i = 0; i < gsize; ++i) {
            if (init_graphbits.find(i) == init_graphbits.end()) {
                init_graphbits[i] = gbitset(gsize);
            }
        }
    }


    //void mvcbitset(int id, int depth, gbitset &bits_in_graph, int solsize, std::vector<int> dummy, void *parent)
    void mvcbitset(int id, int depth, gbits &graphbits, int solsize, void *parent = nullptr) {

        gbitset bits_in_graph(init_graphbits.size());

        for (const auto &kv_pair: graphbits) {
            bits_in_graph[kv_pair.first] = true;
        }




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
                    branchHandler.rank_me(), id, passes, bits_in_graph.count(),
                    branchHandler.refValue(), cursol_size, is_skips, deglb_skips,
                    std::ctime(&time));

            cout << str;


            //<<" seen_skips="<<seen_skips<<" seen.size="<<seen[id].size()<<endl;
            //cout<<"ID="<<id<<" CSOL="<<cursol_size<<" REFVAL="<<branchHandler.getRefValue()<<endl;
            //branchHandler.printDebugInfo();
        }

        //cout<<"depth="<<depth<<" ref="<<branchHandler.getRefValue()<<"cursolsize="<<cursol_size<<" cnt="<<bits_in_graph.count()<<" sol="<<cur_sol<<endl;
        if (graphbits.size() <= 1) {
            terminate_condition_bits(cursol_size, id, depth);
            //terminate_condition_bits(cursol_size, id, depth, dummy);
            return;
        }

        if (cursol_size >= branchHandler.refValue()) {
            return;
        }


        //max degree dude
        int maxdeg = 0;
        int maxdeg_v = 0;

        int nbEdgesDoubleCounted = 0;

        bool someRuleApplies = true;

        std::list<int> to_erase;

        while (someRuleApplies) {
            nbEdgesDoubleCounted = 0;
            maxdeg = -1;
            maxdeg_v = 0;
            someRuleApplies = false;

            for (const auto &kv_pair: graphbits) {
                int i = kv_pair.first;

                if (!bits_in_graph[i]) {
                    continue;
                }

                gbitset nbrs = (kv_pair.second & bits_in_graph);

                int cnt = nbrs.count();
                if (cnt == 0) {
                    bits_in_graph[i] = false;
                    to_erase.push_back(i);
                } else if (cnt == 1) {
                    int the_nbr = nbrs.find_first();
                    //cur_sol[the_nbr] = true;
                    cursol_size++;
                    bits_in_graph[i] = false;
                    bits_in_graph[the_nbr] = false;
                    someRuleApplies = true;
                    to_erase.push_back(i);
                    to_erase.push_back(the_nbr);
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

                            to_erase.push_back(i);
                            to_erase.push_back(n1);
                            to_erase.push_back(n2);
                        }
                    }

                }
                nbEdgesDoubleCounted += cnt;
            }
        }

        for (int er: to_erase) {
            if (er == maxdeg_v) {
                cout << "MAXDEG_ERASED!" << endl;
            }
            graphbits.erase(er);
        }

        int nbVertices = bits_in_graph.count();
        if (nbVertices <= 1) {
            terminate_condition_bits(cursol_size, id, depth);

            return;
        }

        float tmp = (float) (1 - 8 * nbEdgesDoubleCounted / 2 - 4 * nbVertices + 4 * nbVertices * nbVertices);
        int indsetub = (int) (0.5f * (1.0f + sqrt(tmp)));
        int vclb = nbVertices - indsetub;

        if (vclb + cursol_size >= branchHandler.refValue()) {
            is_skips++;
            return;
        }

        int degLB = 0; //getDegLB(bits_in_graph, nbEdgesDoubleCounted/2);
        degLB = (nbEdgesDoubleCounted / 2) / maxdeg;
        if (degLB + cursol_size >= branchHandler.refValue()) {
            deglb_skips++;
            return;
        }

        int newDepth = depth + 1;

        HolderType *dummyParent = nullptr;
        HolderType hol_l(dlb, id, parent);
        HolderType hol_r(dlb, id, parent);

        hol_l.setDepth(depth);
        hol_r.setDepth(depth);

        if (branchHandler.getLoadBalancingStrategy() == gempba::QUASI_HORIZONTAL) {
            dummyParent = new HolderType(dlb, id);
            dlb.linkVirtualRoot(id, dummyParent, hol_l, hol_r);
        }

        hol_l.bind_branch_checkIn([&] {
            int bestVal = branchHandler.refValue();

            //auto t1 = std::chrono::high_resolution_clock::now();
            gbits graphbits_maxdegremoved = graphbits;
            //auto t2 = std::chrono::high_resolution_clock::now();
            //timecopying += duration_cast<std::chrono::milliseconds>(t2 - t1).count();
            //for (const auto &kv : graphbits)
            {
                //	graphbits_maxdegremoved[kv.first] = kv.second;  //copies everything
            }
            graphbits_maxdegremoved.erase(
                    maxdeg_v);    //note: others think they have v as neighbor, but bits_in_graph solve this

            int solsize1 = cursol_size + 1;

            if (solsize1 < bestVal) {
                hol_l.holdArgs(newDepth, graphbits_maxdegremoved, solsize1);
                return true;
            } else
                return false;
        });

        hol_r.bind_branch_checkIn([&] {
            int bestVal = branchHandler.refValue();
            //right branch = take out v nbrs



            //auto t1 = std::chrono::high_resolution_clock::now();
            gbits graphbits_nbrsremoved = graphbits; //copies everything
            //auto t2 = std::chrono::high_resolution_clock::now();
            //timecopying += duration_cast<std::chrono::milliseconds>(t2 - t1).count();
            //for (const auto &kv : graphbits)
            {
                //	graphbits_nbrsremoved[kv.first] = kv.second;  //copies everything
            }


            gbitset nbrs = graphbits[maxdeg_v] & bits_in_graph;

            for (int j = nbrs.find_first(); j != gbitset::npos; j = nbrs.find_next(j)) {
                graphbits_nbrsremoved.erase(
                        j);    //note: this works because we are iterating on graphbits, not graphbits_nbrsremoved
            }


            int solsize2 = cursol_size + nbrs.count();

            if (solsize2 < bestVal) {
                hol_r.holdArgs(newDepth, graphbits_nbrsremoved, solsize2);
                return true;
            } else
                return false;
        });

        if (hol_l.evaluate_branch_checkIn()) {
            //if (nbVertices < 50)
            //    branchHandler.try_push_MT<void>(_f, id, hol_l);
            //else
            {

                branchHandler.try_push_MP<void>(_f, id, hol_l, serializer);
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

        if (solsize < branchHandler.refValue()) {
            //branchHandler.setBestVal(solsize);
            branchHandler.holdSolution(solsize, solsize, serializer);
            branchHandler.updateRefValue(solsize);

            auto clock = std::chrono::system_clock::now();
            std::time_t time = std::chrono::system_clock::to_time_t(clock); //it includes a "\n"

            fmt::print("rank {}, MVC solution so far: {} @ depth : {}, {}", branchHandler.rank_me(), solsize, depth,
                       std::ctime(&time));
            //fmt::print("dummy[0,...,3] = [{}, {}, {}, {}]\n", dummy[0], dummy[1], dummy[2], dummy[3]);
        }

        return;
    }
};

#endif
