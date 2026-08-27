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
#include "display.h"
#include "gfx.h"
#include "link_frame.h"
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

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ------------------------------------------------------------- the heartbeat */

/*
 * Configured and deliberately not driven.
 *
 * It must be toggled by the loop that reads touch and draws STOP -- that is
 * the whole reason it is a heartbeat rather than a level.  That loop now
 * exists, and it still is not driven, because the coprocessor's monostable is
 * not wired and nothing downstream of it can move.  Emitting edges into
 * nothing would make the line look finished.
 *
 * The one line to change when the daughterboard arrives is in beat(), and it
 * is marked.
 */
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
    ESP_LOGW(TAG, "heartbeat on GPIO%d (J8) is held low: no coprocessor to "
                  "gate, so its outputs stay disabled",
             (int)PANEL_HEARTBEAT_PIN);
}

static void beat(bool alive)
{
    (void)alive;
    /* When the monostable is wired: gpio_set_level(PANEL_HEARTBEAT_PIN,
     * alive ? !level : 0) from here, and only from here. */
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

    if (link_uart_init(PANEL_LINK_UART_NUM, LINK_BAUD_BRINGUP,
                       PANEL_LINK_PIN_TX, PANEL_LINK_PIN_RX) == ESP_OK) {
        splash_screen_set(SPLASH_STEP_LINK, SPLASH_OK, "256k 8N1");
    } else {
        splash_screen_set(SPLASH_STEP_LINK, SPLASH_WARN, "not opened");
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
    for (;;) {
        uint8_t buf[LINK_MAX_FRAME];
        const int got = link_uart_read(buf, sizeof(buf), 5);
        for (int i = 0; i < got; ++i) {
            if (link_decode_byte(rx, buf[i], reply)
                && link_host_reply(host, reply, now_ms())) {
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

    link_host_t    host;
    link_decoder_t rx;
    link_host_init(&host, now_ms());
    link_decoder_reset(&rx);

    if (!healthy) {
        ui_router_set_alert("touch did not answer -- the bench will not arm");
    }

    uint32_t last_us   = (uint32_t)esp_timer_get_time();
    uint32_t last_poll = 0;
    uint32_t last_touch_ok = now_ms();
    bool     link_up = false;

    for (;;) {
        const uint32_t us = (uint32_t)esp_timer_get_time();
        const float dt_s = (float)(us - last_us) / 1e6f;
        last_us = us;

        /* --- touch, and the rule that outlives every screen --------------- */
        touch_event_t evt;
        bool saw_touch = false;
        while (touch_wait_event(&evt, 0) == ESP_OK) {
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

        if (ui_router_take_stop()) {
            throttle_arm(&thr, false, now_ms());
            motor_screen_set_armed(false);
        }

        /* --- what the bench screen asked for ----------------------------- */
        motor_cmd_t cmd;
        while (motor_screen_poll_cmd(&cmd)) {
            switch (cmd.kind) {
            case MOTOR_CMD_ARM:
                if (!touch_dead) {
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
        beat(!touch_dead);

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
                answered = read_bench(&host, &rx, &bench);
            } else {
                answered = poll_page(&host, &rx, LINK_PAGE_IDENTITY,
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
        ui_router_render(c, display_back_index());
        display_flip();   /* blocks until the swap has taken effect */
    }
}
