#ifndef MEMORY_H
#define MEMORY_H
#define GB(x) ((uint64_t)(x) * 1024 * 1024 * 1024)
#define MB(x) ((uint64_t)(x) * 1024 * 1024)
#define KB(x) ((uint64_t)(x) * 1024)


#include "utils.h"


uint64_t get_row_bits(addr_tuple addr);
addr_tuple next_row_deterministic(addr_tuple, pfn_va_t *map, size_t pfn_va_len);
addr_tuple next_row(addr_tuple, pfn_va_t *map, size_t pfn_va_len);
addr_tuple prev_row_deterministic(addr_tuple, pfn_va_t *map, size_t pfn_va_len);
addr_tuple prev_row(addr_tuple, pfn_va_t *map, size_t pfn_va_len);
bool is_possibly_same_row(addr_tuple, addr_tuple);
uint64_t get_bank(uint64_t addr);
uint64_t get_column(uint64_t addr);
uint64_t get_row(uint64_t addr);
uint64_t get_channel(uint64_t addr);
uint64_t make_phys_addr(uint64_t row, uint64_t bank, uint64_t column);
void print_location(addr_tuple addr);


#endif // MEMORY_H