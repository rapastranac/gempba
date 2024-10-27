#ifndef GEMPBA_MAIN_HPP
#define GEMPBA_MAIN_HPP

#include "argparse/argparse.hpp"
#include "Graph.hpp"

#include <iostream>
#include <spdlog/spdlog.h>
#include <string>

struct Params {
    int job_id;
    int nodes;
    int ntasks_per_node;
    int ntasks_per_socket;
    int cpus_per_task;
    int prob;
    int thread_per_task;
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

    program.add_argument("-nthreads_per_task", "--nthreads_per_task")
            .help("Number of thread per task")
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
    }
    catch (const std::runtime_error &err) {
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
    int threads_per_task = program.get<int>("--nthreads_per_task");
    auto filename = program.get<std::string>("--indir");


    spdlog::info("argc: {}, nodes: {}, ntasks_per_node: {}, ntasks_per_socket: {}, cpus_per_task: {}, prob : {}, filename: {} \n",
                 argc, nodes, ntasks_per_node, ntasks_per_socket, cpus_per_task, prob, filename);

    return Params{job_id, nodes, ntasks_per_node, ntasks_per_socket, cpus_per_task, prob, threads_per_task, filename};
}

#endif //GEMPBA_MAIN_HPP
