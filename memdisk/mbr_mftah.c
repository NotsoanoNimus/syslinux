#include "mbr_mftah.h"

#include "aes.h"
#include "sha256.h"

#include "e820.h"
#include "conio.h"
#include "memdisk.h"


static const char *const MftahPayloadSignature     = MFTAH_PAYLOAD_SIGNATURE;

/* Provided dynamically during build-time by the compilation of 'Ramdisk.asl'. */
extern unsigned char Ramdisk_aml[];
extern unsigned int Ramdisk_aml_len;

#define POOL_SLOTS  32
#define POOL_SIZE   4096

#define MAX(x,y) \
    (((x) >= (y)) ? (x) : (y))
#define MIN(x,y) \
    (((x) <= (y)) ? (x) : (y))



/* Create a pseudo-heap... This is a trashy but it's cheap and it works.
    This module needs to be a small compiled size to load in 16-bit Real Mode
    while leaving adequate space for the MEMDISK.
    In the future, I might want to consider using the E820 memory map as
    a basis for the memory we utilize in temporary decryption operations
    (because it doesn't need to be reserved or marked at all). */
static char phatpool[POOL_SIZE] = {0};

typedef
struct {
    uint16_t offset;
    uint16_t length;
} memres_t;

static memres_t phatpool_slots[POOL_SLOTS] = {0};



static
memres_t *
first_unused(void)
{
    for (size_t i = 0; i < POOL_SLOTS; ++i) {
        if (phatpool_slots[i].offset || phatpool_slots[i].length) continue;
        return &phatpool_slots[i];
    }

    return NULL;
}


static
void *
malloc(size_t length)
{
    memres_t *slot = first_unused();
    memres_t *p;
    int open_index = 1;     /* Start at 1 so the 0 reservation kicks off the algo */
    int end_index = 1;

    /* No open slots. */
    if (!slot) return NULL;

    /* Scan all slots for the minimum reservation offset. Since slots
        should never overlap, follow each minimum through its reservation
        length until a sufficient gap is found, if one exists at all. If
        one doesn't exist, some convergence algorithm would help here, but
        reeeee I'm not au--gifted-- enough. */
    for (size_t i = 0; i < POOL_SLOTS; ++i) {
        p = &phatpool_slots[i];
        end_index = open_index + length;

        if (open_index >= p->offset && open_index < (p->offset + p->length)) {
            open_index = p->offset + p->length;   /* Move the cursor up to the end of the slot. */
            end_index = open_index + length;
        }

        if (end_index >= p->offset && end_index < (p->offset + p->length)) {
            open_index = p->offset + p->length;   /* Move the cursor up to the end of the slot. */
            end_index = open_index + length;
        }

        /* Out of "memory". */
        if (open_index >= POOL_SIZE || end_index >= POOL_SIZE) return NULL;
    }

    slot->offset = open_index;
    slot->length = length;

    return (void *)&phatpool[slot->offset];
}


static
void *
calloc(size_t count,
       size_t length)
{
    void *alloc = malloc(count * length);

    if (NULL != alloc) {
        memset(alloc, 0x00, count * length);
    }

    return alloc;
}


static
void
free(void *pointer)
{
    if (NULL == pointer) return;

    for (size_t i = 0; i < POOL_SLOTS; ++i) {
        if (pointer == &phatpool[(phatpool_slots[i].offset)]) {
            memset(&phatpool_slots[i], 0x00, sizeof(memres_t));
        }
    }
}


static
mftah_status_t
mftah_crypt_default(mftah_work_order_t *work_order,
                    immutable_ref_t sha256_key,
                    immutable_ref_t iv)
{
    aes_ctx_t *aes_context = NULL;

    switch (work_order->enc_type) {
    case MFTAH_ENC_TYPE_AES256_CBC:
        aes_context = (aes_ctx_t *)calloc(1, sizeof(aes_ctx_t));

        AES_init_ctx_iv(
            aes_context,
            (uint8_t *)sha256_key,
            (uint8_t *)iv
        );

        printf("   %02u: Running decryption (0x%p : %lu)\n",
            work_order->thread_index, work_order->location, work_order->length);

        AES_CBC_decrypt_buffer(
            aes_context,
            work_order->location,
            work_order->length
        );

        free(aes_context);
        break;

    default:
        return MFTAH_INVALID_ENCRYPTION_TYPE;
    }

    return MFTAH_SUCCESS;
}


