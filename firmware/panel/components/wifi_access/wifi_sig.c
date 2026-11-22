/**
 * @file
 * @brief The release key's verdict on a panel image.  See wifi_sig.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_sig.h"

#include <string.h>

#include "esp_log.h"
#include "fw_sign.h"
#include "wa_page.h"

/** @brief The console tag of the verdict's line. */
static const char *TAG = "fwsig";

/** @brief The release key's PEM text, embedded with a terminating NUL. */
extern const char _binary_release_signing_pub_pem_start[];

/** @brief The longest query the page sends: "sig=" and the digits. */
#define SIG_QUERY_MAX (4u + FW_SIG_HEX_MAX)

fw_sig_t wifi_sig_verify(void *ctx, const uint8_t *img, uint32_t bytes,
                         const uint8_t *sig, uint32_t sig_n)
{
    (void)ctx;
    fw_pubkey_t key;
    const char *pem = _binary_release_signing_pub_pem_start;
    const fw_sig_t v = fw_pubkey_parse_pem(pem, strlen(pem), &key)
                           ? fw_sig_verify(&key, img, bytes, sig, sig_n)
                           : FW_SIG_KEY;
    if (v == FW_SIG_OK) {
        ESP_LOGI(TAG, "%lu bytes: %s", (unsigned long)bytes, fw_sig_str(v));
    } else {
        ESP_LOGW(TAG, "%lu bytes: %s", (unsigned long)bytes, fw_sig_str(v));
    }
    return v;
}

fw_sig_t wifi_sig_upload(httpd_req_t *r, const uint8_t *img, uint32_t bytes)
{
    /* The query is read up to one byte past the longest the page sends,
     * so that a longer one is told apart. */
    char q[SIG_QUERY_MAX + 2u];
    char hex[FW_SIG_HEX_MAX + 2u];
    const size_t qn = httpd_req_get_url_query_len(r);
    if (qn == 0u) {
        return FW_SIG_NONE;
    }
    if (qn > SIG_QUERY_MAX
        || httpd_req_get_url_query_str(r, q, sizeof(q)) != ESP_OK) {
        return FW_SIG_FORM;
    }
    const esp_err_t e = httpd_query_key_value(q, WA_SIG_PARAM, hex,
                                              sizeof(hex));
    if (e == ESP_ERR_NOT_FOUND) {
        return FW_SIG_NONE;
    }
    uint8_t  sig[FW_SIG_MAX];
    uint32_t n = 0u;
    if (e != ESP_OK || !fw_sig_from_hex(hex, strlen(hex), sig, &n)) {
        ESP_LOGW(TAG, "upload: the signature is not 16 to 144 hex digits");
        return FW_SIG_FORM;
    }
    return wifi_sig_verify(NULL, img, bytes, sig, n);
}
