/*
 * Author:  David Robert Nadeau
 * Site:    http://NadeauSoftware.com/
 * License: Creative Commons Attribution 3.0 Unported License
 *          http://creativecommons.org/licenses/by/3.0/deed.en_US
 */

#pragma once
#ifndef MPI_SCHEDULER_CENTRALIZED_UTILS_HPP
#define MPI_SCHEDULER_CENTRALIZED_UTILS_HPP

#include <cstddef>
#include <utility>

#ifdef BRANCH_AND_BOUND

    #ifdef OBJECTIVE_DOUBLE
        #define OBJECTIVE_TYPE double
        #pragma message("objective type: double")
    
    #else
        #define OBJECTIVE_TYPE int
        #pragma message("objective type: int")

    #endif

    OBJECTIVE_TYPE getObjectiveValue(char* archiveString);

#else

    /**
     * Return number of bits that are set to 1
     */
    int getNbSetBits(char c);

    int getNbSetBits(std::pair<char *, int> task);

#endif

class TaskComparator {
public:
    bool operator()(std::pair<char *, int> t1, std::pair<char *, int> t2);
};


/**
 * Returns the peak (maximum so far) resident set size (physical
 * memory use) measured in bytes, or zero if the value cannot be
 * determined on this OS.
 */
size_t getPeakRSS();
/**
 * Returns the current resident set size (physical memory use) measured
 * in bytes, or zero if the value cannot be determined on this OS.
 */
size_t getCurrentRSS();

#endif // MPI_SCHEDULER_CENTRALIZED_UTILS_HPP
