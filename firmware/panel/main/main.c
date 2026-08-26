/*
 * The panel application.
 *
 * At this commit it brings the link up and polls: enough to prove the wire,
 * the framing and both watchdogs against real silicon, and nothing more.  The
 * boot sequence, the splash and the router replace this loop when the screens
 * are re-cut.
 */
#include <inttypes.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "link_frame.h"
#include "link_host.h"
#include "link_pages.h"
#include "link_uart.h"
#include "link_wire.h"
#include "panel_pins.h"

static const char *TAG = "rcbench";

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/*
 * The heartbeat is configured and deliberately not driven.
 *
 * It must be toggled by the loop that reads touch and draws the STOP button --
 * that is the entire point of it, and the reason it is a heartbeat rather than
 * a level.  That loop does not exist yet.  Emitting edges from this one would
 * make the line prove that *a* loop is running, which is exactly the failure
 * the design exists to catch, and it would do it while looking like the
 * feature was finished.
 *
 * So the pin is held low: no edges, so the coprocessor's monostable never
 * arms, so its outputs stay disabled.  The bench cannot spin anything until
 * the loop that can stop it is the one asserting that it is alive.
 */
static void heartbeat_configure_but_do_not_drive(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PANEL_HEARTBEAT_PIN,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_set_level(PANEL_HEARTBEAT_PIN, 0));
    ESP_LOGW(TAG, "heartbeat on GPIO%d (J8) is held low until the UI loop "
                  "owns it -- the coprocessor's outputs stay disabled",
             (int)PANEL_HEARTBEAT_PIN);
}

/** Send a request and collect its reply, or time out. */
static bool transact(link_host_t *host, link_decoder_t *rx,
                     const uint8_t *frame, size_t len, link_msg_t *reply)
{
    if (link_uart_write(frame, (size_t)len) < 0) {
        return false;
    }

    const uint32_t started = now_ms();
    for (;;) {
        uint8_t buf[LINK_MAX_FRAME];
        const int n = link_uart_read(buf, sizeof(buf), 20);
        for (int i = 0; i < n; ++i) {
            if (link_decode_byte(rx, buf[i], reply)
                && link_host_reply(host, reply, now_ms())) {
                return true;
            }
        }
        if (link_host_tick(host, now_ms())) {
            /* The bytes of an abandoned reply are still on their way; letting
             * them reach the decoder is how a stale frame gets offered as the
             * answer to the next question. */
            link_uart_flush();
            link_decoder_reset(rx);
            ESP_LOGW(TAG, "no answer in %u ms", (unsigned)(now_ms() - started));
            return false;
        }
        vTaskDelay(1);
    }
}

void app_main(void)
{
    heartbeat_configure_but_do_not_drive();

    ESP_ERROR_CHECK(link_uart_init(PANEL_LINK_UART_NUM,
                                   LINK_BAUD_BRINGUP,
                                   PANEL_LINK_PIN_TX,
                                   PANEL_LINK_PIN_RX));

    link_host_t   host;
    link_decoder_t rx;
    link_host_init(&host, now_ms());
    link_decoder_reset(&rx);

    ESP_LOGI(TAG, "polling the coprocessor: %u polls/s of headroom at %u baud",
             (unsigned)LINK_POLLS_PER_SEC(LINK_BAUD_BRINGUP),
             (unsigned)LINK_BAUD_BRINGUP);

    for (;;) {
        uint8_t frame[LINK_MAX_FRAME];
        const size_t n = link_host_read(&host, LINK_PAGE_IDENTITY, 0,
                                        LINK_ID_COUNT, frame, sizeof(frame));
        link_msg_t reply;
        if (n > 0 && transact(&host, &rx, frame, n, &reply)) {
            if (reply.op == LINK_OP_NACK) {
                ESP_LOGW(TAG, "identity refused: reason %u", reply.regs[0]);
            } else if (reply.regs[LINK_ID_PROTOCOL_MAJOR]
                       != LINK_PROTOCOL_MAJOR) {
                /* Refusing to arm when the versions disagree is one register
                 * and some spine.  This is the register. */
                ESP_LOGE(TAG, "protocol major %u, this panel speaks %u -- "
                              "arming is refused",
                         reply.regs[LINK_ID_PROTOCOL_MAJOR],
                         (unsigned)LINK_PROTOCOL_MAJOR);
            } else {
                ESP_LOGI(TAG, "copro proto %u.%u hw %u | polls %" PRIu32
                              " replies %" PRIu32 " nacks %" PRIu32
                              " | crc err %" PRIu32 " resyncs %" PRIu32,
                         reply.regs[LINK_ID_PROTOCOL_MAJOR],
                         reply.regs[LINK_ID_PROTOCOL_MINOR],
                         reply.regs[LINK_ID_HARDWARE],
                         host.polls, host.replies, host.nacks,
                         rx.crc_errors, rx.resyncs);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));   /* 20 Hz, one poll per panel frame */
    }
}
