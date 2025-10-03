#ifndef GEMPBA_BASIC_ENCODING_UTILS_HPP
#define GEMPBA_BASIC_ENCODING_UTILS_HPP

#include <boost/dynamic_bitset.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/container/flat_map.hpp>
using namespace boost;

#define G_BITSET dynamic_bitset<>
#define G_BITS boost::container::flat_map<int, G_BITSET>


inline G_BITS global_graphbits;


class BitGraph {
public:
    G_BITSET bits_in_graph;
};

namespace boost::serialization {

    template<typename Ar, typename Block, typename Alloc>
    void save(Ar &ar, dynamic_bitset<Block, Alloc> const &bs, unsigned) {
        size_t num_bits = bs.size();
        std::vector<Block> blocks(bs.num_blocks());
        to_block_range(bs, blocks.begin());
        ar & num_bits & blocks;
    }

    template<typename Ar, typename Block, typename Alloc>
    void load(Ar &ar, dynamic_bitset<Block, Alloc> &bs, unsigned) {
        size_t num_bits;
        std::vector<Block> blocks;
        ar & num_bits & blocks;

        bs.resize(num_bits);
        from_block_range(blocks.begin(), blocks.end(), bs);

        bs.resize(num_bits);
    }

    template<typename Ar, typename Block, typename Alloc>
    void serialize(Ar &ar, dynamic_bitset<Block, Alloc> &bs, unsigned version) {
        split_free(ar, bs, version);
    }

}


namespace boost::serialization {

    template<typename Ar>
    void save(Ar &ar, BitGraph const &bg, unsigned) {
        ar << bg.bits_in_graph;

        for (int i = bg.bits_in_graph.find_first(); i != G_BITSET::npos; i = bg.bits_in_graph.find_next(i)) {
            ar << (int32_t) i;
            ar << global_graphbits[i];
        }

    }

    template<typename Ar>
    void load(Ar &ar, BitGraph &bg, unsigned) {
        ar >> bg.bits_in_graph;

        for (int c = 0; c < bg.bits_in_graph.count(); ++c) {
            int32_t v;
            ar >> v;
            G_BITSET bits;
            ar >> bits;
        }
    }

    template<typename Ar>
    void serialize(Ar &ar, BitGraph &bg, unsigned version) {
        split_free(ar, bg, version);
    }

}


void helper_ser(auto &archive, auto &first) {
    archive << first;
}

void helper_ser(auto &archive, auto &first, auto &... args) {
    archive << first;
    helper_ser(archive, args...);
}

auto serializer = [](auto &&... args) {
    /* here inside, user can implement its favourite serialization method given the
	arguments pack and it must return a std::string */
    std::stringstream ss;
    //cereal::BinaryOutputArchive archive(ss);
    //archive(args...);
    boost::archive::text_oarchive archive(ss);
    helper_ser(archive, args...);
    return gempba::task_packet(ss.str());
};

template<typename T>
std::function<gempba::task_packet(T &)> make_single_serializer() {
    return [&](T &p_arg) {
        gempba::task_packet v_ser = serializer(p_arg);
        return v_ser;
    };
}

void helper_dser(auto &archive, auto &first) {
    archive >> first;
}

void helper_dser(auto &archive, auto &first, auto &... args) {
    archive >> first;
    helper_dser(archive, args...);
}

auto deserializer = [](std::stringstream &ss, auto &... args) {
    /* here inside, the user can implement its favourite deserialization method given buffer
	and the arguments pack*/
    //cereal::BinaryInputArchive archive(ss);
    archive::text_iarchive archive(ss);

    helper_dser(archive, args...);
};

#endif //GEMPBA_BASIC_ENCODING_UTILS_HPP
