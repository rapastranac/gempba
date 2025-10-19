#ifndef MP_BITVECTOR_OPTIMIZED_ENCODING_HPP
#define MP_BITVECTOR_OPTIMIZED_ENCODING_HPP

#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <sstream>
#include <thread>
#include <tuple>
#include <spdlog/spdlog.h>

#include <gempba/gempba.hpp>
#include <gempba/core/node.hpp>
#include <gempba/utils/task_packet.hpp>

#include "Graph.hpp"
#include "optimized_encoding_utils.hpp"

class mp_bitvector_optimized_encoding final {
    std::function<void(std::thread::id, int, G_BITSET, int, gempba::node)> m_function;
    std::function<gempba::task_packet(int, G_BITSET, int)> m_args_serializer;
    std::function<std::tuple<int, G_BITSET, int>(gempba::task_packet)> m_args_deserializer;

public:
    long m_is_skips{};
    long m_deglb_skips{};
    long m_seen_skips{};

    std::unordered_map<int, G_BITSET > m_graph_bits;
    std::atomic<size_t> m_passes;
    std::mutex m_mutex;

    gempba::node_manager &m_node_manager;
    gempba::load_balancer *m_load_balancer;

    explicit mp_bitvector_optimized_encoding(gempba::node_manager &p_node_manager, gempba::load_balancer *p_load_balancer) :
        m_node_manager(p_node_manager), m_load_balancer(p_load_balancer) {

        this->m_function = std::bind(&mp_bitvector_optimized_encoding::mvcbitset, this, _1, _2, _3, _4, _5);
        this->m_args_deserializer = create_deserializer();
        this->m_args_serializer = make_serializer();
    }

    ~mp_bitvector_optimized_encoding() = default;

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

            G_BITSET vnbrs(gsize);

