#include "utils.h"
#include "memory.h"
#include "hammer.h"
#include <assert.h>
#include <sys/mman.h>
#include <linux/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <tests.h>

/**
 * hammer_single - Performs a single hammering operation on a given address.
 * @addr: The address tuple containing the virtual address to hammer.
 * @iter: The number of iterations to perform.
 * @timing: Boolean flag to enable timing measurement.
 * @op_type: Operation type: 0 for LDR (read), 1 for STR (write), 2 for ZVA.
 * @cache_op: Cache operation: 0 for none, 1 for CIVAC, 2 for CVAC.
 * @add_dsb: Whether to add DSB barrier after cache op: 0 for no, 1 for yes.
 *
 * Returns the elapsed time in nanoseconds if timing is enabled, otherwise 0.
 */
uint64_t hammer_single(addr_tuple addr, int iter, bool timing, int op_type, int cache_op, int add_dsb)
{
    uint64_t *v_addr = addr.v_addr;
    volatile uint64_t val;
    struct timespec start, end;
    uint64_t elapsed_time = 0;

    uint64_t tmp = *v_addr;
    uint64_t backup = *v_addr;

    uint8_t backup1[64];
    memcpy(backup1, (void *)((uintptr_t)addr.v_addr & ~0x3F), 64);

    // Clean up
    __asm__ volatile("DC CIVAC, %0" : : "r"(v_addr));
    __asm__ volatile("ISB SY");
    __asm__ volatile("DSB SY");

    timespec_get(&start, TIME_UTC);

    __asm__ volatile(
        // Initialize loop variables from C input
        "mov x0, %[iter]\n"     // x0 = iteration counter
        "mov x1, %[addr]\n"     // x1 = address to access (virtual address)
        "mov x2, %[op_type]\n"  // x2 = operation type: 0 for LDR, 1 for STR
        "mov x3, %[tmp]\n"      // x3 = value to store in STR
        "mov x5, %[cache_op]\n" // x5 = cache flush type (0 = none, 1 = CIVAC, 2 = CVAC)
        "mov x6, %[add_dsb]\n"  // x6 = whether to insert DSB after cache op (0 = no, 1 = yes)

        // === Start of loop ===
        "1:\n"

        // --- Cache Maintenance Dispatch ---
        "cmp x5, #1\n" // if cache_op == 1 (CIVAC)
        "b.eq 10f\n"   // jump to label 10 (CIVAC)
        "cmp x5, #2\n" // if cache_op == 2 (CVAC)
        "b.eq 11f\n"   // jump to label 11 (CVAC)
        "b 12f\n"      // else, skip cache maintenance

        // --- DC CIVAC ---
        "10:\n"
        "dc civac, x1\n" // Clean & Invalidate to PoC (Point of Coherency)
        "b 12f\n"        // jump to barrier decision

        // --- DC CVAC ---
        "11:\n"
        "dc cvac, x1\n" // Clean to PoC (Point of Coherency)

        // --- Optional DSB after cache op ---
        "12:\n"
        "cmp x6, #0\n" // if add_dsb == 0
        "b.eq 20f\n"   // skip barrier
        "dsb sy\n"     // full system DSB (ensures cache ops complete before continuing)
        "20:\n"

        // --- Load/Store/ZVA Dispatch ---
        "cmp x2, #2\n" // if op_type == 2
        "b.eq 40f\n"   // jump to ZVA
        "cmp x2, #1\n" // if op_type == 1
        "b.eq 30f\n"   // jump to STR

        // --- LDR path ---
        "dmb sy\n"       // memory barrier before load
        "ldr x4, [x1]\n" // load from address into x4 (destroys x4 each iteration)
        "dmb sy\n"       // memory barrier after load
        "b 31f\n"        // jump to loop decrement

        // --- STR path ---
        "30:\n"
        "dmb sy\n"       // memory barrier before store
        "str x3, [x1]\n" // store tmp value at address
        "dmb sy\n"       // memory barrier after store
        "b 31f\n"        // jump to loop decrement

        // --- DC ZVA path ---
        "40:\n"
        "dc zva, x1\n" // zero cache line at address

        // --- Loop decrement and branch ---
        "31:\n"
        "subs x0, x0, #1\n" // subtract 1 from loop counter
        "b.ne 1b\n"         // if not zero, branch to start of loop (label 1)

        :
        : [iter] "r"(iter),
          [addr] "r"(v_addr),
          [op_type] "r"(op_type),
          [tmp] "r"(tmp),
          [cache_op] "r"(cache_op),
          [add_dsb] "r"(add_dsb)
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "memory");

    timespec_get(&end, TIME_UTC);
    elapsed_time = (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);
    *v_addr = backup;
    if (op_type == 2)
    {
        memcpy((void *)((uintptr_t)v_addr & ~0x3F), backup1, 64);
    }
    // Safe guard against unwanted value changes
    if (*v_addr != backup)
    {
        perror("Wrong hammered value");
        fprintf(stderr, "Expected: %lx, Actual: %lx\n", tmp, *v_addr);
        exit(EXIT_FAILURE);
    }

    return elapsed_time;
}

