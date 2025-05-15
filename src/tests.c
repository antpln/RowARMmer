#include <tests.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

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

uint64_t perform_test(addr_tuple addr, int iter, const char *operation, const char *cache_type, bool add_dsb)
{
    reset_all_counters();
    volatile uint64_t val;
    struct timespec start, end;
    uint64_t elapsed_time = 0;
    uint64_t *v_addr = addr.v_addr;
    uint64_t tmp = *v_addr;

    // Clean up
    clean_cache_line(v_addr, "CIVAC");
    clean_cache_line(v_addr, "CIVAC");
    __asm__ volatile("ISB SY");
    __asm__ volatile("DSB SY");

    reset_all_counters();

    timespec_get(&start, TIME_UTC);
    for (int i = 0; i < iter; i++)
    {
        clean_cache_line(v_addr, cache_type);
        if (add_dsb)
        {
            add_dsb_barrier();
        }
        if (strcmp(operation, "LDR") == 0)
        {
            __asm__ volatile("LDR %0, [%1]" : "=r"(val) : "r"(v_addr));
        }
        else if (strcmp(operation, "STR") == 0)
        {
            __asm__ volatile("STR %0, [%1]" : : "r"(tmp), "r"(v_addr));
        }
    }
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
}

void instructions_timing_test(addr_tuple addr, int iter)
{
    printf("\n");
    init_cache_counters();

    const char *operations[] = {"LDR", "STR"};
    const char *cache_types[] = {"CVAC", "CIVAC", "IVAC"};
    bool dsb_variants[] = {false, true};

    for (int op = 0; op < 2; ++op)
    {
        for (int cache = 0; cache < 3; ++cache)
        {
            for (int dsb = 0; dsb < 2; ++dsb)
            {
                perform_test(addr, iter, operations[op], cache_types[cache], dsb_variants[dsb]);
            }
        }
    }
}