/**
 * @file
 * @brief The bench's arm on the control task: the state the loop starts from, the
 * watch on an arm until the render side has seen it, the run's totals, the
 * latched stop and the act the arming policy asks for, the OUTPUTS screen's
 * binding onto the IOmcu's output pages, disarming wherever it was
 * asked for, one motor command, and the screens' commands in their order.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "arming.h"
#include "bench_state.h"
#include "bind_link.h"
#include "heartbeat.h"
#include "hitec_link.h"
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
#include "servo_link.h"
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

/* The stop count this task has already let go for; see service_arming(). */
static uint32_t    s_stops_served;
/*
 * When the outstanding disarm started holding the safety line down, and how
 * long that may go on for.
 *
 * Twice HEARTBEAT_MAX_GAP_MS (150ms), so a far end that never answers has
 * certainly failed safe by then and the line can go back up: a disarm nobody
 * can deliver must not hold it down for ever.
 */
#define DISARM_INHIBIT_MS (2u * HEARTBEAT_MAX_GAP_MS)
static bool        s_disarm_timing;   /**< whether s_disarm_since means anything */
static uint32_t    s_disarm_since;

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
     * arming and staleness rules as the IOmcu's outputs.  The slew is
     * not shared: this bank takes the `Ramp limit` setting and a bound
     * throttle channel on the IOmcu takes none, so the two ends do
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
    hitec_link_init(&s_hitec_link);
    prog_link_init(&s_prog_link);
    s_supply_ms      = now_ms();
    s_supply_step_ms = now_ms();
    s_pump_live = true;
}

/*
 * The run's charge and energy, counted here from what is shown -- the ESC's
 * readings over the link, the model's while it is down -- so one count runs
 * through the whole run whatever the source does; see bench_totals_t.
 * Control task only.  s_totals_ms is when it last counted.
 */
bench_totals_t s_totals;
uint32_t       s_totals_ms;
/*
 * A SERVO run's voltage, current, power, charge and energy are the supply's:
 * its reading at each sample, and the run's totals of it counted here over
 * the same steps as s_totals.  Control task only.
 */
log_rail_t     s_rail;
bench_totals_t s_rail_totals;

/**
 * @brief An arm was applied: number it, and watch it until the render side has
 * seen it.  The snapshot carries the number; the render side acknowledges it
 * at the end of a frame that began with the bench armed and found no loss.
 */
