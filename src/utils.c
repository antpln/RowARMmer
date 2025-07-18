#include <stdio.h>
#include "utils.h"
#include <fcntl.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <ptedit_header.h>




/**
 * build_pfn_map - Builds a mapping of page frame numbers (PFNs) to virtual addresses.
 * @buf: The buffer to map.
 * @bytes: The size of the buffer in bytes.
 * @out_n: Pointer to store the number of pages mapped.
 *
 * Returns a dynamically allocated array of PFN-to-VA mappings.
 */
pfn_va_t *build_pfn_map(void *buf, size_t bytes, size_t *out_n)
{
    int pg = open("/proc/self/pagemap", O_RDONLY);
    assert(pg >= 0);

    size_t pages = bytes >> 12;                 /* 4 KiB pages   */
    pfn_va_t *map = calloc(pages, sizeof *map); /* dense vector  */

    for (size_t i = 0; i < pages; ++i)
    {
        uint64_t va = (uint64_t)buf + (i << 12);
        uint64_t entry;
        if (pread(pg, &entry, sizeof entry, (va >> 12) * 8) != sizeof(entry))
        {
            perror("Failed to read entry from pagemap");
            exit(EXIT_FAILURE);
        }
        uint64_t pfn = entry & ((1ULL << 55) - 1);
        map[i].pfn = pfn;
        map[i].va = va & ~0xFFFULL;
    }
    close(pg);
    *out_n = pages;
    return map;
}

/**
 * pa_to_va - Converts a physical address to a virtual address using a PFN-to-VA map.
 * @pa: The physical address to convert.
 * @map: The PFN-to-VA mapping.
 * @n: The number of entries in the mapping.
 *
 * Returns the corresponding virtual address, or NULL if not found.
 */
void *pa_to_va(uint64_t pa, pfn_va_t *map, size_t n)
{
    uint64_t pfn = pa >> 12;
    uint64_t off = pa & 0xFFF;

    /* linear search is fine for ≤ a few thousand pages */
    for (size_t i = 0; i < n; ++i)
        if (map[i].pfn == pfn)
            return (void *)(map[i].va + off);

    return NULL; /* PFN not found or table stale */
}

/**
 * get_pfn - Extracts the page frame number (PFN) from a pagemap entry.
 * @entry: The pagemap entry.
 *
 * Returns the PFN extracted from the entry.
 */
uint64_t get_pfn(uint64_t entry)
{
    return entry & ((1ULL << 55) - 1); // Bits 0–54
}

/**
 * get_phys_addr - Retrieves the physical address corresponding to a virtual address.
 * @v_addr: The virtual address to convert.
 *
 * Returns the physical address, or exits the program on failure.
 */
uint64_t get_phys_addr(uint64_t v_addr)
{
    uint64_t entry;
    uint64_t offset = (v_addr / 4096) * sizeof(entry);
    uint64_t pfn;
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0)
    {
        printf("Error: Failed to open /proc/self/pagemap. Are you running as root ?\n");
        perror("Failed to open /proc/self/pagemap");
        exit(EXIT_FAILURE);
    }
    int bytes_read = pread(fd, &entry, sizeof(entry), offset);
    close(fd);
    if (bytes_read != 8)
    {
        fprintf(stderr, "Error: Failed to read 8 bytes from /proc/self/pagemap. Are you running as root ?\n");
        exit(EXIT_FAILURE);
    }
    if (!(entry & (1ULL << 63)))
    {
        fprintf(stderr, "Error: Page is not present in memory\n");
        exit(EXIT_FAILURE);
    }
    pfn = get_pfn(entry);
    if (pfn == 0)
    {
        fprintf(stderr, "Error: PFN is zero, invalid physical address\n");
        exit(EXIT_FAILURE);
    }
    return (pfn * 4096) | (v_addr & 4095);
}

/**
 * gen_addr_tuple - Generates an address tuple containing both virtual and physical addresses.
 * @v_addr: The virtual address.
 *
 * Returns the generated address tuple.
 */
addr_tuple gen_addr_tuple(uint64_t *v_addr)
{
    return (addr_tuple){v_addr, get_phys_addr((uint64_t)v_addr)};
}

/**
 * gen_random_addr - Generates a random address tuple within a buffer.
 * @buffer: The buffer to generate the address from.
 * @size: The size of the buffer in bytes.
 *
 * Returns the generated random address tuple.
 */
