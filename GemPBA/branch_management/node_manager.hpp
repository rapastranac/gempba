#ifndef GEMPBA_NODEMANAGER_H
#define GEMPBA_NODEMANAGER_H

#include <future>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>

#include "BranchHandler/ThreadPool.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */
namespace gempba {

    class NodeManager {

        unsigned int processor_count;
        std::unique_ptr<ThreadPool::Pool> thread_pool;


        NodeManager() {
            processor_count = std::thread::hardware_concurrency();
        }

    public:
        static NodeManager &getInstance() {
            static NodeManager instance;
            return instance;
        }

        ~NodeManager() = default;

        NodeManager(const NodeManager &) = delete;

        NodeManager(NodeManager &&) = delete;

        NodeManager &operator=(const NodeManager &) = delete;

        NodeManager &operator=(NodeManager &&) = delete;

    public:

        std::optional<std::shared_future<std::string>> forwardToExecutor(const std::string &serialized_args, int function_id) {
            spdlog::throw_spdlog_ex("Not yet implemented");
        }

        [[nodiscard]] std::optional<std::pair<int, std::string>> getSerializedBestSolution() const {
            spdlog::throw_spdlog_ex("Not yet implemented");
        }

        double idle_time() {
            spdlog::throw_spdlog_ex("Not yet implemented");
        }

        bool isDone() {
            spdlog::throw_spdlog_ex("Not yet implemented");
        }

        int refValue() const {
            spdlog::throw_spdlog_ex("Not yet implemented");
        }

        void setRefValue(int refValue) {
            spdlog::throw_spdlog_ex("Not yet implemented");
        }

        bool updateRefValue(int new_refValue, int *mostUpToDate = nullptr) {
            spdlog::throw_spdlog_ex("Not yet implemented");
        }

        void initThreadPool(int poolSize) {
            this->processor_count = poolSize;
            thread_pool = std::make_unique<ThreadPool::Pool>(poolSize);
        }

        template<typename F, typename ...Args>
        auto force_push(F &&f, Args...args) -> std::future<decltype(f(0, args...))> {
            return thread_pool->push(f, args...);
        }


#ifdef MULTIPROCESSING_ENABLED
    private:

    public:

#endif
    };
}


#endif //GEMPBA_NODEMANAGER_H