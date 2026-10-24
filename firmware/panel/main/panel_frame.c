/**
 * @file panel_frame.c
 * @brief The render loop, from app_main once the tasks are running.
 *
 * What the control task published goes into the screens; touch, into the
 * router; the screens' commands, back to the control task; then one frame
 * is drawn and flipped.  Every 300 frames the console line gives the frame
 * rate, the draw and wait times, the panel's own refresh and the touch
 * losses.  panel_frame_loop() does not return.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "busfault_screen.h"
#include "display.h"
#include "esc_stick_panel.h"
#include "flash_gate.h"
#include "knob.h"
#include "motor_screen.h"
#include "outputs_pages.h"
#include "outputs_screen.h"
#include "overview_screen.h"
#include "panel.h"
#include "panel_main.h"
#include "pdmini.h"
#include "picker_screen.h"
#include "sensors_rows.h"
#include "servo_link.h"
#include "servo_screen.h"
#include "settings.h"
#include "settings_screen.h"
#include "splash_screen.h"
#include "supply_screen.h"
#include "touch.h"
#include "ui_band.h"
#include "ui_screen.h"
#include "ui_text.h"

static const char *TAG = "rcbench";

/*
 * A link that was up and stopped, said on the panel rather than on a console.
 *
 * The panel has no console an operator can reach while the bench runs: the
 * board sits beside the bench with no computer on its USB (Universal Serial
 * Bus) sockets.  A fault that only a console can explain is one nobody in
 * the field can report.
 *
 * Four seconds, not one: the link drops for a poll now and then, and a screen
 * that took over on every blip would be a screen operators learn to dismiss.
 */
#define LINK_LOST_SCREEN_MS 4000u

gfx_canvas_t *panel_canvas(void)
{
    if (display_take_repaint()) {
        ui_router_invalidate();
    }
    if (display_take_turn_alert()) {
        ui_router_set_alert(TR(ALERT_ROTATION_OFF));
    }
    return display_canvas();
}

