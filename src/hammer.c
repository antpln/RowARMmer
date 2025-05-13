#include <include/utils.h>
#include <include/memory.h>
#include <assert.h>

uint64_t hammer_single(addr_tuple addr, int iter, bool timing){
    uint64_t start, end;
    int i;
    char *v_addr = addr.v_addr;
    volatile char val;


    if (timing) {
        start = read_pmccntr();
    }
    for(int i = 0; i < iter; i++) {
        asm volatile("DC CIVAC, %0" : : "r"(v_addr));
        asm volatile("DSB SY");
        asm volatile("ISB SY");
        val = *v_addr;
    }
    if (timing) {
        end = read_pmccntr();
        return end - start;
    }
    return 0;
}

uint64_t hammer_double(addr_tuple addr_1, addr_tuple addr_2, int iter, bool timing){
    uint64_t start, end;
    int i;
    char *v_addr_1 = addr_1.v_addr;
    char *v_addr_2 = addr_2.v_addr;
    volatile char val;

    if (timing) {
        start = read_pmccntr();
    }
    for(int i = 0; i < iter; i++) {
        asm volatile("DC CIVAC, %0" : : "r"(v_addr_1));
        asm volatile("DC CIVAC, %0" : : "r"(v_addr_2));
        asm volatile("DSB SY");
        asm volatile("ISB SY");
        val = *v_addr_1;
        val = *v_addr_2;
    }
    if (timing) {
        end = read_pmccntr();
        return end - start;
    }
    return 0;
}

uint64_t pattern_single(addr_tuple addr, int iter, bool timing){
    return hammer_single(addr, iter, timing);
}

uint64_t pattern_single_decoy(addr_tuple addr, char* buffer, size_t size, int iter, bool timing){
    addr_tuple decoy = addr;
    while(possibly_same_row(addr, decoy)){
        decoy = gen_random_addr(buffer, size);
    }

    return hammer_double(addr, decoy, iter, timing);
}

uint64_t pattern_quad(addr_tuple addr, char* buffer, size_t size, int iter, bool timing){
    addr_tuple addr_f_minus = prev_row(prev_row(addr));
    addr_tuple addr_f_plus = next_row(next_row(addr));
    assert(addr_f_minus.v_addr >= buffer && addr_f_plus.v_addr < buffer + size);
    return hammer_double(addr_f_minus, addr_f_plus, iter, timing);
}

