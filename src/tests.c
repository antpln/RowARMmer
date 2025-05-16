#define _POSIX_C_SOURCE 199309L
#define MEASUREMENTS 1000000
#include <tests.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <ptedit_header.h>
#include <hammer.h>
#include <sys/mman.h>
#include <linux/mman.h>

/* convenience bits */
#define PMCR_E (1u << 0) /* Enable all counters              */
#define PMCR_P (1u << 1) /* Reset cycle counter              */
#define PMCR_C (1u << 2) /* Reset event counters             */

static inline void pmu_enable_counter(int idx)
{
    __asm__ volatile("msr pmcntenset_el0, %0" ::"r"(1u << idx));
}

static inline void pmu_setup_event(int idx, uint32_t event)
{
    __asm__ volatile("msr pmselr_el0, %0" ::"r"(idx & 0x1F));
    __asm__ volatile("isb");
    __asm__ volatile("msr pmxevtyper_el0, %0" ::"r"(event & 0x3FF));
    __asm__ volatile("msr pmxevcntr_el0,  %x0" ::"r"(0)); /* clear */
    pmu_enable_counter(idx);
}

static inline void pmu_global_start(void)
{
    uint32_t v = PMCR_E | PMCR_P | PMCR_C;
    __asm__ volatile("msr pmcr_el0, %0" ::"r"(v));
    __asm__ volatile("isb");
}

/* ------------------------------------------------------------------ */

static inline void init_cache_counters(void)
{
    pmu_global_start(); /* reset + enable */

    pmu_setup_event(0, 0x03); /* L1D refill */
    pmu_setup_event(1, 0x04); /* L1D access */
    pmu_setup_event(2, 0x17); /* L2D refill */
    pmu_setup_event(3, 0x16); /* L2D access */
    pmu_setup_event(4, 0x52); /* L2D_CACHE_REFILL_LD */
    pmu_setup_event(5, 0x53); /* L2D_CACHE_REFILL_ST */
    pmu_setup_event(6, 0x66); /* MEM_ACCESS_LD */
    pmu_setup_event(7, 0x67); /* MEM_ACCESS_ST */
}

static inline uint64_t read_event_counter(int idx)
{
    __asm__ volatile("msr pmselr_el0, %0" ::"r"(idx & 0x1F));
    __asm__ volatile("isb");
    uint64_t val;
    __asm__ volatile("mrs %0, pmxevcntr_el0" : "=r"(val));
    return val;
}

static inline uint64_t read_l1d_refill_counter() { return read_event_counter(0); }
static inline uint64_t read_l1d_access_counter() { return read_event_counter(1); }
static inline uint64_t read_l2d_refill_counter() { return read_event_counter(2); }
static inline uint64_t read_l2d_access_counter() { return read_event_counter(3); }
static inline uint64_t read_l2d_cache_refill_ld_counter() { return read_event_counter(4); }
static inline uint64_t read_l2d_cache_refill_st_counter() { return read_event_counter(5); }
static inline uint64_t read_mem_access_ld_counter() { return read_event_counter(6); }
static inline uint64_t read_mem_access_st_counter() { return read_event_counter(7); }

static inline void reset_event_counter(int counter)
{
    __asm__ volatile("msr pmselr_el0, %0" ::"r"(counter));
    __asm__ volatile("isb");
    __asm__ volatile("msr pmxevcntr_el0, %0" ::"r"(0));
    __asm__ volatile("isb"); // ensure reset is complete
}

static inline void reset_all_counters()
{
    for (int i = 0; i < 8; ++i)
    {
        reset_event_counter(i);
    }
}

void va_to_pa_test(uint64_t *buffer, size_t size, pfn_va_t *map, size_t n)
{
    const int ITERATIONS = 100;

    for (int i = 0; i < ITERATIONS; ++i)
    {
        addr_tuple addr = gen_random_addr(buffer, size);

        uint64_t pa = get_phys_addr((uint64_t)addr.v_addr);
        void *va2 = pa_to_va(pa, map, n);

        if (!va2)
        {
            printf("pa_to_va failed for PA 0x%llx\n",
                   (uint64_t)pa);
            exit(EXIT_FAILURE);
        }
        if (va2 != addr.v_addr)
        {
            printf(
                "mismatch: VA0 %p → PA 0x%llx → VA1 %p\n",
                addr.v_addr, (uint64_t)pa, va2);
            exit(EXIT_FAILURE);
        }
    }
    printf("VA↔PA round-trip test passed.\n");
}

