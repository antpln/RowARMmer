# Makefile – Jetson‑Nano Rowhammer test‑bed
# -----------------------------------------
# Source layout
#   src/      → all *.c implementation files
#   include/  → all project headers
#
# Targets
#   make            → build hammer_test
#   make run        → build & run with defaults
#   make huge2m     → prepare a hugetlbfs (2 MiB pages) mount
#   make clean      → remove objects + binary
#
# Compilation flags are tuned so the explicit DC/LD hammer accesses CANNOT be
# vectorised, inlined or optimised away, yet we still keep -O2 for the rest.
# ---------------------------------------------------------------------------

### directories ---------------------------------------------------------------
SRC_DIR   := src
INC_DIR   := include

### auto‑discover sources ------------------------------------------------------
SRCS      := $(wildcard $(SRC_DIR)/*.c)
OBJS      := $(SRCS:$(SRC_DIR)/%.c=$(SRC_DIR)/%.o)
TARGET    := hammer_test

### toolchain + flags ----------------------------------------------------------
CC        ?= gcc
INC       := -I$(INC_DIR)

CFLAGS    := -std=c11 -O2 \
             -Wall -Wextra\
             -fno-inline -fno-tree-vectorize -fno-tree-slp-vectorize \
             -fno-aggressive-loop-optimizations -fno-builtin \
             -fno-strict-aliasing -fno-omit-frame-pointer \
             -D_GNU_SOURCE $(INC)

LDFLAGS   :=

.PHONY: all clean run huge2m

### build rules ----------------------------------------------------------------
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# compile each .c in src/ into a matching .o next to it
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

### convenience ----------------------------------------------------------------
run: $(TARGET)
	./$(TARGET)

### hugetlbfs helper ----------------------------------------------------------
NR ?= 1024
MOUNT_DIR ?= /mnt/huge_2M
huge2m:
	sudo echo $(NR) > /proc/sys/vm/nr_hugepages

### enable PMU helper --------------------------------------------------------
enable_pmu:
	sudo bash /enable_arm_pmu/load-module

### clean ---------------------------------------------------------------------
clean:
	rm -f $(OBJS) $(TARGET)
