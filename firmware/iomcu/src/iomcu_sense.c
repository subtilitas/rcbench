/*
 * The SUPPLY page and the PD mini behind it, and the current monitors'
 * pages SENSE, SERVO_SENSE and SERVO_WIN, which core 1 reads the parts
 * for; see iomcu.h for the other files.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "iomcu.h"

#include "pd_uart.h"
#include "sense_map.h"

/* ------------------------------------------------------------- core 1 */

/*
 * The identity page's capability word: what the output drivers make true,
 * and the current monitors the SENSE set-up enables (sense_page_caps()).
 * Fitted as configured, not online now: a panel reads identity at link-up
 * and after it writes SENSE, so the word moves only with a SENSE write
 * taken.  Whether a part answers is SENSE FLAGS and BENCH bit 5.
 */
void capabilities_update(void)
{
    s_state.identity[LINK_ID_CAPABILITIES] =
        (uint16_t)(IOMCU_CAPABILITIES | sense_page_caps(&s_sense));
}

/*
 * The supply's pins: reserved from the outputs while it holds them, and the
 * PIO UART claimed for them.  False when no PIO block can reach them or has
 * room.
 */
bool supply_rewire(void)
{
    pd_uart_close();
    s_pd_open = false;
    reserve_held();
    if (!supply_page_enabled(&s_supply)) {
        return true;
    }
    if (!pd_uart_open(supply_page_tx(&s_supply), supply_page_rx(&s_supply),
                      supply_page_uart_baud(&s_supply))) {
        return false;
    }
    const pdmini_io_t io = { pd_uart_attach, pd_uart_detach, pd_uart_send,
                             NULL };
    pdmini_init(&s_pd, &io, s_now_ms);
    /* Whatever the module was doing before these pins reached it -- left
     * on before a restart, or on from its own button -- is not known: it
     * is held as maybe on until read off. */
    pdmini_restored(&s_pd);
    s_pd_rate = supply_page_rate(&s_supply, NULL);
    s_pd_open = true;
    return true;
}

/*
 * The UART released and the pins this page names reserved, without
 * attaching: true when a PIO block can serve them, tried and let go again.
 */
static bool supply_probe(void)
{
    s_supply_attach = false;
    pd_uart_close();
    s_pd_open = false;
    reserve_held();
    if (!supply_page_enabled(&s_supply)) {
        return true;
    }
    if (!pd_uart_open(supply_page_tx(&s_supply), supply_page_rx(&s_supply),
                      supply_page_uart_baud(&s_supply))) {
        return false;
    }
    pd_uart_close();
    return true;
}

void supply_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    supply_page_read(&s_supply, off, n, out);
}

/*
 * A write to the SUPPLY page, from the link or (@p checked) the wiring
 * change a state read has just shown the module off for
 * (supply_page_wire_ready()), with the UART, the reservation and the save
 * that follow it.
 */
uint8_t supply_take(uint8_t off, uint8_t n, const uint16_t *in,
                           bool checked)
{
    /* What the driver says now, not what the last pass published: replies
     * taken since may have shown the module come on by itself. */
    if (s_pd_open) {
        /* Bytes already in the FIFO first: a reply there may show the
         * module come on by itself, and a rewire would throw it away. */
        uint8_t b;
        while (pd_uart_getc(&b)) {
            pdmini_rx(&s_pd, b, s_now_ms);
        }
    }
    supply_page_follow(&s_supply, s_pd_open ? &s_pd : NULL);
    const supply_page_t was = s_supply;
    if (s_supply_unsaved && (unsigned)off <= (unsigned)LINK_SP_OUTPUT
        && (unsigned)off + (unsigned)n > (unsigned)LINK_SP_OUTPUT
        && in[LINK_SP_OUTPUT - off] != 0u) {
        return LINK_NACK_NOT_ARMED;
    }
    const uint8_t nack =
        checked ? supply_page_wire_write(&s_supply, &s_outputs)
                : supply_page_write(&s_supply, off, n, in, &s_outputs,
                                    safety_gate_supply_ok(&s_beat, &s_dev));
    if (nack != 0u) {
        return nack;
    }
    /* Each register on its own: TX and RX swapped hold the same pins. */
    const bool rewired =
        supply_page_enabled(&s_supply) != supply_page_enabled(&was)
        || (supply_page_enabled(&s_supply)
            && (s_supply.regs[LINK_SP_TX_PIN] != was.regs[LINK_SP_TX_PIN]
                || s_supply.regs[LINK_SP_RX_PIN] != was.regs[LINK_SP_RX_PIN]
                || s_supply.regs[LINK_SP_BAUD] != was.regs[LINK_SP_BAUD]));
    const bool wiring =
        memcmp(&s_supply.regs[LINK_SP_ENABLE], &was.regs[LINK_SP_ENABLE],
               LINK_SP_OUTPUT * sizeof(uint16_t)) != 0;
    if (wiring && s_supply.regs[LINK_SP_OUTPUT] != 0u) {
        /* An ON in the same frame as new wiring: that wiring is not in
         * flash yet, and the ON waits for it. */
        s_supply = was;
        return LINK_NACK_NOT_ARMED;
    }
    if (rewired && !supply_probe()) {
        /* No UART for those pins: the wiring is as it was, and the panel
         * is told rather than shown a supply that is never there.  Wiring
         * still waiting for flash goes back to waiting, detached. */
        s_supply = was;
        if (s_supply_unsaved) {
            (void)supply_probe();
            s_supply_attach = supply_page_enabled(&s_supply);
        } else {
            (void)supply_rewire();
        }
        if (checked) {
            supply_page_wire_refuse(&s_supply);   /* said in FLAGS bit 9 */
        }
        return LINK_NACK_BAD_VALUE;
    }
    if (wiring) {
        save_outputs(&s_state);
        s_supply_unsaved = out_store_pending();
    }
    if (rewired) {
        /* Attached once the wiring is in flash, so a restart in between
         * finds the pins of the module it may have to switch off; a page
         * now disabled has nothing to attach. */
        if (s_supply_unsaved && supply_page_enabled(&s_supply)) {
            s_supply_attach = true;
        } else {
            (void)supply_rewire();
        }
    }
    return 0u;
}

