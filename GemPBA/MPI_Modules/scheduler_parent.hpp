#ifndef GEMPBA_SCHEDULER_HPP
#define GEMPBA_SCHEDULER_HPP

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include "mpi/mpi.h"

namespace gempba {

    class ResultHolderParent;

    class BranchHandler;


    class SchedulerParent {
    public:
        SchedulerParent() = default;

        virtual ~SchedulerParent() = default;

        virtual int rank_me() const = 0;

        virtual std::string fetchSolution() = 0;

        virtual std::vector<std::pair<int, std::string>> fetchResVec() = 0;

        virtual void printStats() = 0;

        virtual double elapsedTime() const = 0;

        virtual void allgather(void *recvbuf, void *sendbuf, MPI_Datatype mpi_datatype) = 0;

        virtual void gather(void *sendbuf, int sendcount, MPI_Datatype sendtype, void *recvbuf, int recvcount, MPI_Datatype recvtype, int root) = 0;

        virtual int getWorldSize() const = 0;

        virtual int tasksRecvd() const = 0;

        virtual int tasksSent() const = 0;

        virtual void barrier() = 0;

        virtual bool openSendingChannel() = 0;

        virtual void closeSendingChannel() = 0;

        virtual int nextProcess() const = 0;

        virtual void setRefValStrategyLookup(bool maximisation) = 0;

        virtual void push(std::string &&message) = 0;

        virtual void runNode(BranchHandler &handler, std::function<std::shared_ptr<ResultHolderParent>(char *, int)> &bufferDecoder,
                             std::function<std::pair<int, std::string>()> &resultFetcher) = 0;

        virtual void runCenter(const char *SEED, const int SEED_SIZE) = 0;

        virtual size_t getTotalRequests() const = 0;

    };
}

#endif //GEMPBA_SCHEDULER_HPP
