/**
 * @file nfit.h
 * @brief Structures for ACPI NFIT tables.
 *
 * @author Zack Puhl <zack@crows.dev>
 * @date 2024-10-21
 * 
 * @copyright Copyright (C) 2024 Zack Puhl
 * 
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 3.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see https://www.gnu.org/licenses/.
 */

#ifndef NFIT_H
#define NFIT_H

#include <inttypes.h>
#include <stdbool.h>

#include "structs.h"
#include "../compiler.h"


#define NFIT_TABLE_TYPE_SPA                 0
#define NFIT_TABLE_TYPE_REGION_MAPPING      1
#define NFIT_TABLE_TYPE_INTERLEAVE          2
#define NFIT_TABLE_TYPE_SMBIOS_MI           3
#define NFIT_TABLE_TYPE_CONTROL_REGION      4
#define NFIT_TABLE_TYPE_BLOCK_DATA_WINDOW   5
#define NFIT_TABLE_TYPE_FLUSH_HINT_ADDR     6
#define NFIT_TABLE_TYPE_PLATFORM_CAPABILITY 7

/* Types 8 and above are reserved per the specification. */
#define NFIT_TABLE_TYPE_RESERVED            8



/* NFIT sub-structures are defined as Type-Length-Value entries. */
typedef
MEMDISK_PACKED_PREFIX
struct {
    uint16_t        type;
    uint16_t        length;
} MEMDISK_PACKED_POSTFIX
nfit_typelen_t;

/* Right now, we only care about SPA entry types. */
typedef
MEMDISK_PACKED_PREFIX
struct {
    nfit_typelen_t  header;
    uint16_t        struct_index;
    uint16_t        flags;   /* See below. Only 3 flags exist. */
    uint32_t        reserved;
    uint32_t        srat_proximity_domain;
    guid_t          addr_range_type_guid;   /* See below */
    uint64_t        range_base;
    uint64_t        range_length;
    uint64_t        range_memory_mapping_attr;   /* See spec */
    /* The spec defines a Location Cookie item, but specifies that it's intended
        to be an "opaque" value set by firmware. When I removed the member, NFIT
        parsing began to work properly with `libnvdimm` on Linux, so I'm going to
        leave it be. */
    /* uint64_t        location_cookie; */
} MEMDISK_PACKED_POSTFIX
nfit_structure_spa_t;

#define NFIT_SPA_FLAG_CONTROL_FOR_HOTSWAP       (1 << 0)
#define NFIT_SPA_FLAG_PROXIMITY_DOMAIN_VALID    (1 << 1)
#define NFIT_SPA_FLAG_LOCATION_COOKIE_VALID     (1 << 2)

#define NFIT_SPA_GUID_PERSISTENT_MEMORY     { 0x66F0D379, 0xB4F3, 0x4074, { 0xAC, 0x43, 0x0D, 0x33, 0x18, 0xB7, 0x8C, 0xDB } }
#define NFIT_SPA_GUID_NVD_CONTROL_REGION    { 0x92F701F6, 0x13B4, 0x405D, { 0x91, 0x0B, 0x29, 0x93, 0x67, 0xE8, 0x23, 0x4C } }
#define NFIT_SPA_GUID_NVD_BLOCK_DATA_WINDOW { 0x91AF0530, 0x5D86, 0x470E, { 0xA6, 0xB0, 0x0A, 0x2D, 0xB9, 0x40, 0x82, 0x49 } }
#define NFIT_SPA_GUID_RAMDISK_VIRT_V_DISK   { 0x77AB535A, 0x45FC, 0x624B, { 0x55, 0x60, 0xF7, 0xB2, 0x81, 0xD1, 0xF9, 0x6E } }
#define NFIT_SPA_GUID_RAMDISK_VIRT_V_CD     { 0x3D5ABD30, 0x4175, 0x87CE, { 0x6D, 0x64, 0xD2, 0xAD, 0xE5, 0x23, 0xC4, 0xBB } }
#define NFIT_SPA_GUID_RAMDISK_VIRT_P_DISK   { 0x5CEA02C9, 0x4D07, 0x69D3, { 0x26, 0x9F, 0x44, 0x96, 0xFB, 0xE0, 0x96, 0xF9 } }
#define NFIT_SPA_GUID_RAMDISK_VIRT_P_CD     { 0x08018188, 0x42CD, 0xBB48, { 0x10, 0x0F, 0x53, 0x87, 0xD5, 0x3D, 0xED, 0x3D } }


/* The raw entry consists of a header, a small reserved DWORD, then a list
    of sub-structures to enumerate. See the ACPI specification here:
    https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/05_ACPI_Software_Programming_Model/ACPI_Software_Programming_Model.html#nvdimm-firmware-interface-table-nfit */
typedef
struct {
    s_acpi_description_header_raw   header;
    uint32_t                        reserved;
} nfit_raw_t;



#endif   /* NFIT_H */
