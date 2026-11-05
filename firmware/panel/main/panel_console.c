/**
 * @file panel_console.c
 * @brief The RCBENCH_PANEL_CONSOLE build; see panel_console.h.
 *
 * The characters come from both serial ways the board has, UART0 and
 * USB-Serial-JTAG, and the output goes to both through stdout, as the log does.
 *
 * A typed line goes to the relay at its end.  A line that starts with the
 * word `ble` stays on the panel: `ble scan` starts Bluetooth LE when it has
 * not started (panel_ble.h), waits for the host's sync and prints every
 * device a 10s scan hears.
 *
 * SPDX-License-Identifier: MIT
 */
#include "panel_console.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"

#include "console_relay.h"
#include "panel.h"
#include "panel_ble.h"

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

/** @brief The length of `ble scan`'s scan, ms. */
#define CONSOLE_BLE_SCAN_MS 10000u

/** @brief The console's task: the line typed so far, held back from the
 *         relay until its end. */
static char s_typing[CONSOLE_RELAY_LINE_MAX + 1u];
/** @brief Characters in s_typing. */
static size_t s_typed;
/** @brief The line ran past CONSOLE_RELAY_LINE_MAX: the relay has it and
 *         takes the rest as typed. */
static bool s_long;

/** @brief Where `ble scan` is. */
typedef enum {
    BLE_CMD_IDLE = 0, /**< no command */
    BLE_CMD_WAIT,     /**< the scan waits for the host's sync */
    BLE_CMD_SCAN,     /**< the scan runs */
} ble_cmd_t;

/** @brief The console's task: `ble scan`'s state. */
static ble_cmd_t s_ble;
/** @brief The console's task: reports printed in this scan. */
static atomic_uint s_ble_reports;
/** @brief The scan's end came, on the host task. */
static atomic_bool s_ble_done;
/** @brief Its reason. */
static atomic_int s_ble_reason;

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
 * @brief Print one advertising report: the address most significant byte
 *        first, its type, the RSSI, the report's event type and its data in
 *        hexadecimal.
 * @param e The report.
 */
static void ble_print_adv(const bleu_evt_t *e)
{
    char hex[2u * BLEU_EVT_DATA + 1u];
    size_t n = 0u;

    for (size_t i = 0u; i < e->len && i < BLEU_EVT_DATA; ++i) {
        n += (size_t)snprintf(&hex[n], sizeof(hex) - n, "%02X", e->data[i]);
    }
    hex[n] = '\0';
    printf("ble: adv %02X:%02X:%02X:%02X:%02X:%02X type %u rssi %ddBm "
           "evtype %d data %s\n", e->addr.val[5], e->addr.val[4],
           e->addr.val[3], e->addr.val[2], e->addr.val[1], e->addr.val[0],
           (unsigned)e->addr.type, (int)e->rssi, (int)e->status, hex);
}

/**
 * @brief The console scan's reports, on the host task: each one printed,
 *        the end noted for ble_pass().
 * @param e A report or the end.
 */
static void ble_report(const bleu_evt_t *e)
{
    if (e->kind == BLEU_EV_ADV) {
        atomic_fetch_add(&s_ble_reports, 1u);
        ble_print_adv(e);
    } else if (e->kind == BLEU_EV_SCAN_END) {
        atomic_store(&s_ble_reason, (int)e->status);
        atomic_store(&s_ble_done, true);
    }
}

/**
 * @brief A typed line that starts with `ble`, kept from the relay.
 * @param line The line, without its end.
 */
