/*
 * The panel application.
 *
 * Bring the board up, report each step to the splash as it happens, then run
 * the router at the panel's own refresh rate.
 *
 * Everything this file does is glue.  What the screens decide, what the link
 * says, what the numbers mean and when the bench fails safe are all pure C in
 * shared/, tested on a laptop; this is the part that cannot be.
 */
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "board_pins.h"
#include "can_selftest.h"
#include "can_twai.h"
#include "display.h"
#include "gfx.h"
#include "heartbeat.h"
#include "link_frame.h"
#include "link_bringup.h"
#include "link_host.h"
#include "link_pages.h"
#include "link_uart.h"
#include "link_wire.h"
#include "log_writer.h"
#include "motor_screen.h"
#include "splash_screen.h"
#include "storage.h"
#include "telemetry_sim.h"
#include "throttle.h"
#include "touch.h"
#include "touch_map.h"
#include "ui_screen.h"
#include "ui_theme.h"

static const char *TAG = "rcbench";

/* Used during bring-up as well as in the loop, so file scope. */
static link_host_t    s_host;
static link_bringup_t s_bring;
static link_decoder_t s_rx;

/* Defined below; bring_up() asks who is there before the loop starts. */
static bool poll_page(link_host_t *host, link_decoder_t *rx, uint8_t page,
                      uint8_t count, link_msg_t *reply);

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ------------------------------------------------------------- the heartbeat */

/*
 * Driven from the loop that reads touch and draws STOP, which is the whole
 * reason it is a heartbeat rather than a level: what it asserts is not "the
 * operator permits this" but "the processor that owns the STOP button is
 * still going round its loop".  A level cannot say that, and neither can a
 * crashed panel.
 *
 * The monostable it is meant to gate is not fitted yet, so today these edges
 * reach a header pin and nothing else.  They are emitted anyway.  The
 * alternative -- holding the line low until the daughterboard exists -- means
 * the first time this code runs for real is the first time it matters, and it
 * costs one GPIO write per frame to have it already running and scopeable.
 */
static heartbeat_gen_t s_beat;

/* Latched by STOP; cleared only by an explicit arm.  See the loop. */
static bool s_stopped;

static void heartbeat_init(void)
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
    ESP_LOGI(TAG, "heartbeat on GPIO%d (J8) every %u ms; the monostable it "
                  "gates is not fitted, so nothing is listening yet",
             (int)PANEL_HEARTBEAT_PIN, (unsigned)HEARTBEAT_PERIOD_MS);
}

static void beat(bool alive)
{
    const bool level = heartbeat_gen_step(&s_beat, now_ms(), alive);
    gpio_set_level(PANEL_HEARTBEAT_PIN, level ? 1 : 0);
}

/* ------------------------------------------------------------------- boot */

static void pump(void)
{
    /* Draw between init steps so the splash fills in as it happens, rather
     * than appearing complete at the end. */
    gfx_canvas_t *c = display_canvas();
    if (c != NULL) {
        ui_router_render(c, display_back_index());
        display_flip();
    }
}

