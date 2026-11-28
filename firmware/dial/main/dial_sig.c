/**
 * @file dial_sig.c
 * @brief The Dial's check of an update's release signature (dial.h): the
 *        release key compiled into the image, verified by shared/relsig.
 *
 * SPDX-License-Identifier: MIT
 */
#include "dial.h"

#include <string.h>

#include "relsig.h"

/** @brief The release key, firmware/panel/main/keys/release_signing_pub.pem,
 *         embedded by main/CMakeLists.txt (EMBED_TXTFILES) with a
 *         terminating NUL. */
extern const char _binary_release_signing_pub_pem_start[];

bool dial_sig_verify(const uint8_t sha[32], const uint8_t sig[64])
{
    const char *pem = _binary_release_signing_pub_pem_start;
    return relsig_verify(pem, strlen(pem), sha, sig);
}
