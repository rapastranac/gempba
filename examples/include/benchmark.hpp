#ifndef GEMPBA_BENCHMARK_HPP
#define GEMPBA_BENCHMARK_HPP

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

struct benchmark_params {
    std::string job_name;
    int job_id;
    int nodes;
    int ntasks_per_node;
    int ntasks_per_socket;
    int cpus_per_task;
    int prob;
    int max_depth;
    bool csv_append;
    std::string dir_name;
};

inline benchmark_params parse(int argc, char *argv[]) {

    argparse::ArgumentParser program("main");

    program.add_argument("-job_name", "--job_name")
            .help("Job name")
            .nargs(1)
            .default_value(std::string{"default_job"})
            .action([](const std::string &value) { return value; });

    program.add_argument("-job_id", "--job_id")
            .help("Job ID")
            .nargs(1)
            .default_value(int{-1})
            .action([](const std::string &value) { return std::stoi(value); });

    program.add_argument("-nodes", "--nodes")
            .help("Number of nodes")
            .nargs(1)
            .default_value(int{-1})
            .action([](const std::string &value) { return std::stoi(value); });

    program.add_argument("-ntasks_per_node", "--ntasks_per_node")
            .help("Number of tasks per node")
            .nargs(1)
            .default_value(int{-1})
            .action([](const std::string &value) { return std::stoi(value); });

    program.add_argument("-ntasks_per_socket", "--ntasks_per_socket")
            .help("Number of tasks per socket")
            .nargs(1)
            .default_value(int{-1})
            .action([](const std::string &value) { return std::stoi(value); });

    program.add_argument("-cpus_per_task", "--cpus_per_task")
            .help("Number of cpus per task")
            .nargs(1)
            .default_value(int{-1})
            .action([](const std::string &value) { return std::stoi(value); });


    program.add_argument("-P", "--prob")
            .help("Density probability of input graph")
            .nargs(1)
            .default_value(int{4})
            .action([](const std::string &value) { return std::stoi(value); });

    program.add_argument("-max_depth", "--max_depth")
            .help("Maximum depth of the search tree")
            .nargs(1)
            .default_value(int{1})
            .action([](const std::string &value) { return std::stoi(value); });

    program.add_argument("-csv", "--csv_append")
            .help("Append to existing CSV summary file")
            .nargs(1)
            .default_value(false)
            .action([](const std::string &value) { return value == "true" || value == "1"; });

    program.add_argument("-I", "--indir")
            .help("Input directory of the graph to be read")
            .nargs(1)
            .default_value(std::string{"data/prob_4/400/00400_1"})
            .action([](const std::string &value) { return value; });
    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error &err) {
        std::cout << err.what() << std::endl;
        std::cout << program;
        exit(0);
    };

    std::string job_name = program.get<std::string>("--job_name");
    int job_id = program.get<int>("--job_id");
    int nodes = program.get<int>("--nodes");
    int ntasks_per_node = program.get<int>("--ntasks_per_node");
    int ntasks_per_socket = program.get<int>("--ntasks_per_socket");
    int cpus_per_task = program.get<int>("--cpus_per_task");
    int prob = program.get<int>("--prob");
    int max_depth = program.get<int>("--max_depth");
    auto filename = program.get<std::string>("--indir");
    bool csv_append = program.get<bool>("--csv_append");


    spdlog::info("argc: {}, nodes: {}, ntasks_per_node: {}, ntasks_per_socket: {}, cpus_per_task: {}, prob : {}, csv_append :{}, filename: {} \n",
                 argc, nodes, ntasks_per_node, ntasks_per_socket, cpus_per_task, prob, csv_append, filename);

    return benchmark_params{job_name, job_id, nodes, ntasks_per_node, ntasks_per_socket, cpus_per_task, prob, max_depth, csv_append, filename};
}

inline std::string create_directory(std::string root) {
    if (!fs::is_directory(root) || !fs::exists(root)) {
        fs::create_directory(root);
    }
    return root;
}

inline std::string create_directory(std::string root, std::string folder) {
    if (!fs::is_directory(root) || !fs::exists(root)) {
        fs::create_directory(root);
    }
    return create_directory(root + "/" + folder + "/");
}

template<typename... T>
std::string create_directory(std::string root, std::string folder, T... dir) {
    if (!fs::is_directory(root) || !fs::exists(root)) {
        fs::create_directory(root);
    }
    return create_directory(root + "/" + folder, dir...);
}

