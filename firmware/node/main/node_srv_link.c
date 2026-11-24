/**
 * @file node_srv_link.c
 * @brief The servo node's `srv` task: the UART link host to the Core2350B
 *        (the IOmcu of board 3), its poll, and the pump that runs the
 *        policy and the heartbeat between and during exchanges.
 *
 * The UART is UART0 on IO43 (TX) and IO44 (RX), LINK_UART_BAUD (1Mbaud)
 * 8N1.  Each link frame travels COBS-framed with a CRC-16 (link_uart.h);
 * the requests and their repeat are link_host.c's, as on the panel's CAN
 * (Controller Area Network) link: one request in flight, its repeat after
 * LINK_HOST_RETRY_MS (50ms), given up after LINK_HOST_TIMEOUT_MS (1000ms).
 * The ESP32-S3 ROM's boot messages at 115,200 baud on the same TX pin reach
 * the Core2350B as bad frames, which its framing drops and counts.
 *
 * Order of work in each pass of the task: the pump; the all-off writes
 * while owed; IDENTITY after a link-up; CLEAR and the arming frame when
 * due; then the poll's next row.  An exchange in flight when the all-off
 * writes become owed is given up at the next pump, so they follow a STOP
 * within one pump (1ms) and one exchange's wire time.
 *
 * SPDX-License-Identifier: MIT
 */
#include "node.h"
#include "node_srv.h"
#include "node_srv_poll.h"

#include <stdatomic.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "heartbeat.h"
#include "link_can.h"
#include "link_host.h"
#include "link_port.h"
#include "link_uart.h"
#include "link_uart_dev.h"
#include "mstime.h"

/** @brief The UART to the Core2350B. */
#define SRV_UART UART_NUM_0
/** @brief The UART driver's receive buffer, bytes: 10ms at 1Mbaud. */
#define SRV_RX_BYTES 1024
/** @brief The panel's operations waiting for the pump. */
#define SRV_EVQ_LEN 16u
/** @brief The console's keep-alive period, ms: the panel's. */
#define SRV_CONSOLE_KA_MS 50u
/** @brief The `srv` task's stack, bytes. */
#define SRV_STACK 4096u
/** @brief The `srv` task's priority: above `ble_tx` (6) and the console. */
#define SRV_PRIO  10u

/** @brief An operation from the panel. */
typedef struct {
    uint8_t  kind;  /**< EV_* */
    uint16_t value; /**< the KEEPALIVE's STOP flag, the ARM's roles or the THROTTLE */
    uint32_t ms;    /**< when it was posted */
} srv_ev_t;

/** @brief srv_ev_t's kinds. */
enum { EV_KA = 1, EV_ARM, EV_SAFE, EV_THROTTLE };

static QueueHandle_t   s_evq;                       /**< srv_ev_t */
static atomic_bool     s_stop_req;                  /**< a STOP waits for the pump */
static atomic_bool     s_console_ka;                /**< the console's keep-alive source */
static portMUX_TYPE    s_view_mux = portMUX_INITIALIZER_UNLOCKED; /**< guards s_view */
static node_srv_view_t s_view;                      /**< the published view */

/* The `srv` task's alone. */
static node_srv_pol_t  s_pol;        /**< the policy */
static node_srv_out_t  s_out;        /**< its last step */
static node_srv_poll_t s_poll;       /**< the schedule */
static link_host_t     s_host;       /**< the link host */
static link_uart_rx_t  s_rx;         /**< the UART frame decoder */
static heartbeat_gen_t s_beat;       /**< the heartbeat's generator */
static bool            s_level;      /**< IO21 as driven */
static node_srv_x_t   s_xkind;      /**< what the exchange in flight writes */
static uint16_t        s_xarm;       /**< the ARM value a CONTROL exchange carries */
static uint16_t        s_xthr;       /**< the THROTTLE a CONTROL exchange carries */
static uint32_t        s_ka_ms;      /**< the console source's last KEEPALIVE */
static uint32_t        s_refused;    /**< frames the framing or the decode dropped */
static uint32_t        s_ident_ms;   /**< when IDENTITY was refused */
static bool            s_ident_wait; /**< IDENTITY waits SRV_IDENT_RETRY_MS from s_ident_ms */
/** @brief The pause before IDENTITY is asked again after a refusal, ms. */
#define SRV_IDENT_RETRY_MS 500u

