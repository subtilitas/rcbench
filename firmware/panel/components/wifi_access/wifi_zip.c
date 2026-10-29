/**
 * @file
 * @brief GET /card.zip on the page's server.  See wifi_zip.h.
 *
 * The archive is wa_zip.c's; this is the request around it: the guard and
 * the access code (wifi_page_begin()), the card held from the plan to the
 * last file, the read buffer, the socket, and the log line at the end.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wifi_zip.h"

#include <stdint.h>
#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "storage.h"
#include "wa_session.h"
#include "wa_zip.h"
#include "wifi_access_int.h"
#include "wifi_sock.h"

/** @brief The log tag, wifi_access.c's. */
static const char *TAG = "wifi";

/**
 * @brief Milliseconds since start-up.
 * @return them, wrapping at 2^32
 */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief A refusal with no body.
 * @param r       the request
 * @param status  the status line
 * @return the send's result
 */
static esp_err_t refused(httpd_req_t *r, const char *status)
{
    httpd_resp_set_status(r, status);
    return httpd_resp_sendstr(r, "");
}

/**
 * @brief The ZIP's read buffer, WA_ZIP_READ_BYTES of PSRAM aligned as the
 *        SDMMC (SD/MMC host) DMA wants a PSRAM buffer, so each read of a
 *        file goes from the card into it in one transfer and no bounce
 *        buffer of the driver's.
 * @return the buffer, or NULL without the PSRAM
 */
static uint8_t *zip_read_buf(void)
{
    /* MALLOC_CAP_CACHE_ALIGNED: start and size on the PSRAM cache's line,
     * as sd_host_check_buffer_alignment() asks of a DMA buffer. */
    return heap_caps_malloc(WA_ZIP_READ_BYTES,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_CACHE_ALIGNED);
}

esp_err_t wifi_zip_get(httpd_req_t *r)
{
    esp_err_t g = ESP_OK;
    wa_guard_t guard = WA_GUARD_OK;
    const wifi_page_go_t go = wifi_page_begin(r, &g, &guard);
    if (go == WIFI_PAGE_DONE) {
        return g;
    }
    if (go == WIFI_PAGE_REFUSED) {
        /* As the page's other refusals of the guard: no body, and the
         * connection goes with the answer (wifi_req.h). */
        httpd_resp_set_hdr(r, "Connection", "close");
        (void)refused(r, wa_guard_status(guard));
        return ESP_FAIL;
    }
    if (go != WIFI_PAGE_ANSWER) {
        wifi_page_end();
        return refused(r, "403 Forbidden");
    }
    const wa_fs_t *card = wifi_page_card();
    size_t io_cap = 0u;
    uint8_t *io = wifi_page_io(&io_cap);
    wa_zip_t *z = heap_caps_malloc(sizeof(*z), MALLOC_CAP_SPIRAM);
    uint8_t *rbuf = zip_read_buf();
    wa_zip_plan_t plan;
    wa_zip_slot_t *slot = NULL;
    esp_err_t res = ESP_FAIL;
    /* One volume from the plan's sizes to the last file: held across the
     * whole archive, as h_file() holds one file. */
    const bool held = storage_hold();
    if (z == NULL || rbuf == NULL || !held
        || wa_zip_plan(card, &plan) != WA_ZIP_OK) {
        res = refused(r, (z == NULL || rbuf == NULL)
                         ? "503 Service Unavailable" : "404 Not Found");
    } else {
        /* A slot an entry counted, and WA_ZIP_SPARE more for files added
         * before wa_zip_begin() walks the card again. */
        const uint64_t slots = plan.entries + WA_ZIP_SPARE;
        slot = (slots <= SIZE_MAX / sizeof(*slot))
               ? heap_caps_malloc((size_t)slots * sizeof(*slot),
                                  MALLOC_CAP_SPIRAM)
               : NULL;
        const wa_zip_err_t be = (slot == NULL)
                                ? WA_ZIP_MEMORY
                                : wa_zip_begin(z, card, slot, slots);
        if (be != WA_ZIP_OK) {
            res = refused(r, (be == WA_ZIP_NO_CARD) ? "404 Not Found"
                                                    : "503 Service Unavailable");
        } else {
            wa_zip_set_read(z, rbuf, WA_ZIP_READ_BYTES);
            char zname[WA_NAME_MAX + 8u];
            snprintf(zname, sizeof(zname), "%s.zip", wifi_page_name());
            const uint32_t t0 = now_ms();
            uint64_t sent = 0u;
            wa_send_res_t sres = wifi_page_raw_head(r, z->plan.bytes, zname);
            bool ok = sres == WA_SEND_DONE;
            while (ok) {
                const int32_t got = wa_zip_next(z, io, (uint32_t)io_cap);
                if (got <= 0) {
                    ok = (got == 0);
                    break;
                }
                sres = wifi_page_raw_send(r, io, (size_t)got);
                ok = (sres == WA_SEND_DONE);
                if (ok) {
                    sent += (uint64_t)got;
                    wifi_page_traffic();  /* a long download is traffic */
                }
            }
            /* The cause: the archive's own failure, with the entry the
             * walk was at, or the send's. */
            const bool zfail = (sres == WA_SEND_DONE && !ok);
            ESP_LOG_LEVEL_LOCAL(ok ? ESP_LOG_INFO : ESP_LOG_WARN, TAG,
                                "zip: %s%s%s, %llu of %llu B in %lu ms",
                                zfail ? wa_zip_err_name(z->err)
                                      : wa_send_res_name(sres),
                                zfail ? " at " : "",
                                zfail ? z->walk.path : "",
                                (unsigned long long)sent,
                                (unsigned long long)z->plan.bytes,
                                (unsigned long)(now_ms() - t0));
            if (!ok) {
                wa_zip_end_early(z);
            }
            res = ok ? ESP_OK : wifi_sock_drop(httpd_req_to_sockfd(r));
        }
    }
    heap_caps_free(slot);
    heap_caps_free(rbuf);
    heap_caps_free(z);
    if (held) {
        storage_release();
    }
    wifi_page_end();
    return res;
}
