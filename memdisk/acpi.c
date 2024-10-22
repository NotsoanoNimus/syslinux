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
    printf("   Using SUM == %u // CHECKSUM == %u\n", sum, sdt->checksum);
}


static
int
relocate_table(uint8_t **p_address)
{
    if (
        NULL == mXsdtStart || NULL == mXsdtEnd
        || *p_address <= mXsdtStart || *p_address >= mXsdtEnd
    ) return 0;

    /* Table pointer falls within our claimed space. Relocate it. */
    s_acpi_description_header_raw *sub = (s_acpi_description_header_raw *)(*p_address);

    uint8_t *new_loc = do_e820_malloc(sub->length, 2);
    if (NULL == new_loc) {
        puts("   ERROR: ACPI: Failed to relocate SDT (out of memory).\n");
        return -XSDT_TABLE_FOUND;
    }

    printf("   ACPI: Relocating SDT (0x%08p -> 0x%08p)\n", sub, new_loc);

    memcpy(new_loc, sub, sub->length);
    *p_address = (uint64_t *)new_loc;

    return 0;
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

        s_rsdp *r = &acpi->rsdp;

        r->valid = true;
        r->address = q;
        cp_str_struct(r->signature);
        cp_struct(&r->checksum);
        cp_str_struct(r->oem_id);
        cp_struct(&r->revision);
        cp_struct(&r->rsdt_address);
        cp_struct(&r->length);
        cp_struct(&r->xsdt_address);
        cp_struct(&r->extended_checksum);
        q += 3;		/* reserved field */

        acpi->rsdt.address = r->rsdt_address;
        acpi->xsdt.address = r->xsdt_address;

        return RSDP_TABLE_FOUND;
    }

    return -RSDP_TABLE_FOUND;
}


static
int
parse_rsdt(s_acpi *acpi, s_rsdt *r)
{
    uint8_t *q = r->address;

    if (0 != memcmp(q, RSDT, sizeof(RSDT) - 1)) {
        return -RSDT_TABLE_FOUND;
    }

    r->valid = true;
    get_acpi_description_header(q, &r->header);

    uint32_t *start = (uint32_t *)r->address;
    start += (ACPI_HEADER_SIZE / 4);

    uint32_t *max = (uint32_t *)r->address;
    max += (r->header.length / 4);

    for (uint32_t *p = start ; p < max; p = ((uintptr_t)p + sizeof(uint32_t))) {
        relocate_table((uint8_t **)p);

        if (parse_header((uint64_t *)(*p), acpi)) {
            r->entry[r->entry_count] = (uint8_t *)(*p);
            r->entry_count++;
        }
    }

    printf("   RSDT provides %u entries\n", r->entry_count);

    return RSDT_TABLE_FOUND;
}


static
int
parse_xsdt(s_acpi *acpi)
{
    uint8_t *q = acpi->xsdt.address;

    s_xsdt *x = &acpi->xsdt;

    if (0 == q || 0 != memcmp(q, XSDT, sizeof(XSDT) - 1)) {
        return -XSDT_TABLE_FOUND;
    }

    x->valid = true;

    get_acpi_description_header(q, &x->header);
    printf("XSDT length: %u\n", x->header.length);
    puts("XSDT start -- "); MEMDUMP((x->address + ACPI_HEADER_SIZE), 64); putchar('\n');

    /* We need to move any adjacent table(s) OUT of the tail end of the XSDT.
        This will allow us to easily add extra entries without crashing the
        loaded kernel. We make space for new entries, but do not yet extend
        the XSDT's `length` value until we insert other SDTs (SSDT/NFIT/etc). */
    uint32_t extra_space = sizeof(uint64_t) * 16;
    mXsdtStart = (uint64_t **)((uintptr_t)x->address + ACPI_HEADER_SIZE);
    mXsdtEnd   = (uint64_t **)((uintptr_t)x->address + x->header.length + extra_space);

    /* Iterate entries... */
    for (
        uint64_t **p = (uint64_t **)((uintptr_t)x->address + ACPI_HEADER_SIZE);
        p < (uint64_t **)((uintptr_t)x->address + x->header.length);
        p++
    ) {
        relocate_table((uint8_t **)p);

        if (parse_header(*p, acpi)) {
            x->entry[x->entry_count] = *p;
            x->entry_count++;
        }
    }

    printf("   XSDT parsed %u entries\n", x->entry_count);

    return XSDT_TABLE_FOUND;
}


