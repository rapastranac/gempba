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
         * @param branchHandler
         */
        virtual void runNode(BranchHandler &branchHandler) = 0;
    };

}
#endif //GEMPBA_SCHEDULER_HPP
