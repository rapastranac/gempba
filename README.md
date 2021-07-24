# Generic Massive Parallelisation of Branching Algorithms


 This tool will help you parallelise almost any branching algorithm that seemed initially impossible or super complex to do. Please refer to ``` paper reference ```, for performance report.

 **GemPBA** will allow you to perform apply parallelism using a multithreading environment o multiprocessing. It also contains a robust *Dynamic Load Balancing* (DLB) that decreseas significantly the CPU idle time, which also increases performance due to the reduction of parallel calls in branching algorithms.

**GemPBA** has three main modules that helps you undertands easily its applications.

 - *BranchHandler:*

    This module is in charge of handling tasks among the processors, whether multithreading or multiprocessing is being used. It manages a thread pool, for which the user can allocate the number of threads he needs, yet it is recommended to create a allocate them accordingly to the number of physical cores in your machine.

    It essentially finds the first available processor in the thread pool, or in a remote participating machine.

 - *ResultHolder:*
 
    In order to keep trak of the arguments such that they can be properly managed by the library. The function signature must be slightly modified to include two additional parameter, one as the first, and other as the last one. This modification will be explained later.

    ResultHolder
 
 - *DLB_Handler:*

    Since recursive functions create a stack, the *DLB* instance has access to all the generated tasks at each level of it. Thus, when creating an instance of the Result



## Multithreading

This is the easiest environment to setup, which is in turn the fastest to implement. In general the user must modify only the main functions to be parrallised. If the algorithm uses more than a single function recursion, this can also be parallelized. 

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
```cpp
void foo(int tid, MyClass instance, float f, double d, void *parent);
```
 Where ```tid``` stands for thread ID and ```parent``` is designed to used the *Novel Dynamic Load Balancing*.

 These additional arguments are to be used by the library only, yet the user could also use them to track like threads utilization and other scenarios that it might find applicable.


 Thus, the parallelised version of the code will be like as follows.

 ```cpp
std::mutex mtx;
auto &dlb = GemPBA::DLB_Handler::getInstance();
auto &branchHandler = GemPBA::BranchHandler::getInstance();
using HType = GemPBA::ResultHolder<void, MyClass, float, double>;

void foo(int tid, MyClass instance, float f, double d, void *parent = nullptr)

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
        if the as long as holdSolution() is not invoked.*/
        branchHandler.updateRefValue(localSolution.size());
        
        return;
    }

    /* intance, f and d are used to create sub instances for the
        recursions, for left, middle and right branches.
    */
    

    HType *dummyParent = nullptr;   // in the case parent is nullptr

    /* The dynamic load balancing uses tracks the search tree using these
    temporary arguments holders.*/

    HType rHolder_l(dlb, tid, parent);
    HType rHolder_m(dlb, tid, parent);
    HType rHolder_r(dlb, tid, parent);

    /*  if parent is nullptr, then a virtual root is should be created
    such that branches within this scope can be accessed from below */
    if (!parent){
        dummyParent = new HolderType(dlb, id);
        dlb.linkVirtualRoot(id, dummyParent, rHolder_l, rHolder_m,rHolder_r);
    }

    /* arguments for each branch should be constructed before making any
    branch call since, there is no guarantee of parallelising each branch 
    on this recursion level*/
    rHolder_l.holdArgs(instance_l, f_l, d_l);
    rHolder_m.holdArgs(instance_m, f_m, d_m);
    rHolder_r.holdArgs(instance_r, f_r, d_r);


    /*  The try_push_MT<>() method is aynchronous as long as an available
    processor is found, other wise, it explore branch in a senquential fashion*/
    branchHandler.try_push_MT<void>(foo, tid, rHolder_l);
    branchHandler.try_push_MT<void>(foo, tid, rHolder_m);
    

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

As seen above, the parallelisation of this algorithm is straightfoward once the library is well managed. It is worth highlighting that input arguments must be ready before any branch calling, since down this branch, the ```DLB``` migth try to call a sibling branch at this level, and it will receive only empty data. This might introduce some unecessary memory utilization. Also, instantiating an input parameter will lead to pass outdated arguments to the functions that may be discarded just after invoking the function. This can be minimised by other available tecnique discussed later.

Let's imagine that there are three functions ```foo1, foo2, foo3```, and they call each other recursively. Like the following snipped.

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

Even if their function signature are different, parallelising the program would not be any different than the version presented above. We should only pay attention to match the proper arguments to the corresponding function. Which just modifying the parallel version, excluding the comment. It will result as follows.


 ```cpp
