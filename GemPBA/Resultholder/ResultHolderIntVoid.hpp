#ifndef VOIDINTERMEDIATE_HPP
#define VOIDINTERMEDIATE_HPP

/*
 * Created by Andres Pastrana on 2019
 * pasr1602@usherbrooke.ca
 * rapastranac@gmail.com
 */

#include "result_holder_base.hpp"

namespace gempba {
    template<typename Ret, typename... Args>
    class result_holder_int<Ret, typename std::enable_if<std::is_void<Ret>::value>::type, Args...> : virtual public result_holder_base<Args...> {
        friend class DLB_Handler;

    public:
        explicit result_holder_int(DLB_Handler &dlb) :
            result_holder_base<Args...>(dlb) {
        }

        ~result_holder_int() override = default;
    };
}
#endif
