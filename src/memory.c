// memory.c – Row helpers for Tegra X1 (Jetson Nano, **4 GB** board)
// -----------------------------------------------------------------
// These helpers treat *any* two physical addresses that share
// the same 17‑bit row index as *possibly* the same DRAM row—even though they
// could still fall into different banks due to the hidden XOR hashing.

#include "memory.h"
#include "utils.h"
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

#if defined(JETSON_NANO)
#define COL_MASK     0b00000000000000000000110001111011100
#define BANK_MASK    0b00000000000000000000001110000000000
#define BANK_OFFSET  10
#define SUBPART_MASK 0b00000000000000000000000000000100000
#define ROW_MASK     0b00001111111111111111000000000000000
#define ROW_OFFSET   15
#define DEVICE_MASK  0b11110000000000000000000000000000000
#define BANK_MASK_0 0x6e574400
#define BANK_MASK_1 0x39722800
#define BANK_MASK_2 0x4b9c1000
#define CHANNEL_MASK 0xffff2400
#elif defined (RPI3)
#define COL_MASK  0b000000000000000001111111111111
#define ROW_MASK  0b111111111111110000000000000000
#define BANK_MASK 0b000000000000001110000000000000
#define ROW_OFFSET 16
#define BANK_OFFSET 13
#elif defined (RPI4)
#define COL_MASK  0b000000000000000000000011111111111
#define ROW_MASK  0b011111111111111111000000000000000
#define BANK_MASK 0b000000000000000000111100000000000
#define ROW_OFFSET 15
#define BANK_OFFSET 11
#else
#define COL_MASK     0b00000000000000000000110001111011100
#define SUBPART_MASK 0b00000000000000000000000000000100000
#define ROW_MASK     0b00001111111111111111000000000000000
#define ROW_OFFSET   15
#define BANK_MASK    0b00000000000000000000001110000000000
#define BANK_OFFSET  10
#define DEVICE_MASK  0b11110000000000000000000000000000000
#define BANK_MASK_0 0x6e574400
#define BANK_MASK_1 0x39722800
#define BANK_MASK_2 0x4b9c1000
#define CHANNEL_MASK 0xffff2400
#endif


/**
 * get_row_bits - Extracts the row bits from a physical address.
 * @addr: The address tuple containing the physical address.
 *
 * Returns the row bits extracted from the physical address.
 */
uint64_t get_row_bits(addr_tuple addr)
{
    return (addr.p_addr & ROW_MASK) >> ROW_OFFSET;
}

/**
 * change_row_bits - Modifies the row bits of a physical address.
 * @addr: The address tuple containing the physical address to modify.
 * @row_bits: The new row bits to set.
 * @map: The page frame number to virtual address mapping.
 * @pfn_va_len: The length of the mapping.
 *
 * Returns the updated address tuple with the modified row bits, or an invalid address tuple if the operation fails.
 */
addr_tuple change_row_bits(addr_tuple addr, uint64_t row_bits, pfn_va_t *map, size_t pfn_va_len) {
    uint64_t new_p_addr = addr.p_addr & (~ROW_MASK);
    new_p_addr |= (row_bits << ROW_OFFSET);
    addr_tuple new_addr = { .v_addr = NULL, .p_addr = new_p_addr };
    new_addr.p_addr = new_p_addr;
    new_addr.v_addr = pa_to_va(new_p_addr, map, pfn_va_len);
    return new_addr;
}

/**
 * next_row - Computes the address tuple for the next possible row.
 * @addr: The address tuple containing the current physical address.
 * @map: The page frame number to virtual address mapping.
 * @pfn_va_len: The length of the mapping.
 *
 * Returns the address tuple for the next possible row, or an invalid address tuple if the operation fails.
 */
addr_tuple next_row(addr_tuple addr, pfn_va_t *map, size_t pfn_va_len) {
    uint64_t row_bits = get_row_bits(addr);
    // Guard against overflow
    if (row_bits == ROW_MASK >> ROW_OFFSET) {
        row_bits = 0;
    } else {
        row_bits++;
    }
    // The resulting address is possibly in the next row due to bank hashing
    return change_row_bits(addr, row_bits, map, pfn_va_len);
}

/**
 * prev_row - Computes the address tuple for the previous possible row.
 * @addr: The address tuple containing the current physical address.
 * @map: The page frame number to virtual address mapping.
 * @pfn_va_len: The length of the mapping.
 *
 * Returns the address tuple for the previous possible row, or an invalid address tuple if the operation fails.
 */
addr_tuple prev_row(addr_tuple addr, pfn_va_t *map, size_t pfn_va_len) {
    uint64_t row_bits = get_row_bits(addr);
    if (row_bits == 0) {
        row_bits = ROW_MASK >> ROW_OFFSET;
    } else {
        row_bits--;
    }
    // The resulting address is possibly in the previous row due to bank hashing
    return change_row_bits(addr, row_bits, map, pfn_va_len);
}

/**
 * is_possibly_same_row - Checks if two addresses are possibly in the same DRAM row.
 * @addr1: The first address tuple.
 * @addr2: The second address tuple.
 *
 * Returns true if the two addresses share the same row bits, otherwise false.
 */
bool is_possibly_same_row(addr_tuple addr1, addr_tuple addr2) {
    return (get_row_bits(addr1) == get_row_bits(addr2));
}

uint64_t get_bank_bits(uint64_t addr)
{
    return (addr & BANK_MASK) >> BANK_OFFSET;
}

