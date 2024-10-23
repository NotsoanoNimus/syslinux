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
 * e820func.c
 *
 * E820 range database manager
 */

#include <stdint.h>

#ifdef TEST
#   include <string.h>
#else
#   include "memdisk.h"
#endif

#include "e820.h"
#include "conio.h"



#define MAXRANGES   256

/* Define a few static globals (kinda sucks; consider refactoring). */
struct e820range ranges[MAXRANGES];
int nranges;


extern const char _end[];		/* Symbol signalling end of data */


void e820map_init(void)
{
    memset(ranges, 0, sizeof(ranges));
    nranges = 0;
    ranges[0].type = -1U;
}


/* TODO! When inserting ranges directly, need to check whether it's interfering
    with a current entry (falls within start to start+length) and adjust the length
    properly. The difference between this and malloc is that the latter accepts a
    return position while this one directly provides an allocation location. */
void
e820_insert_range(uint64_t start,
                  uint64_t length,
                  uint32_t type)
{
    int i, j, k;
    uint64_t l;

    if ((nranges + 1) >= MAXRANGES) {
        die("E820:  Out of useable ranges!");
    }

    if (0 == length) return;

    for (i = 0; i < nranges; ++i) {
        /* Traverse the list until the current entry's start is above the request. */
        if (0 != start && start > ranges[i].start) continue;

        /* Copy all entries from `nranges` back to the current position
            forward by one place to make room. Since the `start` position is
            AHEAD of ranges[i].start, we only go to (i + 1) and that's what
            gets populated below. */
        for (j = nranges; j > (i + 1); --j) {
            memcpy(&ranges[j], &ranges[j-1], sizeof(struct e820range));
        }

        /* Move up to where we're going to put it. */
        ++i;
        break;
    }

    ranges[i].start = start;
    ranges[i].length = length;
    ranges[i].type = type;

    ++nranges;
    ranges[nranges].start = 0;
    ranges[nranges].length = 0;
    ranges[nranges].type = -1U;

    /* Exit if there's nothing to sort (empty or only 1 range). */
    if (nranges <= 1) return;

    /* Since we can never guarantee an ordered memory mapping, we should sort it. */
    for (k = 0; k < nranges; ++k) {
        for (j = k+1, i = nranges, l = ranges[k].start; j < nranges; ++j) {
            i = l > ranges[j].start ? j : i;   /* Select the minimum `start` index. */
        }

        if (i == nranges) continue;

        /* Swap using the end-range index as a placeholder. */
        memcpy(&ranges[nranges], &ranges[k], sizeof(struct e820range));   /* k -> nranges */
        memcpy(&ranges[k], &ranges[i], sizeof(struct e820range));   /* i -> k */
        memcpy(&ranges[i], &ranges[nranges], sizeof(struct e820range));   /* nranges(k) -> i */

        /* Reset the end of the range. */
        ranges[nranges].start = 0;
        ranges[nranges].length = 0;
        ranges[nranges].type = -1U;
    }
}


void
e820_shift_bounds(uint8_t *at,
                  uint32_t length)
{
    /* Search the e820 range from `ranges` and extend it. */
    for (int i = 0; i < nranges; ++i) {
        /* Always skip very high addresses >4G. */
        if ((uint32_t)(ranges[i].start >> 32) > 0) continue;

        uint32_t entry_end = ((uintptr_t)ranges[i].start + ranges[i].length);

        /* Is the range reserved? */
        if (
            (uintptr_t)at >= (uint32_t)(ranges[i].start)
            && (uintptr_t)at < entry_end
        ) {
            /* If it's the last range, we don't need to extend it. */
            if ((nranges-1) == i) continue;

            /* The range matches and this allocation already fits inside of it. */
            if (((uintptr_t)at + length) < entry_end) return;

            /* But... we can't let a resize take over another space. */
            if (((uintptr_t)at + length) > ranges[i+1].start) continue;

            /* If we get here, then the table's length seems to extend beyond the range. */
            printf(
                "\nE820: Shifting range extent from 0x%08p -> 0x%08p.\n",
                entry_end,
                ((uintptr_t)at + length)
            );

            ranges[i].length = ((uintptr_t)at + length) - ranges[i].start;

            return;
        }
    }
}


uint8_t *
do_e820_malloc(uint32_t length,
               uint32_t type)
{
    uint32_t i, startrange, endrange;

    if (!length) {
        return NULL;
    }

    /* printf("E820: Allocating %u bytes of type %u.\n", length, type); */

    /* Things that call `malloc` should prefer to work in higher ranges. */
    for (i = (nranges - 1); i > 0; --i) {
        if (ranges[i].type != 1) continue;

        endrange = (uint32_t)((uintptr_t)(ranges[i].start) + ranges[i].length);
        if ((endrange - length) < ranges[i].start) continue;

        ranges[i].length -= length;

        startrange = (endrange - length);
        e820_insert_range(startrange, length, type);
        parse_mem();

        return (uint8_t *)startrange;
    }

    return NULL;
}


void
e820_dump_ranges(void)
{
    for (int i = 0; i < nranges; ++i) {
        printf(
            "e820:  %08p%08p %08p%08p %u\n",
            (uint32_t)(ranges[i].start >> 32), (uint32_t)(ranges[i].start),
            (uint32_t)(ranges[i].length >> 32), (uint32_t)(ranges[i].length),
            ranges[i].type
        );
    }

    putchar('\n');
}