/** @brief The ms clock.  @return ms since the start, wrapping. */
static uint32_t now_ms(void)
{
    return (uint32_t)(node_us() / 1000u);
}

/**
 * @brief Queue an operation for the pump.
 * @param kind  EV_*.
 * @param value Its value.
 */
static void post(uint8_t kind, uint16_t value)
{
    if (s_evq == NULL) {
        return;
    }
    const srv_ev_t ev = { kind, value, now_ms() };
    if (xQueueSend(s_evq, &ev, 0) != pdTRUE) {
        ESP_LOGW(NODE_TAG, "srv: operation %u dropped, queue full", (unsigned)kind);
    }
}

void node_srv_post_keepalive(bool stop)
{
    if (stop) {
        atomic_store(&s_stop_req, true);
    }
    post(EV_KA, stop ? 1u : 0u);
}

void node_srv_post_stop(void)
{
    atomic_store(&s_stop_req, true);
}

void node_srv_post_arm(uint16_t roles)
{
    post(EV_ARM, roles);
}

void node_srv_post_safe(void)
{
    post(EV_SAFE, 0u);
}

void node_srv_post_throttle(uint16_t value)
{
    post(EV_THROTTLE, value);
}

void node_srv_link_console_ka(bool on)
{
    atomic_store(&s_console_ka, on);
}

bool node_srv_link_console_ka_on(void)
{
    return atomic_load(&s_console_ka);
}

void node_srv_link_view(node_srv_view_t *out)
{
    taskENTER_CRITICAL(&s_view_mux);
    *out = s_view;
    taskEXIT_CRITICAL(&s_view_mux);
}

/**
 * @brief Log the events the policy collected.
 * @param ev NODE_SRV_EV_* bits.
 */
static void log_events(uint8_t ev)
{
    static const struct {
        uint8_t     bit;
        const char *text;
    } k_ev[] = {
        { NODE_SRV_EV_STOP, "STOP taken: all outputs off" },
        { NODE_SRV_EV_KA_LOST, "PANEL LOST: no KEEPALIVE for 300ms, all outputs off" },
        { NODE_SRV_EV_BLE_LOST, "PANEL LOST: Bluetooth LE disconnected, all outputs off" },
        { NODE_SRV_EV_IOMCU_LOST, "IOMCU LOST: no answer for 1000ms" },
        { NODE_SRV_EV_ARM_FAILED, "ARM failed: SAFE" },
        { NODE_SRV_EV_ARMED, "ARMED" },
    };
    for (size_t k = 0; k < sizeof k_ev / sizeof k_ev[0]; ++k) {
        if ((ev & k_ev[k].bit) != 0u) {
            ESP_LOGW(NODE_TAG, "srv: %s", k_ev[k].text);
        }
    }
}

/**
 * @brief The pump: the panel's operations into the policy, its step, the
 *        heartbeat and the RUN pulse.  Runs at least every 2ms while the
 *        task runs, inside exchanges as well.
 */
