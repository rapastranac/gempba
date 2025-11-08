#ifndef MT_BITVECTOR_OPTIMIZED_ENCODING_HPP
#define MT_BITVECTOR_OPTIMIZED_ENCODING_HPP

#include <atomic>
#include <functional>
#include <thread>
#include <tuple>
#include <boost/dynamic_bitset.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/container/flat_map.hpp>
#include <gempba/gempba.hpp>
#include <gempba/core/node.hpp>
#include <spdlog/spdlog.h>

#include "Graph.hpp"

using namespace boost;
using namespace std::placeholders;

#define GBITSET dynamic_bitset<>


class mt_bitvector_optimized_encoding final {
    std::function<void(std::thread::id, int, GBITSET, int, gempba::node)> m_function;

public:
    long m_is_skips{};
    long m_deglb_skips{};
    long m_seen_skips{};

    std::unordered_map<int, GBITSET > m_graphbits;
    std::atomic<size_t> m_passes;
    std::mutex m_mutex;

    gempba::node_manager &m_node_manager;
    gempba::load_balancer *m_load_balancer;

    explicit mt_bitvector_optimized_encoding(gempba::node_manager &p_node_manager, gempba::load_balancer *p_load_balancer) :
        m_node_manager(p_node_manager), m_load_balancer(p_load_balancer) {

        this->m_function = std::bind(&mt_bitvector_optimized_encoding::mvcbitset, this, _1, _2, _3, _4, _5);
    }

    ~mt_bitvector_optimized_encoding() = default;

    void set_graph(Graph &p_graph) {
        m_is_skips = 0;
        m_deglb_skips = 0;
        m_seen_skips = 0;

        m_passes = 0;
        int gsize = p_graph.size() + 1; //+1 cuz some files use node ids from 1 to n (instead of 0 to n - 1)

        cout << "Graph has " << p_graph.size() << " vertices and " << p_graph.getNumEdges() << " edges" << endl;
        vector<pair<int, int> > deg_v;
        for (auto it = p_graph.adj.begin(); it != p_graph.adj.end(); ++it) {
            deg_v.push_back(make_pair(it->second.size(), it->first));
        }

        //for some reason, I decided to sort the vertices by degree.  I don't think it is useful.
        std::sort(deg_v.begin(), deg_v.end());
        map<int, int> remap;
        for (int i = 0; i < static_cast<int>(deg_v.size()); i++) {
            remap[deg_v[i].second] = deg_v.size() - 1 - i;
        }
        map<int, set<int> > adj2;
        for (auto it = p_graph.adj.begin(); it != p_graph.adj.end(); ++it) {
            int v = it->first;
            adj2[remap[v]] = set<int>();
            for (auto w: p_graph.adj[v]) {
                adj2[remap[v]].insert(remap[w]);
            }
        }

        //for (auto it = graph.adj.begin(); it != graph.adj.end(); ++it)
        for (auto it = adj2.begin(); it != adj2.end(); ++it) {
            int v = it->first;

            GBITSET vnbrs(gsize);

            for (int i: it->second) {
                vnbrs[i] = true;
            }
            m_graphbits[v] = vnbrs;
        }

        //check for evil degree 0 vertices
        for (int i = 0; i < gsize; ++i) {
            if (!m_graphbits.contains(i)) {
                m_graphbits[i] = GBITSET(gsize);
            }
        }
    }

    static std::string thread_id_to_string(const std::thread::id p_id) {
        std::ostringstream v_oss;
        v_oss << p_id;
        return v_oss.str();
    }