inline void append_to_summary_csv(const benchmark_params &p_params,
                                  const double p_elapsed_time,
                                  const double p_global_cpu_idle_time,
                                  const size_t p_global_thread_request,
                                  const size_t p_total_requests_at_center,
                                  const int p_world_size) {
    std::string v_directory = create_directory("results");
    const std::string v_csv_path = v_directory + "/" + std::to_string(p_params.max_depth) + "_summary.csv";

    // Check if file exists
    const bool v_exists = std::filesystem::exists(v_csv_path);

    std::ofstream v_out(v_csv_path, std::ios::app);
    if (!v_out.is_open()) {
        throw std::runtime_error("Unable to open CSV summary file: " + v_csv_path);
    }

    // If new file, write header
    if (!v_exists) {
        constexpr std::array<std::string_view, 15> v_columns = {
                "job_name",
                "job_id",
                "file_name",
                "max_depth",
                "world_size",
                "nodes",
                "ntasks_per_node",
                "ntasks_per_socket",
                "cpus_per_task",
                "process_requests_rank0",
                "thread_requests",
                "elapsed_time",
                "cpu_idle_time_global",
                "wall_idle_time_global"
        };
        std::stringstream v_ss;

        for (int i = 0; i < v_columns.size(); ++i) {
            auto v_col = v_columns.at(i);
            if (i == v_columns.size() - 1) {
                v_ss << v_col << "\n";
                break;
            }
            v_ss << v_col << ",";
        }
        v_out << v_ss.str();
    }

    const double v_wall_idle_time = p_global_cpu_idle_time / (p_world_size == -1 ? 1 : p_world_size - 1);

    std::stringstream v_ss;
    v_ss << p_params.job_name << ","
            << p_params.job_id << ","
            << p_params.dir_name << ","
            << p_params.max_depth << ","
            << p_world_size << ","
            << p_params.nodes << ","
            << p_params.ntasks_per_node << ","
            << p_params.ntasks_per_socket << ","
            << p_params.cpus_per_task << ","
            << p_total_requests_at_center << ","
            << p_global_thread_request << ","
            << p_elapsed_time << ","
            << p_global_cpu_idle_time << ","
            << v_wall_idle_time
            << "\n";

    // Write a single CSV row
    v_out << v_ss.str();
    v_out.close();
}

inline std::pair<std::string, std::ofstream> create_or_open_file(const int p_job_id, const int p_nodes, const std::string &p_dir_name, const int p_depth) {
    std::string v_target_dir;
    if (p_nodes == -1) {
        v_target_dir = create_directory("results", "depth-" + std::to_string(p_depth));
    } else {
        v_target_dir = create_directory("results", "depth-" + std::to_string(p_depth), std::to_string(p_nodes));
    }

    // Extract stem and extension
    fs::path v_path(p_dir_name);
    std::string v_stem = v_path.stem().string();
    std::string v_extension = v_path.extension().string();

    std::string v_file_name;
    if (p_job_id == -1) {
        v_file_name = v_stem + v_extension;
    } else {
        v_file_name = v_stem + "_" + std::to_string(p_job_id) + v_extension;
    }

    std::ofstream v_ofstream;
    v_ofstream.open(v_target_dir + v_file_name);

    return std::make_pair(v_file_name, std::move(v_ofstream));
}

inline void print_to_summary_file(const benchmark_params &p_params,
                                  const int p_world_size,
                                  const double p_elapsed_time,
                                  const double p_global_cpu_idle_time,
                                  const size_t p_global_thread_request,
                                  const size_t p_total_requests_at_center,
                                  const std::vector<std::size_t> &p_received_tasks,
                                  const std::vector<std::size_t> &p_sent_tasks,
                                  const std::vector<std::size_t> &p_total_thread_requests
        ) {

    auto [v_dir_name,v_target_file] = create_or_open_file(p_params.job_id, p_params.nodes, p_params.dir_name, p_params.max_depth);

    v_target_file << "directory name:\t" << v_dir_name << std::endl;
    v_target_file << "job name:\t" << p_params.job_name << std::endl;
    v_target_file << "job id:\t" << p_params.job_id << std::endl;
    v_target_file << "max depth:\t\t" << p_params.max_depth << std::endl;
    v_target_file << "nodes:\t" << p_params.nodes << std::endl;
    v_target_file << "ntasks-per-node:\t" << p_params.ntasks_per_node << std::endl;
    v_target_file << "ntasks-per-socket:\t" << p_params.ntasks_per_socket << std::endl;
    v_target_file << "cpus-per-task:\t" << p_params.cpus_per_task << std::endl;
    v_target_file << "process requests (rank 0):\t\t" << p_total_requests_at_center << std::endl;
    v_target_file << "thread requests:\t\t" << p_global_thread_request << std::endl;
    v_target_file << "elapsed time:\t\t" << p_elapsed_time << std::endl;
    v_target_file << "cpu idle time (global):\t" << p_global_cpu_idle_time << std::endl;
    v_target_file << "wall idle time (global):\t" << p_global_cpu_idle_time / (p_world_size == -1 ? 1 : p_world_size - 1) << std::endl;

    v_target_file << std::endl;

    for (int v_rank = 1; v_rank < p_world_size; v_rank++) {
        v_target_file << "tasks received by rank " << v_rank << ":\t" << p_received_tasks[v_rank] << std::endl;
    }
    v_target_file << std::endl;

    for (int v_rank = 1; v_rank < p_world_size; v_rank++) {
        v_target_file << "tasks sent by rank " << v_rank << ":\t" << p_sent_tasks[v_rank] << std::endl;
    }
    v_target_file << std::endl;

    for (int v_rank = 1; v_rank < p_world_size; v_rank++) {
        v_target_file << "rank " << v_rank << ", thread requests:\t" << p_total_thread_requests[v_rank] << std::endl;
    }
    v_target_file.close();

    if (!p_params.csv_append) {
        return;
    }

    // --- NEW: append condensed summary to CSV ---
    append_to_summary_csv(
            p_params,
            p_elapsed_time,
            p_global_cpu_idle_time,
            p_global_thread_request,
            p_total_requests_at_center,
            p_world_size
            );
}

#endif //GEMPBA_BENCHMARK_HPP
