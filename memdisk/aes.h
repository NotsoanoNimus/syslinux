#ifndef MBR_AES_H
#define MBR_AES_H

/*****************************************************************************/
/* Includes:                                                                 */
/*****************************************************************************/
#include <stddef.h>
#include <stdint.h>



/*****************************************************************************/
/* Static structs and other definitions:                                     */
/*****************************************************************************/
#define AES_KEYLEN 32
#define AES_keyExpSize 240

/* Block length in bytes. 128-bit blocks only. */
#define AES_BLOCKLEN 16

typedef
struct AES_ctx {
    uint8_t RoundKey[AES_keyExpSize];
    uint8_t Iv[AES_BLOCKLEN];
} aes_ctx_t;


/**
 * Initialize a new context with an IV.
 */
void
AES_init_ctx_iv(
    struct AES_ctx  *ctx,
    const uint8_t   *key,
    const uint8_t   *iv
);

/*
 * The buffer size MUST be a mutiple of AES_BLOCKLEN.
 * NOTES:
 *   - Need to set IV in ctx via AES_init_ctx_iv()
 *   - No IV should ever be reused with the same key 
 */
void
AES_CBC_decrypt_buffer(
    struct AES_ctx  *ctx,
    uint8_t         *buf,
    size_t          length
);


/*****************************************************************************/
/* Defines:                                                                  */
/*****************************************************************************/
/* Number of columns comprising a state in AES. */
#define Nb 4

/* Number of 32-bit words in a key. */
#define Nk 8

/* Number of rounds in AES Cipher. */
#define Nr 14


// jcallan@github points out that declaring Multiply as a function 
// reduces code size considerably with the Keil ARM compiler.
// See this link for more information: https://github.com/kokke/tiny-AES-C/pull/3
#ifndef MULTIPLY_AS_A_FUNCTION
    #define MULTIPLY_AS_A_FUNCTION 0
#endif




#endif   /* MBR_AES_H */
