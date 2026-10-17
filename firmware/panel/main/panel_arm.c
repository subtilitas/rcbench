/*
 * The bench's arm on the control task: the state the loop starts from, the
 * watch on an arm until the render side has seen it, the run's totals,
 * disarming wherever it was asked for, one motor command, and the OUTPUTS
 * screen's binding onto the coprocessor's output pages.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <string.h>

#include "arming.h"
#include "bench_state.h"
#include "bind_link.h"
#include "heartbeat.h"
#include "kst_link.h"
#include "link_host.h"
#include "link_pages.h"
#include "motor_screen.h"
#include "out_bind.h"
#include "outputs.h"
#include "outputs_pages.h"
#include "outputs_screen.h"
#include "panel.h"
#include "prog_link.h"
#include "sense_link.h"
#include "servo_source.h"
#include "supply.h"
#include "supply_link.h"
#include "telemetry_sim.h"
#include "tone_link.h"
#include "touch_loss.h"
#include "ui_text.h"

static uint16_t pct_to_span(float pct)
{
    if (!(pct > 0.0f)) {
        return 0u;
    }
    if (pct >= 100.0f) {
        return (uint16_t)OUT_SPAN;
    }
    return (uint16_t)((pct * (float)OUT_SPAN / 100.0f) + 0.5f);
}

static uint16_t pct_to_hundredths(float pct)
{
    if (!(pct > 0.0f)) {
        return 0u;
    }
    if (pct >= 100.0f) {
        return (uint16_t)LINK_THROTTLE_MAX;
    }
    return (uint16_t)((pct * 100.0f) + 0.5f);
}

/*
 * The bench's own state, before the loop that maintains it.
 *
 * The last line is the one with a consequence outside this file:
 * s_pump_live tells exchange() that a control task exists, so the wait for a
 * link reply pumps the renderer instead of standing still.  Before this
 * point there is no such task and nothing to pump.
 */
void control_setup(telemetry_sim_t *sim, bench_state_t *bench)
{
    memset(bench, 0, sizeof(*bench));
    telemetry_sim_init(sim, NULL);
    /*
     * The panel's throttle is a channel in an output bank, under the same
     * arming and staleness rules as the coprocessor's outputs.  The slew is
     * not shared: this bank takes the `Ramp limit` setting and a bound
     * throttle channel on the coprocessor takes none, so the two ends do
     * answer that one differently.  Only this bank's answer is read, and
     * only while the link is down.
     */
    outputs_init(&s_out, now_ms());
    (void)outputs_set_role(&s_out, PANEL_CH_THROTTLE, OUT_ROLE_THROTTLE);
    publish_throttle_ramp();
    (void)outputs_set_slew(&s_out, PANEL_CH_THROTTLE, panel_throttle_ramp());
    arming_init(&s_arm, now_ms(), HEARTBEAT_SETTLE_MS);
    /* And past the settle an arm waits for the far end's own word on the
     * line; see far_line_trusted(). */
    arming_set_line_wait(&s_arm, ARMING_LINE_WAIT_MS);
    /* The supply starts switched off, its readings those of an off output. */
    supply_sim_init(&s_supply_sim);
    supply_sim_step(&s_supply_sim, 0.0f, &s_supply);
    supply_link_init(&s_supply_link);
    sense_link_init(&s_sense_link);
    servo_source_init(&s_servo_source);
    tone_link_init(&s_tone_link);
    kst_link_init(&s_kst_link);
    prog_link_init(&s_prog_link);
    s_supply_ms      = now_ms();
    s_supply_step_ms = now_ms();
    s_pump_live = true;
}

/*
 * An arm was applied: number it, and watch it until the render side has
 * seen it.  The snapshot carries the number; the render side acknowledges it
 * at the end of a frame that began with the bench armed and found no loss.
 */
/*
 * The run's charge and energy, counted here from what is shown -- the ESC's
 * readings over the link, the model's while it is down -- so one count runs
 * through the whole run whatever the source does; see bench_totals_t.
 * Control task only.  s_totals_ms is when it last counted.
 */
bench_totals_t s_totals;
uint32_t       s_totals_ms;

void arm_applied(void)
{
    /* A run starts with nothing counted. */
    bench_totals_reset(&s_totals);
    s_totals_ms = now_ms();
    ++s_arm_gen;
    arm_watch_begin(&s_arm_watch, s_arm_take_gen, s_arm_take_drv, s_arm_gen);
}

/*
 * The look at the touch stream for an arm still being handed over, once
 * per pass and after the snapshot is published.  control_pump() looks too,
 * inside every exchange's wait, and stops rather than disarms there; this
 * is the look that also ends the watch once the bench is no longer armed.
 *
 * Until the render side has seen the bench armed, a loss there is answered
 * by cancelling screens that still believe the bench is disarmed, which
 * posts no disarm: the render side reads the snapshot at the start of its
 * frame, so a frame can begin before this arm was published and find a loss
 * after.  So a loss of either kind between the taking and that
 * acknowledgement -- the render side dropping gestures, or a gap this task
 * found in the driver's stream -- undoes the arm here.  After it, a loss is
 * seen by the render side against an armed bench and answered there.  The
 * operator repeats the hold.
 */
