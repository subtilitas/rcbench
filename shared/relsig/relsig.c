/**
 * @file relsig.c
 * @brief The release signature's raw form and its verification
 *        (relsig.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "relsig.h"

#include <string.h>

#include "mbedtls/pk.h"
#include "mbedtls/psa_util.h"
#include "psa/crypto.h"

/** @brief The curve's size, bits. */
#define BITS 256u
/** @brief The longest key text held, with its terminating NUL. */
#define PEM_MAX 256u

bool relsig_der_to_raw(const uint8_t *der, size_t n,
                       uint8_t raw[RELSIG_RAW_LEN])
{
    size_t len = 0u;
    if (der == NULL || raw == NULL || n == 0u || n > RELSIG_DER_MAX) {
        return false;
    }
    return mbedtls_ecdsa_der_to_raw(BITS, der, n, raw, RELSIG_RAW_LEN, &len)
               == 0
           && len == RELSIG_RAW_LEN;
}

bool relsig_raw_to_der(const uint8_t raw[RELSIG_RAW_LEN],
                       uint8_t der[RELSIG_DER_MAX], size_t *n)
{
    static const uint8_t zero[RELSIG_RAW_LEN / 2u];
    if (raw == NULL || der == NULL || n == NULL
        || memcmp(raw, zero, sizeof zero) == 0
        || memcmp(raw + sizeof zero, zero, sizeof zero) == 0) {
        return false;
    }
    return mbedtls_ecdsa_raw_to_der(BITS, raw, RELSIG_RAW_LEN, der,
                                    RELSIG_DER_MAX, n) == 0;
}

bool relsig_verify(const char *pem, size_t pem_n, const uint8_t sha[32],
                   const uint8_t raw[RELSIG_RAW_LEN])
{
    char text[PEM_MAX];
    uint8_t der[RELSIG_DER_MAX];
    size_t der_n = 0u;
    if (pem == NULL || sha == NULL || raw == NULL) {
        return false;
    }
    /* A trailing NUL (an embedded text file carries one) is not text. */
    while (pem_n > 0u && pem[pem_n - 1u] == '\0') {
        --pem_n;
    }
    if (pem_n == 0u || pem_n >= sizeof text
        || !relsig_raw_to_der(raw, der, &der_n)
        || psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    memcpy(text, pem, pem_n);
    text[pem_n] = '\0';
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    /* The length includes the terminating NUL, as the PEM parser needs. */
    bool ok = mbedtls_pk_parse_public_key(&pk, (const unsigned char *)text,
                                          pem_n + 1u) == 0
              && mbedtls_pk_get_key_type(&pk)
                     == PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1)
              && mbedtls_pk_get_bitlen(&pk) == BITS;
    if (ok) {
        ok = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, sha, 32u, der, der_n)
             == 0;
    }
    mbedtls_pk_free(&pk);
    return ok;
}
