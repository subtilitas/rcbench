/**
 * @file
 * @brief The programmable supply, on the control task: the PD (USB Power
 *        Delivery) mini over the IOmcu's SUPPLY page, or the panel's model,
 *        its steps at the sample cadence, its run's rows for the log, and its
 *        page.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "arming.h"
#include "error_sites.h"
#include "esc_profile.h"
#include "esc_sim.h"
#include "link_host.h"
#include "link_pages.h"
#include "outputs.h"
#include "panel.h"
#include "servo_source.h"
#include "supply.h"
#include "supply_link.h"
#include "supply_screen.h"
#include "supply_page.h"
#include "touch_loss.h"
#include "ui_text.h"

/* ------------------------------------------------------------- the supply */

/*
 * The programmable supply.
 *
 * The PD mini, driven by the IOmcu over the SUPPLY page (protocol 4.3)
 * when one answers that speaks it and SETUP INTERFACES enables the module;
 * the panel's model, supply_sim_t, otherwise, so the screen, the log and the
 * rules here are the same either way.  s_supply_on is what the operator
 * asked; the readings come from the source in use.  Control task only,
 * beside the bench, because every stop is seen here and a stop switches the
 * output off whatever screen is up.
 *
 * s_supply_ms is when the totals last counted.  s_supply_fresh says the next
 * reading starts the run's extremes: the output has just come on, and the
 * reading in s_supply is still the one taken with it off.
 */
supply_sim_t          s_supply_sim;
supply_link_t         s_supply_link;
prog_supply_link_t    s_psup_link;
atomic_bool           s_psup_owns;
atomic_bool           s_psup_live;
/*
 * The supply command a programming port left on the SUPPLY page, for the
 * SUPPLY screen's set points: the values, and a count the control task
 * raises with each.  The render loop puts them on the screen and stores the
 * count it served; until then the screen's older set points are not sent.
 */
atomic_uint        s_supply_adopt_mv;
atomic_uint        s_supply_adopt_ma;
atomic_uint        s_supply_adopt_gen;
atomic_uint        s_supply_adopt_done;
/**
 * @brief Whether the last read of the SUPPLY page showed no programming port
 *        holding it and the output off (supply_link_port_free_off()): when
 *        the screen gives the operator's set points back.  Stored before the
 *        adopt count it is read with.
 */
atomic_bool        s_supply_free_off;
/*
 * The ESC a stick run programs, when the supply is the panel's model.
 *
 * ESC STICK publishes the profile of a run under way, and how
 * many runs it has started, from the render loop; the control task makes
 * the model's current the simulated ESC's, so the run counts beeps from the
 * same samples the PD mini would give it.  The ESC sees the throttle the
 * control page carries -- the raw command, as a pin bound on the
 * IOmcu does -- and no signal while the bank is disarmed.
 */
atomic_uintptr_t        s_escsim_profile;
atomic_uint             s_escsim_runs;
static esc_sim_t        s_escsim;          /* control task only */
static unsigned         s_escsim_runs_seen;
static bool             s_escsim_live;
/* The IOmcu that answered speaks protocol 4.3: it has the page. */
bool s_supply_page;
/* And speaks 4.4: AUTO baud and the BAUD_FOUND register. */
bool s_supply_auto;
/* The PD mini, not the model, is the supply; see supply_real_follow(). */
bool                  s_supply_is_real;
static bool           s_supply_on;
supply_state_t        s_supply;
uint32_t              s_supply_ms;
static bool           s_supply_fresh;
/* The stop count the supply has answered; see supply_follow_stops(). */
uint32_t              s_supply_stops_served;
/* The trips' time over their thresholds; see supply_trip_step(). */
static supply_trip_t  s_supply_trip;
/* When the supply was last stepped, on its own cadence; see supply_pump(). */
uint32_t              s_supply_step_ms;
/*
 * The ON in flight, watched as an arm is (s_arm_watch): a hold that completed
 * on a contact whose events went missing is an ON the operator may not have
 * made, and the render side can cancel only an ON it has not yet handed
 * over.  s_supply_gen numbers the ONs applied; the snapshot carries it.
 */
static arm_watch_t    s_supply_watch;
uint32_t              s_supply_gen;
/** @brief The PROG 5V box's ON in flight, watched the same way (s_psup_gen,
 *         s_psup_ack). */
static arm_watch_t    s_psup_watch;

/*
 * The interval since the last step, up to now, on the reading that held
 * through it: counted into the totals and, while a supply run is being
 * logged, written as the run's last row, so the log ends on the totals the
 * screen shows.  For the moments a supply run ends between two steps -- the
 * output switched off, or the bench taking the log over.
 */
void supply_log_tail(void)
{
    const uint32_t t = now_ms();
    float dt = (float)(uint32_t)(t - s_supply_ms) / 1000.0f;
    if (dt > BENCH_TOTALS_MAX_STEP_S) {
        dt = BENCH_TOTALS_MAX_STEP_S;
    }
    supply_count_totals(&s_supply, dt);
    s_supply_ms = t;
    /* No row while a servo run is under way: a limit run switches the bench
     * to SAFE for each write, and its report is the record.  The log's clock
     * runs on through it, as the totals do. */
    if (s_log_kind == LOG_RUN_SUPPLY) {
        s_log_t += dt;
        if (!atomic_load(&s_servo_testing)) {
            log_row_t row = { .kind = LOG_RUN_SUPPLY, .t_s = s_log_t };
            row.u.supply = s_supply;
            log_post(&row);
        }
    }
}