addr_tuple change_bank_bits(addr_tuple addr, uint8_t bank_bits, pfn_va_t *map, size_t pfn_va_len) {
    uint64_t new_p_addr = addr.p_addr & (~BANK_MASK);
    new_p_addr |= (bank_bits << BANK_OFFSET);
    addr.p_addr = new_p_addr;
    addr.v_addr = pa_to_va(new_p_addr, map, pfn_va_len);
    if (addr.v_addr == NULL) {
        addr_tuple invalid_addr = { .v_addr = NULL, .p_addr = new_p_addr };
        return invalid_addr; // Return an invalid addr_tuple if pa_to_va fails
    }
    return addr;
}

addr_tuple next_row_deterministic(addr_tuple addr, pfn_va_t *map, size_t pfn_va_len) {
    uint64_t row_bits = get_row_bits(addr);
    // Guard against overflow
    if (row_bits == (ROW_MASK >> ROW_OFFSET)) {
        addr_tuple invalid = { .v_addr = NULL, .p_addr = 0 };
        return invalid; 
    }
    addr_tuple new_addr = change_row_bits(addr, row_bits + 1, map, pfn_va_len);
    for(int i = 0; i < 8; i++) {
        new_addr = change_bank_bits(new_addr, i, map, pfn_va_len);
        if (new_addr.v_addr != NULL && get_bank(new_addr.p_addr) == get_bank(addr.p_addr) && get_channel(new_addr.p_addr) == get_channel(addr.p_addr)) {
            return new_addr;
        }
    }
    addr_tuple invalid_addr = { .v_addr = NULL, .p_addr = new_addr.p_addr };
    return new_addr;
}

addr_tuple prev_row_deterministic(addr_tuple addr, pfn_va_t *map, size_t pfn_va_len) {
    uint64_t row_bits = get_row_bits(addr);
    addr_tuple invalid_addr = { .v_addr = NULL, .p_addr = 0 };
    // Guard against underflow
    if (row_bits == 0) {
        addr_tuple invalid = { .v_addr = NULL, .p_addr = 0 };
        return invalid; 
    }
    addr_tuple new_addr = change_row_bits(addr, row_bits-1, map, pfn_va_len);
    for(int i = 0; i < 8; i++) {
        new_addr = change_bank_bits(new_addr, i, map, pfn_va_len);
        if (new_addr.v_addr != NULL && get_bank(new_addr.p_addr) == get_bank(addr.p_addr) && get_channel(new_addr.p_addr) == get_channel(addr.p_addr)) {
            return new_addr;
        }
    }
    
    return invalid_addr;
}

uint64_t get_bank(uint64_t addr) {
    
    #if defined(JETSON_NANO)
    uint8_t b0 = parity64(addr & BANK_MASK_0);  // LSB
    uint8_t b1 = parity64(addr & BANK_MASK_1);  // MID
    uint8_t b2 = parity64(addr & BANK_MASK_2);  // MSB
    return (b2 << 2) | (b1 << 1) | b0;
    
    #elif defined(RPI3)
    uint64_t bit13 = (phys_addr >> 13) & 1;
    uint64_t bit14 = (phys_addr >> 14) & 1;
    uint64_t bit15 = (phys_addr >> 15) & 1;

    uint64_t bank_bit0 = bit13 ^ bit14;
    uint64_t bank_bit1 = bit14;
    uint64_t bank_bit2 = bit15;
    #elif defined(RPI4)
    uint64_t bit11 = (phys_addr >> 11) & 1;
    uint64_t bit12 = (phys_addr >> 12) & 1;
    uint64_t bit13 = (phys_addr >> 13) & 1;
    uint64_t bit14 = (phys_addr >> 14) & 1;
    uint64_t bank_bit0 =  bit11 ^ bit12;
    uint64_t bank_bit1 = bit13;
    uint64_t bank_bit2 = bit14;
    return (bank_bit2 << 2) | (bank_bit1 << 1) | bank_bit0;
    #else 
    return 0;
    #endif
}

uint64_t get_row(uint64_t addr)
{
    return (addr & ROW_MASK) >> ROW_OFFSET;
}

uint64_t get_column(uint64_t addr)
{
    #if defined(JETSON_NANO)
    uint64_t p1 = (addr >> 2) & 0b111;
    uint64_t p2 = (addr >> 6) & 0b1111;
    uint64_t p3 = (addr >> 13) & 0b11;
    return p1 | (p2 << 3) | (p3 << 7);
    #else
    return (addr & COL_MASK);
    #endif
}

uint64_t get_channel(uint64_t addr)
{
    #if defined(JETSON_NANO)
    return parity64(addr & CHANNEL_MASK);
    #elif defined(RPI4)
    return addr >> 32;
    #else
    return 0;
    #endif
}

uint64_t get_subpartition(uint64_t addr) {
    #if defined(JETSON_NANO)
    return (addr >> 5) & 1;
    #else
    return 0;
    #endif
}

uint64_t get_device(uint64_t addr) {
    #if defined(JETSON_NANO)
    return (addr & DEVICE_MASK) >> 32;
    #else
    return 0;
    #endif
}

void print_location(addr_tuple addr)
{
    printf("Row: %d, Column: %d, Bank: %d, Channel: %d, Subpartition: %d, Physical addr: %lx\n", get_row(addr.p_addr), get_column(addr.p_addr), get_bank(addr.p_addr), get_channel(addr.p_addr), get_subpartition(addr.p_addr), addr.p_addr);
}


