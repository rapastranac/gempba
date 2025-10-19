#ifndef MP_BITVECTOR_BASIC_ENCODING_CENTRAL_HPP
#define MP_BITVECTOR_BASIC_ENCODING_CENTRAL_HPP

#include <atomic>
#include <format>
#include <functional>
#include <map>
#include <gempba/gempba.hpp>
#include <spdlog/spdlog.h>

#include "VertexCover.hpp"
#include "basic_encoding_utils.hpp"


class mp_bitvector_basic_encoding final : public VertexCover {

    std::function<void(std::thread::id, int, BitGraph, int, gempba::node)> m_function;
    std::function<gempba::task_packet(int, BitGraph, int)> m_args_serializer;
    std::function<std::tuple<int, BitGraph, int>(gempba::task_packet)> m_args_deserializer;

public:
    long is_skips;
    long deglb_skips;
    long seen_skips;

    G_BITS graphbits;
    std::atomic<size_t> passes;
    std::mutex mtx;

    gempba::node_manager &m_node_manager;
    gempba::load_balancer *m_load_balancer;

    mp_bitvector_basic_encoding(gempba::node_manager &p_node_manager, gempba::load_balancer *p_load_balancer) :
        m_node_manager(p_node_manager), m_load_balancer(p_load_balancer) {
        this->m_function = std::bind(&mp_bitvector_basic_encoding::mvcbitset, this, _1, _2, _3, _4, _5);
        this->m_args_deserializer = create_deserializer();
        this->m_args_serializer = make_serializer();
    }

    ~mp_bitvector_basic_encoding() override = default;

    void setGraph(Graph &graph) {
        is_skips = 0;
        deglb_skips = 0;
        seen_skips = 0;

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

        global_graphbits = graphbits;
    }

    static std::string thread_id_to_string(const std::thread::id p_id) {
        std::ostringstream v_oss;
        v_oss << p_id;
        return v_oss.str();
    }


