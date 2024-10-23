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

#ifndef MEMDISK_ACPI_H
#define MEMDISK_ACPI_H

#include <inttypes.h>
#include <stdbool.h>

#include "structs.h"
#include "rsdp.h"
#include "rsdt.h"
#include "xsdt.h"
#include "nfit.h"


enum { ACPI_OK = 0, ACPI_FAIL };


typedef
MEMDISK_PACKED_PREFIX
struct {
    rsdp_raw_t *rsdp;
    s_rsdt rsdt;
    s_xsdt xsdt;
    s_acpi_description_header_raw *nfit;
    s_acpi_description_header_raw *ssdt_nvdimm_root;
} MEMDISK_PACKED_POSTFIX
s_acpi;


int acpi_parse(s_acpi *acpi);

void acpi_dump(s_acpi *acpi);

int acpi_insert_table(
    s_acpi *acpi,       
    uint8_t *address
);

void acpi_table_checksum(
    s_acpi_description_header_raw *sdt
);

uint8_t *acpi_relocate_table(
    s_acpi *acpi,
    uint8_t **p_address,
    uint8_t *start,
    uint8_t *end
);


/* Define a set of known ACPI tables.
    See Tables 5.5 & 5.6 of the ACPI Specification, version 6.4:
    https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/05_ACPI_Software_Programming_Model/ACPI_Software_Programming_Model.html?highlight=xsdt#description-header-signatures-for-tables-defined-by-acpi */
#define RSDP "RSD PTR "
#define RSDT "RSDT"
#define XSDT "XSDT"
#define DSDT "DSDT"
#define SSDT "SSDT"
#define PSDT "PSDT"
#define NFIT "NFIT"

#define APIC "APIC"
#define BERT "BERT"
#define BGRT "BGRT"
#define CPEP "CPEP"
#define ECDT "ECDT"
#define EINJ "EINJ"
#define ERST "ERST"
#define FACP "FACP"
#define FACS "FACS"
#define FPDT "FPDT"
#define GTDT "GTDT"
#define HEST "HEST"
#define MSCT "MSCT"
#define MPST "MPST"
#define OEMx "OEMx"
#define PCCT "PCCT"
#define PHAT "PHAT"
#define PMTT "PMTT"
#define RASF "RASF"
#define SBST "SBST"
#define SDEV "SDEV"
#define SLIT "SLIT"
#define SRAT "SRAT"
#define AEST "AEST"
#define BDAT "BDAT"
#define BOOT "BOOT"
#define CDIT "CDIT"
#define CEDT "CEDT"
#define CRAT "CRAT"
#define CSRT "CSRT"
#define DBGP "DBGP"
#define DBPG "DBPG"   /* Removed the '2' from the end to conform with 4-byte lengths */
#define DMAR "DMAR"
#define DRTM "DRTM"
#define ETDT "ETDT"
#define HPET "HPET"
#define IBFT "IBFT"
#define IORT "IORT"
#define IVRS "IVRS"
#define LPIT "LPIT"
#define MCFG "MCFG"
#define MCHI "MCHI"
#define MPAM "MPAM"
#define MSDM "MSDM"
#define PRMT "PRMT"
#define RGRT "RGRT"
#define SDEI "SDEI"
#define SLIC "SLIC"
#define SPCR "SPCR"
#define SPMI "SPMI"
#define STAO "STAO"
#define SVKL "SVKL"
#define TCPA "TCPA"
#define TPM2 "TPM2"
#define UEFI "UEFI"
#define WAET "WAET"
#define WDAT "WDAT"
#define WDRT "WDRT"
#define WPBT "WPBT"
#define WSMT "WSMT"
#define XENV "XENV"


extern const char *ACPI_SIGS[68];



#endif   /* MEMDISK_ACPI_H */