/* The set points the screen holds, applied when they change; see
 * s_supply_set_mv.  Compared as stored, in mV and mA, because the supply
 * snaps what it is given and a snapped value need not equal it. */
static unsigned s_supply_set_mv_applied;
static unsigned s_supply_set_ma_applied;

void supply_switch(bool on)
{
    if (s_supply_on == on) {
        return;
    }
    if (!on) {
        /* The run's last interval, before the output is marked off: an OFF
         * taken inside an exchange's wait can end a second-long one. */
        supply_log_tail();
        arm_watch_end(&s_supply_watch);
    }
    s_supply_on = on;
    if (s_supply_is_real) {
        supply_link_command(&s_supply_link, on,
                            (uint16_t)s_supply_set_mv_applied,
                            (uint16_t)s_supply_set_ma_applied);
    } else {
        supply_sim_output(&s_supply_sim, on);
    }
    s_supply.output = on;
    atomic_store(&s_supply_live, on);
    if (on) {
        /* A run starts with nothing counted and nothing tripped. */
        supply_reset_totals(&s_supply);
        supply_trip_reset(&s_supply_trip);
        s_supply.trip  = (uint8_t)SUPPLY_TRIP_NONE;
        s_supply_ms    = now_ms();
        s_supply_fresh = true;
        /* And the step clock: the run's first step, and its first row's
         * time, count from the switch-on and not from the last step taken
         * with the output off. */
        s_supply_step_ms = now_ms();
    }
}

/**
 * @brief The bench as an error finds it (error_sites.h): ARMED or a switch
 *        to ARMED waiting, the output and who holds it, and the IOmcu.
 *
 * A port holds the supply when the SUPPLY page's last read says so
 * (supply_link_port_holds()), not when a programmer has a port
 * (s_prog_taken): on the rbmod carrier the ports run from PROG_SUPPLY, and
 * the PD mini is no port's.  The hold counts for an IOmcu older than 4.31
 * only; from 4.31 an error writes the OFF whoever holds the supply and the
 * IOmcu holds it for a write.
 *
 * @return The bench now.
 */
static error_bench_t error_bench(void)
{
    const error_bench_t b = {
        .live        = arming_link_needed(&s_arm, outputs_armed(&s_out)),
        .supply_on   = s_supply_on,
        .port_holds  = supply_link_port_holds(&s_supply_link,
                                              s_supply_is_real),
        .supply_real = s_supply_is_real,
        .supply_page = s_supply_page,
        .far_minor   = s_far_minor,
        /* The page served and the switch fitted, as read or as the board
         * says before the first read. */
        .prog_supply = prog_supply_link_owns(&s_psup_link,
                                             outbind_roles(s_board) != NULL),
    };
    return b;
}

/**
 * @brief What an error asks (error_sites.h), done from here, in order.
 *
 * The disarm is a request, the way a screen's DISARM is one, because this
 * runs inside an exchange's wait as well.  The request withholds the
 * heartbeat while it stands, so the IOmcu fails safe within
 * HEARTBEAT_MAX_GAP_MS (150ms) whatever this task waits for, and
 * service_arming() lets go of the rest when the loop is free.  To an
 * IOmcu of 4.31 the OFF is written whatever this end reads of the output
 * (supply_link_cut()): a port in a power cycle has it off and is about to
 * switch it on, and the page refuses that ON after the panel's OFF.
 *
 * @param a The act.
 */
static void error_act(error_act_t a)
{
    if (a.off_first) {
        supply_switch(false);
    }
    if (a.disarm) {
        atomic_store(&s_disarm_request, true);
    }
    if (a.supply_off) {
        supply_switch(false);
    }
    if (a.cut) {
        supply_link_cut(&s_supply_link);
    }
    if (a.prog_off) {
        prog_supply_link_cut(&s_psup_link);
    }
}

/*
 * The sites below are kept out of line: the bench's copy and the act stay
 * off the frame of supply_pump(), which the control task's deepest chain
 * runs through (docs/Performance.md, "Stacks").
 */
__attribute__((noinline)) void bench_error_stop(void)
{
    const error_bench_t b = error_bench();
    error_act(error_sites_stop(&b));
}

/** @brief The panel's own supply trip: the output off first, whoever holds
 *         it, then the rule (error_sites_cut()); control task only. */
static __attribute__((noinline)) void supply_error_cut(void)
{
    const error_bench_t b = error_bench();
    error_act(error_sites_cut(&b));
}

/** @brief The reading fault of the last step, as error_sites_reading()
 *         keeps it; control task only. */
static error_reading_t s_reading_told;

