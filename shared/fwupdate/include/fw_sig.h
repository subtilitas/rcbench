/**
 * @file fw_sig.h
 * @brief The verdict on the release signature of a panel image, what the
 *        panel does with it, and the signature's text form on the Wi-Fi
 *        page.
 *
 * The release workflow signs each image file with ECDSA (Elliptic Curve
 * Digital Signature Algorithm) on curve P-256 over its SHA-256 (Secure Hash
 * Algorithm, 256 bit) and publishes the signature beside it as
 * `<image>.sig`: the DER (Distinguished Encoding Rules) form that
 * `openssl dgst -sha256 -sign` writes, FW_SIG_MIN to FW_SIG_MAX bytes.
 * fw_sign.h verifies one; this header holds what needs no cryptography
 * library: the verdict, the policy and the hex form.
 *
 * The policy is one compile-time switch, RCBENCH_SIGN_ENFORCE.  At 0 (the
 * default) an image without a valid signature installs with the single 2s
 * INSTALL hold and a warning on the screen.  At 1 it needs a second 2s hold,
 * INSTALL ANYWAY, after the first.  A valid signature installs with the one
 * hold either way.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_FW_SIG_H
#define RCBENCH_FW_SIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RCBENCH_SIGN_ENFORCE
/**
 * @brief 1: an image without a valid release signature needs the second
 *        hold (INSTALL ANYWAY); 0: it installs with the one hold and a
 *        warning.
 */
#define RCBENCH_SIGN_ENFORCE 0
#endif

/** @brief The longest DER signature of P-256: 2 + 2 x (2 + 33) bytes. */
#define FW_SIG_MAX 72u
/** @brief The shortest: 2 + 2 x (2 + 1) bytes. */
#define FW_SIG_MIN 8u
/** @brief The hex form of the longest signature: 2 digits a byte. */
#define FW_SIG_HEX_MAX (2u * FW_SIG_MAX)

/** @brief What the release key says about an image. */
typedef enum {
    FW_SIG_NONE = 0, /**< no signature came with the image, or none was
                          checked                                     */
    FW_SIG_OK,       /**< the signature verifies under the release key  */
    FW_SIG_FORM,     /**< not a DER signature of P-256, or the wrong size */
    FW_SIG_KEY,      /**< the key is not a P-256 public key             */
    FW_SIG_BAD,      /**< well formed, and it does not verify           */
} fw_sig_t;

/**
 * @brief Whether the build enforces the release signature.
 * @return true when RCBENCH_SIGN_ENFORCE is not 0
 */
bool fw_sig_enforced(void);

/**
 * @brief Whether an image with verdict @p s needs the second hold, INSTALL
 *        ANYWAY, before it is written.
 * @param s       the verdict
 * @param enforce the policy: fw_sig_enforced() on the panel
 * @return true for any verdict other than FW_SIG_OK with @p enforce; false
 *         otherwise
 */
bool fw_sig_needs_ack(fw_sig_t s, bool enforce);

/**
 * @brief Whether the screen says the image is signed (true) or warns that
 *        it is not (false).
 * @param s the verdict
 * @return true for FW_SIG_OK only
 */
bool fw_sig_signed(fw_sig_t s);

/**
 * @brief Whether a signature came and did not verify, as opposed to none
 *        coming: the screen's reason line.
 * @param s the verdict
 * @return true for FW_SIG_FORM, FW_SIG_KEY and FW_SIG_BAD
 */
bool fw_sig_bad(fw_sig_t s);

/**
 * @brief Read @p n characters of hex digits into the bytes of a signature.
 *
 * Upper and lower case are both taken.  Nothing else is: no spaces, no
 * prefix.
 *
 * @param hex   the digits, not terminated
 * @param n     the digits' count
 * @param out   takes the bytes, FW_SIG_MAX at most
 * @param out_n takes the count of bytes, 0 on false
 * @return true for an even count from 2 x FW_SIG_MIN to FW_SIG_HEX_MAX
 *         digits; false for any other count, a character that is not a hex
 *         digit, or a NULL argument
 */
bool fw_sig_from_hex(const char *hex, size_t n, uint8_t out[FW_SIG_MAX],
                     uint32_t *out_n);

/**
 * @brief The English name of @p s, for the console.
 * @param s the verdict
 * @return the name, static
 */
const char *fw_sig_str(fw_sig_t s);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_FW_SIG_H */
