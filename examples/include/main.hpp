#ifndef GEMPBA_MAIN_HPP
#define GEMPBA_MAIN_HPP

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;


struct Params {
    std::string job_name;
    int job_id;
    int nodes;
    int ntasks_per_node;
    int ntasks_per_socket;
    int cpus_per_task;
    int prob;
    bool csv_append;
    std::string filename;
};

inline Params parse(int argc, char *argv[]) {

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
    auto filename = program.get<std::string>("--indir");
    bool csv_append = program.get<bool>("--csv_append");


    spdlog::info("argc: {}, nodes: {}, ntasks_per_node: {}, ntasks_per_socket: {}, cpus_per_task: {}, prob : {}, csv_append :{}, filename: {} \n",
                 argc, nodes, ntasks_per_node, ntasks_per_socket, cpus_per_task, prob, csv_append, filename);

    return Params{job_name, job_id, nodes, ntasks_per_node, ntasks_per_socket, cpus_per_task, prob, csv_append, filename};
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

inline void append_to_summary_csv(const std::string& p_job_name, const int p_job_id, const int p_nodes,
                                  const int p_ntasks_per_node, const int p_ntasks_per_socket,
                                  const int p_cpus_per_task, const std::string &p_file_name, const double p_elapsed_time,
                                  const int p_gsize, const int p_sol_size,
                                  const double p_global_cpu_idle_time, const size_t p_global_thread_request,
                                  const size_t p_total_requests_at_center, const int p_world_size) {
    std::string v_directory = create_directory("results");
    const std::string v_csv_path = v_directory + "/" + std::to_string(p_gsize) + "_summary.csv";

    // Check if file exists
    const bool v_exists = std::filesystem::exists(v_csv_path);

    std::ofstream v_out(v_csv_path, std::ios::app);
    if (!v_out.is_open()) {
        throw std::runtime_error("Unable to open CSV summary file: " + v_csv_path);
    }

    // If new file, write header
    if (!v_exists) {
        v_out << "job_name,job_id,file_name,world_size,nodes,ntasks_per_node,ntasks_per_socket,cpus_per_task,"
                << "graph_size,cover_size,process_requests_rank0,thread_requests,"
                << "elapsed_time,cpu_idle_time_global,wall_idle_time_global\n";
    }

    const double v_wall_idle_time = p_global_cpu_idle_time / (p_world_size == -1 ? 1 : p_world_size - 1);

    std::stringstream v_ss;
    v_ss << p_job_name << ","
            << p_job_id << ","
            << p_file_name << ","
            << p_world_size << ","
            << p_nodes << ","
            << p_ntasks_per_node << ","
            << p_ntasks_per_socket << ","
            << p_cpus_per_task << ","
            << p_gsize << ","
            << p_sol_size << ","
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


inline void print_to_summary_file(const std::string &p_job_name, const int p_job_id, const int p_nodes, const int p_ntasks_per_node, const int p_ntasks_per_socket,
                                  const int p_cpus_per_task, const std::string &p_filename_directory, const double p_elapsed_time,
                                  const int p_gsize, const int p_world_size, const std::vector<size_t> &p_total_thread_requests,
                                  const std::vector<size_t> &p_received_tasks, const std::vector<size_t> &p_sent_tasks, const int p_sol_size,
                                  const double p_global_cpu_idle_time, const size_t p_global_thread_request,
                                  const size_t p_total_requests_at_center, const bool p_append_csv) {
    const std::string v_file_name = p_filename_directory.substr(p_filename_directory.find_last_of("/\\") + 1);
    const std::string v_target_dir = create_directory("results", std::to_string(p_gsize), std::to_string(p_nodes));

    std::ofstream v_target_file;
    v_target_file.open(v_target_dir + v_file_name);
    v_target_file << "file name:\t" << v_file_name << std::endl;
    v_target_file << "job name:\t" << p_job_name << std::endl;
    v_target_file << "job id:\t" << p_job_id << std::endl;
    v_target_file << "nodes:\t" << p_nodes << std::endl;
    v_target_file << "ntasks-per-node:\t" << p_ntasks_per_node << std::endl;
    v_target_file << "ntasks-per-socket:\t" << p_ntasks_per_socket << std::endl;
    v_target_file << "cpus-per-task:\t" << p_cpus_per_task << std::endl;
    v_target_file << "graph size:\t\t" << p_gsize << std::endl;
    v_target_file << "cover size:\t\t" << p_sol_size << std::endl;
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

    if (!p_append_csv) {
        return;
    }

    // --- NEW: append condensed summary to CSV ---
    append_to_summary_csv(
            p_job_name,
            p_job_id,
            p_nodes,
            p_ntasks_per_node,
            p_ntasks_per_socket,
            p_cpus_per_task,
            v_file_name,
            p_elapsed_time,
            p_gsize,
            p_sol_size,
            p_global_cpu_idle_time,
            p_global_thread_request,
            p_total_requests_at_center,
            p_world_size
            );
}

#endif //GEMPBA_MAIN_HPP