/**
 * @brief The supply's reading with its output on: silent, or the PD mini
 *        answering ONLINE with a reading count that has stood for
 *        SUPPLY_LINK_BLIND_MS, readings marked not valid that no trip acts
 *        on.  Either is an error (error_sites_reading()).
 * @return The fault acted on, for the band's alert.
 */
static __attribute__((noinline)) error_reading_t supply_reading_fault(void)
{
    const error_bench_t b = error_bench();
    error_act_t act;
    const error_reading_t fault =
        error_sites_reading(&b, s_supply.online,
                            supply_link_blind(&s_supply_link, now_ms()),
                            &s_reading_told, &act);
    error_act(act);
    return fault;
}

/** @brief The card faults the logger counted (s_card_faults), each taken
 *         as an error once; control task only. */
static error_watch_t s_card_watch;

__attribute__((noinline)) void card_faults_follow(void)
{
    const error_bench_t b = error_bench();
    error_act(error_sites_follow(&s_card_watch, atomic_load(&s_card_faults),
                                 &b));
}

/** @brief The programmers' session faults (prog_faults()), each taken as an
 *         error once; control task only. */
static error_watch_t s_prog_watch;

__attribute__((noinline)) void prog_faults_follow(void)
{
    const error_bench_t b = error_bench();
    error_act(error_sites_follow(&s_prog_watch, prog_faults(), &b));
}

/* The operator's limits as the render side last stored them. */
static supply_limits_t supply_limits_now(void)
{
    const supply_limits_t l = {
        (float)atomic_load(&s_supply_vmax_mv) / 1000.0f,
        (float)atomic_load(&s_supply_imax_ma) / 1000.0f,
        (float)atomic_load(&s_supply_trip_ma) / 1000.0f,
        (float)atomic_load(&s_supply_trip_mv) / 1000.0f,
        (float)atomic_load(&s_supply_trip_ms) / 1000.0f,
    };
    return l;
}

void supply_follow_set(void)
{
    unsigned mv = atomic_load(&s_supply_set_mv);
    unsigned ma = atomic_load(&s_supply_set_ma);
    /* Under the caps, whatever the screen sent: the screen keeps its set
     * points under them already, and this is what the supply obeys. */
    const unsigned vmax = atomic_load(&s_supply_vmax_mv);
    const unsigned imax = atomic_load(&s_supply_imax_ma);
    /* A programmer has a port: the set points are the port's, and neither
     * the screen's nor a limit that moved is written over them.  The
     * session counts from the panel's own command, before the SUPPLY page
     * says the port holds the supply; the IOmcu refuses them while it
     * holds it (supply_page.h). */
    if (s_prog_taken) {
        return;
    }
    /* A programming port's command the screen has not taken yet: what the
     * screen holds is older than the page. */
    if (atomic_load(&s_supply_adopt_gen)
        != atomic_load(&s_supply_adopt_done)) {
        return;
    }
    if (mv > vmax) {
        mv = vmax;
    }
    if (ma > imax) {
        ma = imax;
    }
    if (mv == s_supply_set_mv_applied && ma == s_supply_set_ma_applied) {
        return;
    }
    s_supply_set_mv_applied = mv;
    s_supply_set_ma_applied = ma;
    /* The model takes them whichever supply is in use, so a return to it
     * starts where the screen is. */
    supply_sim_set(&s_supply_sim, (float)mv / 1000.0f, (float)ma / 1000.0f);
    if (s_supply_is_real) {
        /* Shown as the module reads them back, once it has. */
        supply_link_command(&s_supply_link, s_supply_on, (uint16_t)mv,
                            (uint16_t)ma);
    } else {
        s_supply.set_v = s_supply_sim.set_v;
        s_supply.set_i = s_supply_sim.set_i;
    }
}

/*
 * What the supply screen asked for.  An ON completes a hold, so it goes the
 * way an arm does: not past a stop, an OFF, a change to the PD mini's
 * wiring or a touch loss that came after the screen posted it, and watched after it is taken until the render side
 * has seen the output on (supply_watch_service()).  The operator repeats the
 * hold.
 */
void apply_supply_cmd(const panel_cmd_t *pc)
{
    const supply_cmd_t *c = &pc->supply;
    if (c->reset) {
        supply_reset_peaks(&s_supply);
    }
    if (c->module_reset && s_supply_is_real) {
        supply_switch(false);
        /* RESET is a 4.4 register: an older IOmcu would refuse it
         * unseen, so the operator is told instead. */
        if (s_supply_auto) {
            supply_link_reset(&s_supply_link);
        } else {
            control_alert(TR(ALERT_PDMINI_OLD));
        }
    }
    if (c->off) {
        supply_switch(false);
    } else if (c->on) {
        /* The pump first, so a STOP or a loss still in the driver's queue
         * is counted before the ON is judged against them. */
        control_pump();
        if (!s_prog_taken
            && pc->stops == arming_stop_count(&s_arm)
            && pc->supply_offs == atomic_load(&s_supply_offs)
            && pc->pdmini_edits == atomic_load(&s_pdmini_edits)
            && arm_watch_take_ok(pc->loss_gen, atomic_load(&s_loss_gen),
                                 s_lost_notice_seq, pc->consumed_seq)) {
            /* At the set points stored before this ON was queued. */
            supply_follow_set();
            supply_switch(true);
            ++s_supply_gen;
            arm_watch_begin(&s_supply_watch, pc->loss_gen,
                            atomic_load(&s_drv_gaps), s_supply_gen);
        }
    }
    if (c->on) {
        /* Taken, whatever became of it, and only now: an applied ON has
         * stored s_supply_live first, so supply_live_or_coming() never
         * reads the gap between the two. */
        atomic_fetch_add(&s_supply_ons_taken, 1u);
    }
}

