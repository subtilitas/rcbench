/*
 * The render side's services, once a frame on app_main: the programmer's
 * model, the supply's levels, the servo test's lines, the rotary knob, the
 * touch drain and the screens' commands on their way to the control task.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "knob.h"
#include "knob_task.h"
#include "kst_panel.h"
#include "kst_prog.h"
#include "motor_screen.h"
#include "panel.h"
#include "esc_stick_panel.h"
#include "servo_link.h"
#include "servo_screen.h"
#include "settings.h"
#include "storage.h"
#include "supply_screen.h"
#include "touch_loss.h"
#include "ui_screen.h"

static const char *TAG = "rcbench";

/*
 * The programmer's model, once a frame: what the control task last
 * published, the commands it has decided on, and its file.
 *
 * A command the queue does not take is not carried out, and the model says
 * so when its answer does not come (KST_PROG_MSG_TIMEOUT).
 */
void kst_ui_service(void)
{
    static kst_prog_feed_t feed;
    static bool            file_out;

    snap_lock();
    feed = s_snap.kst;
    snap_unlock();
    kst_panel_feed(&feed);

    /* A card put in after the boot, or found in place of one taken out,
     * is mounted here, on the task RESCAN mounts one on: once for an
     * opening of the programmer or a tap on one of its actions while none
     * is mounted (storage_check()). */
    if (!file_out && !storage_mounted() && kst_panel_card_look()) {
        (void)storage_check();
        /* And its record is read before a command or a file goes out: a
         * write interrupted before this start stands as at the start. */
        kst_record_load(true);
    }

    /* A command stays with the model while the queue has no room: one
     * taken here and not queued is gone, an ABORT among them. */
    kst_prog_cmd_t cmd;
    while (uxQueueSpacesAvailable(s_kst_q) > 0u && kst_panel_take(&cmd)) {
        if (xQueueSend(s_kst_q, &cmd, 0) != pdTRUE) {
            ESP_LOGW(TAG, "servo programmer: a command was lost");
        }
    }

    /* The servo files: the log task has the card.  No card call is made
     * here. */
    static bool card_out;
    if (card_out
        && atomic_load(&s_kst_card_answered)
               == atomic_load(&s_kst_card_sent)) {
        card_out = false;
        kst_panel_card_done(&s_kst_card_done);
    }
    if (!card_out && kst_panel_card_take(&s_kst_card_req)) {
        atomic_fetch_add(&s_kst_card_sent, 1u);
        card_out = true;
    }

    if (file_out) {
        if (atomic_load(&s_kst_file_done) != atomic_load(&s_kst_file_sent)) {
            return;
        }
        file_out = false;
        kst_panel_file_done(atomic_load(&s_kst_file_ok));
    }
    const size_t len = kst_panel_file_take(s_kst_file, sizeof(s_kst_file));
    if (len != 0u) {
        s_kst_file_len = len;
        atomic_fetch_add(&s_kst_file_sent, 1u);
        file_out = true;
    }
}

/*
 * Take what the screens have decided and hand it to the control task.
 *
 * Called after every touch event as well as once a pass, because a screen's
 * enter() can be long: a command recorded by the screen being left must not
 * wait behind the work of the screen being entered.
 */
void flush_screen_commands(uint32_t stops_now);

/*
 * Queue a command, and do not lose it.  Every command is already taken from
 * its screen by the time it gets here, so a refused send is a discarded
 * disarm or a throttle that never arrives.
 */
