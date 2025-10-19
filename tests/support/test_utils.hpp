#ifndef GEMPBA_TEST_UTILS_HPP
#define GEMPBA_TEST_UTILS_HPP
#include <string>
#include <sstream>


namespace gempba {

    struct test_utils final {
    private:
        test_utils() = default;

    public:
        static std::string strip_boost_metadata(const std::string &archive) {
            std::istringstream iss(archive);
            std::ostringstream oss;
            std::string token;

            // skip the header tokens (22 serialization::archive <version> 0 0 0 0)
            for (int i = 0; i < 6; ++i)
                iss >> token;

            // dump the rest
            oss << iss.rdbuf();
            return oss.str();
        }
    };
}

#endif //GEMPBA_TEST_UTILS_HPP
