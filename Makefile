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

### architecture detection ------------------------------------------------------
ARCH := $(shell uname -m)
DEVICE := $(shell cat /proc/device-tree/model 2>/dev/null || echo "unknown")

### device-specific definitions ------------------------------------------------
ifeq ($(findstring Jetson Nano,$(DEVICE)),Jetson Nano)
    DEVICE_FLAGS := -DJETSON_NANO
else ifeq ($(findstring Raspberry Pi 3 Model B Plus,$(DEVICE)),Raspberry Pi 3 Model B Plus)
    DEVICE_FLAGS := -DRPI3
else ifeq ($(findstring Raspberry Pi 4,$(DEVICE)),Raspberry Pi 4)
    DEVICE_FLAGS := -DRPI4
else
    $(error "Unsupported device: $(DEVICE)")
endif

$(info Device detected: $(DEVICE))

### toolchain + flags ----------------------------------------------------------
CC        ?= gcc
INC       := -I$(INC_DIR) -IPTEditor

CFLAGS    := -std=gnu99 -O2 \
             -Wall -Wextra\
			 -Wno-unused-function \
             -fno-inline -fno-tree-vectorize -fno-tree-slp-vectorize \
             -fno-aggressive-loop-optimizations -fno-builtin \
             -fno-strict-aliasing -fno-omit-frame-pointer \
             -D_GNU_SOURCE $(INC) $(DEVICE_FLAGS)
LDFLAGS :=

.PHONY: all clean run huge2m enable_pmu prepare

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
	for X in $$(seq 0 3); do \
  		for Y in $$(seq 0 1); do \
    		echo 1 | sudo tee /sys/devices/system/cpu/cpu$$X/cpuidle/state$$Y/disable > /dev/null; \
  		done; \
	done; \
	cd enable_arm_pmu && make && (sudo bash load-module || echo "PMU module already loaded") && cd ..

pteditor:
	cd PTEditor && make all && (sudo insmod module/pteditor.ko || echo "PTEditor module already loaded") && cd ..

### prepare -------------------------------------------------------------------
prepare:
	mkdir logs || echo "logs directory already exists"
	sudo make enable_pmu
	sudo make huge2m

### clean ---------------------------------------------------------------------
clean:
	rm -f $(OBJS) $(TARGET)

