/**
 * @file
 * @brief The release key's verdict on a panel image, for the two ways one
 *        arrives: the Wi-Fi page's upload and the download from GitHub.
 *
 * The key is the public half of the release signing key,
 * main/keys/release_signing_pub.pem, embedded as text in this component.
 * The verification is shared/fwupdate's (fw_sign.h); the policy that
 * follows from the verdict is fw_sig.h's.  Both calls hash the whole image
 * once more, in PSRAM (pseudo-static RAM), before the verdict.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_WIFI_SIG_H
#define RCBENCH_WIFI_SIG_H

#include <stdint.h>

#include "esp_http_server.h"
#include "fw_sig.h"

/**
 * @brief wa_run_t's verify hook: the verdict on @p bytes at @p img and the
 *        @p sig_n bytes of DER signature at @p sig.
 * @param ctx   unused
 * @param img   the image
 * @param bytes its length
 * @param sig   the signature; NULL for none
 * @param sig_n its length
 * @return the verdict; FW_SIG_KEY when the embedded key does not parse
 */
fw_sig_t wifi_sig_verify(void *ctx, const uint8_t *img, uint32_t bytes,
                         const uint8_t *sig, uint32_t sig_n);

/**
 * @brief The verdict on an upload: the signature is the hex digits of the
 *        query parameter "sig" of the upload's URL (the page sends it so
 *        when a .sig file is chosen beside the image).
 * @param r     the upload's request
 * @param img   the image received and checked
 * @param bytes its length
 * @return FW_SIG_NONE without the parameter; FW_SIG_FORM for a value that
 *         is not 16 to 144 hex digits; otherwise wifi_sig_verify()'s
 */
fw_sig_t wifi_sig_upload(httpd_req_t *r, const uint8_t *img, uint32_t bytes);

#endif /* RCBENCH_WIFI_SIG_H */
