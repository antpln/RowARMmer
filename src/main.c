// main.c – entry point for Jetson‑Nano Rowhammer test‑bed
// ---------------------------------------------------------
// Added support for a **random fill pattern** whose bytes are reproducible
// through a user‑selectable 64‑bit seed.  Invoke with
//     --pattern rand [--seed <hex/dec>]
// If no seed is supplied the program uses the current epoch seconds.
// The chosen seed is written to the output file so you can replay / verify.
// ------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <sched.h>
#include <unistd.h>

#include "utils.h"
#include "memory.h"
#include "hammer.h"

// ------------------------------------------------------------------
// Global seed for the random pattern (full 64‑bit)
uint64_t g_pattern_seed = 0ULL;
bool VERBOSE = false;

// SplitMix64 hash → one pseudorandom byte --------------------------------------
static inline uint8_t splitmix_byte(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x ^= (x >> 31);
    return (uint64_t)x;
}

// ------------------------------------------------------------------
// Fill‑pattern functions --------------------------------------------------------
static uint64_t pattern_const_aa(uint64_t addr)          { (void)addr; return 0xAAAAAAAA; }
static uint64_t pattern_const_55(uint64_t addr)          { (void)addr; return 0x55555555; }
static uint64_t pattern_address_parity(uint64_t addr)    { return (addr & 1) ? 0xAAAAAAAA : 0x55555555; }
static uint64_t pattern_random_byte(uint64_t addr)       { return (uint64_t)splitmix_byte(addr ^ g_pattern_seed); }

struct pat_entry { const char *name; pattern_func fn; };
static const struct pat_entry pattern_tbl[] = {
    { "aa",     pattern_const_aa        },
    { "55",     pattern_const_55        },
    { "parity", pattern_address_parity  },
    { "rand",   pattern_random_byte     },
    { NULL,      NULL }
};

static pattern_func lookup_pattern(const char *name)
{
    for (const struct pat_entry *p = pattern_tbl; p->name; ++p)
        if (!strcmp(name, p->name))
            return p->fn;
    fprintf(stderr, "Unknown pattern '%s' – falling back to aa.\n", name);
    return pattern_const_aa;
}

// ------------------------------------------------------------------
static hammer_pattern lookup_hammer(const char *name)
{
    if (!strcmp(name, "single"))        return PATTERN_SINGLE;
    if (!strcmp(name, "decoy"))         return PATTERN_SINGLE_DECOY;
    if (!strcmp(name, "quad"))          return PATTERN_QUAD;
    if (!strcmp(name, "double"))        return PATTERN_DOUBLE;
    fprintf(stderr, "Unknown hammer pattern '%s' – falling back to quad.\n", name);
    return PATTERN_QUAD;
}

static buffer_type lookup_buf(const char *name)
{
    if (!strcmp(name, "normal")) return STANDARD;
    if (!strcmp(name, "2M"))     return HUGEPAGE_2MB;
    if (!strcmp(name, "1G"))     return HUGEPAGE_1GB;
    fprintf(stderr, "Unknown buffer type '%s' – using normal mmap.\n", name);
    return STANDARD;
}

// ------------------------------------------------------------------
static void usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    puts("  -s, --size <MB>         buffer size in megabytes (default 32)");
    puts("  -i, --iter <N>          random hammer placements (default 1000)");
    puts("  -n, --hammer <N>        activations per placement (default 1000000)");
    puts("  -H, --hammer-pattern    single | decoy | quad | double (default quad)");
    puts("  -B, --buffer-type       normal | 2M | 1G       (default normal)");
    puts("  -P, --pattern           aa | 55 | parity | rand (default aa)");
    puts("  -S, --seed <hex/dec>    seed for rand pattern (default epoch time)");
    puts("  -v, --verbose           print flips to stdout as well");
    puts("  -u, --uncachable        make memory buffer uncachable");
    puts("  -o, --op-type <N>       operation type (0 = read, 1 = write, 2 = ZVA)");
    puts("  -c, --cache-op <N>      cache operation (0 = none, 1 = CIVAC, 2 = CVAC)");
    puts("  -d, --add-dsb <N>       add DSB after cache operation (0 = no, 1 = yes)");
    puts("  -h, --help              this message\n");
}

