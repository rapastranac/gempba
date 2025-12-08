# Generic Massive Parallelisation of Branching Algorithms

[![C/C++ CI (Ubuntu 24.04)](https://github.com/rapastranac/gempba/actions/workflows/c-cpp-ubuntu.yml/badge.svg)](https://github.com/rapastranac/gempba/actions/workflows/c-cpp-ubuntu.yml)
[![C/C++ CI (Windows 2022)](https://github.com/rapastranac/gempba/actions/workflows/c-cpp-windows.yml/badge.svg)](https://github.com/rapastranac/gempba/actions/workflows/c-cpp-windows.yml)
![GitHub License](https://img.shields.io/github/license/rapastranac/gempba)
![GitHub Release](https://img.shields.io/github/v/release/rapastranac/gempba)

## Requirements

- **C++23** compatible compiler
- **CMake** ≥ 3.28
- **OpenMPI** ≥ 4.0 (for multiprocessing support)
- **Boost** libraries (optional, only for examples and tests)
- **GoogleTest** (optional, for running tests)

## Dependency Management

This project uses [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) for managing external dependencies. CPM is a CMake script-based dependency manager with no installation required. Some dependencies are used only for testing and examples, so they are not included in the main build.

List of dependencies:
- [spdlog](https://github.com/gabime/spdlog)
- [argparse](https://github.com/p-ranav/argparse)

## Platforms
- Linux
- Windows

## Installation

To include **GemPBA** in your project, you can either clone the repository or download it as a zip file. However, it is recommended to use **CPM** to manage the library.

Create an `external/CMakeLists.txt` file:

```cmake
cmake_minimum_required(VERSION 3.28)
include(FetchContent)

project(external)

## CPM
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
if (NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
    message(STATUS "Downloading CPM.cmake")
    file(DOWNLOAD https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/CPM.cmake ${CPM_DOWNLOAD_LOCATION})
endif ()
include(${CPM_DOWNLOAD_LOCATION})

CPMAddPackage(
    NAME gempba
    GITHUB_REPOSITORY rapastranac/gempba
    GIT_TAG main
)

add_library(external INTERFACE)
target_link_libraries(external INTERFACE gempba)
```

Then in your main `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.28)

project(your-project VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)

# GemPBA flags
set(GEMPBA_MULTIPROCESSING ON CACHE BOOL "" FORCE)  # Enable multiprocessing support
set(GEMPBA_DEBUG_COMMENTS ON CACHE BOOL "" FORCE)   # (Optional) Debug output
set(GEMPBA_BUILD_EXAMPLES ON CACHE BOOL "" FORCE)   # (Optional) Build examples
set(GEMPBA_BUILD_TESTS ON CACHE BOOL "" FORCE)      # (Optional) Build tests
add_subdirectory(external)

# Link GemPBA with your executable
target_link_libraries(main PUBLIC gempba::gempba)
```

For minimal reproducible examples, see the [GemPBA tester](https://github.com/rapastranac/gempba-tester) repository.

## Description

This tool helps you parallelize almost any branching algorithm that initially seemed impossible or super complex to parallelize. Please refer to this [MSc. Thesis](http://hdl.handle.net/11143/18687) and [Paper](https://doi.org/10.1016/j.parco.2023.103024) for a performance report.

**GemPBA** is a hybrid parallelization tool that allows you to perform parallelism using multithreading, multiprocessing, or both simultaneously. It features the novel **Quasi-Horizontal Load Balancing** strategy, which significantly decreases CPU idle time and increases performance by reducing the overhead of parallel calls in branching algorithms. The library seamlessly handles all three modes: pure multithreading within a single process, pure multiprocessing across multiple processes, or a hybrid approach where each process runs multiple worker threads for maximum scalability.

## Core Concepts

### The Facade Pattern

GemPBA is designed around a **facade pattern**. Everything you need is accessible by including a single header:

```cpp
#include <gempba/gempba.hpp>
```

This facade provides factory functions and utilities to create and interact with the library components. Behind the facade, GemPBA uses interfaces that allow you to provide your own implementations as long as you abide by the contract.

### Extensible Architecture

GemPBA components are designed to be extensible:

- **`scheduler`**: The default implementation uses OpenMPI for inter-process communication, but you can implement your own using UPC++, MPI alternatives, or any other IPC framework. Each scheduler implementation has a corresponding stats implementation.

- **`load_balancer`**: GemPBA provides two implementations:
    - **QUASI_HORIZONTAL** (recommended): A novel load balancing strategy that uses the tree structure to make intelligent work distribution decisions
    - **Work-stealing/Greedy**: A naive approach for comparison purposes

  You can implement your own load balancing strategy by extending the `load_balancer` interface.

- **`node_core`**: The underlying implementation of nodes. While most users will use the provided implementations through the facade, advanced users can implement their own `node_core` for specialized use cases.

- **`serial_runnable`**: Defines how functions are invoked from serialized data in multiprocessing contexts. You can create custom runnables for specialized execution patterns.

- **`stats`**: Each scheduler implementation has its own stats implementation. Stats use the **visitor pattern** to allow flexible data retrieval. Each stats implementation has a corresponding visitor that accesses internal metrics via string keys.

### Problem Goals

Branching algorithms typically aim to find the best solution according to some criteria, which is usually a minimization or maximization problem.

- `gempba::goal::MAXIMISE` (default): Find the maximum score
- `gempba::goal::MINIMISE`: Find the minimum score

Set your goal using:
```cpp
node_manager.set_goal(gempba::goal::MINIMISE, gempba::score_type::I32);
```

### Score Types

The `gempba::score` class wraps primitive types used to compare solution quality. The score represents anything that can be used to evaluate solution quality: the size of the solution, its cost, or any other metric.

Available types via `gempba::score_type`:
- `I32` (default), `I64` for integers
- `U_I32`, `U_I64` for unsigned integers
- `F32`, `F64`, `F128` for floating point numbers

Example:
```cpp
gempba::score score = gempba::score::make(42);
node_manager.set_score(score);
```

### Nodes and Node Core

In **GemPBA**, each branch in your recursive algorithm is represented by a `gempba::node`. The `node` is a **lightweight shell** that should be passed by value. It wraps a `node_core` implementation, which contains the actual logic and data.

Key characteristics:
- **Nodes form a tree structure** where parent-child relationships track the recursion hierarchy
- **Each node represents a function call** with its arguments
- **The load balancer uses this tree** to make intelligent work distribution decisions
- **Nodes are lightweight**: Pass them by value without performance concerns
- **Node core is extensible**: Advanced users can implement custom `node_core` types for specialized behavior

The separation between `node` (the interface) and `node_core` (the implementation) allows for:
- Easy copying and passing of nodes
- Custom implementations without breaking the API
- Efficient memory management through shared pointers internally

## Multithreading

Multithreading is the simplest parallelization mode. Consider a sequential branching function:

```cpp
template<typename... Args>
void foo(Args... p_args) {
    if (/* termination condition */) {
        return;
    }
    
    // Recursive branches
    foo(/* left branch args */);
    foo(/* middle branch args */);
    foo(/* right branch args */);
}
```

### Function Signature Changes

To parallelize, modify the function signature to accept:
1. `std::thread::id` as the first parameter (for thread identification)
2. Your original arguments
3. `gempba::node` as the last parameter (for tree structure tracking)

```cpp
#include <gempba/gempba.hpp>

template<typename... Args>
void foo(std::thread::id p_tid, Args... p_args, gempba::node p_parent) {
    const auto &v_node_manager = gempba::get_node_manager();
    
    if (/* termination condition */) {
        ResultType v_result;  // Compute result
        gempba::score v_new_score = gempba::score::make(v_result);
        v_node_manager.try_update_result(v_result, v_new_score);
        return;
    }
    
    const auto v_load_balancer = gempba::get_load_balancer();
    
    // Create parent node if needed
    gempba::node v_parent = p_parent ? p_parent : gempba::create_dummy_node(*v_load_balancer);
    
    // Create nodes for branches
    gempba::node v_left = gempba::mt::create_explicit_node<void>(
        *v_load_balancer, v_parent, &foo, std::make_tuple(/* left branch args */)
    );
    
    gempba::node v_right = gempba::mt::create_explicit_node<void>(
        *v_load_balancer, v_parent, &foo, std::make_tuple(/* right branch args */)
    );
    
    // Submit branches for parallel execution
    v_node_manager.try_local_submit(v_left);
    v_node_manager.forward(v_right);  // Execute last branch sequentially
}
```

### Main Function Setup

```cpp
int main() {
    // Initialize components
    auto *v_load_balancer = gempba::mt::create_load_balancer(gempba::balancing_policy::QUASI_HORIZONTAL);
    auto &v_node_manager = gempba::mt::create_node_manager(v_load_balancer);
    
    // Configure problem
    v_node_manager.set_goal(gempba::goal::MINIMISE, gempba::score_type::I32);
    v_node_manager.set_thread_pool_size(numThreads);
    v_node_manager.set_score(gempba::score::make(initialValue));
    
    // Create initial task
    auto seed = gempba::create_seed_node<void>(*v_load_balancer, &foo, std::make_tuple(/* initial args */));
    
    // Execute and wait
    v_node_manager.try_local_submit(seed);
    v_node_manager.wait();
    
    // Retrieve result
    gempba::score final_score = v_node_manager.get_score();
    
    return gempba::shutdown();
}
```

### Lazy Nodes (Branch Discarding)

Use lazy nodes to avoid creating arguments for branches that will be discarded:

```cpp
gempba::node v_left = gempba::mt::create_lazy_node<void>(
    *v_load_balancer, v_parent, &foo, 
    [&]() -> std::optional<std::tuple<Args...>> {
        // Build arguments only if needed
        if (/* branch is worth exploring */) {
            return std::make_tuple(/* left branch args */);
        }
        return std::nullopt;  // Skip this branch
    }
);
```

## Multiprocessing

Multiprocessing extends parallelization across multiple machines or processes using OpenMPI(default). It requires:
1. Serialization/deserialization functions
2. Scheduler initialization
3. Different code paths for center (rank 0) and worker processes

### Running with MPI

```bash
mpirun -n 10 ./your_program args...
```

Where:
- `-n 10` spawns 10 processes (1 center + 9 workers)

### Function Modifications

The function body remains similar to multithreading, but uses `gempba::mp` namespace and requires serializers:

```cpp
template<typename... Args>
void foo(std::thread::id p_tid, Args... p_args, gempba::node p_parent) {
    const auto &v_node_manager = gempba::get_node_manager();
    
    if (/* termination condition */) {
        ResultType v_result;
        gempba::score v_new_score = gempba::score::make(v_result);
        
        // Serializer needed for result transmission
        auto result_serializer = [](ResultType& r) {
            // Serialize result to gempba::task_packet
            return gempba::task_packet{/* serialized bytes */};
        };
        
        v_node_manager.try_update_result(v_result, v_new_score, result_serializer);
        return;
    }
    
    const auto v_load_balancer = gempba::get_load_balancer();
    gempba::node v_parent = p_parent ? p_parent : gempba::create_dummy_node(*v_load_balancer);
    
    // Define serializers for arguments
    auto args_serializer = [](Args... args) {
        // Convert args to gempba::task_packet
        return gempba::task_packet{/* serialized bytes */};
    };
    
    auto args_deserializer = [](gempba::task_packet packet) {
        // Convert packet back to tuple of args
        return std::tuple<Args...>{/* deserialized args */};
    };
    
    // Create nodes with serialization support
    gempba::node v_left = gempba::mp::create_lazy_node<void>(
        *v_load_balancer, v_parent, &foo,
        [&]() -> std::optional<std::tuple<Args...>> {
            if (/* worth exploring */) {
                return std::make_tuple(/* left args */);
            }
            return std::nullopt;
        },
        args_serializer,
        args_deserializer
    );
    
    // Try remote submission first, falls back to local
    v_node_manager.try_remote_submit(v_left, 0);
    v_node_manager.forward(v_right);
}
```

### Main Function (Multiprocessing)

```cpp
int main() {
    auto *v_scheduler = gempba::mp::create_scheduler(
        gempba::mp::scheduler_topology::SEMI_CENTRALIZED
    );
    
    int v_rank = v_scheduler->rank_me();
    
    // Helper functions to initialize components
    auto *v_load_balancer = initiate_load_balancer(v_scheduler, gempba::balancing_policy::QUASI_HORIZONTAL);
    auto &v_node_manager = initiate_node_manager(v_scheduler, v_load_balancer);
    
    v_node_manager.set_goal(gempba::goal::MINIMISE, gempba::score_type::I32);
    v_node_manager.set_score(gempba::score::make(initialValue));
    
    v_scheduler->barrier();
    
    constexpr int v_runnable_id = 0;
    
    if (v_rank == 0) {
        // CENTER PROCESS: Coordinator
        gempba::task_packet v_seed{/* serialized initial args */};
        
        auto &v_center = v_scheduler->center_view();
        v_center.run(v_seed, v_runnable_id);
        
        v_scheduler->barrier();
        
        // Retrieve result
        gempba::task_packet result = v_center.get_result();
        // Deserialize result...
        
    } else {
        // WORKER PROCESS: Executor
        v_node_manager.set_thread_pool_size(numThreads);
        
        auto &v_worker = v_scheduler->worker_view();
        
        // Create runnable from function
        auto v_deserializer = [](gempba::task_packet packet) {
            return std::tuple<Args...>{/* deserialized */};
        };
        
        auto v_runnable = gempba::mp::runnables::return_none::create(
            v_runnable_id, &foo, v_deserializer
        );
        
        std::map<int, std::shared_ptr<gempba::serial_runnable>> v_runnables;
        v_runnables[v_runnable_id] = v_runnable;
        
        v_worker.run(v_node_manager, v_runnables);
        
        v_scheduler->barrier();
    }
    
    return gempba::shutdown();
}
```

### Helper Functions for Multiprocessing

```cpp
gempba::node_manager& initiate_node_manager(gempba::scheduler *p_scheduler, gempba::load_balancer *p_load_balancer) {
    if (p_scheduler->rank_me() == 0) {
        return gempba::mt::create_node_manager(p_load_balancer);
    }
    return gempba::mp::create_node_manager(p_load_balancer, &p_scheduler->worker_view());
}

gempba::load_balancer* initiate_load_balancer(gempba::scheduler *p_scheduler, gempba::balancing_policy p_policy) {
    if (p_scheduler->rank_me() == 0) {
        return gempba::mt::create_load_balancer(p_policy);
    }
    return gempba::mp::create_load_balancer(p_policy,&p_scheduler->worker_view());
}
```

## Statistics Collection

After execution, gather performance metrics:

```cpp
// Synchronize stats across all processes
v_scheduler->synchronize_stats();

if (v_rank == 0) {
    auto stats_vector = v_scheduler->get_stats_vector();
    
    for (int i = 0; i < stats_vector.size(); ++i) {
        auto visitor = gempba::mp::get_default_mpi_stats_visitor();
        stats_vector[i]->visit(visitor.get());
        
        std::cout << std::format("Rank {}\n", i);
        std::cout << std::format("  Received: {}\n", visitor->m_received_task_count);
        std::cout << std::format("  Sent: {}\n", visitor->m_sent_task_count);
        std::cout << std::format("  Idle time: {}s\n", visitor->m_idle_time);
        std::cout << std::format("  Elapsed: {}s\n", visitor->m_elapsed_time);
    }
}
```

For multithreading:
```cpp
double idle_time = v_node_manager.get_idle_time();
size_t thread_requests = v_node_manager.get_thread_request_count();
```

## Key API Components

### Multithreading (`gempba::mt`)

```cpp
// Create load balancer
load_balancer* create_load_balancer(balancing_policy);

// Create node manager
node_manager& create_node_manager(load_balancer*);

// Create nodes
node create_explicit_node<Ret, Args...>(load_balancer&, node&, function, args);
node create_lazy_node<Ret, Args...>(load_balancer&, node&, function, args_initializer);
```

### Multiprocessing (`gempba::mp`)

```cpp
// Create scheduler
scheduler* create_scheduler(scheduler_topology, timeout = 3.0);

// Create load balancer
load_balancer* create_load_balancer(balancing_policy, scheduler::worker*);

// Create node manager
node_manager& create_node_manager(load_balancer*, scheduler::worker*);

// Create serializable nodes
node create_explicit_node<Ret, Args...>(load_balancer&, node&, function, args, serializer, deserializer);
node create_lazy_node<Ret, Args...>(load_balancer&, node&, function, args_initializer, serializer, deserializer);

// Create runnables
namespace runnables::return_none {
    shared_ptr<serial_runnable> create(id, function, deserializer);
}
```

### Node Manager

```cpp
void set_goal(goal, score_type);
void set_score(score);
score get_score();
void set_thread_pool_size(size);

bool try_local_submit(node&);
bool try_remote_submit(node&, runnable_id);
void forward(node&);

bool try_update_result(solution, score);
bool try_update_result(solution, score, serializer);

void wait();
double get_idle_time();
size_t get_thread_request_count();
```

## Examples

See the repository for complete working examples:
- `mt_benchmark.cpp`: Multithreading benchmark
- `mp_benchmark.cpp`: Multiprocessing benchmark
- `mt_bitvect_opt_enc_semi.cpp`: Multithreading vertex cover
- `mp_bitvect_opt_enc_semi.cpp`: Multiprocessing vertex cover

## Centralized Scheduler (Optional)

GemPBA includes an optional centralized scheduler for comparison purposes. Note that this is not part of the main project scope.

## Starring the Project

If you find this project helpful, I'd greatly appreciate it if you could give it a star on GitHub! This helps me gauge how many people are benefiting from the code and encourages me to continue enhancing and maintaining it.

## Copyright and Citing

Copyright © 2021-2025 [Andrés Pastrana](https://www.linkedin.com/in/andrepas/). Licensed under the [MIT license](https://github.com/rapastranac/gempba/blob/main/LICENSE).

If you use this library in software of any kind, please provide a link to [the GitHub repository](https://github.com/rapastranac/gempba) in the source code and documentation.

If you use this library in published research, please cite it as follows:

* Andres Pastrana-Cruz, Manuel Lafond, *"A lightweight semi-centralized strategy for the massive parallelization of branching algorithms"*, [doi.org/10.1016/j.parco.2023.103024](https://doi.org/10.1016/j.parco.2023.103024), [Parallel Computing (2023) 103024](https://www.sciencedirect.com/science/article/abs/pii/S0167819123000303), [arXiv:2305.09117](https://arxiv.org/abs/2305.09117)

For BibTeX users:

```bibtex
@article{PASTRANACRUZ2023103024,
    archiveprefix = {arXiv},
    title = {A lightweight semi-centralized strategy for the massive parallelization of branching algorithms},
    journal = {Parallel Computing},
    volume = {116},
    pages = {103024},
    year = {2023},
    issn = {0167-8191},
    doi = {https://doi.org/10.1016/j.parco.2023.103024},
    url = {https://www.sciencedirect.com/science/article/pii/S0167819123000303},
    author = {Andres Pastrana-Cruz and Manuel Lafond},
    keywords = {Load balancing, Vertex cover, Parallel algorithms, Scalable parallelism, Branching algorithms}
}
```

The publication is on [Parallel Computing](https://doi.org/10.1016/j.parco.2023.103024) and [arXiv](https://arxiv.org/abs/2305.09117) do not reflect the most recent library updates. These papers help researchers discover the library and provide citation references. For the latest documentation, see the [GitHub repository](https://github.com/rapastranac/gempba).