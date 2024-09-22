#ifndef GEMPBA_RESULTAPI_HPP
#define GEMPBA_RESULTAPI_HPP

#include <any>
#include <future>
#include <list>
#include <string>
#include <utility>

#include "trace_node.hpp"
#include "utils/utils.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */
namespace gempba {

    /**
     * @brief This abstract class is used to represent a trace node that has arguments and a result.
     * @tparam Args the original arguments of the function
     */
    template<typename... Args>
    class AbstractTraceNode : public TraceNode {

    public:
        AbstractTraceNode() = default;

        ~AbstractTraceNode() override = default;

        virtual void setArguments(Args &...args) final {
            setArguments(std::make_tuple(std::forward<Args &&>(args)...));
        }

        virtual void setArguments(std::tuple<Args &&...> args) = 0;

        virtual std::tuple<Args...> &getArgumentsRef() = 0;

        virtual std::tuple<Args...> getArgumentsCopy() = 0;

    };

}

#endif //GEMPBA_RESULTAPI_HPP