static void pump(void)
{
    uint32_t now = now_ms();
    if (atomic_exchange(&s_stop_req, false)) {
        node_srv_pol_stop(&s_pol, now);
    }
    srv_ev_t ev;
    while (xQueueReceive(s_evq, &ev, 0) == pdTRUE) {
        switch (ev.kind) {
        case EV_KA:
            node_srv_pol_keepalive(&s_pol, ev.ms, ev.value != 0u);
            break;
        case EV_ARM: {
            const node_srv_arm_why_t why = node_srv_pol_arm(&s_pol, now, ev.value);
            ESP_LOGI(NODE_TAG, "srv: ARM 0x%04X %s (%d)", (unsigned)ev.value,
                     why == NODE_SRV_ARM_OK ? "taken" : "refused", (int)why);
            break;
        }
        case EV_SAFE:
            node_srv_pol_safe(&s_pol);
            break;
        case EV_THROTTLE:
            node_srv_pol_throttle(&s_pol, ev.ms, ev.value);
            break;
        default:
            break;
        }
    }
    now = now_ms();
    /* The console's source stands in for a connected panel: on, it is a
     * link; off, a disconnect. */
    const bool console_ka = atomic_load(&s_console_ka);
    if (console_ka && mstime_elapsed(now, s_ka_ms, SRV_CONSOLE_KA_MS)) {
        s_ka_ms = now;
        node_srv_pol_keepalive(&s_pol, now, false);
    }
    node_srv_pol_ble(&s_pol, now, node_ble_linked() || console_ka);
    const bool was_lost = s_pol.iomcu_lost;
    node_srv_pol_step(&s_pol, now, &s_out);
    if (s_pol.iomcu_lost && !was_lost) {
        /* A returning IOmcu is asked for its IDENTITY first. */
        s_host.numbered = false;
        s_host.held_reads = false;
    }
    uint32_t pulses = 0u;
    if (s_out.run_pulse) {
        ESP_LOGW(NODE_TAG, "srv: the Core2350B silent for 3000ms: RUN pulsed");
        node_srv_hb_run_pulse();
        pulses = 1u;
    }
    const bool level = heartbeat_gen_step(&s_beat, now, s_out.beat);
    uint32_t beats = 0u;
    if (level != s_level) {
        s_level = level;
        node_srv_hb_set(level);
        beats = 1u;
    }
    log_events(node_srv_pol_events(&s_pol));
    taskENTER_CRITICAL(&s_view_mux);
    s_view.pol = s_pol;
    s_view.beat = s_out.beat;
    s_view.linked = s_pol.identity_ok;
    s_view.run_pulses += pulses;
    s_view.beats += beats;
    s_view.refused = s_refused;
    taskEXIT_CRITICAL(&s_view_mux);
}

/**
 * @brief Count one outcome in the view.
 * @param field Which counter.
 */
static void count(uint32_t *field)
{
    taskENTER_CRITICAL(&s_view_mux);
    ++*field;
    taskEXIT_CRITICAL(&s_view_mux);
}

/**
 * @brief Send a request's frames.
 * @param req The request.
 * @return false when it renders as no frame.
 */
static bool send_msg(const link_msg_t *req)
{
    link_can_frame_t f[LINK_CAN_MAX_FRAMES];
    const size_t n = link_can_encode(req, f, LINK_CAN_MAX_FRAMES);
    for (size_t i = 0; i < n; ++i) {
        uint8_t wire[LINK_UART_WIRE_MAX];
        const size_t w = link_uart_encode(&f[i], wire, sizeof wire);
        if (w > 0u) {
            (void)uart_write_bytes(SRV_UART, wire, w);
        }
    }
    return n > 0u;
}

/**
 * @brief One request and its answer: link_port_t's exchange.
 *
 * Pumps first and waits in reads of up to 1ms, pumping after each.  Ends
 * answered (a NACK included), unanswered at the request's timeout, or
 * given up, before it is sent or while it waits, as soon as
 * node_srv_pol_keep() refuses s_xkind: a SAFE, STOP or loss taken by the
 * pump ends a write from before it.
 *
 * @param ctx   Not used.
 * @param h     The link host.
 * @param req   The request.
 * @param reply Receives the answer.
 * @return true when answered.
 */
