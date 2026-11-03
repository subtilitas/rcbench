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
#include "stop_latch.h"
#include "ui_text.h"

stop_latch_t s_stop_latch;
atomic_bool  s_stop_reset_request;

/**
 * @brief One register of the IOmcu read and tested: the read answered and
 *        none of @p bits is set.
 * @param page the page
 * @param reg  the register
 * @param bits the bits that mean on; 0 tests nothing beyond the answer
 * @param state true to compare the register with LINK_STATE_ARMED instead
 * @return true when it answered and reads off
 */
static bool far_reads_off(uint8_t page, uint8_t reg, uint16_t bits,
                          bool state)
{
    link_msg_t m = { 0 };
    if (!read_regs(&s_host, page, reg, 1u, &m) || m.op != LINK_OP_DATA) {
        return false;
    }
    if (state) {
        return m.regs[0] != (uint16_t)LINK_STATE_ARMED;
    }
    return (m.regs[0] & bits) == 0u;
}

/**
 * @brief Whether the bench reads SAFE with every output off, now.
 *
 * This end first: its bank disarmed, and neither supply on nor an ON on its
 * way.  With the link up the IOmcu as well, each page read in this pass:
 * STATUS not ARMED, the SUPPLY output off where it serves the page, the
 * PROG_SUPPLY output off where it serves that one.  With no link there is
 * nobody to ask, and an IOmcu that is wired has failed safe on link
 * silence after 200ms.
 * @param link_up whether the link to the IOmcu answers
 * @return true when everything reads off
 */
static bool everything_off(bool link_up)
{
    if (outputs_armed(&s_out) || supplies_live_or_coming()) {
        return false;
    }
    if (!link_up) {
        return true;
    }
    if (!far_reads_off(LINK_PAGE_STATUS, LINK_ST_STATE, 0u, true)) {
        return false;
    }
    if (s_supply_page
        && !far_reads_off(LINK_PAGE_SUPPLY, LINK_SP_FLAGS, LINK_SP_ON,
                          false)) {
        return false;
    }
    return !s_psup_link.page
           || far_reads_off(LINK_PAGE_PROG_SUPPLY, LINK_PS_FLAGS, LINK_PS_ON,
                            false);
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
