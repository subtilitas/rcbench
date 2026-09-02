/*
 * The CAN (Controller Area Network) bring-up self-test; selftest.h says what
 * it answers.
 *
 * SPDX-License-Identifier: MIT
 */

#include "selftest.h"

#ifdef RCBENCH_CAN_SELFTEST

#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "can_selftest.h"
#include "can_twai.h"

static const char *TAG = "rcbench";

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Ask the far end what it can see.  False if it did not answer. */
static bool can_ask_remote(can_remote_status_t *out)
{
    for (int try = 0; try < 5; ++try) {
        link_can_frame_t req, in;
        if (!can_selftest_status_request(&req) || !can_twai_send(&req, 10)) {
            return false;
        }
        const uint32_t deadline = now_ms() + 50u;
        while (now_ms() < deadline) {
            if (can_twai_recv(&in, 10) && can_selftest_status_parse(&in, out)) {
                return true;
            }
        }
    }
    return false;
}

void can_selftest_run(uint32_t seconds)
{
    /*
     * The far end's counter runs from its own boot, so a reading taken only
     * at the end is a total and not a measurement.  Sampling either side of
     * the echo phase turns it into one.
     */
    can_remote_status_t before;
    const bool have_before = can_ask_remote(&before);

    can_selftest_t st;
    can_selftest_init(&st, 50);
    const uint32_t until = now_ms() + seconds * 1000u;
    uint32_t queue_full = 0;
    bool said_unacked = false;

    while (now_ms() < until) {
        link_can_frame_t probe;
        if (can_selftest_probe(&st, now_ms(), &probe)) {
            const int64_t sent_us = esp_timer_get_time();
            if (!can_twai_send(&probe, 10)) {
                /*
                 * Counted, not printed: a bus with nobody on it fills the
                 * queue on every pass, and the count goes into the report.
                 */
                ++queue_full;
            }
            link_can_frame_t in;
            if (can_twai_recv(&in, 20)) {
                can_selftest_rx(&st, &in,
                                (uint32_t)(esp_timer_get_time() - sent_us));
            }
        }
        can_selftest_tick(&st, now_ms());

        /*
         * Said one time, as early as possible.  A CAN transmitter needs one
         * other node to pull the acknowledge slot dominant; without one every
         * frame fails and is retried, and the transmit error counter climbs
         * by 8 each time.  128 is error-passive and means no other node is
         * on the bus, a different fault from a bus that corrupts frames.
         */
        uint32_t tec = 0;
        can_twai_errors(&tec, NULL, NULL, NULL);
        if (!said_unacked && tec >= 128u) {
            said_unacked = true;
            ESP_LOGE(TAG, "nothing is acknowledging: transmit errors reached "
                          "%lu, so no other node is answering on this bus",
                     (unsigned long)tec);
            ESP_LOGE(TAG, "  coprocessor powered and its own console showing "
                          "CAN up? CANH/CANL the right way round?");
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    /*
     * The controller's bus error count separates two faults the verdict
     * cannot tell apart on its own: frames corrupted on the wire, and frames
     * that arrived intact and were dropped unread.  It is filled in before
     * the verdict is computed.
     */
    uint32_t tx_err = 0, rx_err = 0, bus_err = 0;
    bool bus_off = false;
    can_twai_errors(&tx_err, &rx_err, &bus_err, &bus_off);
    st.bus_errors = bus_err;

    can_remote_status_t remote;
    const bool have_remote = can_ask_remote(&remote);

    const can_selftest_verdict_t v = can_selftest_verdict(&st);
    ESP_LOGI(TAG, "CAN self-test: %s", can_selftest_text(v));
    if (v != CAN_SELFTEST_OK && v != CAN_SELFTEST_RUNNING) {
        ESP_LOGW(TAG, "  check: %s", can_selftest_hint(v));
    }
    ESP_LOGI(TAG, "  sent %lu echoed %lu corrupt %lu lost %lu stale %lu"
                  "  (transmit queue full %lu times)",
             (unsigned long)st.sent, (unsigned long)st.echoed,
             (unsigned long)st.corrupt, (unsigned long)st.timed_out,
             (unsigned long)st.stale, (unsigned long)queue_full);
    if (st.echoed > 0) {
        ESP_LOGI(TAG, "  round trip min %lu max %lu us",
                 (unsigned long)st.rtt_min_us, (unsigned long)st.rtt_max_us);
    }
    ESP_LOGI(TAG, "  panel  tx_err %lu rx_err %lu bus_err %lu%s",
             (unsigned long)tx_err, (unsigned long)rx_err,
             (unsigned long)bus_err, bus_off ? "  BUS OFF" : "");
    if (have_remote) {
        ESP_LOGI(TAG, "  iomcu  %s, %u echoes, %u overflow(s), "
                      "tx_err %u rx_err %u flags 0x%02X",
                 remote.up ? "CAN up" : "CAN DOWN",
                 remote.echoes, remote.overflows, remote.tx_errors,
                 remote.rx_errors, remote.flags);
        /*
         * The comparison both ends make together.  Frames the coprocessor
         * answered but the panel never received are a return-path fault;
         * frames it never answered are an outbound fault; overflows are a
         * receive buffer that filled while its owner was busy, which no bus
         * counter records.
         */
        if (remote.overflows > 0u) {
            ESP_LOGW(TAG, "  the coprocessor dropped %u frame(s) for want of "
                          "a free buffer -- not a bus fault",
                     remote.overflows);
        }
        if (have_before) {
            /*
             * Everything that came back, not only what was accepted: a late
             * or altered echo still crossed the return path.
             */
            const uint32_t received = st.echoed + st.stale + st.corrupt;
            const uint16_t lost = can_selftest_return_loss(before.echoes,
                                                           remote.echoes,
                                                           received);
            if (lost > 0u) {
                ESP_LOGW(TAG, "  it answered %u more than we heard: the "
                              "return path is losing frames", lost);
            }
        } else {
            /* Without a reading from before the run there is only a lifetime
             * total, which is not comparable with this run's count. */
            ESP_LOGI(TAG, "  (no baseline from the far end, so its echo count "
                          "is a lifetime total and not comparable)");
        }
    } else {
        ESP_LOGW(TAG, "  iomcu  did not answer a status request");
    }
}

#endif /* RCBENCH_CAN_SELFTEST */
