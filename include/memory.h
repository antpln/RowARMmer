#ifndef MEMORY_H
#define MEMORY_H
#define GB(x) ((uint64_t)(x) * 1024 * 1024 * 1024)
#define MB(x) ((uint64_t)(x) * 1024 * 1024)
#define KB(x) ((uint64_t)(x) * 1024)

#define BUFFER_SIZE
#include <include/utils.h>


uint64_t get_row_bits(addr_tuple addr);
addr_tuple next_row(addr_tuple);
addr_tuple prev_row(addr_tuple);
addr_tuple is_possibly_same_row(addr_tuple, addr_tuple);


#endif // MEMORY_H