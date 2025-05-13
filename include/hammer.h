#ifndef HAMMER_H
#define HAMMER_H

#include "include/utils.h"

enum PATTERN {
    PATTERN_SINGLE,
    PATTERN_SINGLE_DECOY,
    PATTERN_QUAD
};

uint64_t hammer_single(addr_tuple addr, int iter, bool timing);
uint64_t hammer_double(addr_tuple addr_1, addr_tuple addr_2, int iter, bool timing);

#endif // HAMMER_H