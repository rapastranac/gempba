#ifndef GEMPBA_GEMPBA_UTILS_H
#define GEMPBA_GEMPBA_UTILS_H

namespace gempba {

    enum LookupStrategy {
        MAXIMISE, MINIMISE
    };
    enum LoadBalancingStrategy {
        QUASI_HORIZONTAL, // Our Novel Dynamic Load Balancer
        WORK_STEALING
    };

    enum FunctionType {
        VOID, NON_VOID
    };

    using RawArgs = void **;
    using RawVoidFunc = void (*)(RawArgs);
    using RawNonVoidFunc = void *(*)(RawArgs);

    /**
     * Implementation of the forwarder function
     */
    static void raw_forwarder(RawVoidFunc f, RawArgs args) {
        f(args);
    }

    template<typename RType, typename F, std::size_t... Is, typename... Args, std::enable_if_t<std::is_void_v<RType>, int> = 0>
    static void forwarder_impl(F &f, std::index_sequence<Is...>, Args &... args) {

        // Create an array of void* to hold the addresses of the arguments
        void *arg_array[] = {reinterpret_cast<void *>(&f), reinterpret_cast<void *>(&args)...};

        // Call the raw forwarder with the function and arguments
        RawVoidFunc raw_function = [](void **raw_args) {
            // Retrieve the function pointer
            F &reinterpreted_function = *reinterpret_cast<F *>(raw_args[0]);
            // Create a tuple of argument pointers using the helper functions
            std::tuple arg_tuple = std::make_tuple(*reinterpret_cast<Args *>(raw_args[Is + 1])...);
            // Apply the function to the dereferenced arguments
            std::apply(reinterpreted_function, arg_tuple);
        };
        raw_forwarder(raw_function, arg_array);
    }

    template<typename RType, typename F, typename... Args, std::enable_if_t<std::is_void_v<RType>, int> = 0>
    static void forwarder(F &f, Args &... args) {
        forwarder_impl<void>(f, std::index_sequence_for<Args...>(), args...);
    }


    static void *raw_forwarder(RawNonVoidFunc f, RawArgs args) {
        return f(args);
    }

    template<typename RType, typename F, std::size_t... Is, typename... Args, std::enable_if_t<!std::is_void_v<RType>, int> = 0>
    static RType forwarder_impl(F &f, std::index_sequence<Is...>, Args &... args) {

        // Create an array of void* to hold the addresses of the arguments
        void *arg_array[] = {reinterpret_cast<void *>(&f), reinterpret_cast<void *>(&args)...};

        // Call the raw forwarder with the function and arguments
        RawNonVoidFunc raw_function = [](void **raw_args) {
            // Retrieve the function pointer
            F &reinterpreted_function = *reinterpret_cast<F *>(raw_args[0]);
            // Create a tuple of argument pointers using the helper functions
            std::tuple arg_tuple = std::make_tuple(*reinterpret_cast<Args *>(raw_args[Is + 1])...);
            // Apply the function to the dereferenced arguments
            RType rType = std::apply(reinterpreted_function, arg_tuple);
            return reinterpret_cast<void *>(rType);
        };
        void *raw_result = raw_forwarder(raw_function, arg_array);
        return reinterpret_cast<RType >(raw_result);
    }

    template<typename RType, typename F, typename... Args, std::enable_if_t<!std::is_void_v<RType>, int> = 0>
    static RType forwarder(F &f, Args &... args) {
        return forwarder_impl<RType>(f, std::index_sequence_for<Args...>(), args...);
    }

    // other utils
    template<typename T>
    static bool is_future_ready(const std::shared_future<T> &future_to_check) {
        if (!future_to_check.valid()) {
            return false;
        }

        return future_to_check.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    }
}

#endif //GEMPBA_GEMPBA_UTILS_H
