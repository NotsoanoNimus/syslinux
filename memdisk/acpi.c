/**
 * @file acpi.c
 * @brief HEAVILY reworked from HDT methods in ../com32/gpllib/acpi.
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

#include "acpi/acpi.h"
#include "memdisk.h"
#include "conio.h"
#include "e820.h"



/* The memory gained by shrinking the ACPI meta structure down
    far outweighs the cost of this string array (by almost a whole
    order of magnitude, actually). */
const char *ACPI_SIGS[68] = {
    RSDP, RSDT, XSDT,
    DSDT, SSDT, PSDT,
    NFIT,
    APIC,
    BERT,
    BGRT,
    CPEP,
    ECDT,
    EINJ,
    ERST,
    FACP,
    FACS,
    FPDT,
    GTDT,
    HEST,
    MSCT,
    MPST,
    OEMx,
    PCCT,
    PHAT,
    PMTT,
    RASF,
    SBST,
    SDEV,
    SLIT,
    SRAT,
    AEST,
    BDAT,
    BOOT,
    CDIT,
    CEDT,
    CRAT,
    CSRT,
    DBGP,
    DBPG,
    DMAR,
    DRTM,
    ETDT,
    HPET,
    IBFT,
    IORT,
    IVRS,
    LPIT,
    MCFG,
    MCHI,
    MPAM,
    MSDM,
    PRMT,
    RGRT,
    SDEI,
    SLIC,
    SPCR,
    SPMI,
    STAO,
    SVKL,
    TCPA,
    TPM2,
    UEFI,
    WAET,
    WDAT,
    WDRT,
    WPBT,
    WSMT,
    XENV
};


static
bool
acpi_parse_header(s_acpi *acpi,
                  uint64_t *address)
{
    /* We only care about tracking very particular table types. */
    if (memcmp(address, NFIT, sizeof(NFIT) - 1) == 0) {
        acpi->nfit = (s_acpi_description_header_raw *)address;
    }

    /* PSDT have to be considered as SSDT. Intel ACPI Spec @ 5.2.11.3 */
    else if (
        0 == memcmp(address, SSDT, sizeof(SSDT) - 1)
        || 0 == memcmp(address, PSDT, sizeof(PSDT) - 1)
    ) {
        /* Expensive process, but scan byte-by-byte looking for a matching "ACPI0012" HID.
            This should find us the NVDIMM Root Device if one exists, which is all MEMDISK
            cares about. See Section 5.6.7 of the ACPI spec:
            https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html */

        /* First, we just look for "A". When we find one, we check all 8 bytes. */
        for (
            uintptr_t p = (uintptr_t)address + sizeof(s_acpi_description_header_raw);
            p < (uintptr_t)address + ((s_acpi_description_header_raw *)address)->length;
            ++p
        ) {
            if (0 != memcmp((void *)p, "A", 1)) continue;
            if (0 != memcmp((void *)p, "ACPI0012", 8)) continue;
            acpi->ssdt_nvdimm_root = (s_acpi_description_header_raw *)address;
            break;
        }
    }
    
    return true;
}


static
int
search_rsdp(s_acpi *acpi)
{
    for (
        uint8_t *q = (uint8_t *)RSDP_MIN_ADDRESS;
        q < (uint8_t *)RSDP_MAX_ADDRESS;
        q += 16   /* Signature is always on a 16-byte boundary */
    ) {
        /* Searching for RSDP with "RSD PTR " signature */
        if (0 != memcmp(q, RSDP, sizeof(RSDP) - 1)) continue;

        acpi->rsdp = (rsdp_raw_t *)q;

        acpi->rsdt.address = (s_acpi_description_header_raw *)acpi->rsdp->rsdt_address;
        acpi->xsdt.address = (s_acpi_description_header_raw *)acpi->rsdp->xsdt_address;

        return RSDP_TABLE_FOUND;
    }

    return -RSDP_TABLE_FOUND;
}


