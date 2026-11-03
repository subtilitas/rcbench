/**
 * @file
 * @brief The reset of a latched STOP on the control task: the hold's request
 *        taken, the IOmcu asked whether everything is off, and the
 *        heartbeat and CLEAR given back once it is (stop_latch.h).
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
#include "ui_text.h"

stop_latch_t s_stop_latch;
atomic_bool  s_stop_reset_request;

/**
 * @brief One register of the IOmcu, read.
 * @param page the page
 * @param reg  the register
 * @param out  receives its value
 * @return true when the read was answered with the value
 */
static bool far_reg(uint8_t page, uint8_t reg, uint16_t *out)
{
    link_msg_t m = { 0 };
    if (!read_regs(&s_host, page, reg, 1u, &m) || m.op != LINK_OP_DATA) {
        return false;
    }
    *out = m.regs[0];
    return true;
}

/**
 * @brief Whether the bench reads SAFE with every output off, now
 *        (stop_latch_all_off()).
 *
 * This end: its bank, the supply's output or an ON on its way, the ports'
 * own 5V on, owed or on its way.  A held programming port is not an output:
 * the gates of the install and the card count it as live
 * (supplies_live_or_coming()), and a STOP during a programming session is
 * reset with the session still open.  With the link up, the IOmcu's pages,
 * each read in this pass and stopping at the first that reads on or goes
 * unanswered: STATUS, SUPPLY where it serves the page, PROG_SUPPLY where it
 * serves that one.
 * @param link_up whether the link to the IOmcu answers
 * @return true when everything reads off
 */
static bool everything_off(bool link_up)
{
    stop_latch_reading_t r = {
        .bank_armed = outputs_armed(&s_out),
        .supply_on  = supply_live_or_coming(),
        .prog_on    = atomic_load(&s_psup_ons_taken)
                          != atomic_load(&s_psup_ons_sent)
                      || s_psup_link.on_owed
                      || prog_supply_link_state(&s_psup_link)
                             == PROG_SUPPLY_ON,
        .port_held  = s_prog_taken,
        .link_up    = link_up,
    };
    if (!link_up || !stop_latch_all_off(&r)) {
        return stop_latch_all_off(&r);
    }
    uint16_t v = 0u;
    r.far_read = far_reg(LINK_PAGE_STATUS, LINK_ST_STATE, &v);
    r.far_armed = r.far_read && v == (uint16_t)LINK_STATE_ARMED;
    if (r.far_read && !r.far_armed && s_supply_page) {
        r.far_read   = far_reg(LINK_PAGE_SUPPLY, LINK_SP_FLAGS, &v);
        r.far_supply = r.far_read && (v & LINK_SP_ON) != 0u;
    }
    if (r.far_read && !r.far_armed && !r.far_supply && s_psup_link.page) {
        r.far_read = far_reg(LINK_PAGE_PROG_SUPPLY, LINK_PS_FLAGS, &v);
        r.far_prog = r.far_read && (v & LINK_PS_ON) != 0u;
    }
    return stop_latch_all_off(&r);
}

/**
 * @brief The reset of a latched STOP, once a pass of the control task after
 *        the stops are served: the hold's request, the confirmation while
 *        one waits, and what comes of it.
 * @param link_up whether the link to the IOmcu answers
 */
void stop_reset_service(bool link_up)
{
    const uint32_t now = now_ms();
    (void)stop_latch_follow(&s_stop_latch, arming_pressed_count(&s_arm), now);
    if (atomic_exchange(&s_stop_reset_request, false)) {
        (void)stop_latch_ask(&s_stop_latch, arming_stopped(&s_arm), now);
    }
    const bool confirmed = stop_latch_read_due(&s_stop_latch, now)
                           && everything_off(link_up);
    /*
     * The exchanges pump, so a STOP can land in them: it is counted before
     * the step, and withdraws the reset rather than being reset by it.
     */
    (void)stop_latch_follow(&s_stop_latch, arming_pressed_count(&s_arm),
                            now_ms());
    switch (stop_latch_step(&s_stop_latch, now_ms(), arming_stopped(&s_arm),
                            confirmed)) {
    case STOP_LATCH_RESUME: {
        /*
         * The heartbeat edges again from the next beat; CLEAR releases the
         * IOmcu's arm latch, which its monitor set when the line went
         * quiet.  Written once, and taken whether or not its monitor trusts
         * the line yet: CLEAR carries no ARM.  Nothing is armed and nothing
         * is switched on: each output needs its own hold.  A CLEAR that
         * does not come back leaves the latch to the next arm, which
         * writes its own.
         */
        arming_stop_release(&s_arm);
        if (link_up) {
            link_msg_t ack = { 0 };
            (void)control_clear_failsafe(&ack);
        }
        break;
    }
    case STOP_LATCH_GIVE_UP:
        control_alert(TR(ALERT_STOP_NO_CONFIRM));
        break;
    default:
        break;
    }
}
