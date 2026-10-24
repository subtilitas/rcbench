/**
 * @file panel_control.c
 * @brief The control task: touch, STOP, arming, the outputs, the link and
 *        the heartbeat.
 *
 * The task runs at a fixed CONTROL_PERIOD_MS on the core the renderer
 * does not use.  control_pump() is the part of it with a deadline; it
 * also runs inside every link exchange's wait.  The heartbeat's pin and the
 * die temperature are this task's too.  app_main (main.c) creates it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <math.h>
#include <stdatomic.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "arming.h"
#include "board_pins.h"
#include "frame_watch.h"
#include "gfx.h"
#include "heartbeat.h"
#include "outputs.h"
#include "panel.h"
#include "panel_main.h"
#include "servo_link.h"
#include "telemetry_sim.h"
#include "touch.h"
#include "touch_loss.h"
#include "ui_band.h"
#include "ui_text.h"

static const char *TAG = "rcbench";

/* ------------------------------------------------------------ the heartbeat */

/*
 * Driven from the loop that reads touch and draws STOP.  The line asserts
 * that the processor owning the STOP button is running its loop, which a
 * level cannot assert and a crashed panel cannot fake.
 *
 * The monostable the line gates is on no board: the edges reach a header
 * pin of the panel (PANEL_HEARTBEAT_PIN) and the IOmcu's monitor.  They
 * are emitted regardless, at one GPIO (general-purpose input/output) write
 * per pass, so the line is running and can be scoped.
 */
static heartbeat_gen_t s_beat;


void heartbeat_init(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PANEL_HEARTBEAT_PIN,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_set_level(PANEL_HEARTBEAT_PIN, 0));
    heartbeat_gen_init(&s_beat);
    ESP_LOGI(TAG, "heartbeat on GPIO%d every %u ms; the monostable it "
                  "gates is on no board",
             (int)PANEL_HEARTBEAT_PIN, (unsigned)HEARTBEAT_PERIOD_MS);
}

/* ------------------------------------------------------------ die temperature */

/*
 * The panel's own die, not the IOmcu's.  The two boards sit in
 * different air and run different loads, and this is the one the ESP32-P4
 * can measure without a wire.  Read once a second; the sensor is slow and
 * nothing on the screen changes faster.
 */
static temperature_sensor_handle_t s_tsens;
/* Read and published by the control task; app_main takes it from the
 * snapshot with everything else that crosses between the two. */
float s_mcu_c = NAN;

void tsens_init(void)
{
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&cfg, &s_tsens) != ESP_OK
        || temperature_sensor_enable(s_tsens) != ESP_OK) {
        s_tsens = NULL;
        ESP_LOGW(TAG, "no die temperature; the strip shows --");
    }
}

static void tsens_read(void)
{
    float c = 0.0f;
    if (s_tsens != NULL && temperature_sensor_get_celsius(s_tsens, &c) == ESP_OK) {
        s_mcu_c = c;
    }
}

static void beat(bool alive)
{
    const bool level = heartbeat_gen_step(&s_beat, now_ms(), alive);
    gpio_set_level(PANEL_HEARTBEAT_PIN, level ? 1 : 0);
}

/* Whether a STOP is drawn to press; set by the render loop
 * (panel_frame.c). */
atomic_bool s_stop_live;
/* The driver's queue, numbered (touch_loss.h); see s_drv_gaps, main.c. */
static touch_seq_rx_t s_drv_rx = { .next = 1u };   /* touch_seq_rx_init() */
/* s_touch_q's numbers, this task's alone; see s_tq_published, main.c. */
static uint32_t    s_tq_seq;
/** @brief The render loop's watchdog, this task's alone; see
 *         render_watch(). */
static frame_watch_t s_frame_watch;

/* ALERT_SCREEN_STALLED names the timeout. */
_Static_assert(FRAME_WATCH_MS == 1000u,
               "ALERT_SCREEN_STALLED says 1s: change both together");