std::mutex mtx;
auto &dlb = GemPBA::DLB_Handler::getInstance();
auto &branchHandler = GemPBA::BranchHandler::getInstance();
using HType = GemPBA::ResultHolder<void, MyClass, float, double>;

void foo1(int tid, MyClass instance, float f, double d, void *parent = nullptr)

    if (localSolution < branchHandler.refValue()){
        std::scoped_lock<std::mutex> lock(mtx); 
        branchHandler.holdSolution(localSolution);
        branchHandler.updateRefValue(localSolution.size());
        return;
    }

    
    HType *dummyParent = nullptr;
    HType rHolder_l(dlb, tid, parent);
    HType rHolder_m(dlb, tid, parent);
    HType rHolder_r(dlb, tid, parent);

    if (!parent){
        dummyParent = new HolderType(dlb, id);
        dlb.linkVirtualRoot(id, dummyParent, rHolder_l, rHolder_m,rHolder_r);
    }

    rHolder_l.holdArgs(instance_l, f_l, d_l);
    rHolder_m.holdArgs(instance_m, f_m, d_m);
    rHolder_r.holdArgs(instance_r, f_r, d_r);

    branchHandler.try_push_MT<void>(foo1, tid, rHolder_l);
    branchHandler.try_push_MT<void>(foo2, tid, rHolder_m);
    branchHandler.forward<void>(foo3, id, rHolder_r);

    if (dummyParent)
            delete dummyParent;
    
    return;
}
```


If there is no interest in parallelising a branch, it can simply be invoked as its sequential fashion, however the two new arguments must be considered. For instance, the last branch.

``` foo(tid, instance_r, f_r, d_r, nullptr) ```

If this branch is to be run sequentially, then no instance of ```GemPBA::ResultHolder``` should be created for it.


Most of the time, the code of a branching algorithm is optimised to check if the branch is worth it to explore. What it usually happens is that the instances to be passed are compared somehow against the best solution so far, and therefore it is possible to conclude that a branch is leading to a best or worse solution.

Then, an optimised version of the our sequential algorithm would be as follows.



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
    if( /* leads to a better solution */)
        foo(instance_l, f_l, d_l);
    if( /* leads to a better solution */)
        foo(instance_m, f_m, d_m);
    if( /* leads to a better solution */)
        foo(instance_r, f_r, d_r);

    return;
}
```

The above code does not change in its parallel version, the branch checking will be exactly the same.


***GemPBA*** has a method to avoid instantiating input parameter for each branch, which is the ```bind_branch_checkIn``` method. This method, guarantees to instantiate the input arguments just before using them, thus guaranteeing to use the most up-to-date date. This avoids sending useless data to processors just to be discarded by the algorithm in the first few lines.


Let's optimise our reference parallel code. 



 ```cpp
std::mutex mtx;
auto &dlb = GemPBA::DLB_Handler::getInstance();
auto &branchHandler = GemPBA::BranchHandler::getInstance();
using HType = GemPBA::ResultHolder<void, MyClass, float, double>;

void foo(int tid, MyClass instance, float f, double d, void *parent = nullptr)

    if (localSolution < branchHandler.refValue()){
        std::scoped_lock<std::mutex> lock(mtx); 
        branchHandler.holdSolution(localSolution);
        branchHandler.updateRefValue(localSolution.size());
        
        return;
    }

    HType *dummyParent = nullptr;
    HType rHolder_l(dlb, tid, parent);
    HType rHolder_m(dlb, tid, parent);
    HType rHolder_r(dlb, tid, parent);

    if (!parent){
        dummyParent = new HolderType(dlb, id);
        dlb.linkVirtualRoot(id, dummyParent, rHolder_l, rHolder_m,rHolder_r);
    }
    

    rHolder_l.bind_branch_checkIn([&]{
                                      /* arguments intantiation instance_l, f_l, d_l */
                                      if (/* leads to a better solution */){
                                          rHolder_l.holdArgs(instance_l, f_l, d_l);
                                          return true;
                                      }
                                      else { return false;}                                          
                                  });

    rHolder_m.bind_branch_checkIn([&]{
                                      /* arguments intantiation instance_m, f_m, d_m */
                                      if (/* leads to a better solution */){
                                          rHolder_m.holdArgs(instance_m, f_m, d_m);
                                          return true;
                                      }
                                      else { return false;}                                          
                                  });

    rHolder_r.bind_branch_checkIn([&]{
                                      /* arguments intantiation instance_r, f_r, d_r */
                                      if (/* leads to a better solution */){
                                          rHolder_r.holdArgs(instance_r, f_r, d_r);
                                          return true;
                                      }
                                      else { return false;}                                          
                                  });

    if (rHolder_l.evaluate_branch_checkIn()){
        branchHandler.try_push_MT<void>(foo, tid, rHolder_l);
    }
    if (rHolder_m.evaluate_branch_checkIn()){
        branchHandler.try_push_MT<void>(foo, tid, rHolder_m);
    }
    if (rHolder_r.evaluate_branch_checkIn()){
        branchHandler.forward<void>(foo, id, rHolder_r);
    }

    if (dummyParent)
            delete dummyParent;
    
    return;
}
```


