#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
	char* 		v_addr; 
	uint64_t 	p_addr;
} addr_tuple;

uint64_t get_phys_addr(uint64_t v_addr);
addr_tuple gen_addr_tuple(char *v_addr);
static inline uint64_t read_pmccntr(void);
static inline int hweight64(uint64_t x);
static inline int parity64(uint64_t x);
addr_tuple gen_random_addr(char *buffer, size_t size);



#endif // UTILS_H