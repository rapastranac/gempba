#ifndef MAIN_H
#define MAIN_H

#include <string>
#include <vector>

std::vector<std::string> read_graphs(std::string graphSize);

#ifdef VC_VOID
int main_void(int numThreads, int prob, std::string &filename);
#elif VC_VOID_MPI
int main_void_MPI(int numThreads, int prob, std::string filename);
#elif VC_NON_VOID
int main_non_void(int numThreads, int prob, std::string &&filename);
#elif VC_NON_VOID_MPI
int main_non_void_MPI(int numThreads, int prob, std::string &filename);
#elif BITVECTOR_VC
int main_void_MPI_bitvec(int numThreads, int prob, std::string &filename);
#elif BITVECTOR_VC_THREAD
int main_void_bitvec(int numThreads, int prob, std::string &filename);
#endif

#endif