static
uint8_t *
mix_vectors(mftah_payload_header_t *header,
            uint8_t threads)
{
    /* NOTE: This MUST be done because the same IV should NEVER be used with different, 
        non-sequential blocks of data. This effectively mixes IVs for each block with a
        random IV seed in an XOR chain from the previous one. */
    uint8_t *mixed_vectors = calloc(1, (threads * sizeof(header->initialization_vector)));

    /* The first IV is equivalent to the "public" one used on the header. */
    memcpy(mixed_vectors,
           header->initialization_vector,
           sizeof(header->initialization_vector));

    /* The rest of them sequentially mix with the previous vector. */
    /* NOTE: We only go up to 'threads-1' because the first IV is already set up (see above). */
    for (uint8_t t = 0; t < (threads - 1); ++t) {
        size_t iv_base_offset = (t + 1) * sizeof(header->initialization_vector);

        /* Only include the 'step' in the XOR if all seeds have already been used once. */
        uint8_t step = (threads >= sizeof(header->iv_seeds)) ? header->iv_seed_step : 0x00;

        /* The index of which seed to use depends on whether we've wrapped around once.
            And the value AT sizeof(seeds) is NOT included in this rotation. */
        uint8_t seed_position = t < sizeof(header->iv_seeds) ? t : (t % sizeof(header->iv_seeds));

        /* When the thread count has first passed the seeds available, use just the 'seed-step' to XOR the IV. */
        uint8_t seed = (threads == sizeof(header->iv_seeds)) ? 0x00 : (header->iv_seeds[seed_position]);

        /* Now that we've got what we need, do the XOR. */
        for (size_t x = 0; x < sizeof(header->initialization_vector); ++x) {
            mixed_vectors[iv_base_offset + x]
                = (
                    mixed_vectors[(iv_base_offset + x) - sizeof(header->initialization_vector)]
                    ^ (step ^ seed)
                );
        }
    }

    return mixed_vectors;
}