void arm_watch_service(bool link_up)
{
    if (!outputs_armed(&s_out)) {
        arm_watch_end(&s_arm_watch);
        return;
    }
    if (arm_watch_lost(&s_arm_watch, atomic_load(&s_loss_gen),
                       atomic_load(&s_drv_gaps), atomic_load(&s_arm_ack))) {
        (void)disarm_here(link_up);
        /* A disarm counts no stop, so the supply is cut here as well: an
         * arm the watch undid is a stop to the supply wherever it is found
         * (control_pump() finds it by arming_stop()). */
        supply_switch(false);
        control_alert(TR(ALERT_TOUCH_ARM_AGAIN));
    }
}

/*
 * The outputs screen's choice, onto the coprocessor's two output pages.
 *
 * Leaves what became of it in s_outputs_result, which app_main hands to the
 * screen; screen state is app_main's alone.  True when a page may have been
 * written: the rate reset that leads the sequence landed, or there is none.
 */
bool write_output_binding(const outbind_t *bind)
{
    uint16_t cfg[LINK_CC_COUNT];
    uint16_t slots[LINK_OS_COUNT];
    outbind_to_chan_cfg(bind, cfg, endpoints_min(), endpoints_max());
    (void)outbind_to_slots(bind, slots);

    /*
     * The sequence is bind_link_write()'s: prepared and committed whole on a
     * coprocessor speaking 4.10, page by page on an older one.
     *
     * A write that got no answer and one that was refused are different things
     * to be told.  REFUSED sends the operator back to the pins they chose; NO
     * LINK sends them to the cable.  Collapsing the two would send them to the
     * wrong one every time the link dropped mid-write.
     */
    outputs_result_t res = OUTPUTS_OK;
    bind_rate_t rate = BIND_RATE_NOT_SENT;
    switch (bind_link_write(&s_host, &k_port, s_far_minor, cfg, slots,
                            &rate)) {
    case BIND_WRITTEN: res = OUTPUTS_OK;      break;
    case BIND_REFUSED: res = OUTPUTS_REFUSED; break;
    case BIND_NO_LINK:
    default:           res = OUTPUTS_NO_LINK; break;
    }
    /* The sequence put each slot back at its own rate first, or found that
     * it could not: the rate this end holds the far end to follows it. */
    servo_rate_reset_note(rate != BIND_RATE_NOT_SENT,
                          rate == BIND_RATE_LANDED);
    atomic_store(&s_outputs_result, (int)res);
    return rate == BIND_RATE_NOT_SENT || rate == BIND_RATE_LANDED;
}

/*
 * Disarming, wherever it was asked for.
 *
 * Two screens can arm the bench and both disarm it the same way: the policy
 * is told, this end's own bank stops driving, the throttle goes to zero so
 * the next arm cannot carry the last one's command, and the far end is told
 * while there is a link to tell it on.
 */
bool disarm_here(bool link_up)
{
    arming_request_disarm(&s_arm);
    outputs_arm(&s_out, false, now_ms());
    throttle_to_zero();
    servo_let_go();
    if (!link_up) {
        return false;   /* nothing to tell it on, so it has not been told */
    }
    link_msg_t ack = { 0 };
    return control_write(false, &ack);
}

/*
 * One motor command.  Arming is asked of the policy rather than done here;
 * the throttle is a channel command like any other.
 */
void apply_motor_cmd(const motor_cmd_t *mc, bool link_up,
                     bench_state_t *bench)
{
    switch (mc->kind) {
    case MOTOR_CMD_ARM:
        /*
         * Arming is the deliberate act that clears a latched stop. The policy
         * clears it, gives the heartbeat time to be believed and only then
         * asks for the write; see shared/safety/arming.c.
         */
        /* The throttle's outputs, and no other: a servo bound beside the
         * motor stays without a pulse.  Armed for the surfaces, the bench
         * disarms first (arming_request_arm_roles()). */
        arming_request_arm_roles(&s_arm, now_ms(),
                                 OUT_ROLE_BIT(OUT_ROLE_THROTTLE));
        break;
    case MOTOR_CMD_DISARM:
        (void)disarm_here(link_up);
        break;
    case MOTOR_CMD_THROTTLE:
        s_throttle_hundredths = pct_to_hundredths(mc->value);
        (void)outputs_set(&s_out, PANEL_CH_THROTTLE,
                          pct_to_span(mc->value), now_ms());
        break;
    case MOTOR_CMD_RESET_PEAKS: bench_state_reset_peaks(bench); break;
    default: break;
    }
}