static bool xchg(void *ctx, link_host_t *h, const link_msg_t *req,
                 link_msg_t *reply)
{
    (void)ctx;
    pump();
    if (!node_srv_pol_keep(&s_pol, &s_out, s_xkind, s_xarm, s_xthr)) {
        link_host_abandon(h);
        count(&s_view.preempted);
        return false;
    }
    if (!send_msg(req)) {
        link_host_abandon(h);
        count(&s_view.unanswered);
        return false;
    }
    for (;;) {
        uint8_t buf[64];
        bool done = false;
        int got = uart_read_bytes(SRV_UART, buf, 1u, 1);
        if (got == 1) {
            size_t more = 0u;
            (void)uart_get_buffered_data_len(SRV_UART, &more);
            if (more > sizeof buf - 1u) {
                more = sizeof buf - 1u;
            }
            if (more > 0u) {
                const int extra = uart_read_bytes(SRV_UART, buf + 1, (uint32_t)more, 0);
                got += extra > 0 ? extra : 0;
            }
        }
        for (int i = 0; i < got; ++i) {
            link_can_frame_t fr;
            link_msg_t part;
            if (link_uart_feed(&s_rx, buf[i], &fr, &s_refused) && !done
                && link_can_decode_counted(&fr, &part, &s_refused)
                && link_host_accept(h, &part, now_ms(), reply)) {
                /* The rest of this read is fed on, so the decoder stays in
                 * step; a part of a late answer is dropped. */
                done = true;
            }
        }
        if (done) {
            node_srv_pol_answer(&s_pol, now_ms());
            count(&s_view.exchanges);
            if (reply->op == LINK_OP_NACK) {
                count(&s_view.nacks);
            }
            return true;
        }
        pump();
        if (!node_srv_pol_keep(&s_pol, &s_out, s_xkind, s_xarm, s_xthr)) {
            link_host_abandon(h);
            count(&s_view.preempted);
            return false;
        }
        link_msg_t again;
        if (link_host_retry(h, now_ms(), &again)) {
            (void)send_msg(&again);
        }
        (void)link_host_tick(h, now_ms());
        if (!link_host_pending(h)) {
            count(&s_view.unanswered);
            return false;
        }
    }
}

/** @brief link_port_t's clock.  @param ctx Not used.  @return ms. */
static uint32_t port_now(void *ctx)
{
    (void)ctx;
    return now_ms();
}

/** @brief link_port_t's between: the pump.  @param ctx Not used. */
static void port_between(void *ctx)
{
    (void)ctx;
    pump();
}

/** @brief The link's transport for link_port.c. */
static const link_port_t k_port = { xchg, port_now, NULL, port_between };

/**
 * @brief Write registers; true when answered with an ACK.
 * @param page   The page.
 * @param offset The first register.
 * @param n      The count.
 * @param regs   The values.
 * @param answered Receives whether every frame was answered; may be NULL.
 * @return true on an ACK.
 */
static bool write_regs(uint8_t page, uint8_t offset, uint8_t n,
                       const uint16_t *regs, bool *answered)
{
    link_msg_t reply;
    const bool a = link_write_acked(&s_host, &k_port, page, offset, n, regs, &reply);
    if (answered != NULL) {
        *answered = a;
    }
    return a && reply.op == LINK_OP_ACK;
}

/**
 * @brief The all-off writes: ARM 0 and THROTTLE 0, RAIL 0, PORT_WANT 0,
 *        the supply's OUTPUT 0.  Done when every one was answered.
 */
static void do_off(void)
{
    static const uint16_t k_zero[2] = { 0u, 0u };
    static const struct {
        uint8_t page;
        uint8_t offset;
        uint8_t count;
    } k_writes[] = {
        { LINK_PAGE_CONTROL, LINK_CT_ARM, 2u },
        { LINK_PAGE_POWER, LINK_PW_RAIL, 1u },
        { LINK_PAGE_POWER, LINK_PW_PORT_WANT, 1u },
        { LINK_PAGE_SUPPLY, LINK_SP_OUTPUT, 1u },
    };
    const uint8_t gen = s_out.off_gen;
    s_xkind = NODE_SRV_X_OFF;
    bool all = true;
    for (size_t k = 0; k < sizeof k_writes / sizeof k_writes[0] && all; ++k) {
        /* A refusal is an answer: RAIL 0 and OUTPUT 0 are always taken, and
         * an IOmcu without the page has nothing to switch off there.  An
         * unanswered write ends the round; the next pass starts it again. */
        const bool ack = write_regs(k_writes[k].page, k_writes[k].offset,
                                    k_writes[k].count, k_zero, &all);
        if (k == 0u && ack) {
            node_srv_pol_ctl_done(&s_pol, 0u, 0u);
        }
    }
    s_xkind = NODE_SRV_X_READ;
    if (all) {
        node_srv_pol_off_done(&s_pol, gen);
    }
}

