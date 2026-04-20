#ifndef GEMPBA_TEST_UTILS_HPP
#define GEMPBA_TEST_UTILS_HPP
#include <sstream>
#include <string>


namespace gempba {

    struct test_utils final {
    private:
        test_utils() = default;

    public:
        static std::string strip_boost_metadata(const std::string &p_archive) {
            std::istringstream v_iss(p_archive);
            std::ostringstream v_oss;
            std::string v_token;

            // skip the header tokens (22 serialization::archive <version> 0 0 0 0)
            for (int i = 0; i < 6; ++i)
                v_iss >> v_token;

            // dump the rest
            v_oss << v_iss.rdbuf();
            return v_oss.str();
        }
    };
} // namespace gempba

#endif // GEMPBA_TEST_UTILS_HPP
