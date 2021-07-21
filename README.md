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

This is easiest environment to setup, which is in turn the fastest to implement.



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
