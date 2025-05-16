#ifndef HAMMER_H
#define HAMMER_H

#include "utils.h"

typedef enum {
    STANDARD,
    HUGEPAGE_2MB,
    HUGEPAGE_1GB,
} buffer_type;


typedef enum  {
    PATTERN_SINGLE,
    PATTERN_SINGLE_DECOY,
    PATTERN_QUAD
} hammer_pattern;

typedef struct {
    bool direction;
    addr_tuple addr;
    uint64_t expected;
    uint64_t actual;
    uint8_t bit_pos;
} bitflip;

typedef uint64_t (*pattern_func)(uint64_t);

uint64_t hammer_single(addr_tuple addr, int iter, bool timing);
uint64_t hammer_double(addr_tuple addr_1, addr_tuple addr_2, int iter, bool timing);
uint64_t pattern_single(addr_tuple addr, int iter, bool timing);
uint64_t pattern_single_decoy(addr_tuple addr, uint64_t* buffer, size_t size, int iter, bool timing);
uint64_t pattern_quad(addr_tuple addr, uint64_t* buffer, size_t size, int iter, bool timing, pfn_va_t* map, size_t pfn_va_len);
bitflip* detect_bitflips(uint64_t* buffer, size_t size, pattern_func pattern);
void bitflip_test(size_t buffer_size, buffer_type b_type, pattern_func pattern, hammer_pattern hammer_pattern, bool timing, int iter, int hammer_iter, char *output_file, bool uncachable);
uint64_t *buffer_init(size_t size, buffer_type type, pattern_func pattern);

#endif // HAMMER_H