static
int
parse_rsdt(s_acpi *acpi)
{
    s_rsdt *r = &(acpi->rsdt);

    if (
        0 == r->address
        || 0 != memcmp(r->address, RSDT, sizeof(RSDT) - 1)
    ) {
        return -RSDT_TABLE_FOUND;
    }

    r->valid = true;

    for (
        uintptr_t p = ((uintptr_t)r->address + sizeof(s_acpi_description_header_raw));
        p < ((uintptr_t)r->address + r->address->length);
        p += sizeof(uint32_t)
    ) {
        if (acpi_parse_header(acpi, *((uint64_t **)p))) {
            if (r->entry_count >= (RSDT_MAX_ENTRIES-1)) continue;

            r->entry[r->entry_count] = (s_acpi_description_header_raw *)p;
            r->entry_count++;
        }
    }

    printf("   ok; parsed %u entries\n", r->entry_count);

    return RSDT_TABLE_FOUND;
}


static
int
parse_xsdt(s_acpi *acpi)
{
    s_xsdt *x = &(acpi->xsdt);

    if (
        0 == x->address
        || 0 != memcmp(x->address, XSDT, sizeof(XSDT) - 1)
    ) {
        return -XSDT_TABLE_FOUND;
    }

    x->valid = true;

    /* We need to move any adjacent table(s) OUT of the tail end of the XSDT.
        This will allow us to easily add extra entries without crashing the
        loaded kernel. We make space for new entries, but do not yet extend
        the XSDT's `length` value until we insert other SDTs (SSDT/NFIT/etc). */
    uint32_t extra_space  = sizeof(uint64_t) * 16;

    uint64_t **xsdt_start = (uint64_t **)((uintptr_t)x->address + sizeof(s_acpi_description_header_raw));
    uint64_t **xsdt_end   = (uint64_t **)((uintptr_t)x->address + x->address->length + extra_space);

    /* Iterate entries... */
    for (
        uint64_t **p = xsdt_start;
        p < (uint64_t **)((uintptr_t)x->address + x->address->length);
        ++p
    ) {
        /* If the table is located within the extra space we'd like to
            claim, then it needs to be relocated. */
        uint8_t *new_loc =
            acpi_relocate_table(acpi, (uint8_t **)p, (uint8_t *)xsdt_start, (uint8_t *)xsdt_end);

        if (-1U == new_loc) {
            puts("   ERROR: ACPI: Colliding table in XSDT could not be moved.\n");
            return -XSDT_TABLE_FOUND;
        }

        if (acpi_parse_header(acpi, (uint64_t *)(new_loc ? new_loc : *p))) {
            if (x->entry_count >= (XSDT_MAX_ENTRIES-1)) continue;

            x->entry[x->entry_count] = (s_acpi_description_header_raw *)(*p);
            x->entry_count++;
        }
    }

    printf("   ok; parsed %u entries\n", x->entry_count);

    /* As a final effort, it's important to scan the `extra_space` area for
        any table signatures whose references might be referenced in sub-tables
        (such as the FACS/FADT). If a matching signature is found, the table
        should be reallocated. This is expensive, but worth it to avoid crashes. */
    for (
        uintptr_t p = (uintptr_t)x->address + x->address->length;
        p < (uintptr_t)xsdt_end;
        p++
    ) {
        if (*((uint8_t *)p) < 'A' || *((uint8_t *)p) > 'Z') continue;

        /* If this is not done, `acpi_relocate_table` will update the value of `p`
            and the process will get stuck in an infinite loop. Design here is shoddy tbh. */
        uint8_t *shadow = (uint8_t *)p;

        for (int i = 0; i < (sizeof(ACPI_SIGS) / sizeof(const char *)); ++i) {
            if (0 != memcmp((void *)p, ACPI_SIGS[i], 4)) continue;

            uint8_t *new_loc =
                acpi_relocate_table(acpi, &shadow, (uint8_t *)xsdt_start, (uint8_t *)xsdt_end);

            if (-1U == new_loc) {
                puts("   ERROR: ACPI: Colliding table in XSDT could not be moved.\n");
                return -XSDT_TABLE_FOUND;
            }

            if (NULL != new_loc && acpi_parse_header(acpi, (uint64_t *)new_loc)) {
                if (x->entry_count >= (XSDT_MAX_ENTRIES-1)) continue;

                x->entry[x->entry_count] = (s_acpi_description_header_raw *)new_loc;
                x->entry_count++;
            }

            /* We can skip the whole table now. */
            p += ((s_acpi_description_header_raw *)p)->length - 1;

            break;
        }
    }

    return XSDT_TABLE_FOUND;
}


