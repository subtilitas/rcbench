/**
 * @file fw_sign.h
 * @brief Verify the release signature of an image against a public key.
 *
 * fw_sig.h says what the signature is and what the panel does with the
 * verdict.  This half does the cryptography: it parses the release public
 * key from its PEM (Privacy-Enhanced Mail) text and verifies an ECDSA
 * (Elliptic Curve Digital Signature Algorithm) P-256 signature over the
 * SHA-256 (Secure Hash Algorithm, 256 bit) of a whole image with Mbed TLS.
 *
 * The DER (Distinguished Encoding Rules) form is checked here before Mbed
 * TLS sees it: a SEQUENCE of two INTEGERs of 1 to 33 bytes each, with no
 * byte after it.  Anything else is FW_SIG_FORM.  Mbed TLS does not insist
 * on the shortest integer encoding, so such a form can still verify.
 *
 * Needs the Mbed TLS 4 headers and library (TF-PSA-Crypto): the ESP-IDF
 * (Espressif Internet-of-Things Development Framework) component on the
 * panel, the release that test/host/CMakeLists.txt fetches on the host.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FW_SIGN_H
#define RCBENCH_FW_SIGN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fw_sig.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The longest key text held, with its terminating NUL. */
#define FW_PEM_MAX 256u

/** @brief A public key: its PEM text, parsed again for each verification. */
typedef struct {
    char   pem[FW_PEM_MAX]; /**< the text, NUL-terminated               */
    size_t n;               /**< text length, without the terminator   */
} fw_pubkey_t;

/**
 * @brief Take the public key out of @p n bytes of PEM text, as
 *        `openssl ec -pubout` writes it.  Trailing NULs are not text.
 * @param pem the text
 * @param n   its bytes
 * @param out takes the key; unchanged on false
 * @return false for text Mbed TLS does not parse as an elliptic curve
 *         public key on secp256r1 (P-256), for a text of FW_PEM_MAX bytes or
 *         more, and for a NULL argument
 */
bool fw_pubkey_parse_pem(const char *pem, size_t n, fw_pubkey_t *out);

/**
 * @brief Verify @p sig of @p sig_n bytes over the SHA-256 of @p bytes at
 *        @p img.
 * @param key   the release key
 * @param img   the image
 * @param bytes its length
 * @param sig   the DER signature; NULL for none
 * @param sig_n its length
 * @return FW_SIG_NONE for a NULL @p sig or 0 bytes; FW_SIG_FORM for a size
 *         outside FW_SIG_MIN to FW_SIG_MAX or a form that is not a DER
 *         signature; FW_SIG_KEY for a key that is not a P-256 public key;
 *         FW_SIG_BAD for a signature that does not verify; FW_SIG_OK
 */
fw_sig_t fw_sig_verify(const fw_pubkey_t *key, const uint8_t *img,
                       uint32_t bytes, const uint8_t *sig, size_t sig_n);

/**
 * @brief As fw_sig_verify(), over a SHA-256 already computed.
 * @param key   the release key
 * @param hash  the image's SHA-256, 32 bytes
 * @param sig   the DER signature; NULL for none
 * @param sig_n its length
 * @return as fw_sig_verify()
 */
fw_sig_t fw_sig_verify_hash(const fw_pubkey_t *key, const uint8_t hash[32],
                            const uint8_t *sig, size_t sig_n);

/**
 * @brief Whether @p sig of @p n bytes has the DER form of an ECDSA P-256
 *        signature: SEQUENCE { INTEGER r, INTEGER s }, each 1 to 33 bytes,
 *        short lengths, nothing after it.
 * @param sig the bytes
 * @param n   their count
 * @return true for that form
 */
bool fw_sig_der_ok(const uint8_t *sig, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_SIGN_H */
