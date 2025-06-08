#ifndef GEMPBA_GEMPBA_UTILS_H
#define GEMPBA_GEMPBA_UTILS_H

namespace gempba {

    enum lookup_strategy {
        MAXIMISE, MINIMISE
    };

    enum load_balancing_strategy {
        QUASI_HORIZONTAL, // Our Novel Dynamic Load Balancer
        WORK_STEALING
    };
}

#endif //GEMPBA_GEMPBA_UTILS_H