static void send_cmd(const panel_cmd_t *in)
{
    panel_cmd_t cmd = *in;
    const panel_cmd_t *pc = &cmd;
    /*
     * A disarm does not depend on the queue.
     *
     * The queue drops its oldest entry when it is full, and a screen that is
     * being left generates commands on the way out while the next screen
     * generates more: a disarm posted by leave() can be evicted by them and
     * the bench stays armed with nobody watching it.  So it is also a flag,
     * the way a stop is, and applying it twice costs nothing.
     */
    if (pc->kind == PANEL_CMD_MOTOR && pc->motor.kind == MOTOR_CMD_DISARM) {
        atomic_fetch_add(&s_lets_go, 1u);
        atomic_store(&s_disarm_request, true);
    } else if (pc->kind == PANEL_CMD_SERVO
               && pc->servo.kind == SERVO_CMD_DISARM) {
        atomic_fetch_add(&s_lets_go, 1u);
        atomic_store(&s_disarm_request, true);
        servo_link_ask_release();
    } else if (pc->kind == PANEL_CMD_SERVO
               && pc->servo.kind == SERVO_CMD_RELEASE) {
        /*
         * RELEASE lets go of the pin without disarming the bench, and it is
         * as much a safety command as the disarm: waiting behind a backlog
         * of positions, three exchanges each, would hold the servo for
         * seconds after the operator asked it to stop.
         */
        atomic_fetch_add(&s_lets_go, 1u);
        servo_link_ask_release();
    } else if (pc->kind == PANEL_CMD_SERVO
               && (pc->servo.kind == SERVO_CMD_HOLD || pc->servo.ends_sweep)) {
        /*
         * HOLD, and a finger on the dial or CENTRE that ends a sweep, stop
         * a moving servo, so they do not wait either: a sweep being written
         * gives way at once, and one queued before them is dropped as
         * stale.  Each is a drive itself, so it carries the generation it
         * opens.
         */
        cmd.lets_go = atomic_fetch_add(&s_lets_go, 1u) + 1u;
        servo_link_ask_hold();
    } else if (pc->kind == PANEL_CMD_SUPPLY && pc->supply.off) {
        /* The supply's OFF, for the same reason as the disarm. */
        atomic_fetch_add(&s_supply_offs, 1u);
        atomic_store(&s_supply_off_request, true);
    }

    if (xQueueSend(s_cmd_q, pc, pdMS_TO_TICKS(5)) == pdTRUE) {
        return;
    }
    panel_cmd_t stale;
    if (xQueueReceive(s_cmd_q, &stale, 0) == pdTRUE
        && stale.kind == PANEL_CMD_SUPPLY && stale.supply.on) {
        /* An ON the control task will never see is one it will never take
         * either; counted here, so nothing waits on it for ever. */
        atomic_fetch_add(&s_supply_ons_taken, 1u);
    }
    if (xQueueSend(s_cmd_q, pc, 0) != pdTRUE) {
        ESP_LOGW(TAG, "control queue full; a command was lost");
    }
}

/*
 * The render loop's record of s_touch_q: what it has taken, and the losses it
 * found there, for the frame log.  Render loop only.
 */
static touch_seq_rx_t s_tq_rx = { .next = 1u };    /* touch_seq_rx_init() */
unsigned              s_tq_lost;

/*
 * Whether the supply's output is on, or an ON for it is queued and not yet
 * taken.  Read at the moment it is asked, from what the control task stores
 * when it switches, so an ON queued earlier in this frame counts.
 */
bool supply_live_or_coming(void)
{
    /* The counters before the flag.  The control task stores the flag
     * before it counts an ON taken, so read in this order there is no
     * moment at which both say nothing is coming while the output is on. */
    const bool coming = atomic_load(&s_supply_ons_taken)
                        != atomic_load(&s_supply_ons_sent);
    return coming || atomic_load(&s_supply_live);
}

/*
 * The supply screen's limits and set points, as the levels the control task
 * reads.  Every frame, from the render loop that owns the screen.
 */
void publish_supply_levels(void)
{
    const supply_limits_t lim = supply_screen_limits();
    atomic_store(&s_supply_vmax_mv, (unsigned)lroundf(lim.v_max * 1000.0f));
    atomic_store(&s_supply_imax_ma, (unsigned)lroundf(lim.i_max * 1000.0f));
    atomic_store(&s_supply_trip_ma, (unsigned)lroundf(lim.trip_i * 1000.0f));
    atomic_store(&s_supply_trip_mv, (unsigned)lroundf(lim.trip_v * 1000.0f));
    atomic_store(&s_supply_trip_ms, (unsigned)lroundf(lim.trip_s * 1000.0f));
    atomic_store(&s_supply_set_mv,
                 (unsigned)lroundf(supply_screen_set_v() * 1000.0f));
    atomic_store(&s_supply_set_ma,
                 (unsigned)lroundf(supply_screen_set_i() * 1000.0f));
}

