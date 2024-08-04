# Generic Massive Parallelisation of Branching Algorithms

<br /> 

 This tool will help you parallelise almost any branching algorithm that seemed initially impossible or super complex to do. Please refer to this [MSc. Thesis](http://hdl.handle.net/11143/18687) and [Paper](https://doi.org/10.1016/j.parco.2023.103024), for a performance report.

 **GemPBA** will allow you to perform parallelism using a multithreading or multiprocessing environment. It also contains a robust ***Dynamic Load Balancing*** (DLB) that significantly decreases the CPU idle time, which also increases performance due to the reduction of parallel calls in branching algorithms.

**GemPBA** has four main modules that help you easily understand its applications.

 - *Branch Handler:*

    This module is in charge of handling tasks among the processors, whether multithreading or multiprocessing is being used. It manages a thread pool, for which the user can allocate the number of threads he needs, yet it is recommended to allocate them according to the number of physical cores in your machine.

    It essentially finds the first available processor in the thread pool, or in a remote participating machine.
    <br /> 

 - *Result Holder:*
 
    In order to keep track of the arguments such that they can properly be managed by the library. The function signature must be slightly modified to include two additional parameters, one as the first, and other as the last one. This modification will be explained later.
    <br /> 
 
 - *Dynamic Load Balancing Handler:*

    Since recursive functions create a stack, the *DLB* instance has access to all the generated tasks at each level of it by the means of the *ResultHolder* instances. Thus, when creating an instance of the *ResultHolder*, the *Dynamic Load Balancer* must be passed in the constructor.
    <br /> 
 
 - *MPI Scheduler:*

    This additional module is in charge of establishing inter-process communication using a lightweight semi-centralised topology. If multiprocessing parallelism is of the user's interest, then this module must be used and passed to the Branch Handler just after its initialisation,
    

<br /> 
<br />

## Multithreading

This is the easiest environment to setup, which is in turn the fastest to implement. In general the user must modify only the main functions to be parallelised. If the algorithm uses more than a single function recursion, this can also be parallelised.

Consider the following function with three branches per recursion. Essentially, when finding a solution, every algorithm can be reduced to a minimisation or maximisation problem. For the sake of the following example, let's assume that it is a minimisation problem.

```cpp
void foo(MyClass instance, float f, double d){
    /* local solution might be the size of any input argument*/
    if (localSolution < bestValueSoFar){
        bestValueSoFar = localSolution;
        return;
    }

    /* intance, f and d are used to create sub instances for the
        recursions, for left, middle and right branches.
    */

    foo(instance_l, f_l, d_l);
    foo(instance_m, f_m, d_m);
    foo(instance_r, f_r, d_r);
    return;
}
```

In order to parallelise the previous code, the function signature should change like this.
<br /> 

```cpp
void foo(int id, MyClass instance, float f, double d, void *parent = nullptr);
```

<br />

 Where ```id``` stands for thread ID and ```parent``` is designed to used the *Novel Dynamic Load Balancing*.

 These additional arguments are to be used by the library only, yet the user could also use them to track like threads utilization and other scenarios that it might find applicable.


 Thus, the parallelised version of the code will be like as follows.

 ```cpp
std::mutex mtx;
auto &dlb = GemPBA::DLB_Handler::getInstance();
auto &branchHandler = GemPBA::BranchHandler::getInstance();
using HolderType = GemPBA::ResultHolder<void, MyClass, float, double>;

void foo(int id, MyClass instance, float f, double d, void *parent = nullptr)

    // local solution might be the size of any input argument

    if (localSolution < branchHandler.refValue()){
        /* the following mutex ensures that one best solution so far is
        store at a time */
        std::scoped_lock<std::mutex> lock(mtx); 

        /*  if the condition is met, then the solution can be stored in
        the library, which can be retrieved at the end of the
        execution, if the solution is not required to be stored, then
        there is no need to invoke this method nor to use the mutex */   
        branchHandler.holdSolution(localSolution);
        
        /*this method stores the best reference value of the solution 
        found so far, which is usually the size of it. It is thread safe,
        and no synchronisation methods are required from the user's end
        as long as holdSolution() is not invoked.*/
        branchHandler.updateRefValue(localSolution.size());
        
        return;
    }

    /* intance, f and d are used to create sub instances for the
        recursions, for left, middle and right branches.
    */
    

    HolderType *dummyParent = nullptr;   // in the case parent is nullptr

    /* The dynamic load balancing uses tracks the search tree using these
    temporary arguments holders.*/

    HolderType rHolder_l(dlb, id, parent);
    HolderType rHolder_m(dlb, id, parent);
    HolderType rHolder_r(dlb, id, parent);

    /*  if parent is nullptr, then a virtual root is should be created
    such that branches within this scope can be accessed from below */
    if (!parent){
        dummyParent = new HolderType(dlb, id);
        dlb.linkVirtualRoot(id, dummyParent, rHolder_l, rHolder_m, rHolder_r);
    }

    /* arguments for each branch should be constructed before making any
    branch call since, there is no guarantee of parallelising each branch 
    on this recursion level*/
    rHolder_l.holdArgs(instance_l, f_l, d_l);
    rHolder_m.holdArgs(instance_m, f_m, d_m);
    rHolder_r.holdArgs(instance_r, f_r, d_r);


    /*  The try_push_MT<>() method is aynchronous as long as an available
    processor is found, other wise, it explore branch in a senquential fashion*/
    branchHandler.try_push_MT<void>(foo, id, rHolder_l);
    branchHandler.try_push_MT<void>(foo, id, rHolder_m);
    

    /*  it makes no sense to call asynchronously the last branch, since it
    can be safely be executed sequentially, yet, if down the search tree,the owner thread of this search domain finds an available processor, 
    then this branch can be sent to another processor.*/
    branchHandler.forward<void>(foo, id, rHolder_r);

    
    // if virtual root allocated, memory should be freed
    if (dummyParent)
            delete dummyParent;
    
    return;
}
```

<br /> 
<br />

As seen above, the parallelisation of this algorithm is straightforward once the library is well managed. It is worth highlighting that input arguments must be ready before any branch calling, since down this branch, the ```DLB``` might try to call a sibling branch at this level, and it will receive only empty data. This might introduce some unnecessary memory utilization. Also, instantiating an input parameter will lead to passing outdated arguments to the functions that may be discarded just after invoking the function. This can be minimised by other available techniques discussed later.

Let's imagine that there are three functions ```foo1, foo2, foo3```, and they call each other recursively. Like the following snippet.

```cpp
void foo1(MyClass instance, float f, double d){
    /* local solution might be the size of any input argument*/
    if (localSolution < bestValueSoFar){
        bestValueSoFar = localSolution;
        return;
    }

    /* intance, f and d are used to create sub instances for the
        recursions, for left, middle and right branches.
    */

    foo1(instance_l, f_l, d_l);
    foo2(instance_m, f_m, d_m);
    foo3(instance_r, f_r, d_r);
    return;
}
```

Parallelising the program would not be any different than the version presented above. We should only pay attention to match the proper arguments to the corresponding function. Which just modifies the parallel version, excluding the comment. It will result as follows.
<br /> 
<br />

 ```cpp
std::mutex mtx;
auto &dlb = GemPBA::DLB_Handler::getInstance();
auto &branchHandler = GemPBA::BranchHandler::getInstance();
using HolderType = GemPBA::ResultHolder<void, MyClass, float, double>;

void foo1(int id, MyClass instance, float f, double d, void *parent = nullptr)

    if (localSolution < branchHandler.refValue()){
        std::scoped_lock<std::mutex> lock(mtx); 
        branchHandler.holdSolution(localSolution);
        branchHandler.updateRefValue(localSolution.size());
        return;
    }

    
    HolderType *dummyParent = nullptr;
    HolderType rHolder_l(dlb, id, parent);
    HolderType rHolder_m(dlb, id, parent);
    HolderType rHolder_r(dlb, id, parent);

    if (!parent){
        dummyParent = new HolderType(dlb, id);
        dlb.linkVirtualRoot(id, dummyParent, rHolder_l, rHolder_m, rHolder_r);
    }

    rHolder_l.holdArgs(instance_l, f_l, d_l);
    rHolder_m.holdArgs(instance_m, f_m, d_m);
    rHolder_r.holdArgs(instance_r, f_r, d_r);

    branchHandler.try_push_MT<void>(foo1, id, rHolder_l);
    branchHandler.try_push_MT<void>(foo2, id, rHolder_m);
    branchHandler.forward<void>(foo3, id, rHolder_r);

    if (dummyParent)
            delete dummyParent;
    
    return;
}
```


If there is no interest in parallelising a branch, it can simply be invoked as its sequential fashion, however the two new arguments must be considered. For instance, the last branch.

``` foo(id, instance_r, f_r, d_r, nullptr) ```

If this branch is to be run sequentially, then no instance of ```GemPBA::ResultHolder``` should be created for it.


Most of the time, the code of a branching algorithm is optimised to check if the branch is worth it to explore. What usually happens is that the instances to be passed are compared somehow against the best solution so far, and therefore it is possible to conclude that a branch is leading to a better or worse solution.

Then, an optimised version of our sequential algorithm would be as follows.



```cpp
void foo(MyClass instance, float f, double d){
    /* local solution might be the size of any input argument*/
    if (localSolution < bestValueSoFar){
        bestValueSoFar = localSolution;
        return;
    }

    /* intance, f and d are used to create sub instances for the
        recursions, for left, middle and right branches.
    */
    if( /* left branch leads to a better solution */)
        foo(instance_l, f_l, d_l);
    if( /* middle branch leads to a better solution */)
        foo(instance_m, f_m, d_m);
    if( /* right branch leads to a better solution */)
        foo(instance_r, f_r, d_r);

    return;
}
```

The branch checking in the above code does not change in its parallel version.


***GemPBA*** has a method to avoid instantiating input parameters for each branch, which is the ```bind_branch_checkIn``` method. This method guarantees to instantiate the input arguments just before using them, thus guaranteeing to use the most up-to-date values. This avoids sending useless data to processors just to be discarded by the algorithm in the first few lines.


Let's optimise our reference parallel code. 
<br /> 
<br />

 ```cpp
std::mutex mtx;
auto &dlb = GemPBA::DLB_Handler::getInstance();
auto &branchHandler = GemPBA::BranchHandler::getInstance();
using HolderType = GemPBA::ResultHolder<void, MyClass, float, double>;

void foo(int id, MyClass instance, float f, double d, void *parent = nullptr)

    if (localSolution < branchHandler.refValue()){
        std::scoped_lock<std::mutex> lock(mtx); 
        branchHandler.holdSolution(localSolution);
        branchHandler.updateRefValue(localSolution.size());
        
        return;
    }

    HolderType *dummyParent = nullptr;
    HolderType rHolder_l(dlb, id, parent);
    HolderType rHolder_m(dlb, id, parent);
    HolderType rHolder_r(dlb, id, parent);

    if (!parent){
        dummyParent = new HolderType(dlb, id);
        dlb.linkVirtualRoot(id, dummyParent, rHolder_l, rHolder_m, rHolder_r);
    }
    

    rHolder_l.bind_branch_checkIn([&]{
                                      /* arguments intantiation instance_l, f_l, d_l */
                                      if (/* left branch leads to a better solution */){
                                          rHolder_l.holdArgs(instance_l, f_l, d_l);
                                          return true;
                                      }
                                      else { return false;}                                          
                                  });

    rHolder_m.bind_branch_checkIn([&]{
                                      /* arguments intantiation instance_m, f_m, d_m */
                                      if (/* middle branch leads to a better solution */){
                                          rHolder_m.holdArgs(instance_m, f_m, d_m);
                                          return true;
                                      }
                                      else { return false;}                                          
                                  });

    rHolder_r.bind_branch_checkIn([&]{
                                      /* arguments intantiation instance_r, f_r, d_r */
                                      if (/* right branch leads to a better solution */){
                                          rHolder_r.holdArgs(instance_r, f_r, d_r);
                                          return true;
                                      }
                                      else { return false;}                                          
                                  });

    if (rHolder_l.evaluate_branch_checkIn()){
        branchHandler.try_push_MT<void>(foo, id, rHolder_l);
    }
    if (rHolder_m.evaluate_branch_checkIn()){
        branchHandler.try_push_MT<void>(foo, id, rHolder_m);
    }
    if (rHolder_r.evaluate_branch_checkIn()){
        branchHandler.forward<void>(foo, id, rHolder_r);
    }

    if (dummyParent)
            delete dummyParent;
    
    return;
}
```


As seen above, the ```HolderType``` instance wraps a lambda function, where the instantiation is delegated to it, and it must return a boolean. The purpose of this lambda function is to be able to tell the library if it is worth it to invoke a branch. Then, after the instantiation within the lambda function scope, this is verified. Since it is reading the most up-to-date values, now it is possible to use the custom verification to skip a branch or not. If it is worth it, then the ```HolderType``` instance holds the arguments as usual and the lambda function returns ```true```. If it is not worth it, there is no need to hold arguments, and the lambda function returns ```false```. Note that the lambda function captures by references, this is important if we implement this as a memory optimiser.


Since this lambda function wraps the branch verification condition, there is no need to write it again in the main scope, since it can be simply invoked by calling the method ```evaluate_branch_checkIn()``` as shown above.



This ```evaluate_branch_checkIn()``` method is also invoked internally in ***GemPBA*** so the ***DLB*** discards automatically a useless task, and skips to the next branch.

<br />

**Important**: In your main function (in which you initially call ```foo```), you need to instantiate an instance of ```BranchHandler``` and call ```branchHandler.initThreadPool(numOfThreads)``` with your desired number of threads.

<br /> 
<br />

## Multiprocessing

This is aimed for advanced users who might want to massively parallelise their applications. However, a user without parallelisation experience would be able to set this up eaisly. Before continue reading, we encourage the reader to learn and undertand the difference between a process and a thread.

Multiprocessing can also be used within a single machine depending on the user's interests.


***GemPBA*** uses *openmpi* to establish interprocess communication. In order to achieve better performance, a semi-centralised topology is the core of the communication. When launching your program with *openmpi*, the next command is used in the bash.

```
    mpirun -n 10 --oversubscribe a.out args...
```

The ```-n``` tells *openmpi* the number of processes to be spawned. The keyword. ```--oversubscribe``` is usually included if all launching processes will be executed within a machine that does not have at least the same number of physical cores. ```a.out``` is the executable and ```args...``` stands for any other argument that the application receives before running.


When executing the aforementioned command, it will launch *N* copies of the program that will do exactly the same. The trick is to make them communicate. For this, the semi-centralised topology is implemented. 

If the environment has been properly setup for multiprocessing, the center process (rank 0) will do the following steps:

- initialise:
  - BranchHandler();
  - MPIScheduler();
- read input data
- build arguments that will be passed to the function that we want to parallelise.
- serialise these arguments to create a string data buffer. ```std::string```
- invoke the ``` mpiScheduler.runCenter(buffer.data(),buffer.size()) ``` to passed the raw data buffer ```char[]```.


<br /> 

All other processes will do the following steps:

- initialise:
  - BranchHandler();
  - MPIScheduler();
- read input data
  - This is only necessary if all processes need a global copy of the initial data set. Otherwise it can be avoided.
- Initialise a buffer decoder. This instance will know the data types that the received buffer is going to be converted to.
- Initialise the ```resultFetcher```. This instance will fetch the result from the *branch handler* the process has ended all its tasks, and it will send it back to the corresponding process. This *result fetcher* is usually invoked when the center has notified termination to all the processes. However, it is aimed to be used for non-void functions, when this result must be returned to another process different from the center.
- invoke ```mpiScheduler.runNode(branchHandler, bufferDecoder, resultFetcher, serializer)```

<br /> 
These functions are synchronised such that no process ends until all of them have properly finished their duties.

After doing this, if the user wants to fetch the solution. It should invoke from the center process:

```cpp
std::string buffer = mpiScheduler.fetchSolution();
```

Which is the best solution according to the user's criteria, stored in a serial fashion. This buffer must be deserialised in order to have the actual solution.


Thus a way to set up the ```main.cpp``` would go like this.


```cpp

#include "MPI_Scheduler.hpp"
#include "BranchHandler.hpp"

auto deserializer = [](std::stringstream &ss, auto &...args) {
    // - serialize arguments into stream ss
    // - the user can implement its favourite serialization technique
};

auto serializer = [](auto &&...args) {
    return // std::string type
};


int main(){
    // parallel library local reference (BranchHandler is a singleton )
	auto &branchHandler = GemPBA::BranchHandler::getInstance(); 
    // Interprocess communication handler local reference (MPI_Scheduler is a singleton)
	auto &mpiScheduler = GemPBA::MPI_Scheduler::getInstance();
    /* gets the rank of the current process, so we know which process is 
        the center */
	int rank = mpiScheduler.rank_me();
    /* mpiScheduler is manually passed so the Branch Handler can invoke it
        to pass task to other processes    */
	branchHandler.passMPIScheduler(&mpiScheduler);

    // arguments initialization so each process knows their data types.
    MyClass instance;
    float f;
    double d;

    /*
    *   initial data can be read in here by all processes, yet only the rank 0
    *   would be able to use it
    */
    
    /* set if the algorithm might be seen as a maximisation or minimsation problem
    the two available keyworsd are: minimise or maximise, not case sensitive.
    If maximisation, reference value or best value so far is INT_MIN
    If minimisation, reference value or best value so far is INT_MAX
    
    By default, GemPBA maximises, thus the below line is optional for maximisation*/
	branchHandler.setRefValStrategyLookup("minimise");

    // Here we set the best value so far if known somehow, optional
    branchHandler.setRefValue(/* some integer*/); 



    /* this necessary condition statement guarantees that each process will do its corresponding part
        in the execution, if this condition is not done, all processes will invoke the same methods,
        which will lead to unknown behaviour, and probably a simple deadlock in the most optimistic
        scenario */
    if (rank == 0) {
        //   or initial data can be read in here by only the rank 0

        // input arguments are serialized and converted to a std::string
        std::string buffer = serializer(instance, f, d);
		// center process receive only the raw buffer and its size
		mpiScheduler.runCenter(buffer.data(), buffer.size());
	}
	else
	{
		/*	worker process
			main thread will take care of Inter-process communication (IPC), dedicated core
			numThreads could be the number of physical cores managed by this process - 1
		*/
        // only workers need to initialized a pool
		branchHandler.initThreadPool(numThreads);
        
        /*  Workers need to know the type of the incoming buffers, for this we invoke the method
            constructBufferDecoder<returnType, Type1, Type2>(foo, deserializer)
            Note that the user only must pass the original argument types in the function ingoring 
            the thread ID and parent arguments. As seen above, this method receives the target function
            and a instance of the deserializer such that the library can decode this buffer and
            construct the arguments from the buffer and then forward them to the function.
        */
		auto bufferDecoder = branchHandler.constructBufferDecoder<void, MyClass, float, double>(foo, deserializer);
        /* Workers are constantly searching for a solution, for which some processes might attain it
            and some others might not. This solution is also constantly replaced for a most promising one
            but it is sent to the center process only when the execution is terminated.
            
            This method is in charge of directly fetching the result from the Branch Handler as a
            std::pair<int, std::string> */
		auto resultFetcher = branchHandler.constructResultFetcher();
        // Finally these instances are passed through the runNode(...) method
		mpiScheduler.runNode(branchHandler, bufferDecoder, resultFetcher, serializer);
	}
    /* this is a barrier just in case the user want to ensure that all processes reach this part 
        before continuing */
	mpiScheduler.barrier();

    /*
        The Branch Handler and the Mpi Scheduler offer certain information that can be fetched 
        that might be relevant for the user analysis of the parallelization.
        BranchHandler:  
            -   Idle time measured by thread pool
            -   Total number of task received by the thread pool
        Similarly

        MpiScheduler:
            -   Number of sent tasks by the process
            -   Number of received tasks by the process
        
        Since most of this information is private to each process, it can be gather invoking either
        the gather or allgather method in MpiScheduler, which follow the same rules of the MPI methods.

        gather if only one process will receive the data
        allgather if all process need the data
    */

    // fetch the total number of participatin processes
    int world_size = mpiScheduler.getWorldSize();
    /* creates a vector to fetch the idle time of each process
        it can also be a simple array 
        double idleTime[worldSize];
    */
    std::vector<double> idleTime(world_size);
    // actual idle time private to each process
    double idl_tm = 0;  

    // rank 0 does not instantiate a thread pool
    if (rank != 0) { 
		idl_tm = branchHandler.getPoolIdleTime();
    }

    /* a pointer to the buffer is passed
        and idl_tm is copied to the buffer in its corresponding rank. Ie.
        if idl_tm is in process rank = 3, then it is going to be copied in all
        processes in the idleTime vector at position 3.
    */
    mpiScheduler.allgather(idleTime.data(), &idl_tm, MPI_DOUBLE);

    /* The user might want to print statistic or fetch the final solution, which is now stored 
        in the center process, another if statement is required.    */

    if (rank == 0) { 
		mpiScheduler.printStats();
		std::string buffer = mpiScheduler.fetchSolution(); // returns a stringstream

        /* Solution data type must coincide with the data type passed through
        the method BranchHandler::holdSolution(...) */
        SolType solution;
        std::stringstream ss;
        ss << buffer;   // buffer passed to a stringstream
		deserializer(ss, solution);

        // do something else with the solution
    }        
}
```

As seen above, it is pretty simple to set the multi-processing environment since the difficult
synchronisation part is delagated to the ***GemPBA***. 


If we compare the aforementioned code vs a sequential version, we could notice that there are 
only a few modification. As shown below excluding the explaning comments and statistic information.

- #### Sequential main.cpp

```cpp

int main(){
    MyClass instance;
    float f;
    double d;

    /*
    *   initial data reading
    */

    foo(instance, f,d);
    // do something else with the solution
}
```

<br /> 
<br />

- #### Multiprocessing main.cpp

```cpp
#include "BranchHandler.hpp"
#include "MPI_Scheduler.hpp"

auto deserializer = [](std::stringstream &ss, auto &...args){/*procedure*/};

auto serializer = [](auto &&...args){
    /*procedure*/
    return /* std::string type */ 
    };

int main(){
    auto &branchHandler = GemPBA::BranchHandler::getInstance(); 
    auto &mpiScheduler = GemPBA::MPI_Scheduler::getInstance();
    int rank = mpiScheduler.rank_me();
    branchHandler.passMPIScheduler(&mpiScheduler);

    MyClass instance;
    float f;
    double d;

    /*
    *   initial data reading
    */
    
    branchHandler.setRefValStrategyLookup("minimise");
    branchHandler.setRefValue(/* some integer*/); 
    if (rank == 0) {
        std::string buffer = serializer(instance, f, d);
        mpiScheduler.runCenter(buffer.data(), buffer.size());
    }
    else {
        branchHandler.initThreadPool(numThreads);
        auto bufferDecoder = branchHandler.constructBufferDecoder<void, MyClass, float, double>(foo, deserializer);
        auto resultFetcher = branchHandler.constructResultFetcher();
        mpiScheduler.runNode(branchHandler, bufferDecoder, resultFetcher, serializer);
    }
    mpiScheduler.barrier();

    if (rank == 0) { 
        std::string buffer = mpiScheduler.fetchSolution(); 
        SolType solution;
        std::stringstream ss;
        ss << buffer;
        deserializer(ss, solution);

        // do something with "solution"
    }
        
}
```
<br /> 
<br />
<br /> 
<br />

Hence, the code modifications to convert the Multithreading function to Multiprocessing are minors. As presented below.

<br /> 


 ```cpp
std::mutex mtx;
auto &dlb = GemPBA::DLB_Handler::getInstance();
auto &branchHandler = GemPBA::BranchHandler::getInstance();
using HolderType = GemPBA::ResultHolder<void, MyClass, float, double>;

void foo(int id, MyClass instance, float f, double d, void *parent = nullptr)

    if (localSolution < branchHandler.refValue()){
        std::scoped_lock<std::mutex> lock(mtx); 
        branchHandler.holdSolution(localSolution.size(), localSolution, serializer);
        branchHandler.updateRefValue(localSolution.size());
        
        return;
    }

    HolderType *dummyParent = nullptr;
    HolderType rHolder_l(dlb, id, parent);
    HolderType rHolder_m(dlb, id, parent);
    HolderType rHolder_r(dlb, id, parent);

    if (!parent){
        dummyParent = new HolderType(dlb, id);
        dlb.linkVirtualRoot(id, dummyParent, rHolder_l, rHolder_m, rHolder_r);
    }
    

    rHolder_l.bind_branch_checkIn([&]{
                                      /* arguments intantiation instance_l, f_l, d_l */
                                      if (/* left branch leads to a better solution */){
                                          rHolder_l.holdArgs(instance_l, f_l, d_l);
                                          return true;
                                      }
                                      else { return false;}                                          
                                  });

    rHolder_m.bind_branch_checkIn([&]{
                                      /* arguments intantiation instance_m, f_m, d_m */
                                      if (/* middle branch leads to a better solution */){
                                          rHolder_m.holdArgs(instance_m, f_m, d_m);
                                          return true;
                                      }
                                      else { return false;}                                          
                                  });

    rHolder_r.bind_branch_checkIn([&]{
                                      /* arguments intantiation instance_r, f_r, d_r */
                                      if (/* right branch leads to a better solution */){
                                          rHolder_r.holdArgs(instance_r, f_r, d_r);
                                          return true;
                                      }
                                      else { return false;}                                          
                                  });

    if (rHolder_l.evaluate_branch_checkIn()){
        branchHandler.try_push_MP<void>(foo, id, rHolder_l, serializer);
    }
    if (rHolder_m.evaluate_branch_checkIn()){
        branchHandler.try_push_MP<void>(foo, id, rHolder_m, serializer);
    }
    if (rHolder_r.evaluate_branch_checkIn()){
        branchHandler.forward<void>(foo, id, rHolder_r);
    }

    if (dummyParent)
            delete dummyParent;
    
    return;
}
```


As you may have noticed, within the termination condition, the solution is captured in a serial fashion. This solution is stored in a ```std::pair<int, string>``` data type, where the integer is the reference value associated with the solution. This reference value is used by the ```MpiScheduler``` when retrieving the global solution by the means of comparing the best value so far against the solutions attained by each process.

As for the parallel calls, the method ```BranchHandler::try_push_MT(...)``` is changed to ```BranchHandler::try_push_MP(..., serializer)```.

This ```MT``` suffix stands for Multithreading whereas the ```MP``` suffix stands for Multiprocessing.

Internally, ```try_push_MP``` will invoke the ```MpiScheduler``` to ascertain for any available processor, if none, then it will invoke ```try_push_MT``` for a local thread.

```try_push_MT``` and ```try_push_MP``` return ```true``` if the asynchronous operation was succeeding, otherwise, it will continue sequentially and when it returns, it will be ```false```.

**Hint**: Depending on your project's structure, additional imports might be required because of hidden dependencies inside the *GemPBA* library.

### Multiprocessing with centralized scheduler

The *GemPBA* library includes a centralized scheduler as well. The usage is almost the same as with the semicentralized scheduler. It can be activated by using the compile flag ```-D SCHEDULER_CENTRALIZED```.

Additionally one need to include the centralized scheduler instead of the semicentralized scheduler:

```cpp

#include "MPI_Scheduler_Centralized.hpp"
```
