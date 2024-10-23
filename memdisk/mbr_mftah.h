#ifndef MBR_MFTAH_H
#define MBR_MFTAH_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* Assumes 'MFTAH' is installed locally. If not, the include file can be
    fetched from https://github.com/NotsoanoNimus/MFTAH.
    This MEMDISK modification really only needs the MFTAH definitions to
    be sourced in order to compile. It doesn't used any objects or xlats
    compiled elsewhere. */
#include <mftah.h>



#define MFTAH_OPTION_NAME       "mftahdisk"
#define MFTAH_OPTION_KEY        "mftahkey"
#define MFTAH_OPTION_EPHEMERAL  "mftaheph"
#define MFTAH_OPTION_RAW_CLI    "mftahcli"


mftah_status_t
decrypt(
    void *payload,
    uint32_t alleged_payload_size,
    immutable_ref_t key,
    size_t key_length
);



#endif   /* MBR_MFTAH_H */