static inline void clean_cache_line(void *addr, const char *type)
{
    if (strcmp(type, "CVAC") == 0)
    {
        __asm__ volatile("DC CVAC, %0" : : "r"(addr));
    }
    else if (strcmp(type, "CIVAC") == 0)
    {
        __asm__ volatile("DC CIVAC, %0" : : "r"(addr));
    }
}

static inline void add_dsb_barrier()
{
    __asm__ volatile("DSB 0xb");
}

#define OP_LDR 0
#define OP_STR 1

uint64_t perform_test(addr_tuple addr, int iter, const char *operation, const char *cache_type, bool add_dsb)
{
    reset_all_counters();
    volatile uint64_t val;
    struct timespec start, end;
    uint64_t elapsed_time = 0;
    uint64_t *v_addr = addr.v_addr;
    uint64_t tmp = *v_addr;

    // Parse operation
    int op_type = (strcmp(operation, "STR") == 0) ? 1 : 0;

    // Parse cache_type
    enum { CACHE_NONE = 0, CACHE_CIVAC, CACHE_CVAC } cache_op = CACHE_NONE;
    if (strcmp(cache_type, "CIVAC") == 0) cache_op = CACHE_CIVAC;
    else if (strcmp(cache_type, "CVAC") == 0) cache_op = CACHE_CVAC;

    // Initial clean + barrier to start from clean state
    __asm__ volatile("DC CIVAC, %0" ::"r"(addr.v_addr));
    __asm__ volatile("DSB ISH");
    __asm__ volatile("ISB");

    reset_all_counters();
    timespec_get(&start, TIME_UTC);

    // Assembly loop
    __asm__ volatile(
        // Initialize loop variables from C input
        "mov x0, %[iter]\n"       // x0 = iteration counter
        "mov x1, %[addr]\n"       // x1 = address to access (virtual address)
        "mov x2, %[op_type]\n"    // x2 = operation type: 0 for LDR, 1 for STR
        "mov x3, %[tmp]\n"        // x3 = value to store in STR
        "mov x5, %[cache_op]\n"   // x5 = cache flush type (0 = none, 1 = CIVAC, 2 = CVAC)
        "mov x6, %[add_dsb]\n"    // x6 = whether to insert DSB after cache op (0 = no, 1 = yes)
    
        // === Start of loop ===
        "1:\n"
    
        // --- Cache Maintenance Dispatch ---
        "cmp x5, #1\n"            // if cache_op == 1 (CIVAC)
        "b.eq 10f\n"              // jump to label 10 (CIVAC)
        "cmp x5, #2\n"            // if cache_op == 2 (CVAC)
        "b.eq 11f\n"              // jump to label 11 (CVAC)
        "b 12f\n"                 // else, skip cache maintenance
    
        // --- DC CIVAC ---
        "10:\n"
        "dc civac, x1\n"          // Clean & Invalidate to PoC (Point of Coherency)
        "b 12f\n"                 // jump to barrier decision
    
        // --- DC CVAC ---
        "11:\n"
        "dc cvac, x1\n"           // Clean to PoC (Point of Coherency)
    
        // --- Optional DSB after cache op ---
        "12:\n"
        "cmp x6, #0\n"            // if add_dsb == 0
        "b.eq 20f\n"              // skip barrier
        "dsb sy\n"                // full system DSB (ensures cache ops complete before continuing)
        "20:\n"
    
        // --- Load/Store Dispatch ---
        "cmp x2, #1\n"            // if op_type == 1
        "b.eq 30f\n"              // jump to STR
    
        // --- LDR path ---
        "dmb sy\n"                // memory barrier before load
        "ldr x4, [x1]\n"          // load from address into x4 (destroys x4 each iteration)
        "dmb sy\n"                // memory barrier after load
        "b 31f\n"                 // jump to loop decrement
    
        // --- STR path ---
        "30:\n"
        "dmb sy\n"                // memory barrier before store
        "str x3, [x1]\n"          // store tmp value at address
        "dmb sy\n"                // memory barrier after store
    
        // --- Loop decrement and branch ---
        "31:\n"
        "subs x0, x0, #1\n"       // subtract 1 from loop counter
        "b.ne 1b\n"               // if not zero, branch to start of loop (label 1)
    
        :
        : [iter] "r"(iter),
          [addr] "r"(v_addr),
          [op_type] "r"(op_type),
          [tmp] "r"(tmp),
          [cache_op] "r"(cache_op),
          [add_dsb] "r"(add_dsb)
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "memory"
    );
    

    timespec_get(&end, TIME_UTC);
    __asm__ volatile("ISB SY");
    __asm__ volatile("DSB SY");

    elapsed_time = (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);

    uint64_t l1d_refill = read_l1d_refill_counter();
    uint64_t l1d_access = read_l1d_access_counter();
    uint64_t l2d_refill = read_l2d_refill_counter();
    uint64_t l2d_access = read_l2d_access_counter();
    uint64_t l2d_cache_refill_ld = read_l2d_cache_refill_ld_counter();
    uint64_t l2d_cache_refill_st = read_l2d_cache_refill_st_counter();
    uint64_t mem_access_ld = read_mem_access_ld_counter();
    uint64_t mem_access_st = read_mem_access_st_counter();

    printf("Average %s + %s%s time: %lu ns\n", operation, cache_type, add_dsb ? " + DSB" : "", elapsed_time / iter);
    printf("L1D refill: %lu, L1D access: %lu, L2D refill: %lu, L2D access: %lu, L2D_CACHE_REFILL_LD: %lu, L2D_CACHE_REFILL_ST: %lu\n",
           l1d_refill, l1d_access, l2d_refill, l2d_access, l2d_cache_refill_ld, l2d_cache_refill_st);
    uint64_t nb_act = l2d_cache_refill_ld + l2d_cache_refill_st;
    printf("ACTs per second: %lu\n\n", nb_act * 1000000000 / elapsed_time);

    return elapsed_time;
}