/** @brief IDENTITY after a link-up: the board and the protocol. */
static void do_identity(void)
{
    link_msg_t reply;
    if (!link_read_window(&s_host, &k_port, LINK_PAGE_IDENTITY, 0u,
                          (uint8_t)LINK_ID_COUNT, &reply)
        || reply.op != LINK_OP_DATA) {
        return;
    }
    const uint16_t minor = reply.regs[LINK_ID_PROTOCOL_MINOR];
    const bool ok = node_srv_pol_identity(&s_pol, reply.regs[LINK_ID_PROTOCOL_MAJOR],
                                          minor, reply.regs[LINK_ID_HARDWARE]);
    s_host.held_reads = minor >= LINK_MINOR_READ_HELD;
    s_host.numbered = minor >= LINK_MINOR_REPEAT;
    ESP_LOGI(NODE_TAG, "srv: IOmcu protocol %u.%u, firmware %u.%u.%u, board %u: %s",
             (unsigned)reply.regs[LINK_ID_PROTOCOL_MAJOR], (unsigned)minor,
             (unsigned)reply.regs[LINK_ID_FIRMWARE_MAJOR],
             (unsigned)reply.regs[LINK_ID_FIRMWARE_MINOR],
             (unsigned)reply.regs[LINK_ID_FIRMWARE_PATCH],
             (unsigned)reply.regs[LINK_ID_HARDWARE],
             ok ? "taken" : "refused: board 3 at protocol 4.41 or later only");
    taskENTER_CRITICAL(&s_view_mux);
    memcpy(s_view.identity, reply.regs, (size_t)LINK_ID_COUNT * sizeof(uint16_t));
    taskEXIT_CRITICAL(&s_view_mux);
    if (!ok) {
        /* Asked again after a pause, not in a tight loop; the task pumps
         * on through it. */
        s_ident_ms = now_ms();
        s_ident_wait = true;
    }
}

/**
 * @brief CLEAR (0x5AFE), then ARM and THROTTLE 0 in one frame.  Each is an
 *        NODE_SRV_X_ARMING exchange: a SAFE, STOP or loss taken before or
 *        during either ends the sequence before ARM is sent, or ends the
 *        wait for its answer, and CONTROL's ARM 0 or the all-off writes
 *        follow (node_srv_pol_keep()).
 */
static void do_arm(void)
{
    const uint16_t clear = LINK_CLEAR_MAGIC;
    const uint16_t arm[2] = { s_pol.roles, 0u };
    s_xkind = NODE_SRV_X_ARMING;
    bool taken = write_regs(LINK_PAGE_CONTROL, LINK_CT_CLEAR, 1u, &clear, NULL);
    if (taken) {
        node_srv_pol_ctl_sent(&s_pol, arm[0], arm[1]);
        taken = write_regs(LINK_PAGE_CONTROL, LINK_CT_ARM, 2u, arm, NULL);
        if (taken) {
            node_srv_pol_ctl_done(&s_pol, arm[0], arm[1]);
        }
    }
    s_xkind = NODE_SRV_X_READ;
    node_srv_pol_arm_done(&s_pol, now_ms(), taken);
}

/** @brief CONTROL's ARM and THROTTLE, as the policy has them. */
static void do_control(void)
{
    const uint16_t regs[2] = { s_out.arm, s_out.throttle };
    bool answered = false;
    s_xkind = NODE_SRV_X_CONTROL;
    s_xarm = regs[0];
    s_xthr = regs[1];
    node_srv_pol_ctl_sent(&s_pol, regs[0], regs[1]);
    const bool ack = write_regs(LINK_PAGE_CONTROL, LINK_CT_ARM, 2u, regs, &answered);
    s_xkind = NODE_SRV_X_READ;
    s_xarm = 0u;
    s_xthr = 0u;
    if (ack) {
        node_srv_pol_ctl_done(&s_pol, regs[0], regs[1]);
    } else if (answered && regs[0] != 0u) {
        node_srv_pol_refused(&s_pol);
    }
}

/**
 * @brief Read one row of the poll into the view.
 * @param row The row, not NODE_SRV_ROW_CONTROL.
 */