/*
 * The servo test's lines, from the screen to the logger, as many as the
 * queue takes; the rest wait in the screen's outbox for the next frame.
 * Every frame, whichever screen is up, so a run ended by leaving SERVO still
 * writes its report.  And the files' number back to the screen, once the
 * logger has answered every OPEN sent.
 */
void test_lines_service(void)
{
    bool open_waits = false;     /* a new run's OPEN not yet queued */
    for (;;) {
        const char *text = NULL;
        const servo_test_out_t k = servo_screen_test_peek(&text);
        if (k == SERVO_TEST_OUT_NONE) {
            break;
        }
        test_line_t l;
        l.kind = (uint8_t)k;
        snprintf(l.text, sizeof(l.text), "%s", text);
        const bool open = k == SERVO_TEST_OUT_OPEN
                          || k == SERVO_TEST_OUT_OPEN_TXT;
        if (xQueueSend(s_test_q, &l, 0) != pdTRUE) {
            open_waits = open;
            break;
        }
        if (open) {
            atomic_fetch_add(&s_test_opens_sent, 1u);
        }
        if (k == SERVO_TEST_OUT_END) {
            ++s_test_ends_sent;
        }
        servo_screen_test_pop();
    }
    /* Not the last run's number for a run whose OPEN still waits.  The
     * counts are read before the number and the report state, which the
     * logger stores before it counts. */
    const unsigned sent = atomic_load(&s_test_opens_sent);
    const unsigned done = atomic_load(&s_test_opens_done);
    const unsigned ends = atomic_load(&s_test_ends_done);
    if (sent != 0u && !open_waits && done == sent) {
        servo_screen_test_files(atomic_load(&s_test_file),
                                atomic_load(&s_test_report));
    }
    /* No run starts until the logger has taken the last run's OPEN and
     * END, and the screen has their answer: one number and one report
     * state in flight at a time, each the run's that sent it. */
    servo_screen_test_logger_busy(open_waits || done != sent
                                  || ends != s_test_ends_sent);
}

/*
 * The rotary knob turns the slider of the bench screen on top: the throttle
 * on MOTOR & ESC, the horn on SERVO.
 *
 * The motion is taken at the start of the frame together with the screen it
 * was turned on and the router's navigation count, and applied at the end,
 * after the last touch drain and the tick.  A motion turned on another
 * screen, with the setting off, in a frame in which the router navigated at
 * all (a tap on a menu item navigates inside the drain, and a tap away and a
 * tap back lands on the same screen), in a frame that lost touch events, or
 * in a frame in which a finger owned the slider or the dial is dropped and
 * not applied later.  The knob's command is posted after the frame's flush
 * and goes with the next one.  If the next frame's first drain finds a touch
 * loss, the command is withdrawn before the loss flushes the screens
 * (knob_withdraw()), so a frame that lost touch never moves the output from
 * the knob.  The knob moves a value by how far it turned; it arms nothing,
 * and a disarm, a stop or leaving the screen act exactly as they do for the
 * touch slider.
 */
knob_turn_t knob_take(void)
{
    const bool on = settings_get_bool(SET_KNOB_EN);
    knob_task_set_enabled(on);
    const int steps = knob_task_take();
    motor_screen_knob_frame();
    servo_screen_knob_frame();
    return (knob_turn_t){ .steps = on ? steps : 0,
                          .route = ui_router_current(),
                          .navigations = ui_router_navigations() };
}

static void knob_withdraw(void)
{
    motor_screen_knob_cancel();
    servo_screen_knob_cancel();
}

