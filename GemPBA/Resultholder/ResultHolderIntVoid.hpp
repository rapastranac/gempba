#ifndef VOIDINTERMEDIATE_HPP
#define VOIDINTERMEDIATE_HPP

/*
* Created by Andres Pastrana on 2019
* pasr1602@usherbrooke.ca
* rapastranac@gmail.com
*/

#include "ResultHolderBase.hpp"

namespace GemPBA
{
    template <typename _Ret, typename... Args>
    class ResultHolderInt<_Ret, typename std::enable_if<std::is_void<_Ret>::value>::type, Args...> : virtual public ResultHolderBase<Args...>
    {
        friend class DLB_Handler;

    public:
        ResultHolderInt(DLB_Handler &dlb) : ResultHolderBase<Args...>(dlb) {}
    };
}
#endif