static bool bring_up(void)
{
    bool ok = true;

    splash_screen_set(SPLASH_STEP_BOARD, SPLASH_OK, "CH422G");

    display_config_t dcfg = DISPLAY_CONFIG_DEFAULT();
    if (display_init(&dcfg) == ESP_OK) {
        splash_screen_set(SPLASH_STEP_DISPLAY, SPLASH_OK, "800x480 39Hz");
    } else {
        splash_screen_set(SPLASH_STEP_DISPLAY, SPLASH_FAIL, "no panel");
        return false;   /* nothing can be reported after this */
    }
    pump();

    touch_config_t tcfg = TOUCH_CONFIG_DEFAULT();
    if (touch_init(&tcfg) == ESP_OK) {
        splash_screen_set(SPLASH_STEP_TOUCH, SPLASH_OK, "GT911 5pt");
    } else {
        /* A bench with no touch has no STOP button, so this is fatal rather
         * than degraded -- but it is reported first. */
        splash_screen_set(SPLASH_STEP_TOUCH, SPLASH_FAIL, "no answer");
        ok = false;
    }
    pump();

    /* A card is optional: the log viewer wants one, nothing else does, so a
     * missing card is a warning the operator reads on the way past rather
     * than a boot failure. */
    (void)storage_init();
    splash_screen_set(SPLASH_STEP_STORAGE,
                      storage_mounted() ? SPLASH_OK : SPLASH_WARN,
                      storage_status());
    pump();

    splash_screen_set(SPLASH_STEP_SETTINGS, SPLASH_OK, "defaults");
    pump();

    const bool link_open = (link_uart_init(PANEL_LINK_UART_NUM,
                                           LINK_BAUD_BRINGUP,
                                           PANEL_LINK_PIN_TX,
                                           PANEL_LINK_PIN_RX) == ESP_OK);
    splash_screen_set(SPLASH_STEP_LINK,
                      link_open ? SPLASH_OK : SPLASH_WARN,
                      link_open ? "256k 8N1" : "not opened");
    pump();

    /*
     * And ask who is there.
     *
     * This step existed and was never answered, so all_answered() was never
     * true, so the splash never handed over -- the panel sat on its own boot
     * screen for ever.  A step that is declared and never set is worse than
     * one that does not exist: the list looks complete and the machine waits.
     */
    link_host_init(&s_host, now_ms());
    link_decoder_reset(&s_rx);
    if (link_open) {
        link_msg_t reply;
        if (poll_page(&s_host, &s_rx, LINK_PAGE_IDENTITY, LINK_ID_COUNT,
                      &reply) && reply.op == LINK_OP_DATA) {
            /* Wide enough for three 16-bit registers plus the words: the
             * splash truncates its own detail field anyway, but a
             * truncating snprintf is a warning, and warnings are errors. */
            char detail[40];
            snprintf(detail, sizeof(detail), "proto %u.%u hw %u",
                     reply.regs[LINK_ID_PROTOCOL_MAJOR],
                     reply.regs[LINK_ID_PROTOCOL_MINOR],
                     reply.regs[LINK_ID_HARDWARE]);
            s_bring.have_identity = true;
            s_bring.proto_major   = reply.regs[LINK_ID_PROTOCOL_MAJOR];
            s_bring.proto_minor   = reply.regs[LINK_ID_PROTOCOL_MINOR];
            const bool speaks_ours =
                reply.regs[LINK_ID_PROTOCOL_MAJOR] == LINK_PROTOCOL_MAJOR;
            splash_screen_set(SPLASH_STEP_COPRO,
                              speaks_ours ? SPLASH_OK : SPLASH_FAIL, detail);
            ok = ok && speaks_ours;
        } else {
            /* Not a failure: the bench is useful without one, and says so. */
            splash_screen_set(SPLASH_STEP_COPRO, SPLASH_WARN, "no answer");
        }
    } else {
        splash_screen_set(SPLASH_STEP_COPRO, SPLASH_WARN, "no link");
    }
    pump();

    return ok;
}

/* ------------------------------------------------------------- the logger */

/*
 * A run is written while the bench is armed and closed when it disarms, which
 * is the only definition of "a run" the bench has.  The file is the one the
 * log viewer reads, and there is a host test that writes one and parses it
 * back rather than trusting that.
 */
static FILE       *s_log_file;
static log_writer_t s_log;
static float        s_log_t;

static int file_write(void *ctx, const void *data, size_t len)
{
    FILE *f = (FILE *)ctx;
    return (int)fwrite(data, 1, len, f);
}

static void log_start(void)
{
    if (s_log_file != NULL || !storage_mounted()) {
        return;
    }
    /* Numbered rather than timestamped: there is no clock on this board that
     * survives a power cycle, and a file called 1970-01-01 every time is
     * worse than one called 3. */
    for (int i = 1; i < 1000 && s_log_file == NULL; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "/sdcard/BENCH%03d.CSV", i);
        FILE *probe = fopen(path, "r");
        if (probe != NULL) {
            fclose(probe);
            continue;
        }
        s_log_file = fopen(path, "w");
        if (s_log_file != NULL) {
            ESP_LOGI(TAG, "logging to %s", path);
        }
    }
    if (s_log_file == NULL) {
        return;
    }
    const log_sink_t sink = { file_write, s_log_file };
    log_writer_init(&s_log, &sink);
    s_log_t = 0.0f;
}

static void log_stop(void)
{
    if (s_log_file == NULL) {
        return;
    }
    if (log_writer_failed(&s_log)) {
        ESP_LOGW(TAG, "the log is short: a write failed after %u rows",
                 (unsigned)s_log.rows);
    } else {
        ESP_LOGI(TAG, "%u rows written", (unsigned)s_log.rows);
    }
    fclose(s_log_file);
    s_log_file = NULL;
}

/* ---------------------------------------------------- asking the far end */

