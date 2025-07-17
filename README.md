# ARMv8 Rowhammer Test-Bed

A Rowhammer Test-Bed for ARMv8 inspired by :

> Kogler, A. (*n.d.*). *Half-Double: Hammering From the Next Row Over*.  

This test-bed targets **LPDDR4** memory in **TRR-protected systems** like the NVIDIA **Jetson Nano 4GB**, exploiting Half-Double effects and bitflip detection to **reverse engineer physical-to-DRAM address mappings** (channel, bank, row, column).

## Features

- TRR-compliant Rowhammer patterns (`quad`, `decoy`, etc.)
- Bitflip logging and automatic victim row scanning
- Huge page support (2 MB or 1 GB)
- PMU-based timing and performance tracking
- Runtime configuration (pattern, timing, buffer size, etc.)
- Physical address translation via `/proc/self/pagemap`
- VA ↔ PA reverse-lookup with support for 64-bit PFNs

---

## Prerequisites

- NVIDIA Jetson Nano (4GB) or Raspberry Pi 3 Model B+ or Raspberry Pi 4 (4GB)
- Linux kernel headers installed
- GCC toolchain
- Git (for cloning dependencies)

## Setup

Initialize PTEditor (required for uncacheable memory):
```sh
make pteditor
```

## 🔧 Build

```sh
make
```

This will build the main binary: `hammer_test`

### Build Targets

| Command              | Description                                    |
|----------------------|------------------------------------------------|
| `make`               | Compile `hammer_test`                          |
| `make run`           | Run with default parameters                    |
| `make clean`         | Clean up all binaries and objects              |
| `make huge2m`        | Enable 2 MiB hugepages (`/proc/sys/vm/...`)    |
| `make enable_pmu`    | Disable idle states & load kernel PMU module   |
| `make prepare`       | Enable hugepages and PMU in one step           |

---

## Usage

```sh
./hammer_test [options]
```

### Options

| Option                  | Description                                                 |
|--------------------------|-------------------------------------------------------------|
| `-s`, `--size <MB>`     | Buffer size in megabytes (default: `32`)                   |
| `-i`, `--iter <N>`      | Number of random hammer placements (default: `1000`)       |
| `-n`, `--hammer <N>`    | Activations per placement (default: `1000000`)             |
| `-H`, `--hammer-pattern`| `single` \| `decoy` \| `quad` (default: `quad`)            |
| `-B`, `--buffer-type`   | `normal` \| `2M` \| `1G` (default: `normal`)               |
| `-P`, `--pattern`       | `aa` \| `55` \| `parity` \| `rand` (default: `aa`)          |
| `-S`, `--seed <value>`  | Seed for `rand` pattern (default: current epoch time)      |
| `-v`, `--verbose`       | Print bitflips to stdout                                   |
| `-u`, `--uncachable`     | Make the memory buffer uncachable (default: disabled)         |
| `-h`, `--help`          | Print this usage message                                   |

---

## Directory Structure

```
.
├── include/             # All header files
├── src/                 # All source files
├── enable_arm_pmu/      # Kernel module to expose PMU
├── logs/                # Output bitflip logs (created at runtime)
├── Makefile
└── README.md
```

---

## References

- Kogler, A. (s. d.). Half-Double : Hammering From the Next Row Over.
- Frigo, P., Vannacci, E., Hassan, H., Veen, V. van der, Mutlu, O., Giuffrida, C., Bos, H., & Razavi, K. (2020). TRRespass : Exploiting the Many Sides of Target Row Refresh (No. arXiv:2004.01807). arXiv. https://doi.org/10.48550/arXiv.2004.01807
- Zhang, Z., Zhan, Z., Balasubramanian, D., Koutsoukos, X., & Karsai, G. (2018). Triggering Rowhammer Hardware Faults on ARM : A Revisit. Proceedings of the 2018 Workshop on Attacks and Solutions in Hardware Security, 24‑33. https://doi.org/10.1145/3266444.3266454
- Tegra X1 Technical Reference Manual

---

## Disclaimer

This project is intended for **educational and research** purposes only.  
Do **not** run Rowhammer experiments on hardware you cannot afford to damage.