void apply_prog5v_cmd(const panel_cmd_t *pc)
{
    const prog5v_cmd_t *c = &pc->prog5v;
    if (c->off) {
        prog_supply_link_ask_off(&s_psup_link);
        arm_watch_end(&s_psup_watch);
        return;
    }
    if (!c->on) {
        return;
    }
    /* The pump first, so a STOP or a loss still in the driver's queue is
     * counted before the ON is judged against them; then the PD mini's
     * OUTPUT ON rules. */
    control_pump();
    if (!s_prog_taken
        && pc->stops == arming_stop_count(&s_arm)
        && pc->prog5v_offs == atomic_load(&s_prog5v_offs)
        && arm_watch_take_ok(pc->loss_gen, atomic_load(&s_loss_gen),
                             s_lost_notice_seq, pc->consumed_seq)
        && prog_supply_link_ask_on(&s_psup_link)) {
        const uint32_t gen = atomic_load(&s_psup_gen) + 1u;
        atomic_store(&s_psup_gen, gen);
        arm_watch_begin(&s_psup_watch, pc->loss_gen,
                        atomic_load(&s_drv_gaps), gen);
        /* The ON owed is live for the gates from here (on_owed). */
        prog_supply_publish();
    }
    /* Done with it, taken or refused, and only now: s_psup_live is stored
     * first, so supplies_live_or_coming() never reads a gap. */
    atomic_fetch_add(&s_psup_ons_taken, 1u);
}

void prog5v_off_service(void)
{
    if (atomic_exchange(&s_prog5v_off_request, false)) {
        prog_supply_link_ask_off(&s_psup_link);
        arm_watch_end(&s_psup_watch);
    }
}

void prog_supply_watch_service(void)
{
    if (prog_supply_link_state(&s_psup_link) != PROG_SUPPLY_ON
        && !s_psup_link.on_owed) {
        arm_watch_end(&s_psup_watch);
        return;
    }
    if (arm_watch_lost(&s_psup_watch, atomic_load(&s_loss_gen),
                       atomic_load(&s_drv_gaps), atomic_load(&s_psup_ack))) {
        bench_error_stop();
        control_alert(TR(ALERT_P5_TOUCH));
    }
}

/* The supply as it is now, for the screen; the oldest sample goes when the
 * queue is full, as the bench's do. */
static void supply_queue_sample(void)
{
    supply_item_t item = {
        .st      = s_supply,
        .source  = servo_source_id(&s_servo_source),
        .changes = servo_source_changes(&s_servo_source),
    };
    item.dropped = servo_source_dropped(&s_servo_source, &item.dropped_at);
    if (xQueueSend(s_supply_q, &item, 0) != pdTRUE) {
        supply_item_t stale;
        (void)xQueueReceive(s_supply_q, &stale, 0);
        (void)xQueueSend(s_supply_q, &item, 0);
    }
}

/*
 * Which supply the screen drives: the PD mini whenever SETUP INTERFACES
 * enables it -- not answering while no IOmcu that speaks the SUPPLY
 * page does -- and the model otherwise.  A change switches the output off.
 * No exchange here: called from the step, inside the pump.  Returns the
 * wiring word it followed, so a write chosen after it is chosen for that
 * word and not for one stored since.
 */
static unsigned supply_real_follow(void)
{
    /*
     * Any change to the PD mini's wiring, not only enabling it: the page
     * takes new pins only with the output off, so a change under a live
     * output would otherwise wait for an OFF nobody asks for.
     */
    static supply_link_follow_t seen;
    /* The count before the word: an edit that has counted and not yet
     * stored its word is followed as a change, so it cannot pass as none. */
    const unsigned edits = atomic_load(&s_pdmini_edits);
    const unsigned wiring = atomic_load(&s_pdmini_wiring);
    const bool rewired = supply_link_wiring_moved(&seen, edits, wiring);
    const bool real = (wiring & (1u << 24)) != 0u;
    if (real == s_supply_is_real) {
        if (rewired) {
            atomic_store(&s_supply_vin_mv, 0u);   /* other pins, perhaps */
            atomic_store(&s_supply_baud, 0u);
        }
        if (rewired && real && s_supply_on) {
            supply_switch(false);
            control_alert(TR(ALERT_PDMINI_WIRING));
        }
        return wiring;
    }
    if (s_supply_on) {
        supply_switch(false);
        control_alert(TR(ALERT_SUPPLY_CHANGED));
    }
    s_supply_is_real = real;
    atomic_store(&s_supply_vin_mv, 0u);   /* another module, perhaps */
    atomic_store(&s_supply_baud, 0u);
    supply_link_command(&s_supply_link, false,
                        (uint16_t)s_supply_set_mv_applied,
                        (uint16_t)s_supply_set_ma_applied);
    atomic_store(&s_supply_real, real);
    return wiring;
}

