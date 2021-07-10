#ifdef VC_NON_VOID

#include "../include/main.h"
#include "../include/Graph.hpp"

#include "../include/VC_non_void.hpp"

#include "../include/ResultHolder.hpp"
#include "../include/BranchHandler.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <istream>
#include <sstream>
#include <iterator>
#include <string>
#include <vector>

int main_non_void(int numThreads,int prob, std::string filename)
{
	auto &handler = GemPBA::BranchHandler::getInstance(); // parallel GemPBA

	Graph graph;
	Graph oGraph;
	VC_non_void cover;

	graph.readEdges(filename);

	cover.init(graph, numThreads, filename, prob);
	cover.findCover(1);
	cover.printSolution();

	return 0;
}

#endif