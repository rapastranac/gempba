#include <iostream>
#include "../GemPBA/BranchHandler/BranchHandler.hpp"

int main(int argc, char *argv[]) {
    auto &handler = gempba::BranchHandler::getInstance();
    std::cout << "Hello World!" << std::endl;
    return 0;
}