void
acpi_table_checksum(s_acpi_description_header_raw *sdt)
{
    uint32_t sum = 0;

    sdt->checksum = 0;

    for (uint32_t i = 0; i < sdt->length; ++i) {
        sum += *(uint8_t *)((uintptr_t)sdt + i);
    }

    sdt->checksum = (uint8_t)(0x100 - (sum % 0x100));
}


uint8_t *
acpi_relocate_table(s_acpi *acpi,
                    uint8_t **p_address,
                    uint8_t *start,
                    uint8_t *end)
{
    if (
        NULL == start || NULL == end
        || end <= start
        || *p_address <= start || *p_address >= end
    ) return NULL;

    /* Table pointer falls within our claimed space. Relocate it. */
    s_acpi_description_header_raw *sub = (s_acpi_description_header_raw *)(*p_address);

    uint8_t *new_loc = do_e820_malloc(sub->length, 2);
    if (NULL == new_loc) {
        puts("   ERROR: ACPI: Failed to relocate SDT (out of memory).\n");
        return (uint8_t *)(-1U);
    }

    printf("   ACPI: Relocating table (0x%08p -> 0x%08p : %u)...", sub, new_loc, sub->length);

    memcpy(new_loc, sub, sub->length);
    *p_address = new_loc;

    /* Clean it up. */
    memset(sub, 0x00, sub->length);

    /* Replace the entry within the XSDT (if applicable). */
    if (NULL != acpi->xsdt.address) {
        uint64_t **xsdt_start = (uint64_t **)((uintptr_t)acpi->xsdt.address + sizeof(s_acpi_description_header_raw));
        uint64_t **xsdt_end   = (uint64_t **)((uintptr_t)(acpi->xsdt.address) + acpi->xsdt.address->length);

        for (uint64_t **r = xsdt_start; r < xsdt_end; ++r) {
            if (*r == (uint64_t *)sub) *((uint8_t **)(*r)) = new_loc;
        }
    }

    /* Relocate the same entry within the RSDT (in case the loaded OS views that instead). */
    if (NULL != acpi->rsdt.address) {
        uint64_t **rsdt_start = (uint64_t **)((uintptr_t)acpi->rsdt.address + sizeof(s_acpi_description_header_raw));
        uint64_t **rsdt_end   = (uint64_t **)((uintptr_t)(acpi->rsdt.address) + acpi->rsdt.address->length);

        for (uintptr_t r = (uintptr_t)rsdt_start; r < (uintptr_t)rsdt_end; r += sizeof(uint32_t)) {
            if (*((uint64_t **)r) == (uint64_t *)sub) *((uint32_t *)r) = (uint32_t)new_loc;
        }
    }

    puts("   ok\n");

    return new_loc;
}


int
acpi_parse(s_acpi *acpi)
{
    int ret_val = ACPI_FAIL;
    memset(acpi, 0x00, sizeof(s_acpi));

    /* For MEMDISK loads, we only care about getting a straight line to
        the SSDT entries. Everything else can be disregarded. */
    puts("   Searching for RSDP...");
    if (RSDP_TABLE_FOUND != (ret_val = search_rsdp(acpi))) {
        return ret_val;
    }
    puts("   ok\n");

    if (acpi->rsdp->revision < 2) {
        puts("   ERROR: ACPI: Revision too low. Incompatible.\n");
        return ACPI_FAIL;
    }

    puts("   Parsing RSDT...");
    if (RSDP_TABLE_FOUND != (ret_val = parse_rsdt(acpi))) {
        puts("   not found\n");
        return ret_val;
    }

    puts("   Parsing XSDT...");
    if (XSDT_TABLE_FOUND != (ret_val = parse_xsdt(acpi))) {
        puts("   not found\n");
        return ACPI_FAIL;
    }

    puts("  ACPI OK\n");

    return ACPI_OK;
}


