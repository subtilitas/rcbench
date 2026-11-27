/**
 * @file relsig.h
 * @brief The release signature in the raw form a device takes over
 *        Bluetooth LE (protocols/bleota, protocol 2), and its verification
 *        over a SHA-256 (Secure Hash Algorithm, 256 bit) with the release
 *        key.
 *
 * The release signs each image with ECDSA (Elliptic Curve Digital
 * Signature Algorithm) on curve P-256 and publishes the DER (Distinguished
 * Encoding Rules) form, 8 to 72 bytes (shared/fwupdate/include/fw_sig.h).
 * The raw form is r then s, 32 bytes each, big-endian: 64 bytes in every
 * case, four HASH parts of 16 bytes.  The panel turns the DER form into
 * the raw one before it sends it; the device turns it back and verifies.
 *
 * Needs the Mbed TLS 4 headers and library (TF-PSA-Crypto): the ESP-IDF
 * (Espressif Internet-of-Things Development Framework) component on the
 * devices and the panel, the release that test/host/CMakeLists.txt
 * fetches on the host.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RELSIG_H
#define RCBENCH_RELSIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The raw signature: r then s, bytes. */
#define RELSIG_RAW_LEN 64u
/** @brief The longest DER signature of P-256, bytes. */
#define RELSIG_DER_MAX 72u

/**
 * @brief The raw form of a DER signature.
 * @param der the DER signature
 * @param n   its bytes
 * @param raw receives r then s
 * @return false for NULL or a DER form that is no P-256 signature
 */
bool relsig_der_to_raw(const uint8_t *der, size_t n,
                       uint8_t raw[RELSIG_RAW_LEN]);

/**
 * @brief The DER form of a raw signature.
 * @param raw r then s
 * @param der receives the DER form, up to RELSIG_DER_MAX bytes
 * @param n   receives its length
 * @return false for NULL or an r or s of zero
 */
bool relsig_raw_to_der(const uint8_t raw[RELSIG_RAW_LEN],
                       uint8_t der[RELSIG_DER_MAX], size_t *n);

/**
 * @brief Whether @p raw is a signature of @p sha under the public key in
 *        @p pem.
 * @param pem   the key as `openssl ec -pubout` writes it; trailing NULs
 *              are not text
 * @param pem_n its bytes
 * @param sha   the SHA-256 that was signed
 * @param raw   the signature, r then s
 * @return true only for a P-256 key and a signature that verifies
 */
bool relsig_verify(const char *pem, size_t pem_n, const uint8_t sha[32],
                   const uint8_t raw[RELSIG_RAW_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_RELSIG_H */
