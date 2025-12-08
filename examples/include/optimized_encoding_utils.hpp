#ifndef GEMPBA_OPTIMIZED_ENCODING_UTILS_HPP
#define GEMPBA_OPTIMIZED_ENCODING_UTILS_HPP

#include <boost/dynamic_bitset.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/container/flat_map.hpp>

using namespace std::placeholders;

#define G_BITSET boost::dynamic_bitset<>
#define G_BITS boost::container::flat_map<int, G_BITSET>
inline G_BITS global_graph_bits;

namespace boost::serialization {

    template<typename Ar, typename Block, typename Alloc>
    void save(Ar &p_ar, dynamic_bitset<Block, Alloc> const &p_bs, unsigned) {

        size_t v_num_bits = p_bs.size();
        std::vector<Block> v_blocks(p_bs.num_blocks());
        to_block_range(p_bs, v_blocks.begin());
        p_ar & v_num_bits & v_blocks;
    }

    template<typename Ar, typename Block, typename Alloc>
    void load(Ar &p_ar, dynamic_bitset<Block, Alloc> &p_bs, unsigned) {
        size_t v_num_bits;
        std::vector<Block> v_blocks;
        p_ar & v_num_bits & v_blocks;

        p_bs.resize(v_num_bits);
        from_block_range(v_blocks.begin(), v_blocks.end(), p_bs);

        p_bs.resize(v_num_bits);
    }

    template<typename Ar, typename Block, typename Alloc>
    void serialize(Ar &p_ar, dynamic_bitset<Block, Alloc> &p_bs, unsigned p_version) {
        split_free(p_ar, p_bs, p_version);
    }
}

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

inline std::function<std::tuple<int, G_BITSET, int>(const gempba::task_packet &&)> create_deserializer() {
    return [](const gempba::task_packet &&p_buffer) {
        int v_depth;
        G_BITSET v_bits_in_graph;
        int v_solution_size;

        std::stringstream v_ss;
        v_ss.write(reinterpret_cast<const char *>(p_buffer.data()), static_cast<int>(p_buffer.size()));
        deserializer(v_ss, v_depth, v_bits_in_graph, v_solution_size);

        return std::make_tuple(v_depth, v_bits_in_graph, v_solution_size);
    };
};

inline std::function<gempba::task_packet(int, G_BITSET, int)> make_serializer() {
    return [](int p_depth, G_BITSET p_bits_in_graph, int p_solution_size) {
        auto v_ser = serializer(p_depth, p_bits_in_graph, p_solution_size);
        return v_ser;
    };
}

#endif //GEMPBA_OPTIMIZED_ENCODING_UTILS_HPP
