#ifndef GEMPBA_GEMPBA_UTILS_H
#define GEMPBA_GEMPBA_UTILS_H

namespace gempba {

    enum LookupStrategy {
        MAXIMISE, MINIMISE
    };


    template<typename T>
    static bool is_future_ready(const std::shared_future<T> &future_to_check) {
        if (!future_to_check.valid()) {
            return false;
        }
        return future_to_check.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    }

    // Helper function to extract the tail of a tuple
    template<typename Tuple, std::size_t... Idx>
    static auto tuple_tail_impl(const Tuple &tuple, std::index_sequence<Idx...>) {
        // Create a new tuple excluding the first element
        return std::make_tuple(std::get<Idx + 1>(tuple)...);
    }

    // Main function to extract the tail of a tuple
    template<typename... Args>
    static auto tuple_tail(const std::tuple<Args...> &tuple) {
        // Generate an index sequence for the tuple size minus one
        return tuple_tail_impl(tuple, std::make_index_sequence<sizeof...(Args) - 1>());
    }
}

#endif //GEMPBA_GEMPBA_UTILS_H
