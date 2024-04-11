#ifndef VOIDINTERMEDIATE_HPP
#define VOIDINTERMEDIATE_HPP

/*
 * Created by Andres Pastrana on 2019
 * pasr1602@usherbrooke.ca
 * rapastranac@gmail.com
 */

#include "ResultHolderBase.hpp"

namespace gempba {
    template<typename Ret, typename... Args>
    class ResultHolderInt<Ret, typename std::enable_if<std::is_void<Ret>::value>::type, Args...> : virtual public ResultHolderBase<Args...> {
        friend class DLB_Handler;

    public:
        explicit ResultHolderInt(DLB_Handler &dlb) : ResultHolderBase<Args...>(dlb) {}
    };
}
#endif