    void some_rules_applies(GBITSET& p_bits_in_graph, int &p_cursol_size, int &p_maxdeg, int &p_maxdeg_v, int &p_nb_edges_double_counted) {
        bool v_some_rule_applies = true;
        while (v_some_rule_applies) {
            p_nb_edges_double_counted = 0;
            p_maxdeg = -1;
            p_maxdeg_v = 0;
            v_some_rule_applies = false;

            for (size_t i = p_bits_in_graph.find_first(); i != GBITSET::npos; i = p_bits_in_graph.find_next(i)) {

                GBITSET nbrs = (m_graphbits[i] & p_bits_in_graph);

                int cnt = nbrs.count();
                if (cnt == 0) {
                    p_bits_in_graph[i] = false;
                } else if (cnt == 1) {
                    int the_nbr = nbrs.find_first();
                    //cur_sol[the_nbr] = true;
                    p_cursol_size++;
                    p_bits_in_graph[i] = false;
                    p_bits_in_graph[the_nbr] = false;
                    v_some_rule_applies = true;
                } else if (cnt > p_maxdeg) {
                    p_maxdeg = cnt;
                    p_maxdeg_v = i;
                } else {
                    //looking for a nbr with at least same nbrs as i
                    if (cnt == 2) {
                        int n1 = nbrs.find_first();
                        int n2 = nbrs.find_next(n1);
                        if (m_graphbits[n1][n2]) {
                            //cur_sol[n1] = true;
                            //cur_sol[n2] = true;
                            p_bits_in_graph[i] = false;
                            p_bits_in_graph[n1] = false;
                            p_bits_in_graph[n2] = false;
                            p_cursol_size += 2;
                            v_some_rule_applies = true;
                        }
                    }
                }
                p_nb_edges_double_counted += cnt;
            }
        }
    }

    void mvcbitset(std::thread::id p_tid, int p_depth, GBITSET p_bits_in_graph, int p_solsize, gempba::node p_parent) {

        ++m_passes;
        int v_cursol_size = p_solsize;

        if (m_passes % static_cast<size_t>(1e6) == 0) {
            auto clock = std::chrono::system_clock::now();
            std::time_t v_time = std::chrono::system_clock::to_time_t(clock); //it includes a "\n"

            auto v_str = fmt::format(
                    "WR= {} ID= {} passes={} gsize={} refvalue={} solsize={} isskips={} deglbskips={} {}",
                    m_node_manager.rank_me(), thread_id_to_string(p_tid), m_passes.load(), p_bits_in_graph.count(),
                    m_node_manager.get_score().get<int>(), v_cursol_size, m_is_skips, m_deglb_skips,
                    std::ctime(&v_time));

            std::cout << v_str;

        }

        if (p_bits_in_graph.count() <= 1) {

            terminate_condition_bits(v_cursol_size, p_depth);
            return;
        }

        if (v_cursol_size >= m_node_manager.get_score().get<int>()) {
            return;
        }

        //max degree dude
        int v_maxdeg = 0;
        int v_maxdeg_v = 0;
        int v_nb_edges_double_counted = 0;

        some_rules_applies(p_bits_in_graph, v_cursol_size, v_maxdeg, v_maxdeg_v, v_nb_edges_double_counted);

        int v_nb_vertices = p_bits_in_graph.count();
        if (v_nb_vertices <= 1) {
            terminate_condition_bits(v_cursol_size, p_depth);
            return;
        }

        const float v_tmp = static_cast<float>(1 - 8 * v_nb_edges_double_counted / 2 - 4 * v_nb_vertices + 4 * v_nb_vertices * v_nb_vertices);
        int v_indsetub = static_cast<int>(0.5f * (1.0f + sqrt(v_tmp)));
        const int vclb = v_nb_vertices - v_indsetub;

        if (vclb + v_cursol_size >= m_node_manager.get_score().get<int>()) {
            m_is_skips++;
            return;
        }

        int v_deg_lb = 0; //getDegLB(bits_in_graph, nbEdgesDoubleCounted/2);
        v_deg_lb = (v_nb_edges_double_counted / 2) / v_maxdeg;
        if (v_deg_lb + v_cursol_size >= m_node_manager.get_score().get<int>()) {
            m_deglb_skips++;
            return;
        }

        int v_new_depth = p_depth + 1;

        const gempba::node v_parent = p_parent == nullptr ? gempba::create_dummy_node(*m_load_balancer) : p_parent;

        std::function<std::optional<std::tuple<int, GBITSET, int> >()> v_left_args_initializer = [&]()-> std::optional<std::tuple<int, GBITSET, int> > {
            GBITSET v_ingraph1 = p_bits_in_graph;

            if (!v_ingraph1[v_maxdeg_v]) {
                spdlog::error("ERROR : max deg_v already gone");
            }
            v_ingraph1.set(v_maxdeg_v, false);
            int v_solution_size1 = v_cursol_size + 1;
            const int v_best_val = m_node_manager.get_score().get<int>();

            if (v_solution_size1 < v_best_val) {
                return std::make_tuple(v_new_depth, v_ingraph1, v_solution_size1);
            }
            return std::nullopt;
        };

        gempba::node v_left = gempba::mt::create_lazy_node<void>(
                *m_load_balancer,
                v_parent, m_function,
                v_left_args_initializer
                );

        std::function<std::optional<std::tuple<int, GBITSET, int> >()> v_right_args_initializer = [&]()-> std::optional<std::tuple<int, GBITSET, int> > {
            //right branch = take out v nbrs
            GBITSET v_ingraph2 = p_bits_in_graph;

            v_ingraph2 = p_bits_in_graph & (~m_graphbits[v_maxdeg_v]);
            GBITSET v_neighbours = (m_graphbits[v_maxdeg_v] & p_bits_in_graph);
            int v_solutions_size2 = v_cursol_size + v_neighbours.count();
            const int v_best_val = m_node_manager.get_score().get<int>();

            if (v_solutions_size2 < v_best_val) {
                return std::make_tuple(v_new_depth, v_ingraph2, v_solutions_size2);
            }
            return std::nullopt;
        };

        gempba::node v_right = gempba::mt::create_lazy_node<void>(
                *m_load_balancer,
                v_parent, m_function,
                v_right_args_initializer
                );

        m_node_manager.try_local_submit(v_left);
        m_node_manager.forward(v_right);
    }

private:
    Graph m_graph;
    double elapsed_secs{};
    size_t preSize = 0;

