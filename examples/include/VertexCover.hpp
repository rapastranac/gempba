#ifndef VERTEXCOVER_HPP
#define VERTEXCOVER_HPP

#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <format>
#include <iomanip>
#include <vector>

#include <gempba/gempba.hpp>
#include "Graph.hpp"
#include "util.hpp"

using namespace std::placeholders;

namespace fs = std::filesystem;

class VertexCover {
public:
    VertexCover() = default;

    virtual ~VertexCover() = default;

    void init(Graph &graph, int numThreads, std::string file, int prob) {
        this->graph = graph;
        input_file_name = file;
        this->numThreads = numThreads;
        std::string graph_size = std::to_string(graph.size());

        std::string dir = graph_size;
        std::string threads_dir = std::to_string(numThreads);
    }

    void setMVCSize(size_t mvcSize) {
        this->currentMVCSize = mvcSize;
    }

    static std::string thread_id_to_string(const std::thread::id p_id) {
        std::ostringstream v_oss;
        v_oss << p_id;
        return v_oss.str();
    }

    void recurrent_msg(std::thread::id p_id) {
        auto clock = std::chrono::system_clock::now();
        std::time_t time = std::chrono::system_clock::to_time_t(clock); //it includes a "\n"
        string col1 = fmt::format("VC = {}", m_node_manager.get_score().to_string());
        string col2 = fmt::format("process {}, thread {}, {}", m_node_manager.rank_me(), thread_id_to_string(p_id), std::ctime(&time));
        cout << std::internal
                << std::setfill('.')
                << col1
                << std::setw(wide - col1.size())
                << col2;
    }

    size_t maximum_matching(Graph g) {
        size_t k = 0;
        int v, w;

        while (!g.isCovered()) {
            v = g.id_max(false);
            w = *g[v].begin();
            k += 2;
            try {
                g.removeVertex(v);
                g.removeVertex(w);
                g.removeZeroVertexDegree();
                //				g.clean_graph(); //stupid thing
            } catch (const std::exception &e) {
                std::cerr << e.what() << std::endl;
            }
        }
        return k;
    }

    auto getGraphRes() {
        return graph_res2;
    }

protected:
    size_t relaxation(const size_t &k1, const size_t &k2) {
        return floor(((1.0 - factor) * (double) k1 + factor * (double) k2));
    }

protected:
    gempba::node_manager &m_node_manager = gempba::get_node_manager();

    Graph graph;
    Graph graph_res;
    Graph graph_res2;
    std::chrono::steady_clock::time_point begin;
    std::chrono::steady_clock::time_point end;
    double elapsed_secs;
    size_t preSize = 0;
    std::mutex mtx;

    std::set<int> cover;
    std::vector<int> visited;

    size_t leaves = 0;
    int currentMVCSize = 0;
    size_t refGlobal = 0;
    size_t foundAtDepth = 0;
    size_t measured_Depth = 0;

    double factor = 0.0; /* k_lower_bound [0.0 - 1.0] k_upper_bound*/
public:
    int wide = 60;

    std::string input_file_name;
    int numThreads = 0;
};

#endif
