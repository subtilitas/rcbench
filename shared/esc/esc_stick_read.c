/*
 * Stick programming: the readouts of a run.  The words for each reason and
 * phase, whether an end lights the red light, and what a run has done so
 * far.  See esc_stick.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick.h"

const char *esc_stick_reason_text(esc_stick_reason_t r)
{
    switch (r) {
    case ESC_STICK_R_NONE:        return "";
    case ESC_STICK_R_STOP:        return "STOP";
    case ESC_STICK_R_BENCH_STOP:  return "BENCH STOPPED";
    case ESC_STICK_R_DISARMED:    return "SAFE";
    case ESC_STICK_R_LINK:        return "LINK LOST";
    case ESC_STICK_R_SUPPLY_OFF:  return "SUPPLY OFF";
    case ESC_STICK_R_SUPPLY_LOST: return "SUPPLY NOT ANSWERING";
    case ESC_STICK_R_STALE:       return "NO READINGS";
    case ESC_STICK_R_RATE:        return "READ RATE";
    case ESC_STICK_R_NOT_ARMED:   return "STILL SAFE";
    case ESC_STICK_R_NO_POWER:    return "NO POWER";
    case ESC_STICK_R_SUPPLY_ON:   return "SUPPLY STAYS ON";
    case ESC_STICK_R_TOUCH:       return "TOUCH LOST";
    case ESC_STICK_R_NO_BEEPS:    return "NO BEEPS";
    case ESC_STICK_R_HIGH:        return "CURRENT STAYS HIGH";
    case ESC_STICK_R_TIMEOUT:     return "TIMEOUT";
    case ESC_STICK_R_HAND:        return "NOT CONFIRMED";
    case ESC_STICK_R_USER:        return "ABORTED";
    case ESC_STICK_R_LEFT:        return "SCREEN LEFT";
    }
    return "?";
}

esc_stick_reason_t esc_stick_stop_reason(uint32_t stops, uint32_t pressed)
{
    return (stops > pressed) ? ESC_STICK_R_BENCH_STOP : ESC_STICK_R_STOP;
}

/*
 * Every reason decided here, without a default, so a reason added to the
 * list is decided too: -Wswitch names it.
 */
bool esc_stick_reason_is_fault(esc_stick_reason_t r)
{
    switch (r) {
    case ESC_STICK_R_NONE:          /* DONE, or not ended            */
    case ESC_STICK_R_STOP:          /* STOP pressed                  */
    case ESC_STICK_R_USER:          /* ABORT                         */
    case ESC_STICK_R_LEFT:          /* the screen left               */
        return false;
    case ESC_STICK_R_BENCH_STOP:    /* touch, or the far end         */
    case ESC_STICK_R_DISARMED:
    case ESC_STICK_R_LINK:
    case ESC_STICK_R_SUPPLY_OFF:
    case ESC_STICK_R_SUPPLY_LOST:
    case ESC_STICK_R_STALE:
    case ESC_STICK_R_RATE:
    case ESC_STICK_R_NOT_ARMED:
    case ESC_STICK_R_NO_POWER:
    case ESC_STICK_R_SUPPLY_ON:
    case ESC_STICK_R_NO_BEEPS:
    case ESC_STICK_R_HIGH:
    case ESC_STICK_R_TIMEOUT:
    case ESC_STICK_R_HAND:          /* no DONE: not chosen either    */
    case ESC_STICK_R_TOUCH:         /* events lost, not chosen       */
        return true;
    }
    return true;        /* a value outside the list is not as expected */
}

const char *esc_stick_phase_text(esc_stick_phase_t ph)
{
    switch (ph) {
    case ESC_STICK_IDLE:    return "READY";
    case ESC_STICK_ARMING:  return "ARMING";
    case ESC_STICK_SIGNAL:  return "SIGNAL";
    case ESC_STICK_POWER:   return "POWER ON";
    case ESC_STICK_ENTRY:   return "ENTRY";
    case ESC_STICK_ITEMS:   return "ITEMS";
    case ESC_STICK_VALUES:  return "VALUES";
    case ESC_STICK_STORE:   return "STORING";
    case ESC_STICK_CYCLE:   return "POWER CYCLE";
    case ESC_STICK_HAND_OFF: return "MANUAL STEP";
    case ESC_STICK_HAND_ON: return "MANUAL STEP, POWERED";
    case ESC_STICK_HAND_END: return "WAITING FOR THE ESC";
    case ESC_STICK_OFF:     return "POWER OFF";
    case ESC_STICK_DONE:    return "DONE";
    case ESC_STICK_ABORTED: return "ABORTED";
    }
    return "?";
}

bool esc_stick_running(const esc_stick_t *e)
{
    return e != NULL && e->phase != ESC_STICK_IDLE
           && e->phase != ESC_STICK_DONE && e->phase != ESC_STICK_ABORTED;
}

const esc_stick_out_t *esc_stick_out(const esc_stick_t *e)
{
    return (e != NULL) ? &e->out : NULL;
}

unsigned esc_stick_done_count(const esc_stick_t *e)
{
    unsigned n = 0u;
    for (unsigned i = 0; e != NULL && i < e->n; ++i) {
        n += e->done[i] ? 1u : 0u;
    }
    return n;
}

bool esc_stick_unsure(const esc_stick_t *e, unsigned i)
{
    return e != NULL && i < e->n && e->done[i] && i == e->active
           && esc_stick_cut_short(e);
}

unsigned esc_stick_made_count(const esc_stick_t *e)
{
    unsigned n = 0u;
    for (unsigned i = 0; e != NULL && i < e->n; ++i) {
        n += (e->done[i] && !esc_stick_unsure(e, i)) ? 1u : 0u;
    }
    return n;
}

unsigned esc_stick_beeps(const esc_stick_t *e)
{
    if (e == NULL || !e->det.group_open) {
        return 0u;
    }
    return (unsigned)e->det.longs * e->det.long_equals_short + e->det.shorts
           + (e->det.high ? 1u : 0u);
}
