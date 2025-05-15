#ifndef MEMORY_H
#define MEMORY_H
#define GB(x) ((uint64_t)(x) * 1024 * 1024 * 1024)
#define MB(x) ((uint64_t)(x) * 1024 * 1024)
#define KB(x) ((uint64_t)(x) * 1024)

#define ROW_SIZE (2048)


#include "utils.h"


uint64_t get_row_bits(addr_tuple addr);
addr_tuple next_row(addr_tuple, pfn_va_t *map, size_t pfn_va_len);
addr_tuple prev_row(addr_tuple, pfn_va_t *map, size_t pfn_va_len);
bool is_possibly_same_row(addr_tuple, addr_tuple);


#endif // MEMORY_H