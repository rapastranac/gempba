#ifndef GEMPBA_NODEMANAGER_H
#define GEMPBA_NODEMANAGER_H

#include <future>
#include <spdlog/spdlog.h>

/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */
namespace gempba {

    class NodeManager {

        NodeManager() = default;

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


#ifdef MULTIPROCESSING_ENABLED
    private:

    public:

#endif
    };
}


#endif //GEMPBA_NODEMANAGER_H
