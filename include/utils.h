#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define DEBUG 1

typedef struct
{
    uint64_t *v_addr;
    uint64_t p_addr;
} addr_tuple;

typedef struct
{
    uint64_t pfn;
    uint64_t va;
} pfn_va_t;

uint64_t get_phys_addr(uint64_t v_addr);
addr_tuple gen_addr_tuple(uint64_t *v_addr);
int hweight64(uint64_t x);
int parity64(uint64_t x);
addr_tuple gen_random_addr(uint64_t *buffer, size_t size);
pfn_va_t *build_pfn_map(void *buf, size_t bytes, size_t *out_n);
void *pa_to_va(uint64_t pa, pfn_va_t *map, size_t n);

static inline uint64_t read_pmccntr(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, PMCCNTR_EL0" : "=r"(v));
    return v;
}

#endif // UTILS_H