/** @brief The render loop (panel_main.h).  Does not return. */
void panel_frame_loop(void)
{
    uint32_t frames  = 0;
    uint32_t flip_timeouts = 0;   /* since start-up */
    uint32_t last_us = (uint32_t)esp_timer_get_time();
    uint32_t last_vs = display_vsync_count();
    uint32_t last_vs_us = last_us;
    bool     was_armed = false;
    uint32_t last_stops = 0;

    for (;;) {
        const uint32_t us = (uint32_t)esp_timer_get_time();
        const float dt_s = (float)(us - last_us) / 1e6f;
        last_us = us;
        /* The clock the supply's readings are stamped with, for the servo
         * test's timing, before this frame's samples. */
        servo_screen_clock(now_ms());

        /*
         * What the bench is, before this frame's touch is dispatched.
         *
         * A press dispatched below can command a position, and the screens
         * decide what a command means from whether the bench is armed: an arm
         * discards what was held before it, so learning of the arm after the
         * press would throw away a position issued after it and leave the
         * screen showing nothing driven while the panel held one.
         */
        bool     armed_now;
        bool     arm_wanted_now;
        bool     supply_now;
        uint32_t supply_gen_now;
        uint32_t stops_now;
        uint32_t pressed_now;
        uint32_t arm_gen_now;
        bool     link_now;
        servo_source_id_t servo_source_now;
        uint32_t servo_changes_now;
        snap_lock();
        link_now       = s_snap.link_up;
        servo_source_now = s_snap.servo_source;
        servo_changes_now = s_snap.servo_source_changes;
        const servo_source_why_t servo_why_now = s_snap.servo_source_why;
        const servo_source_why_t servo_dropped_now =
            s_snap.servo_source_dropped;
        const uint32_t servo_dropped_at_now = s_snap.servo_source_dropped_at;
        armed_now      = s_snap.armed;
        const uint8_t armed_roles_now = armed_now ? s_snap.armed_roles : 0u;
        arm_wanted_now = s_snap.arm_wanted;
        supply_now     = s_snap.supply.output;
        supply_gen_now = s_snap.supply_gen;
        stops_now      = s_snap.stops;
        pressed_now    = s_snap.pressed;
        arm_gen_now    = s_snap.arm_gen;
        snap_unlock();
        /* Whether this frame found the touch stream broken; an arm is
         * acknowledged only by a frame that did not.  See the end of the
         * frame. */
        bool frame_lost = false;
        const knob_turn_t knob_turn = knob_take();

        /*
         * A stop ends any hold under way and drops an arm it has already
         * produced, on both screens.
         *
         * Counted rather than watched for an edge.  The latch says only that
         * a stop is in force, so a second STOP during a hold begun after the
         * first one changes nothing about it, and that hold would run to
         * completion and clear the latch.  Every stop is an event here, and
         * touch that stops answering is counted as one.
         */
        if (stops_now != last_stops) {
            motor_screen_cancel_arm();
            servo_screen_cancel_arm();
            supply_screen_cancel_on();
        }
        last_stops = stops_now;
        /* And a stick run on MOTOR & ESC > PROGRAM, which ends on a stop,
         * a disarm or a link that went, and steps on the frame's time.
         * The pressed count tells an operator's STOP from the bench's
         * own. */
        /*
         * Each screen is told whether the bench is armed for its own role:
         * an arm covers the role of the screen that asked for it (MOTOR & ESC
         * and its stick run the throttle, SERVO the surfaces), and a
         * bench armed for the other role is a disarmed one to it.
         */
        const bool motor_armed_now =
            (armed_roles_now & OUT_ROLE_BIT(OUT_ROLE_THROTTLE)) != 0u;
        const bool servo_armed_now =
            (armed_roles_now & OUT_ROLE_BIT(OUT_ROLE_SURFACE)) != 0u;
        esc_stick_panel_bench(now_ms(), motor_armed_now, stops_now,
                                pressed_now, link_now);
        /* The phase tap's readout for the run's page, copied under the
         * lock from the snapshot. */
        snap_lock();
        esc_stick_panel_tone(&s_snap.tone);
        snap_unlock();
        /* And the servo programmer on SERVO. */
        kst_ui_service();

        /* The slider follows the bench: a disarm returns the command to
         * zero, so the control the operator picks up next is at zero too. */
        if (was_armed && !motor_armed_now) {
            motor_screen_set_throttle(0.0f);
            motor_cmd_t follow;
            (void)motor_screen_poll_cmd(&follow);   /* not a command */
        }
        was_armed = motor_armed_now;
        /* The arm state the plot was last told, so the arm edge is seen here
         * rather than inferred from was_armed, which several other things
         * above already consume. */
        static bool motor_armed;
        /*
         * The arm edge before this frame's samples and the disarm edge after
         * them, so a queued sample lands on the side of the run boundary it
         * was captured on.  The queue holds SAMPLE_Q_LEN samples, and this
         * ordering is what decides where a run's trace begins and ends: the
         * arm clears the plot, so an arm reported after the drain would wipe
         * the samples the drain had just recorded into the new run.
         *
         * What this does not cover: a run that both arms and disarms between
         * two frames is never seen armed here, so it leaves no trace at all.
         * That is a run shorter than one frame, about 17ms at 60Hz.
         */
        if (motor_armed_now && !motor_armed) {
            /*
             * A run starts at the arm, so what the queue is holding from
             * before it is not part of it.  Dropped rather than pushed: the
             * queue can hold SAMPLE_Q_LEN samples, 400ms at
             * PANEL_SAMPLE_HZ, and without this the first 400ms of a run's
             * trace would be readings taken while the bench was not driving.
             */
            bench_state_t stale;
            while (xQueueReceive(s_sample_q, &stale, 0) == pdTRUE) { }
        }
        if (motor_armed_now) {
            motor_screen_set_armed(true);
        }
        motor_armed = motor_armed_now;
        /* One sample, one plot column, however many frames it took to get
         * here: the queue holds what this loop was too busy to draw. */
        bench_state_t queued;
        while (xQueueReceive(s_sample_q, &queued, 0) == pdTRUE) {
            motor_screen_push(&queued);
        }
        if (!motor_armed_now) {
            motor_screen_set_armed(false);
        }
        servo_screen_set_armed(servo_armed_now);
        servo_screen_set_bench_armed(armed_now);
        {
            /*
             * Whether the screen's frame rate reached the pins, and the
             * drive it reached: one pair, for the note that names both.
             * The control task (core 1) stores the rate reset of a TARGET
             * change before the new drive (prog_link_service(), the
             * link-up), and a rate that lands on the new target after it.
             * This core reads the drive, the rate, then the drive again,
             * every access sequentially consistent: the same drive on
             * both sides of the rate means the two held together at the
             * rate's load, so the note never pairs a rate with a target it
             * was not stored for.  A drive that moves on every try keeps
             * the screen's last pair for this frame.
             */
            unsigned drive = atomic_load(&s_drive);
            unsigned r = 0u;
            bool paired = false;
            for (int k = 0; k < 3 && !paired; ++k) {
                r = servo_link_rate_shown();
                const unsigned again = atomic_load(&s_drive);
                paired = again == drive;
                drive = again;
            }
            if (paired) {
                servo_screen_rate((servo_rate_state_t)(r >> 16),
                                  (uint16_t)(r & 0xFFFFu));
                servo_screen_set_drive((uint8_t)(drive >> 8),
                                       (uint8_t)(drive & 0xFFu),
                                       (drive & DRIVE_PWM_BIT) != 0u);
            }
            servo_screen_set_sweep(servo_link_sweep_able());
            servo_screen_set_surfaces(servo_link_surfaces());
            /* What the tester drives and whether the port renders its
             * pulse, to the control task; the port's pin, to both views of
             * the binding, from one read. */
            atomic_store(&s_prog_want_target,
                         (unsigned)settings_get_int(SET_SERVO_TARGET));
            atomic_store(&s_prog_want_pwm,
                         settings_get_bool(SET_SERVO_PORT_PWM));
            prog_ports_to_screens();
            motor_screen_set_throttle_pins(atomic_load(&s_throttle_pins),
                                           atomic_load(&s_throttle_pins_known));
            motor_screen_set_throttle_protocol(
                (uint8_t)atomic_load(&s_throttle_proto));
            servo_screen_set_pins(atomic_load(&s_servo_pins),
                                  atomic_load(&s_servo_pins_known));
            servo_screen_set_enc_held(atomic_load(&s_enc_held));
            settings_screen_set_unheld(
                (uint16_t)atomic_load(&s_sense_unheld),
                (uint8_t)atomic_load(&s_tone_unheld));
            sensors_rows_set_status((uint16_t)atomic_load(&s_sense_parts));
            sensors_rows_set_limits((uint16_t)atomic_load(&s_sense_limits));
            /* What the IOmcu holds of Hitec D receive, back to SETUP's
             * row. */
            settings_screen_set_hitec_rx(
                (uint8_t)atomic_load(&s_hitec_rx_mark));
            if (servo_link_take_sweep_refused()) {
                servo_screen_sweep_refused();
            }
            uint16_t pause_seq = 0u;
            if (servo_link_take_hold_lost(&pause_seq)) {
                servo_screen_released(pause_seq);
            }
            /* The pause's phase before a resume that rebases on it. */
            uint32_t kept_ms = 0u;
            if (servo_link_take_sweep_held(&pause_seq, &kept_ms)) {
                servo_screen_sweep_held(pause_seq, kept_ms);
            }
            if (servo_link_take_sweep_start()) {
                const uint32_t now = now_ms();
                const servo_link_start_t st = servo_link_sweep_start();
                servo_screen_sweep_started(st.start_seq, now - st.start_ms,
                                           st.from, now - st.frozen_ms);
            }
        }

        /*
         * The supply's run, bounded by each sample's own output: a sample
         * taken with it on starts or continues the run, one taken with it
         * off ends it before its readings are shown.  The snapshot's edge
         * cannot place them, because the control task steps the supply
         * inside an exchange's wait and publishes the snapshot only after.
         * supply_seen is the output the newest sample reported.
         */
        static bool supply_seen;
        /* Which supply the control task drives: the screen's header, caps
         * and the menu's badge follow it. */
        static bool supply_real_shown;
        static unsigned supply_vcap_shown;
        const bool supply_real = atomic_load(&s_supply_real);
        /* The PD mini is given no more than its input less PDMINI_CAP_MV:
         * the screen's voltage cap follows the input as the page reads it,
         * moved only on a change of 100mV or more. */
        unsigned vcap = 0u;
        if (supply_real) {
            const unsigned vin = atomic_load(&s_supply_vin_mv);
            /* 0 is an input not read: the full range.  Any other input
             * gets the cap the driver writes, floor included. */
            vcap = (vin == 0u) ? PDMINI_V_MAX_MV : pdmini_cap_mv(vin);
        }
        const unsigned vcap_moved = (vcap > supply_vcap_shown)
                                        ? vcap - supply_vcap_shown
                                        : supply_vcap_shown - vcap;
        if (supply_real != supply_real_shown || vcap_moved >= 100u) {
            supply_real_shown = supply_real;
            supply_vcap_shown = vcap;
            supply_caps_t pdmini = SUPPLY_CAPS_PDMINI;
            if (vcap != 0u) {
                pdmini.v_max = (float)vcap / 1000.0f;
            }
            const supply_caps_t pps = SUPPLY_CAPS_PPS_DEFAULT;
            supply_screen_set_caps(supply_real ? &pdmini : &pps);
            supply_screen_set_model(!supply_real);
            overview_screen_set_supply_real(supply_real);
        }
        {
            const supply_wiring_t wiring = pdmini_wiring();
            supply_screen_set_pins(wiring.tx, wiring.rx);
        }
        supply_screen_set_baud(supply_real ? atomic_load(&s_supply_baud)
                                           : 0u);
        servo_test_enc_t enc;
        while (xQueueReceive(s_enc_q, &enc, 0) == pdTRUE) {
            servo_screen_encoder(&enc);
        }
        /* The servo rail's meter, then the INA3221's CH1 windows, each
         * once with the meter of its poll: the SERVO screen's CURRENT row,
         * line and plot show the meter's reading, and a servo test reads
         * the meter it started with.  Why the INA3221 last stopped being
         * the meter goes in ahead of each, so a run that ends on the
         * change names the condition. */
        servo_screen_source_why(servo_why_now);
        servo_screen_source_dropped(servo_dropped_now, servo_dropped_at_now);
        servo_screen_source(servo_source_now, servo_changes_now);
        servo_win_item_t win;
        while (xQueueReceive(s_win_q, &win, 0) == pdTRUE) {
            servo_screen_source_dropped(win.dropped, win.dropped_at);
            servo_screen_window(&win.win, win.source, win.changes);
        }
        /* The pulse COMMANDED shows and the angle MEASURED shows, for a
         * SERVO run's rows: one word, so the control task reads the two as
         * one. */
        {
            uint16_t us  = 0u;
            float    deg = 0.0f;
            const bool have_us  = servo_screen_log_pulse(&us);
            const bool have_deg = servo_screen_log_angle(&deg);
            atomic_store(&s_servo_log,
                         log_servo_pack(have_us, us, have_deg, deg));
        }
        supply_item_t sup_item;
        while (xQueueReceive(s_supply_q, &sup_item, 0) == pdTRUE) {
            const supply_state_t sup = sup_item.st;
            supply_screen_set_output(sup.output);
            supply_screen_push(&sup);
            /* And the SERVO screen's servo test and, while the supply is
             * the servo rail's meter, its live power plot. */
            servo_screen_source_dropped(sup_item.dropped,
                                        sup_item.dropped_at);
            servo_screen_supply_at(&sup, sup_item.source, sup_item.changes);
            /* And a stick run, which counts beeps in every reading. */
            esc_stick_panel_supply(&sup);
            supply_seen = sup.output;
        }
        /* The simulated ESC follows the run, while the model is the
         * supply; see escsim_load(). */
        atomic_store(&s_escsim_runs, (unsigned)esc_stick_panel_runs());
        atomic_store(&s_escsim_profile,
                     (uintptr_t)esc_stick_panel_profile());

        if (drain_touch(stops_now)) {
            frame_lost = true;
        }

        /* What the screens decided, back to the control task. */
        /*
         * A command is taken from its screen before it is queued, so a queue
         * that refused it would drop it for good -- a disarm among them.  The
         * send waits briefly and, if the queue is still full, drops the
         * OLDEST entry rather than this one: the newest throttle position and
         * a disarm both matter more than a stale step.
         */
        /* What the IOmcu says its outputs are, and what became of the
         * last write.  Screen state stays app_main's; the control task only
         * leaves values behind. */
        /* The flag is read inside the lock that guards the value it refers
         * to.  Testing it outside would let this task cache it and miss a
         * binding the control task had just read off the wire. */
        static bind_reading_t got;      /* app_main only */
        static bool got_read;           /* app_main only */
        static uint8_t got_throttle;    /* app_main only */
        bool have = false;
        if (xSemaphoreTake(s_snap_lock, 0) == pdTRUE) {
            have = s_outputs_read_fresh;
            if (have) {
                got = s_outputs_read;
                s_outputs_read_fresh = false;
            }
            xSemaphoreGive(s_snap_lock);
        }
        if (have) {
            /*
             * Both views of one binding: whichever is on screen, the other is
             * showing the same thing when the operator reaches it.
             *
             * The outputs screen first, and the picker takes what came out of
             * it rather than what came off the wire.  The outputs screen
             * holds the selected protocol -- the picker's list selects
             * through it -- and an empty page carries no protocol at all, so
             * handing the picker the raw read would leave it on OFF and
             * unable to add a pin, however the outputs screen had
             * reconciled it.
             */
            outputs_screen_set_reading(&got);
            picker_screen_set_binding(outputs_screen_binding());
            /* RUN on ESC STICK moves the throttle pins: none bound, or
             * no binding read, is a run refused with the reason. */
            got_read = got.state == BIND_READ_OK;
            got_throttle = got_read
                ? outputs_role_channels(got.slots, got.cfg, OUT_ROLE_THROTTLE)
                : 0u;
        }
        esc_stick_panel_set_throttle(got_read, got_throttle);
        /* A pick in either list writes nothing and so reads nothing back:
         * the picker follows the outputs screen's selection, and whether
         * the binding is one a read confirmed, every frame. */
        picker_screen_follow(outputs_screen_binding()->proto,
                             outputs_screen_editable());
        /* A drawing a link-up edge published or withdrew, shown now and not
         * on the picker's next visit. */
        drawing_follow();
        outputs_screen_set_result(
            (outputs_result_t)atomic_load(&s_outputs_result));

        flush_screen_commands(stops_now);
        test_lines_service();
        /* An ON the supply screen sent and the control task dropped -- stale,
         * or lost to a full queue -- stops counting as live there. */
        supply_screen_set_on_coming(supply_live_or_coming());
        /* A servo test's end and the set points it put back, whichever
         * screen is up: after this frame's OFF went and its samples. */
        servo_screen_service();
        /*
         * Whether a STOP is on screen to press.  The control task hit-tests
         * the band's rectangle and cannot see which screen is up.
         */
        atomic_store(&s_stop_live, ui_router_stop_live());
        /*
         * The control task hit-tests STOP itself; this is the backstop for a
         * press it did not see.  It sets the flag rather than queueing,
         * because a full queue must not be able to discard a stop.
         */
        if (ui_router_take_stop()) {
            atomic_store(&s_stop_request, true);
        }

        bench_state_t bench;
        bool     link_up;
        uint16_t faults;
        uint32_t link_errors;
        uint32_t link_refused;
        float    mcu_temp_c;
        uint32_t run_seconds;
        char     alert[ALERT_MAX];
        bool     have_alert;
        cu_snap_t cu;
        snap_lock();
        cu          = s_snap.cu;
        bench       = s_snap.bench;
        link_up     = s_snap.link_up;
        faults      = s_snap.faults;
        link_errors = s_snap.link_errors;
        link_refused = s_snap.link_refused;
        mcu_temp_c  = s_snap.mcu_temp_c;
        run_seconds = s_snap.run_seconds;
        have_alert  = ui_alert_slot_take(&s_snap.alert, alert, sizeof(alert));
        const uint32_t alert_taken = s_snap.alert.taken;
        const uint32_t withdrawn   = s_snap.alert.withdrawn;
        snap_unlock();

        /* The control task's alert on the band, cleared when the control
         * task takes it back while the band still shows it. */
        static ui_alert_band_t s_band;
        (void)ui_alert_band_update(&s_band, have_alert ? alert : NULL,
                                   alert_taken, withdrawn);
        /* A servo test ends when the link goes. */
        servo_screen_set_link(link_up);
        /* The armed state this frame acted on, read before the touch was
         * dispatched; the band shows what the screens were told. */
        const bool armed = armed_now;
        const bool cu_active = cu.active;
        const ui_bench_status_t status = {
            .link_up     = link_up,
            .armed       = armed,
            .armed_roles = (uint8_t)((motor_armed_now ? UI_ARMED_MOTOR : 0u)
                                     | (servo_armed_now ? UI_ARMED_SERVO
                                                        : 0u)),
            .faults      = faults,
            .run_seconds = run_seconds,
#ifdef RCBENCH_TOUCH_INJECTOR
            /* A build with the touch injector names an attached one where the
             * mode goes; LINK still tells BENCH from SIM, and the
             * SIMULATION watermark still covers a simulated bench. */
            .mode        = touch_injector_attached()
                               ? ui_band_mode_inject()
                               : ui_band_mode(link_up),
#else
            .mode        = ui_band_mode(link_up),
#endif
            /* Not over an update: the link is down for it, and the
             * simulator that runs while it is down drives nothing shown. */
            .simulated   = bench_state_simulated(&bench) && !cu_active,
            .capabilities = (uint16_t)atomic_load(&s_capabilities),
            .link_errors = link_errors,
            .link_refused = link_refused,
            .mcu_temp_c  = mcu_temp_c,
        };
        ui_router_set_status(&status);

        if (splash_screen_done() && ui_router_current() == SCREEN_SPLASH) {
            /*
             * A bus that failed its test is what the panel says first.  The
             * menu offers screens that all read the same numbers, and every
             * one of them would show the same nothing without saying why.
             */
            ui_router_goto(s_bus_ok ? SCREEN_OVERVIEW : SCREEN_BUSFAULT);
            if (s_boot_alert[0] != '\0') {
                ui_router_set_alert(s_boot_alert);
                s_boot_alert[0] = '\0';
            }
        }

        /*
         * And the menu once it has been acknowledged.  The hold is the
         * screen's; where an acknowledged fault leads is not, so it latches
         * and this decides.
         */
        if (busfault_screen_take_ack()) {
            ui_router_goto(SCREEN_OVERVIEW);
        }

        /*
         * The IOmcu's update.  The question opens on the menu, on a
         * bench that is SAFE with the supply off, the panel's radio wholly
         * off and no install of the panel under way, once per offer; START
         * and CANCEL go to the control task.  SETUP > UPDATES' hold starts
         * the same update, offered or declined, under the same conditions.  While the update runs the screen
         * shows its progress, and the menu comes back when it has ended --
         * the alert says how.
         */
        /* The release reminder first: it yields to the IOmcu's
         * question, which comes first. */
        upd_remind_service(armed || arm_wanted_now,
                           armed || arm_wanted_now || supply_now
                               || supply_live_or_coming(),
                           &cu);
        cu_render_service(armed || arm_wanted_now || supply_now
                              || supply_live_or_coming(),
                          link_up, &cu);

        /*
         * A link that was up and has been gone for LINK_LOST_SCREEN_MS says
         * so, with the controller's own counters on it.  The panel has no
         * console an operator can reach while the bench runs, so a fault it
         * cannot show is a fault nobody can report.
         *
         * Never while armed.  This screen carries no band and therefore no
         * STOP, and a bench with something spinning must not have its stop
         * button covered by a diagnosis.  Armed, the alert band already says
         * the link is gone, and the screen waits for the disarm.  Nor while
         * the supply's output is on, by the snapshot or by the newest
         * sample: the supply does not need the link, and STOP and OUTPUT
         * OFF stay on screen for as long as it is live.  Nor over WI-FI,
         * whose Wi-Fi does not need the link, nor over PANEL
         * UPDATE, which withholds the heartbeat on purpose while it writes.
         *
         * The timestamp is read once and tested twice.  The control task
         * clears it on the other core the moment the link answers, and a
         * second read that caught the zero would test now_ms() - 0, the
         * uptime, against LINK_LOST_SCREEN_MS: past 4000ms of uptime that
         * passes, and the screen takes over on a link that is up.
         */
        const uint32_t lost_ms = atomic_load(&s_link_lost_ms);
        if (!armed && !cu_active && !supply_now && !supply_seen && !supply_live_or_coming()
            && lost_ms != 0u && !s_link_lost_shown
            && (uint32_t)(now_ms() - lost_ms) >= LINK_LOST_SCREEN_MS
            && ui_router_current() != SCREEN_SPLASH
            && ui_router_current() != SCREEN_BUSFAULT
            && ui_router_current() != SCREEN_WIFI
            && ui_router_current() != SCREEN_UPDATE) {
            /*
             * The screen has no STOP, so it opens on a supply that is off
             * and stays off: an ON this frame already queued, which the
             * snapshot read above cannot show, is cancelled the way OUTPUT
             * OFF cancels one -- the count it carries goes stale, and the
             * flag switches it off again should it have been applied.
             */
            atomic_fetch_add(&s_supply_offs, 1u);
            atomic_store(&s_supply_off_request, true);
            busfault_report_t r;
            link_lost_report(&r);
            busfault_screen_set(&r);
            s_link_lost_shown = true;
            ui_router_goto(SCREEN_BUSFAULT);
        }
        /*
         * And once more, right before the tick.  Both queues can lose an
         * event while this loop is running: the control task refills
         * s_touch_q from the other core, and the driver's own queue drops its
         * oldest when nobody collects it.  A gesture that completes on a
         * timer -- the arming hold, the fault acknowledgement -- would
         * otherwise finish in this tick on a contact that has gone, its loss
         * found only next frame.  The second drain finds everything offered
         * up to now.
         */
        if (drain_touch(stops_now)) {
            frame_lost = true;
        }
        ui_router_tick(dt_s);
        knob_apply(&knob_turn, frame_lost);
        wifi_service(arm_wanted_now || armed_now, stops_now);
        upd_service(arm_wanted_now || armed_now,
                    arm_wanted_now || armed_now || supply_now
                        || supply_live_or_coming(),
                    stops_now, &cu);

        /*
         * The arm this frame began with, acknowledged -- only by a frame that
         * began with the bench armed and found the touch stream whole.  Until
         * then the control task undoes the arm on any loss
         * (arm_watch_service()), because a frame that began disarmed cancels
         * screens that post no disarm.  A loss in a frame that began armed is
         * answered by the screens against an armed bench.
         */
        if (armed_now && !frame_lost) {
            atomic_store(&s_arm_ack, arm_gen_now);
        }
        /* And the supply's ON the same way; see supply_watch_service(). */
        if (supply_now && !frame_lost) {
            atomic_store(&s_supply_ack, supply_gen_now);
        }

        /*
         * The save the settings screen asked for, taken at a moment that can
         * afford it.
         *
         * Writing settings commits an NVS page, and a flash operation on this
         * part disables the cache: neither core runs code that is not in
         * IRAM for its duration.  Taken here it costs a frame.  When it may
         * happen is flash_gate_open()'s rule, the one the drawing's keep
         * asks too: not while the bench is armed or an arm waits -- the
         * control task beats the safety line and its ceiling is
         * HEARTBEAT_MAX_GAP_MS (150ms) -- and not while the supply's output
         * is on or an ON is queued: OUTPUT OFF, STOP and the trips are the
         * control task's, and a flash write would stall them for its length.
         * Nor while the board's drawing is being fetched or kept: one flash
         * writer at a time.
         *
         * Waiting costs nothing.  The request stands until a quiet frame
         * comes, and disarmed -- which is where the settings screen is used
         * -- the next frame is one.
         */
        (void)settings_save_tick(flash_gate_open(arm_wanted_now,
                                                 supply_live_or_coming())
                                 && !s_artbusy && !atomic_load(&s_keeping));
        /* And the ramp, from the task that owns the values, for the control
         * task to read on its next pump. */
        publish_throttle_ramp();

        gfx_canvas_t *c = panel_canvas();
        const int64_t draw_start = esp_timer_get_time();
        ui_router_render(c, display_back_index());
        const uint32_t draw_us = (uint32_t)(esp_timer_get_time() - draw_start);
        /* Blocks until the swap has taken effect.  A flip that saw no
         * frame-done event in 200ms is counted for the console line. */
        if (display_flip() == ESP_ERR_TIMEOUT) {
            ++flip_timeouts;
        }

        /*
         * DRAW is the paint time of the frame; WAIT is how long the flip
         * blocked afterwards, TURN the part of WAIT Screen rotation 180°
         * spent copying the frame turned (0 in NORMAL).  A healthy frame
         * is mostly WAIT, the loop paced by the panel.  DRAW climbing
         * until WAIT reaches zero is the frame budget being spent.  VSYNC is the panel's refresh as
         * measured here, FLIP TIMEOUTS the flips since start-up that waited
         * 200ms for a frame-done event in vain.  Printed every 300 frames.
         */
        if (++frames % 300u == 0u) {
            /* And the panel's own refresh, from its VSYNC (vertical sync)
             * events over the same frames. */
            const uint32_t vs = display_vsync_count();
            const uint32_t vs_us = (uint32_t)esp_timer_get_time();
            const float vsync_hz =
                (vs_us != last_vs_us)
                    ? (float)(vs - last_vs) * 1e6f / (float)(vs_us - last_vs_us)
                    : 0.0f;
            last_vs = vs;
            last_vs_us = vs_us;
            ESP_LOGI(TAG,
                     "%.1f fps  DRAW %u us  WAIT %u us  TURN %u us  "
                     "VSYNC %.2f Hz  FLIP TIMEOUTS %u  TOUCHLOST %u/%u",
                     (double)display_fps(), (unsigned)draw_us,
                     (unsigned)display_last_wait_us(),
                     (unsigned)display_last_turn_us(), (double)vsync_hz,
                     (unsigned)flip_timeouts, s_tq_lost,
                     atomic_load(&s_drv_gaps));
        }
    }
}