/* The model's load is the simulated ESC while a stick run wants one. */
static void escsim_load(void)
{
    const esc_profile_t *p =
        (const esc_profile_t *)atomic_load(&s_escsim_profile);
    if (p == NULL) {
        s_escsim_live = false;
        return;
    }
    const unsigned runs = atomic_load(&s_escsim_runs);
    if (!s_escsim_live || runs != s_escsim_runs_seen || s_escsim.p != p) {
        esc_sim_init(&s_escsim, p, NULL);
        s_escsim_runs_seen = runs;
        s_escsim_live = true;
    }
    const float pct = outputs_role_armed(&s_out, OUT_ROLE_THROTTLE)
                          ? (float)s_throttle_hundredths / 100.0f : -1.0f;
    const int32_t ma = esc_sim_step(&s_escsim, now_ms(), s_supply_on, pct);
    if (s_supply.output) {
        s_supply.i = (float)ma / 1000.0f;
        s_supply.p = s_supply.v * s_supply.i;
        s_supply.mode = SUPPLY_MODE_CV;
    }
}

/*
 * One step of the supply at the sample cadence: its readings, the run's
 * extremes and totals, and a sample for the screen.
 */
static void supply_step(float step_s)
{
    /* A stop counted outside the pump -- the far end's refusal, or STOP
     * from the queue -- is answered before this reading is taken. */
    supply_follow_stops();
    (void)supply_real_follow();
    if (s_supply_is_real) {
        supply_link_state(&s_supply_link, now_ms(), &s_supply);
        s_supply.output = s_supply_on;
    } else {
        /* Every step of the model is a reading, taken now. */
        supply_sim_step(&s_supply_sim, step_s, &s_supply);
        s_supply.taken_ms = now_ms();   /* a reading every step */
        s_supply.stamped  = true;
        escsim_load();
    }
    /*
     * A supply that stops answering takes its output with it, switched off
     * here so that it does not come back on by itself when the supply
     * answers again.  The model always answers.  A PD mini that answers
     * and reads ONLINE, but whose reading count has stood for
     * SUPPLY_LINK_BLIND_MS, gives readings marked not valid, which no trip
     * acts on: off, as for a supply gone.  Either is left on for a
     * programming port that keeps the supply (error_sites_keeps()).
     */
    const error_reading_t fault = supply_reading_fault();
    if (fault == ERROR_READING_SILENT) {
        control_alert(TR(ALERT_SUPPLY_SILENT));
    } else if (fault == ERROR_READING_BLIND) {
        control_alert(TR(ALERT_SUPPLY_NO_READING));
    }
    if (s_supply.output) {
        if (s_supply_fresh) {
            supply_reset_peaks(&s_supply);
            s_supply_fresh = false;
        } else {
            supply_track_peaks(&s_supply);
        }
    }
    /* The operator's trips, on this reading.  The output goes off and the
     * trip stays on the MODE card until the output is switched on again. */
    const supply_limits_t lim = supply_limits_now();
    const supply_trip_kind_t trip =
        supply_trip_step(&s_supply_trip, &lim, &s_supply, step_s);
    if (trip != SUPPLY_TRIP_NONE) {
        /* The reading that tripped, as it was taken -- on -- so the plot
         * keeps it; the sample after the switch-off ends the run. */
        supply_queue_sample();
        supply_error_cut();
        s_supply.trip = (uint8_t)trip;
        /* In integers: this can run inside an exchange's wait, deep on the
         * control task's stack, and a float conversion is the deepest thing
         * snprintf does. */
        /* The threshold the trip was judged against, from the same
         * snapshot, not one an edit may have stored since. */
        const unsigned milli = (unsigned)lroundf(
            ((trip == SUPPLY_TRIP_CURRENT) ? lim.trip_i : lim.trip_v)
            * 1000.0f);
        char line[ALERT_MAX];
        snprintf(line, sizeof(line), TR(ALERT_SUPPLY_TRIP),
                 milli / 1000u, (milli % 1000u) / 10u,
                 (trip == SUPPLY_TRIP_CURRENT) ? "A" : "V");
        control_alert(line);
    }
    /* Over the time that passed, measured, as the bench's totals are. */
    const uint32_t t = now_ms();
    supply_count_totals(&s_supply, (float)(uint32_t)(t - s_supply_ms) / 1000.0f);
    s_supply_ms = t;
    supply_queue_sample();
}

/*
 * The ON in flight, against the touch stream: the arm watch's rules on the
 * supply's output.  Until the render side has acknowledged this ON from a
 * frame that began with the output on and found the stream whole, a loss of
 * either kind is an error: the output off again, unless a programming port
 * has taken the supply since and keeps it (error_sites_keeps()).
 */
