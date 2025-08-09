#ifndef GEMPBA_MAIN_HPP
#define GEMPBA_MAIN_HPP

#include "argparse/argparse.hpp"
#include "Graph.hpp"
#include "MPI_Modules/scheduler_parent.hpp"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <string>
namespace fs = std::filesystem;


struct Params {
    int job_id;
    int nodes;
    int ntasks_per_node;
    int ntasks_per_socket;
    int cpus_per_task;
    int prob;
    std::string filename;
};

Params parse(int argc, char *argv[]) {

    argparse::ArgumentParser program("main");

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

    program.add_argument("-I", "--indir")
            .help("Input directory of the graph to be read")
            .nargs(1)
            .default_value(std::string{"input/prob_4/400/00400_1"})
            .action([](const std::string &value) { return value; });
    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error &err) {
        std::cout << err.what() << std::endl;
        std::cout << program;
        exit(0);
    };

    int job_id = program.get<int>("--job_id");
    int nodes = program.get<int>("--nodes");
    int ntasks_per_node = program.get<int>("--ntasks_per_node");
    int ntasks_per_socket = program.get<int>("--ntasks_per_socket");
    int cpus_per_task = program.get<int>("--cpus_per_task");
    int prob = program.get<int>("--prob");
    auto filename = program.get<std::string>("--indir");


    spdlog::info("argc: {}, nodes: {}, ntasks_per_node: {}, ntasks_per_socket: {}, cpus_per_task: {}, prob : {}, filename: {} \n",
                 argc, nodes, ntasks_per_node, ntasks_per_socket, cpus_per_task, prob, filename);

    return Params{job_id, nodes, ntasks_per_node, ntasks_per_socket, cpus_per_task, prob, filename};
}

std::string create_directory(std::string root) {
    if (!fs::is_directory(root) || !fs::exists(root)) {
        fs::create_directory(root);
    }
    return root;
}

std::string create_directory(std::string root, std::string folder) {
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

void printToSummaryFile(int job_id, int nodes, int ntasks_per_node, int ntasks_per_socket, int cpus_per_task,
                        const std::string &filename_directory, gempba::scheduler_parent &mpiScheduler, int gsize,
                        int world_size, const std::vector<size_t> &threadRequests, const std::vector<int> &nTasksRecvd,
                        const std::vector<int> &nTasksSent, int solSize, double global_cpu_idle_time,
                        size_t totalThreadRequests) {
    std::string file_name = filename_directory.substr(filename_directory.find_last_of("/\\") + 1);
    const std::string targetDir = create_directory("results", std::to_string(gsize), std::to_string(nodes));

    ofstream myfile;
    myfile.open(targetDir + file_name);
    myfile << "job id:\t" << job_id << std::endl;
    myfile << "nodes:\t" << nodes << std::endl;
    myfile << "ntasks-per-node:\t" << ntasks_per_node << std::endl;
    myfile << "ntasks-per-socket:\t" << ntasks_per_socket << std::endl;
    myfile << "cpus-per-task:\t" << cpus_per_task << std::endl;
    myfile << "graph size:\t\t" << gsize << std::endl;
    myfile << "cover size:\t\t" << solSize << std::endl;
    myfile << "process requests:\t\t" << mpiScheduler.get_total_requests() << std::endl;
    myfile << "thread requests:\t\t" << totalThreadRequests << std::endl;
    myfile << "elapsed time:\t\t" << mpiScheduler.elapsed_time() << std::endl;
    myfile << "cpu idle time (global):\t" << global_cpu_idle_time << std::endl;
    myfile << "wall idle time (global):\t" << global_cpu_idle_time / (world_size - 1) << std::endl;

    myfile << std::endl;

    for (int rank = 1; rank < world_size; rank++) {
        myfile << "tasks sent by rank " << rank << ":\t" << nTasksSent[rank] << std::endl;
    }
    myfile << std::endl;

    for (int rank = 1; rank < world_size; rank++) {
        myfile << "tasks received by rank " << rank << ":\t" << nTasksRecvd[rank] << std::endl;
    }
    myfile << std::endl;

    for (int rank = 1; rank < world_size; rank++) {
        myfile << "rank " << rank << ", thread requests:\t" << threadRequests[rank] << std::endl;
    }
    myfile.close();
}

#endif //GEMPBA_MAIN_HPP