static
void
parse_dsdt(s_dsdt *d)
{
    uint8_t *q = (uint8_t *)d->address;
    q += ACPI_HEADER_SIZE;

    /* Just directly assign the table to the address instead of copying it. */
    d->definition_block = q;

    // /* Searching how much definition blocks we must copy */
    // uint32_t definition_block_size=d->header.length-ACPI_HEADER_SIZE;

    // if ((d->definition_block = malloc(definition_block_size)) != NULL) {
    //     memcpy(d->definition_block, q, definition_block_size);
    // }
}


int
parse_acpi(s_acpi *acpi)
{
    int ret_val = -ACPI_FOUND;
    memset(acpi, 0x00, sizeof(s_acpi));

    /* For MEMDISK loads, we only care about getting a straight line to
        the SSDT entries. Everything else can be disregarded. */
    puts("   Searching for RSDP...");
    if (RSDP_TABLE_FOUND != (ret_val = search_rsdp(acpi))) {
        return ret_val;
    }
    puts("   ok\n");

    if (acpi->rsdp.revision < 2) {
        puts("   ERROR: ACPI: Revision too low. Incompatible.\n");
        return -ACPI_FOUND;
    }

    puts("   Parsing XSDT...\n");
    if (XSDT_TABLE_FOUND != (ret_val = parse_xsdt(acpi))) {
        puts("XSDT not found\n");
        return -ACPI_FOUND;
    } else puts("   ok\n");

    puts("   Parsing/Checking RSDT...");
    if (RSDP_TABLE_FOUND != (ret_val = parse_rsdt(acpi, &(acpi->rsdt)))) {
        return ret_val;
    }
    puts("   ok\n");

    return ACPI_FOUND;
}


void
get_acpi_description_header(uint8_t *q,
                            s_acpi_description_header *adh)
{
    cp_str_struct(adh->signature);
    cp_struct(&adh->length);
    cp_struct(&adh->revision);
    cp_struct(&adh->checksum);
    cp_str_struct(adh->oem_id);
    cp_str_struct(adh->oem_table_id);
    cp_struct(&adh->oem_revision);
    cp_str_struct(adh->creator_id);
    cp_struct(&adh->creator_revision);
}


