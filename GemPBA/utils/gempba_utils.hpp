#ifndef GEMPBA_GEMPBA_UTILS_H
#define GEMPBA_GEMPBA_UTILS_H

namespace gempba {
    
    enum LookupStrategy {
        MAXIMISE, MINIMISE
    };
    enum LoadBalancingStrategy {
        QUASI_HORIZONTAL, // Our Novel Dynamic Load Balancer
        WORK_STEALING
    };
}

#endif //GEMPBA_GEMPBA_UTILS_H
