/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2009-2011 Erwan Velu - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

#ifndef MEMDISK_ACPI_RSDT_H
#define MEMDISK_ACPI_RSDT_H

#include <inttypes.h>
#include <stdbool.h>

#include "structs.h"


#define RSDT_MAX_ENTRIES 256



enum { RSDT_TABLE_FOUND = 1};


typedef
struct {
    s_acpi_description_header_raw *address;
    s_acpi_description_header_raw *entry[RSDT_MAX_ENTRIES - 1];
    uint8_t entry_count;
    bool valid;
} s_rsdt;



#endif   /* MEMDISK_ACPI_RSDT_H */