    void mvcbitset(std::thread::id p_id, int depth, BitGraph bg, int solsize, gempba::node p_parent) {

        G_BITSET &bits_in_graph = bg.bits_in_graph;
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

        int v_cursol_size = solsize;

        if (passes % (size_t) 1e6 == 0) {
            auto clock = std::chrono::system_clock::now();
            const std::time_t v_time = std::chrono::system_clock::to_time_t(clock); //it includes a "\n"

            auto v_ctime = std::string(std::ctime(&v_time));
            const auto v_str = std::format(
                    "WR= {} ID= {} passes={} gsize={} refvalue={} solsize={} isskips={} deglbskips={} {}",
                    m_node_manager.rank_me(), thread_id_to_string(p_id), passes.load(), bits_in_graph.count(),
                    m_node_manager.get_score().get<int>(), v_cursol_size, is_skips, deglb_skips,
                    v_ctime);

            cout << v_str;
        }

        if (bits_in_graph.count() <= 1) {

            terminate_condition_bits(v_cursol_size, depth);
            return;
        }

        if (v_cursol_size >= m_node_manager.get_score().get<int>()) {
            return;
        }

        //max degree dude
        int maxdeg = 0;
        int maxdeg_v = 0;

        int nbEdgesDoubleCounted = 0;

        bool some_rule_applies = true;

        while (some_rule_applies) {
            nbEdgesDoubleCounted = 0;
            maxdeg = -1;
            maxdeg_v = 0;
            some_rule_applies = false;

            for (int i = bits_in_graph.find_first(); i != G_BITSET::npos; i = bits_in_graph.find_next(i)) {

                G_BITSET nbrs = (graphbits[i] & bits_in_graph);

                int cnt = nbrs.count();
                if (cnt == 0) {
                    bits_in_graph[i] = false;
                } else if (cnt == 1) {
                    int the_nbr = nbrs.find_first();
                    //cur_sol[the_nbr] = true;
                    v_cursol_size++;
                    bits_in_graph[i] = false;
                    bits_in_graph[the_nbr] = false;
                    some_rule_applies = true;
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
                            v_cursol_size += 2;
                            some_rule_applies = true;
                        }
                    }

                }
                nbEdgesDoubleCounted += cnt;
            }
        }

        const int nb_vertices = bits_in_graph.count();
        if (nb_vertices <= 1) {
            terminate_condition_bits(v_cursol_size, depth);
            return;
        }

        const float tmp = (float) (1 - 8 * nbEdgesDoubleCounted / 2 - 4 * nb_vertices + 4 * nb_vertices * nb_vertices);
        const int indsetub = (int) (0.5f * (1.0f + sqrt(tmp)));
        const int vclb = nb_vertices - indsetub;

        if (vclb + v_cursol_size >= m_node_manager.get_score().get<int>()) {
            is_skips++;
            return;
        }

        int deg_lb = 0;
        deg_lb = (nbEdgesDoubleCounted / 2) / maxdeg;
        if (deg_lb + v_cursol_size >= m_node_manager.get_score().get<int>()) {
            deglb_skips++;
            return;
        }

        int newDepth = depth + 1;
        const gempba::node v_parent = p_parent == nullptr ? gempba::create_dummy_node(*m_load_balancer) : p_parent;


        std::function<std::optional<std::tuple<int, BitGraph, int> >()> v_left_args_initializer = [&]() -> std::optional<std::tuple<int, BitGraph, int> > {
            G_BITSET v_ingraph1 = bits_in_graph;

            if (!v_ingraph1[maxdeg_v]) {
                std::cout << "ERROR : maxdeg_v already gone" << std::endl;
            }
            v_ingraph1.set(maxdeg_v, false);
            int v_solution_size1 = v_cursol_size + 1;
            const int v_best_val = m_node_manager.get_score().get<int>();

            if (v_solution_size1 < v_best_val) {
                BitGraph v_bg;
                v_bg.bits_in_graph = v_ingraph1;
                return std::make_tuple(newDepth, v_bg, v_solution_size1);
            }
            return std::nullopt;
        };

        gempba::node v_left = gempba::mp::create_lazy_node<void>(
                *m_load_balancer,
                v_parent, m_function,
                v_left_args_initializer,
                m_args_serializer,
                m_args_deserializer
                );

        std::function<std::optional<std::tuple<int, BitGraph, int> >()> v_right_args_initializer = [&]() -> std::optional<std::tuple<int, BitGraph, int> > {
            //right branch = take out v nbrs
            G_BITSET v_ingraph2 = bits_in_graph;

            v_ingraph2 = bits_in_graph & (~graphbits[maxdeg_v]);
            G_BITSET v_neighbours = (graphbits[maxdeg_v] & bits_in_graph);
            int v_solution_size2 = v_cursol_size + v_neighbours.count();
            const int v_best_val = m_node_manager.get_score().get<int>();

            if (v_solution_size2 < v_best_val) {
                BitGraph v_bg;
                v_bg.bits_in_graph = v_ingraph2;
                return std::make_tuple(newDepth, v_bg, v_solution_size2);
            }
            return std::nullopt;
        };

        gempba::node v_right = gempba::mp::create_lazy_node<void>(
                *m_load_balancer,
                v_parent, m_function,
                v_right_args_initializer,
                m_args_serializer,
                m_args_deserializer
                );

        m_node_manager.try_remote_submit(v_left, 0);
        m_node_manager.forward(v_right);
    }

private:
    void terminate_condition_bits(int p_solution_size, int p_depth) const {
        if (p_solution_size == 0) {
            return;
        }

        if (p_solution_size < m_node_manager.get_score().get<int>()) {
            spdlog::debug("About to update result: {}", p_solution_size);
            std::function<gempba::task_packet(int &)> v_serializer = make_single_serializer<int>();
            m_node_manager.try_update_result(p_solution_size, gempba::score::make(p_solution_size), v_serializer);

            const auto v_clock = std::chrono::system_clock::now();
            const std::time_t v_time = std::chrono::system_clock::to_time_t(v_clock); //it includes a "\n"

            spdlog::debug("rank {}, MVC solution so far: {} @ depth : {}, {}", m_node_manager.rank_me(), p_solution_size, p_depth, std::ctime(&v_time));

        }
    }
};


#endif // MP_BITVECTOR_BASIC_ENCODING_CENTRAL_HPP