/**
 * One poll.  Returns true when the coprocessor answered.
 *
 * Deliberately blocking and deliberately short: the link is host-polled, so
 * there is never a second frame in flight, and the whole transaction is well
 * under one panel frame at either baud rate.
 */
static bool poll_page(link_host_t *host, link_decoder_t *rx, uint8_t page,
                      uint8_t count, link_msg_t *reply)
{
    uint8_t frame[LINK_MAX_FRAME];
    const size_t n = link_host_read(host, page, 0, count, frame,
                                    sizeof(frame));
    if (n == 0 || link_uart_write(frame, n) < 0) {
        return false;
    }
    /*
     * Measured from the last byte handed to the driver to the byte that
     * completes the reply, so it includes the far end's turnaround wait.
     * That is the number LINK_TURNAROUND_US has to be checked against, and it
     * cannot be got any other way than from the wire.
     */
    const uint32_t sent_us = (uint32_t)esp_timer_get_time();
    for (;;) {
        uint8_t buf[LINK_MAX_FRAME];
        const int got = link_uart_read(buf, sizeof(buf), 5);
        for (int i = 0; i < got; ++i) {
            if (link_decode_byte(rx, buf[i], reply)
                && link_host_reply(host, reply, now_ms())) {
                link_bringup_add_rtt(
                    &s_bring,
                    (uint32_t)((uint32_t)esp_timer_get_time() - sent_us));
                return true;
            }
        }
        if (link_host_tick(host, now_ms())) {
            /* The bytes of an abandoned reply are still on their way; letting
             * them reach the decoder is how a stale frame gets offered as the
             * answer to the next question. */
            link_uart_flush();
            link_decoder_reset(rx);
            return false;
        }
    }
}

/* ------------------------------------------------------------ CAN bring-up */

/*
 * The first thing to run when the two boards are wired together.
 *
 * Built in only when asked for, because starting it takes native USB away --
 * GPIO19 and GPIO20 carry both, and the multiplexer has to choose.  Build with
 *
 *     idf.py build -DRCBENCH_CAN_SELFTEST=1
 *
 * and watch the UART socket, not the USB one.  docs/Bringup.md has the rest.
 *
 * It answers one question and deliberately not two: do frames cross this bus
 * intact?  Nothing above the wire is involved, so a pass here and a link that
 * still does not work means the fault is in the protocol rather than the
 * cabling -- which is worth knowing before anybody unplugs anything.
 */
