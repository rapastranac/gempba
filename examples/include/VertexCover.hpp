#ifndef VERTEXCOVER_HPP
#define VERTEXCOVER_HPP

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

#include "Graph.hpp"
#include "BranchHandler/branch_handler.hpp"
#include "Resultholder/ResultHolder.hpp"
#include "util.hpp"
#include <format>
#include "DLB/DLB_Handler.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <vector>
#include <chrono>
#include <ctime>

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
        std::string nameout = graph_size + "_out.dat";
        std::string nameout_raw = graph_size + "_out_raw.csv";

        std::string dir = graph_size;
        std::string threads_dir = std::to_string(numThreads);

        this->outPath = "output/";

        if (!fs::is_directory(outPath)) {
            fs::create_directory(outPath);
        }

        this->outPath += "prob_" + std::to_string(prob) + "/";

        if (!fs::is_directory(outPath)) {
            fs::create_directory(outPath);
        }

        this->outPath += dir + "/";

        if (!fs::is_directory(outPath)) {
            fs::create_directory(outPath);
        }

        this->outPath += threads_dir;

        if (!fs::is_directory(outPath)) {
            fs::create_directory(outPath);
        }

        this->outPath = outPath + "/";
        this->outPath_raw = outPath;

        this->outPath = outPath + nameout;
        this->outPath_raw = outPath_raw + nameout_raw;

        this->output.open(outPath, std::ofstream::in | std::ofstream::out | std::ofstream::app);
        this->output_raw.open(outPath_raw, std::ofstream::in | std::ofstream::out | std::ofstream::app);
        if (!output.is_open()) {
            printf("Error, output file not found !");
        }
        output.close();
        output_raw.close();
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

    void printSolution() {
        this->output.open(outPath, std::ofstream::in | std::ofstream::out | std::ofstream::app);
        if (!output.is_open()) {
            printf("Error, output file not found !");
        }

        cout << "!" << fmt::format("{:-^{}}", "Minimum vertex cover", wide - 2) << "!"
                << "\n";
        output << "!" << fmt::format("{:-^{}}", "Minimum vertex cover", wide - 2) << "!"
                << "\n";

        /* create string of lenght wide*/
        auto it = cover.begin();
        string str;
        while (it != cover.end()) {
            str += Util::ToString(*it) + "   ";
            if (static_cast<int>(str.size()) >= wide) {
                std::cout << fmt::format("{:<{}}", str, wide);
                std::cout << "\n";
                output << fmt::format("{:<{}}", str, wide);
                output << "\n";
                str.clear();
            }
            it++;
        }
        cout << fmt::format("{:<{}}", str, wide);
        cout << "\n";
        output << fmt::format("{:<{}}", str, wide);
        output << "\n";
        str.clear();
        cout << fmt::format("{:-^{}}", "", wide) << "\n";
        output << fmt::format("{:-^{}}", "", wide) << "\n";

        cout << "\n";
        output << "\n";

        string col1 = "path";
        string col2 = input_file_name;

        cout << std::internal
                << col1
                << std::setfill(' ')
                << std::setw(wide - col1.size()) << col2
                << "\n";
        output << std::internal
                << col1
                << std::setw(wide - col1.size()) << col2
                << "\n";

        col1 = "Initial graph size after preprocessing: ";
        col2 = Util::ToString((int) preSize);
        cout << std::internal
                << col1
                << std::setfill(' ')
                << std::setw(wide - col1.size()) << col2
                << "\n";
        output << std::internal
                << col1
                << std::setw(wide - col1.size()) << col2
                << "\n";

        col1 = "Size:";
        col2 = Util::ToString((int) cover.size());
        cout << std::internal
                << col1
                << std::setw(wide - col1.size()) << col2
                << "\n";
        output << std::internal
                << col1
                << std::setw(wide - col1.size()) << col2
                << "\n";

        col1 = "Found at depth:";
        col2 = Util::ToString((int) foundAtDepth);
        cout << std::internal
                << col1
                << std::setw(wide - col1.size()) << col2
                << "\n";
        output << std::internal
                << col1
                << std::setw(wide - col1.size()) << col2
                << "\n";

        col1 = "Elapsed time:";
        col2 = Util::ToString((double) (elapsed_secs * 1.0e-9)) + " s";
        //auto tmp = std::setw(wide - col1.size() - col2.size());
        cout << std::internal
                << col1
                << std::setw(wide - col1.size()) << col2
                << "\n";
        output << std::internal
                << col1
                << std::setw(wide - col1.size()) << col2
                << "\n";

        col1 = "Number of leaves:";
        col2 = Util::ToString((int) leaves);
        cout << std::internal
                << col1
                << std::setw(wide - col1.size()) << col2
                << "\n";
        output << std::internal
                << col1
                << std::setw(wide - col1.size()) << col2
                << "\n";

        col1 = "Maximum depth reached:";
        col2 = Util::ToString((int) measured_Depth);
        cout << std::internal
                << col1
                << std::setw(wide - col1.size()) << col2
                << "\n";
        output << std::internal
                << col1
                << std::setw(wide - col1.size()) << col2
                << "\n";

        col1 = "Idle time:";
        col2 = std::to_string(branchHandler.idle_time());
        string col3 = std::to_string((branchHandler.idle_time() * 100.0 / (elapsed_secs * 1.0e-9))) + "%";

        cout << std::left << std::setw(wide * 0.3)
                << col1
                << std::right << std::setw(wide * 0.3)
                << col2
                << std::right << std::setw(wide * 0.4)
                << col3
                << "\n";
        output << std::left << std::setw(wide * 0.3)
                << col1
                << std::right << std::setw(wide * 0.3)
                << col2
                << std::right << std::setw(wide * 0.4)
                << col3
                << "\n";

        col1 = "Pool idle time:";
        col2 = Util::ToString((double) (branchHandler.get_pool_idle_time()));
        col3 = Util::ToString((double) (branchHandler.get_pool_idle_time() * 100.0 / (elapsed_secs * 1.0e-9))) + "%";

        cout << std::left << std::setw(wide * 0.3)
                << col1
                << std::right << std::setw(wide * 0.3)
                << col2
                << std::right << std::setw(wide * 0.4)
                << col3
                << "\n";
        output << std::left << std::setw(wide * 0.3)
                << col1
                << std::right << std::setw(wide * 0.3)
                << col2
                << std::right << std::setw(wide * 0.4)
                << col3
                << "\n";

        col1 = "Successful requests:";
        col2 = std::to_string(branchHandler.number_thread_requests());
        cout << std::internal
                << col1
                << std::setfill(' ')
                << std::setw(wide - col1.size()) << col2
                << "\n";
        output << std::internal
                << col1
                << std::setw(wide - col1.size()) << col2
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
                << Util::ToString((double) (branchHandler.idle_time() * 1.0e-9)) << ","
                << Util::ToString((double) (branchHandler.get_pool_idle_time())) << "\n";
        output_raw.close();
    }

    void setMVCSize(size_t mvcSize) {
        this->currentMVCSize = mvcSize;
    }

    void recurrent_msg(int id) {
        auto clock = std::chrono::system_clock::now();
        std::time_t time = std::chrono::system_clock::to_time_t(clock); //it includes a "\n"
        string col1 = fmt::format("VC = {}", branchHandler.reference_value());
        string col2 = fmt::format("process {}, thread {}, {}", branchHandler.rank_me(), id, std::ctime(&time));
        cout << std::internal
                << std::setfill('.')
                << col1
                << std::setw(wide - col1.size())
                << col2;

        outFile(col1, col2);
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
    gempba::DLB_Handler &dlb = gempba::DLB_Handler::getInstance();
    gempba::branch_handler &branchHandler = gempba::branch_handler::get_instance();

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
    ofstream output;
    ofstream output_raw;
    std::string outPath;
    std::string outPath_raw;
    int wide = 60;

    std::string input_file_name;
    int numThreads = 0;
};

#endif
