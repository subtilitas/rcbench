/**
 * @file fw_sign.c
 * @brief The release signature check: the pinned public key is parsed and
 *        the ECDSA signature verified by Mbed TLS 4.  See fw_sign.h.
 *
 * The SHA-256 is the project's own (fw_sha256.c), so the image is hashed by
 * the same code that checks its appended hash.
 *
 * SPDX-License-Identifier: MIT
 */
#include "fw_sign.h"

#include <string.h>

#include "mbedtls/pk.h"
#include "psa/crypto.h"

#include "fw_sha256.h"

/** @brief DER tag of a SEQUENCE. */
#define DER_SEQ 0x30u
/** @brief DER tag of an INTEGER. */
#define DER_INT 0x02u
/** @brief The longest integer of P-256: 32 bytes and a leading zero. */
#define DER_INT_MAX 33u

/**
 * @brief One INTEGER of the signature at @p p[*at].
 * @param p   the signature
 * @param n   its bytes
 * @param at  the offset; moved past the integer on true
 * @return true for a tag, a short length of 1 to DER_INT_MAX and that many
 *         bytes within @p n
 */
static bool der_int(const uint8_t *p, size_t n, size_t *at)
{
    if (*at + 2u > n || p[*at] != DER_INT) {
        return false;
    }
    const size_t len = p[*at + 1u];
    if (len == 0u || len > DER_INT_MAX || *at + 2u + len > n) {
        return false;
    }
    *at += 2u + len;
    return true;
}

bool fw_sig_der_ok(const uint8_t *sig, size_t n)
{
    if (sig == NULL || n < FW_SIG_MIN || n > FW_SIG_MAX
        || sig[0] != DER_SEQ || (size_t)sig[1] + 2u != n) {
        return false;
    }
    size_t at = 2u;
    if (!der_int(sig, n, &at)) {       /* r */
        return false;
    }
    if (!der_int(sig, n, &at)) {       /* s */
        return false;
    }
    return at == n;
}

/**
 * @brief Parse the key text into @p pk.
 * @param pk  an initialised context; the caller frees it in every case
 * @param key the key text
 * @return true for an elliptic curve public key on secp256r1 (P-256)
 */
static bool key_load(mbedtls_pk_context *pk, const fw_pubkey_t *key)
{
    if (key == NULL || key->n == 0u || key->n >= FW_PEM_MAX
        || key->pem[key->n] != '\0') {
        return false;
    }
    /* Mbed TLS 4 runs every key operation through PSA (Platform Security
     * Architecture) Crypto; a second call returns at once. */
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    /* The length includes the terminating NUL, as the PEM parser needs. */
    if (mbedtls_pk_parse_public_key(pk, (const unsigned char *)key->pem,
                                    key->n + 1u) != 0) {
        return false;
    }
    return mbedtls_pk_get_key_type(pk)
               == PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1)
           && mbedtls_pk_get_bitlen(pk) == 256u;
}

bool fw_pubkey_parse_pem(const char *pem, size_t n, fw_pubkey_t *out)
{
    if (pem == NULL || out == NULL) {
        return false;
    }
    /* A trailing NUL (an embedded text file carries one) is not text. */
    while (n > 0u && pem[n - 1u] == '\0') {
        --n;
    }
    if (n == 0u || n >= FW_PEM_MAX) {
        return false;
    }
    fw_pubkey_t k;
    memset(&k, 0, sizeof(k));
    memcpy(k.pem, pem, n);
    k.n = n;
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    const bool ok = key_load(&pk, &k);
    mbedtls_pk_free(&pk);
    if (ok) {
        *out = k;
    }
    return ok;
}

fw_sig_t fw_sig_verify_hash(const fw_pubkey_t *key, const uint8_t hash[32],
                            const uint8_t *sig, size_t sig_n)
{
    if (sig == NULL || sig_n == 0u) {
        return FW_SIG_NONE;
    }
    if (!fw_sig_der_ok(sig, sig_n)) {
        return FW_SIG_FORM;
    }
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    fw_sig_t r = FW_SIG_KEY;
    if (key_load(&pk, key)) {
        const int e = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash,
                                        FW_SHA256_BYTES, sig, sig_n);
        if (e == 0) {
            r = FW_SIG_OK;
        } else if (e == PSA_ERROR_INVALID_SIGNATURE) {
            r = FW_SIG_BAD;
        } else {
            r = FW_SIG_FORM;
        }
    }
    mbedtls_pk_free(&pk);
    return r;
}

fw_sig_t fw_sig_verify(const fw_pubkey_t *key, const uint8_t *img,
                       uint32_t bytes, const uint8_t *sig, size_t sig_n)
{
    if (sig == NULL || sig_n == 0u) {
        return FW_SIG_NONE;
    }
    uint8_t h[FW_SHA256_BYTES];
    fw_sha256(img, bytes, h);
    return fw_sig_verify_hash(key, h, sig, sig_n);
}
