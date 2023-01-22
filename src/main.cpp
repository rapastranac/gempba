#include "../include/main.h"
#include "fmt/format.h"

#include <argparse.hpp>

#include <iostream>

//#include <mpi.h>
int main(int argc, char *argv[])
{
	argparse::ArgumentParser program("main");
	program.add_argument("-N", "--nThreads")
		.help("Number of threads")
		.nargs(1)
		.default_value(int{1})
		.action([](const std::string &value)
				{ return std::stoi(value); });

	program.add_argument("-P", "--prob")
		.help("Density probability of input graph")
		.nargs(1)
		.default_value(int{4})
		.action([](const std::string &value)
				{ return std::stoi(value); });

	program.add_argument("-I", "--indir")
		.help("Input directory of the graph to be read")
		.nargs(1)
		.default_value(std::string{"input/prob_4/400/00400_1"})
		.action([](const std::string &value)
				{ return value; });
	try
	{
		program.parse_args(argc, argv);
	}
	catch (const std::runtime_error &err)
	{
		std::cout << err.what() << std::endl;
		std::cout << program;
		exit(0);
	};

	int numThreads = program.get<int>("--nThreads");
	int prob = program.get<int>("--prob");
	auto filename = program.get<std::string>("--indir");

	fmt::print("argc: {}, threads: {}, prob : {}, filename: {} \n", argc, numThreads, prob, filename);

#ifdef VC_VOID
	return main_void(numThreads, prob, filename);
#elif VC_VOID_MPI
	return main_void_MPI(numThreads, prob, filename);
#elif VC_NON_VOID
	return main_non_void(numThreads, prob, filename);
#elif VC_NON_VOID_MPI
	return main_non_void_MPI(numThreads, prob, filename);
#elif BITVECTOR_VC
	return main_void_MPI_bitvec(numThreads, prob, filename);
#elif BITVECTOR_VC_THREAD
	return main_void_bitvec(numThreads, prob, filename);
#endif
}