addr_tuple gen_random_addr(uint64_t *buffer, size_t size)
{
    size_t alignment = 64; // Align to 64 bytes
    size_t aligned_size = size & ~(alignment - 1);
    if (aligned_size == 0)
    {
        fprintf(stderr, "Error: Buffer size is too small for alignment\n");
        exit(EXIT_FAILURE);
    }

    size_t random_offset = (rand() % (aligned_size / alignment)) * alignment;
    uint64_t *random_addr = (uint64_t *)((char *)buffer + random_offset);

    return gen_addr_tuple(random_addr);
}

/**
 * hweight64 - Counts the number of set bits (1s) in a 64-bit integer.
 * @x: The 64-bit integer.
 *
 * Returns the number of set bits.
 */
int hweight64(uint64_t x)
{
    int res = 0;
    while (x)
    {
        res += x & 1;
        x >>= 1;
    }
    return res;
}

/**
 * parity64 - Computes the parity of a 64-bit integer.
 * @x: The 64-bit integer.
 *
 * Returns 1 if the number of set bits is odd, otherwise 0.
 */
int parity64(uint64_t x)
{
    return hweight64(x) & 1;
}
size_t make_uncacheable(void *buffer_ptr)
{
    ptedit_init();
    //int uncachable_memory_type = ptedit_find_first_mt(PTEDIT_MT_UC);
    int uncachable_memory_type = 3;
    ptedit_entry_t page_table_entry = ptedit_resolve(buffer_ptr, 0);
    size_t original_page_descriptor = page_table_entry.pmd;
    page_table_entry.pmd = ptedit_apply_mt_huge(page_table_entry.pmd, uncachable_memory_type);
    page_table_entry.valid = PTEDIT_VALID_MASK_PMD;
    ptedit_update(buffer_ptr, 0, &page_table_entry);
    ptedit_cleanup();
    return original_page_descriptor;
}

size_t make_cacheable(void *buffer_ptr, size_t original_page_descriptor)
{
    ptedit_init();
    //int uncachable_memory_type = ptedit_find_first_mt(PTEDIT_MT_UC);
    ptedit_entry_t page_table_entry = ptedit_resolve(buffer_ptr, 0);
    page_table_entry.pmd = original_page_descriptor;
    page_table_entry.valid = PTEDIT_VALID_MASK_PMD;
    ptedit_update(buffer_ptr, 0, &page_table_entry);
    ptedit_cleanup();
    return original_page_descriptor;
}

size_t* make_uncacheable_multi(void *buffer_ptr, size_t size) {
    if (ptedit_init()) {
        fprintf(stderr, "Failed to initialize ptedit\n");
        return NULL;
    }

    /*int uc_mt = ptedit_find_first_mt(PTEDIT_MT_UC);
    if (uc_mt == -1) {
        fprintf(stderr, "No UC memory type available\n");
        ptedit_cleanup();
        return NULL;
    }*/
    int uc_mt = 3;

    size_t huge_page_size = 2 * 1024 * 1024;
    size_t num_pages = size / huge_page_size;
    size_t* original_pmds = malloc(num_pages * sizeof(size_t));

    for (size_t i = 0; i < num_pages; ++i) {
        void* cur = (char*)buffer_ptr + i * huge_page_size;
        ptedit_entry_t entry = ptedit_resolve(cur, 0);
        original_pmds[i] = entry.pmd;

        entry.pmd = ptedit_apply_mt_huge(entry.pmd, uc_mt);
        entry.valid = PTEDIT_VALID_MASK_PMD;
        ptedit_update(cur, 0, &entry);
        ptedit_invalidate_tlb(cur);
    }

    ptedit_cleanup();
    return original_pmds;
}

void make_cacheable_multi(void *buffer_ptr, size_t size, size_t* original_pmds) {
    if (ptedit_init()) {
        fprintf(stderr, "Failed to initialize ptedit\n");
        return;
    }

    size_t huge_page_size = 2 * 1024 * 1024;
    size_t num_pages = size / huge_page_size;

    for (size_t i = 0; i < num_pages; ++i) {
        void* cur = (char*)buffer_ptr + i * huge_page_size;
        ptedit_entry_t entry = ptedit_resolve(cur, 0);
        entry.pmd = original_pmds[i];
        entry.valid = PTEDIT_VALID_MASK_PMD;
        ptedit_update(cur, 0, &entry);
        ptedit_invalidate_tlb(cur);
    }

    ptedit_cleanup();
}
