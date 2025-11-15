#ifndef GEMPBA_BENCHMARK_UTILS_H
#define GEMPBA_BENCHMARK_UTILS_H
#include <gempba/gempba.hpp>

#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/vector.hpp>


void helper_ser(auto &p_archive, auto &p_first) {
    p_archive << p_first;
}

void helper_ser(auto &p_archive, auto &p_first, auto &... p_args) {
    p_archive << p_first;
    helper_ser(p_archive, p_args...);
}

inline auto serializer = [](auto &&... p_args) {
    /* here inside, user can implement its favourite serialization method given the
    arguments pack, and it must return a std::string */
    std::stringstream v_ss;
    boost::archive::text_oarchive v_archive(v_ss);
    helper_ser(v_archive, p_args...);
    return gempba::task_packet(v_ss.str());
};

template<typename T>
std::function<gempba::task_packet(T &)> make_single_serializer() {
    return [&](T &p_arg) {
        auto v_ser = serializer(p_arg);
        return v_ser;
    };
}


void deserializer_helper(auto &p_archive, auto &p_first) {
    p_archive >> p_first;
}

void deserializer_helper(auto &p_archive, auto &p_first, auto &... p_args) {
    p_archive >> p_first;
    deserializer_helper(p_archive, p_args...);
}

inline auto deserializer = [](std::stringstream &p_ss, auto &... p_args) {
    /* here inside, the user can implement its favourite deserialization method given buffer
    and the arguments pack*/
    boost::archive::text_iarchive v_archive(p_ss);

    deserializer_helper(v_archive, p_args...);
};

inline std::function<std::tuple<int, int, std::vector<double> >(const gempba::task_packet &&)> make_deserializer() {
    return [](const gempba::task_packet &p_buffer) {
        int v_depth;
        int v_max_depth;
        std::vector<double> v_dummy;

        std::stringstream v_ss;
        v_ss.write(reinterpret_cast<const char *>(p_buffer.data()), static_cast<int>(p_buffer.size()));
        deserializer(v_ss, v_depth, v_max_depth, v_dummy);

        return std::make_tuple(v_depth, v_max_depth, v_dummy);
    };
};

inline std::function<gempba::task_packet(int, int, std::vector<double>)> make_serializer() {
    return [](int p_depth, int p_max_depth, std::vector<double> p_dummy) {
        auto v_ser = serializer(p_depth, p_max_depth, p_dummy);
        return v_ser;
    };
}

#endif //GEMPBA_BENCHMARK_UTILS_H