uint8_t supply_write(void *ctx, uint8_t off, uint8_t n,
                            const uint16_t *in)
{
    (void)ctx;
    /* Judged against the pins core 1 still holds, as of its last tick. */
    sense_sync();
    return supply_take(off, n, in, false);
}

void sense_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    sense_sync();
    sense_page_read(&s_sense, off, n, out);
}

/*
 * The sensor bus's set-up: judged by the page against the bank and the
 * supply's pins, then its pins reserved, the set-up saved and core 1 told.
 * Refused while armed, so a save it asks for is taken at the next disarm
 * like any other.
 */
uint8_t sense_write(void *ctx, uint8_t off, uint8_t n,
                           const uint16_t *in)
{
    (void)ctx;
    /* The pins core 1 still holds, as of its last tick. */
    sense_sync();
    uint16_t was[LINK_SN_CONFIG_COUNT];
    memcpy(was, s_sense.sense, sizeof(was));
    const uint8_t nack = sense_page_write(&s_sense, off, n, in, &s_outputs,
                                          supply_page_pins(&s_supply)
                                          | tone_page_pins(&s_tone));
    if (nack != 0u) {
        return nack;
    }
    if (memcmp(was, s_sense.sense, sizeof(was)) != 0) {
        reserve_held();
        save_outputs(&s_state);
        capabilities_update();
        sense_order();
    }
    return 0u;
}

void servo_sense_read(void *ctx, uint8_t off, uint8_t n,
                             uint16_t *out)
{
    (void)ctx;
    sense_sync();
    sense_servo_read(&s_sense, off, n, out);
}

/*
 * SENSE_MAP: judged by the page against the bank and the pins SENSE's would
 * be, then, when it moved, the bus set up again as a SENSE write does it
 * and the map saved.  A change of WATCH alone is saved and leaves core 1 as
 * it is; IDENTITY's capabilities follow any change taken.
 */
void sense_map_rd(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    sense_sync();
    sense_map_read(&s_sense, off, n, out);
}

uint8_t sense_map_wr(void *ctx, uint8_t off, uint8_t n, const uint16_t *in)
{
    (void)ctx;
    sense_sync();
    const uint16_t gen = s_sense.cfg_gen;
    const bool was_set = s_sense.map_set;
    uint16_t was[LINK_SM_KEPT];
    memcpy(was, s_sense.map, sizeof(was));
    const uint8_t nack = sense_map_write(&s_sense, off, n, in, &s_outputs,
                                         supply_page_pins(&s_supply)
                                         | tone_page_pins(&s_tone));
    if (nack != 0u) {
        return nack;
    }
    if (s_sense.cfg_gen != gen) {
        reserve_held();
        sense_order();
    }
    if (s_sense.cfg_gen != gen || !was_set
        || memcmp(was, s_sense.map, sizeof(was)) != 0) {
        /* A WATCH alone moves IDENTITY's SERVO_SENSE bit as well. */
        capabilities_update();
        save_outputs(&s_state);
    }
    return 0u;
}

/* SENSE_WIN_A and SENSE_WIN_B: every channel's last window.  Read only. */
void sense_win_a_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    sense_sync();
    sense_xwin_read(&s_sense, 0u, off, n, out);
}

void sense_win_b_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    sense_sync();
    sense_xwin_read(&s_sense, 1u, off, n, out);
}

/* CH1's last four windows.  Read only: the page has no write handler. */
void servo_win_read(void *ctx, uint8_t off, uint8_t n, uint16_t *out)
{
    (void)ctx;
    sense_sync();
    sense_win_read(&s_sense, off, n, out);
}

/*
 * A capture armed or disarmed.  An arm watches its output channel for the
 * edge: the first pass that renders a changed pulse there stamps the frame
 * that carries it (outputs_hw_watch()).  Either way core 1 is told, and an
 * edge stamped for an earlier capture no longer counts.
 */
uint8_t servo_sense_write(void *ctx, uint8_t off, uint8_t n,
                                 const uint16_t *in)
{
    (void)ctx;
    const uint16_t gen = s_sense.cap_gen;
    const uint8_t nack = sense_servo_write(&s_sense, off, n, in, &s_outputs);
    if (nack != 0u || s_sense.cap_gen == gen) {
        return nack;
    }
    const uint16_t arm = s_sense.servo[LINK_SS_CAP_ARM];
    int watch = -1;
    if ((arm & LINK_SS_ARM) != 0u) {
        /* A programming port's pulse is its channel of the bank. */
        watch = ((arm & LINK_SS_ARM_PORT) != 0u)
                    ? (int)prog_port_channel((uint8_t)LINK_SS_ARM_OUT(arm))
                    : (int)LINK_SS_ARM_OUT(arm);
    }
    outputs_hw_watch(watch);
    s_edge_set = false;
    sense_order();
    return 0u;
}
