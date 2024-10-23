/**
 * @file acpi.c
 * @brief Reworked from HDT methods in ../com32/gpllib/acpi.
 */

#include "acpi/acpi.h"
#include "memdisk.h"
#include "conio.h"
#include "e820.h"

/* TODO: Clean up this file. Remove unnecessary tracking/logging. */


static uint64_t **mXsdtStart = NULL;
static uint64_t **mXsdtEnd = NULL;

/* The memory gained by shrinking the ACPI meta structure down
    far outweighs the cost of this string array (by almost a whole
    order of magnitude, actually). */
const char *ACPI_SIGS[] = {
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
void
compute_checksum(s_acpi_description_header_raw *sdt)
{
    uint32_t sum = 0;

    sdt->checksum = 0;

    for (uint32_t i = 0; i < sdt->length; ++i) {
        sum += *(uint8_t *)((uintptr_t)sdt + i);
    }

    sdt->checksum = (uint8_t)(0x100 - (sum % 0x100));
}


static
int
relocate_table(uint8_t **p_address)
{
    if (
        NULL == mXsdtStart || NULL == mXsdtEnd
        || *p_address <= mXsdtStart || *p_address >= mXsdtEnd
    ) return ACPI_OK;

    /* Table pointer falls within our claimed space. Relocate it. */
    s_acpi_description_header_raw *sub = (s_acpi_description_header_raw *)(*p_address);

    uint8_t *new_loc = do_e820_malloc(sub->length, 2);
    if (NULL == new_loc) {
        puts("   ERROR: ACPI: Failed to relocate SDT (out of memory).\n");
        return ACPI_FAIL;
    }

    printf("   ACPI: Relocating table (0x%08p -> 0x%08p)\n", sub, new_loc);

    memcpy(new_loc, sub, sub->length);
    *p_address = (uint64_t *)new_loc;

    return ACPI_OK;
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

        acpi->rsdt.address = acpi->rsdp->rsdt_address;
        acpi->xsdt.address = acpi->rsdp->xsdt_address;

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
        uint32_t **p = ((uintptr_t)r->address + sizeof(s_acpi_description_header_raw));
        p < ((uintptr_t)r->address + r->address->length);
        p = ((uintptr_t)p + sizeof(uint32_t))
    ) {
        relocate_table((uint8_t **)p);

        if (parse_header(acpi, (uint64_t *)(*p))) {
            if (r->entry_count >= RSDT_MAX_ENTRIES) continue;

            r->entry[r->entry_count] = (s_acpi_description_header_raw *)(*p);
            r->entry_count++;
        }
    }

    printf("   RSDT: parsed %u entries\n", r->entry_count);

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
    uint32_t extra_space = sizeof(uint64_t) * 16;
    mXsdtStart = (uint64_t **)((uintptr_t)x->address + sizeof(s_acpi_description_header_raw));
    mXsdtEnd   = (uint64_t **)((uintptr_t)x->address + x->address->length + extra_space);

    /* Iterate entries... */
    for (
        uint64_t **p = mXsdtStart;
        p < (uint64_t **)((uintptr_t)x->address + x->address->length);
        p++
    ) {
        /* If the table is located within the extra space we'd like to
            claim, then it needs to be relocated. */
        relocate_table((uint8_t **)p);

        if (parse_header(acpi, *p)) {
            if (x->entry_count >= XSDT_MAX_ENTRIES) continue;

            x->entry[x->entry_count] = (s_acpi_description_header_raw *)(*p);
            x->entry_count++;
        }
    }

    printf("   XSDT: parsed %u entries\n", x->entry_count);

    /* As a final effort, it's important to scan the `extra_space` area for
        any table signatures whose references might be referenced in sub-tables
        (such as the FACS/FADT). If a matching signature is found, the table
        should be reallocated. This is expenseive, but worth it to avoid crashes. */
    for (
        uintptr_t p = (uintptr_t)(x->address) + x->address->length;
        p < (uintptr_t)mXsdtEnd;
        ++p
    ) {
        for (int i = 0; i < (sizeof(ACPI_SIGS) / sizeof(const char *)); ++i) {
            if (0 != memcmp((void *)p, ACPI_SIGS[i], 4)) continue;

            relocate_table((uint8_t **)p);
        }
    }

    return XSDT_TABLE_FOUND;
}


int
parse_acpi(s_acpi *acpi)
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

    puts("   Parsing XSDT...\n");
    if (XSDT_TABLE_FOUND != (ret_val = parse_xsdt(acpi))) {
        puts("XSDT not found\n");
        return ACPI_FAIL;
    } else puts("   ok\n");

    puts("   Checking RSDT...");
    if (RSDP_TABLE_FOUND != (ret_val = parse_rsdt(acpi))) {
        return ret_val;
    }
    puts("   ok\n");

    return ACPI_OK;
}


bool
parse_header(s_acpi *acpi,
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
            uintptr_t p = (uintptr_t)address;
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


int
insert_acpi_table(s_acpi *acpi,
                  uint8_t *address)
{
    bool overrides = false;
    bool found_entry = false;
    s_acpi_description_header_raw *adhr;
    char signature[5] = {0};

    if (NULL == acpi || NULL == address) {
        puts("\nERROR: ACPI: Cannot insert. Null address.\n");
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
        acpi->xsdt.entry[acpi->xsdt.entry_count] = (uint64_t *)address;
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
        acpi->rsdt.entry[acpi->rsdt.entry_count] = (uint8_t *)address;
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

    goto insert__compute_checksum;


insert__append_address:
    *(uint64_t **)((uintptr_t)adhr + adhr->length) = (uint64_t *)address;
    adhr->length += sizeof(uint64_t);

    e820_shift_bounds((uint8_t *)adhr, adhr->length);
    /* Fall through */

insert__compute_checksum:
    /* Calculate the inner table's checksum. */
    compute_checksum((s_acpi_description_header_raw *)address);

    /* Calculate the checksum 'globally' (R/XSDT). */
    compute_checksum(adhr);


    return parse_header(acpi, (uint64_t *)address) ? ACPI_OK : ACPI_FAIL;
}