As seen above, the ```HType``` instance wraps a lambda function, where the instantion is delegated to it, and it must return a boolean. The purpose of this lambda function is to be able to tell the library is it is worth it to invoke a branch. Then, after the instantiation withing the lambda function scope, this is verified. Since it is reading the most up-to-date values, now it is possible to use the custom verification to discard skip a branch or not. If it is worth it, then the the ```HType``` instance holde the arguments as usual and the lambda function returns ```true```. If it is not worth it, there is no need to holde arguments, and the lambda function returns ```false```.


Since this lambda function wraps the branch verification condition, there is no need to write it again in the main scope, since it can be simply invoked by calling the method ```evaluate_branch_checkIn()``` as shown above.



This ```evaluate_branch_checkIn()``` is also evaluated internally in ***GemPBA*** so the ***DLB*** discards automatically a useless task, and skips to next branch.




## Multiprocessing

This is aimed for advance users who want to massively parallelise their applications. However, a user without parallelisation experience would be able to set this up eaisly. Before continue reading, we encourage the reader to learn and undertand the difference between a process and a thread.

```
    TODO ..
```


***GenPMBA*** uses *openmpi* to establish interprocess communication. In order to achieve better performance, a semi-centralised topology is the core of the communication. When launching your program with *openmpi*, the next command is used in the bash.

```
    mpirun -n 10 --oversubscribe a.out args...
```

The ```-n``` tell to *openmpi* the number of processes to be spawned. The keyword. ```--oversubscribe``` is usually included if all launching processes will be executed within a machine that does not have at least the same number of physical cores. ```a.out``` is the executable and ```args...``` stands for any other argument that the application receives before running.


When executing the aforementione command, it will launch *N* copies of the program that will do exactly the same. The trick is to make them communicate. For this, the semi-centralised topology is implemented. 

If the environment has been properly setup for multiprocessing, the center process (rank 0) will do the following steps:

- initialise:
    * BranchHandler();
    * MPIScheduler();
- read input data
- build arguments that will be passed to the function that we want to parallelise.
- serialise these arguments to create a string data buffer. ```std::string```
- invoke the ``` mpiScheduler.runCenter(buffer.data(),buffer.size()) ``` to passed the raw data buffe ```char[]```.




All other processes will do the following steps:

- initialise:
    * BranchHandler();
    * MPIScheduler();
- read input data
    * this is only necessy if all processes need a global copy of the initial data set. Othesewise it can be avoided.
- Initialise a buffer decoder. This instance will know the data types that the received buffer is going to be converted to.
- Initialise the ```resultFetcher```. This instance will fetch the result from the *branch handler* the process has ended all its tasks, and it will send it back to the corresponding process. This *result fetcher* is usually invoked when the center has notified termination to all the processes. However, it is aimed to be used for non-void functions, when this result must be returned to another process different than center.
- invoke ```mpiScheduler.runNode(branchHandler, bufferDecoder, resultFetcher, serializer)```


These functions are synchronised such that no process ends until all of them have properly finished their duties.

After doing this, if the user wants to fetch the solution. It should invoke from the center process:

```cpp
std::string buffer = mpiScheduler.fetchSolution();
```

Which is the best solution according to the user's criteria, stored in a serial fashion. This buffer must be deserialised in order to have the actual solution.