void dump_mts()
{
    size_t mts = ptedit_get_mts();
    printf("MTs (raw): %zx\n", mts);
    int i;
    for (i = 0; i < 8; i++)
    {
        printf("MT%d: %d -> %s\n", i, ptedit_get_mt(i),
               ptedit_mt_to_string(ptedit_get_mt(i)));
    }
}

#define PTEDIT_PMD_PSE (1ULL << 7)

int ptedit_entry_is_huge(ptedit_entry_t entry)
{
    return (entry.pmd & PTEDIT_PMD_PSE) != 0;
}

void flush(void *p)
{
    asm volatile("DC CIVAC, %0" ::"r"(p));
    asm volatile("DSB ISH");
    asm volatile("ISB");
}

// ---------------------------------------------------------------------------
void maccess(void *p)
{
    volatile uint32_t value;
    asm volatile("LDR %0, [%1]\n\t" : "=r"(value) : "r"(p));
    // asm volatile("DSB ISH");
}

uint64_t rdtsc()
{
#if 1
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return t1.tv_sec * 1000 * 1000 * 1000ULL + t1.tv_nsec;
#else
    uint64_t result = 0;

    asm volatile("DSB SY");
    asm volatile("ISB");
    asm volatile("MRS %0, PMCCNTR_EL0" : "=r"(result));
    asm volatile("DSB SY");
    asm volatile("ISB");

    return result;
#endif
}
int access_time(void *ptr)
{
    uint64_t start = 0, end = 0, sum = 0;

    reset_all_counters();

    for (int i = 0; i < MEASUREMENTS; i++)
    {
        start = rdtsc();
        maccess(ptr);
        end = rdtsc();
        sum += end - start;
    }

    uint64_t l1d_refill = read_l1d_refill_counter();
    uint64_t l1d_access = read_l1d_access_counter();
    uint64_t l2d_refill = read_l2d_refill_counter();
    uint64_t l2d_access = read_l2d_access_counter();

    printf("L1D refill: %lu, L1D access: %lu, L2D refill: %lu, L2D access: %lu\n",
           l1d_refill, l1d_access, l2d_refill, l2d_access);

    return (int)(sum / MEASUREMENTS);
}

int set_memory_mt(void *addr, int mt, int is_huge)
{
    ptedit_entry_t entry = ptedit_resolve(addr, 0);
    if (is_huge)
    {
        entry.pmd = ptedit_apply_mt_huge(entry.pmd, mt);
        entry.valid = PTEDIT_VALID_MASK_PMD;
    }
    else
    {
        entry.pte = ptedit_apply_mt(entry.pte, mt);
        entry.valid = PTEDIT_VALID_MASK_PTE;
    }
    ptedit_update(addr, 0, &entry);
    return 0;
}

void instructions_timing_test(addr_tuple addr, int iter, uint64_t *buffer, size_t size)
{
    printf("\n");
    init_cache_counters();
    const char *operations[] = {"LDR", "STR"};
    const char *cache_types[] = {"CVAC", "CIVAC"};
    bool dsb_variants[] = {false, true};

    for (int op = 0; op < 2; ++op)
    {
        perform_test(addr, iter, operations[op], "", false);
        perform_test(addr, iter, operations[op], "", true);
        for (int cache = 0; cache < 2; ++cache)
        {
            for (int dsb = 0; dsb < 2; ++dsb)
            {
                perform_test(addr, iter, operations[op], cache_types[cache], dsb_variants[dsb]);
            }
        }
    }
}