static void supply_watch_service(void)
{
    if (!s_supply.output) {
        arm_watch_end(&s_supply_watch);
        return;
    }
    if (arm_watch_lost(&s_supply_watch, atomic_load(&s_loss_gen),
                       atomic_load(&s_drv_gaps), atomic_load(&s_supply_ack))) {
        bench_error_stop();
        control_alert(TR(ALERT_TOUCH_SWITCH_ON));
    }
}

/*
 * The supply, from control_pump(): at the top of every pass and inside every
 * exchange's wait, which can last LINK_HOST_TIMEOUT_MS (1000ms) a time.  A
 * stop, an OFF and a touch loss cut the output here, and the supply is
 * stepped, logged and published every 1/PANEL_SAMPLE_HZ whatever the link is
 * doing -- the supply is the panel's own and an unanswered IOmcu is
 * no reason for its plot or its log to thin out.
 */
void supply_pump(void)
{
    /* The cuts first -- a stop, then a lost ON -- and only then the set
     * points, so a level stored after a touch loss never reaches an output
     * that loss is about to switch off. */
    supply_follow_stops();
    supply_watch_service();
    prog_supply_watch_service();
    card_faults_follow();
    prog_faults_follow();
    supply_service();

    const uint32_t since = (uint32_t)(now_ms() - s_supply_step_ms);
    if (since < (uint32_t)(1000.0f / PANEL_SAMPLE_HZ)) {
        return;
    }
    s_supply_step_ms = now_ms();
    float step_s = (float)since / 1000.0f;
    if (step_s > BENCH_TOTALS_MAX_STEP_S) {
        step_s = BENCH_TOTALS_MAX_STEP_S;
    }
    /*
     * The run's edges before the step, so a bench taking the log over ends
     * the supply's file on the interval up to now, and this step's row is
     * the new run's.  Never the end of a bench run: that is the outer
     * loop's, after the pass's bench sample is logged, as it always was.
     */
    if (!log_run_armed(s_log_kind)) {
        log_follow_runs();
    }
    supply_step(step_s);

    /* Not after a trip or a lost supply in this step: the switch-off wrote
     * the run's last row already, and the run ends at the next edge.  No
     * row while a servo run is under way, and the log's clock runs on
     * through it, as the totals do. */
    if (s_log_kind == LOG_RUN_SUPPLY && s_supply.output) {
        s_log_t += step_s;
        if (!atomic_load(&s_servo_testing)) {
            log_row_t row = { .kind = LOG_RUN_SUPPLY, .t_s = s_log_t };
            row.u.supply = s_supply;
            log_post(&row);
        }
    }

    snap_lock();
    s_snap.supply     = s_supply;
    s_snap.supply_gen = s_supply_gen;
    snap_unlock();
}

/**
 * @brief A programming port's command the link took from the page
 *        (supply_link_adopted()): this end's copy follows it, so the next
 *        command written is the port's and not the one from before it, and
 *        the SUPPLY screen shows the set points in force.  Control task
 *        only.
 */
static void supply_adopt(void)
{
    bool     on;
    uint16_t mv, ma;
    if (supply_link_adopted(&s_supply_link, &on, &mv, &ma)) {
        s_supply_set_mv_applied = mv;
        s_supply_set_ma_applied = ma;
        atomic_store(&s_supply_adopt_mv, mv);
        atomic_store(&s_supply_adopt_ma, ma);
        atomic_fetch_add(&s_supply_adopt_gen, 1u);
        supply_switch(on);
    }
}

/**
 * @brief A SUPPLY page event that ends the panel's ON: the output off here,
 *        or, for an ON refused while a programming port keeps the supply
 *        (error_sites_page_keeps()), the page's command left to the port
 *        with no OFF written over it (supply_link_keep()).  The error's
 *        own act follows (bench_error_stop()).  Control task only.
 *
 * @param ev The page's events, SUPPLY_LINK_EV_*.
 */
static __attribute__((noinline)) void supply_page_off(uint16_t ev)
{
    const error_bench_t b = error_bench();
    const bool far_off = (ev & (SUPPLY_LINK_EV_ON_LOST | SUPPLY_LINK_EV_TRIPPED
                                | SUPPLY_LINK_EV_SAGGED)) != 0u;
    if (error_sites_page_keeps(&b, far_off)
        && supply_link_keep(&s_supply_link)) {
        supply_adopt();
    } else {
        supply_switch(false);
    }
}

/* The last alert posted for a refused wiring, kept until the page holds
 * the wiring asked (supply_link_event_stands()): its number (0 none) and
 * its event.  Control task only. */
static uint32_t s_wire_alert_gen;
static uint16_t s_wire_alert_ev;

/*
 * What the SUPPLY page said that the operator is told, and an ON it refused
 * or let go switched off here too (supply_page_off()).  An alert for a
 * refused wiring is taken back once the page holds the wiring asked: pins
 * stepped back to ones it takes.
 */