            for (int i: it->second) {
                vnbrs[i] = true;
            }
            m_graph_bits[v] = vnbrs;
        }

        //check for evil degree 0 vertices
        for (int i = 0; i < gsize; ++i) {
            if (!m_graph_bits.contains(i)) {
                m_graph_bits[i] = G_BITSET(gsize);
            }
        }
    }

    static std::string thread_id_to_string(const std::thread::id p_id) {
        std::ostringstream v_oss;
        v_oss << p_id;
        return v_oss.str();
    }

    void mvcbitset(std::thread::id p_tid, int p_depth, G_BITSET p_bits_in_graph, int p_solsize, gempba::node p_parent) {

        ++m_passes;
        int v_cursol_size = p_solsize;

        if (m_passes < 2) {
            spdlog::debug("mvcbitset(...) called: depth: {}, solsize: {}, bits_in_graph: {}", p_depth, p_solsize, p_bits_in_graph.count());
        }

        if (m_passes % static_cast<size_t>(1e6) == 0) {
            const auto clock = std::chrono::system_clock::now();
            const std::time_t v_time = std::chrono::system_clock::to_time_t(clock); //it includes a "\n"

            const auto v_str = fmt::format(
                    "WR= {} ID= {} passes={} gsize={} refvalue={} solsize={} isskips={} deglbskips={} {}",
                    m_node_manager.rank_me(), thread_id_to_string(p_tid), m_passes.load(), p_bits_in_graph.count(),
                    m_node_manager.get_score().get<int>(), v_cursol_size, m_is_skips, m_deglb_skips,
                    std::ctime(&v_time));

            spdlog::info(v_str);
        }

        if (p_bits_in_graph.count() <= 1) {

            terminate_condition_bits(v_cursol_size, p_depth);
            return;
        }

        if (v_cursol_size >= m_node_manager.get_score().get<int>()) {
            spdlog::debug("Return with solsize: {}, score: {}", v_cursol_size, m_node_manager.get_score().to_string());
            return;
        }

        //max degree dude
        int v_maxdeg = 0;
        int v_maxdeg_v = 0;

        int v_nb_edges_double_counted = 0;

        bool v_some_rule_applies = true;

        while (v_some_rule_applies) {
            v_nb_edges_double_counted = 0;
            v_maxdeg = -1;
            v_maxdeg_v = 0;
            v_some_rule_applies = false;

            for (size_t i = p_bits_in_graph.find_first(); i != G_BITSET::npos; i = p_bits_in_graph.find_next(i)) {

                G_BITSET nbrs = (m_graph_bits[i] & p_bits_in_graph);

                int cnt = nbrs.count();
                if (cnt == 0) {
                    p_bits_in_graph[i] = false;
                } else if (cnt == 1) {
                    int the_nbr = nbrs.find_first();
                    //cur_sol[the_nbr] = true;
                    v_cursol_size++;
                    p_bits_in_graph[i] = false;
                    p_bits_in_graph[the_nbr] = false;
                    v_some_rule_applies = true;
                } else if (cnt > v_maxdeg) {
                    v_maxdeg = cnt;
                    v_maxdeg_v = i;
                } else {
                    //looking for a nbr with at least same nbrs as i
                    if (cnt == 2) {
                        int n1 = nbrs.find_first();
                        int n2 = nbrs.find_next(n1);
                        if (m_graph_bits[n1][n2]) {
                            //cur_sol[n1] = true;
                            //cur_sol[n2] = true;
                            p_bits_in_graph[i] = false;
                            p_bits_in_graph[n1] = false;
                            p_bits_in_graph[n2] = false;
                            v_cursol_size += 2;
                            v_some_rule_applies = true;
                        }
                    }
                }
                v_nb_edges_double_counted += cnt;
            }
        }

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

        std::function<std::optional<std::tuple<int, G_BITSET, int> >()> v_left_args_initializer = [&]()-> std::optional<std::tuple<int, G_BITSET, int> > {
            G_BITSET v_ingraph1 = p_bits_in_graph;

            if (!v_ingraph1[v_maxdeg_v]) {
                cout << "ERROR : max deg_v already gone" << endl;
            }
            v_ingraph1.set(v_maxdeg_v, false);
            int v_solution_size1 = v_cursol_size + 1;
            int v_best_val = m_node_manager.get_score().get<int>();

            if (v_solution_size1 < v_best_val) {
                return std::make_tuple(v_new_depth, v_ingraph1, v_solution_size1);
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

        std::function<std::optional<std::tuple<int, G_BITSET, int> >()> v_right_args_initializer = [&]() -> std::optional<std::tuple<int, G_BITSET, int> > {
            //right branch = take out v nbrs
            G_BITSET v_ingraph2 = p_bits_in_graph;

            v_ingraph2 = p_bits_in_graph & (~m_graph_bits[v_maxdeg_v]);
            G_BITSET v_neighbours = (m_graph_bits[v_maxdeg_v] & p_bits_in_graph);
            int v_solutions_size2 = v_cursol_size + v_neighbours.count();
            const int v_best_val = m_node_manager.get_score().get<int>();

            if (v_solutions_size2 < v_best_val) {
                return std::make_tuple(v_new_depth, v_ingraph2, v_solutions_size2);
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
    std::ofstream output;
    std::ofstream output_raw;
    std::string outPath;
    std::string outPath_raw;
    int wide = 60;

    std::string input_file_name;

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


    void outFile(std::string col1, std::string col2) {
        //std::unique_lock<std::mutex> lck(mtx);
        this->output.open(outPath, std::ofstream::in | std::ofstream::out | std::ofstream::app);
        if (!output.is_open()) {
            printf("Error, output file not found !");
        }

        output << std::internal
                << col1
                << std::setw(wide - col1.size())
                << col2;

        output.close();
    }

    void print_solution() {
        this->output.open(outPath, std::ofstream::in | std::ofstream::out | std::ofstream::app);
        if (!output.is_open()) {
            printf("Error, output file not found !");
        }

        cout << "!" << fmt::format("{:-^{}}", "Minimum vertex cover", wide - 2) << "!" << "\n";
        output << "!" << fmt::format("{:-^{}}", "Minimum vertex cover", wide - 2) << "!" << "\n";

        /* create string of length wide*/
        auto it = cover.begin();
        string v_str;
        while (it != cover.end()) {
            v_str += Util::ToString(*it) + "   ";
            if (std::cmp_greater_equal(v_str.size(), wide)) {
                std::cout << fmt::format("{:<{}}", v_str, wide);
                std::cout << "\n";
                output << fmt::format("{:<{}}", v_str, wide);
                output << "\n";
                v_str.clear();
            }
            ++it;
        }
        cout << fmt::format("{:<{}}", v_str, wide) << std::endl;
        output << fmt::format("{:<{}}", v_str, wide) << std::endl;

        v_str.clear();
        cout << fmt::format("{:-^{}}", "", wide) << "\n";
        output << fmt::format("{:-^{}}", "", wide) << "\n";

        cout << "\n";
        output << "\n";

        string v_col1 = "path";
        string v_col2 = input_file_name;

        cout << std::internal
                << v_col1
                << std::setfill(' ')
                << std::setw(wide - v_col1.size()) << v_col2
                << "\n";
        output << std::internal
                << v_col1
                << std::setw(wide - v_col1.size()) << v_col2
                << "\n";

        v_col1 = "Initial graph size after preprocessing: ";
        v_col2 = Util::ToString((int) preSize);
        cout << std::internal
                << v_col1
                << std::setfill(' ')
                << std::setw(wide - v_col1.size()) << v_col2
                << "\n";
        output << std::internal
                << v_col1
                << std::setw(wide - v_col1.size()) << v_col2
                << "\n";

        v_col1 = "Size:";
        v_col2 = Util::ToString((int) cover.size());
        cout << std::internal
                << v_col1
                << std::setw(wide - v_col1.size()) << v_col2
                << "\n";
        output << std::internal
                << v_col1
                << std::setw(wide - v_col1.size()) << v_col2
                << "\n";

        v_col1 = "Found at depth:";
        v_col2 = Util::ToString((int) foundAtDepth);
        cout << std::internal
                << v_col1
                << std::setw(wide - v_col1.size()) << v_col2
                << "\n";
        output << std::internal
                << v_col1
                << std::setw(wide - v_col1.size()) << v_col2
                << "\n";

        v_col1 = "Elapsed time:";
        v_col2 = Util::ToString((double) (elapsed_secs * 1.0e-9)) + " s";
        //auto tmp = std::setw(wide - col1.size() - col2.size());
        cout << std::internal
                << v_col1
                << std::setw(wide - v_col1.size()) << v_col2
                << "\n";
        output << std::internal
                << v_col1
                << std::setw(wide - v_col1.size()) << v_col2
                << "\n";

        v_col1 = "Number of leaves:";
        v_col2 = Util::ToString((int) leaves);
        cout << std::internal
                << v_col1
                << std::setw(wide - v_col1.size()) << v_col2
                << "\n";
        output << std::internal
                << v_col1
                << std::setw(wide - v_col1.size()) << v_col2
                << "\n";

        v_col1 = "Maximum depth reached:";
        v_col2 = Util::ToString((int) measured_Depth);
        cout << std::internal
                << v_col1
                << std::setw(wide - v_col1.size()) << v_col2
                << "\n";
        output << std::internal
                << v_col1
                << std::setw(wide - v_col1.size()) << v_col2
                << "\n";

        v_col1 = "Idle time:";
        v_col2 = std::to_string(m_node_manager.get_idle_time());
        string col3 = std::to_string((m_node_manager.get_idle_time() * 100.0 / (elapsed_secs * 1.0e-9))) + "%";

        cout << std::left << std::setw(wide * 0.3)
                << v_col1
                << std::right << std::setw(wide * 0.3)
                << v_col2
                << std::right << std::setw(wide * 0.4)
                << col3
                << "\n";
        output << std::left << std::setw(wide * 0.3)
                << v_col1
                << std::right << std::setw(wide * 0.3)
                << v_col2
                << std::right << std::setw(wide * 0.4)
                << col3
                << "\n";

        v_col1 = "Pool idle time:";
        v_col2 = Util::ToString((double) (m_node_manager.get_idle_time()));
        col3 = Util::ToString((double) (m_node_manager.get_idle_time() * 100.0 / (elapsed_secs * 1.0e-9))) + "%";

        cout << std::left << std::setw(wide * 0.3)
                << v_col1
                << std::right << std::setw(wide * 0.3)
                << v_col2
                << std::right << std::setw(wide * 0.4)
                << col3
                << "\n";
        output << std::left << std::setw(wide * 0.3)
                << v_col1
                << std::right << std::setw(wide * 0.3)
                << v_col2
                << std::right << std::setw(wide * 0.4)
                << col3
                << "\n";

        v_col1 = "Successful requests:";
        v_col2 = std::to_string(m_node_manager.get_thread_request_count());
        cout << std::internal
                << v_col1
                << std::setfill(' ')
                << std::setw(wide - v_col1.size()) << v_col2
                << "\n";
        output << std::internal
                << v_col1
                << std::setw(wide - v_col1.size()) << v_col2
                << "\n";

        std::cout << "!" << fmt::format("{:-^{}}", "", wide - 2) << "!"
                << "\n";
        output << "!" << fmt::format("{:-^{}}", "", wide - 2) << "!"
                << "\n";
        std::cout << "\n"
                << "\n"
                << "\n";
        output << "\n"
                << "\n"
                << "\n";

        output.close();

        this->output_raw.open(outPath_raw, std::ofstream::in | std::ofstream::out | std::ofstream::app);

        output_raw << input_file_name << ","
                << Util::ToString((int) preSize) << ","
                << Util::ToString((int) cover.size()) << ","
                << Util::ToString((int) foundAtDepth) << ","
                << Util::ToString((double) (elapsed_secs * 1.0e-9)) << ","
                << Util::ToString((int) leaves) << ","
                << Util::ToString((int) measured_Depth) << ","
                << Util::ToString(m_node_manager.get_idle_time() * 1.0e-9) << ","
                << Util::ToString(m_node_manager.get_idle_time()) << "\n";
        output_raw.close();
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

        outFile(col1, col2);
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
        const std::string v_graph_size = std::to_string(p_graph.size());
        const std::string v_nameout = v_graph_size + "_out.dat";
        const std::string v_nameout_raw = v_graph_size + "_out_raw.csv";

        const std::string v_dir = v_graph_size;
        const std::string v_threads_dir = std::to_string(p_num_threads);

        this->outPath = "output/";

        if (!std::filesystem::is_directory(outPath)) {
            std::filesystem::create_directory(outPath);
        }

        this->outPath += "prob_" + std::to_string(p_probility) + "/";

        if (!std::filesystem::is_directory(outPath)) {
            std::filesystem::create_directory(outPath);
        }

        this->outPath += v_dir + "/";

        if (!std::filesystem::is_directory(outPath)) {
            std::filesystem::create_directory(outPath);
        }

        this->outPath += v_threads_dir;

        if (!std::filesystem::is_directory(outPath)) {
            std::filesystem::create_directory(outPath);
        }

        this->outPath = outPath + "/";
        this->outPath_raw = outPath;

        this->outPath = outPath + v_nameout;
        this->outPath_raw = outPath_raw + v_nameout_raw;

        this->output.open(outPath, std::ofstream::in | std::ofstream::out | std::ofstream::app);
        this->output_raw.open(outPath_raw, std::ofstream::in | std::ofstream::out | std::ofstream::app);
        if (!output.is_open()) {
            printf("Error, output file not found !");
        }
        output.close();
        output_raw.close();
    }
};

#endif // MP_BITVECTOR_OPTIMIZED_ENCODING_HPP
