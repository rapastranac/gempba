#ifndef GEMPBA_EXECUTOR_HPP
#define GEMPBA_EXECUTOR_HPP

#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>

#include "branch_management/node_manager.hpp"
#include "utils/gempba_utils.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-08-11
 */
namespace gempba {
    /**
     * The purpose of this structure is to wrap and serialize function calls with a specific signature,
     * allowing for asynchronous execution and retrieval of results.
     *
     * The expected function signature has the following form:
     *
     * @code
     * ReturnType foo(int id, "Original function parameters"..., void *dummy)
     * @endcode
     *
     * Here's the breakdown:
     * <ul>
     * <li>Serializes parameters into a string</li>
     * <li>Deserializes the string into actual parameters</li>
     * <li>Delegates the function call to a NodeManager</li>
     * <li>Returns a future that will be fetched asynchronously until the result is available</li>
     * <li>Serializes the result value as a string, allowing the client to retrieve it in this form.</li>
     *</ul>
     *
     */
    template<typename Signature>
    class Executor;

    template<typename ReturnType, typename... Args>
    class Executor<ReturnType(Args...)> {

        std::function<ReturnType(Args...)> _original;
        gempba::NodeManager &_node_manager;

    public:
        template<typename F>
        explicit Executor(F &&f, gempba::NodeManager &nodeManager): _original(f), _node_manager(nodeManager) {}

        virtual ~Executor() = default;

        //<editor-fold desc="Operators">
        template<typename T = ReturnType>
        typename std::enable_if<!std::is_void<T>::value, std::shared_future<std::string>>::type operator()(std::string serialized_args) const {
            std::tuple<Args...> args = deserialize(serialized_args);
            auto tail = gempba::tuple_tail(args);

            std::future<T> f = std::apply([this](auto &&...args) {
                return _node_manager.force_push(_original, args...);
            }, tail);

            std::future<std::string> fut = std::async(std::launch::async, [this, share = f.share()]() {
                T actual_result = share.get();
                std::string serialized_result = serialize(static_cast<void *>(&actual_result));
                return serialized_result;
            });

            return fut.share();
        }

        template<typename T = ReturnType>
        typename std::enable_if<std::is_void<T>::value, std::shared_future<void>>::type operator()(std::string serialized_args) const {
            std::tuple<Args...> args = deserialize(serialized_args);
            auto tail = gempba::tuple_tail(args);

            std::future<T> f = std::apply([this](auto &&...args) {
                return _node_manager.force_push(_original, args...);
            }, tail);

            return f.share();
        }
        //</editor-fold>
    protected:
        /**
         * Deserialize a string into a tuple of arguments, the first argument (int id) and the last argument (void * parent), should not be
         * included in the deserialization, however, they should be included in the tuple that will be returned.
         *
         * @param serialized_args serialized string of arguments excluding id and parent
         * @return tuple of arguments including id and parent
         */
        virtual std::tuple<Args...> deserialize(const std::string &serialized_args) const = 0;

        /**
         * Serialize a return value into a string, optional as it is not applicable for void functions
         *
         * @param raw_return_value
         * @return serialized return value
         */
        virtual std::string serialize(void *raw_return_value) const = 0;


    };
}
#endif //GEMPBA_EXECUTOR_HPP