void pin_to_core(int core_id)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pid_t pid = getpid(); // or use syscall(SYS_gettid) for threads

    if (sched_setaffinity(pid, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("sched_setaffinity");
        exit(EXIT_FAILURE);
    }
    else {
        fprintf(stderr, "[ LOG ] - Pinned to core %d\n", core_id);
    }
}


int main(int argc, char *argv[])
{
    pin_to_core(3); // Pin to core 3
    /* ---- defaults ---- */
    size_t        size_mb      = 2;
    int           iter         = 10000;
    int           hammer_iter  = 1000000;
    buffer_type   btype        = STANDARD;
    hammer_pattern hpat        = PATTERN_SINGLE;
    pattern_func  pattern      = pattern_const_aa;
    const char   *pattern_name = "aa";
    bool          timing       = true;
    bool          verbose      = false;
    bool          seed_given   = false;
    bool          uncachable   = false;
    
    // New parameters for hammering strategy
    int           op_type      = 0; // Default: read operation (LDR)
    int           cache_op     = 0; // Default: no cache operation
    int           add_dsb      = 0; // Default: no DSB after cache op

    static struct option long_opts[] = {
        {"size",   required_argument, 0, 's'},
        {"iter",   required_argument, 0, 'i'},
        {"hammer", required_argument, 0, 'n'},
        {"hammer-pattern", required_argument, 0, 'H'},
        {"buffer-type",    required_argument, 0, 'B'},
        {"pattern",        required_argument, 0, 'P'},
        {"seed",           required_argument, 0, 'S'},
        {"verbose",no_argument,       0, 'v'},
        {"uncachable", no_argument,   0, 'u'},
        {"op-type", required_argument, 0, 'o'},
        {"cache-op", required_argument, 0, 'c'},
        {"add-dsb", required_argument, 0, 'd'},
        {"help",   no_argument,       0, 'h'},
        {0,0,0,0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "s:i:n:H:B:P:S:tvuo:c:d:h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 's': size_mb     = strtoul(optarg, NULL, 0); break;
            case 'i': iter        = atoi(optarg);            break;
            case 'n': hammer_iter = atoi(optarg);            break;
            case 'H': hpat        = lookup_hammer(optarg);   break;
            case 'B': btype       = lookup_buf(optarg);      break;
            case 'P': pattern_name= optarg;                  pattern = lookup_pattern(optarg); break;
            case 'S': g_pattern_seed = strtoull(optarg, NULL, 0); seed_given = true; break;
            case 't': timing      = true;                    break;
            case 'v': verbose     = true;                    VERBOSE = 1; break;
            case 'u': uncachable  = true;                    break;
            case 'o': op_type     = atoi(optarg);            break;
            case 'c': cache_op    = atoi(optarg);            break;
            case 'd': add_dsb     = atoi(optarg);            break;
            default : usage(argv[0]); return opt=='h'?0:1;
        }
    }

    if (!seed_given) g_pattern_seed = (uint64_t)time(NULL);

    /* ---- output filename ---- */
    char out_name[128] = "";
    {
        time_t now = time(NULL);
        struct tm tm; localtime_r(&now, &tm);
        strftime(out_name, sizeof(out_name), "logs/flips_%Y%m%d_%H%M%S.txt", &tm);
    }
    printf("Starting test: size=%zu MB, iter=%d, hammer=%d, pattern=%s, seed=0x%llx, HP=%d, uncachable=%d, file=%s\n",
           size_mb, iter, hammer_iter, pattern_name, (unsigned long long)g_pattern_seed, btype, uncachable, out_name);
    printf("Choosen strategy: op=%s, cache_op=%s, add_dsb=%s\n",
           op_type == 0 ? "LDR" : (op_type == 2 ? "ZVA" : "STR"),
           cache_op == 0 ? "none" : (cache_op == 1 ? "CIVAC" : "CVAC"),
           add_dsb ? "yes" : "no");
    /* ---- write header to file ---- */
    FILE *hdr = fopen(out_name, "w");
    if (hdr) {
        fclose(hdr);
    }
    else {
        fprintf(stderr, "Failed to open output file '%s' for writing.\n", out_name);
        return 1;
    }
    size_t buf_bytes = MB(size_mb);
    if(verbose) {
        printf("Starting...");
    }

    bitflip_test(buf_bytes, btype, pattern, hpat, true, iter, hammer_iter, out_name, uncachable, op_type, cache_op, add_dsb);

    puts("Done.");
    return 0;
}
