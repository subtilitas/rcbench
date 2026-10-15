/*
 * Release signature check of a panel image: the pinned public key is parsed
 * and the ECDSA signature is verified by mbedTLS.  See fw_sign.h.
 *
 * Only calls with the same signature in mbedTLS 2.28 and 3.6 are used.  The
 * SHA-256 is the project's own (fw_sha256.c), whose name does not move
 * between mbedTLS versions.
 *
 * SPDX-License-Identifier: MIT
 */
#include "fw_sign.h"

#include <string.h>

#include "mbedtls/ecp.h"
#include "mbedtls/error.h"
#include "mbedtls/pk.h"
#include "mbedtls/version.h"

#include "fw_sha256.h"

/*
 * Parse the key text into @p pk.  True for an elliptic curve key on
 * secp256r1 (P-256); @p pk must be freed by the caller in every case.
 */
static bool key_load(mbedtls_pk_context *pk, const fw_pubkey_t *key)
{
    if (key == NULL || key->n == 0u || key->n >= FW_PEM_MAX
        || key->pem[key->n] != '\0') {
        return false;
    }
    /* The length includes the terminating NUL, as the PEM parser needs. */
    if (mbedtls_pk_parse_public_key(pk, (const unsigned char *)key->pem,
                                    key->n + 1u) != 0) {
        return false;
    }
    if (mbedtls_pk_get_type(pk) != MBEDTLS_PK_ECKEY) {
        return false;
    }
#if MBEDTLS_VERSION_MAJOR >= 3
    return mbedtls_ecp_keypair_get_group_id(mbedtls_pk_ec(*pk))
           == MBEDTLS_ECP_DP_SECP256R1;
#else
    return mbedtls_pk_ec(*pk)->grp.id == MBEDTLS_ECP_DP_SECP256R1;
#endif
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
    if (sig_n < FW_SIG_MIN || sig_n > FW_SIG_MAX) {
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
        } else if (e == MBEDTLS_ERR_ECP_VERIFY_FAILED) {
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

const char *fw_sig_str(fw_sig_t r)
{
    switch (r) {
    case FW_SIG_OK:   return "signature verifies";
    case FW_SIG_NONE: return "no signature";
    case FW_SIG_FORM: return "signature is not a P-256 DER signature";
    case FW_SIG_KEY:  return "release key is not a P-256 public key";
    case FW_SIG_BAD:
    default:          return "signature does not verify";
    }
}