/**
 * @brief The render loop, watched while the bench is ARMED or the supply's
 *        output is on (frame_watch.h).
 *
 * A render loop that has stopped leaves STOP working, through this task's
 * own hit test, and nothing else: DISARM, the throttle slider, the servo
 * dial and OUTPUT OFF are the loop's, and the outputs hold their last
 * command.  A counter that has stood still for FRAME_WATCH_MS is answered
 * as a STOP of the bench's own, the way an arm the watch undoes is:
 * arming_stop() drops the heartbeat in this pass, the IOmcu fails safe
 * within HEARTBEAT_MAX_GAP_MS (150ms), supply_pump() switches the output off
 * in this pass, and service_arming() lets go of the rest.  Nothing here arms
 * or switches anything on, so a loop that comes back finds the latch as a
 * STOP leaves it.  The alert shows when the loop draws again.
 *
 * Called from control_pump(), here and inside every exchange's wait, before
 * the supply's pump.
 */
static void render_watch(void)
{
    if (frame_watch_step(&s_frame_watch, atomic_load(&s_render_progress),
                         outputs_armed(&s_out), atomic_load(&s_supply_live),
                         now_ms())) {
        ESP_LOGW(TAG, "render loop still for %u ms: stopped",
                 (unsigned)frame_watch_still_ms(&s_frame_watch, now_ms()));
        arming_stop(&s_arm);
        control_alert(TR(ALERT_SCREEN_STALLED));
    }
}

/*
 * Touch, STOP, the outputs and the heartbeat -- everything with a deadline,
 * and nothing that talks to the link.
 *
 * Called from the top of the control task and again from inside the wait in
 * exchange(), because that wait runs for up to LINK_HOST_TIMEOUT_MS (1000ms)
 * and the heartbeat's ceiling is HEARTBEAT_MAX_GAP_MS (150ms).  Without this
 * a single unanswered CAN reply drops the IOmcu's outputs and latches
 * its failsafe.  The loop that owns STOP really is still running during that
 * wait; this is what makes the line say so.
 */
/*
 * Offer one item to s_touch_q, numbered.  Returns whether the queue holds it.
 *
 * The consumer is behind when the send fails.  The oldest entry is dropped
 * and the newest kept, which is what the GT911's own event queue and the
 * command queue do -- but which of the two is lost is not what makes this
 * safe, and no choice is safe on its own: a release that never arrives
 * leaves a screen holding a press, a DOWN that never arrives orphans the
 * release after it, and the MOVE where a finger leaves ARM is what abandons
 * the hold.  What makes it safe is that the dropped entry's number is
 * missing from what the render loop takes, and it drops the gesture before
 * it handles the next event (touch_loss.h).  Nothing is counted here.
 *
 * The retry runs whether or not the receive took anything: the render loop
 * can empty the queue between the failed send and the receive, and then
 * there is room without anything having been evicted.
 */
static bool touch_q_put(const touch_event_t *evt, bool lost)
{
    touch_item_t it;
    memset(&it, 0, sizeof(it));
    if (evt != NULL) {
        it.evt = *evt;
    }
    it.lost = lost;
    it.seq  = ++s_tq_seq;
    bool kept = (xQueueSend(s_touch_q, &it, 0) == pdTRUE);
    if (!kept) {
        touch_item_t stale;
        (void)xQueueReceive(s_touch_q, &stale, 0);
        kept = (xQueueSend(s_touch_q, &it, 0) == pdTRUE);
    }
    atomic_store(&s_tq_published, it.seq);
    if (lost) {
        s_lost_notice_seq = it.seq;
    }
    return kept;
}

/*
 * The driver's stream lost @p n events.  Found on the event after the gap,
 * before that event goes through the STOP branch, or at the end of a drain
 * against the number the driver last published.
 *
 * The render loop is told by an item in s_touch_q, ahead of the events that
 * followed the gap, so it drops the screens' gestures before it handles any
 * of them.
 *
 * A STOP press needs nothing here: it stopped the bench at its DOWN, and
 * its release does nothing.  Losses in s_touch_q itself cannot lose a STOP
 * either: this task evicts from that queue only after the event has been
 * through the STOP branch.
 */