static void supply_link_alerts(void)
{
    if (s_wire_alert_gen != 0u
        && !supply_link_event_stands(&s_supply_link, s_wire_alert_ev)) {
        control_alert_withdraw(s_wire_alert_gen);
        s_wire_alert_gen = 0u;
    }
    const uint16_t ev = supply_link_events(&s_supply_link);
    if ((ev & (SUPPLY_LINK_EV_ON_REFUSED | SUPPLY_LINK_EV_ON_LOST
               | SUPPLY_LINK_EV_TRIPPED | SUPPLY_LINK_EV_SAGGED)) != 0u) {
        supply_page_off(ev);
    }
    /* An ON refused in the IOmcu's failsafe names it and the way out: only
     * the CLEAR an ARM writes lifts it (supply_link_on_ended()). */
    const supply_link_say_t say = supply_link_on_ended(ev, far_end_faults());
    if (say != SUPPLY_LINK_SAY_NONE) {
        control_alert((say == SUPPLY_LINK_SAY_TRIPPED)
                          ? TR(ALERT_PDMINI_OFF)
                      : (say == SUPPLY_LINK_SAY_LOST)
                          ? TR(ALERT_SUPPLY_OFF_REMOTE)
                      : (say == SUPPLY_LINK_SAY_FAILSAFE)
                          ? TR(ALERT_SUPPLY_REFUSED_FAILSAFE)
                          : TR(ALERT_SUPPLY_REFUSED_ON));
    }
    if ((ev & SUPPLY_LINK_EV_SAGGED) != 0u) {
        /* Switched off at the IOmcu for an input under the set point
         * and the headroom: the input and the set point it was read with. */
        uint16_t vin = 0u;
        uint16_t set = 0u;
        supply_link_sag(&s_supply_link, &vin, &set);
        char line[ALERT_MAX];
        snprintf(line, sizeof(line), TR(ALERT_PDMINI_SAG),
                 vin / 1000u, (vin % 1000u) / 10u,
                 set / 1000u, (set % 1000u) / 10u);
        control_alert(line);
    }
    if ((ev & SUPPLY_LINK_EV_WIRING_REFUSED) != 0u) {
        s_wire_alert_gen = control_alert_numbered(TR(ALERT_PDMINI_PINS));
        s_wire_alert_ev  = SUPPLY_LINK_EV_WIRING_REFUSED;
    }
    if ((ev & SUPPLY_LINK_EV_WIRING_LIVE) != 0u) {
        s_wire_alert_gen = control_alert_numbered(
            TR(ALERT_PDMINI_WIRING_LIVE));
        s_wire_alert_ev  = SUPPLY_LINK_EV_WIRING_LIVE;
    }
    if ((ev & SUPPLY_LINK_EV_STUCK) != 0u) {
        control_alert(TR(ALERT_PDMINI_SWITCH));
    }
    if ((ev & SUPPLY_LINK_EV_SET_STUCK) != 0u) {
        control_alert(TR(ALERT_PDMINI_SET));
    }
    /* And every error of the supply: the output off and the bench SAFE. */
    if (supply_link_ev_error(ev)) {
        bench_error_stop();
    }
    /* A rate AUTO found is not a fault: it is in the SUPPLY screen's
     * header, not in the alert band, which stays until another alert
     * replaces it. */
}

/*
 * The SUPPLY page: what is owed written -- an OFF first -- and the page read
 * every SUPPLY_LINK_READ_MS.  From poll_bench(), outside the pump: the pump
 * runs inside an exchange and switches the supply by asking
 * (supply_switch()), which this pays.  An IOmcu without the page is
 * left alone, and the PD mini reads as not answering.
 */
void supply_link_service(void)
{
    if (!s_supply_page) {
        return;
    }
    supply_wiring_t w = { 0 };
    for (int k = 0; k < 3; ++k) {
        /*
         * An edit on SETUP is followed straight before each write, and the
         * write chosen after it, with no exchange between: an ON taken
         * before the edit is switched off before it can reach the page with
         * the old wiring.  The exchanges of the writes before this one run
         * the pump and take milliseconds; the render core publishes edits
         * throughout.  An edit landing between this and the frame leaving
         * comes after the ON as the page sees it, and the next pass writes
         * the OFF, as for an edit made while the output is on.
         */
        w = wiring_of(supply_real_follow());
        if (w.baud >= SUPPLY_LINK_BAUD_AUTO && !s_supply_auto) {
            w.baud = 1u;   /* a 4.3 IOmcu has no AUTO: the as-shipped 19200 */
        }
        supply_link_wire(&s_supply_link, &w);
        uint8_t off = 0u;
        uint8_t n = 0u;
        uint16_t regs[LINK_SP_FLAGS];
        if (supply_link_next(&s_supply_link, &off, &n, regs)
            == SUPPLY_LINK_W_NONE) {
            break;
        }
        link_msg_t reply = { 0 };
        int result = SUPPLY_LINK_NO_ANSWER;
        if (write_regs(&s_host, LINK_PAGE_SUPPLY, off, n, regs, &reply)) {
            result = (reply.op == LINK_OP_NACK) ? (int)reply.regs[0]
                                                : SUPPLY_LINK_ACK;
        }
        supply_link_written(&s_supply_link, result);
        /* Before the next exchange, whose pump could ask again for an ON
         * this one refused. */
        supply_link_alerts();
        if (result == SUPPLY_LINK_NO_ANSWER) {
            break;
        }
    }
    /* Read while the PD mini is in use, or while something written waits on
     * a read; a disabled one settled costs nothing. */
    if ((w.en || !supply_link_settled(&s_supply_link))
        && supply_link_read_due(&s_supply_link, now_ms())) {
        link_msg_t reply = { 0 };
        /* A 4.3 IOmcu's page ends before BAUD_FOUND. */
        const bool read = poll_page(&s_host, LINK_PAGE_SUPPLY,
                                    s_supply_auto ? (uint8_t)LINK_SP_COUNT
                                                  : (uint8_t)LINK_SP_BAUD_FOUND,
                                    &reply)
                          && reply.op == LINK_OP_DATA;
        if (read && !s_supply_auto) {
            reply.regs[LINK_SP_BAUD_FOUND] = SUPPLY_LINK_BAUD_AUTO;
        }
        supply_link_read(&s_supply_link, read ? reply.regs : NULL, now_ms());
        atomic_store(&s_supply_free_off,
                     read && supply_link_port_free_off(&s_supply_link));
        /* A programming port commanded the supply (4.13). */
        supply_adopt();
        if (read) {
            atomic_store(&s_supply_vin_mv, reply.regs[LINK_SP_VIN_MV]);
            /* 0 for a rate still being looked for: no rate in the header.
             * A 4.3 IOmcu has no BAUD_FOUND and runs at the BAUD it
             * holds, which is what it accepted, not what was last sent: a
             * refused wiring write leaves the old rate in place. */
            atomic_store(&s_supply_baud,
                         (unsigned)supply_page_baud(
                             s_supply_auto ? reply.regs[LINK_SP_BAUD_FOUND]
                                           : reply.regs[LINK_SP_BAUD]));
        }
    }
    supply_link_alerts();
}

