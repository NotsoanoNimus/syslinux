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

#ifndef ACPI_STRUCTS_H
#define ACPI_STRUCTS_H

#include <inttypes.h>
#include <stdbool.h>

#include "../compiler.h"



typedef
MEMDISK_PACKED_PREFIX
struct {
    uint8_t     signature[4];
    uint32_t    length;
    uint8_t     revision;
    uint8_t     checksum;
    uint8_t     oem_id[6];
    uint8_t     oem_table_id[8];
    uint32_t    oem_revision;
    uint8_t     creator_id[4];
    uint32_t    creator_revision;
} MEMDISK_PACKED_POSTFIX
s_acpi_description_header_raw;


typedef
MEMDISK_PACKED_PREFIX
struct {          
    uint32_t    Data1;
    uint16_t    Data2;
    uint16_t    Data3;
    uint8_t     Data4[8]; 
} MEMDISK_PACKED_POSTFIX
guid_t;



#endif   /* ACPI_STRUCTS_H */