void knob_apply(const knob_turn_t *turn, bool frame_lost)
{
    if (turn->steps == 0 || frame_lost
        || ui_router_navigations() != turn->navigations
        || ui_router_current() != turn->route) {
        return;
    }
    const float span =
        knob_span_fraction(turn->steps, settings_get_int(SET_KNOB_SCALE));
    switch (turn->route) {
    case SCREEN_MOTOR: motor_screen_knob(span); break;
    case SCREEN_SERVO: servo_screen_knob(span); break;
    default: break;
    }
}

/*
 * Stamped with what this loop knows of the touch stream as it queues the
 * command: the gestures it has dropped (s_loss_gen, which only this loop
 * writes) and the number of the last event it took.  The control task
 * compares both when it takes an arm; see arm_watch_take_ok().
 */
void flush_screen_commands(uint32_t stops_now)
{
    const uint32_t loss_gen = atomic_load(&s_loss_gen);
    const uint32_t consumed = s_tq_rx.next - 1u;
    motor_cmd_t mc;
    while (motor_screen_poll_cmd(&mc)) {
        panel_cmd_t pc = { .kind = PANEL_CMD_MOTOR, .motor = mc,
                           .stops = stops_now,
                           .lets_go = atomic_load(&s_lets_go),
                           .loss_gen = loss_gen,
                           .consumed_seq = consumed };
        send_cmd(&pc);
    }
    /* A stick run on MOTOR & ESC > PROGRAM moves the throttle and arms as
     * LIVE does, through the same commands and the same policy. */
    while (esc_stick_panel_poll_cmd(&mc)) {
        panel_cmd_t pc = { .kind = PANEL_CMD_MOTOR, .motor = mc,
                           .stops = stops_now,
                           .lets_go = atomic_load(&s_lets_go),
                           .loss_gen = loss_gen,
                           .consumed_seq = consumed };
        send_cmd(&pc);
    }
    servo_cmd_t sv;
    if (servo_screen_take(&sv)) {
        panel_cmd_t pc = { .kind = PANEL_CMD_SERVO, .servo = sv,
                           .stops = stops_now,
                           .lets_go = atomic_load(&s_lets_go),
                           .loss_gen = loss_gen,
                           .consumed_seq = consumed };
        send_cmd(&pc);
    }
    /* A programming port's command on the supply, onto the screen before
     * the screen's set points are published; see s_supply_adopt_mv. */
    const unsigned adopt = atomic_load(&s_supply_adopt_gen);
    if (adopt != atomic_load(&s_supply_adopt_done)) {
        supply_screen_adopt((float)atomic_load(&s_supply_adopt_mv) / 1000.0f,
                            (float)atomic_load(&s_supply_adopt_ma) / 1000.0f);
    }
    /* The limits and the set points first, as levels; see
     * s_supply_set_mv. */
    publish_supply_levels();
    atomic_store(&s_supply_adopt_done, adopt);
    supply_cmd_t sc;
    if (supply_screen_poll_cmd(&sc)) {
        if (sc.on) {
            atomic_fetch_add(&s_supply_ons_sent, 1u);
        }
        panel_cmd_t pc = { .kind = PANEL_CMD_SUPPLY, .supply = sc,
                           .stops = stops_now,
                           .supply_offs = atomic_load(&s_supply_offs),
                           .pdmini_edits = atomic_load(&s_pdmini_edits),
                           .loss_gen = loss_gen,
                           .consumed_seq = consumed };
        send_cmd(&pc);
    }
}

/*
 * The screens' record of the glass is stale: an event between the last one
 * they saw and the next never reached them.  Every gesture in progress is
 * dropped, which asks for nothing -- what letting go early already does --
 * except where abandoning is itself the failure (a disarm press; see
 * ui_router_cancel_gestures()).  What that posts goes now, not at the end of
 * the frame: a lost event is what a stalled renderer produces, and a DISARM
 * must not wait behind the render and the flip.
 *
 * The STOP marker goes too, unless a request is already raised.  The marker
 * says the router will latch a stop the control task already applied, and
 * the router's press of the band was just cancelled, so no request follows
 * to consume it; one left standing would swallow the next stop that
 * genuinely needs the backstop.  A request the router latched before the
 * loss will consume it as it should -- including one still in the router's
 * own latch, which this frame would hand over only later: it is handed over
 * here first, the same way, so the marker is kept for it.  The residual is
 * a request raised by the backstop between the load and the store, which
 * resolves to a stop applied twice, and that latches the same way one does.
 */