bool
parse_header(uint64_t *address,
             s_acpi *acpi)
{
    s_acpi_description_header adh = {0};
    get_acpi_description_header((uint8_t *)address, &adh);

    /* Trying to determine the pointed table */
    /* Looking for FADT */
    if (0 == memcmp(adh.signature, FACP, sizeof(FACP) - 1)) {
        s_fadt *f = &acpi->fadt;
        s_facs *fa = &acpi->facs;
        s_dsdt *d = &acpi->dsdt;

        // puts("   FADT table found\n");

        /* This structure is valid, let's fill it */
        f->valid = true;
        f->address = address;
        memcpy(&f->header, &adh, sizeof(adh));
        // parse_fadt(f);

        /* FACS wasn't already detected
            * FADT points to it, let's try to detect it */
        if (fa->valid == false) {
            relocate_table((uint8_t **)(uintptr_t)address + ((uintptr_t)&(f->firmware_ctrl) - (uintptr_t)f->address));
            relocate_table((uint8_t **)(uintptr_t)address + ((uintptr_t)&(f->x_firmware_ctrl) - (uintptr_t)f->address));

            fa->address = (uint64_t *)f->x_firmware_ctrl;
            // parse_facs(fa);
            if (fa->valid == false) {
                /* Let's try again */
                fa->address = (uint64_t *)f->firmware_ctrl;
                // parse_facs(fa);
            }
        }

        /* DSDT wasn't already detected
            * FADT points to it, let's try to detect it */
        if (d->valid == false) {
            s_acpi_description_header new_adh;
            get_acpi_description_header((uint8_t *)f->x_dsdt, &new_adh);

            if (memcmp(new_adh.signature, DSDT, sizeof(DSDT) - 1) == 0) {
                // puts("   DSDT table found via x_dsdt\n");
                relocate_table((uint8_t **)(uintptr_t)address + ((uintptr_t)&(f->x_dsdt) - (uintptr_t)f->address));
                d->valid = true;
                d->address = (uint64_t *)f->x_dsdt;
                memcpy(&d->header, &new_adh, sizeof(new_adh));
                parse_dsdt(d);
            } else {
                /* Let's try again */
                get_acpi_description_header((uint8_t *)f->dsdt_address, &new_adh);
                if (memcmp(new_adh.signature, DSDT, sizeof(DSDT) - 1) == 0) {
                    // puts("   DSDT table found via dsdt_address\n");
                    relocate_table((uint8_t **)(uintptr_t)address + ((uintptr_t)&(f->dsdt_address) - (uintptr_t)f->address));
                    d->valid = true;
                    d->address = (uint64_t *)f->dsdt_address;
                    memcpy(&d->header, &new_adh, sizeof(new_adh));
                    parse_dsdt(d);
                }
            }
        }
    }

    else if (memcmp(adh.signature, APIC, sizeof(APIC) - 1) == 0) {
        // DEBUG_PRINT(("MADT table found\n"));
        // s_madt *m = &acpi->madt;
        // /* This structure is valid, let's fill it */
        // m->valid = true;
        // m->address =address;
        // memcpy(&m->header, &adh, sizeof(adh));
        // parse_madt(acpi);
    }
    
    else if (memcmp(adh.signature, DSDT, sizeof(DSDT) - 1) == 0) {
        // puts("   DSDT table found\n");
        s_dsdt *d = &acpi->dsdt;
        d->valid = true;
        d->address = address;
        memcpy(&d->header, &adh, sizeof(adh));
        parse_dsdt(d);
    }

    /* PSDT have to be considered as SSDT. Intel ACPI Spec @ 5.2.11.3 */
    else if (
        0 == memcmp(adh.signature, SSDT, sizeof(SSDT) - 1)
        || 0 == memcmp(adh.signature, PSDT, sizeof(PSDT) - 1)
    ) {
        // printf("   SSDT table found (#%u)\n", acpi->ssdt_count);

        if (acpi->ssdt_count >= (MAX_SSDT - 1)) {
            printf("   Too many SSDT entries (%u maximum)\n", MAX_SSDT);
            return false;
        }

        // /* We can have many SSDT, so let's allocate a new one */
        // if (NULL == (acpi->ssdt[acpi->ssdt_count] = malloc(sizeof(s_ssdt)))) {
        //     return false;
        // }

        s_ssdt *s = &(acpi->ssdt[acpi->ssdt_count]);

        /* This structure is valid, let's fill it */
        s->valid = true;
        s->address = address;
        memcpy(&s->header, &adh, sizeof(adh));

        /* Searching how much definition blocks we must copy */
        // uint32_t definition_block_size = (adh.length - ACPI_HEADER_SIZE);

        /* Don't MALLOC per below, just point the block to the address. */
        s->definition_block = (uint8_t *)((uintptr_t)(s->address) + ACPI_HEADER_SIZE);

        // if (NULL != (s->definition_block = malloc(definition_block_size))) {
        //     memcpy(s->definition_block,
        //            (s->address + ACPI_HEADER_SIZE),
        //            definition_block_size);
        // }

        /* Increment the number of ssdt we have */
        acpi->ssdt_count++;
    }
    
    return true;
}


/* TODO: Make sure the updated table isn't going outside its memory allocation. */
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
    printf("   Inserting new table with signature '%s'\n", signature);

    /* If the given table is NOT an S/PSDT, then it's intended to overwrite
        an existing table entry or add a new one. */
    /* NFITs are also allowed to have multiples. */
    if (
        0 != memcmp(address, SSDT, sizeof(SSDT) - 1)
        && 0 != memcmp(address, PSDT, sizeof(PSDT) - 1)
        && 0 != memcmp(address, NFIT, sizeof(NFIT) - 1)
    ) {
        puts("   Table is an override type.\n");
        overrides = true;
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
        *(uint64_t **)((uintptr_t)adhr + adhr->length) = (uint64_t *)address;
        adhr->length += sizeof(uint64_t);

        e820_shift_bounds((uint8_t *)adhr, adhr->length);
    } else {
        /* Entry is updating a current pointer in the table, not adding onto the table. */
        for (
            uintptr_t entry = ((uintptr_t)adhr + ACPI_HEADER_SIZE);
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
            *(uint64_t **)((uintptr_t)adhr + adhr->length) = (uint64_t *)address;
            adhr->length += sizeof(uint64_t);

            e820_shift_bounds((uint8_t *)adhr, adhr->length);
        }
    }

    /* Calculate the inner table's checksum. */
    compute_checksum((s_acpi_description_header_raw *)address);

    /* Calculate the checksum 'globally' (R/XSDT). */
    compute_checksum(adhr);

    return parse_header((uint64_t *)address, acpi) ? ACPI_OK : ACPI_FAIL;
}