mftah_status_t
decrypt(void *payload,
        uint32_t alleged_payload_size,
        immutable_ref_t key,
        size_t key_length)
{
    mftah_work_order_t work_order = {0};

    uint8_t remainder = 0;
    size_t total_crypt_size = 0;

    uint8_t password_hash[SIZE_OF_SHA_256_HASH] = {0};
    uint8_t wrapper_hmac[SIZE_OF_SHA_256_HASH] = {0};
    uint8_t original_hmac[SIZE_OF_SHA_256_HASH] = {0};

    size_t stored_length = 0;
    uint8_t threads = 0;

    uint8_t *mixed_vectors = NULL;

    mftah_status_t MftahStatus = MFTAH_SUCCESS;
    mftah_payload_header_t *header = (mftah_payload_header_t *)payload;
    mftah_payload_header_t saved_header = {0};

    /* Preserve the decrypted copy of the header. Easier to calculate W-HMAC when we want. */
    memcpy(&saved_header, header, sizeof(mftah_payload_header_t));

    if (
        NULL == payload
        || NULL == key || 0 == key_length
    ) {
        return MFTAH_INVALID_PARAMETER;
    }

    /* Hash the given password. */
    calc_sha_256(password_hash, key, key_length);
    puts("Password Hash ok -- ");
    MEMDUMP(password_hash, SIZE_OF_SHA_256_HASH);

    /* Now decrypt the header. Offset 96 is the start of the blob and two blocks (32 bytes) get decrypted. */
    mftah_work_order_t *header_order = (mftah_work_order_t *)calloc(1, sizeof(mftah_work_order_t));
    header_order->length = MFTAH_HEADER_ENCRYPT_ADDL_SIZE;
    header_order->location = ((uint8_t *)&saved_header + MFTAH_HEADER_ENCRYPT_OFFSET);
    header_order->enc_type = (mftah_encryption_type_t)(saved_header.encryption_type);
    header_order->hmac_type = (mftah_hmac_type_t)(saved_header.hmac_type);

    puts("Decrypting header...\n");
    MftahStatus = mftah_crypt_default(header_order, password_hash, header->initialization_vector);
    if (MFTAH_ERROR(MftahStatus)) {
        return MftahStatus;
    }

    /* Check the signature. */
    if (0 != memcmp(&(saved_header.signature), MftahPayloadSignature, MFTAH_PAYLOAD_SIGNATURE_SIZE)) {
        return MFTAH_INVALID_PASSWORD;
    }

    /* Extract interesting initial values and evaluate them. */
    /* NOTE: If the payload is over UINT32_MAX, we'll have problems. ignoring... */
    puts("ok -- ");
    MEMDUMP(&saved_header, sizeof(mftah_payload_header_t));

    stored_length = (size_t)(saved_header.payload_length);
    if (stored_length > alleged_payload_size) {
        return MFTAH_BAD_PAYLOAD_LEN;
    }

    threads = MAX(1, saved_header.thread_count);
    if (threads > MFTAH_MAX_THREAD_COUNT) {
        return MFTAH_INVALID_THREAD_COUNT;
    }

    /* Calculate some information based on the parsed header information. */
    remainder = (stored_length % AES_BLOCKLEN)
        ? (AES_BLOCKLEN - (stored_length % AES_BLOCKLEN))
        : 0;
    total_crypt_size = stored_length + remainder;

    /* Reproduce the wrapper HMAC over the encrypted content and verify that it matches. */
    puts("\nCalculating Wrapper HMAC...\n");
    switch (header->hmac_type) {
    case MFTAH_HMAC_TYPE_SHA256:
        hmac_sha256(key,
                    key_length,
                    ((uint8_t *)header + MFTAH_HEADER_ENCRYPT_OFFSET),
                    (total_crypt_size + MFTAH_HEADER_ENCRYPT_ADDL_SIZE),
                    wrapper_hmac);
        break;
    default:
        return MFTAH_INVALID_HMAC_TYPE;
    }

    puts("   Testing it...\n");
    if (0 != memcmp(wrapper_hmac, header->wrapper_hmac, SIZE_OF_SHA_256_HASH)) {
        return MFTAH_BAD_W_HMAC;
    }

    puts("W-HMAC ok -- ");
    MEMDUMP(wrapper_hmac, SIZE_OF_SHA_256_HASH);

    /* Flash the decrypted header onto the live payload now that W-HMAC is verified. */
    memcpy(header, &saved_header, sizeof(mftah_payload_header_t));

    size_t chunk_size = (total_crypt_size / threads) - ((total_crypt_size / threads) % AES_BLOCKLEN);
    size_t last_chunk_size = total_crypt_size - ((threads - 1) * chunk_size);

    /* Form the initialization vector chain; same way we do in 'encrypt'. */
    printf("Mixing initialization vectors (%u : %u).", threads, header->iv_seed_step);
    mixed_vectors = mix_vectors(header, threads);

    /* Prepare all decryption work orders. This of course won't be threaded. */
    printf("\nDecrypting across %d vectors. Please wait.\r\n", threads);
    for (uint8_t t = 0; t < threads; ++t) {
        work_order.location = (uint8_t *)((uint8_t *)payload + sizeof(mftah_payload_header_t) + (t * chunk_size));
        work_order.length = ((threads - 1) == t) ? last_chunk_size : chunk_size;
        work_order.thread_index = t;
        work_order.enc_type = header->encryption_type;
        work_order.hmac_type = header->hmac_type;

        MftahStatus = mftah_crypt_default(&work_order,
                                          (immutable_ref_t)password_hash,
                                          (immutable_ref_t)&(mixed_vectors[t * sizeof(header->initialization_vector)]));
        if (MFTAH_ERROR(MftahStatus)) {
            return MftahStatus;
        }
    }

    /* Finally, no need for this anymore. */
    free(mixed_vectors);

    /* Sanity check: our expected signature should still exist. */
    puts("   Testing signature...\n");
    if (0 != memcmp(&(header->signature), MftahPayloadSignature, MFTAH_PAYLOAD_SIGNATURE_SIZE)) {
        return MFTAH_INVALID_SIGNATURE;
    }

    /* Reproduce the HMAC over the original content that was encrypted. */
    puts("\nCalculating Original HMAC...\n");
    hmac_sha256(key, key_length,
                (uint8_t *)payload + sizeof(mftah_payload_header_t),
                header->payload_length,
                original_hmac);

    puts("   Testing it...\n");
    if (0 != memcmp(original_hmac, header->original_hmac, SIZE_OF_SHA_256_HASH)) {
        return MFTAH_BAD_O_HMAC;
    }

    puts("O-HMAC ok -- ");
    MEMDUMP(original_hmac, SIZE_OF_SHA_256_HASH);

    return MFTAH_SUCCESS;
}


