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

#define COL_MASK 0b00000000000000000001110001111011100
#define ROW_MASK 0b00011111111111111110000000000000000

/**
 * get_row_bits - Extracts the row bits from a physical address.
 * @addr: The address tuple containing the physical address.
 *
 * Returns the row bits extracted from the physical address.
 */
uint64_t get_row_bits(addr_tuple addr)
{
    return (addr.p_addr & ROW_MASK) >> 13;
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
    new_p_addr |= (row_bits << 13);
    addr.p_addr = new_p_addr;
    addr.v_addr = pa_to_va(new_p_addr, map, pfn_va_len);
    if (addr.v_addr == NULL) {
        addr_tuple invalid_addr = { .v_addr = NULL, .p_addr = 0 };
        return invalid_addr; // Return an invalid addr_tuple if pa_to_va fails
    }
    return addr;
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
    if (row_bits == 0b1111111111111111) {
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
        row_bits = 0b1111111111111111;
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