/**
 * @file
 * @brief The reset of a latched STOP on the control task: the machine's
 *        inputs read, and its reads, heartbeat and CLEAR carried out
 *        (stop_latch.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>

#include "arming.h"
#include "link_host.h"
#include "link_pages.h"
#include "outputs.h"
#include "panel.h"
#include "prog_supply_link.h"
#include "stop_latch.h"
#include "touch.h"
#include "ui_text.h"

stop_latch_t s_stop_latch;
atomic_bool  s_stop_reset_request;
atomic_uint  s_stop_reset_ms;
atomic_uint  s_touch_breaks;
/** @brief When the latest operator's STOP was applied; control task only. */
static uint32_t s_stop_press_ms;
/** @brief When the latest contact break was counted; control task only. */
static uint32_t s_touch_break_ms;
/** @brief The touch controller answered within STOP_LATCH_FRESH_MS at the
 *         last look; control task only. */
static bool     s_touch_fresh = true;
/** @brief The link's state for the pass under way. */
static bool     s_reset_link_up;

void stop_pressed_here(void)
{
    arming_stop_pressed(&s_arm);
    s_stop_press_ms = now_ms();
}

void stop_touch_watch(void)
{
    const bool fresh = touch_age_ms() < STOP_LATCH_FRESH_MS;
    if (s_touch_fresh && !fresh) {
        /* Contact reports stopped: a hold on STOP no longer rests on one. */
        s_touch_break_ms = now_ms();
        atomic_fetch_add(&s_touch_breaks, 1u);
    }
    s_touch_fresh = fresh;
}

/**
 * @brief The machine's inputs, read now; the hold's request is left as is.
 *
 * This end counts as on with its bank armed, the supply's output on or
 * coming, or the ports' own 5V on, owed or on its way.  A held programming
 * port is not an output: a STOP during a programming session is reset with
 * the session still open.
 * @param in receives the inputs
 */
static void read_inputs(stop_latch_in_t *in)
{
    in->now_ms   = now_ms();
    in->pressed  = arming_pressed_count(&s_arm);
    in->press_ms = s_stop_press_ms;
    in->breaks   = (uint32_t)atomic_load(&s_touch_breaks);
    in->break_ms = s_touch_break_ms;
    in->held     = arming_held(&s_arm);
    in->link_up  = s_reset_link_up;
    in->local_on = outputs_armed(&s_out) || supply_live_or_coming()
                   || atomic_load(&s_psup_ons_taken)
                          != atomic_load(&s_psup_ons_sent)
                   || s_psup_link.on_owed
                   || prog_supply_link_state(&s_psup_link) == PROG_SUPPLY_ON;
    in->supply_page = s_supply_page;
    in->prog_page   = s_psup_link.page;
}

/** @brief stop_latch_io_t.refresh. */
static void io_refresh(void *ctx, stop_latch_in_t *in)
{
    (void)ctx;
    read_inputs(in);
}

/** @brief stop_latch_io_t.read: one register of the IOmcu. */
static bool io_read(void *ctx, uint8_t page, uint8_t reg, uint16_t *out)
{
    (void)ctx;
    link_msg_t m = { 0 };
    if (!read_regs(&s_host, page, reg, 1u, &m) || m.op != LINK_OP_DATA) {
        return false;
    }
    *out = m.regs[0];
    return true;
}

/** @brief stop_latch_io_t.resume: the heartbeat edges again from the next
 *         beat.  Nothing is armed and nothing is switched on: each output
 *         needs its own hold. */
static void io_resume(void *ctx)
{
    (void)ctx;
    /* Before the release: a command queued while the STOP stood is out of
     * date from the pass that releases it. */
    atomic_fetch_add(&s_stop_resets, 1u);
    arming_stop_release(&s_arm);
}

/** @brief stop_latch_io_t.clear: CLEAR, written once.  One that does not
 *         come back leaves the IOmcu's latch to the next arm, which writes
 *         its own. */
static void io_clear(void *ctx)
{
    (void)ctx;
    link_msg_t ack = { 0 };
    (void)control_clear_failsafe(&ack);
}

static const stop_latch_io_t k_io = {
    .ctx = NULL, .read = io_read, .resume = io_resume, .clear = io_clear,
    .refresh = io_refresh,
};

void stop_reset_init(void)
{
    s_reset_link_up = false;
    stop_latch_in_t in = { 0 };
    read_inputs(&in);
    stop_latch_init(&s_stop_latch, &in);
}

void stop_reset_service(bool link_up)
{
    s_reset_link_up = link_up;
    stop_latch_in_t in = { 0 };
    read_inputs(&in);
    in.hold_done = atomic_exchange(&s_stop_reset_request, false);
    in.hold_ms   = (uint32_t)atomic_load(&s_stop_reset_ms);
    if (stop_latch_run(&s_stop_latch, &in, &k_io) == STOP_LATCH_GIVE_UP) {
        control_alert(TR(ALERT_STOP_NO_CONFIRM));
    }
}
