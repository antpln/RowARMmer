#include <stdio.h>
#include <include/utils.h>
#include <fcntl.h>

uint64_t get_phys_addr(uint64_t v_addr)
{
    uint64_t entry;
    uint64_t offset = (v_addr / 4096) * sizeof(entry);
    uint64_t pfn;
    int fd = open("/proc/self/pagemap", O_RDONLY);
    assert(fd >= 0);
    int bytes_read = pread(fd, &entry, sizeof(entry), offset);
    close(fd);
    assert(bytes_read == 8);
    assert(entry & (1ULL << 63));
    pfn = get_pfn(entry);
    assert(pfn != 0);
    return (pfn * 4096) | (v_addr & 4095);
}

//----------------------------------------------------------
addr_tuple gen_addr_tuple(char *v_addr)
{
    return (addr_tuple){v_addr, get_phys_addr((uint64_t)v_addr)};
}

addr_tuple gen_random_addr(char *buffer, size_t size)
{
    addr_tuple addr;
    uint64_t offset = (uint64_t)buffer + (rand() % size);
    addr.v_addr = (char *)offset;
    addr.p_addr = get_phys_addr(offset);
    return addr;
}

static inline uint64_t read_pmccntr(void)
{
    uint64_t val;
    asm volatile("mrs %0, pmccntr_el0" : "=r"(val));
    return val;
}

static inline int hweight64(uint64_t x)
{
    int res = 0;
    while (x)
    {
        res += x & 1;
        x >>= 1;
    }
    return res;
}

static inline int parity64(uint64_t x)
{
    return hweight64(x) & 1;
}