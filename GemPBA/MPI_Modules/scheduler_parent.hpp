#ifndef GEMPBA_SCHEDULER_HPP
#define GEMPBA_SCHEDULER_HPP

#include <functional>
#include <memory>
#include <mpi.h>
#include <utils/ipc/result.hpp>
#include <utils/ipc/task_packet.hpp>
#include <utils/tree.hpp>

namespace gempba {

    class ResultHolderParent;

    class BranchHandler;

    class scheduler_parent {
    public:
        scheduler_parent() = default;

        virtual ~scheduler_parent() = default;

        [[nodiscard]] virtual int rank_me() const = 0;

        virtual task_packet fetch_solution() = 0;

        virtual std::vector<result> fetch_result_vector() = 0;

        virtual void print_stats() = 0;

        [[nodiscard]] virtual double elapsed_time() const = 0;

        virtual void allgather(void *p_recvbuf, void *p_sendbuf, MPI_Datatype p_mpi_datatype) = 0;

        virtual void gather(void *p_sendbuf, int p_sendcount, MPI_Datatype p_sendtype, void *p_recvbuf, int p_recvcount, MPI_Datatype p_recvtype, int p_root) = 0;

        [[nodiscard]] virtual int get_world_size() const = 0;

        [[nodiscard]] virtual int tasks_recvd() const = 0;

        [[nodiscard]] virtual int tasks_sent() const = 0;

        virtual void barrier() = 0;

        virtual bool open_sending_channel() = 0;

        virtual void close_sending_channel() = 0;

        [[nodiscard]] virtual int next_process() const = 0;

        virtual void set_goal(bool p_maximisation) = 0;

        virtual void push(task_packet &&p_task_packet) = 0;

        virtual void run_node(BranchHandler &p_branch_handler, std::function<std::shared_ptr<ResultHolderParent>(task_packet)> &p_buffer_decoder,
                              std::function<result()> &p_result_fetcher) = 0;

        virtual void run_center(task_packet &p_seed) = 0;

        [[nodiscard]] virtual size_t get_total_requests() const = 0;

        virtual void set_custom_initial_topology(tree &&p_tree) = 0;

    };
}

#endif //GEMPBA_SCHEDULER_HPP
