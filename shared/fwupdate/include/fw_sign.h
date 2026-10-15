/*
 * The release signature of a panel application image.
 *
 * The release workflow signs the image file, byte for byte, with ECDSA
 * (Elliptic Curve Digital Signature Algorithm) on curve P-256 over its
 * SHA-256 (Secure Hash Algorithm, 256 bit), and publishes the signature as
 * `<image>.bin.sig`: the DER (Distinguished Encoding Rules) form that
 * `openssl dgst -sha256 -sign` writes, a SEQUENCE of the two integers r and
 * s, FW_SIG_MIN to FW_SIG_MAX bytes.  The panel holds the matching public
 * key and verifies the whole image against it before the install path
 * starts.
 *
 * A signature is optional: none, a malformed one and one that does not
 * verify each give a result other than FW_SIG_OK, and the caller decides
 * what to do with the image (the panel installs it after a second hold).
 *
 * The verification is mbedTLS's (mbedtls_pk_verify over the SHA-256 made by
 * fw_sha256).  mbedTLS reads the DER form with its own parser: a form it
 * cannot read, including bytes after the SEQUENCE, gives FW_SIG_FORM; it
 * does not insist on the shortest integer encoding, so such a form can still
 * verify.
 *
 * Needs the mbedTLS headers and library: the ESP-IDF (Espressif
 * Internet-of-Things Development Framework) component on the panel, a source
 * tree on the host.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FW_SIGN_H
#define RCBENCH_FW_SIGN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The longest DER signature of P-256: 2 + 2 x (2 + 33) bytes. */
#define FW_SIG_MAX 72u
/** The shortest: 2 + 2 x (2 + 1) bytes. */
#define FW_SIG_MIN 8u

/** The longest key text held, with its terminating NUL. */
#define FW_PEM_MAX 256u

/** A public key: its PEM text, parsed again for each verification. */
typedef struct {
    char pem[FW_PEM_MAX];
    size_t n;            /**< text length, without the terminating NUL */
} fw_pubkey_t;

typedef enum {
    FW_SIG_OK = 0,   /**< the signature verifies under the key            */
    FW_SIG_NONE,     /**< no signature was given                          */
    FW_SIG_FORM,     /**< not a DER signature of P-256, or the wrong size */
    FW_SIG_KEY,      /**< the key is not a P-256 public key               */
    FW_SIG_BAD,      /**< well formed, and it does not verify             */
} fw_sig_t;

/**
 * Take the public key out of @p n bytes of PEM (Privacy-Enhanced Mail)
 * text, as `openssl ec -pubout` writes it.  A trailing NUL is allowed.
 * False for anything that mbedTLS does not parse as an elliptic curve
 * public key on secp256r1 (P-256), for a text of FW_PEM_MAX bytes or more,
 * and for NULL arguments.
 */
bool fw_pubkey_parse_pem(const char *pem, size_t n, fw_pubkey_t *out);

/**
 * Verify @p sig of @p sig_n bytes over the SHA-256 of @p bytes at @p img.
 * NONE for a NULL @p sig or 0 bytes, FORM for a size outside FW_SIG_MIN to
 * FW_SIG_MAX or a DER form mbedTLS cannot read, KEY for a key that is not a
 * P-256 public key, BAD for a signature that does not verify.
 */
fw_sig_t fw_sig_verify(const fw_pubkey_t *key, const uint8_t *img,
                       uint32_t bytes, const uint8_t *sig, size_t sig_n);

/** As fw_sig_verify(), over a SHA-256 already computed. */
fw_sig_t fw_sig_verify_hash(const fw_pubkey_t *key, const uint8_t hash[32],
                            const uint8_t *sig, size_t sig_n);

/** The English name of @p r, for the console. */
const char *fw_sig_str(fw_sig_t r);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_SIGN_H */