static void ble_command(const char *line)
{
    if (strcmp(line, "ble controller") == 0) {
        printf("ble: the C6 controller init and enable requests, up to "
               "10s\n");
        const esp_err_t r = panel_ble_controller();
        printf("ble: controller requests: %s\n",
               r == ESP_OK ? "sent, see the ble lines"
                           : "Bluetooth LE is not started or still starting");
        return;
    }
    if (strcmp(line, "ble scan") != 0) {
        printf("ble: commands: ble scan, ble controller\n");
        return;
    }
    if (s_ble != BLE_CMD_IDLE) {
        printf("ble: a scan is under way\n");
        return;
    }
    if (panel_ble_state() == PANEL_BLE_OFF) {
        printf("ble: starting Bluetooth LE; the scan follows the sync\n");
        panel_ble_start();
    }
    s_ble = BLE_CMD_WAIT;
    atomic_store(&s_ble_reports, 0u);
    atomic_store(&s_ble_done, false);
}

/**
 * @brief The console's pass of `ble scan`: start the scan once synced, print
 *        the events while it runs.
 */
static void ble_pass(void)
{
    if (s_ble == BLE_CMD_WAIT) {
        const panel_ble_state_t st = panel_ble_state();

        if (st == PANEL_BLE_READY) {
            const esp_err_t r = panel_ble_scan(CONSOLE_BLE_SCAN_MS, ble_report);

            if (r == ESP_OK) {
                printf("ble: scan for %ums\n", (unsigned)CONSOLE_BLE_SCAN_MS);
                s_ble = BLE_CMD_SCAN;
            } else {
                printf("ble: no scan: %s\n", esp_err_to_name(r));
                s_ble = BLE_CMD_IDLE;
            }
        } else if (st == PANEL_BLE_NO_LINK || st == PANEL_BLE_NO_SYNC) {
            printf("ble: no scan: %s\n", st == PANEL_BLE_NO_LINK
                                             ? "no link to the C6" : "no sync");
            s_ble = BLE_CMD_IDLE;
        }
    }
    if (s_ble == BLE_CMD_SCAN && atomic_load(&s_ble_done)) {
        printf("ble: scan done: %u reports, reason %d, %u events dropped "
               "since start-up\n", (unsigned)atomic_load(&s_ble_reports),
               atomic_load(&s_ble_reason), (unsigned)panel_ble_dropped());
        s_ble = BLE_CMD_IDLE;
    }
}

/**
 * @brief The held line's characters to the relay.
 */
static void flush_typed(void)
{
    for (size_t i = 0u; i < s_typed; ++i) {
        console_relay_key(&s_relay, s_typing[i]);
    }
    s_typed = 0u;
}

/**
 * @brief One character from a serial port: held until the line ends, then
 *        to the relay, or to ble_command() for a line that starts with `ble`.
 *        The relay's own rules for backspace, control characters and an
 *        overlong line hold for what it is handed.
 * @param ch The character.
 */
static void console_key(int ch)
{
    if (ch == '\r' || ch == '\n') {
        s_typing[s_typed] = '\0';
        if (!s_long && (strcmp(s_typing, "ble") == 0
                        || strncmp(s_typing, "ble ", 4u) == 0)) {
            ble_command(s_typing);
            s_typed = 0u;
        } else {
            flush_typed();
        }
        s_long = false;
        console_relay_key(&s_relay, ch);
        return;
    }
    if (s_long) {
        console_relay_key(&s_relay, ch);
        return;
    }
    if (ch == 0x08 || ch == 0x7F) {
        if (s_typed != 0u) {
            --s_typed;
        }
        return;
    }
    if (ch < 0x20 || ch > 0x7E) {
        return;
    }
    if (s_typed >= CONSOLE_RELAY_LINE_MAX) {
        flush_typed();
        s_long = true;
        console_relay_key(&s_relay, ch);
        return;
    }
    s_typing[s_typed++] = (char)ch;
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
            console_key(in[i]);
        }
        if (usb_serial_jtag_ll_rxfifo_data_available() != 0) {
            const int usb = usb_serial_jtag_ll_read_rxfifo(in, sizeof(in));

            for (int i = 0; i < usb; ++i) {
                console_key(in[i]);
            }
        }
        ble_pass();
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