static void drv_lost(uint32_t n)
{
    atomic_fetch_add(&s_drv_gaps, (unsigned)n);
    (void)touch_q_put(NULL, true);
}

void control_pump(void)
{
    /*
     * The driver's last published number, before the drain: every event up
     * to it had been offered by then, so one the drain does not deliver was
     * dropped (touch_seq_tail()).
     */
    const uint32_t drv_published = touch_published();

    touch_event_t evt;
    uint32_t seq = 0u;
    bool saw_touch = false;
    while (touch_wait_event(&evt, &seq, 0)) {
        saw_touch = true;
        /*
         * A gap is answered before this event is looked at, so the render
         * loop learns of it ahead of the events that followed it.
         */
        const uint32_t missed = touch_seq_take(&s_drv_rx, seq);
        if (missed > 0u) {
            drv_lost(missed);
        }
        bool counted_here = false;
        /*
         * The band's rectangle is a constant, so it has to be asked whether a
         * STOP is drawn: on the splash a tap in that corner presses nothing.
         */
        const bool in_stop =
            atomic_load(&s_stop_live)
            && gfx_rect_contains(ui_band_stop_rect(), evt.point.x,
                                 evt.point.y);
        if (evt.type == TOUCH_EVENT_DOWN && in_stop) {
            /*
             * Applied at the press, not at the release: a stop never waits
             * for a lift that may not come, and a release lost to a full
             * queue or a dropped read would otherwise be a STOP the
             * operator made and the bench never saw.
             *
             * Applied here, not recorded for later.  This runs inside the
             * link's wait as well as at the top of the loop, and a servo
             * command makes up to three exchanges of up to
             * LINK_HOST_TIMEOUT_MS (1000ms) each.  A stop that only set a
             * flag would wait all of that out with the far end driving,
             * because the policy that acts on the flag is what is blocked.
             * arming_stop() takes effect in the same pass:
             * arming_heartbeat() is false while stopped, so the beat at the
             * end of this function stops asserting the line and the IOmcu
             * fails safe within HEARTBEAT_MAX_GAP_MS (150ms) whatever this
             * task is waiting for.
             *
             * The rest of a stop -- the bank, the throttle, the servo's
             * slot, telling the far end -- follows when the loop is free.
             * Counted as pressed: an operator's STOP, which a stick run
             * tells apart from the bench's own.
             */
            arming_stop_pressed(&s_arm);
            counted_here = true;
        }
        /*
         * The screen still sees every event: it draws the press.
         *
         * A still finger emits nothing, so filling 32 slots takes either
         * coordinate wobble on the held contact or a second one; a palm
         * resting on the glass reaches it in about 90ms of undrained
         * frame.  What happens when it does fill is touch_q_put()'s.
         *
         * The router will latch this same press and the backstop would
         * then stop the bench a second time, so a stop applied here is
         * marked with the number the press takes in s_touch_q -- this task
         * alone numbers it, so the number is known before the offer.
         * Marked before the event is offered, so the render loop cannot
         * take the press and find a loss ahead of it before the marker
         * exists.  The number keeps the marker for this press across a loss
         * notice queued ahead of it (stop_mark_keeps()).  Taken back if the
         * queue did not keep the event: a marker left standing for an event
         * nobody saw is consumed by the next stop the backstop really does
         * have to apply, and that stop would be ignored.
         */
        const uint32_t mark = s_tq_seq + 1u;
        if (counted_here) {
            atomic_store(&s_stop_counted, (unsigned)mark);
        }
        const bool routed = touch_q_put(&evt, false);
        if (counted_here && !routed) {
            unsigned expect = (unsigned)mark;
            (void)atomic_compare_exchange_strong(&s_stop_counted, &expect,
                                                 0u);
        }
    }
    /* And a loss at the end of the stream, with no later event to show it. */
    const uint32_t tail = touch_seq_tail(&s_drv_rx, drv_published);
    if (tail > 0u) {
        drv_lost(tail);
    }
    /*
     * An arm the render side has not seen yet, against what this pass found
     * -- here, inside every exchange's wait, and not only once a pass.  The
     * pass that applied the arm can block for seconds on a backlog of servo
     * exchanges, and a loss in that time would leave the arm driving until
     * the pass ended.  Undone the way a STOP is undone from here, because a
     * disarm writes the far end and this runs inside an exchange:
     * arming_stop() drops the heartbeat now, the IOmcu fails safe
     * within HEARTBEAT_MAX_GAP_MS (150ms) whatever this task is waiting
     * for, and service_arming() lets go of the rest when the loop is free.
     * A queued drive command carries the old stop count and is dropped.
     */
    if (outputs_armed(&s_out)
        && arm_watch_lost(&s_arm_watch, atomic_load(&s_loss_gen),
                          atomic_load(&s_drv_gaps), atomic_load(&s_arm_ack))) {
        arming_stop(&s_arm);
        control_alert(TR(ALERT_TOUCH_LOST_ARMING));
    }
    /* And a render loop that has stopped, before the supply's pump, so the
     * stop it counts switches the output off in this pass. */
    render_watch();
    /*
     * touch_age_ms() is the time since the controller last answered a poll,
     * not since the last touch.  An untouched panel is healthy; a controller
     * that has stopped answering is not.
     */
    if (saw_touch || touch_age_ms() < 200u) {
        arming_touch_seen(&s_arm, now_ms());
    }
    /* And judged here, at the rate touch is judged: this runs inside the
     * link's wait, where arming_step() does not. */
    arming_touch_poll(&s_arm, now_ms());
    /* And the supply: off at every stop counted above and at an OFF, and
     * stepped on its own cadence.  It needs no link, so nothing here waits
     * on it. */
    supply_pump();

    /* The setting can change under this loop, and a ramp read once at
     * start-up would be the one the panel booted with.  The value read here
     * is the one app_main last published, not the settings model itself. */
    (void)outputs_set_slew(&s_out, PANEL_CH_THROTTLE, panel_throttle_ramp());
    outputs_step(&s_out, now_ms());
    /*
     * Not gated on the link.  The heartbeat asserts that the processor owning
     * STOP is running its loop; whether the two boards can talk is a separate
     * question with its own watchdog at each end.  Gating on both would let a
     * dropped CAN frame cut the safety line.
     *
     * It is gated on a disarm nobody has served yet.  A write that has been
     * transmitted cannot be recalled: the far end applies it and then
     * acknowledges, and if that acknowledgement is lost this task waits
     * LINK_HOST_TIMEOUT_MS (1000ms) with the request unserved and the
     * output driving.  The line is the one channel that does not need the
     * link, so it carries the disarm instead.
     *
     * On a healthy link this costs nothing -- the request is served on the
     * next pass, well inside HEARTBEAT_MAX_GAP_MS (150ms).  On a link that
     * has stopped answering it fails the far end safe, and a bench whose
     * panel is asking it to disarm and cannot be heard is one that should
     * fail safe.
     *
     * A release is not a disarm and does not do this: letting go of one pin
     * is not worth latching the far end's failsafe.
     */
    beat(arming_heartbeat(&s_arm, now_ms())
         && !atomic_load(&s_disarm_request));
}

