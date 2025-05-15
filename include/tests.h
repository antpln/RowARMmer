#ifndef TESTS_H
#define TESTS_H

#include <utils.h>
#include <memory.h>
#include <hammer.h>

void va_to_pa_test(uint64_t* buffer, size_t size, pfn_va_t *map, size_t n);
void instructions_timing_test(addr_tuple addr, int iter);



#endif // TESTS_H