static void touch_stream_broke(uint32_t stops_now)
{
    /* A throttle or position the knob posted last frame and the flush below
     * would send is not sent: this frame lost touch. */
    knob_withdraw();
    ui_router_cancel_gestures();
    atomic_fetch_add(&s_loss_gen, 1u);
    if (ui_router_take_stop()) {
        atomic_store(&s_stop_request, true);
    }
    if (!atomic_load(&s_stop_request)) {
        atomic_store(&s_stop_counted, false);
    }
    flush_screen_commands(stops_now);
}

/*
 * Hand the screens what the control task saw of the panel.  Returns whether
 * the stream was found broken.  Render loop only.
 */
bool drain_touch(uint32_t stops_now)
{
    bool lost = false;
    /*
     * What the control task saw of the panel, numbered.  The last number
     * it published is read before the drain: every item up to it had
     * been offered by then, so one the drain does not deliver was
     * dropped (touch_seq_tail()).
     *
     * A loss is answered before the next event is dispatched.  The
     * events after it were captured around the one that went missing,
     * and dispatching them into a screen whose record of the glass is
     * stale is what the cancellation exists to prevent: a queued movement
     * on a reused track id commands a servo position, an orphan release
     * applies a binding change, and cancelling afterwards cannot take
     * either back.  A notice from the control task -- a gap in the
     * driver's stream -- arrives in the same order, ahead of the events
     * that followed that gap.
     */
    const uint32_t tq_published = atomic_load(&s_tq_published);
    touch_item_t item;
    while (xQueueReceive(s_touch_q, &item, 0) == pdTRUE) {
        const uint32_t missed = touch_seq_take(&s_tq_rx, item.seq);
        s_tq_lost += missed;
        if (missed > 0u || item.lost) {
            touch_stream_broke(stops_now);
            lost = true;
        }
        if (item.lost) {
            continue;
        }
        const ui_screen_id_t before = ui_router_current();
        ui_router_event(&item.evt);
        /*
         * A navigation, and only a navigation, is taken out before the
         * next event is dispatched.
         *
         * Leaving a bench screen records a disarm, and entering a screen
         * runs its enter() there and then -- the log viewer's reads the
         * card's whole root directory.  Two taps in one drain, HOME and
         * then LOGS, would otherwise leave the disarm sitting in the
         * screen while the walk ran, and the output stays live for as
         * long as that takes.
         *
         * Not after every event.  The servo screen holds one pending
         * command and lets the next overwrite it, so a drag's queued
         * MOVEs collapse to where the finger is now.  Taking each one out
         * as it arrives turns that into a queue of positions the finger
         * has already left, and the horn follows them one link exchange
         * at a time.  A navigation is the one thing that cannot be
         * coalesced away, and nothing else here needs to jump the queue.
         *
         * STOP is unaffected either way: the control task hit-tests its
         * band itself.
         */
        if (ui_router_current() != before) {
            flush_screen_commands(stops_now);
        }
    }
    {
        const uint32_t tail = touch_seq_tail(&s_tq_rx, tq_published);
        if (tail > 0u) {
            s_tq_lost += tail;
            touch_stream_broke(stops_now);
            lost = true;
        }
    }    return lost;
}

/*
 * The outputs screen's choice, on its way to the control task.
 *
 * The screen runs on app_main and the link belongs to the control task, so
 * this queues rather than writes.  Everything the far end thinks of the
 * choice comes back as a word the control task leaves behind.
 */
void outputs_apply(const outbind_t *b)
{
    if (b == NULL) {
        return;
    }
    panel_cmd_t pc = { .kind = PANEL_CMD_OUTPUTS, .bind = *b };
    send_cmd(&pc);
}