/*
 * Everything the bench does, at a fixed 5ms on the core the renderer does
 * not use: touch, STOP, arming, the outputs, the link and the heartbeat.
 *
 * STOP is hit-tested here against the band's own rectangle rather than waiting
 * for the router to report a press, because the router only sees events when
 * a frame is drawn and a frame can cost 50ms.  app_main forwards the
 * router's own STOP as well, so a press this test misses still latches.
 */
void control_task(void *arg)
{
    (void)arg;

    telemetry_sim_t sim;
    bench_state_t   bench;
    /* Before anything that can start a run: a run's start sets its clock. */
    log_cadence_init(&s_log_cad, now_ms());
    control_setup(&sim, &bench);
    /* From here: the bench starts SAFE with the supply off, so the start-up
     * passes before the first frame stop nothing. */
    frame_watch_init(&s_frame_watch, atomic_load(&s_render_progress),
                     now_ms());

    uint32_t last_poll     = 0;
    uint32_t last_status   = 0;
    uint32_t last_report   = 0;
    uint32_t last_temp     = 0;
    bool     link_up       = false;

    for (;;) {
        /*
         * An install about to write the flash: this loop stands still.  No
         * poll and no exchange starts, and the heartbeat is withheld on
         * purpose, so the IOmcu goes to failsafe as it does at any
         * restart of the panel.  A flash erase stalls this core anyway; the
         * failsafe then comes from a decision rather than from the stall.
         */
        if (atomic_load(&s_fw_quiet_req)) {
            beat(false);
            atomic_store(&s_fw_quiet_ack, true);
            vTaskDelay(pdMS_TO_TICKS(CONTROL_PERIOD_MS));
            continue;
        }
        atomic_store(&s_fw_quiet_ack, false);

        control_pump();

        /*
         * The policy first, then the screens.
         *
         * A stop must not wait behind a command that talks to the link: an
         * unanswered exchange holds this loop for LINK_HOST_TIMEOUT_MS
         * (1000ms), and a queue with several such commands in it multiplies
         * that, with the far end armed the whole time.  control_pump() keeps
         * the heartbeat going during the wait but only records further stop
         * requests; acting on them is here.
         *
         * What made this ordering unsafe before was an arm queued from a
         * gesture made before the stop, which would clear the latch on the
         * next pass.  Each command now carries the count of stops its sender
         * had seen and an arm whose count is stale is dropped, so the stop
         * can be served first without an older arm undoing it.
         */
        kst_port_look();
        service_arming(link_up);
        supply_service();

        drain_commands(link_up, &bench);
        kst_commands();
        (void)outputs_keepalive(&s_out, PANEL_CH_THROTTLE, now_ms());
        servo_link_service(link_up);

        log_follow_runs();

        const float emitted =
            (float)outputs_actual(&s_out, PANEL_CH_THROTTLE) * 100.0f
            / (float)OUT_SPAN;

        /*
         * And the transaction clock, every pass.
         *
         * Defence rather than mechanism: exchange() releases the slot on the
         * way out of every path it has.  But its tick was the only one, and
         * a request left outstanding by any means at all would refuse every
         * later one from a place that could no longer run the timeout.  Here
         * the clock runs whatever the link is doing, so an outstanding
         * request is abandoned a second after it was sent and the next poll
         * gets its turn.
         */
        (void)link_host_tick(&s_host, now_ms());

        /* --- the far end, at 1Hz until it answers ----------------------- */
        bool new_sample = false;
        if (!cu_service(&link_up, &bench, &last_poll, &last_status)) {
            new_sample = poll_far_end(&link_up, &bench, &last_poll,
                                      &last_status);
        }

        advance_model_and_log(link_up, emitted, &sim, &bench, &new_sample);

        if ((uint32_t)(now_ms() - last_temp) >= 1000u) {
            last_temp = now_ms();
            tsens_read();
        }

        /* Every 5s while the link is down, every 60s while it is up. */
        if ((uint32_t)(now_ms() - last_report)
            >= (link_up ? 60000u : 5000u)) {
            last_report = now_ms();
            link_report();
        }

        /* --- hand the screen what it draws -------------------------------- */
        publish_snapshot(&bench, link_up, new_sample);
        /* And only now the look at the touch stream for an arm the render
         * side has not seen yet; see arm_watch_service(). */
        arm_watch_service(link_up);

        /* --- last, after every arm and ON this pass applied: a sector of
         * the drawing's keep, on a safe bench only ---------------------- */
        art_keep_service();

        vTaskDelay(pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}
