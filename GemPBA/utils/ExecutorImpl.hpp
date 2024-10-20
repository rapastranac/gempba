#ifndef GEMPBA_EXECUTORIMPL_HPP
#define GEMPBA_EXECUTORIMPL_HPP

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>

#include "Executor.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-08-11
 */
namespace gempba {

    /**
     * This is a template class that implements the Executor interface for a specific function signature.
     * This serves as an example of how to implement the Executor interface for different function signatures.
     *
     */
    template<typename Signature>
    class ExecutorImpl;

    template<typename ReturnType, typename ...Args>
    class ExecutorImpl<ReturnType(Args...)> final : public Executor<ReturnType(Args...)> {

    public:
        template<typename F>
        explicit ExecutorImpl(F &&f, gempba::NodeManager &nodeManager):Executor<ReturnType(Args...)>(f, nodeManager) {}

    protected:
        std::tuple<Args...> deserialize(const std::string &serializedParams) const override {
            // To deserialize, simply read the values back from the string
            std::istringstream iss(serializedParams);
            boost::archive::binary_iarchive ia(iss);

            int dummy_id = -1;
            int val1;
            float val2;
            double val3;
            void *dummy_last = nullptr;

            ia >> val1;
            ia >> val2;
            ia >> val3;

            return std::make_tuple(dummy_id, val1, val2, val3, dummy_last);
        }

        std::string serialize(void *raw_ret_value) const override {
            return serialize_helper(raw_ret_value);
        }

        template<typename T = ReturnType>
        typename std::enable_if<!std::is_void<T>::value, std::string>::type
        serialize_helper(void *raw_ret_value) const {
            auto *return_val_ptr = static_cast<T *>(raw_ret_value);
            T &actualValue = *return_val_ptr;

            // Create an archive to serialize the values
            std::ostringstream oss;
            boost::archive::binary_oarchive oa(oss);

            // Serialize each value into the archive
            oa << actualValue;

            // Get the serialized string from the output stream
            std::string serialized_result = oss.str();
            return serialized_result;
        }

        template<typename T = ReturnType>
        typename std::enable_if<std::is_void<T>::value, std::string>::type
        serialize_helper(void *unused) const {
            // Nothing to serialize for void return type
            return {};
        }
    };
}
#endif //GEMPBA_EXECUTORIMPL_HPP