void prog_supply_service(void)
{
    uint8_t  off = 0u;
    uint16_t v   = 0u;
    if (prog_supply_link_next(&s_psup_link, &off, &v)) {
        link_msg_t reply = { 0 };
        int result = PROG_SUPPLY_LINK_NO_ANSWER;
        if (write_regs(&s_host, LINK_PAGE_PROG_SUPPLY, off, 1u, &v, &reply)) {
            result = (reply.op == LINK_OP_NACK) ? (int)reply.regs[0]
                                                : PROG_SUPPLY_LINK_ACK;
        }
        prog_supply_link_written(&s_psup_link, off, v, result);
        /* What the write left (an ON's ACK, an OFF owed) reaches the gates
         * now, not after the next read. */
        prog_supply_publish();
        /* An ON of the PROG 5V box the IOmcu did not take is said. */
        switch (prog_supply_link_take_on_result(&s_psup_link)) {
        case PROG_SUPPLY_LINK_NONE:
        case PROG_SUPPLY_LINK_ACK:
            break;
        case PROG_SUPPLY_LINK_NO_ANSWER:
            control_alert(TR(ALERT_P5_NO_ANSWER));
            break;
        case (int)LINK_NACK_NOT_ARMED:
            control_alert(TR(ALERT_P5_NO_BEAT));
            break;
        default:
            control_alert(TR(ALERT_P5_HELD));
            break;
        }
    }
    if (prog_supply_link_read_due(&s_psup_link, now_ms())) {
        link_msg_t reply = { 0 };
        const bool read = poll_page(&s_host, LINK_PAGE_PROG_SUPPLY,
                                    (uint8_t)LINK_PS_COUNT, &reply)
                          && reply.op == LINK_OP_DATA;
        prog_supply_link_read(&s_psup_link, read ? reply.regs : NULL,
                              now_ms());
    }
    supply_screen_set_prog_5v((uint8_t)prog_supply_link_state(&s_psup_link),
                              s_psup_link.pin);
    prog_supply_publish();
}

/**
 * @brief The port a programmer session holds the ports' 5V with, for the
 *        PROG 5V box: the KST session's, else the Hitec page's; some port
 *        while the page says one holds it and neither session does.
 * @return 0 to 3, PROG5V_HELD_SOME or PROG5V_HELD_NONE
 */
static uint8_t prog5v_held_port(void)
{
    kst_link_readout_t rd;
    kst_link_readout(&s_kst_link, &rd);
    if (kst_prog_port_taken(&rd) && rd.port_no < LINK_PG_PORTS_MAX) {
        return rd.port_no;
    }
    const int h = hitec_taken_port();
    if (h >= 0) {
        return (uint8_t)h;
    }
    return (s_prog_taken || prog_supply_link_port(&s_psup_link))
               ? (uint8_t)PROG5V_HELD_SOME : (uint8_t)PROG5V_HELD_NONE;
}

void prog_supply_publish(void)
{
    prog5v_box_publish((uint8_t)prog_supply_link_state(&s_psup_link),
                       s_psup_link.pin, prog5v_held_port());
    const bool board = outbind_roles(s_board) != NULL;
    atomic_store(&s_psup_owns, prog_supply_link_owns(&s_psup_link, board));
    atomic_store(&s_psup_live, prog_supply_link_live(&s_psup_link, board,
                                                     s_prog_taken));
}