#ifdef RCBENCH_CAN_SELFTEST
static void can_selftest_run(uint32_t seconds)
{
    if (can_twai_start(PANEL_CAN_BITRATE) != ESP_OK) {
        ESP_LOGE(TAG, "CAN would not start; nothing to test");
        return;
    }

    can_selftest_t st;
    can_selftest_init(&st, 50);
    const uint32_t until = now_ms() + seconds * 1000u;

    while (now_ms() < until) {
        link_can_frame_t probe;
        if (can_selftest_probe(&st, now_ms(), &probe)) {
            const int64_t sent_us = esp_timer_get_time();
            if (!can_twai_send(&probe, 10)) {
                ESP_LOGW(TAG, "the transmit queue would not take a frame");
            }
            link_can_frame_t in;
            if (can_twai_recv(&in, 20)) {
                can_selftest_rx(&st, &in,
                                (uint32_t)(esp_timer_get_time() - sent_us));
            }
        }
        can_selftest_tick(&st, now_ms());
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    const can_selftest_verdict_t v = can_selftest_verdict(&st);
    ESP_LOGI(TAG, "CAN self-test: %s", can_selftest_text(v));
    if (v != CAN_SELFTEST_OK && v != CAN_SELFTEST_RUNNING) {
        ESP_LOGW(TAG, "  check: %s", can_selftest_hint(v));
    }
    ESP_LOGI(TAG, "  sent %lu echoed %lu corrupt %lu lost %lu stale %lu",
             (unsigned long)st.sent, (unsigned long)st.echoed,
             (unsigned long)st.corrupt, (unsigned long)st.timed_out,
             (unsigned long)st.stale);
    if (st.echoed > 0) {
        ESP_LOGI(TAG, "  round trip min %lu max %lu us",
                 (unsigned long)st.rtt_min_us, (unsigned long)st.rtt_max_us);
    }
    uint32_t tx_err = 0, rx_err = 0, bus_err = 0;
    bool bus_off = false;
    can_twai_errors(&tx_err, &rx_err, &bus_err, &bus_off);
    ESP_LOGI(TAG, "  controller tx_err %lu rx_err %lu bus_err %lu%s",
             (unsigned long)tx_err, (unsigned long)rx_err,
             (unsigned long)bus_err, bus_off ? "  BUS OFF" : "");

    can_twai_stop();
}
#endif /* RCBENCH_CAN_SELFTEST */

/*
 * One block, naming the most fundamental thing that is wrong rather than the
 * loudest.  Printed while the link is unhealthy and once a minute when it is,
 * because a bring-up wants it constantly and a working bench does not.
 */
static void link_report(void)
{
    s_bring.polls         = s_host.polls;
    s_bring.replies       = s_host.replies;
    s_bring.timeouts      = s_host.timeouts;
    s_bring.mismatches    = s_host.mismatches;
    s_bring.nacks         = s_host.nacks;
    s_bring.rx_crc_errors = s_rx.crc_errors;
    s_bring.rx_resyncs    = s_rx.resyncs;

    const link_diag_t d = link_bringup_diagnose(&s_bring);
    ESP_LOGI(TAG, "LINK %s", link_diag_text(d));
    if (d != LINK_DIAG_OK) {
        ESP_LOGW(TAG, "  check: %s", link_diag_hint(d));
    }
    ESP_LOGI(TAG, "  panel  polls %lu replies %lu timeouts %lu stale %lu "
                  "nack %lu crc %lu resync %lu",
             (unsigned long)s_bring.polls, (unsigned long)s_bring.replies,
             (unsigned long)s_bring.timeouts,
             (unsigned long)s_bring.mismatches, (unsigned long)s_bring.nacks,
             (unsigned long)s_bring.rx_crc_errors,
             (unsigned long)s_bring.rx_resyncs);
    if (s_bring.have_status) {
        ESP_LOGI(TAG, "  copro  frames %lu crc %lu resync %lu",
                 (unsigned long)s_bring.dev_frames,
                 (unsigned long)s_bring.dev_crc_errors,
                 (unsigned long)s_bring.dev_resyncs);
    } else {
        ESP_LOGI(TAG, "  copro  never answered a status read");
    }
    if (s_bring.rt_samples > 0) {
        /* Against LINK_TURNAROUND_US: if the minimum round trip is close to
         * it, the far end is waiting out the direction circuit and not the
         * wire, and the poll period is being spent on an RC network. */
        ESP_LOGI(TAG, "  round trip min %lu avg %lu max %lu us "
                      "(turnaround allowance %u us)",
                 (unsigned long)s_bring.rt_min_us,
                 (unsigned long)s_bring.rt_mean_us,
                 (unsigned long)s_bring.rt_max_us,
                 (unsigned)LINK_TURNAROUND_US);
    }
}

/*
 * The seam.
 *
 * When the coprocessor answers, its numbers are the numbers -- measured or
 * modelled, it says which and the flag travels with them.  When it does not,
 * the panel models locally and says so the same way.  Nothing above this
 * function knows the difference, which is the whole point of bench_state.
 */
static bool read_bench(link_host_t *host, link_decoder_t *rx,
                       bench_state_t *out)
{
    link_msg_t reply;
    if (!poll_page(host, rx, LINK_PAGE_BENCH, LINK_BN_COUNT, &reply)) {
        return false;
    }
    if (reply.op == LINK_OP_NACK) {
        return false;
    }
    bench_state_from_regs(out, reply.regs, reply.offset, reply.count);
    return true;
}

/* ------------------------------------------------------------------- main */

void app_main(void)
{
    ESP_ERROR_CHECK(board_init());
    heartbeat_init();
#ifdef RCBENCH_CAN_SELFTEST
    can_selftest_run(5);
#endif

    ui_theme_set(UI_THEME_DARK);
    ui_router_init();

    const bool healthy = bring_up();

    /*
     * No coprocessor answers yet, so the numbers come from the model -- and
     * every one of them carries LINK_BN_SIMULATED, which is what puts
     * SIMULATION across the screen.  When the far end starts answering, the
     * only line that changes is which of these two fills bench.
     */
    telemetry_sim_t sim;
    bench_state_t   bench;
    throttle_t      thr;
    memset(&bench, 0, sizeof(bench));
    telemetry_sim_init(&sim, NULL);
    throttle_init(&thr, NULL, now_ms());

    if (!healthy) {
        ui_router_set_alert("touch did not answer -- the bench will not arm");
    }

    uint32_t frames    = 0;
    uint32_t last_us   = (uint32_t)esp_timer_get_time();
    uint32_t last_poll = 0;
    uint32_t last_status = 0;
    uint32_t last_report = 0;
    uint32_t last_touch_ok = now_ms();
    bool     link_up = false;

    for (;;) {
        const uint32_t us = (uint32_t)esp_timer_get_time();
        const float dt_s = (float)(us - last_us) / 1e6f;
        last_us = us;

        /* --- touch, and the rule that outlives every screen --------------- */
        /*
         * touch_wait_event returns bool, not esp_err_t.  This was written as
         * `== ESP_OK` for a year, and ESP_OK is zero -- so the loop ran while
         * there was *no* event and stopped the moment one arrived.  Three
         * things followed from that one comparison:
         *
         *   - it never terminated, because the queue is empty most of the
         *     time, so the loop below it never ran and nothing was drawn
         *     after the first frame;
         *   - ui_router_event was handed uninitialised stack on every spin;
         *   - saw_touch was set on every spin, so last_touch_ok never aged
         *     and the disarm-on-dead-touch rule could not fire.
         *
         * The function has a timeout argument, which is what made it look
         * like an esp_err_t call.  It is not one.
         */
        touch_event_t evt;
        bool saw_touch = false;
        while (touch_wait_event(&evt, 0)) {
            saw_touch = true;
            ui_router_event(&evt);
        }
        /*
         * touch_age_ms is how long since the controller last answered a poll,
         * which is the question -- not how long since somebody touched it.
         * A panel nobody is touching is fine; a controller that has stopped
         * answering is not.
         */
        if (saw_touch || touch_age_ms() < 200u) {
            last_touch_ok = now_ms();
        }
        /*
         * The panel is the only place a STOP button exists, so a touch
         * controller that has stopped answering means the bench cannot be
         * stopped -- and a bench that cannot be stopped must not be armed.
         */
        const bool touch_dead =
            (uint32_t)(now_ms() - last_touch_ok) >= 500u;
        if (touch_dead && thr.armed) {
            throttle_arm(&thr, false, now_ms());
            ui_router_set_alert("touch stopped answering -- disarmed");
        }

        /*
         * STOP latches here rather than clearing on the next frame.  The
         * button's job is to stop the bench, and a stop that lasts one frame
         * is a stop the coprocessor may never have seen -- its monostable
         * holds for longer than a frame either way.  Clearing it is a
         * deliberate act: disarm and arm again.
         */
        if (ui_router_take_stop()) {
            throttle_arm(&thr, false, now_ms());
            motor_screen_set_armed(false);
            s_stopped = true;
        }


        /* --- what the bench screen asked for ----------------------------- */
        motor_cmd_t cmd;
        while (motor_screen_poll_cmd(&cmd)) {
            switch (cmd.kind) {
            case MOTOR_CMD_ARM:
                /* Arming is the deliberate act that clears a latched stop.
                 * Nothing else does: not navigating away, not the alert
                 * expiring, not the link coming back. */
                if (!touch_dead) {
                    s_stopped = false;
                    throttle_arm(&thr, true, now_ms());
                }
                break;
            case MOTOR_CMD_DISARM:   throttle_arm(&thr, false, now_ms()); break;
            case MOTOR_CMD_THROTTLE: throttle_set(&thr, cmd.value, now_ms()); break;
            case MOTOR_CMD_RESET_PEAKS: bench_state_reset_peaks(&bench); break;
            default: break;
            }
        }
        throttle_keepalive(&thr, now_ms());
        motor_screen_set_armed(thr.armed);

        const bool was_armed = (s_log_file != NULL);
        if (thr.armed && !was_armed) {
            log_start();
        } else if (!thr.armed && was_armed) {
            log_stop();
        }

        const float emitted = throttle_step(&thr, dt_s);
        /*
         * Deliberately not gated on the link.  The heartbeat says "the
         * processor that owns STOP is still running its loop"; whether the
         * two boards can still talk is a separate question with its own
         * watchdog at each end.  Folding them together would mean a dropped
         * frame of UART traffic cutting the safety line, and a safety line
         * that cries wolf is one people bypass.
         */
        beat(!touch_dead && !s_stopped);

        /* --- the numbers ------------------------------------------------- */
        /* Modelled here only while nothing is answering; when the link is up,
         * read_bench has already filled this with what the far end said. */
        if (!link_up) {
            telemetry_sim_step(&sim, emitted, dt_s, &bench);
        }
        motor_screen_push(&bench);
        if (s_log_file != NULL) {
            s_log_t += dt_s;
            (void)log_writer_row(&s_log, s_log_t, &bench);
        }

        /* --- the far end, at 1 Hz until it answers ----------------------- */
        if ((uint32_t)(now_ms() - last_poll) >= (link_up ? 50u : 1000u)) {
            last_poll = now_ms();
            link_msg_t reply;
            bool answered;
            if (link_up) {
                /* Ask for what is wanted every frame; identity is asked once
                 * and then only to notice the far end coming back. */
                answered = read_bench(&s_host, &s_rx, &bench);
            } else {
                answered = poll_page(&s_host, &s_rx, LINK_PAGE_IDENTITY,
                                     LINK_ID_COUNT, &reply);
                if (answered
                    && reply.regs[LINK_ID_PROTOCOL_MAJOR]
                           != LINK_PROTOCOL_MAJOR) {
                    /* Refusing to arm when the versions disagree is one
                     * register and some spine.  This is the register. */
                    ESP_LOGE(TAG, "coprocessor speaks protocol %u, we speak %u",
                             reply.regs[LINK_ID_PROTOCOL_MAJOR],
                             (unsigned)LINK_PROTOCOL_MAJOR);
                    throttle_arm(&thr, false, now_ms());
                    ui_router_set_alert("protocol mismatch -- will not arm");
                    answered = false;
                }
            }
            if (answered != link_up) {
                ESP_LOGI(TAG, "coprocessor %s",
                         answered ? "answered" : "went quiet");
            }
            link_up = answered;

            /*
             * The far end's own view, once a second.  Not every poll: it
             * costs a whole transaction and the numbers it carries move
             * slowly.  Without it the panel can see that something is wrong
             * and not which end of the cable it is at.
             */
            if (link_up
                && (uint32_t)(now_ms() - last_status) >= 1000u) {
                last_status = now_ms();
                link_msg_t st;
                if (poll_page(&s_host, &s_rx, LINK_PAGE_STATUS,
                              LINK_ST_COUNT, &st)
                    && st.op == LINK_OP_DATA) {
                    s_bring.have_status = true;
                    s_bring.dev_frames =
                        (uint32_t)st.regs[LINK_ST_FRAMES_LO]
                        | ((uint32_t)st.regs[LINK_ST_FRAMES_HI] << 16);
                    s_bring.dev_crc_errors = st.regs[LINK_ST_CRC_ERRORS];
                    s_bring.dev_resyncs    = st.regs[LINK_ST_RESYNCS];
                }
            }
        }

        /*
         * Every five seconds while it is unhealthy, once a minute when it is
         * not.  A bring-up wants this constantly; a bench that has been
         * running for an hour wants its log readable.
         */
        if ((uint32_t)(now_ms() - last_report)
            >= (link_up ? 60000u : 5000u)) {
            last_report = now_ms();
            link_report();
        }

        const ui_bench_status_t status = {
            .link_up     = link_up,
            .armed       = thr.armed,
            .faults      = 0,
            .run_seconds = now_ms() / 1000u,
            .mode        = link_up ? "LINK" : "SIM",
            .simulated   = bench_state_simulated(&bench),
        };
        ui_router_set_status(&status);

        if (splash_screen_done() && ui_router_current() == SCREEN_SPLASH) {
            ui_router_goto(SCREEN_OVERVIEW);
        }
        ui_router_tick(dt_s);

        gfx_canvas_t *c = display_canvas();
        const int64_t draw_start = esp_timer_get_time();
        ui_router_render(c, display_back_index());
        const uint32_t draw_us = (uint32_t)(esp_timer_get_time() - draw_start);
        display_flip();   /* blocks until the swap has taken effect */

        /*
         * DRAW is how long the frame took to paint; WAIT is how long the flip
         * then blocked.  A healthy frame is mostly WAIT -- the loop paced by
         * the panel.  DRAW climbing until WAIT reaches zero is the frame
         * budget being spent, and it is the number to look at before
         * believing anything about the frame rate.  The driver has counted
         * this since the beginning and nothing ever printed it.
         */
        if (++frames % 300u == 0u) {
            ESP_LOGI(TAG, "%.1f fps  DRAW %u us  WAIT %u us",
                     (double)display_fps(), (unsigned)draw_us,
                     (unsigned)display_last_wait_us());
        }
    }
}
