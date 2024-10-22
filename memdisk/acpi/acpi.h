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

#ifndef ACPI_H
#define ACPI_H

#include <inttypes.h>
#include <stdbool.h>

/* TODO: Get rid of unnecessary tables for this loader. This isn't 'hdt', meaning
    we don't need to track and detect every single ACPI table there is. It's a waste. */
#include "structs.h"
#include "rsdp.h"
#include "rsdt.h"
#include "xsdt.h"
#include "fadt.h"
#include "madt.h"
#include "dsdt.h"
#include "ssdt.h"
#include "sbst.h"
#include "ecdt.h"
#include "facs.h"
#include "hpet.h"
#include "tcpa.h"
#include "mcfg.h"
#include "slic.h"
#include "boot.h"
#include "nfit.h"


enum { ACPI_FOUND = 1, ENO_ACPI = 2 , MADT_FOUND = 3 , ENO_MADT = 4 };

enum { ACPI_OK = 0, ACPI_FAIL };

#define MAX_SSDT 128
#define MAX_NFIT 16

/* Some other description HEADERS : ACPI doc: 5.2.6*/
#define OEMX "OEMx"
#define SRAR "SRAT"
#define BERT "BERT"
#define BOOT "BOOT"
#define CPEP "CPEP"
#define DBGP "DGBP"
#define DMAR "DMAR"
#define ERST "ERST"
#define ETDT "ETDT"
#define HEST "HEST"
#define HPET "HPET"
#define IBFT "IBFT"
#define MCFG "MCFG"
#define SPCR "SPCR"
#define SPMI "SPMI"
#define TCPA "TCPA"
#define UEFI "UEFI"
#define WAET "WAET"
#define WDAT "WDAT"
#define WDRT "WDRT"
#define WSPT "WSPT"
#define SLIC "SLIC"

/* This macro are used to extract ACPI structures 
 * please be careful about the q (interator) naming */
#define cp_struct(dest) \
    memcpy(dest, q, sizeof(*dest)); q += sizeof(*dest)

#define cp_str_struct(dest) \
    memcpy(dest, q, sizeof(dest) - 1); dest[sizeof(dest)-1] = 0; q += sizeof(dest) - 1


typedef struct {
    s_rsdp rsdp;
    s_rsdt rsdt;
    s_xsdt xsdt;
    s_fadt fadt;
    s_madt madt;
    s_dsdt dsdt;
    s_ssdt ssdt[MAX_SSDT];
    uint8_t ssdt_count;
    s_sbst sbst;
    s_ecdt ecdt;
    s_facs facs;
    s_hpet hpet;
    s_tcpa tcpa;
    s_mcfg mcfg;
    s_slic slic;
    s_boot boot;
    s_nfit nfit[MAX_NFIT];
    uint8_t nfit_count;
} s_acpi;


int parse_acpi(s_acpi *acpi);

void get_acpi_description_header(
    uint8_t *q,
    s_acpi_description_header *adh
);

bool parse_header(
    uint64_t *address,
    s_acpi *acpi
);

int insert_acpi_table(
    s_acpi *acpi,       
    uint8_t *address
);



#endif   /* ACPI_H */
