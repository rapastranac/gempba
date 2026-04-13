# Generic Massive Parallelisation of Branching Algorithms

[![C/C++ CI (Ubuntu 24.04)](https://github.com/rapastranac/gempba/actions/workflows/c-cpp-ubuntu.yml/badge.svg)](https://github.com/rapastranac/gempba/actions/workflows/c-cpp-ubuntu.yml)
[![C/C++ CI (Windows 2022)](https://github.com/rapastranac/gempba/actions/workflows/c-cpp-windows.yml/badge.svg)](https://github.com/rapastranac/gempba/actions/workflows/c-cpp-windows.yml)
![GitHub License](https://img.shields.io/github/license/rapastranac/gempba)
![GitHub Release](https://img.shields.io/github/v/release/rapastranac/gempba)

## Why This Exists

If you have ever tried to parallelize a branch-and-bound algorithm by hand, you already know the pain. You start with a clean recursive function, it works great, and then someone says "can we use all these CPU cores?" Suddenly you are drowning in thread pools, work queues, mutexes protecting a shared best-solution, and recursive calls that somehow need to coordinate across threads without stomping on each other.

I built GemPBA because I kept solving the same problem from scratch for every new branching algorithm I worked on. The scheduling scaffold was always the same; only the algorithm in the middle changed. And every time I searched for an existing library, I either could not figure out how to build it (the documentation was three paragraphs and a "see the tests"), or it was so tightly coupled to one algorithm structure that adapting it meant basically rewriting it.

GemPBA's answer to this is a framework that inserts itself into your recursion through a small set of parameter additions. You keep writing your algorithm the way you always have. GemPBA handles the rest.

## What is GemPBA

GemPBA is a hybrid parallelization framework for branching algorithms. It supports:

- **Multithreading**: multiple worker threads within a single process
- **Multiprocessing**: work distributed across multiple processes (OpenMPI by default, but pluggable)
- **Hybrid**: multiple threads per process, spread across multiple nodes

There are two main research contributions baked into the framework.

The first is the **Quasi-Horizontal Load Balancing** strategy. Standard work-stealing treats all pending tasks equally. Quasi-horizontal balancing understands the shape of your recursion tree and selects work near the root, where each task will spawn the most downstream work. For branching algorithms where subtree sizes vary dramatically, this makes a significant difference in CPU utilization.

The second is the **Semi-Centralized Scheduler**. Distributing work across processes sounds straightforward until you hit the rejected task problem: a worker receives a task, but by the time it arrives the worker is already busy and has to bounce it back to the sender. Do this naively and you spend more time rerouting work than doing it. The semi-centralized design avoids this by keeping a lightweight center that never holds tasks itself but maintains global priority awareness. The center always knows which worker is idle and routes tasks directly there, eliminating the bounce-back problem without becoming a bottleneck.

For the full performance analysis and formal description, see:

- [MSc. Thesis](http://hdl.handle.net/11143/18687)
- [Paper in Parallel Computing](https://doi.org/10.1016/j.parco.2023.103024)

## Platforms

- Linux
- Windows

## Requirements

- **C++23** compatible compiler
- **CMake** >= 3.28
- **OpenMPI** >= 4.0 (only needed for multiprocessing support)
- **Boost** (optional, only for examples and tests)
- **GoogleTest** (optional, for running tests)

## Getting Started

### Building from the repository

```bash
git clone https://github.com/rapastranac/gempba.git
cd gempba
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Run tests
ctest

# Run an example
./bin/mt_benchmark
```

Building GemPBA as the root project automatically enables examples and tests. All binaries land in `bin/`.

### Dependencies (the no-hassle part)

GemPBA uses [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) for some of its external dependencies, and expects others to be installed on the system:

- **spdlog**: Must be installed system-wide (`apt-get install libspdlog-dev` / `pacman -S mingw-w64-x86_64-spdlog`)
- **BS_thread_pool**: Thread pool (fetched automatically by CPM)

When building examples or tests, CPM also fetches:

- **Boost**: Serialization and fiber support
- **GoogleTest**: Test framework

No `apt install libboost-all-dev` hunting required. I know, I was surprised too the first time it worked.

### Installing via system package manager

If you just want to consume GemPBA as a library without embedding it in your build, install it system-wide and use `find_package`.

**Ubuntu (APT)**

```bash
curl -fsSL https://rapastranac.github.io/gempba/pubkey.gpg \
  | sudo gpg --dearmor -o /etc/apt/keyrings/gempba.gpg
echo "deb [signed-by=/etc/apt/keyrings/gempba.gpg] https://rapastranac.github.io/gempba stable main" \
  | sudo tee /etc/apt/sources.list.d/gempba.list
sudo apt-get update
sudo apt-get install libgempba-dev
```

**Windows (MSYS2 / MinGW64)**

Download the latest `.pkg.tar.zst` from the [Releases page](https://github.com/rapastranac/gempba/releases), then:

```bash
pacman -U mingw-w64-x86_64-gempba-<version>-1-x86_64.pkg.tar.zst
```

Once installed, wire it into your CMake project:

```cmake
find_package(gempba REQUIRED)
target_link_libraries(your-target PRIVATE gempba::gempba)
```

### Installing into your own project

The recommended approach is CPM. Create an `external/CMakeLists.txt`:

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

Then in your root `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.28)

project(your-project VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)

# GemPBA flags
set(GEMPBA_MULTIPROCESSING ON CACHE BOOL "" FORCE)  # Enable MPI support
set(GEMPBA_DEBUG_COMMENTS ON CACHE BOOL "" FORCE)   # (Optional) extra logging
set(GEMPBA_BUILD_EXAMPLES ON CACHE BOOL "" FORCE)   # (Optional) build examples
set(GEMPBA_BUILD_TESTS ON CACHE BOOL "" FORCE)      # (Optional) build tests
add_subdirectory(external)

target_link_libraries(main PUBLIC gempba::gempba)
```

For minimal reproducible examples you can use as starting points, see the [GemPBA tester](https://github.com/rapastranac/gempba-tester) repository.

## Quick-Start: Your First Parallel Algorithm

Here is the minimal code to parallelize a branching algorithm. The example explores a binary tree to depth 5 (2^5 = 32 leaves, manageable without melting your laptop). The same pattern works for any recursive algorithm.

The recipe is always three additions:
1. Add `std::thread::id` as the first parameter and `gempba::node` as the last parameter of your function
2. Create nodes for each branch inside the function
3. Submit one branch to the thread pool queue, forward the other on the current thread

```cpp
#include <gempba/gempba.hpp>
#include <iostream>

// 1. Add std::thread::id and gempba::node to the signature
void explore(std::thread::id tid, int depth, gempba::node parent) {
    auto& nm = gempba::get_node_manager();
    auto& lb = *gempba::get_load_balancer();

    if (depth == 0) {
        int result = 1;
        nm.try_update_result(result, gempba::score::make(1));
        return;
    }

    // 2. Create nodes for each branch
    auto left = gempba::mt::create_explicit_node<void>(
        lb, parent, &explore, std::make_tuple(depth - 1)
    );
    auto right = gempba::mt::create_explicit_node<void>(
        lb, parent, &explore, std::make_tuple(depth - 1)
    );

    // 3. Submit one to the thread pool, run the other here
    nm.try_local_submit(left);
    nm.forward(right);
}

int main() {
    auto* lb = gempba::mt::create_load_balancer(gempba::balancing_policy::QUASI_HORIZONTAL);
    auto& nm = gempba::mt::create_node_manager(lb);

    nm.set_goal(gempba::goal::MAXIMISE, gempba::score_type::I32);
    nm.set_thread_pool_size(4);
    nm.set_score(gempba::score::make(0));

    // Kick off the search from depth 5
    auto seed = gempba::create_seed_node<void>(*lb, &explore, std::make_tuple(5));
    nm.try_local_submit(seed);
    nm.wait();

    std::cout << "Done! Explored the tree in parallel." << std::endl;
    return gempba::shutdown();
}
```

That is the whole pattern. Your algorithm stays almost unchanged. GemPBA inserts itself through the node parameters and manages all the scheduling. Check the [examples](examples/) directory for production-ready code with multiprocessing, result tracking, and serialization.

## The v3.0 Architecture

The v3.0 redesign has one guiding principle: the public API should be simple and template-free, while everything underneath should be pluggable.

The pluggable architecture is the core innovation of this release. Every major component is defined by an interface: the load balancer, the scheduler, the node implementation. You can replace any of them without touching user code. The default implementations cover the common cases. If you have specialized hardware, a custom IPC transport, or a domain-specific scheduling strategy, you implement the interface and plug it in.

What this means in practice for a user: you include one header, call a handful of factory functions, and wrap your recursive branches in nodes. The template machinery (function signature matching, IPC serialization, thread pool management) lives in `detail/` and stays there. You write zero templates. The load balancer does not know your function signatures. The scheduler does not know your argument types. Everything is decoupled through interfaces, which is exactly why it can be this clean from the outside.

The sections below document each public header and what it does.

## `gempba.hpp`

```cpp
#include <gempba/gempba.hpp>
```

This is the only header you need. It is the facade for the entire library. It exposes the global accessors, all factory functions, and the two namespaces you will use: `gempba::mt` for multithreading and `gempba::mp` for multiprocessing.

### Global accessors

These are how you reach the shared components from inside your algorithm, after they have been created in `main()`:

```cpp
load_balancer* lb = gempba::get_load_balancer();
node_manager&  nm = gempba::get_node_manager();
scheduler*      s = gempba::get_scheduler();   // multiprocessing only
```

### Seed creation

The seed node is the root of your search tree. It has no parent, so it is created outside the recursive function:

```cpp
// void function
auto seed = gempba::create_seed_node<void>(*lb, &my_func, std::make_tuple(initial_args...));

// non-void function
auto seed = gempba::create_seed_node<MyReturnType>(*lb, &my_func, std::make_tuple(initial_args...));
```

### Multithreading: `gempba::mt`

```cpp
// Built-in policy
auto* lb = gempba::mt::create_load_balancer(gempba::balancing_policy::QUASI_HORIZONTAL);

// Or bring your own
auto* lb = gempba::mt::create_load_balancer(std::make_unique<MyLoadBalancer>());

// One node manager per process
auto& nm = gempba::mt::create_node_manager(lb);

// Inside your recursive function: create a node for each branch
auto child = gempba::mt::create_explicit_node<void>(
    lb, parent, &my_func, std::make_tuple(args...)
);

// Lazy variant: args are computed on demand, which is useful when preparing them
// is expensive and the branch might be pruned before it ever executes
auto child = gempba::mt::create_lazy_node<void>(
    lb, parent, &my_func, args_initializer_fn
);
```

### Multiprocessing: `gempba::mp`

Each process runs the setup code and branches on its role (center or worker):

```cpp
// Built-in scheduler topology
auto* s = gempba::mp::create_scheduler(gempba::mp::scheduler_topology::SEMI_CENTRALIZED);

// Or bring your own
auto* s = gempba::mp::create_scheduler(std::make_unique<MyScheduler>());

// Load balancer that knows about the scheduler
auto* lb = gempba::mp::create_load_balancer(
    gempba::balancing_policy::QUASI_HORIZONTAL,
    &s->worker_view()
);

// Node manager for workers
auto& nm = gempba::mp::create_node_manager(lb, &s->worker_view());

// Nodes need serializers because arguments cross process boundaries as bytes
auto child = gempba::mp::create_explicit_node<void>(
    lb, parent, &my_func,
    std::make_tuple(args...),
    args_serializer_fn,    // Args... -> task_packet
    args_deserializer_fn   // task_packet -> tuple<Args...>
);
```

### Load balancing policies

Both namespaces support the same two policies:

- **`QUASI_HORIZONTAL`** (recommended): Understands the recursion tree structure. It preferentially takes work near the root of the tree, where each task will spawn the most downstream work. This reduces idle time significantly compared to naive work-stealing, especially on unbalanced trees. This is the main algorithmic contribution of the original research.

- **Work-stealing**: Takes the first available task from the queue. Simple to reason about, useful as a comparison baseline.

### Shutdown

```cpp
return gempba::shutdown();
```

Call at the end of `main()`. It finalizes the IPC backend (MPI by default) when multiprocessing is enabled and returns the correct exit code. Do not skip this if multiprocessing is on.

## `node_manager.hpp`

`node_manager` is a concrete class. It is intentionally not extensible: every user of GemPBA interacts with the same control interface, and there is no reason for different implementations. You get it from `gempba::get_node_manager()` or from the factory function.

Think of it as your algorithm's control panel: configure the goal, submit work, wait for completion, and collect the result.

### Setup

Call these before submitting any work:

```cpp
// Set the optimization direction and score representation type
nm.set_goal(gempba::goal::MAXIMISE, gempba::score_type::I32);
// or
nm.set_goal(gempba::goal::MINIMISE, gempba::score_type::F64);

// Set the initial best-known score (your algorithm improves from here)
nm.set_score(gempba::score::make(0));

// Set the number of worker threads
nm.set_thread_pool_size(std::thread::hardware_concurrency());
```

Available score types:

| Type | Use when |
|----------------------|-------------------------------|
| `I32` (default) | Integer scores, 32-bit range |
| `I64` | Integer scores, 64-bit range |
| `U_I32`, `U_I64` | Unsigned integer scores |
| `F32`, `F64`, `F128` | Floating point scores |

The score represents whatever "best" means for your problem: minimum cost, maximum coverage, shortest path length, fewest moves. You define the semantics.

### Submitting work

```cpp
// Push a node to the thread pool queue
// Returns false if no thread picked it up (it was resolved sequentially)
bool queued = nm.try_local_submit(node);

// Execute the node on the current thread immediately
nm.forward(node);

// (Multiprocessing) Send a node to another MPI rank
nm.try_remote_submit(node, runnable_id);

// Block until all work is complete
nm.wait();
```

The typical pattern inside a recursive function:

```cpp
void my_func(std::thread::id tid, /* your args */, gempba::node parent) {
    // ... create left and right nodes ...

    nm.try_local_submit(left);  // offer to the thread pool
    nm.forward(right);           // run this one on the current thread
}
```

`try_local_submit` either sends `left` to an idle thread or runs it locally if no thread is free. `forward` always runs on the current thread. The load balancer makes the actual scheduling decision.

### Updating results

When your algorithm finds a candidate solution, report it:

```cpp
// Pass the result by reference (not a literal) and its score
MyResult candidate = compute_candidate();
bool improved = nm.try_update_result(candidate, gempba::score::make(42));
```

`try_update_result` is thread-safe. It locks internally, compares the new score against the current best, and updates only if the new one is better according to the goal you set. Returns `true` if the update actually happened.

In multiprocessing mode, you also need a serializer so the result can be transmitted across process boundaries:

```cpp
std::function<task_packet(MyResult&)> serializer = [](MyResult& r) {
    // convert r to bytes
    return task_packet{/* serialized bytes */};
};

nm.try_update_result(candidate, new_score, serializer);
```

### Reading results

```cpp
// Best score seen so far (works in both mt and mp modes)
gempba::score best = nm.get_score();

// Multithreading: retrieve the actual result object
auto opt   = nm.get_result<MyResult>();     // returns std::nullopt if not set

// Multiprocessing: retrieve result as raw bytes for deserialization
std::optional<result> raw = nm.get_result_bytes();
```

### Diagnostics

```cpp
double idle    = nm.get_idle_time();              // cumulative thread idle time (ms)
size_t reqs    = nm.get_thread_request_count();   // number of times threads requested work
int    rank    = nm.rank_me();                    // MPI rank (-1 if not in mp mode)
double elapsed = node_manager::get_wall_time();   // wall clock time
```

The idle time metric is useful for performance tuning. High idle time usually means the tree is too narrow at the top (not enough parallel work early on) or the branching factor drops off faster than threads can grab tasks.

## `node.hpp`

```cpp
#include <gempba/core/node.hpp>  // included automatically via gempba.hpp
```

`node` is a `final` class. You cannot inherit from it. Internally it holds a `shared_ptr<node_core>` and delegates every operation to the underlying implementation through composition. This is what makes the framework pluggable: you can replace `node_core` with a custom implementation without changing anything in user code or in `node` itself.

Because `node` is just a shared pointer wrapper, copying it is cheap and safe. Two copies of the same node refer to the same underlying branch in the tree.

### You do not construct nodes directly

Factory functions in `gempba.hpp` create them:

```cpp
// Inside your recursive function
auto left = gempba::mt::create_explicit_node<void>(
    lb, parent, &my_func, std::make_tuple(args...)
);

// In main(), to start the whole thing
auto seed = gempba::create_seed_node<void>(*lb, &my_func, std::make_tuple(initial_args...));

// A dummy node acts as a placeholder (used when you need a root context without arguments)
auto dummy = gempba::create_dummy_node(*lb);
```

### Function signature requirements

Every function you parallelize must follow this pattern:

```cpp
// void return
void my_func(std::thread::id tid, ArgType1 a, ArgType2 b, gempba::node parent);

// non-void return
MyResult my_func(std::thread::id tid, ArgType1 a, ArgType2 b, gempba::node parent);
```

`std::thread::id` must be first. `gempba::node` must be last. Your actual arguments go between them. The `parent` node represents the current call in the tree, and you use it to create child nodes for the next level of recursion.

### Null checks and copying

```cpp
if (left) { /* initialized */ }
if (left != nullptr) { /* same thing */ }

gempba::node copy = left;  // cheap copy, same shared_ptr underneath

if (left == right) { /* same underlying node_core */ }
```

`node` satisfies `node_traits<node>`, which is the contract that load balancers and the scheduler depend on. The next section explains that contract.

## `node_core.hpp`

```cpp
#include <gempba/core/node_core.hpp>  // included automatically via gempba.hpp
```

`node_core` is an abstract base class. It defines what a node actually does: execute a function, serialize its arguments for IPC, track its position in the tree (parent, children, siblings), and manage its lifecycle state.

The default implementation, `node_core_impl` in `detail/nodes/node_core_impl.hpp`, handles all of this. Most users never interact with `node_core` directly.

This header exists for advanced users who need custom node behavior. Realistic use cases:

- **GPU execution**: `run()` dispatches a kernel to the device instead of calling a CPU function
- **Custom memory**: non-standard allocators, memory-mapped argument storage, arena allocation
- **Specialized serialization**: a custom IPC protocol where Boost serialization is not appropriate

To write a custom implementation, subclass `node_core` and implement every pure virtual method from `node_traits`. Then wrap it in a node using `gempba::create_custom_node`:

```cpp
class MyNodeCore : public gempba::node_core {
public:
    void run() override {
        // dispatch to GPU, custom allocator, or whatever you need
    }

    void delegate_locally(load_balancer* lb) override { /* ... */ }
    void delegate_remotely(scheduler::worker* w, int id) override { /* ... */ }
    task_packet serialize() override { /* ... */ }
    void deserialize(const task_packet& p) override { /* ... */ }
    // ... and all remaining pure virtual methods from node_traits
};

auto my_node = gempba::create_custom_node(std::make_unique<MyNodeCore>());
```

Fair warning: `node_core` requires implementing quite a few methods. Before starting, look at `node_core_impl` to understand the expected semantics for each one.

`node_core` itself extends `node_traits<shared_ptr<node_core>>`, using shared pointers as the handle type for tree traversal at the implementation layer.

## `node_traits.hpp`

```cpp
#include <gempba/core/node_traits.hpp>  // included automatically via gempba.hpp
```

`node_traits<T>` is the abstract interface that defines what every node must be able to do. It is templated on `T`, the handle type used for tree navigation:

- `node_traits<node>`: satisfied by `node` (the public API, handles passed by value)
- `node_traits<shared_ptr<node_core>>`: satisfied by `node_core` (the implementation layer, handles as shared pointers)

Load balancers program against `node_traits<node>`. They do not depend on `node`, `node_core`, or any specific class. This is the core of the pluggable design: the load balancer does not know or care whether you are using the default `node_core_impl` or a custom GPU implementation. As long as the handle satisfies the contract, everything works.

### The contract

**Tree navigation** (used by the load balancer to understand the shape of the search):

```cpp
virtual T get_parent() = 0;
virtual T get_root() = 0;
virtual T get_leftmost_child() = 0;
virtual T get_second_leftmost_child() = 0;
virtual T get_leftmost_sibling() = 0;
virtual T get_left_sibling() = 0;
virtual T get_right_sibling() = 0;
virtual int get_children_count() const = 0;
```

**Lifecycle** (used by the load balancer to decide whether a node is worth taking):

```cpp
virtual node_state get_state() const = 0;
virtual void set_state(node_state) = 0;
virtual bool is_consumed() const = 0;
virtual bool should_branch() = 0;
virtual void prune() = 0;
```

**Execution** (how a node actually runs):

```cpp
virtual void run() = 0;
virtual void delegate_locally(load_balancer*) = 0;
virtual void delegate_remotely(scheduler::worker*, int runner_id) = 0;
```

**Serialization** (used when nodes travel across process boundaries):

```cpp
virtual task_packet serialize() = 0;
virtual void deserialize(const task_packet&) = 0;
virtual void set_result_serializer(...) = 0;
virtual void set_result_deserializer(...) = 0;
```

**Metadata** (mostly useful for debugging):

```cpp
virtual int get_node_id() const = 0;
virtual std::thread::id get_thread_id() const = 0;
virtual int get_forward_count() const = 0;
virtual int get_push_count() const = 0;
virtual bool is_dummy() const = 0;
```

You interact with this indirectly through `node` and `node_manager`. You only need to read this header directly if you are implementing a custom `node_core` or a custom `load_balancer`.

### Node lifecycle states

```cpp
enum node_state {
    UNUSED,                   // just created, not yet submitted
    FORWARDED,                // executed on the current thread
    PUSHED,                   // sent to another thread within this process
    DISCARDED,                // pruned before it ran
    RETRIEVED,                // result was collected
    SENT_TO_ANOTHER_PROCESS   // handed off to another MPI rank
};
```

## `serial_runnable.hpp`

```cpp
#include <gempba/core/serial_runnable.hpp>  // included automatically via gempba.hpp
```

`serial_runnable` solves a problem that only appears in multiprocessing: your algorithm may call several different functions with different signatures. When a task crosses a process boundary, type information is gone. You cannot send a `std::function<void(int, Graph&, node)>` as-is. You can only send bytes.

The solution is type erasure. Each `serial_runnable` wraps one typed function and exposes only two things to the outside world:

```cpp
class serial_runnable {
public:
    virtual int get_id() const = 0;

    virtual std::optional<std::shared_future<task_packet>>
    operator()(node_manager& nm, const task_packet& task) = 0;
};
```

The scheduler maintains a map of `{ id -> serial_runnable }`. When a `task_packet` arrives, the scheduler looks up the ID, calls `operator()` on the matching runnable, and that is it. The runnable knows the actual function type internally (it captured it at construction), so it can deserialize the arguments from bytes, call the function, and serialize the result back. The scheduler never touches function types. It just routes bytes by ID.

### Creating a runnable for a void function

```cpp
// The deserializer reconstructs the argument tuple from incoming bytes
std::function<std::tuple<int, MyGraph>(task_packet)> deserializer = [](task_packet p) {
    // parse p and return the argument tuple
    return std::make_tuple(/* depth */, /* graph */);
};

auto runnable = gempba::mp::runnables::return_none::create<int, MyGraph>(
    MY_FUNCTION_ID,   // unique integer ID for this function
    &my_func,
    deserializer
);
```

### Creating a runnable for a non-void function

```cpp
std::function<std::tuple<int, MyGraph>(task_packet)> deserializer = /* ... */;

// The serializer converts the return value to bytes for the return trip
std::function<task_packet(MyResult)> serializer = [](MyResult r) {
    return task_packet{/* r serialized */};
};

auto runnable = gempba::mp::runnables::return_value::create<MyResult, int, MyGraph>(
    MY_FUNCTION_ID,
    &my_func,
    deserializer,
    serializer
);
```

### Registering runnables and starting the scheduler

Collect your runnables into a map and pass them to the scheduler's worker. The worker will block and process incoming tasks until the computation is complete:

```cpp
std::map<int, std::shared_ptr<gempba::serial_runnable>> runnables;
runnables[MY_FUNCTION_ID]    = runnable_a;
runnables[OTHER_FUNCTION_ID] = runnable_b;

// This blocks until the scheduler signals completion
s->worker_view().run(nm, runnables);
```

At runtime, when a `task_packet` arrives tagged with `MY_FUNCTION_ID`, the scheduler finds the matching `serial_runnable` and calls its `operator()`. All the template machinery (argument deserialization, function dispatch, result serialization) stays inside the concrete implementations in `detail/runnables/`. Nothing typed ever crosses a process boundary.

## Examples

The `examples/` directory has complete, runnable programs. They double as templates: find the closest one to your use case, swap in your algorithm, and go.

### Benchmarks

Synthetic binary tree traversal, no domain logic. Start here if you just want to see GemPBA running.

| File | Mode | Scheduler | Notes |
|--------------------|----------------|------------------|-------|
| `mt_benchmark.cpp` | Multithreading | n/a | Simplest possible setup. No MPI, no serialization. |
| `mp_benchmark.cpp` | Multiprocessing | Semi-centralized | Same traversal over MPI. Shows the center/worker split and stats collection. |

### Minimum Vertex Cover

A real combinatorial optimization problem on graphs, with pruning and result tracking. Use these as templates for your own branch-and-bound algorithm.

| File | Mode | Scheduler | Encoding | Notes |
|--------------------------------------|----------------|------------------|-------------------|--------------------------------------|
| `mt_bitvect_opt_enc_semi.cpp` | Multithreading | n/a | Bitvector | Recommended starting point for MT. |
| `mp_bitvect_opt_enc_semi.cpp` | Multiprocessing | Semi-centralized | Bitvector | Recommended starting point for MP. |
| `mp_bitvect_opt_enc_central.cpp` | Multiprocessing | Centralized | Bitvector | Same as above, centralized topology. |
| `mt_graph_opt_enc_semi.cpp` | Multithreading | n/a | Graph class | Same problem, larger per-node payload. |
| `mt_graph_opt_enc_semi_non_void.cpp` | Multithreading | n/a | Graph class | Non-void recursive function variant. |
| `mp_bitvect_basic_enc_semi.cpp` | Multiprocessing | Semi-centralized | Bitvector (basic) | Older encoding, kept for comparison. |
| `mp_bitvect_basic_enc_central.cpp` | Multiprocessing | Centralized | Bitvector (basic) | Older encoding, centralized topology. |

## The Centralized Scheduler

GemPBA ships with two scheduler topologies for multiprocessing. The default recommendation is `SEMI_CENTRALIZED`. The `CENTRALIZED` topology is included for comparison.

```cpp
// Semi-centralized (recommended)
auto* s = gempba::mp::create_scheduler(gempba::mp::scheduler_topology::SEMI_CENTRALIZED);

// Centralized (for comparison)
auto* s = gempba::mp::create_scheduler(gempba::mp::scheduler_topology::CENTRALIZED);
```

In the **semi-centralized** topology, rank 0 acts as a coordinator that routes tasks between workers but does not participate in computation. Workers request work from the center when their queues run dry. This keeps the coordinator lightweight and scales better as the number of worker ranks grows.

In the **centralized** topology, the center takes a more active role in routing decisions. At small process counts it can perform comparably, but it becomes a bottleneck at scale because all task routing goes through a single point. It is kept in the library primarily as a baseline for the performance comparisons in the original paper.

If you are not sure which to use, go with `SEMI_CENTRALIZED`. The centralized scheduler is there when you need to reproduce the paper's comparison experiments or benchmark your own scheduling strategy against it.

## If You Find This Useful

If GemPBA saved you time or helped your research, please consider giving the repository a star on GitHub. It helps gauge how many people are actually using it and gives me a reason to keep improving things. Setting up a decent parallel framework is genuinely painful, and if this one got you unstuck, that is worth knowing.

## Copyright and Citing

Copyright © 2021-2025 [Andrés Pastrana](https://www.linkedin.com/in/andrepas/). Licensed under the [MIT license](https://github.com/rapastranac/gempba/blob/main/LICENSE).

If you use GemPBA in software or research of any kind, please include a link to [the GitHub repository](https://github.com/rapastranac/gempba) in your source code and documentation.

If you publish results obtained with GemPBA, please also cite the paper:

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