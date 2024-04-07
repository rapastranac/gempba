#include "../include/main.h"

#include <fstream>
#include <filesystem>
#include <string>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

std::vector<std::string> read_graphs(std::string graphSize) {
    std::string path = "Input";
    std::vector<std::string> files;

    if (!fs::is_directory(path)) {
        throw "Input directory not found!";
    }

    std::string graph_dir = path + "/" + graphSize;

    if (!fs::is_directory(graph_dir)) {
        throw "graph data-base not found!";
    }

    for (auto &p: fs::directory_iterator(graph_dir)) {
        std::string file_path = p.path().string();
        files.push_back(file_path);
    }

    return files;
}