void
mftah_acpi_setup(s_acpi *acpi,
                 uint8_t *ramdisk_start,
                 uint32_t ramdisk_length)
{
    const char *error_str;

    /* Register the ACPI NFIT table if possible. */
    puts("Parsing system ACPI entries\n");
    if (ACPI_OK != acpi_parse(acpi)) {
        error_str = "Failed to parse ACPI structures";
        goto mftahdisk_error;
    }
    printf("   (RSDT %p / XSDT %p)\n", acpi->rsdt.address, acpi->xsdt.address);

    /* Only add an NVDIMM Root Device SSDT if one doesn't already exist. */
    if (NULL == acpi->ssdt_nvdimm_root) {
        puts("Updating NVDIMM Root Device entry\n");
        uint8_t *nvdimm_root_table = do_e820_malloc(Ramdisk_aml_len, 4);
        if (NULL == nvdimm_root_table) {
            error_str = "Out of memory";
            goto mftahdisk_error;
        }

        printf(
            "   Allocated ACPI SSDT NVDR entry at '0x%p' (%u)\n",
            nvdimm_root_table,
            Ramdisk_aml_len
        );

        /* memcpy the Ramdisk AML into the table and try to link it in. */
        memcpy(nvdimm_root_table, Ramdisk_aml, Ramdisk_aml_len);
        if (ACPI_OK != acpi_insert_table(acpi, nvdimm_root_table)) {
            error_str = "Could not register a new SSDT";
            goto mftahdisk_error;
        }
    }

    /* Allocate the NFIT ACPI header and a single SPA sub-structure. */
    puts("Registering an ACPI NFIT entry\n");
    nfit_raw_t *nfit_table = (nfit_raw_t *)
        do_e820_malloc(sizeof(nfit_raw_t) + sizeof(nfit_structure_spa_t), 4);
    if (NULL == nfit_table) {
        error_str = "Out of memory";
        goto mftahdisk_error;
    }

    /* Zero it out. */
    memset(nfit_table, 0x00, (sizeof(nfit_raw_t) + sizeof(nfit_structure_spa_t)));

    /* Populate the SDT header. */
    s_acpi_description_header_raw *nfit_raw =
        (s_acpi_description_header_raw *)&(nfit_table->header);
    const char *nfit_sig = NFIT;
    const char *oem_id      = "MFTAH ";
    const char *oem_table   = "MFTAHNVD";
    const char *creator     = "XMIT";
    guid_t pdisk_guid       = NFIT_SPA_GUID_RAMDISK_VIRT_V_DISK;

    memcpy(nfit_raw->signature, nfit_sig, 4);
    nfit_raw->length = (sizeof(nfit_raw_t) + sizeof(nfit_structure_spa_t));
    nfit_raw->revision = 1;
    memcpy(nfit_raw->oem_id, oem_id, 6);
    memcpy(nfit_raw->oem_table_id, oem_table, 8);
    nfit_raw->oem_revision = 0x1000;   /* Not really anything specific. */
    memcpy(nfit_raw->creator_id, creator, 4);
    nfit_raw->creator_revision = MFTAH_RELEASE_DATE;

    /* Fill out the SPA sub-structure. */
    nfit_structure_spa_t *spa = (nfit_structure_spa_t *)
        ((uintptr_t)nfit_table + sizeof(nfit_raw_t));
    spa->header.type = NFIT_TABLE_TYPE_SPA;
    spa->header.length = sizeof(nfit_structure_spa_t);
    memcpy(&(spa->addr_range_type_guid), &pdisk_guid, sizeof(guid_t));
    spa->range_base = (uint64_t)ramdisk_start;
    spa->range_length = (uint64_t)ramdisk_length;

    /* We need to see if an NFIT already exists. */
    if (NULL != acpi->nfit) {
        /* If so, append the SPA structure after relocating any adjacent
            tables that might be in the way. */
        /* This process is expensive, but worth doing. It mirrors the functionality
            of `parse_xsdt` in that it relocates colliding tables dynamically. */
        uintptr_t nfit_end_begin = (uintptr_t)((uintptr_t)(acpi->nfit) + acpi->nfit->length);
        uintptr_t nfit_end_new   = (uintptr_t)(nfit_end_begin + sizeof(nfit_structure_spa_t));

        for (uintptr_t p = nfit_end_begin; p < nfit_end_new; ++p) {
            for (int i = 0; i < (sizeof(ACPI_SIGS) / sizeof(const char *)); ++i) {
                if (0 != memcmp((void *)p, ACPI_SIGS[i], 4)) continue;

                acpi_relocate_table(acpi,
                                    (uint8_t **)&p,
                                    (uint8_t *)nfit_end_begin,
                                    (uint8_t *)nfit_end_new);
            }
        }

        memcpy((void *)nfit_end_begin, spa, sizeof(nfit_structure_spa_t));

        acpi->nfit->length += sizeof(nfit_structure_spa_t);
        acpi_table_checksum(acpi->nfit);
    } else {
        /* If not, link the new NFIT into the ACPI hierarchy. */
        if (ACPI_OK != acpi_insert_table(acpi, (uint8_t *)nfit_table)) {
            error_str = "Could not register an NFIT table";
            goto mftahdisk_error;
        }
    }

    printf("NFIT entry -- ");
    MEMDUMP(nfit_table, nfit_raw->length + 8);

    /* Finally done. */
    puts("OK - The Ramdisk is now available via ACPI!\n\n");
    return;

mftahdisk_error:
    printf("ERROR:  %s.\n", error_str);
    puts("   The ramdisk might not be discoverable by the OS!\n\n");
    pause("  Press any key to continue... ");
}