static void arm_applied(void)
{
    /* A run starts with nothing counted. */
    bench_totals_reset(&s_totals);
    bench_totals_reset(&s_rail_totals);
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
 * The latched stop, and then the act the arming policy asks for.
 *
 * link_up is passed in because an arm and a disarm are written to the
 * IOmcu's control page only while the link is up.  The flag is the
 * control loop's own and is published nowhere this could read it.
 */
/*
 * A disarm that was asked for, whether or not its queue entry survived.
 *
 * Doing it twice is doing it once: the policy, the bank and the far end all
 * take the same state again.  Called between queue entries as well as before
 * the drain, because a backlog of servo positions can hold the drain for
 * seconds -- three exchanges each -- and a disarm must not wait behind
 * commands that were asked for before it.
 */
static void service_disarm(bool link_up)
{
    bool owed = false;

    if (servo_link_take_release()) {
        owed = true;
    }

    if (atomic_load(&s_disarm_request)) {
        if (!s_disarm_timing) {
            s_disarm_timing = true;
            s_disarm_since  = now_ms();
        }
        const bool told = disarm_here(link_up);
        /*
         * The request stands until the far end has taken it, because the
         * heartbeat is withheld while it stands and clearing it first would
         * put the line back up during the very transaction meant to deliver
         * it -- with the far end still armed if that transaction is lost.
         *
         * Or until the line has been down long enough that the far end has
         * failed safe on its own account, which is the same outcome by the
         * other route and stops a disarm nobody can deliver from holding the
         * line down for ever.
         */
        if (told
            || (uint32_t)(now_ms() - s_disarm_since) >= DISARM_INHIBIT_MS) {
            atomic_store(&s_disarm_request, false);
            s_disarm_timing = false;
        }
        owed = true;
    }

    /* Whichever of the two it was, the position it let go of is settled
     * here rather than at the end of the drain, behind whatever else is
     * queued. */
    if (owed) {
        servo_link_service(link_up);
    }
}

/**
 * @brief The latched stop and the act the arming policy asks for, once a
 *        pass of the control task: the router's backstop STOP unless the
 *        pump counted the same press, the let-go every stop owes, the
 *        supply, then the arm or the disarm.
 * @param link_up whether the link to the IOmcu answers
 */
void service_arming(bool link_up)
{
    /*
     * STOP latches rather than clearing on the next frame: a stop that lasts
     * one frame is one the IOmcu may never see, and its monostable holds
     * for longer than a frame.  Only an explicit arm clears it.
     *
     * This is the router's backstop for a press the pump's own hit test
     * missed; a press it saw has already been applied there.
     */
    if (atomic_exchange(&s_stop_request, false)
        && atomic_exchange(&s_stop_counted, 0u) == 0u) {
        arming_stop_pressed(&s_arm);        /* the band's STOP, pressed */
    }

    /*
     * Every stop lets go of what was being driven, whether or not the bench
     * was armed and whichever thing raised it -- a press, the far end, touch
     * that stopped answering.  A bench that was not armed still had a servo
     * held, and nothing else would have released it: the position would go on
     * being refreshed every 100ms, over the top of an outputs binding made
     * later.
     */
    service_disarm(link_up);

    if (arming_stop_unserved(&s_arm, &s_stops_served)) {
        throttle_to_zero();
        servo_link_let_go();
    }
    supply_follow_stops();

    /*
     * One place decides, and it is the one under test.  A disarm here is the
     * policy's, not this loop's: a latched stop, dead touch, or an arm that
     * finished settling.
     */
    /*
     * An arm past its settle asks the far end whether it trusts the line,
     * once a pass until it does or the bound has passed; a yes that comes
     * back past the bound arms nothing.  With no far end there is nobody to
     * ask and the settle alone decides, however late this pass is.  The exchange pumps, so a STOP can land in
     * it: the policy then holds no arm and drops the answer.
     *
     * A question nobody answers is the link going quiet under a waiting
     * arm, and is stopped here as poll_far_end() stops it on its own edge:
     * left to the policy it would run past the bound and be given up, and
     * the poll would then find no arm to stop.
     */
    if (!link_up) {
        arming_line_nobody(&s_arm);
    } else if (arming_line_wanted(&s_arm, now_ms())) {
        bool answered = true;
        const bool trusted = far_line_trusted(&answered);
        if (answered) {
            arming_line_report(&s_arm, trusted);
        } else if (arming_link_lost(&s_arm, outputs_armed(&s_out))) {
            far_end_stop_here();
        }
    }

    const bool was_touch_dead = arming_touch_dead(&s_arm, now_ms());
    switch (arming_step(&s_arm, now_ms())) {
    case ARMING_ACT_DISARM:
        outputs_arm(&s_out, false, now_ms());
        throttle_to_zero();
        servo_link_let_go();
        if (was_touch_dead) {
            control_alert(TR(ALERT_TOUCH_STOPPED));
        }
        if (link_up) {
            link_msg_t ack = { 0 };
            (void)control_write(false, &ack);
        }
        break;
    case ARMING_ACT_ARM: {
        link_msg_t ack = { 0 };
        const uint32_t quiet = s_unanswered;
        /*
         * Each point of the handshake is decided by arming_handshake_step()
         * from what this loop learned for it; the writes, the alerts and
         * the bank stay here.
         */
        arming_hs_facts_t facts = { .link_up    = link_up,
                                    .port_taken = s_prog_taken };
        if (arming_handshake_step(&s_arm, ARMING_HS_HANDED, &facts, now_ms())
            == ARMING_HS_PROG) {
            control_alert(TR(ALERT_ARM_PROG));
            break;
        }
        /*
         * An arm starts from nothing, and this is where that is made true
         * rather than hoped for.  ARM and THROTTLE travel in one
         * transaction, so a command left over from before the disarm is the
         * first thing the far end acts on -- and it steps straight to it.
         * The IOmcu's throttle is bank channel 8, off the CHAN_CFG
         * page that addresses channels 0 to 7, so its slew is never
         * configured and stays at the zero outputs_init() left: there is no
         * ramp on that channel at any time.
         *
         * Every disarm returns the command to zero as well.  A disarm that
         * forgot to would be a motor stepping to its old position on a bench
         * the operator had just stopped, and there has been one.
         */
        throttle_to_zero();
        servo_link_let_go();
        /*
         * And the surfaces are centred before the arm, not after it.  The far
         * end applies ARM and stamps every channel's clock before it steps its
         * outputs, so a channel still holding a position at that moment
         * renders it for as long as the centre takes to arrive.
         */
        servo_link_service(link_up);
        facts.release_owed = link_up && servo_link_release_owed();
        /* Asked only where the answer decides: asking can take an
         * exchange. */
        facts.rate_settled = !link_up || facts.release_owed
                             || servo_link_rate_settled();
        /*
         * And the throttle's pulse endpoints, owed or blocked by the last
         * answer.  This task pays the debt (endpoints_service(), at the
         * poll), so no payment is under way while this runs.  An edit the
         * settings screen publishes during the CLEAR is asked about again
         * after it, and one during the frame that arms after that.
         */
        facts.endpoints_blocked = endpoints_blocked(&s_endpoints);
        switch (arming_handshake_step(&s_arm, ARMING_HS_PREPARED, &facts,
                                      now_ms())) {
        case ARMING_HS_SERVO_HELD:
            /*
             * The release did not land, so the far end still holds the
             * position.  Arming now would render it before anything else
             * reached it, so the arm is refused and the debt stays; the next
             * attempt starts by paying it.
             *
             * Only while there is a link.  With none, the debt cannot be paid
             * by anybody and nothing at the far end is being armed either, so
             * refusing would leave the panel unable to arm its own bank --
             * the simulator included -- until an IOmcu answered again.
             * What the far end must not do meanwhile is arm; see
             * poll_far_end().
             */
            arm_write_failed(quiet, TR(ALERT_SERVO_NOT_RELEASED));
            break;
        case ARMING_HS_SERVO_RATE:
            /* The far end may hold a rate faster than the surfaces' servos
             * take, and nothing here knows otherwise; refused like an
             * unpaid release, and asked again on the next attempt. */
            arm_write_failed(quiet, TR(ALERT_SERVO_RATE));
            break;
        case ARMING_HS_ENDPOINTS:
            /*
             * Idle pulse and Full pulse not in force at the far end: owed,
             * or refused or stored otherwise by the last answer.  The frame
             * that arms carries no CHAN_CFG, so the throttle would run on
             * whatever range the far end holds.  Refused like an unpaid
             * release, nothing written; the alert names what to change,
             * and the operator holds ARM again.
             */
            arm_write_failed(
                quiet, motor_endpoints_alert(endpoints_block(&s_endpoints)));
            break;
        case ARMING_HS_CLOSED:
            /* Past the arm's deadline before CLEAR; see below. */
            control_alert(TR(ALERT_ARM_REFUSED));
            break;
        case ARMING_HS_GO: {
            /*
             * Two exchanges: CLEAR on its own, then the frame that arms.
             * Each can wait a second, and what the operator wants can change
             * between them: the pump runs inside both and applies a stop,
             * and a disarm can be posted while the clear is still on the
             * wire.  Asked again before the write that actually arms,
             * because after it the far end is driving and nothing here can
             * take it back for the length of a timeout.
             *
             * CLEAR travels first and alone.  The far end checks ARM against
             * its failsafe before it applies a CLEAR from the same frame, so
             * a frame carrying both is refused with NOT_ARMED exactly when
             * the clear was needed.  The frame that arms carries ARM,
             * THROTTLE and the pole count together, so the run starts on the
             * count that was sent or does not start; control_arm() says why.
             */
            /*
             * And the arm's deadline, the one its wait for the line ended
             * at, is asked again before each of the two writes: the
             * exchanges above and the CLEAR are answered in about a
             * millisecond and can be answered up to LINK_HOST_TIMEOUT_MS
             * (1000ms) late.  Past it the arm is given up and nothing
             * further is written (arming_handshake_open()): before CLEAR
             * as a refusal, after it as a stop, whose withheld heartbeat
             * sets the far end's latch again.  The frame that arms is the
             * limit: acknowledged late, it has armed the far end, at rest.
             */
            if (!control_clear_failsafe(&ack)) {
                arm_write_failed(quiet, TR(ALERT_ARM_REFUSED));
                break;
            }
            facts.disarm_asked   = atomic_load(&s_disarm_request);
            facts.endpoints_blocked = endpoints_blocked(&s_endpoints);
            const arming_hs_t cleared =
                arming_handshake_step(&s_arm, ARMING_HS_CLEARED, &facts,
                                      now_ms());
            if (cleared == ARMING_HS_WITHDRAWN) {
                /* Stopped or disarmed while the clear was in flight.  No
                 * alert: the operator asked for this and knows. */
                break;
            }
            if (cleared == ARMING_HS_ENDPOINTS) {
                control_alert(
                    motor_endpoints_alert(endpoints_block(&s_endpoints)));
                break;
            }
            if (cleared != ARMING_HS_GO) {
                control_alert(TR(ALERT_ARM_REFUSED));
                break;
            }
            if (!control_arm(&ack)) {
                arm_write_failed(quiet, TR(ALERT_ARM_REFUSED));
                break;
            }
            /*
             * An edit published while the frame that arms was on the wire.
             * The far end is armed at rest on the range it held; ARM = 0
             * goes in the next exchange, and the bank here is not armed.
             */
            facts.endpoints_blocked = endpoints_blocked(&s_endpoints);
            if (arming_handshake_step(&s_arm, ARMING_HS_ARMED, &facts,
                                      now_ms())
                == ARMING_HS_ENDPOINTS) {
                (void)disarm_here(link_up);
                control_alert(
                    motor_endpoints_alert(endpoints_block(&s_endpoints)));
            } else {
                /* The roles the far end armed, which an older one makes
                 * every role. */
                outputs_arm_roles(&s_out, outputs_arm_roles_of(s_arm_value),
                                  now_ms());
                arm_applied();
            }
            break;
        }
        case ARMING_HS_LOCAL:
            outputs_arm_roles(&s_out,
                              (uint8_t)(arming_roles(&s_arm) & OUT_ROLES_ALL),
                              now_ms());
            arm_applied();
            break;
        default:
            /* No other verdict is given at ARMING_HS_PREPARED. */
            break;
        }
        break;
    }
    case ARMING_ACT_GIVE_UP:
        /* The far end did not come to trust the line inside the bound.
         * Nothing was written, CLEAR included, so its arm latch stands. */
        control_alert(TR(ALERT_ARM_REFUSED));
        break;
    default:
        break;
    }
}

/*
 * The outputs screen's choice, onto the IOmcu's two output pages.
 *
 * Leaves what became of it in s_outputs_result, which app_main hands to the
 * screen; screen state is app_main's alone.  True when a page may have been
 * written: the rate reset that leads the sequence landed, or there is none.
 */
static bool write_output_binding(const outbind_t *bind)
{
    uint16_t cfg[LINK_CC_COUNT];
    uint16_t slots[LINK_OS_COUNT];
    outbind_to_chan_cfg(bind, cfg, endpoints_min(&s_endpoints),
                        endpoints_max(&s_endpoints));
    (void)outbind_to_slots(bind, slots);

    /*
     * The sequence is bind_link_write()'s: prepared and committed whole on an
     * IOmcu speaking 4.10, page by page on an older one.
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
    servo_link_rate_reset_note(rate != BIND_RATE_NOT_SENT,
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
    servo_link_let_go();
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
static void apply_motor_cmd(const motor_cmd_t *mc, bool link_up,
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

/*
 * What the screens asked for, in the order they asked it.
 *
 * The screens run on app_main and the link belongs to this task, so a command
 * crosses as a queue entry and is acted on here.  link_up is passed in
 * because most of these write to the far end, and the flag that says whether
 * that is possible is the control loop's.
 */
void drain_commands(bool link_up, bench_state_t *bench)
{
    panel_cmd_t pc;
    while (xQueueReceive(s_cmd_q, &pc, 0) == pdTRUE) {
        /*
         * Between entries, because a backlog of positions is seconds of
         * exchanges and a disarm asked for during it must not wait them out.
         */
        service_disarm(link_up);
        supply_service();

        /* The supply is the panel's own and touches no link; its ON carries
         * its own checks.  See apply_supply_cmd(). */
        if (pc.kind == PANEL_CMD_SUPPLY) {
            apply_supply_cmd(&pc);
            continue;
        }
        if (pc.kind == PANEL_CMD_PROG5V) {
            apply_prog5v_cmd(&pc);
            continue;
        }

        /*
         * Nothing asked for before a stop drives anything after it.
         *
         * The count of stops the sender had seen is no longer current, so
         * something stopped the bench between the asking and the arriving.
         * An arm would clear that stop's own latch; a position or a throttle
         * would put back what the stop had just let go of -- and a stop from
         * touch dying or from the far end has no queued STOP behind it to
         * settle the surfaces a second time.
         *
         * Only what drives.  A disarm, a release or a binding asked for
         * before the stop still means what it meant.
         */
        const bool drives = (pc.kind == PANEL_CMD_MOTOR
                             && (pc.motor.kind == MOTOR_CMD_ARM
                                 || pc.motor.kind == MOTOR_CMD_THROTTLE))
                            || (pc.kind == PANEL_CMD_SERVO
                                && (pc.servo.kind == SERVO_CMD_ARM
                                    || pc.servo.kind == SERVO_CMD_POSITION
                                    || pc.servo.kind == SERVO_CMD_CENTRE
                                    || pc.servo.kind == SERVO_CMD_SWEEP
                                    || pc.servo.kind == SERVO_CMD_HOLD));
        if (drives
            && (pc.stops != arming_stop_count(&s_arm)
                || pc.lets_go != atomic_load(&s_lets_go))) {
            continue;
        }
        /*
         * Nothing arms across a touch loss.  The hold that posted this arm
         * may have completed on a contact whose release went missing, and
         * the render side can cancel only what it has not yet handed over;
         * this arm was.  So the driver's queue is drained first -- a loss in
         * it that happened before now is found here and queued as a notice
         * the render side has not consumed -- and the arm is dropped when the
         * render side has dropped gestures since it posted, or has a notice
         * it had not yet seen (arm_watch_take_ok()).  The operator repeats
         * the hold.
         */
        const bool arms = (pc.kind == PANEL_CMD_MOTOR
                           && pc.motor.kind == MOTOR_CMD_ARM)
                          || (pc.kind == PANEL_CMD_SERVO
                              && pc.servo.kind == SERVO_CMD_ARM);
        if (arms && s_prog_taken) {
            /* Programming a servo and an armed bench are exclusive,
             * whichever screen asks. */
            control_alert(TR(ALERT_ARM_PROG));
            continue;
        }
        if (arms) {
            control_pump();
            if (!arm_watch_take_ok(pc.loss_gen, atomic_load(&s_loss_gen),
                                   s_lost_notice_seq, pc.consumed_seq)) {
                continue;
            }
            s_arm_take_gen = pc.loss_gen;
            s_arm_take_drv = atomic_load(&s_drv_gaps);
        }
        if (pc.kind == PANEL_CMD_STOP) {
            arming_stop_pressed(&s_arm);    /* a screen's STOP, pressed */
            outputs_arm(&s_out, false, now_ms());
            throttle_to_zero();
            servo_link_let_go();
            if (link_up) {
                link_msg_t ack = { 0 };
                (void)control_write(false, &ack);
            }
            continue;
        }
        if (pc.kind == PANEL_CMD_OUTPUTS) {
            if (!link_up) {
                atomic_store(&s_outputs_result, (int)OUTPUTS_NO_LINK);
                post_no_reading();
                continue;
            }
            /*
             * Only over pages that were read.  A binding queued before a
             * read failed, or built on a screen that showed nothing, would
             * replace whatever the far end holds; the screens refuse the
             * tap themselves once they are told, and this is the same rule
             * for a command already on its way (bind_link_may_write()).
             * Nothing is sent.  LAST WRITE goes from WRITTEN to NOT WRITTEN;
             * a NO LINK or REFUSED that explains the state stays.
             */
            if (!bind_link_may_write(s_bind_read, s_board, &pc.bind)) {
                int was = (int)OUTPUTS_OK;
                (void)atomic_compare_exchange_strong(&s_outputs_result, &was,
                                                     (int)OUTPUTS_IDLE);
                continue;
            }
            /* The sequence leads with the SERVO rate reset and writes
             * nothing over a rate that did not go back: a refusal is told
             * as REFUSED, no answer as NO LINK. */
            if (!write_output_binding(&pc.bind)) {
                /* No page was written.  The screens still show the tap, so
                 * the far end is asked what it holds. */
                read_outputs_binding();
                continue;
            }
            /*
             * What the horn may drive has changed, and what this end sent is
             * not the answer: a write whose acknowledgement was lost was
             * applied over there all the same, so a result of NO LINK does
             * not mean the old binding still stands.  The mask goes unknown
             * and the far end is asked, here rather than at the next link-up
             * edge -- the operator can bind a servo and walk straight to the
             * screen that drives it, and a read that fails is retried by the
             * poll loop.
             *
             * The debt is not voided.  A binding writes the roles and the
             * slots and not the commands, so a surface the horn left
             * somewhere is still there afterwards and still owes a centre.
             * The release goes to whatever is a surface under the new
             * binding: a channel this binding turned into a motor carries the
             * rest its new role brought with it, and a servo horn writing to
             * it would undo exactly that.  What stops here is the holding --
             * the screen's position was for the wiring just replaced.
             */
            servo_link_forget_binding();
            servo_link_let_go();
            /* And the throttle channels may have moved: the pair is asked
             * again at the next poll, and a switch to ARMED waits for it. */
            endpoints_recheck(&s_endpoints);
            read_outputs_binding();
            continue;
        }
        if (pc.kind == PANEL_CMD_SERVO) {
            servo_link_apply(pc.servo, link_up, pc.stops);
            continue;
        }

        apply_motor_cmd(&pc.motor, link_up, bench);
    }
}
