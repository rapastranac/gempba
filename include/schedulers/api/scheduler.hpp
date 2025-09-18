#ifndef GEMPBA_SCHEDULER_HPP
#define GEMPBA_SCHEDULER_HPP

#include <functional>
#include <memory>
#include <mpi.h>
#include <schedulers/api/stats.hpp>
#include <utils/gempba_utils.hpp>
#include <utils/tree.hpp>
#include <utils/ipc/result.hpp>
#include <utils/ipc/score.hpp>
#include <utils/ipc/task_packet.hpp>

namespace gempba {

    class result_holder_parent;

    class branch_handler;

    class scheduler {
    public:
        scheduler() = default;

        virtual ~scheduler() = default;

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

        virtual bool try_open_transmission_channel() = 0;

        virtual void close_transmission_channel() = 0;

        [[nodiscard]] virtual int next_process() const = 0;

        virtual void set_goal(goal p_goal, score_type p_type) = 0;

        virtual void push(task_packet &&p_task_packet) = 0;

        virtual void run_node(branch_handler &p_branch_handler, std::function<std::shared_ptr<result_holder_parent>(task_packet)> &p_buffer_decoder,
                              std::function<result()> &p_result_fetcher) = 0;

        virtual void run_center(task_packet &p_seed) = 0;

        [[nodiscard]] virtual size_t get_total_requests() const = 0;

        virtual void set_custom_initial_topology(tree &&p_tree) = 0;

        /**
         * Get the statistics of the scheduler at the current process as a unique pointer.
         *
         * @return A unique pointer to the stats object.
         */
        [[nodiscard]] virtual std::unique_ptr<stats> get_stats() const = 0;

    };
}

#endif //GEMPBA_SCHEDULER_HPP
