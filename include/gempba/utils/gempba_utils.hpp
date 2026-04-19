#ifndef GEMPBA_GEMPBA_UTILS_H
#define GEMPBA_GEMPBA_UTILS_H

namespace gempba {

    enum goal { MAXIMISE, MINIMISE };

    enum balancing_policy {
        QUASI_HORIZONTAL, // Our Novel Dynamic Load Balancer
        WORK_STEALING
    };
} // namespace gempba

#endif // GEMPBA_GEMPBA_UTILS_H
