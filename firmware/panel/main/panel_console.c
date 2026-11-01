/**
 * @file panel_console.c
 * @brief The RCBENCH_PANEL_CONSOLE build; see panel_console.h.
 *
 * The characters come from both serial ways the board has, UART0 and
 * USB-Serial-JTAG, and the output goes to both through stdout, as the log does.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_console.h"

#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"

#include "console_relay.h"
#include "panel.h"

/** @brief The console task's stack: printf and its buffers. */
#define CONSOLE_STACK 4096u
/** @brief Below the card and the control task. */
#define CONSOLE_PRIO 2u
/** @brief Its period, ms: the port's FIFO of 128 bytes lasts 11ms at 115200 baud. */
#define CONSOLE_PERIOD_MS 5u
/** @brief The UART driver's receive buffer, bytes. */
#define CONSOLE_UART_RX 512

/** @brief The relay; its two sides are in console_relay.h. */
static console_relay_t s_relay;
/** @brief Control task: the answer, off its stack. */
static link_msg_t s_reply;
/** @brief The console's task: a line going out. */
static char s_out_line[256];

/**
 * @brief The link's state and at most one exchange of the page.
 *
 * @param link_up The link is up.
 */
void panel_console_pass(bool link_up)
{
    console_relay_op_t op;

    console_relay_link(&s_relay, link_up, now_ms());
    if (!console_relay_next(&s_relay, now_ms(), &op)) {
        return;
    }
    const bool answered =
        op.write ? write_regs(&s_host, LINK_PAGE_CONSOLE, op.off, op.n, op.regs, &s_reply)
                 : read_regs(&s_host, LINK_PAGE_CONSOLE, op.off, op.n, &s_reply);
    int result = CONSOLE_RELAY_NO_ANSWER;

    if (answered) {
        if (s_reply.op == LINK_OP_NACK) {
            result = (int)s_reply.regs[0];
        } else if (op.write || (s_reply.op == LINK_OP_DATA && s_reply.count >= op.n)) {
            result = 0;
        }
    }
    console_relay_done(&s_relay, result,
                       result == 0 && !op.write ? s_reply.regs : NULL);
}

/**
 * @brief The console's task.
 *
 * @param arg Unused.
 */
static void console_task(void *arg)
{
    uint8_t in[64];

    (void)arg;
    for (;;) {
        const int got = uart_read_bytes(UART_NUM_0, in, sizeof(in), 0);

        for (int i = 0; i < got; ++i) {
            console_relay_key(&s_relay, in[i]);
        }
        if (usb_serial_jtag_ll_rxfifo_data_available() != 0) {
            const int usb = usb_serial_jtag_ll_read_rxfifo(in, sizeof(in));

            for (int i = 0; i < usb; ++i) {
                console_relay_key(&s_relay, in[i]);
            }
        }
        for (;;) {
            const size_t n = console_relay_take(&s_relay, s_out_line, sizeof(s_out_line));

            if (n == 0u) {
                break;
            }
            (void)fwrite(s_out_line, 1u, n, stdout);
        }
        (void)fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(CONSOLE_PERIOD_MS));
    }
}

/** @brief The console's task, and UART0's driver for what it reads. */
void panel_console_start(void)
{
    console_relay_init(&s_relay);
    /* Receive only: the log's writes keep their own path to the port. */
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, CONSOLE_UART_RX, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(console_task, "console", CONSOLE_STACK,
                                            NULL, CONSOLE_PRIO, NULL, 0) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI("console", "the bench console over the link: debug build");
}