void
acpi_dump(s_acpi *acpi)
{
    char signature[5] = {0};
    uint64_t **p = NULL;
    int i = 0;

    printf(" RSDP  [%08p : %08x]\n", acpi->rsdp);
    printf(" RSDT  [%08p : %08x] (disregarded)\n", acpi->rsdt.address,
        (acpi->rsdt.address ? acpi->rsdt.address->length : 0));
    printf(" XSDT  [%08p : %08x]\n", acpi->xsdt.address,
        (acpi->xsdt.address ? acpi->xsdt.address->length : 0));

    if (NULL == acpi->xsdt.address) {
        puts("   (No XSDT table captures)\n");
        return;
    }

    p = (uint64_t **)((uintptr_t)(acpi->xsdt.address) + sizeof(s_acpi_description_header_raw));
    do {
        if (NULL != *p) {
            memcpy(signature, *p, 4);
            printf(" %s  [%08p : %08x]    ", signature, *p, ((s_acpi_description_header_raw *)(*p))->length);
        } else continue;

        if (!(++i % 2)) putchar('\n');
    } while (((uintptr_t)(++p)) < ((uintptr_t)acpi->xsdt.address + acpi->xsdt.address->length));

    if (i % 2) putchar('\n');
}


int
acpi_insert_table(s_acpi *acpi,
                  uint8_t *address)
{
    bool overrides = false;
    bool found_entry = false;
    s_acpi_description_header_raw *adhr;
    char signature[5] = {0};

    if (NULL == acpi || NULL == address) {
        puts("\nERROR: ACPI: Cannot insert a null address.\n");
        return ACPI_FAIL;
    }

    /* The signature always comes first in each SDT, so this is OK. */
    memcpy(signature, address, sizeof(signature) - 1);

    /* If the given table is NOT an S/PSDT, then it's intended to overwrite
        an existing table entry or add a new one. */
    if (
        0 != memcmp(address, SSDT, sizeof(SSDT) - 1)
        && 0 != memcmp(address, PSDT, sizeof(PSDT) - 1)
    ) {
        overrides = true;
        printf("   Inserting new table with signature '%s'\n", signature);
    } else {
        printf("   Updating table with signature '%s'\n", signature);
    }

    /* Are we using the XSDT instead of the RSDT? */
    if (acpi->xsdt.valid) {
        adhr = (s_acpi_description_header_raw *)(acpi->xsdt.address);

        /* Add the entry to the tracked XSDT structure. */
        acpi->xsdt.entry[acpi->xsdt.entry_count] = (s_acpi_description_header_raw *)address;
        acpi->xsdt.entry_count++;
    }

    else {
        /* Work on the RSDT (provided that is valid). */
        if (!acpi->rsdt.valid) {
            puts("\nERROR: ACPI: Cannot insert. RSDT & XSDT tables are not valid.\n");
            return ACPI_FAIL;
        }

        adhr = (s_acpi_description_header_raw *)(acpi->rsdt.address);

        /* Add the entry to the tracked RSDT structure. */
        acpi->rsdt.entry[acpi->rsdt.entry_count] = (s_acpi_description_header_raw *)address;
        acpi->rsdt.entry_count++;
    }

    if (!overrides) {
        goto insert__append_address;
    }
    
    /* Entry is updating a current pointer in the table, not adding onto the table. */
    for (
        uintptr_t entry = ((uintptr_t)adhr + sizeof(s_acpi_description_header_raw));
        entry < ((uintptr_t)adhr + adhr->length);
        entry += sizeof(uint64_t)
    ) {
        if (
            0 != entry
            && 0 == memcmp(*((uint8_t **)entry), adhr->signature, sizeof(adhr->signature))
        ) {
            *((uint8_t **)(entry)) = address;   /* Update the table's pointer for that SDT */
            found_entry = true;
        }
    }

    /* If the entry wasn't found, it needs to be added. */
    if (!found_entry) {
        goto insert__append_address;
    }

    goto insert__acpi_table_checksum;


insert__append_address:
    *(uint64_t **)((uintptr_t)adhr + adhr->length) = (uint64_t *)address;
    adhr->length += sizeof(uint64_t);

    e820_shift_bounds((uint8_t *)adhr, adhr->length);
    /* Fall through */

insert__acpi_table_checksum:
    /* Calculate the inner table's checksum. */
    acpi_table_checksum((s_acpi_description_header_raw *)address);

    /* Calculate the checksum 'globally' (R/XSDT). */
    acpi_table_checksum(adhr);


    return acpi_parse_header(acpi, (uint64_t *)address) ? ACPI_OK : ACPI_FAIL;
}
