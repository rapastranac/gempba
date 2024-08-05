#ifndef GEMPBA_TESTONLY_HPP
#define GEMPBA_TESTONLY_HPP

#include <iostream>
#include <map>
#include <tuple>
#include "schedulers/factory/scheduler_factory.hpp"
#include "schedulers/api/scheduler.hpp"

/*

template<typename F, std::size_t... Is, typename... Args>
static void forwarder_impl(F &f, std::index_sequence<Is...>, Args &... args) {
    // Create an array of void* to hold the addresses of the arguments
    void *argArray[] = {reinterpret_cast<void *>(&f), reinterpret_cast<void *>(&args)...};

    // Call the raw forwarder with the function and arguments
    void (*raw_function)(void **) = [](void **args_raw) {
        // Retrieve the function pointer
        F &reinterpreted_func = *reinterpret_cast<F *>(args_raw[0]);
        // Create a tuple of argument pointers using the helper functions
        std::tuple arg_tuple = std::make_tuple(*reinterpret_cast<Args *>(args_raw[Is + 1])...);

        // Apply the function to the dereferenced arguments
        std::apply(reinterpreted_func, arg_tuple);
    };

    using RawFuncType = void (*)(void *, void **);

    RawFuncType pFunction = [](void *raw_f, void **args_raw) {

        F &reinterpreted_func = *reinterpret_cast<F *>(raw_f);

        // Create a tuple of argument pointers using the helper functions
        std::tuple arg_tuple = std::make_tuple(*reinterpret_cast<Args *>(args_raw[Is])...);

        // Apply the function to the dereferenced arguments
        std::apply(reinterpreted_func, arg_tuple);
    };

    std::map<int, RawFuncType> map;

    map[1] = pFunction;

    raw_function(argArray);

    pFunction(argArray[0], &argArray[1]);
}

template<typename F, typename... Args>
void forwarder(F &f, Args &... args) {
    forwarder_impl(f, std::index_sequence_for<Args...>(), args...);
}

*/

static void print_vector(const std::vector<int> &vec) {
    for (int i = 0; i < vec.size(); ++i) {
        std::cout << "vec[" << i << "] = " << vec[i] << std::endl;
    }
    std::cout << std::endl;
}

void foo1(int id, int val1, float val2, double val3) {
    std::cout << "val1 = " << val1 << std::endl;
    std::cout << "val2 = " << val2 << std::endl;
    std::cout << "val3 = " << val3 << std::endl;
}

void foo2(int id, float val1, int val2) {
    std::cout << "val1 = " << val1 << std::endl;
    std::cout << "val2 = " << val2 << std::endl;
}

std::vector<int> *foo3(int id, double val1, std::vector<int> vec) {
    std::cout << "val1 = " << val1 << std::endl;
    std::cout << "vec size = " << vec.size() << std::endl;
    print_vector(vec);
    return new std::vector{7, 7, 7};
}


std::shared_future<void *> foo4() {
    auto tt = std::async(std::launch::async, []() {
        std::this_thread::sleep_for(std::chrono::seconds(3));

        std::string *msg = new std::string("Hello from foo4");
        return reinterpret_cast<void *>(msg);
    });
    return tt.share();
}

void returnWhenReady(const std::shared_future<void *> &future) {
    std::cout << "Waiting for foo4 to complete..." << std::endl;

    while (!gempba::is_future_ready(future)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "Waiting for foo4 to complete..." << std::endl;
    }


    void *result = future.get();
    std::cout << "foo4 completed. Result: " << *(reinterpret_cast<std::string *>(result)) << std::endl;
}

enum Test {
    ONE, TWO = 0, THREE
};


std::optional<int> foo() {
    // Hello
    return {};
}


int main2() {

   /* auto &bh = gempba::BranchHandler::getInstance();

//    bh.storeFunctions(map);


    auto &factory = gempba::SchedulerFactory::getInstance();
    auto &scheduler = factory.createScheduler(gempba::InterprocessProvider::MPI, gempba::Topology::SEMI_CENTRALIZED);

    auto val = scheduler.get_received_task_count(4);
    auto val2 = scheduler.get_received_task_count(4);


    gempba::SchedulerFactory &factory2 = gempba::SchedulerFactory::getInstance();


    auto sch = factory2.getSchedulerInstance();
    auto &sch2 = gempba::Scheduler::getInstance();

//
//    int id = -1;
//    gempba::forwarder<void>(foo1, id, val1, val2, val3);
//
//    auto result = gempba::forwarder<std::vector<int> *>(foo3, id, val3, dummy);
//    std::cout << std::endl << "Result vector is:" << std::endl;
//    print_vector(*result);*/

    return 0;
}

#endif //GEMPBA_TESTONLY_HPP
