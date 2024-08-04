#ifndef ARGS_HANDLER_HPP
#define ARGS_HANDLER_HPP

#include "BranchHandler/ThreadPool.hpp"

#include <functional>
#include <iostream>
#include <type_traits>
#include <tuple>
#include <utility>

/*
 * Created by Andres Pastrana on 2019
 * pasr1602@usherbrooke.ca
 * rapastranac@gmail.com
 */

namespace std {

    class args_handler {
    private:
        template<typename F, typename... Args>
        //,  // typename std::enable_if<std::is_same<P, ThreadPool::Pool>::value>::type * = nullptr>
        static constexpr decltype(auto) helper(ThreadPool::Pool &pool, F &&f, Args &&...args) {
            return pool.push(std::forward<F>(f), std::forward<Args>(args)..., std::forward<nullptr_t>(nullptr));
        }

    public:

        /*begin<----------	This unpacks tuple before pushing to pool -------------------*/
        // void Callable

        template<typename F, typename Tuple, size_t... I>
        static constexpr decltype(auto) unpack_and_push_void(ThreadPool::Pool &pool, F &&f, Tuple &&t, std::index_sequence<I...>) {
            return helper(pool, std::forward<F>(f), std::get<I>(std::forward<Tuple>(t))...);
        }

        template<typename F, typename Tuple>
        static constexpr decltype(auto) unpack_and_push_void(ThreadPool::Pool &pool, F &&f, Tuple &&t) {
            return unpack_and_push_void(pool, std::forward<F>(f),
                                        std::forward<Tuple>(t),
                                        std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<Tuple>>>{});
        }

        /*-------------	This unpacks tuple before pushing to pool ---------------->end*/

        /*begin<----------	This unpacks tuple before pushing to pool -------------------*/
        // non void callable

        template<typename F, typename Tuple, size_t... I>
        static auto unpack_and_push_non_void(ThreadPool::Pool &pool, F &&f, Tuple &t, std::index_sequence<I...>) {
            return helper(pool, f, std::get<I>(t)...);
        }

        template<typename F, typename Tuple>
        static auto unpack_and_push_non_void(ThreadPool::Pool &pool, F &&f, Tuple &t) {
            // https://stackoverflow.com/a/36656413/5248548
            static constexpr auto size = std::tuple_size<Tuple>::value;
            return unpack_and_push_non_void(pool, f, t, std::make_index_sequence<size>{});
        }

        /*-------------	This unpacks tuple before pushing to pool ---------------->end*/

        /*begin<--- This unpacks tuple before forwarding it through the function -----*/
        // non void callable

        template<typename HOLDER, typename Function, typename Tuple, size_t... I>
        static auto unpack_and_forward_non_void(Function &&f, int id, Tuple &t, HOLDER *h, std::index_sequence<I...>) {
            return f(id, std::get<I>(t)..., h);
        }

        template<typename HOLDER, typename Function, typename Tuple>
        static auto unpack_and_forward_non_void(Function &&f, int id, Tuple &t, HOLDER *holder) {
            // https://stackoverflow.com/a/36656413/5248548
            static constexpr auto size = std::tuple_size<Tuple>::value;
            // std::cout << typeid(t).name() << "\n";
            return unpack_and_forward_non_void(f, id, t, holder, std::make_index_sequence<size>{});
        }
        /*------- This unpacks tuple before forwarding it through the function ----->end*/

        /*begin<--- This unpacks tuple before forwarding it through the function -----*/
        /* same as above, not tracking stack */ // TO IMPROVE

        template<typename F, typename Tuple, size_t... I>
        static constexpr decltype(auto) unpack_and_forward_void(F &&f, int id, Tuple &&t, void *holder, std::index_sequence<I...>) {
            return std::invoke(std::forward<F>(f),
                               std::forward<int>(id),
                               std::get<I>(std::forward<Tuple>(t))...,
                               std::forward<void *>(holder));
        }

        template<typename F, typename Tuple>
        static constexpr decltype(auto) unpack_and_forward_void(F &&f, int id, Tuple &&t, void *holder) {
            // https://stackoverflow.com/a/36656413/5248548
            return unpack_and_forward_void(std::forward<F>(f),
                                           std::forward<int>(id),
                                           std::forward<Tuple>(t),
                                           std::forward<void *>(holder),
                                           std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<Tuple>>>{});
        }
        /*------- This unpacks tuple before forwarding it through the function ----->end*/
    };

} // namespace std
#endif