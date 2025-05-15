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

volatile uint64_t g_hammer_sink;

/**
 * hammer_single - Performs a single hammering operation on a given address.
 * @addr: The address tuple containing the virtual address to hammer.
 * @iter: The number of iterations to perform.
 * @timing: Boolean flag to enable timing measurement.
 *
 * Returns the elapsed time in nanoseconds if timing is enabled, otherwise 0.
 */
uint64_t hammer_single(addr_tuple addr, int iter, bool timing)
{
    uint64_t *v_addr = addr.v_addr;
    volatile uint64_t val;
    struct timespec start, end;
    uint64_t elapsed_time = 0;

    uint64_t tmp = *v_addr;
    // Clean up
    __asm__ volatile("DC CIVAC, %0" : : "r"(v_addr));
    __asm__ volatile("ISB SY");
    __asm__ volatile("DSB SY");

    if (timing)
    {
        timespec_get(&start, TIME_UTC);
    }

    for (int i = 0; i < iter; i++)
    {
        __asm__ volatile("DC CIVAC, %0" : : "r"(v_addr));
        __asm__ volatile("LDR %0, [%1]" : "=r"(val) : "r"(v_addr));
    }

    if (timing)
    {
        timespec_get(&end, TIME_UTC);
        elapsed_time = (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);
    }

    // Safe guard against unwanted value changes
    __asm__ volatile("LDR %0, [%1]" : "=r"(val) : "r"(v_addr));
    if (val != tmp)
    {
        perror("Wrong hammered value");
        fprintf(stderr, "Expected: %lx, Actual: %lx\n", tmp, val);
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
 *
 * Returns the elapsed time in nanoseconds if timing is enabled, otherwise 0.
 */
uint64_t hammer_double(addr_tuple addr_1, addr_tuple addr_2, int iter, bool timing)
{
    uint64_t *v_addr_1 = addr_1.v_addr;
    uint64_t *v_addr_2 = addr_2.v_addr;
    volatile uint64_t val1;
    volatile uint64_t val2;
    struct timespec start, end;
    uint64_t elapsed_time = 0;

    uint64_t tmp1 = *v_addr_1;
    uint64_t tmp2 = *v_addr_2;
    // Clean up
    __asm__ volatile("DC CIVAC, %0" : : "r"(v_addr_1));
    __asm__ volatile("DC CIVAC, %0" : : "r"(v_addr_2));
    __asm__ volatile("ISB SY");
    __asm__ volatile("DSB SY");

    if (timing)
    {
        timespec_get(&start, TIME_UTC);
    }

    for (int i = 0; i < iter; i++)
    {
        __asm__ volatile("DC CIVAC, %0" : : "r"(v_addr_1));
        __asm__ volatile("DC CIVAC, %0" : : "r"(v_addr_2));
        __asm__ volatile("LDR %0, [%1]" : "=r"(val1) : "r"(v_addr_1));
        __asm__ volatile("LDR %0, [%1]" : "=r"(val2) : "r"(v_addr_2));
    }

    if (timing)
    {
        timespec_get(&end, TIME_UTC);
        elapsed_time = (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);
    }


    return elapsed_time;
}

/**
 * pattern_single - Executes a single hammering pattern.
 * @addr: The address tuple containing the virtual address to hammer.
 * @iter: The number of iterations to perform.
 * @timing: Boolean flag to enable timing measurement.
 *
 * Returns the elapsed time in nanoseconds if timing is enabled, otherwise 0.
 */
uint64_t pattern_single(addr_tuple addr, int iter, bool timing)
{
    return hammer_single(addr, iter, timing);
}

/**
 * pattern_single_decoy - Executes a single hammering pattern with a decoy address.
 * @addr: The address tuple containing the virtual address to hammer.
 * @buffer: The buffer containing potential decoy addresses.
 * @size: The size of the buffer.
 * @iter: The number of iterations to perform.
 * @timing: Boolean flag to enable timing measurement.
 *
 * Returns the elapsed time in nanoseconds if timing is enabled, otherwise 0.
 */
uint64_t pattern_single_decoy(addr_tuple addr, uint64_t *buffer, size_t size, int iter, bool timing)
{
    addr_tuple decoy = addr;
    while (is_possibly_same_row(addr, decoy))
    {
        decoy = gen_random_addr(buffer, size);
    }

    return hammer_double(addr, decoy, iter, timing);
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
 *
 * Returns the elapsed time in nanoseconds if timing is enabled, otherwise 0.
 */
uint64_t pattern_quad(addr_tuple addr, uint64_t *buffer, size_t size, int iter, bool timing, pfn_va_t *map, size_t pfn_va_len)
{
    addr_tuple addr_n_plus = next_row(addr, map, pfn_va_len);
    addr_tuple addr_n_minus = prev_row(addr, map, pfn_va_len);
    if (addr_n_plus.v_addr == NULL || addr_n_minus.v_addr == NULL)
    {
        return 0;
    }
    addr_tuple addr_f_plus = next_row(addr_n_plus, map, pfn_va_len);
    addr_tuple addr_f_minus = prev_row(addr_n_minus, map, pfn_va_len);
    if (addr_f_plus.v_addr == NULL || addr_f_minus.v_addr == NULL)
    {
        return 0;
    }

    if (addr_f_minus.v_addr > buffer && addr_f_plus.v_addr < buffer + size)
    {
        return hammer_double(addr_f_plus, addr_f_minus, iter, timing);
    }
    else
    {
        return 0;
    }
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

    for (size_t i = 0; i < word_count; i++)
    {
        uint64_t byte = buffer[i];
        uint64_t addr = (uint64_t)(&buffer[i]);
        uint64_t expected = pattern(addr);
        if (expected != byte)
        {
            if (bitflip_count >= 256)
            {
                break;
            }

            uint8_t diff = expected ^ byte;
            for (int j = 0; j < 8; j++)
            {
                if (diff & (1 << j))
                {
                    if (bitflip_count >= 256)
                    {
                        break;
                    }
                    bitflips[bitflip_count].direction = (byte & (1 << j)) ? 1 : 0;
                    bitflips[bitflip_count].addr.v_addr = addr;
                    bitflips[bitflip_count].addr.p_addr = get_phys_addr(addr);
                    bitflips[bitflip_count].bit_pos = j;
                    bitflips[bitflip_count].expected = expected;
                    bitflips[bitflip_count].actual = byte;
                    bitflip_count++;
                }
            }
        }
        // Correct the bitflip in the buffer
        if (bitflip_count < 256)
        {
            buffer[i] = expected;
        }
        else
        {
            break;
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
 * focused_detect_bitflips - Detects bitflips in a focused region of a buffer.
 * @buffer: The buffer to check for bitflips.
 * @size: The size of the buffer in bytes.
 * @pattern: The pattern function used to generate expected values.
 * @addr: The address tuple to focus on.
 * @map: The page frame number to virtual address mapping.
 * @pfn_va_len: The length of the mapping.
 *
 * Returns an array of detected bitflips, with a maximum of 256 entries.
 */
bitflip *focused_detect_bitflips(uint64_t *buffer, size_t size, pattern_func pattern, addr_tuple addr, pfn_va_t *map, size_t pfn_va_len)
{
    bitflip *bitflips = (bitflip *)calloc(256, sizeof(bitflip));
    int bitflip_count = 0;

    /* initialise every struct to {NULL,0} */
    addr_tuple addr_n_plus = {0}, addr_f_plus = {0}, addr_vf_plus = {0};
    addr_tuple addr_n_minus = {0}, addr_f_minus = {0}, addr_vf_minus = {0};

    /* first neighbours */
    addr_n_plus = next_row(addr, map, pfn_va_len);
    addr_n_minus = prev_row(addr, map, pfn_va_len);

    /* second neighbours  (+2 / –2) */
    if (addr_n_plus.v_addr)
        addr_f_plus = next_row(addr_n_plus, map, pfn_va_len);
    if (addr_n_minus.v_addr)
        addr_f_minus = prev_row(addr_n_minus, map, pfn_va_len);

    /* third neighbours  (+3 / –3) */
    if (addr_f_plus.v_addr)
        addr_vf_plus = next_row(addr_f_plus, map, pfn_va_len);
    if (addr_f_minus.v_addr)
        addr_vf_minus = prev_row(addr_f_minus, map, pfn_va_len);

    // Get the smallest valid pointer
    addr_tuple *candidates[] = {&addr_vf_minus, &addr_f_minus, &addr_n_minus, &addr};
    uint64_t *smallest = NULL;

    for (int i = 0; i < 4; i++)
    {
        if (candidates[i]->v_addr != NULL)
        {
            smallest = candidates[i]->v_addr;
            break;
        }
    }

    // Get the largest valid pointer
    addr_tuple *candidates2[] = {&addr_vf_plus, &addr_f_plus, &addr_n_plus, &addr};
    uint64_t *biggest = NULL;
    for (int i = 0; i < 4; i++)
    {
        if (candidates2[i]->v_addr != NULL)
        {
            biggest = candidates2[i]->v_addr;
            break;
        }
    }
    printf("Smallest: %p, Biggest: %p\n", smallest, biggest);
    if (smallest == NULL || biggest == NULL)
    {
        return NULL;
    }

    // Check the pointers on all words between the smallest and biggest
    if (smallest < buffer)
    {
        smallest = buffer;
    }
    if (biggest > buffer + size)
    {
        biggest = buffer + size;
    }
    size_t word_count = biggest - smallest;
    for (size_t i = 0; i <= word_count; i++)
    {
        uint64_t byte = smallest[i];
        printf("Byte: %lx\n", byte);
        uint64_t addr = (uint64_t)(&smallest[i]);
        uint64_t expected = pattern(addr);
        if (expected != byte)
        {
            if (bitflip_count >= 256)
            {
                break;
            }

            uint64_t diff = expected ^ byte;
            for (int j = 0; j < 64; j++)
            {
                if (diff & (1 << j))
                {
                    if (bitflip_count >= 256)
                    {
                        break;
                    }
                    bitflips[bitflip_count].direction = (byte & (1 << j)) ? 1 : 0;
                    bitflips[bitflip_count].addr.v_addr = addr;
                    bitflips[bitflip_count].addr.p_addr = get_phys_addr(addr);
                    bitflips[bitflip_count].bit_pos = j;
                    bitflips[bitflip_count].expected = expected;
                    bitflips[bitflip_count].actual = byte;
                    bitflip_count++;
                }
            }
            // Correct the bitflip in the buffer
            smallest[i] = expected;
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
    uint64_t flags = MAP_PRIVATE | MAP_ANONYMOUS;

    if (type == HUGEPAGE_2MB)
        flags |= MAP_HUGETLB | MAP_HUGE_2MB;

    uint64_t *buffer = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (buffer == MAP_FAILED)
    {
        perror("mmap");
        return NULL;
    }

    for (size_t i = 0; i < count; ++i)
    {
        uint64_t byte_addr = (uint64_t)buffer + i * sizeof(uint64_t);
        buffer[i] = (uint64_t)pattern(byte_addr);
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

    printf("\r[%.*s%.*s] %5.1f %%   flips: %zu",
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
 */
void bitflip_test(size_t buffer_size, buffer_type b_type, pattern_func pattern, hammer_pattern hammer_pattern, bool timing, int iter, int hammer_iter, char *output_file)
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
            fflush(file);
        }
    }
    uint64_t *buffer = buffer_init(buffer_size, b_type, pattern);
    buffer_init_check(buffer, buffer_size, pattern);

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
    instructions_timing_test(gen_random_addr(buffer, buffer_size), 100000);
    int total_flips = 0;
    int i = 0;
    int progress = 0;
    progress_bar(0, iter, total_flips);

    struct timespec start, end;

    timespec_get(&start, TIME_UTC);
    int valid_iter = 0;
    int hammer_time = 0; // To calculate average time for hammering

    for (i = 0; i < iter; i++)
    {
        addr_tuple addr = gen_random_addr(buffer, buffer_size);
        uint64_t iter_time;  // To calculate time for each iteration

        switch (hammer_pattern)
        {
        case PATTERN_SINGLE:
            iter_time = pattern_single(addr, hammer_iter, timing);
            if (iter_time > 0)
            {
                hammer_time += iter_time;
            }
            break;
        case PATTERN_QUAD:
            iter_time = pattern_quad(addr, buffer, buffer_size, hammer_iter, timing, pmap, pmap_len) / 2;
            if (iter_time > 0)
            {
                hammer_time += iter_time;
            }
            break;
        default:
        case PATTERN_SINGLE_DECOY:
            iter_time = pattern_single_decoy(addr, buffer, buffer_size, hammer_iter, timing) / 2;
            if (iter_time > 0)
            {
                hammer_time += iter_time;
            }
            break;
        }
        bitflip *bitflips = detect_bitflips(buffer, buffer_size, pattern);
        for (int j = 0; j < 256; j++)
        {
            if (bitflips[j].addr.v_addr != NULL)
            {
                if (file != NULL)
                {
                    // For single and decoy patterns
                    fprintf(file, "Iter: %d, Aggr_v: %p, Aggr_p: %lx, Virtual: %p, Physical: %lx, Expected: %x, Actual: %x, Bit_pos: %d\n",
                            i, addr.v_addr, addr.p_addr,
                            bitflips[j].addr.v_addr, bitflips[j].addr.p_addr,
                            bitflips[j].expected, bitflips[j].actual, bitflips[j].bit_pos);
                    fflush(file);
                }
                total_flips++;
            }
        }
        if (file != NULL)
        {
            fclose(file);
            file = NULL;
        }
        free(bitflips);
        progress++;
        if (progress >= iter / 1000)
        {
            timespec_get(&end, TIME_UTC);
            double elapsed_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
            double time_per_iter = elapsed_time / progress;
            int remaining_iter = iter - i;
            double remaining_time = time_per_iter * remaining_iter;
            progress_bar(i, iter, total_flips);
            if (timing == 1)
            {
                printf("\t Average time of access: %d ns", hammer_time / progress / hammer_iter);
            }
            printf("\t ETA : %dh %dmin %dsec", (int)(remaining_time / 3600), (int)((int)remaining_time % 3600) / 60, (int)remaining_time % 60);
            progress = 0;
            hammer_time = 0;
            timespec_get(&start, TIME_UTC);
        }
    }
    munmap(buffer, buffer_size);
    free(pmap);
}
