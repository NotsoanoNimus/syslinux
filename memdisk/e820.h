/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2001-2008 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * e820.h
 *
 * Common routines for e820 memory map management
 */

#include <stdint.h>

struct e820range {
    uint64_t start;
    uint32_t type;
    uint64_t length;
} __attribute__ ((packed));

extern struct e820range ranges[];
extern int nranges;
extern uint32_t dos_mem, low_mem, high_mem;

extern void e820map_init(void);
extern void get_mem(void);
extern void parse_mem(void);
uint8_t *do_e820_malloc(uint32_t length, uint32_t type);
void e820_shift_bounds(uint8_t *at, uint32_t length);
void e820_dump_ranges(void);
void e820_insert_range(uint64_t start, uint64_t length, uint32_t type);