static void do_read(node_srv_row_t row)
{
    const node_srv_row_def_t *d = &node_srv_rows[row];
    link_msg_t reply;
    if (!link_read_split(&s_host, &k_port, d->page, 0u, d->count, &reply)
        || reply.op != LINK_OP_DATA) {
        return;
    }
    uint16_t *dst = NULL;
    size_t cap = 0u;
    switch (row) {
    case NODE_SRV_ROW_STATUS:
        node_srv_pol_status(&s_pol, now_ms(), reply.regs[LINK_ST_FAULTS]);
        dst = s_view.status;
        cap = LINK_ST_COUNT;
        break;
    case NODE_SRV_ROW_POWER:
        dst = s_view.power;
        cap = LINK_PW_COUNT;
        break;
    case NODE_SRV_ROW_SUPPLY:
        dst = s_view.supply;
        cap = LINK_SP_COUNT;
        break;
    case NODE_SRV_ROW_WIN_A:
        dst = s_view.win_a;
        cap = LINK_MAX_REGS;
        break;
    case NODE_SRV_ROW_WIN_B:
        dst = s_view.win_b;
        cap = LINK_MAX_REGS;
        break;
    default:
        return;
    }
    const size_t n = reply.count < cap ? reply.count : cap;
    taskENTER_CRITICAL(&s_view_mux);
    memcpy(dst, reply.regs, n * sizeof(uint16_t));
    taskEXIT_CRITICAL(&s_view_mux);
}

/**
 * @brief The `srv` task.  Does not return.
 * @param arg Not used.
 */
static void srv_task(void *arg)
{
    (void)arg;
    for (;;) {
        pump();
        if (s_out.off) {
            do_off();
            continue;
        }
        if (s_out.ctl_now) {
            /* CONTROL owed now goes before every read, IDENTITY's included:
             * node_srv_pol_keep() gives reads up while it is owed. */
            node_srv_poll_taken(&s_poll, NODE_SRV_ROW_CONTROL, now_ms());
            do_control();
            continue;
        }
        if (!s_pol.identity_ok) {
            if (!s_ident_wait
                || mstime_elapsed(now_ms(), s_ident_ms, SRV_IDENT_RETRY_MS)) {
                s_ident_wait = false;
                do_identity();
            } else {
                vTaskDelay(1);
            }
            continue;
        }
        if (s_out.arm_write) {
            do_arm();
            continue;
        }
        const uint32_t now = now_ms();
        const node_srv_row_t row = node_srv_poll_next(&s_poll, now, s_out.status_fast);
        if (row == NODE_SRV_ROW_NONE) {
            vTaskDelay(1);
            continue;
        }
        node_srv_poll_taken(&s_poll, row, now);
        if (row == NODE_SRV_ROW_CONTROL) {
            do_control();
        } else {
            do_read(row);
        }
    }
}

void node_srv_link_start(void)
{
    const uint32_t now = now_ms();
    node_srv_pol_init(&s_pol, now);
    node_srv_poll_init(&s_poll);
    link_host_init(&s_host, now);
    node_srv_rx_start(&s_rx);
    heartbeat_gen_init(&s_beat);
    s_evq = xQueueCreate(SRV_EVQ_LEN, sizeof(srv_ev_t));
    const uart_config_t cfg = {
        .baud_rate = (int)LINK_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t e = uart_driver_install(SRV_UART, SRV_RX_BYTES, 0, 0, NULL, 0);
    if (e == ESP_OK) {
        e = uart_param_config(SRV_UART, &cfg);
    }
    if (e == ESP_OK) {
        e = uart_set_pin(SRV_UART, NODE_PIN_SRV_TX, NODE_PIN_SRV_RX,
                         UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (e == ESP_OK) {
        /* What the line carried before the driver ran is no answer. */
        e = uart_flush_input(SRV_UART);
    }
    if (e != ESP_OK || s_evq == NULL) {
        /* No link: no heartbeat either, so the Core2350B stays SAFE. */
        ESP_LOGE(NODE_TAG, "srv: UART0 not started (%s): no link, no heartbeat",
                 esp_err_to_name(e));
        return;
    }
    if (xTaskCreate(srv_task, "srv", SRV_STACK, NULL, SRV_PRIO, NULL) != pdPASS) {
        ESP_LOGE(NODE_TAG, "srv: the task did not start: no link, no heartbeat");
        return;
    }
    ESP_LOGI(NODE_TAG, "srv: UART0 %lu baud on IO%d/IO%d, heartbeat on IO%d",
             (unsigned long)LINK_UART_BAUD, NODE_PIN_SRV_TX, NODE_PIN_SRV_RX,
             NODE_PIN_SRV_HB);
}