/**
 * hammer_double - Performs a double hammering operation on two given addresses.
 * @addr_1: The first address tuple containing the virtual address to hammer.
 * @addr_2: The second address tuple containing the virtual address to hammer.
 * @iter: The number of iterations to perform.
 * @timing: Boolean flag to enable timing measurement.
 * @op_type: Operation type: 0 for LDR (read), 1 for STR (write), 2 for ZVA.
 * @cache_op: Cache operation: 0 for none, 1 for CIVAC, 2 for CVAC.
 * @add_dsb: Whether to add DSB barrier after cache op: 0 for no, 1 for yes.
 *
 * Returns the elapsed time in nanoseconds if timing is enabled, otherwise 0.
 */
uint64_t hammer_double(addr_tuple addr_1, addr_tuple addr_2, int iter, bool timing, int op_type, int cache_op, int add_dsb)
{
    uint64_t *v_addr_1 = addr_1.v_addr;
    uint64_t *v_addr_2 = addr_2.v_addr;
    volatile uint64_t val1;
    volatile uint64_t val2;
    struct timespec start, end;
    uint64_t elapsed_time = 0;

    uint64_t tmp1 = *v_addr_1;
    uint64_t tmp2 = *v_addr_2;
    uint64_t tmp1_m = ~tmp1;
    uint64_t tmp2_m = ~tmp2;

    uint8_t backup1[64], backup2[64];
    memcpy(backup1, (void *)((uintptr_t)addr_1.v_addr & ~0x3F), 64);
    memcpy(backup2, (void *)((uintptr_t)addr_2.v_addr & ~0x3F), 64);
    // Clean up
    __asm__ volatile("DC CIVAC, %0" : : "r"(v_addr_1));
    __asm__ volatile("DC CIVAC, %0" : : "r"(v_addr_2));
    __asm__ volatile("ISB SY");
    __asm__ volatile("DSB SY");

    timespec_get(&start, TIME_UTC);

    __asm__ volatile(
        // Initialize loop variables from C input
        "mov x0, %[iter]\n"     // x0 = iteration counter
        "mov x1, %[addr1]\n"    // x1 = first address
        "mov x2, %[addr2]\n"    // x2 = second address
        "mov x3, %[op_type]\n"  // x3 = operation type: 0 for LDR, 1 for STR
        "mov x4, %[tmp1_m]\n"   // x4 = value to store for first address
        "mov x5, %[tmp2_m]\n"   // x5 = value to store for second address
        "mov x6, %[cache_op]\n" // x6 = cache flush type
        "mov x7, %[add_dsb]\n"  // x7 = whether to add DSB

        // === Start of loop ===
        "1:\n"

        // --- First perform both cache operations ---
        "cmp x6, #1\n" // if cache_op == 1 (CIVAC)
        "b.eq 10f\n"   // jump to CIVAC
        "cmp x6, #2\n" // if cache_op == 2 (CVAC)
        "b.eq 11f\n"   // jump to CVAC
        "b 12f\n"      // skip cache operations if none

        // --- CIVAC for both addresses ---
        "10:\n"
        "dc civac, x1\n" // Clean & Invalidate first address
        "dc civac, x2\n" // Clean & Invalidate second address
        "b 12f\n"        // continue to barrier check

        // --- CVAC for both addresses ---
        "11:\n"
        "dc cvac, x1\n" // Clean first address
        "dc cvac, x2\n" // Clean second address

        // --- Optional barrier after cache operations ---
        "12:\n"
        "cmp x7, #0\n" // check if barrier needed
        "b.eq 20f\n"   // skip if no barrier
        "dsb sy\n"     // barrier after cache operations

        // --- Memory operations ---
        "20:\n"
        "cmp x3, #2\n" // check operation type for ZVA
        "b.eq 40f\n"   // branch to ZVA if op_type == 2
        "cmp x3, #1\n" // check operation type
        "b.eq 30f\n"   // branch to STR if op_type == 1

        // --- LDR path (both loads) ---
        "ldr x10, [x1]\n" // load first address
        "ldr x11, [x2]\n" // load second address
        "isb\n"           // barrier after loads
        "b 31f\n"         // continue to loop end

        // --- STR path (both stores) ---
        "30:\n"
        "str x4, [x1]\n" // store to first address
        "str x5, [x2]\n" // store to second address
        "isb\n"          // barrier after stores
        "b 31f\n"        // continue to loop end

        // --- DC ZVA path (both addresses) ---
        "40:\n"
        "dmb sy\n"     // barrier before ZVA operations
        "dc zva, x1\n" // zero cache line at first address
        "dc zva, x2\n" // zero cache line at second address
        "dmb sy\n"     // barrier after ZVA operations

        // --- Loop control ---
        "31:\n"
        "subs x0, x0, #1\n" // decrement counter
        "b.ne 1b\n"         // loop if not done

        :
        : [iter] "r"(iter),
          [addr1] "r"(v_addr_1),
          [addr2] "r"(v_addr_2),
          [op_type] "r"(op_type),
          [tmp1_m] "r"(tmp1_m),
          [tmp2_m] "r"(tmp2_m),
          [cache_op] "r"(cache_op),
          [add_dsb] "r"(add_dsb)
        : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x10", "x11", "memory");

    timespec_get(&end, TIME_UTC);
    elapsed_time = (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);
    // Restore values before checking
    *v_addr_1 = tmp1;
    *v_addr_2 = tmp2;
    if (op_type == 2)
    {
        memcpy((void *)((uintptr_t)v_addr_1 & ~0x3F), backup1, 64);
        memcpy((void *)((uintptr_t)v_addr_2 & ~0x3F), backup2, 64);
    }
    // Last check that we didn't change the values
    if (*v_addr_1 != tmp1)
    {
        perror("Wrong hammered value");
        fprintf(stderr, "Expected: %lx, Actual: %lx\n", tmp1, *v_addr_1);
        exit(EXIT_FAILURE);
    }
    if (*v_addr_2 != tmp2)
    {
        perror("Wrong hammered value");
        fprintf(stderr, "Expected: %lx, Actual: %lx\n", tmp2, *v_addr_2);
        exit(EXIT_FAILURE);
    }

    return elapsed_time;
}

uint64_t hammer_multiple(addr_tuple *addrs, int num_addrs, int iter, bool timing, int op_type, int cache_op, int add_dsb)
{
    uint64_t elapsed_time = 0;
    for(int i = 0; i < num_addrs; i++)
    {
        if (addrs[i].v_addr == NULL)
        {
            fprintf(stderr, "Invalid address in hammer_multiple\n");
            return 0;
        }
    }
    struct timespec start, end;
    timespec_get(&start, TIME_UTC);
    uint64_t backups[num_addrs];
    for (int i = 0; i < num_addrs; i++)
    {
        backups[i] = *addrs[i].v_addr;
    }

    for(int i = 0; i < num_addrs; i++)
    {
        __asm__ volatile("DC CIVAC, %0" : : "r"(addrs[i].v_addr));
    }
    __asm__ volatile("ISB SY");
    __asm__ volatile("DSB SY");
    volatile uint64_t val;
    for (int i = 0; i < num_addrs*iter; i++)
    {
        uint64_t *v_addr = addrs[i % num_addrs].v_addr;
        switch(op_type)
        {
            case 0: // LDR
                __asm__ volatile("LDR %0, [%1]" : "=r"(val) : "r"(v_addr));
                break;
            case 1: // STR
                __asm__ volatile("STR %0, [%1]" : : "r"(val), "r"(v_addr));
                break;
            case 2: // ZVA
                __asm__ volatile("DC ZVA, %0" : : "r"(v_addr));
                break;
            default:
                fprintf(stderr, "Invalid operation type\n");
                exit(EXIT_FAILURE);
        }
        switch(cache_op)
        {
            case 0: // No cache operation
                break;
            case 1: // CIVAC
                __asm__ volatile("DC CIVAC, %0" : : "r"(v_addr));
                break;
            case 2: // CVAC
                __asm__ volatile("DC CVAC, %0" : : "r"(v_addr));
                break;
            default:
                fprintf(stderr, "Invalid cache operation type\n");
                exit(EXIT_FAILURE);
        }
        if (add_dsb)
        {
            __asm__ volatile("DSB SY");
        }
    }
    timespec_get(&end, TIME_UTC);
    elapsed_time = (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);
    for (int i = 0; i < num_addrs; i++)
    {
        *addrs[i].v_addr = backups[i];
    }
    return elapsed_time;
}

/**
 * pattern_single - Executes a single hammering pattern.
 * @addr: The address tuple containing the virtual address to hammer.
 * @iter: The number of iterations to perform.
 * @timing: Boolean flag to enable timing measurement.
 * @op_type: Operation type: 0 for LDR (read), 1 for STR (write).
 * @cache_op: Cache operation: 0 for none, 1 for CIVAC, 2 for CVAC.
 * @add_dsb: Whether to add DSB barrier after cache op: 0 for no, 1 for yes.
 *
 * Returns the elapsed time in nanoseconds if timing is enabled, otherwise 0.
 */
uint64_t pattern_single(addr_tuple addr, int iter, bool timing, int op_type, int cache_op, int add_dsb)
{
    return hammer_single(addr, iter, timing, op_type, cache_op, add_dsb);
}

/**
 * pattern_single_decoy - Executes a single hammering pattern with a decoy address.
 * @addr: The address tuple containing the virtual address to hammer.
 * @buffer: The buffer containing potential decoy addresses.
 * @size: The size of the buffer.
 * @iter: The number of iterations to perform.
 * @timing: Boolean flag to enable timing measurement.
 * @op_type: Operation type: 0 for LDR (read), 1 for STR (write).
 * @cache_op: Cache operation: 0 for none, 1 for CIVAC, 2 for CVAC.
 * @add_dsb: Whether to add DSB barrier after cache op: 0 for no, 1 for yes.
 *
 * Returns the elapsed time in nanoseconds if timing is enabled, otherwise 0.
 */
uint64_t pattern_single_decoy(addr_tuple addr, uint64_t *buffer, size_t size, int iter, bool timing, int op_type, int cache_op, int add_dsb)
{
    addr_tuple decoy = addr;
    while (is_possibly_same_row(addr, decoy))
    {
        decoy = gen_random_addr(buffer, size);
    }

    return hammer_double(addr, decoy, iter, timing, op_type, cache_op, add_dsb);
}

/**
 * pattern_quad - Executes a quad hammering pattern.
 * @addr: The address tuple containing the virtual address to hammer.
 * @buffer: The buffer containing potential addresses.
 * @size: The size of the buffer.
 * @iter: The number of iterations to perform.
 * @timing: Boolean flag to enable timing measurement.
 * @map: The page frame number to virtual address mapping.
 * @pfn_va_len: The length of the mapping.
 * @op_type: Operation type: 0 for LDR (read), 1 for STR (write).
 * @cache_op: Cache operation: 0 for none, 1 for CIVAC, 2 for CVAC.
 * @add_dsb: Whether to add DSB barrier after cache op: 0 for no, 1 for yes.
 *
 * Returns the elapsed time in nanoseconds if timing is enabled, otherwise 0.
 */
uint64_t pattern_quad(addr_tuple addr, uint64_t *buffer, size_t size, int iter, bool timing, pfn_va_t *map, size_t pfn_va_len, int op_type, int cache_op, int add_dsb)
{
    addr_tuple addr_n_plus = next_row_deterministic(addr, map, pfn_va_len);
    addr_tuple addr_n_minus = prev_row_deterministic(addr, map, pfn_va_len);
    if (addr_n_plus.v_addr == NULL || addr_n_minus.v_addr == NULL)
    {
        return 0;
    }
    addr_tuple addr_f_plus = next_row_deterministic(addr_n_plus, map, pfn_va_len);
    addr_tuple addr_f_minus = prev_row_deterministic(addr_n_minus, map, pfn_va_len);
    if (addr_f_plus.v_addr == NULL || addr_f_minus.v_addr == NULL)
    {
        return 0;
    }
    // Last check for quad pattern
    int bank = get_bank(addr_f_plus.p_addr);
    // Check that all addresses belong to same bank
    if (get_bank(addr_n_plus.p_addr) != bank || get_bank(addr.p_addr) != bank || get_bank(addr_n_minus.p_addr) != bank || get_bank(addr_f_minus.p_addr) != bank)
    {
        return 0;
    }
    // Check that all addresses belong to same channel
    int channel = get_channel(addr_f_plus.p_addr);
    if (get_channel(addr_n_plus.p_addr) != channel || get_channel(addr.p_addr) != channel || get_channel(addr_n_minus.p_addr) != channel || get_channel(addr_f_minus.p_addr) != channel)
    {
        return 0;
    }

    if (addr_f_minus.v_addr > buffer && addr_f_plus.v_addr < buffer + size)
    {
        return hammer_double(addr_f_plus, addr_f_minus, iter, timing, op_type, cache_op, add_dsb);
    }
    else
    {
        return 0;
    }
}

uint64_t pattern_many_sided(addr_tuple addr, int inter, bool timing, pfn_va_t *map, size_t pfn_va_len, int op_type, int cache_op, int add_dsb, int nb_sides)
{
    int current_length = 0;
    addr_tuple addrs[nb_sides];
    addrs[0] = addr; // Start with the given address
    current_length++;
    int bank = get_bank(addr.p_addr);
    int channel = get_channel(addr.p_addr);
    int column = get_column(addr.p_addr);
    int sub = get_subpartition(addr.p_addr);
    while(current_length < nb_sides) {
        addr_tuple new_addr;
        if (current_length == 1) {
            addrs[current_length] = next_row_deterministic(addr, map, pfn_va_len);
            if(addrs[current_length].v_addr == NULL)
            {
                return 0;
            }
            current_length++;
            continue;
        }
        if (current_length % 2 == 0) {
            new_addr = prev_row_deterministic(prev_row_deterministic(addrs[current_length-2], map, pfn_va_len), map, pfn_va_len);
        } else {
            new_addr = next_row_deterministic(next_row_deterministic(addrs[current_length-2], map, pfn_va_len), map, pfn_va_len);
        }
        if(new_addr.v_addr == NULL || get_bank(new_addr.p_addr) != bank || get_channel(new_addr.p_addr) != channel || get_column(new_addr.p_addr) != column || get_subpartition(new_addr.p_addr) != sub)
        {
            return 0;
        }
        addrs[current_length] = new_addr;
        current_length++;
    }
    return hammer_multiple(addrs, current_length, inter, timing, op_type, cache_op, add_dsb);
}

uint64_t pattern_double(addr_tuple addr, uint64_t *buffer, size_t size, int iter, bool timing, pfn_va_t *map, size_t pfn_va_len, int op_type, int cache_op, int add_dsb)
{
    addr_tuple addr_n_plus = next_row_deterministic(addr, map, pfn_va_len);
    addr_tuple addr_n_minus = prev_row_deterministic(addr, map, pfn_va_len);
    if (addr_n_plus.v_addr == NULL || addr_n_minus.v_addr == NULL)
    {
        return 0;
    }
    return hammer_double(addr_n_plus, addr_n_minus, iter, timing, op_type, cache_op, add_dsb);
}

/**
 * detect_bitflips - Detects bitflips in a buffer using a given pattern function.
 * @buffer: The buffer to check for bitflips.
 * @size: The size of the buffer in bytes.
 * @pattern: The pattern function used to generate expected values.
 *
 * Returns an array of detected bitflips, with a maximum of 256 entries.
 */
bitflip *detect_bitflips(uint64_t *buffer, size_t size, pattern_func pattern)
{
    bitflip *bitflips = (bitflip *)calloc(256, sizeof(bitflip));
    int bitflip_count = 0;
    size_t word_count = size / sizeof(uint64_t);

    // Process in larger chunks to improve memory locality
    const size_t CHUNK_SIZE = 4096; // Process 4KB chunks at a time
    size_t chunks = (word_count + CHUNK_SIZE - 1) / CHUNK_SIZE;

    for (size_t chunk = 0; chunk < chunks && bitflip_count < 256; chunk++)
    {
        size_t start_idx = chunk * CHUNK_SIZE;
        size_t end_idx = (start_idx + CHUNK_SIZE) < word_count ? (start_idx + CHUNK_SIZE) : word_count;

        // Pre-calculate all expected values for this chunk
        uint64_t expected_values[CHUNK_SIZE];
        for (size_t i = start_idx; i < end_idx; i++)
        {
            uint64_t addr = (uint64_t)(&buffer[i]);
            expected_values[i - start_idx] = pattern(addr);
        }

        // Now check for bitflips with better cache locality
        for (size_t i = start_idx; i < end_idx && bitflip_count < 256; i++)
        {
            uint64_t byte = buffer[i];
            uint64_t expected = expected_values[i - start_idx];

            if (expected != byte)
            {
                uint64_t diff = expected ^ byte;
                // Skip byte if there are no changes - optimization for sparse bitflips
                if (diff == 0)
                    continue;

                uint64_t addr = (uint64_t)(&buffer[i]);

                // Optimize bit position checking - use intrinsics if available
                for (int j = 0; j < 64 && bitflip_count < 256; j++)
                {
                    if (diff & (1ULL << j))
                    {
                        bitflips[bitflip_count].direction = (byte & (1ULL << j)) ? 1 : 0;
                        bitflips[bitflip_count].addr.v_addr = (void *)addr;
                        bitflips[bitflip_count].addr.p_addr = get_phys_addr(addr);
                        bitflips[bitflip_count].bit_pos = j;
                        bitflips[bitflip_count].expected = expected;
                        bitflips[bitflip_count].actual = byte;
                        bitflip_count++;
                    }
                }

                // Correct the bitflip in the buffer
                buffer[i] = expected;
            }
        }
    }

    // Zero out the rest of the bitflips slots
    for (int k = bitflip_count; k < 256; k++)
    {
        bitflips[k].addr.v_addr = NULL;
    }

    return bitflips;
}

/**
 * buffer_init - Initializes a buffer with a given pattern.
 * @size: The size of the buffer in bytes.
 * @type: The type of buffer (e.g., HUGEPAGE_2MB).
 * @pattern: The pattern function used to initialize the buffer.
 *
 * Returns a pointer to the initialized buffer, or NULL on failure.
 */
uint64_t *buffer_init(size_t size, buffer_type type, pattern_func pattern)
{
    size_t count = size / sizeof(uint64_t);
    uint64_t flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE;

    if (type == HUGEPAGE_2MB)
        flags |= MAP_HUGETLB | MAP_HUGE_2MB;

    uint64_t *buffer = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (buffer == MAP_FAILED)
    {
        if (type == HUGEPAGE_2MB || type == HUGEPAGE_1GB)
            printf("Failed to allocate huge page. Run 'sudo make prepare' or 'sudo make huge2m'.\n");
        perror("mmap");
        return NULL;
    }
    printf("Allocated %zu bytes at %p\n", size, buffer);

    for (size_t i = 0; i < count; ++i)
    {
        uint64_t byte_addr = (uint64_t)buffer + i * sizeof(uint64_t);
        if (pattern == NULL)
        {
            buffer[i] = 0;
            continue;
        }
        else
        {
            buffer[i] = (uint64_t)pattern(byte_addr);
        }
    }
    return buffer;
}

/**
 * progress_bar - Displays a progress bar in the terminal.
 * @done: The number of completed iterations.
 * @total: The total number of iterations.
 * @flips: The number of detected bitflips.
 */
static void progress_bar(size_t done, size_t total, size_t flips)
{
    const size_t bar_w = 50; // width in chars
    double pct = (double)done / total;
    size_t mark = (size_t)(pct * bar_w);

    printf("\t\t\r[%.*s%.*s] %5.1f %%   flips: %zu",
           (int)mark, "##################################################",
           (int)(bar_w - mark), "                                                  ",
           pct * 100.0, flips);
    fflush(stdout);
}

/**
 * buffer_init_check - Verifies that a buffer is correctly initialized.
 * @buffer: The buffer to check.
 * @size: The size of the buffer in bytes.
 * @pattern: The pattern function used to generate expected values.
 */
void buffer_init_check(uint64_t *buffer, size_t size, pattern_func pattern)
{
    bitflip *bitflips = detect_bitflips(buffer, size, pattern);
    for (int i = 0; i < 256; i++)
    {
        if (bitflips[i].addr.v_addr != NULL)
        {
            fprintf(stderr, "Buffer initialization failed at address %p\n", bitflips[i].addr.v_addr);
            fprintf(stderr, "Expected: %lx, Actual: %lx\n", bitflips[i].expected, bitflips[i].actual);
            free(bitflips);
            exit(EXIT_FAILURE);
        }
    }
    free(bitflips);
}

/**
 * bitflip_test - Performs a bitflip test on a buffer using various hammering strategies.
 * @buffer_size: The size of the buffer in bytes.
 * @b_type: The type of buffer (e.g., HUGEPAGE_2MB).
 * @pattern: The pattern function used to initialize the buffer.
 * @hammer_pattern: The hammering pattern to use.
 * @timing: Boolean flag to enable timing measurement.
 * @iter: The number of iterations to perform.
 * @hammer_iter: The number of hammering iterations per test.
 * @output_file: The file to write test results to, or NULL for no output.
 * @uncachable: Boolean flag to make the buffer uncachable.
 * @op_type: Operation type: 0 for LDR (read), 1 for STR (write).
 * @cache_op: Cache operation: 0 for none, 1 for CIVAC, 2 for CVAC.
 * @add_dsb: Whether to add DSB barrier after cache op: 0 for no, 1 for yes.
 */
void bitflip_test(size_t buffer_size, buffer_type b_type, pattern_func pattern, hammer_pattern hammer_pattern, bool timing, int iter, int hammer_iter, char *output_file, bool uncachable, int op_type, int cache_op, int add_dsb, int nb_sides)
{
    FILE *file = NULL;
    if (output_file != NULL)
    {
        file = fopen(output_file, "w");
        if (file != NULL)
        {
            fprintf(file, "Buffer Size: %zu\n", buffer_size);
            fprintf(file, "Buffer Type: %d\n", b_type);
            fprintf(file, "Hammer Pattern: %d\n", hammer_pattern);
            fprintf(file, "Iterations: %d\n", iter);
            fprintf(file, "Hammer Iterations: %d\n", hammer_iter);
            fprintf(file, "Bitflips details :\n");
            fprintf(file, "Operation Type: %d\n", op_type);
            fprintf(file, "Cache Operation: %d\n", cache_op);
            fprintf(file, "Add DSB: %d\n", add_dsb);
            fprintf(file, "Uncacheable: %d\n", uncachable);
            fprintf(file, "----------------------------------------\n");
            fflush(file);
        }
    }
    uint64_t *buffer = buffer_init(buffer_size, b_type, pattern);
    buffer_init_check(buffer, buffer_size, pattern);

    if (uncachable)
    {
        make_uncacheable_multi(buffer, buffer_size);
        printf("Buffer made uncachable\n");
    }

    size_t pmap_len = 0;
    pfn_va_t *pmap = build_pfn_map(buffer, buffer_size, &pmap_len);
    if (pmap == NULL || pmap_len == 0)
    {
        printf("Failed to build PFN map\n");
        exit(EXIT_FAILURE);
    }
    printf("PFN map built with %zu entries\n", pmap_len);
    va_to_pa_test(buffer, buffer_size, pmap, pmap_len);

    printf("Testing hammering strategies...\n");
    instructions_timing_test(gen_random_addr(buffer, buffer_size), 1000000, buffer, buffer_size);
    int total_flips = 0;
    int i = 0;
    int progress = 0;
    progress_bar(0, iter, total_flips);

    struct timespec start, end;

    timespec_get(&start, TIME_UTC);

    for (i = 0; i < iter; i++)
    {
        addr_tuple addr = gen_random_addr(buffer, buffer_size);
        uint64_t iter_time;
        uint64_t average_time = 0;

        switch (hammer_pattern)
        {
        case PATTERN_SINGLE:
            iter_time = pattern_single(addr, hammer_iter, timing, op_type, cache_op, add_dsb);
            average_time = iter_time / hammer_iter;
            break;
        case PATTERN_QUAD:
            iter_time = pattern_quad(addr, buffer, buffer_size, hammer_iter, timing, pmap, pmap_len, op_type, cache_op, add_dsb) / 2;
            average_time = iter_time / hammer_iter;
            break;
        default:
        case PATTERN_SINGLE_DECOY:
            iter_time = pattern_single_decoy(addr, buffer, buffer_size, hammer_iter, timing, op_type, cache_op, add_dsb) / 2;
            average_time = iter_time / hammer_iter;

            break;
        case PATTERN_DOUBLE:
            iter_time = pattern_double(addr, buffer, buffer_size, hammer_iter, timing, pmap, pmap_len, op_type, cache_op, add_dsb) / 2;
            average_time = iter_time / hammer_iter;
            break;
        case PATTERN_MANY_SIDED:
            iter_time = many_sided(addr, hammer_iter, timing, pmap, pmap_len, op_type, cache_op, add_dsb, nb_sides) / nb_sides;
            average_time = iter_time / hammer_iter;
            break;
        }
        if (iter_time == 0)
        {
            i--;
            continue;
        }

        // Measure time for detect_bitflips
        struct timespec detect_start, detect_end;
        timespec_get(&detect_start, TIME_UTC);

        bitflip *bitflips = detect_bitflips(buffer, buffer_size, pattern);

        timespec_get(&detect_end, TIME_UTC);
        double detect_time = (detect_end.tv_sec - detect_start.tv_sec) * 1000.0 +
                             (detect_end.tv_nsec - detect_start.tv_nsec) / 1000000.0;
        for (int j = 0; j < 256; j++)
        {
            if (bitflips[j].addr.v_addr != NULL)
            {
                printf("\r\nBitflip detected at address %p\n", bitflips[j].addr.v_addr);
                printf("Expected: %lx, Actual: %lx\n", bitflips[j].expected, bitflips[j].actual);
                printf("Bit position: %d Flip direction: %d\n", bitflips[j].bit_pos, bitflips[j].direction);
                printf("Hammered address: %p\n", addr.v_addr);
                if (file != NULL)
                {
                    fprintf(file, "Iter: %d, Aggr_v: %p, Aggr_p: %lx, Virtual: %p, Physical: %lx, Expected: %x, Actual: %x, Bit_pos: %d\n",
                            i, addr.v_addr, addr.p_addr,
                            bitflips[j].addr.v_addr, bitflips[j].addr.p_addr,
                            bitflips[j].expected, bitflips[j].actual, bitflips[j].bit_pos);
                    fflush(file);
                }
                total_flips++;
            }
        }
        free(bitflips);
        progress++;
        if (progress >= iter / 2000 || i == 0)
        {
            timespec_get(&end, TIME_UTC);
            double elapsed_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
            double time_per_iter = elapsed_time / progress;
            int remaining_iter = iter - i - 1; // Adjust to account for current iteration
            double remaining_time = time_per_iter * remaining_iter;
            progress_bar(i + 1, iter, total_flips);
            printf("\t ETA : %dh %dmin %dsec", (int)(remaining_time / 3600), (int)((int)remaining_time % 3600) / 60, (int)remaining_time % 60);
            printf("\t Iterations: %d/%d", i + 1, iter);
            progress = 0;
            timespec_get(&start, TIME_UTC);
        }
    }

    if (file != NULL)
    {
        fclose(file);
    }

    munmap(buffer, buffer_size);
    free(pmap);
}
