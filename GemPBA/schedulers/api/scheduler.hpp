#ifndef GEMPBA_SCHEDULER_HPP
#define GEMPBA_SCHEDULER_HPP

#include "schedulers/api/scheduler.hpp"
#include "function_trace/api/trace_node.hpp"
#include "BranchHandler/BranchHandler.hpp"
#include <spdlog/spdlog.h>
#include <functional>

/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */
namespace gempba {

    enum InterprocessProvider {
        MPI, UPC
    };

    enum Topology {
        SEMI_CENTRALIZED, CENTRALIZED
    };

    //sanity assignment
    enum Tags {
        CENTER_NODE = 0,
        RUNNING_STATE = 1,
        ASSIGNED_STATE = 2,
        AVAILABLE_STATE = 3,
        TERMINATION = 4,
        REFERENCE_VAL_UPDATE = 5,
        NEXT_PROCESS = 6,
        HAS_RESULT = 7,
        NO_RESULT = 8,
        METADATA = 9,
        NON_VOID_RESULT = 10,
        FUNCTION_ARGS = 11,
        FUNCTION_ID = 12
    };

    class BranchHandler;

    class Scheduler {
        // Private constructor for singleton
    protected:
        Scheduler() = default;

    public:
        // Singleton instance creation
        static Scheduler &getInstance();

        // Virtual destructor for polymorphic behaviour
        virtual ~Scheduler() {
            spdlog::info("Scheduler (parent) destroyed");
        };


        /**
         * Runs the scheduler for the center node
         *
         * @param seed serialized data to be broadcast to the first available worker node
         * @param count size of the serialized data
         */
        virtual void runCenter(const char *seed, int count) = 0;

        /**
         * Runs the scheduler for the worker node
         *
         * @param branchHandler (this could be the NodeManager?)
         */
        virtual void runNode(BranchHandler &branchHandler) = 0;

        /**
         * The purpose of this method is to synchronize the statistics with the center node so statistics can be easily retrieved.
         * This method should be called by all nodes after the end of the computation.
         */
        virtual void synchronize_statistics() = 0;

        virtual void setLookupStrategy(LookupStrategy strategy) = 0;

        [[nodiscard]] virtual int rank_me() const = 0;

        [[nodiscard]] virtual int world_size() const = 0;

        virtual void barrier() = 0;

        [[nodiscard]] virtual size_t get_received_task_count(int rank) const = 0;

        [[nodiscard]] virtual size_t get_sent_task_count(int rank) const = 0;

        [[nodiscard]] virtual double get_idle_time(int rank) const = 0;

        [[nodiscard]] virtual double get_elapsed_time() const = 0;

        [[nodiscard]] virtual size_t get_total_requested_tasks() const = 0;

    public:

        /**
         * Pushes a message to the next assigned process. This method is not thread-safe. Sending channel must be opened before calling this
         * and closed after calling it. Use openSendingChannel() and closeSendingChannel() to manage the sending channel.
         * Use judiciously. So far, this is only called within the BranchHandler. It is public just to avoid making the BranchHandler a friend.
         *
         * @brief function_id is used to identify the function that is being executed, so the message can be routed to the correct function.
         *
         * @param message The serialized message to be sent.
         * @param function_id The function id of the message
         */
        virtual void push(std::string &&message, int function_id) = 0;

        virtual bool try_open_transmission_channel() = 0;

        virtual void close_transmission_channel() = 0;

    };

}
#endif //GEMPBA_SCHEDULER_HPP