    std::set<int> cover;
    std::vector<int> visited;

    size_t leaves = 0;
    int currentMVCSize = 0;
    size_t foundAtDepth = 0;
    size_t measured_Depth = 0;

    double factor = 0.0; /* k_lower_bound [0.0 - 1.0] k_upper_bound*/
public:
    int wide = 60;

    std::string input_file_name;
    int numThreads = 0;

private:
    void terminate_condition_bits(int p_solution_size, int p_depth) const {
        if (p_solution_size == 0)
            return;

        if (p_solution_size < m_node_manager.get_score().get<int>()) {
            m_node_manager.try_update_result(p_solution_size, gempba::score::make(p_solution_size));

            const auto v_clock = std::chrono::system_clock::now();
            const std::time_t v_time = std::chrono::system_clock::to_time_t(v_clock); //it includes a "\n"

            spdlog::debug("rank {}, MVC solution so far: {} @ depth : {}, {}", m_node_manager.rank_me(), p_solution_size, p_depth, std::ctime(&v_time));

        }
    }

    void set_mvc_size(const size_t p_mvc_size) {
        this->currentMVCSize = p_mvc_size;
    }

    void recurrent_msg(int id) {
        auto clock = std::chrono::system_clock::now();
        std::time_t time = std::chrono::system_clock::to_time_t(clock); //it includes a "\n"
        string col1 = fmt::format("VC = {}", m_node_manager.get_score().to_string());
        string col2 = fmt::format("process {}, thread {}, {}", m_node_manager.rank_me(), id, std::ctime(&time));
        cout << std::internal
                << std::setfill('.')
                << col1
                << std::setw(wide - col1.size())
                << col2;

    }

    static size_t maximum_matching(Graph p_graph) {
        size_t k = 0;
        int v, w;

        while (!p_graph.isCovered()) {
            v = p_graph.id_max(false);
            w = *p_graph[v].begin();
            k += 2;
            try {
                p_graph.removeVertex(v);
                p_graph.removeVertex(w);
                p_graph.removeZeroVertexDegree();
                //				g.clean_graph(); //stupid thing
            } catch (const std::exception &e) {
                std::cerr << e.what() << std::endl;
            }
        }
        return k;
    }

    size_t relaxation(const size_t &p_k1, const size_t &p_k2) const {
        return floor(((1.0 - factor) * static_cast<double>(p_k1) + factor * static_cast<double>(p_k2)));
    }

public:
    void init(Graph &p_graph, const int p_num_threads, const std::string &p_file, const int p_probility) {
        this->m_graph = p_graph;
        input_file_name = p_file;
        this->numThreads = p_num_threads;
        const std::string v_graph_size = std::to_string(p_graph.size());

        const std::string v_dir = v_graph_size;
        const std::string v_threads_dir = std::to_string(p_num_threads);
    }
};

#endif // MT_BITVECTOR_OPTIMIZED_ENCODING_HPP
