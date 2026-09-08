/*
 * The panel application.
 *
 * Brings the board up, reports each step to the splash as it happens, then
 * runs the router at the panel's refresh rate.
 *
 * This file is glue.  What the screens decide, what the link carries, what
 * the numbers mean and when the bench fails safe are pure C in shared/,
 * tested on the host; this file holds the parts that need the hardware.
 *
 * SPDX-License-Identifier: MIT
 */
#include <inttypes.h>
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "board.h"
#include "ui_band.h"
#include "board_pins.h"
#include "can_twai.h"
#include "busfault_screen.h"
#include "can_selftest.h"
#include "selftest.h"
#include "display.h"
#include "gfx.h"
#include "arming.h"
#include "art_flash_esp.h"
#include "art_fetch.h"
#include "art_store.h"
#include "heartbeat.h"
#include "link_bringup.h"
#include "link_host.h"
#include "link_pages.h"
#include "log_writer.h"
#include "motor_screen.h"
#include "servo_screen.h"
#include "outputs_screen.h"
#include "picker_screen.h"
#include "rcbench_version.h"
#include "settings.h"
#include "settings_screen.h"
#include "splash_screen.h"
#include "storage.h"
#include "telemetry_sim.h"
#include "outputs.h"

/*
 * The panel's throttle, as a channel in an output bank.
 *
 * The ramp is a proportion of travel per second rather than a percentage, so
 * the same number means the same speed on an output whose travel is not
 * measured in percent.
 */
#define PANEL_CH_THROTTLE     0u
#define PANEL_THROTTLE_RAMP   ((uint16_t)(OUT_SPAN * 55u / 100u))   /* 55 %/s */

/*
 * The servo bench's output: bank and wire channel 0, slot 0, on GP2.  The
 * panel owns these: the driver, the pin and the range travel on the OUTPUTS
 * and CHAN_CFG pages, and the coprocessor holds no servo-specific constant.
 */
#define SERVO_CH        0u
#define SERVO_SLOT      0u
#define SERVO_PIN       2u
/*
 * How often the servo's position is said again, against the far end's
 * OUT_DEFAULT_TIMEOUT_MS of 500 ms.  Five times the margin, and one
 * register on the wire each time.
 */
#define SERVO_HOLD_MS   100u
#define SERVO_MIN_US    1000u
#define SERVO_MAX_US    2000u

/* A pulse in microseconds as a proportion of the servo's own travel, which is
 * what the CHANNELS page carries.  Clamped, not wrapped, below the range, as
 * the coprocessor's conversion is. */
static uint16_t us_to_span(uint16_t us, uint16_t min_us, uint16_t max_us)
{
    if (max_us <= min_us || us <= min_us) {
        return 0u;
    }
    const uint32_t span = (uint32_t)(us - min_us) * LINK_CH_SPAN
                          / (uint32_t)(max_us - min_us);
    return (span > LINK_CH_SPAN) ? (uint16_t)LINK_CH_SPAN : (uint16_t)span;
}

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
#include "touch.h"
#include "touch_map.h"
#include "ui_screen.h"
#include "ui_theme.h"

static const char *TAG = "rcbench";

/* Used during bring-up as well as in the loop, so file scope. */
static link_host_t    s_host;
/* A link_cap_t bitmap from the coprocessor's identity page.  Zero until
 * something answers, which is also what it stays if nothing is fitted. */
static uint16_t       s_capabilities;
/* LINK_ST_FAULTS from the coprocessor's last status poll, shown in the band. */
static uint16_t       s_dev_faults;
static link_bringup_t s_bring;


/* Defined below; bring_up() asks who is there before the loop starts. */
static bool poll_page(link_host_t *host, uint8_t page, uint8_t count,
                      link_msg_t *reply);
static bool write_page(link_host_t *host, uint8_t page, uint8_t count,
                       const uint16_t *regs, link_msg_t *reply);
/* Paired with throttle_to_zero() in every path that stops the bench; defined
 * with the rest of the servo's wire handling. */
static void servo_let_go(void);
static void servo_service(bool link_up);
static void disarm_here(bool link_up);

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ------------------------------------------------------------ the CAN bus */

/*
 * How long the start-up echo test runs.
 *
 * A verdict needs CAN_SELFTEST_MIN_PROBES (8) probes and one probe is in
 * flight at a time, so with a far end that answers this is hundreds of
 * probes and with one that does not it is tens.  It is spent inside the
 * splash, which holds afterwards anyway, so it costs no start-up time an
 * operator waits through.
 */
#define CAN_SELFTEST_MS 1200u

static busfault_report_t s_busfault;
static bool              s_bus_ok = true;   /* until the test says otherwise */

/*
 * A link that was up and stopped, said on the panel rather than on a console.
 *
 * The panel has no console an operator can reach while the bench runs: the
 * native USB socket carries GPIO19 and GPIO20, which the multiplexer hands to
 * CAN about a second into boot, and the bridged socket is not wired to UART0
 * on every board (board_pins.h).  A fault that only a console can explain is
 * one nobody in the field can report, and this one took five rounds with a
 * tester to narrow down.
 *
 * Four seconds, not one: the link drops for a poll now and then, and a screen
 * that took over on every blip would be a screen operators learn to dismiss.
 */
#define LINK_LOST_SCREEN_MS 4000u

static uint32_t s_link_lost_ms;      /* when it went, 0 while it is up   */
static bool     s_link_lost_shown;   /* the screen has had its turn      */
/*
 * Two counts, because they answer two questions.  The per-outage one sits on
 * the screen beside "down N s" and says whether the recovery is working now;
 * the lifetime one goes in the file and says whether this has been happening
 * all session.  One number doing both would be read as the wrong one in at
 * least one of the two places.
 */
static uint32_t s_recoveries;        /* this outage                      */
static uint32_t s_recoveries_total;  /* since boot                       */

/* ------------------------------------------------------------ the heartbeat */

/*
 * Driven from the loop that reads touch and draws STOP.  The line asserts
 * that the processor owning the STOP button is running its loop, which a
 * level cannot assert and a crashed panel cannot fake.
 *
 * The monostable the line gates is on no board: the edges reach the J8
 * header pin and nothing else.  They are emitted regardless, at one GPIO
 * (general-purpose input/output) write per frame, so the line is running and
 * can be scoped before the daughterboard exists.
 */
static heartbeat_gen_t s_beat;


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
                  "gates is on no board",
             (int)PANEL_HEARTBEAT_PIN, (unsigned)HEARTBEAT_PERIOD_MS);
}

/* ------------------------------------------------------------ die temperature */

/*
 * The panel's own die, not the coprocessor's.  The two boards sit in
 * different air and run different loads, and this is the one the ESP32-S3
 * can measure without a wire.  Read once a second; the sensor is slow and
 * nothing on the screen changes faster.
 */
static temperature_sensor_handle_t s_tsens;
/* Read and published by the control task; app_main takes it from the
 * snapshot with everything else that crosses between the two. */
static float                       s_mcu_c = NAN;

static void tsens_init(void)
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

/* --------------------------------------------------- the two loops' plumbing */

/*
 * The bench and the screen run on separate cores.
 *
 * Touch, the outputs, the link and the heartbeat live in the control task at
 * a fixed 5 ms; drawing lives in app_main at whatever rate the panel manages.
 * A frame that costs 50 ms then delays what is shown and not what the bench
 * does, and STOP does not wait for a repaint.
 *
 * The heartbeat follows the control task because the control task owns STOP,
 * and the heartbeat's whole claim is that the loop owning STOP is running.
 *
 * The two share nothing directly.  Touch crosses one way and commands cross
 * the other, both as queues; the numbers the screen draws cross as a snapshot
 * under a mutex.  Screen state is static and single-threaded, so it is
 * touched only by app_main.
 */
#define CONTROL_PERIOD_MS 5u
#define TOUCH_Q_LEN       32
#define CMD_Q_LEN         16
#define SAMPLE_Q_LEN      8
#define ALERT_MAX         64

/*
 * The bench page is polled every 50 ms, so a sample is 1/20 s of plot.  The
 * motor screen scales its time axis by the same number; the two have to
 * agree or the axis lies about how long ago something happened.
 */
#define PANEL_SAMPLE_HZ   20.0f

typedef enum { PANEL_CMD_MOTOR = 0, PANEL_CMD_SERVO,
               PANEL_CMD_STOP, PANEL_CMD_OUTPUTS } panel_cmd_kind_t;

typedef struct {
    panel_cmd_kind_t kind;
    motor_cmd_t      motor;
    servo_cmd_t      servo;
    outbind_t        bind;   /**< PANEL_CMD_OUTPUTS: the protocols and their pins */
    /**
     * How many stops the sender had seen when it queued this.
     *
     * The queue and the stop latch cross between the two tasks
     * independently, so an arm can be queued from a gesture the sender
     * watched, and a stop be applied before that arm is drained: the arm
     * would then clear a latch that was set after it was asked for. An arm
     * carrying a count that is no longer current is out of date and is
     * dropped.
     */
    uint32_t         stops;
    /** And how many disarms; see s_disarms. */
    uint32_t         disarms;
} panel_cmd_t;

/*
 * What became of the last output-page write.
 *
 * The control task writes it and app_main hands it to the screen, because
 * screen state is app_main's alone.  Atomic rather than volatile, for the
 * reason this file already gives for s_stop_live: volatile orders nothing
 * between processors and promises no atomicity, and these two tasks are
 * pinned to different cores.
 */
static atomic_int s_outputs_result;

/*
 * What the coprocessor says its outputs are.
 *
 * Read from the far end rather than remembered here.  A binding describes
 * wiring, and this board is not the one the wires are in: a panel that kept a
 * configuration and pushed it would be applying it to whatever is on the
 * bench now.  The coprocessor keeps it in its own flash, and this asks.
 */
static outbind_t s_outputs_read;
static bool      s_outputs_read_fresh;    /* both under s_snap_lock */

/* The coprocessor's board identity, from the identity page at bring-up. */
static uint16_t  s_board;

/*
 * Fetching the board's photograph, a slice of a poll at a time.
 *
 * Two hundred kilobytes over a link that carries sixty-two bytes a
 * transaction is thousands of round trips.  Doing them in a loop would stop
 * the control task for the length of the transfer, which is the bug the
 * identity read had at a tenth of the scale; so a bounded slice of each poll
 * goes to it and the bench keeps its cadence.  The transfer takes longer in
 * wall clock than the link alone would need, and that is the trade: it
 * happens once per board and never again.
 */
#define ART_SLICE_MS 15u

static const art_flash_t *s_artflash;
static art_fetch_t        s_artfetch;
static uint8_t           *s_artbuf;
static uint16_t           s_artboard;
static bool               s_artbusy;

/*
 * Writing it down happens on a task of its own, not here.
 *
 * Erasing a slot and filling it is a quarter of a megabyte of flash: hundreds
 * of milliseconds during which nothing else runs on this task.  The control
 * task is the one that beats the safety line, and its ceiling is 150 ms -- so
 * a commit taken here would drop the heartbeat and the coprocessor would fail
 * safe, which is the interlock working and a bench that stops for a
 * photograph.
 *
 * The display stalls for the length of a flash operation whichever task takes
 * it, because the panel's bounce-buffer refill reads PSRAM through the cache
 * a flash operation closes.  Settings saves already cost that.  What must not
 * happen is the heartbeat missing its window, and a separate task is the
 * whole of the fix.
 */
static uint8_t     *s_keepbuf;      /* the keeper's, once handed over */
static art_entry_t  s_keepentry;
static volatile bool s_keeping;

static QueueHandle_t     s_touch_q;   /**< control task -> app_main */
static QueueHandle_t     s_cmd_q;     /**< app_main -> control task */
/*
 * One entry per bench sample, not a count of them.  A counter would lose the
 * samples a stalled renderer did not come back for, and the plot's time axis
 * would compress by exactly the frames it missed, which is the frame-rate
 * coupling this task exists to remove.  Full means the renderer is more than
 * SAMPLE_Q_LEN samples behind; the oldest is dropped, because a plot that is
 * 400 ms out of date is worth less than one that is current.
 */
static QueueHandle_t     s_sample_q;  /**< control task -> app_main */
static SemaphoreHandle_t s_snap_lock;

/* What the screen reads.  Written by the control task, copied by app_main. */
static struct {
    bench_state_t bench;
    bool          link_up;
    bool          armed;
    float         mcu_temp_c;
    bool          stopped;
    uint32_t      stops;
    uint16_t      faults;
    uint32_t      link_errors;
    uint32_t      run_seconds;
    char          alert[ALERT_MAX];
    bool          alert_pending;
} s_snap;

static void snap_lock(void)   { xSemaphoreTake(s_snap_lock, portMAX_DELAY); }
static void snap_unlock(void) { xSemaphoreGive(s_snap_lock); }

/* Called from the control task, which must not touch the router. */
static void control_alert(const char *text)
{
    snap_lock();
    snprintf(s_snap.alert, sizeof(s_snap.alert), "%s", text);
    s_snap.alert_pending = true;
    snap_unlock();
}

/*
 * The safety loop's own state, at file scope because control_pump() services
 * it from inside a link exchange as well as from the top of the control task.
 */
static outputs_t s_out;
/*
 * When the bench may be armed.  The policy lives in shared/safety/arming.c so
 * the host suite can hold it; this file drives it and acts on what it says.
 */
static arming_t  s_arm;

/* What the servo screen is holding, so it can be said again before the far
 * end times it out.  SERVO_CMD_NONE means nothing is being held. */
/* The stop count this task has already let go for; see service_arming(). */
static uint32_t    s_stops_served;

static servo_cmd_t s_servo_held;
static uint32_t    s_servo_next_ms;
/*
 * A slot the far end still has and this end has finished with.
 *
 * Kept as a debt rather than written and forgotten: a screen left while the
 * link is down cannot send the release, and the far end keeps both the slot
 * and the channel command through a failsafe.  A later arm -- from either
 * screen -- would then drive the servo to where it was before.
 */
static bool        s_servo_release_owed;
static uint16_t  s_throttle_hundredths;   /* 0..LINK_THROTTLE_MAX */

/*
 * A disarm returns the command to zero, here and in the output bank.
 *
 * The throttle used to survive a disarm, so the next arm wrote the previous
 * position to the control page in the same transaction that armed: the bench
 * went from stopped to whatever it was last set to, with the ramp on the far
 * side of the arm rather than in front of it.  An arm starts from nothing.
 */
static void throttle_to_zero(void)
{
    s_throttle_hundredths = 0u;
    (void)outputs_set(&s_out, PANEL_CH_THROTTLE, 0u, now_ms());
}
static bool      s_stop_press;
static uint8_t   s_stop_id;
/*
 * Both cross the two cores, so both are atomic rather than volatile: volatile
 * orders nothing between processors and promises no atomicity.
 *
 * s_stop_request carries the router's backstop for a press this task's own
 * hit test did not see, so it crosses from app_main.  A press this task sees
 * is not recorded but applied, in the pump that saw it: waiting for the
 * policy to run would wait out a link exchange with the far end driving.
 *
 * It is taken with an exchange rather than a test and a clear.  A stop
 * arriving between those two would have been dropped -- the read said "none",
 * the write then said "none" over the top of it.
 */
static atomic_bool s_stop_live;
static atomic_bool s_stop_request;
/* A disarm, and the servo screen's own release, as flags rather than queue
 * entries: neither may be lost to an eviction.  See send_cmd(). */
/* Set when the pump applied a stop for a press the router will also latch,
 * so the backstop does not stop the bench twice for one press. */
static atomic_bool s_stop_counted;
static atomic_bool s_disarm_request;
static atomic_bool s_servo_release_request;
/*
 * How many disarms have been asked for.
 *
 * The same job the stop count does: a disarm can be acted on between queue
 * entries, out of the order the queue holds, so a drive command still behind
 * it would otherwise put back what the disarm let go of.
 */
static atomic_uint s_disarms;
/* False until the control task owns the safety state; bring-up polls the
 * link before that, with nothing to service. */
static bool s_pump_live;

/*
 * Touch, STOP, the outputs and the heartbeat -- everything with a deadline,
 * and nothing that talks to the link.
 *
 * Called from the top of the control task and again from inside the wait in
 * exchange(), because that wait runs for up to LINK_HOST_TIMEOUT_MS (1000 ms)
 * and the heartbeat's ceiling is HEARTBEAT_MAX_GAP_MS (150 ms).  Without this
 * a single unanswered CAN reply drops the coprocessor's outputs and latches
 * its failsafe.  The loop that owns STOP really is still running during that
 * wait; this is what makes the line say so.
 */
static void control_pump(void)
{
    touch_event_t evt;
    bool saw_touch = false;
    while (touch_wait_event(&evt, 0)) {
        saw_touch = true;
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
            s_stop_press = true;
            s_stop_id    = evt.point.id;
        } else if (s_stop_press && evt.point.id == s_stop_id
                   && evt.type == TOUCH_EVENT_UP) {
            s_stop_press = false;
            if (in_stop) {
                /*
                 * Applied here, not recorded for later.
                 *
                 * This runs inside the link's wait as well as at the top of
                 * the loop, and a servo command makes up to three exchanges
                 * of up to LINK_HOST_TIMEOUT_MS (1000 ms) each.  A stop that
                 * only set a flag would wait all of that out with the far end
                 * driving, because the policy that acts on the flag is what
                 * is blocked.  arming_stop() takes effect in the same pass:
                 * arming_heartbeat() is false while stopped, so the beat at
                 * the end of this function stops asserting the line and the
                 * coprocessor fails safe within HEARTBEAT_MAX_GAP_MS
                 * (150 ms) whatever this task is waiting for.
                 *
                 * The rest of a stop -- the bank, the throttle, the servo's
                 * slot, telling the far end -- follows when the loop is free.
                 */
                arming_stop(&s_arm);
                counted_here = true;
            }
        }
        /* The screen still sees every event: it draws the press. */
        const bool routed = (xQueueSend(s_touch_q, &evt, 0) == pdTRUE);
        /*
         * The router will latch this same release and the backstop would
         * then stop the bench a second time, so a stop applied here is
         * marked -- but only if the event actually reached the router.  A
         * marker left standing for an event nobody saw is consumed by the
         * next stop the backstop really does have to apply, and that stop
         * would be ignored.
         */
        if (counted_here && routed) {
            atomic_store(&s_stop_counted, true);
        }
    }
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

    outputs_step(&s_out, now_ms());
    /*
     * Not gated on the link.  The heartbeat asserts that the processor owning
     * STOP is running its loop; whether the two boards can talk is a separate
     * question with its own watchdog at each end.  Gating on both would let a
     * dropped CAN frame cut the safety line.
     */
    beat(arming_heartbeat(&s_arm, now_ms()));
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

/*
 * How long to keep asking who is there: 3000 ms is about three attempts,
 * because a poll with no answer costs the 1000 ms host timeout.
 */
#define IDENTITY_WAIT_MS 3000u

/*
 * Ask the coprocessor who it is, repeatedly, for IDENTITY_WAIT_MS.
 *
 * The two boards do not finish booting at the same instant, and a first frame
 * onto a bus whose far end is not listening is retransmitted in silicon until
 * it is acknowledged, so a single attempt can miss a working coprocessor.
 * The wait is bounded: a bench with no coprocessor attached must not sit on
 * the splash for ever.
 */
static bool poll_identity(link_host_t *host, link_msg_t *reply)
{
    const uint32_t start = now_ms();
    do {
        if (poll_page(host, LINK_PAGE_IDENTITY, LINK_ID_COUNT, reply)
            && reply->op == LINK_OP_DATA) {
            return true;
        }
        pump();
    } while ((uint32_t)(now_ms() - start) < IDENTITY_WAIT_MS);
    return false;
}

static bool bring_up(void)
{
    bool ok = true;

    /* The panel's own build, on the first line it draws.  It is the host, so
     * it publishes no identity page; without this the only version anywhere
     * on the bench would be the coprocessor's. */
    splash_screen_set(SPLASH_STEP_BOARD, SPLASH_OK,
                      "CH422G fw " RCBENCH_VERSION_STRING);

    /*
     * Schema defaults, then the values the NVS (non-volatile storage) store
     * holds on top of them.  A store that cannot be opened leaves the
     * defaults in place: a working bench with unsaved settings.
     *
     * Done before the panel starts scanning, and reported at its place in the
     * list below.  Two reasons.  The stored theme is in force for the first
     * frame drawn rather than from the second onwards.  And the main flash is
     * quiet while the RGB (red, green, blue) panel scans: the panel's
     * interrupt handler copies the framebuffer out of PSRAM (pseudo-static
     * random-access memory) through the external memory cache, and a flash
     * operation closes that cache.  The handler then faults on PSRAM and the
     * core panics with `Cache disabled but cached memory region accessed`.
     */
    const settings_store_t *store = settings_nvs_store();
    settings_set_store(store);
    settings_init();
    settings_apply_ui();

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

    /* Loaded above, before the panel started scanning. */
    splash_screen_set(SPLASH_STEP_SETTINGS,
                      store != NULL ? SPLASH_OK : SPLASH_WARN,
                      store != NULL ? "NVS" : "NVS unavailable");
    pump();

    /*
     * CAN (Controller Area Network), and from here on there is no native USB
     * (Universal Serial Bus): GPIO19 and GPIO20 carry both, and the
     * multiplexer selects one.  The console is on UART0 (universal
     * asynchronous receiver-transmitter 0) for that reason.
     */
    const bool link_open = (can_twai_start(PANEL_CAN_BITRATE) == ESP_OK);
    splash_screen_set(SPLASH_STEP_LINK,
                      link_open ? SPLASH_OK : SPLASH_WARN,
                      link_open ? "CAN 1 Mbit/s" : "not opened");
    /*
     * After the bus is up, so the echo test measures the bus rather than
     * reporting "nothing came back" regardless of the hardware, and before
     * the identity poll, so a broken bus is diagnosed as such rather than as
     * an identity that never answers.
     *
     * It runs at every start-up.  A bus that does not carry frames looks
     * exactly like a coprocessor that is not fitted, and both look like a
     * bench that shows no numbers; nothing else the panel does separates
     * them, and an operator with no console cannot.
     */
    if (link_open) {
        s_bus_ok = can_selftest_run(CAN_SELFTEST_MS, &s_busfault);
        splash_screen_set(SPLASH_STEP_LINK,
                          s_bus_ok ? SPLASH_OK : SPLASH_FAIL,
                          s_bus_ok ? "CAN 1 Mbit/s"
                                   : can_selftest_text(s_busfault.verdict));
        if (!s_bus_ok) {
            busfault_screen_set(&s_busfault);
        }
    }
    pump();

    /*
     * Ask who is there.  The IOMCU step is set to a result on every path: a
     * splash step that is declared and never set keeps all_answered() false,
     * and the splash never hands over.
     */
    /* The partition the photographs live in, looked up once. */
    s_artflash = art_flash_esp();

    link_host_init(&s_host, now_ms());
    if (link_open) {
        link_msg_t reply;
        if (poll_identity(&s_host, &reply)) {
            /*
             * 48 bytes holds five 16-bit registers plus the words.  The
             * splash truncates its detail field anyway, but a truncating
             * snprintf is a compiler warning, and warnings are errors.
             *
             * The far end's firmware version is printed as well as the
             * protocol it speaks.  Two boards can speak the same protocol
             * and be different builds, and "which one is on the bench" is
             * the question a bring-up line exists to answer.
             */
            char detail[48];
            snprintf(detail, sizeof(detail), "proto %u.%u fw %u.%u.%u",
                     (unsigned)reply.regs[LINK_ID_PROTOCOL_MAJOR],
                     (unsigned)reply.regs[LINK_ID_PROTOCOL_MINOR],
                     (unsigned)reply.regs[LINK_ID_FIRMWARE_MAJOR],
                     (unsigned)reply.regs[LINK_ID_FIRMWARE_MINOR],
                     (unsigned)reply.regs[LINK_ID_FIRMWARE_PATCH]);
            s_bring.have_identity = true;
            /*
             * What the far end can do.  Read here, at bring-up, rather than
             * at a later poll: a menu that greys itself only after a poll
             * shows a state in which the bench claims more than it has.
             */
            s_capabilities        = reply.regs[LINK_ID_CAPABILITIES];
            /*
             * Which board answered.  Everything the outputs screen offers is
             * that board's, and a board this build does not know offers
             * nothing -- guessing a pin map is how an output ends up on the
             * safety line.
             */
            s_board               = reply.regs[LINK_ID_HARDWARE];
            s_bring.proto_major   = reply.regs[LINK_ID_PROTOCOL_MAJOR];
            s_bring.proto_minor   = reply.regs[LINK_ID_PROTOCOL_MINOR];
            const bool speaks_ours =
                reply.regs[LINK_ID_PROTOCOL_MAJOR] == LINK_PROTOCOL_MAJOR;
            splash_screen_set(SPLASH_STEP_IOMCU,
                              speaks_ours ? SPLASH_OK : SPLASH_FAIL, detail);
            ok = ok && speaks_ours;
        } else {
            /*
             * Not a failure: the bench runs without a coprocessor.  The two
             * words name the kind of silence: nothing arriving and a reply
             * that is rejected are different faults, and the poller counts
             * which happened.
             */
            const char *why = "no answer";
            if (s_host.nacks > 0) {
                why = "refused";
            } else if (s_host.mismatches > 0) {
                why = "wrong reply";
            }
            splash_screen_set(SPLASH_STEP_IOMCU, SPLASH_WARN, why);
        }
    } else {
        splash_screen_set(SPLASH_STEP_IOMCU, SPLASH_WARN, "no link");
    }
    pump();

    return ok;
}

/* ------------------------------------------------------------- the logger */

/*
 * A run is written while the bench is armed and closed when it disarms; that
 * is the bench's definition of a run.  The file format is the one the log
 * viewer reads, and a host test writes a run and parses it back.
 */
static FILE       *s_log_file;

/*
 * Whether a run is open, which is not the same as whether a file is.
 *
 * A card that is full or unwritable leaves s_log_file NULL, and the arming
 * edge was read from that pointer: with the bench armed and the open
 * failing, every pass of the control loop looked like a fresh arm and ran
 * the whole scan again -- card work on the task that drives the heartbeat,
 * once per CONTROL_PERIOD_MS, for as long as the bench stayed armed. The run
 * is its own flag, so a failed open is a run without a log rather than a
 * retry.
 */
static bool        s_log_run;

/*
 * Where the numbering got to.  The scan is a linear probe from 1, so a card
 * holding 400 runs cost 400 opens at the arming edge; after the first one it
 * starts from what it found.
 */
static int         s_log_next = 1;
static log_writer_t s_log;
static float        s_log_t;

static int file_write(void *ctx, const void *data, size_t len)
{
    FILE *f = (FILE *)ctx;
    return (int)fwrite(data, 1, len, f);
}

/*
 * Open the run's file.  Called on the arming edge, from the task that drives
 * the heartbeat and reads STOP.
 *
 * Every probe is a card transaction, and the loop can make hundreds of them.
 * The heartbeat's ceiling is HEARTBEAT_MAX_GAP_MS (150 ms) and STOP has to
 * be seen within a frame of the press, so control_pump() runs between
 * probes -- the same reason exchange() pumps while it waits for a reply.
 */
static void log_start(void)
{
    if (s_log_file != NULL) {
        return;
    }
    /*
     * Said, not swallowed, and said on the panel rather than to a console.
     *
     * The retry is gated on the run now, so a card that is not there at the
     * arming edge means this run is not recorded and nothing tries again
     * until the next arm.  That is the right behaviour -- polling a missing
     * card from the task that drives the heartbeat is what this change is
     * removing -- but it has to be visible, and the panel's console is not
     * reachable on every bench.
     */
    if (!storage_mounted()) {
        ESP_LOGW(TAG, "no card mounted; this run is not recorded");
        control_alert("no card -- this run is not recorded");
        return;
    }
    /* Numbered, not timestamped: no clock on this board survives a power
     * cycle, so every file would be dated 1970-01-01. */
    for (int i = s_log_next; i < 1000 && s_log_file == NULL; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "/sdcard/BENCH%03d.CSV", i);
        control_pump();
        FILE *probe = fopen(path, "r");
        if (probe != NULL) {
            fclose(probe);
            continue;
        }
        s_log_file = fopen(path, "w");
        if (s_log_file != NULL) {
            s_log_next = i + 1;
            ESP_LOGI(TAG, "logging to %s", path);
        }
    }
    if (s_log_file == NULL) {
        ESP_LOGW(TAG, "no log file could be opened; the run is not recorded");
        control_alert("card full or unwritable -- run not recorded");
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

/*
 * Put a built request on the wire and wait for its answer.  Blocking, and
 * short: the link is host-polled, so there is never a second request in
 * flight, and a transaction is well under one panel frame.
 *
 * Reads and writes share everything after the request is built, including
 * two failure modes: a frame that never reached the wire has nothing to wait
 * for and must release the outstanding slot, and a reply wider than four
 * registers arrives in pieces that each carry their own offset.
 */
static bool exchange(link_host_t *host, const link_msg_t *req,
                     link_msg_t *reply)
{
    link_can_frame_t out[LINK_CAN_MAX_FRAMES];
    const size_t n = link_can_encode(req, out, LINK_CAN_MAX_FRAMES);
    if (n == 0) {
        link_host_abandon(host);
        return false;
    }
    const uint32_t sent_us = (uint32_t)esp_timer_get_time();
    for (size_t i = 0; i < n; ++i) {
        if (!can_twai_send(&out[i], 5)) {
            /* Nothing reached the wire, so there is nothing to wait for.
             * Leaving it outstanding would refuse every later request. */
            link_host_abandon(host);
            return false;
        }
    }

    /*
     * No sequence to follow and no continuation timer: the host knows what it
     * asked for, and that is the only state there is.
     */
    for (;;) {
        link_can_frame_t in;
        if (can_twai_recv(&in, 5)) {
            link_msg_t part;
            if (link_can_decode(&in, &part)
                && link_host_accept(host, &part, now_ms(), reply)) {
                link_bringup_add_rtt(
                    &s_bring,
                    (uint32_t)((uint32_t)esp_timer_get_time() - sent_us));
                return true;
            }
        }
        /*
         * This wait runs to LINK_HOST_TIMEOUT_MS.  The safety loop cannot
         * stop for that long, so it runs here too: one pump per 5 ms receive
         * window keeps the heartbeat inside its 150 ms ceiling and STOP
         * inside a frame of the press.  Only during bring-up, before the
         * control task exists, is there nothing to pump.
         */
        if (s_pump_live) {
            control_pump();
        }
        /*
         * The request's own timeout is the only thing that ends this wait.
         *
         * link_host_tick() answers true for two different facts: this
         * request has been outstanding too long, and the link as a whole has
         * gone quiet.  Only the first one releases the outstanding slot, and
         * leaving on the second left the transaction pending for ever --
         * link_host_read() then refused every later request, this function
         * was never entered again, and tick() lives only in here, so the
         * timeout that would have cleared it could not run.  The link stayed
         * down until the panel was switched off, with polls frozen and not
         * one timeout counted.
         *
         * The far end had gone quiet for a second, which is exactly when the
         * second fact fires first: it is measured from the last reply and
         * this request was sent after it.
         */
        (void)link_host_tick(host, now_ms());
        if (!link_host_pending(host)) {
            return false;
        }
    }
}

static bool poll_page(link_host_t *host, uint8_t page, uint8_t count,
                      link_msg_t *reply)
{
    link_msg_t req;
    if (!link_host_read(host, page, 0, count, now_ms(), &req)) {
        return false;
    }
    return exchange(host, &req, reply);
}

static bool write_regs(link_host_t *host, uint8_t page, uint8_t offset,
                       uint8_t count, const uint16_t *regs, link_msg_t *reply)
{
    link_msg_t req;
    if (!link_host_write(host, page, offset, count, regs, now_ms(), &req)) {
        return false;
    }
    return exchange(host, &req, reply);
}

static bool write_page(link_host_t *host, uint8_t page, uint8_t count,
                       const uint16_t *regs, link_msg_t *reply)
{
    return write_regs(host, page, 0, count, regs, reply);
}

/* --------------------------------------------------- the board photograph */

static void art_keep_task(void *arg)
{
    (void)arg;
    if (art_store_put(s_artflash, s_keepentry.board, &s_keepentry, s_keepbuf)) {
        ESP_LOGI(TAG, "hardware %u's photograph kept",
                 (unsigned)s_keepentry.board);
    } else {
        ESP_LOGW(TAG, "hardware %u's photograph could not be kept; it will be "
                      "fetched again", (unsigned)s_keepentry.board);
    }
    heap_caps_free(s_keepbuf);
    s_keepbuf = NULL;
    s_keeping = false;
    vTaskDelete(NULL);
}

/*
 * The picker asking for a board's photograph, on the way into the screen.
 *
 * Read rather than mapped: art_store_read() checks the payload against the
 * checksum the header carries, so flash that decayed since it was written is
 * a board drawn from its outline rather than a picture that is quietly
 * wrong.
 *
 * One buffer, freed when the next photograph is asked for rather than when
 * the screen closes -- so it outlives a visit and is reused by the following
 * one. Two hundred kilobytes of PSRAM held between visits is worth less than
 * a release path that has to be got right; what must not happen is two of
 * them, and asking always frees before it allocates.
 */
static uint8_t *s_artshow;

static void art_for_picker(uint16_t board)
{
    picker_screen_set_artwork(NULL, 0, 0);
    if (s_artshow != NULL) {
        heap_caps_free(s_artshow);
        s_artshow = NULL;
    }
    art_entry_t e;
    if (s_artflash == NULL || !art_store_find(s_artflash, board, &e)) {
        return;
    }
    s_artshow = heap_caps_malloc(e.bytes, MALLOC_CAP_SPIRAM);
    if (s_artshow == NULL) {
        ESP_LOGW(TAG, "no room to show hardware %u's photograph",
                 (unsigned)board);
        return;
    }
    if (!art_store_read(s_artflash, board, s_artshow, e.bytes)) {
        ESP_LOGW(TAG, "hardware %u's kept photograph did not check out",
                 (unsigned)board);
        heap_caps_free(s_artshow);
        s_artshow = NULL;
        return;
    }
    picker_screen_set_artwork((const gfx_color_t *)s_artshow, e.width,
                              e.height);
}

static void art_stop(const char *why)
{
    if (s_artbuf != NULL) {
        heap_caps_free(s_artbuf);
        s_artbuf = NULL;
    }
    if (s_artbusy && why != NULL) {
        ESP_LOGW(TAG, "gave up on hardware %u's photograph: %s",
                 (unsigned)s_artboard, why);
    }
    s_artbusy = false;
}

/*
 * Ask for the picture, unless it is already kept or there is none.
 *
 * Runs on the link-up edge and does one transaction: everything after this
 * happens a slice at a time.  A coprocessor built before the page answers
 * NACK, which is not a fault -- the board is then drawn from its shape.
 */
/*
 * How the fetch reaches the far end.  The sequence itself is in
 * shared/artwork, where the suite runs it against a device in memory and can
 * stop the link at a chosen transaction; this is the two calls it needs.
 */
static bool art_link_read(void *ctx, uint8_t page, uint8_t count,
                          uint16_t *regs)
{
    (void)ctx;
    link_msg_t r;
    if (!poll_page(&s_host, page, count, &r) || r.op != LINK_OP_DATA) {
        return false;
    }
    for (uint8_t i = 0; i < count; ++i) {
        regs[i] = r.regs[i];
    }
    return true;
}

static bool art_link_write(void *ctx, uint8_t page, uint8_t reg,
                           uint16_t value)
{
    (void)ctx;
    link_msg_t ack;
    return write_regs(&s_host, page, reg, 1u, &value, &ack)
           && ack.op != LINK_OP_NACK;
}

static const art_transport_t s_art_link = {
    art_link_read, art_link_write, NULL,
};

/*
 * Ask for the picture, unless it is already kept or there is none.
 *
 * Runs on the link-up edge and does one transaction; everything after this
 * happens a slice at a time.  A coprocessor built before the page answers
 * NACK, which is not a fault -- the board is then drawn from its shape.
 */
static void art_begin(uint16_t board)
{
    art_stop(NULL);
    s_artboard = board;

    if (s_keeping) {
        return;         /* the last one is still being written down */
    }

    art_entry_t kept;
    if (s_artflash != NULL && art_store_find(s_artflash, board, &kept)) {
        ESP_LOGI(TAG, "hardware %u's photograph is already kept (%u x %u)",
                 (unsigned)board, (unsigned)kept.width, (unsigned)kept.height);
        return;
    }

    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 0u;
    const char *why = "";
    if (!art_fetch_meta(&s_art_link, meta, &bytes, &why)) {
        /* Three different things end here -- no picture, no page, and a page
         * that disagrees with itself -- and only the last is a fault. The
         * line says which rather than reporting all three as the first. */
        ESP_LOGI(TAG, "no photograph from hardware %u: %s", (unsigned)board,
                 why);
        return;
    }
    if (s_artflash != NULL && bytes > art_store_capacity(s_artflash)) {
        ESP_LOGW(TAG, "hardware %u's photograph is %u bytes and a slot holds "
                      "%u; not fetching it", (unsigned)board, (unsigned)bytes,
                 (unsigned)art_store_capacity(s_artflash));
        return;         /* ten seconds of link for nowhere to put it */
    }

    /* PSRAM: two hundred kilobytes is not internal memory's to spare, and
     * the buffer lives only for the transfer. */
    s_artbuf = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (s_artbuf == NULL) {
        ESP_LOGW(TAG, "no room for a %u byte photograph", (unsigned)bytes);
        return;
    }
    if (!art_fetch_begin(&s_artfetch, meta, s_artbuf, bytes)) {
        ESP_LOGW(TAG, "hardware %u: %s", (unsigned)board,
                 art_fetch_why(&s_artfetch));
        art_stop(NULL);
        return;
    }
    s_artbusy = true;
    ESP_LOGI(TAG, "fetching hardware %u's photograph: %u x %u, %u bytes",
             (unsigned)board, (unsigned)art_fetch_width(&s_artfetch),
             (unsigned)art_fetch_height(&s_artfetch), (unsigned)bytes);
}

/* It arrived and it is the picture it claimed.  Hand it to the keeper. */
static void art_finish(void)
{
    s_artbusy = false;
    if (s_artflash == NULL) {
        ESP_LOGW(TAG, "hardware %u's photograph arrived; nowhere to keep it, "
                      "so it will be fetched again", (unsigned)s_artboard);
        art_stop(NULL);
        return;
    }
    s_keepentry.board  = s_artboard;
    s_keepentry.width  = art_fetch_width(&s_artfetch);
    s_keepentry.height = art_fetch_height(&s_artfetch);
    s_keepentry.crc    = art_fetch_crc(&s_artfetch);
    s_keepentry.bytes  = art_fetch_bytes(&s_artfetch);
    /*
     * Hand the buffer over and stop owning it.  Below the renderer and on
     * the other core: it is a long flash operation and nothing waits on it.
     */
    s_keepbuf = s_artbuf;
    s_artbuf  = NULL;
    s_keeping = true;
    if (xTaskCreatePinnedToCore(art_keep_task, "artkeep", 4096, NULL,
                                2, NULL, 0) != pdPASS) {
        ESP_LOGW(TAG, "no task to keep hardware %u's photograph",
                 (unsigned)s_artboard);
        heap_caps_free(s_keepbuf);
        s_keepbuf = NULL;
        s_keeping = false;
    }
}

/*
 * One bounded slice of the fetch.
 *
 * A block at a time until the slice is spent: this task also beats the
 * safety line, and its ceiling is 150 ms.  The sequence does exactly the
 * blocks it is given, so how much of a poll goes to a photograph is decided
 * here and nowhere else.
 */
static void art_slice(void)
{
    const uint32_t began = now_ms();
    while (s_artbusy && (uint32_t)(now_ms() - began) < ART_SLICE_MS) {
        switch (art_fetch_step(&s_artfetch, &s_art_link, 1u)) {
        case ART_FETCH_RUNNING:
            break;
        case ART_FETCH_DONE:
            art_finish();
            return;
        default:
            art_stop(art_fetch_why(&s_artfetch));
            return;
        }
    }
}

/* ------------------------------------------------------- the control page */

/*
 * ARM and THROTTLE travel together, offset 0 and count 2, at every poll while
 * the link is up: the coprocessor's throttle channel stops driving after
 * OUT_DEFAULT_TIMEOUT_MS (500 ms) without a write, so the panel keeps writing
 * while armed.  CLEAR (register 2) is written on its own and only on an
 * explicit arm: a write that touches it must carry LINK_CLEAR_MAGIC, and it
 * lifts a latched failsafe, which no other write may do.
 */

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

/* True when the coprocessor acknowledged; a NACK (negative acknowledge) or
 * no answer is false. */
static bool control_write(bool armed, link_msg_t *reply)
{
    const uint16_t regs[2] = { armed ? 1u : 0u, s_throttle_hundredths };
    return write_regs(&s_host, LINK_PAGE_CONTROL, LINK_CT_ARM, 2u, regs, reply)
           && reply->op == LINK_OP_ACK;
}

/*
 * The magnet count of the motor under test, sent once when the coprocessor
 * answers.
 *
 * A bidirectional DShot ESC (electronic speed controller) reports electrical
 * periods and has no idea what it is bolted to, so this is the one number the
 * far end cannot work out and the near end already has, in the Motor poles
 * setting.  Until it arrives the coprocessor reports no speed at all rather
 * than a speed derived from a guess.
 */
static bool control_write_poles(link_msg_t *reply)
{
    const uint16_t poles = (uint16_t)settings_get_int(SET_MOTOR_POLES);
    return write_regs(&s_host, LINK_PAGE_CONTROL, LINK_CT_MOTOR_POLES, 1u,
                      &poles, reply)
           && reply->op == LINK_OP_ACK;
}

static bool control_clear_failsafe(link_msg_t *reply)
{
    const uint16_t magic = LINK_CLEAR_MAGIC;
    return write_regs(&s_host, LINK_PAGE_CONTROL, LINK_CT_CLEAR, 1u, &magic,
                      reply)
           && reply->op == LINK_OP_ACK;
}


/*
 * One block naming the most fundamental fault rather than the loudest.
 * Printed every 5 s while the link is down and every 60 s while it is up.
 */
/*
 * The same report, appended to the card.
 *
 * A tester can send a file; a tester cannot send a console this board does
 * not have.  Only while the link is down and only at link_report()'s 5 s
 * cadence, so the volume is a few hundred bytes a minute, and the write is
 * SPI to the card rather than internal flash -- it does not close the cache
 * the way a settings save does.
 */
static void debug_log(const char *line)
{
    if (!storage_mounted()) {
        return;
    }
    FILE *f = fopen("/sdcard/RCBENCH.LOG", "a");
    if (f == NULL) {
        return;
    }
    fputs(line, f);
    fputc('\n', f);
    fclose(f);
}

/* A number, or "?" when it could not be read: the column keeps its place. */
static const char *u32(char *out, size_t n, uint32_t v)
{
    snprintf(out, n, "%lu", (unsigned long)v);
    return out;
}

/* What can_twai_recover() found, in the screen's vocabulary. */
static busfault_bus_t bus_state(void)
{
    switch (can_twai_health()) {
    case CAN_TWAI_RUNNING:    return BUSFAULT_BUS_RUNNING;
    case CAN_TWAI_RECOVERING: return BUSFAULT_BUS_RECOVERING;
    case CAN_TWAI_STOPPED:    return BUSFAULT_BUS_STOPPED;
    case CAN_TWAI_BUS_OFF:    return BUSFAULT_BUS_OFF;
    case CAN_TWAI_UNKNOWN:
    default:                  return BUSFAULT_BUS_UNKNOWN;
    }
}

/*
 * What the panel knows about a link that stopped, for the screen and for the
 * card.  Everything in it is read here rather than remembered, so a
 * photograph of the screen and a line in the file describe the same moment.
 */
static void link_lost_report(busfault_report_t *r)
{
    memset(r, 0, sizeof(*r));
    r->kind       = BUSFAULT_LINK_LOST;
    r->bus        = bus_state();
    r->down_s     = s_link_lost_ms == 0u
                        ? 0u
                        : (uint32_t)(now_ms() - s_link_lost_ms) / 1000u;
    r->recoveries = s_recoveries;
    r->polls      = s_host.polls;
    r->timeouts   = s_host.timeouts;
    (void)can_twai_errors(&r->tx_errors, &r->rx_errors, &r->bus_errors,
                          &r->bus_off);
}

static void link_report(void)
{
    s_bring.polls         = s_host.polls;
    s_bring.replies       = s_host.replies;
    s_bring.timeouts      = s_host.timeouts;
    s_bring.mismatches    = s_host.mismatches;
    s_bring.nacks         = s_host.nacks;
    /*
     * Left at zero.  link_bringup reads rx_crc_errors as "frames this end
     * received and found corrupt", and TWAI (Two-Wire Automotive Interface,
     * the ESP32-S3's CAN controller) has no such counter: bus_error_count is
     * cumulative for the life of the driver and includes transmit-side
     * acknowledge errors, so one missing ACK (acknowledge) at power-on would
     * pin the diagnosis at "frames arrive corrupt" for the whole session.
     * The controller's own counters are printed below under their own names.
     */
    s_bring.rx_crc_errors = 0;
    s_bring.rx_resyncs    = 0;

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
        ESP_LOGI(TAG, "  iomcu  frames %lu crc %lu resync %lu",
                 (unsigned long)s_bring.dev_frames,
                 (unsigned long)s_bring.dev_crc_errors,
                 (unsigned long)s_bring.dev_resyncs);
    } else {
        ESP_LOGI(TAG, "  iomcu  never answered a status read");
    }
    if (s_bring.rt_samples > 0) {
        /*
         * The round trip as measured.  CAN arbitrates rather than taking
         * turns, so there is no turnaround allowance to compare it against.
         */
        ESP_LOGI(TAG, "  round trip min %lu avg %lu max %lu us",
                 (unsigned long)s_bring.rt_min_us,
                 (unsigned long)s_bring.rt_mean_us,
                 (unsigned long)s_bring.rt_max_us);
    }

    /*
     * And the controller itself, which is a different question from whether
     * anything answered: a transmit error counter climbing towards 256 says
     * nobody is acknowledging, and bus off says the panel has already stopped
     * transmitting.  The recovery runs on the poll gate; this line is how it
     * is seen from a console.
     */
    uint32_t tec = 0, rec = 0, bus = 0;
    bool off = false;
    char n1[12], n2[12], n3[12];
    const bool have_bus = can_twai_errors(&tec, &rec, &bus, &off);
    if (have_bus) {
        ESP_LOGI(TAG, "  bus    tx errors %lu rx errors %lu bus errors %lu%s",
                 (unsigned long)tec, (unsigned long)rec, (unsigned long)bus,
                 off ? " -- BUS OFF" : "");
    } else {
        /* Zeros here would read as a healthy bus.  A controller that never
         * started is a different diagnosis from one with no errors. */
        ESP_LOGI(TAG, "  bus    the controller is not running");
    }

    /*
     * And to the card, one line with everything on it, the same fields in the
     * same order every time.  This is the line a tester sends when the
     * panel's console cannot be reached; a file whose shape changes with the
     * fault is one nobody can read down a column, and the reading that most
     * needs a timestamp is the one where the controller would not answer.
     */
    char row[208];
    snprintf(row, sizeof(row),
             "t=%lus link=down for %lus  bus=%s tx_err=%s rx_err=%s "
             "bus_err=%s rejoins=%lu/%lu  polls=%lu replies=%lu timeouts=%lu",
             (unsigned long)(now_ms() / 1000u),
             (unsigned long)(s_link_lost_ms == 0u
                                 ? 0u
                                 : (now_ms() - s_link_lost_ms) / 1000u),
             !have_bus ? "not running" : (off ? "OFF" : "on"),
             have_bus ? u32(n1, sizeof(n1), tec) : "?",
             have_bus ? u32(n2, sizeof(n2), rec) : "?",
             have_bus ? u32(n3, sizeof(n3), bus) : "?",
             (unsigned long)s_recoveries, (unsigned long)s_recoveries_total,
             (unsigned long)s_bring.polls, (unsigned long)s_bring.replies,
             (unsigned long)s_bring.timeouts);
    debug_log(row);
}

/*
 * The seam between measured and modelled numbers.  When the coprocessor
 * answers, its numbers are used, and the LINK_BN_SIMULATED flag travels with
 * them.  When it does not, the panel models locally and sets the same flag.
 * Nothing above this function knows the difference.
 */
static bool read_bench(link_host_t *host, bench_state_t *out)
{
    link_msg_t reply;
    if (!poll_page(host, LINK_PAGE_BENCH, LINK_BN_COUNT, &reply)) {
        return false;
    }
    if (reply.op == LINK_OP_NACK) {
        return false;
    }
    bench_state_from_regs(out, reply.regs, reply.offset, reply.count);
    return true;
}

/* ------------------------------------------------------------------- main */

/* --------------------------------------------------------- the control task */

/*
 * The bench's own state, before the loop that maintains it.
 *
 * The last line is the one with a consequence outside this file:
 * s_pump_live tells exchange() that a control task exists, so the wait for a
 * link reply pumps the renderer instead of standing still.  Before this
 * point there is no such task and nothing to pump.
 */
static void control_setup(telemetry_sim_t *sim, bench_state_t *bench)
{
    memset(bench, 0, sizeof(*bench));
    telemetry_sim_init(sim, NULL);
    /*
     * The panel's throttle is a channel in an output bank, under the same
     * arming, slew and staleness rules as the coprocessor's outputs, so the
     * two ends cannot answer those questions differently.
     */
    outputs_init(&s_out, now_ms());
    (void)outputs_set_role(&s_out, PANEL_CH_THROTTLE, OUT_ROLE_THROTTLE);
    (void)outputs_set_slew(&s_out, PANEL_CH_THROTTLE, PANEL_THROTTLE_RAMP);
    arming_init(&s_arm, now_ms(),
                HEARTBEAT_GOOD_RUN * HEARTBEAT_PERIOD_MS + HEARTBEAT_PERIOD_MS);
    s_pump_live = true;
}

/*
 * The latched stop, and then the act the arming policy asks for.
 *
 * link_up is passed in because an arm and a disarm are written to the
 * coprocessor's control page only while the link is up.  The flag is the
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
    if (atomic_exchange(&s_servo_release_request, false)) {
        s_servo_held.kind = SERVO_CMD_NONE;
        s_servo_release_owed = true;
    }
    if (atomic_exchange(&s_disarm_request, false)) {
        disarm_here(link_up);
        servo_service(link_up);
    }
}

static void service_arming(bool link_up)
{
    /*
     * STOP latches rather than clearing on the next frame: a stop that lasts
     * one frame is one the coprocessor may never see, and its monostable holds
     * for longer than a frame.  Only an explicit arm clears it.
     *
     * This is the router's backstop for a press the pump's own hit test
     * missed; a press it saw has already been applied there.
     */
    if (atomic_exchange(&s_stop_request, false)
        && !atomic_exchange(&s_stop_counted, false)) {
        arming_stop(&s_arm);
    }

    /*
     * Every stop lets go of what was being driven, whether or not the bench
     * was armed and whichever thing raised it -- a press, the far end, touch
     * that stopped answering.  A bench that was not armed still had a servo
     * held, and nothing else would have released it: the slot would go on
     * being refreshed every 100 ms, over the top of an outputs binding made
     * later.
     */
    service_disarm(link_up);

    const uint32_t stops = arming_stop_count(&s_arm);
    if (stops != s_stops_served) {
        s_stops_served = stops;
        throttle_to_zero();
        servo_let_go();
    }

    /*
     * One place decides, and it is the one under test.  A disarm here is the
     * policy's, not this loop's: a latched stop, dead touch, or an arm that
     * finished settling.
     */
    const bool was_touch_dead = arming_touch_dead(&s_arm, now_ms());
    switch (arming_step(&s_arm, now_ms())) {
    case ARMING_ACT_DISARM:
        outputs_arm(&s_out, false, now_ms());
        throttle_to_zero();
        servo_let_go();
        if (was_touch_dead) {
            control_alert("touch stopped answering -- disarmed");
        }
        if (link_up) {
            link_msg_t ack = { 0 };
            (void)control_write(false, &ack);
        }
        break;
    case ARMING_ACT_ARM: {
        link_msg_t ack = { 0 };
        /*
         * An arm starts from nothing, and this is where that is made true
         * rather than hoped for.  ARM and THROTTLE travel in one
         * transaction, so a command left over from before the disarm is the
         * first thing the far end acts on -- and it steps straight to it.
         * The coprocessor's throttle is bank channel 8, off the CHAN_CFG
         * page that addresses channels 0 to 7, so its slew is never
         * configured and stays at the zero outputs_init() left: there is no
         * ramp on that channel at any time.
         *
         * Every disarm returns the command to zero as well.  A disarm that
         * forgot to would be a motor stepping to its old position on a bench
         * the operator had just stopped, and there has been one.
         */
        throttle_to_zero();
        servo_let_go();
        /*
         * And the slot goes before the arm does, not after it.  The far end
         * applies ARM and stamps every channel's clock before it steps its
         * outputs, so a slot still bound at that moment renders its stale
         * command for as long as the release takes to arrive.
         */
        servo_service(link_up);
        if (link_up && s_servo_release_owed) {
            /*
             * The release did not land, so the far end still has the slot and
             * the command in it.  Arming now would render that command before
             * anything else reached it, so the arm is refused and the debt
             * stays; the next attempt starts by paying it.
             *
             * Only while there is a link.  With none, the debt cannot be paid
             * by anybody and nothing at the far end is being armed either, so
             * refusing would leave the panel unable to arm its own bank --
             * the simulator included -- until a coprocessor answered again.
             * What the far end must not do meanwhile is arm; see poll_bench().
             */
            arming_refused(&s_arm);
            control_alert("servo output not released -- arm again");
        } else if (link_up) {
            /*
             * Two exchanges, each of which can wait a second, and what the
             * operator wants can change between them: the pump runs inside
             * both and applies a stop, and a disarm can be posted while the
             * clear is still on the wire.  Asked again before the write that
             * actually arms, because after it the far end is driving and
             * nothing here can take it back for the length of a timeout.
             */
            if (!control_clear_failsafe(&ack)) {
                arming_refused(&s_arm);
                control_alert("coprocessor refused to arm");
            } else if (arming_stopped(&s_arm)
                       || atomic_load(&s_disarm_request)) {
                /* Stopped or disarmed while the clear was in flight.  No
                 * alert: the operator asked for this and knows. */
                arming_refused(&s_arm);
            } else if (!control_write(true, &ack)) {
                arming_refused(&s_arm);
                control_alert("coprocessor refused to arm");
            } else {
                outputs_arm(&s_out, true, now_ms());
            }
        } else {
            outputs_arm(&s_out, true, now_ms());
        }
        break;
    }
    default:
        break;
    }
}

/*
 * The outputs screen's choice, onto the coprocessor's two output pages.
 *
 * Leaves what became of it in s_outputs_result, which app_main hands to the
 * screen; screen state is app_main's alone.
 */
static void write_output_binding(const outbind_t *bind)
{
    /*
     * CHAN_CFG first.  It says what a channel is; OUTPUTS says what renders
     * it.  A slot that starts rendering a channel whose role has not arrived
     * would drive it to the wrong rest for as long as the second write takes.
     */
    uint16_t cfg[LINK_CC_COUNT];
    uint16_t slots[LINK_OS_COUNT];
    outbind_to_chan_cfg(bind, cfg,
                        (uint16_t)settings_get_int(SET_OUT_MIN_US),
                        (uint16_t)settings_get_int(SET_OUT_MAX_US));
    (void)outbind_to_slots(bind, slots);

    /*
     * A write that got no answer and one that was refused are different things
     * to be told.  REFUSED sends the operator back to the pins they chose; NO
     * LINK sends them to the cable.  Collapsing the two would send them to the
     * wrong one every time the link dropped mid-write.
     */
    link_msg_t reply;
    outputs_result_t res = OUTPUTS_OK;
    if (!write_page(&s_host, LINK_PAGE_CHAN_CFG, LINK_CC_COUNT, cfg,
                    &reply)) {
        res = OUTPUTS_NO_LINK;
    } else if (reply.op != LINK_OP_ACK) {
        res = OUTPUTS_REFUSED;
    } else if (!write_page(&s_host, LINK_PAGE_OUTPUTS, LINK_OS_COUNT, slots,
                           &reply)) {
        res = OUTPUTS_NO_LINK;
    } else if (reply.op != LINK_OP_ACK) {
        res = OUTPUTS_REFUSED;
    }
    atomic_store(&s_outputs_result, (int)res);
}

/*
 * One servo command, as configuration and pulse.
 */
static bool write_servo(const servo_cmd_t sv)
{
    link_msg_t reply;
    if (sv.kind == SERVO_CMD_RELEASE) {
        /* Stop driving: clear the slot.  The channel keeps its last command,
         * but with nothing rendering it that is inert.
         *
         * Whether the far end took it is returned rather than assumed: a
         * write that did not land leaves the slot bound, and the caller's
         * record of owing the release is the only thing that would notice. */
        uint16_t slot[LINK_OS_STRIDE] = { LINK_DRIVER_NONE, 0, 0, 0 };
        return write_page(&s_host, LINK_PAGE_OUTPUTS, LINK_OS_STRIDE, slot,
                          &reply)
               && reply.op == LINK_OP_ACK;
    } else {
        /*
         * Configuration and command, sent whole every time.  The coprocessor
         * may have reset since the last write, so the range the pulse is
         * clamped against and the driver that renders it are restated with
         * each pulse.
         */
        /*
         * The endpoints the screen named, not this file's.  A narrow servo
         * runs 660 to 860 us and its centre is below a standard servo's
         * floor, so clamping it against 1000 to 2000 would send its whole
         * travel to one end.  A command that names no range keeps the
         * standard one.
         */
        const bool named = sv.max_us > sv.min_us
                           && sv.min_us >= LINK_CC_FLOOR_US
                           && sv.max_us <= LINK_CC_CEILING_US;
        const uint16_t min_us = named ? sv.min_us : (uint16_t)SERVO_MIN_US;
        const uint16_t max_us = named ? sv.max_us : (uint16_t)SERVO_MAX_US;
        uint16_t cfg[LINK_CC_STRIDE] = {
            [LINK_CC_ROLE]   = LINK_CC_ROLE_SURFACE,
            [LINK_CC_SLEW]   = 0u,
            [LINK_CC_MIN_US] = min_us,
            [LINK_CC_MAX_US] = max_us,
        };
        uint16_t slot[LINK_OS_STRIDE] = {
            [LINK_OS_DRIVER]  = LINK_DRIVER_PWM,
            [LINK_OS_PIN]     = SERVO_PIN,
            [LINK_OS_RANGE]   = LINK_OS_RANGE_OF(SERVO_CH, 1),
            [LINK_OS_RATE_HZ] = 50u,
        };
        const uint16_t span = us_to_span(sv.value_us, min_us, max_us);
        /*
         * What the channel is, then what it is to do, and only then the slot
         * that renders it.  Each is its own transaction and the far end steps
         * its outputs between them, so a slot bound before the position had
         * arrived would drive whatever channel 0 was holding -- the position
         * from before the last release, or the surface rest of mid-travel
         * once that has gone stale -- and would keep driving it if the
         * position write then failed.  Binding last means the pin is either
         * unbound or already carrying what was asked for.
         *
         * All three are required.  The endpoints travel with the command, so
         * a CHAN_CFG that was refused or timed out leaves the far end
         * clamping against the range it had before: a narrow servo selected
         * against a standard configuration renders 1500 us, past its 860 us
         * maximum. The 100 ms refresh is the retry.
         */
        if (!write_page(&s_host, LINK_PAGE_CHAN_CFG, LINK_CC_STRIDE, cfg,
                        &reply)
            || reply.op != LINK_OP_ACK) {
            return false;
        }
        if (!write_page(&s_host, LINK_PAGE_CHANNELS, 1u, &span, &reply)
            || reply.op != LINK_OP_ACK) {
            return false;
        }
        return write_page(&s_host, LINK_PAGE_OUTPUTS, LINK_OS_STRIDE, slot,
                          &reply)
               && reply.op == LINK_OP_ACK;
    }
}

/*
 * Disarming, wherever it was asked for.
 *
 * Two screens can arm the bench and both disarm it the same way: the policy
 * is told, this end's own bank stops driving, the throttle goes to zero so
 * the next arm cannot carry the last one's command, and the far end is told
 * while there is a link to tell it on.
 */
static void disarm_here(bool link_up)
{
    arming_request_disarm(&s_arm);
    outputs_arm(&s_out, false, now_ms());
    throttle_to_zero();
    servo_let_go();
    if (link_up) {
        link_msg_t ack = { 0 };
        (void)control_write(false, &ack);
    }
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
        arming_request_arm(&s_arm, now_ms());
        break;
    case MOTOR_CMD_DISARM:
        disarm_here(link_up);
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
 * One servo command.
 *
 * Arming and disarming go to the same policy the motor screen's do -- the
 * bench has one armed state and one set of rules for reaching it, whichever
 * screen is up.  A disarm is acted on with or without a link, because the
 * part of it that matters most is at this end.
 */
static void apply_servo_cmd(const servo_cmd_t sv, bool link_up, uint32_t stops)
{
    if (sv.kind == SERVO_CMD_ARM) {
        /*
         * The slot goes first, whether or not this process cached one.  After
         * a panel restart the far end can still hold slot 0 and the command
         * in it, and arming would render that: the same reason DISARM and
         * RELEASE ask unconditionally.  The arm itself waits for the clear --
         * see ARMING_ACT_ARM.
         */
        s_servo_held.kind = SERVO_CMD_NONE;
        s_servo_release_owed = true;
        servo_service(link_up);
        /*
         * And the gesture is asked about again, because that call can wait a
         * second on the wire and the pump runs inside it: a stop, or touch
         * dying and recovering, can happen between the drain's check and
         * this line, and the arm would then be one the bench has already
         * invalidated.
         */
        if (stops != arming_stop_count(&s_arm)) {
            return;
        }
        arming_request_arm(&s_arm, now_ms());
        return;
    }
    if (sv.kind == SERVO_CMD_DISARM) {
        /*
         * Asked for by this screen, so the slot goes whether or not this
         * process cached a position for it -- the far end keeps its slots
         * across a panel restart, and the screen promises to let go of the
         * pin.  A stop from anywhere else stays conditional: it must not
         * quietly clear a slot 0 the OUTPUTS screen bound.
         */
        s_servo_held.kind = SERVO_CMD_NONE;
        s_servo_release_owed = true;
        disarm_here(link_up);
        servo_service(link_up);
        return;
    }
    if (sv.kind == SERVO_CMD_RELEASE) {
        /*
         * Asked for, so the slot is cleared whether or not this process
         * remembers binding it: after a panel restart, or with slot 0 bound
         * from the OUTPUTS screen, the far end holds a slot this end has
         * never written, and the button says it releases the output.
         */
        s_servo_held.kind = SERVO_CMD_NONE;
        s_servo_release_owed = true;
        servo_service(link_up);
        return;
    }
    s_servo_held = sv;
    s_servo_next_ms = now_ms() + SERVO_HOLD_MS;
    if (link_up && write_servo(sv)) {
        /*
         * The slot is bound again, by this write, to this position: an older
         * release still owed for it is void.  Paying it afterwards would
         * clear what was just asked for and leave the pin dead until the
         * next refresh.  A write that failed leaves the debt where it was.
         */
        s_servo_release_owed = false;
    }
}

/*
 * Stop holding the servo, the way throttle_to_zero() stops holding the
 * throttle, and for the same reason: a disarm that leaves a position behind
 * is an arm that steps straight back to it.
 *
 * The far end keeps the slot and the channel command through a disarm and a
 * failsafe, and outputs_arm() stamps every channel's clock, so on the next
 * arm the servo is neither overdue nor at rest -- it is at its old position,
 * with nobody having touched anything.  So the slot has to go, and until it
 * can the release is owed.
 */
static void servo_let_go(void)
{
    if (s_servo_held.kind != SERVO_CMD_NONE) {
        s_servo_release_owed = true;
    }
    s_servo_held.kind = SERVO_CMD_NONE;
}

/*
 * Say the servo's position again before the far end stops believing it, and
 * pay off a release that is owed.
 *
 * A channel nobody has commanded for OUT_DEFAULT_TIMEOUT_MS (500 ms) goes to
 * its rest, which for a surface is mid-travel: a servo held at an endpoint
 * would swing back to centre half a second after the finger stopped, with
 * the screen still showing where it was put.  The throttle is kept alive by
 * the control page, which is written every poll; nothing writes this channel
 * between touches.
 *
 * One register, and only while there is something to say.
 */
static void servo_service(bool link_up)
{
    if (!link_up) {
        return;   /* nothing can be said, and the debt keeps */
    }
    if (s_servo_release_owed) {
        const servo_cmd_t release = { SERVO_CMD_RELEASE, 0, 0, 0 };
        /* Only a write the far end acknowledged pays it off.  link_up is a
         * snapshot and the link can go during the transaction; forgetting an
         * unacknowledged clear would leave the slot bound with nothing left
         * to remember it. */
        s_servo_release_owed = !write_servo(release);
        return;
    }
    if (s_servo_held.kind == SERVO_CMD_NONE) {
        return;
    }
    const uint32_t now = now_ms();
    if ((int32_t)(now - s_servo_next_ms) < 0) {
        return;
    }
    s_servo_next_ms = now + SERVO_HOLD_MS;
    (void)write_servo(s_servo_held);
}

/*
 * What the screens asked for, in the order they asked it.
 *
 * The screens run on app_main and the link belongs to this task, so a command
 * crosses as a queue entry and is acted on here.  link_up is passed in
 * because most of these write to the far end, and the flag that says whether
 * that is possible is the control loop's.
 */
static void drain_commands(bool link_up, bench_state_t *bench)
{
    panel_cmd_t pc;
    while (xQueueReceive(s_cmd_q, &pc, 0) == pdTRUE) {
        /*
         * Between entries, because a backlog of positions is seconds of
         * exchanges and a disarm asked for during it must not wait them out.
         */
        service_disarm(link_up);

        /*
         * Nothing asked for before a stop drives anything after it.
         *
         * The count of stops the sender had seen is no longer current, so
         * something stopped the bench between the asking and the arriving.
         * An arm would clear that stop's own latch; a position or a throttle
         * would put back what the stop had just let go of -- and a stop from
         * touch dying or from the far end has no queued STOP behind it to
         * release the slot a second time.
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
                                    || pc.servo.kind == SERVO_CMD_CENTRE));
        if (drives
            && (pc.stops != arming_stop_count(&s_arm)
                || pc.disarms != atomic_load(&s_disarms))) {
            continue;
        }
        if (pc.kind == PANEL_CMD_STOP) {
            arming_stop(&s_arm);
            outputs_arm(&s_out, false, now_ms());
            throttle_to_zero();
            servo_let_go();
            if (link_up) {
                link_msg_t ack = { 0 };
                (void)control_write(false, &ack);
            }
            continue;
        }
        if (pc.kind == PANEL_CMD_OUTPUTS) {
            if (!link_up) {
                atomic_store(&s_outputs_result, (int)OUTPUTS_NO_LINK);
                continue;
            }
            write_output_binding(&pc.bind);
            /*
             * A binding that landed says what every slot is, slot 0
             * included, so an older release still owed for that slot is
             * void: paying it afterwards would clear a binding the screen
             * has just been told was written, and the far end would keep
             * the cleared page.  A binding that did not land changes
             * nothing and the debt stands.
             */
            if (atomic_load(&s_outputs_result) == (int)OUTPUTS_OK) {
                s_servo_held.kind = SERVO_CMD_NONE;
                s_servo_release_owed = false;
            }
            continue;
        }
        if (pc.kind == PANEL_CMD_SERVO) {
            apply_servo_cmd(pc.servo, link_up, pc.stops);
            continue;
        }

        apply_motor_cmd(&pc.motor, link_up, bench);
    }
}

/*
 * A run is one arming: the log opens when the bank arms and closes when it
 * disarms.  s_log_file is the record of which of the two happened last.
 */
static void log_follow_arming(void)
{
    const bool armed_now = outputs_armed(&s_out);
    if (armed_now == s_log_run) {
        return;
    }
    s_log_run = armed_now;
    if (armed_now) {
        log_start();
    } else {
        log_stop();
    }
}

/*
 * The bench page, and the control page in the same pass.
 *
 * While the far end answers, the bench page is what is asked for; identity is
 * asked only while the far end is silent.
 */
static bool poll_bench(bench_state_t *bench)
{
    const bool answered = read_bench(&s_host, bench);
    if (answered) {
        link_msg_t ack = { 0 };
        /*
         * Not while a servo slot is still bound at the far end and owed a
         * release.  A bank armed with no link -- the simulator, or a cable
         * pulled -- reaches this the moment one answers, and the far end
         * would render the slot's old command before the clear arrived.
         * servo_service() pays the debt every pass, so this holds for one.
         */
        const bool armed = outputs_armed(&s_out) && !s_servo_release_owed;
        if (!control_write(armed, &ack) && armed && ack.op == LINK_OP_NACK) {
            /*
             * The coprocessor is in failsafe or has lost the heartbeat.  A
             * stop latches at this end too.
             *
             * The command goes to zero here rather than through the policy:
             * arming_stop_from_far_end() clears a->armed itself, and
             * arming_step()'s disarm is gated on a->armed, so
             * ARMING_ACT_DISARM cannot follow and the throttle would keep
             * its last value.
             */
            outputs_arm(&s_out, false, now_ms());
            throttle_to_zero();
            servo_let_go();
            arming_stop_from_far_end(&s_arm);
            control_alert("coprocessor disarmed -- arm again");
        }
    }
    return answered;
}

/*
 * Who is there, while nothing is answering.
 *
 * True only for a DATA identity page from a coprocessor speaking
 * LINK_PROTOCOL_MAJOR; reply then holds that page.
 */
static bool probe_identity(link_msg_t *reply)
{
    bool answered;
    /*
     * The bus first, because a controller that has fallen off it cannot ask
     * anything.  A transmitter nobody answers -- a coprocessor not powered
     * yet, or one that has reset -- adds 8 to its error counter per attempt
     * and is off the bus in about four milliseconds, and the ESP32-S3's TWAI
     * does not come back on its own.  Without this the first quiet moment on
     * the bus is permanent: every later transmit fails, the panel shows NO
     * LINK and only a power cycle clears it.
     */
    if (can_twai_recover() == CAN_TWAI_RECOVERING) {
        ++s_recoveries;
        ++s_recoveries_total;
    }

    /*
     * A NACK answers the request it refuses, so poll_page() is true for one
     * and regs[0] carries a refusal reason rather than the first identity
     * register.  Only a DATA reply holds an identity page, and only an
     * identity page says there is a coprocessor there to talk to.
     */
    answered = poll_page(&s_host, LINK_PAGE_IDENTITY, LINK_ID_COUNT, reply)
               && reply->op == LINK_OP_DATA;
    if (answered
        && reply->regs[LINK_ID_PROTOCOL_MAJOR] != LINK_PROTOCOL_MAJOR) {
        /* A protocol major that differs from LINK_PROTOCOL_MAJOR refuses
         * arming; the register is the first one of the identity page. */
        ESP_LOGE(TAG, "coprocessor speaks protocol %u, we speak %u",
                 (unsigned)reply->regs[LINK_ID_PROTOCOL_MAJOR],
                 (unsigned)LINK_PROTOCOL_MAJOR);
        control_alert("protocol mismatch -- will not arm");
        answered = false;
    }
    return answered;
}

/*
 * A board this build ships no catalogue for, described by the board itself.
 */
static void learn_board_pins(void)
{
    link_msg_t cat;
    const bool answered_cat =
        poll_page(&s_host, LINK_PAGE_CATALOGUE, LINK_CAT_COUNT, &cat);
    if (answered_cat && cat.op == LINK_OP_DATA
        && outbind_learn_board(s_board, cat.regs)) {
        ESP_LOGI(TAG, "hardware %u described itself: %u pins",
                 (unsigned)s_board,
                 (unsigned)outbind_pin_count(s_board));
        /*
         * And where they are, if it says.  Only a picture of the board needs
         * this, so a board that does not answer is used from its catalogue and
         * simply is not drawn.
         */
        link_msg_t shp;
        if (poll_page(&s_host, LINK_PAGE_SHAPE, LINK_SH_COUNT, &shp)
            && shp.op == LINK_OP_DATA
            && outbind_learn_shape(s_board, shp.regs)) {
            ESP_LOGI(TAG, "hardware %u says where its pads are",
                     (unsigned)s_board);
        } else {
            ESP_LOGI(TAG, "hardware %u does not say where its pads are; it "
                          "will be listed and not drawn", (unsigned)s_board);
        }
        /*
         * And which of them are grounds and rails.  A board that does not say
         * has them unmarked, which is a lead placed by reading the board
         * rather than the screen.
         */
        link_msg_t pdr;
        if (poll_page(&s_host, LINK_PAGE_PADS, LINK_PAD_COUNT, &pdr)
            && pdr.op == LINK_OP_DATA
            && outbind_learn_pads(s_board, pdr.regs)) {
            ESP_LOGI(TAG, "hardware %u says which pads are grounds and rails",
                     (unsigned)s_board);
        }
    } else if (answered_cat && cat.op == LINK_OP_NACK
               && cat.regs[0] == LINK_NACK_BAD_PAGE) {
        /*
         * Not a fault, and not warned about: a coprocessor built before the
         * page refuses it by design, every time the link comes up.  A warning
         * on every link-up for a bench that is working as built is a warning
         * nobody reads.
         */
        ESP_LOGI(TAG, "hardware %u predates the catalogue page; the screen "
                      "will offer no pins", (unsigned)s_board);
    } else {
        ESP_LOGW(TAG, "hardware %u has no pin map in this build and did not "
                      "describe itself; the screen will offer no pins",
                 (unsigned)s_board);
    }
}

/*
 * What the coprocessor's outputs already are, into the snapshot the outputs
 * and picker screens read.
 */
static void read_outputs_binding(void)
{
    link_msg_t orr, ccr;
    outbind_t got;
    /*
     * Both pages, because the slots page alone cannot say whether a 50 Hz
     * pulse slot is a servo or a motor.  The roles on CHAN_CFG say which,
     * and reading the binding back without them rests an ESC at half
     * throttle.  A CHAN_CFG that will not read leaves the roles unknown
     * rather than guessed, and the binding is then not shown at all.
     */
    if (poll_page(&s_host, LINK_PAGE_OUTPUTS, LINK_OS_COUNT, &orr)
        && orr.op != LINK_OP_NACK
        && poll_page(&s_host, LINK_PAGE_CHAN_CFG, LINK_CC_COUNT, &ccr)
        && ccr.op != LINK_OP_NACK
        && outbind_from_slots(&got, s_board, orr.regs, ccr.regs)) {
        if (xSemaphoreTake(s_snap_lock, portMAX_DELAY) == pdTRUE) {
            s_outputs_read = got;
            s_outputs_read_fresh = true;
            xSemaphoreGive(s_snap_lock);
        }
    } else {
        /*
         * Two different failures land here and they are not the same to
         * somebody reading the log.  A board with no pin map in this build can
         * offer nothing at all; a known board whose page would not read still
         * offers its pins, with nothing selected.
         */
        outbind_t none;
        outbind_init(&none);
        outbind_set_board(&none, s_board);
        if (xSemaphoreTake(s_snap_lock, portMAX_DELAY) == pdTRUE) {
            s_outputs_read = none;
            s_outputs_read_fresh = true;
            xSemaphoreGive(s_snap_lock);
        }
        if (outbind_board(s_board) == NULL) {
            ESP_LOGW(TAG, "hardware %u has no pin map in this build; the "
                          "screen will offer no pins", (unsigned)s_board);
        } else {
            ESP_LOGW(TAG, "could not read the outputs page; the screen will "
                          "show nothing configured");
        }
    }
}

/*
 * The edge: a coprocessor started answering.
 *
 * All of it costs a transaction and none of it changes while the link is up,
 * so it runs once per edge rather than once per 50 ms poll.  reply is the
 * identity page that detected the edge.
 */
static void link_came_up(const link_msg_t *reply)
{
    /*
     * Who answered, before anything is decoded against it.
     *
     * The identity read at bring-up runs once, with whatever was attached then
     * -- which may have been nothing.  A coprocessor that turns up later, or
     * one swapped for another, would otherwise have its outputs page read
     * against a board identity from boot, or against zero, and the screen
     * would offer no pins for as long as it stayed plugged in.
     *
     * The identity page that detected this edge is that answer, so it is used
     * rather than read again.  A second read costs a transaction on the edge.
     * The retry loop that would wrap it is bring-up's: it draws a frame
     * between attempts, which belongs to the splash and not to a task running
     * beside the renderer.
     *
     * Nothing has to forget the board.  The edge fires only on an identity
     * page, and the pages below are decoded against it in the same pass, so no
     * read of it can reach a value from an earlier coprocessor.
     */
    s_board = reply->regs[LINK_ID_HARDWARE];

    /*
     * A board this build ships no catalogue for describes its own pins, so a
     * coprocessor newer than this panel is usable rather than blank.
     *
     * Only when the build has none.  A board it knows uses its own catalogue:
     * that one has been read by somebody, names the exact signal holding each
     * reserved pin, and cannot change under a running bench.
     *
     * Nothing here makes a pin safe.  The coprocessor reserves its own set at
     * its own end whatever this page says, so a catalogue that is wrong costs
     * a pin rather than the safety line.  A coprocessor built before the page
     * answers NACK, which is not a failure: the screen then offers nothing for
     * that board, as it did before.
     */
    if (outbind_board(s_board) == NULL) {
        learn_board_pins();
    }

    /*
     * And a photograph of it, if there is one and it is not already kept.  One
     * transaction here; the rest happens a slice of a poll at a time below.
     */
    art_begin(s_board);

    /* On the edge, not every poll: it does not change while the link is up,
     * so a write per poll would cost a transaction for nothing. */
    link_msg_t pr;
    if (!control_write_poles(&pr)) {
        ESP_LOGW(TAG, "coprocessor did not take the pole count -- rpm will "
                      "read empty");
    }
    /*
     * And what its outputs already are.  The screen shows what is configured
     * over there, not what this panel last sent: after a panel restart those
     * are different things, and only one of them is driving pins.
     */
    read_outputs_binding();
}

/*
 * The status page's counters, into the bring-up record.
 */
static void read_status_counters(void)
{
    link_msg_t st;
    if (poll_page(&s_host, LINK_PAGE_STATUS, LINK_ST_COUNT, &st)
        && st.op == LINK_OP_DATA) {
        s_bring.dev_frames = (uint32_t)st.regs[LINK_ST_FRAMES_LO]
                             | ((uint32_t)st.regs[LINK_ST_FRAMES_HI] << 16);
        s_bring.dev_crc_errors = st.regs[LINK_ST_CRC_ERRORS];
        s_bring.dev_resyncs    = st.regs[LINK_ST_RESYNCS];
        s_dev_faults           = st.regs[LINK_ST_FAULTS];
        /* Two of link_bringup's diagnoses are gated on this; it was lost in
         * the move and left every one of them dead. */
        s_bring.have_status    = true;
    }
}

/*
 * The far end: one poll, and everything that hangs off whether it answered.
 *
 * Every value it maintains belongs to the control loop and is passed in.
 * link_up is both the gate's period -- 50 ms up, 1000 ms down -- and this
 * poll's answer; last_poll and last_status are the two gates' clocks.
 * Returns whether this pass produced a bench sample.
 */
static bool poll_far_end(bool *link_up, bench_state_t *bench,
                         uint32_t *last_poll, uint32_t *last_status)
{
    bool new_sample = false;
    if ((uint32_t)(now_ms() - *last_poll) >= (*link_up ? 50u : 1000u)) {
        *last_poll = now_ms();
        link_msg_t reply;
        bool answered;
        if (*link_up) {
            answered = poll_bench(bench);
        } else {
            answered = probe_identity(&reply);
        }
        if (answered != *link_up) {
            ESP_LOGI(TAG, "coprocessor %s",
                     answered ? "answered" : "went quiet");
            if (answered) {
                link_came_up(&reply);
            }
        }
        if (!answered && s_artbusy) {
            /* The board that was sending it is gone, so the rest of its
             * picture is not coming.  Nothing was kept: the store only becomes
             * findable once the whole thing has checked out. */
            art_stop("the link went quiet");
        }
        /*
         * The link going, and how long ago.  Taken here rather than in the
         * render loop because this is where the answer arrives; the screen is
         * only shown from there.
         */
        if (answered) {
            s_link_lost_ms    = 0;
            s_link_lost_shown = false;
            s_recoveries      = 0;   /* the next outage counts its own */
        } else if (*link_up) {
            /* The edge: it was up until this poll. */
            s_link_lost_ms = now_ms();
        }
        /*
         * A sample exists only if the bench page was read.  A poll that timed
         * out republishes nothing: counting it would put a stale reading on
         * the plot as a fresh column and stamp a log row for a measurement
         * that never arrived.
         *
         * Taken before link_up moves, and from the branch rather than from
         * the answer.  While the link is down the question asked is the
         * identity page, so an answer there says a coprocessor is there to
         * talk to and says nothing about the bench: bench still holds
         * whatever it held, which at boot is zeros.
         */
        new_sample = *link_up && answered;
        *link_up = answered;

        /*
         * The status page is read a tenth as often as the bench page: a status
         * read costs a whole transaction and its numbers move slowly.
         */
        if (*link_up && (uint32_t)(now_ms() - *last_status) >= 500u) {
            *last_status = now_ms();
            read_status_counters();
        }

        /*
         * And a slice of the photograph, last: the bench's own pages are what
         * the operator is watching, and this is a transfer that happens once
         * and can afford to wait for them.
         */
        if (*link_up && s_artbusy) {
            art_slice();
        }
    }
    return new_sample;
}

/*
 * The model and the log advance on their own 50 ms cadence, not on the poll
 * gate's.  The gate runs at 1 Hz while the link is down, which would step the
 * model once per 1000 ms of wall clock instead of twenty times -- the plot's
 * axis and every CSV timestamp twenty times slow.
 */
static void advance_model_and_log(bool link_up, float emitted,
                                  telemetry_sim_t *sim, bench_state_t *bench,
                                  uint32_t *last_sample, bool *new_sample)
{
    if ((uint32_t)(now_ms() - *last_sample)
        >= (uint32_t)(1000.0f / PANEL_SAMPLE_HZ)) {
        *last_sample = now_ms();
        if (!link_up) {
            telemetry_sim_step(sim, emitted, 1.0f / PANEL_SAMPLE_HZ, bench);
            *new_sample = true;
        }
        if (*new_sample && s_log_file != NULL) {
            s_log_t += 1.0f / PANEL_SAMPLE_HZ;
            (void)log_writer_row(&s_log, s_log_t, bench);
        }
    }
}

/*
 * What the renderer draws: the numbers under the mutex, and the sample on the
 * queue.
 */
static void publish_snapshot(const bench_state_t *bench, bool link_up,
                             bool new_sample)
{
    snap_lock();
    s_snap.bench       = *bench;
    s_snap.link_up     = link_up;
    s_snap.armed       = outputs_armed(&s_out);
    s_snap.stopped     = arming_stopped(&s_arm);
    s_snap.stops       = arming_stop_count(&s_arm);
    s_snap.faults      = link_up ? s_dev_faults : (uint16_t)0;
    s_snap.link_errors = (uint32_t)s_bring.dev_crc_errors
                         + (uint32_t)s_bring.dev_resyncs;
    s_snap.run_seconds = arming_run_seconds(&s_arm);
    s_snap.mcu_temp_c  = s_mcu_c;
    snap_unlock();

    /* One queue entry per sample, so none of the plot's time base is lost to
     * a renderer that was busy. */
    if (new_sample && xQueueSend(s_sample_q, bench, 0) != pdTRUE) {
        bench_state_t stale;
        (void)xQueueReceive(s_sample_q, &stale, 0);
        (void)xQueueSend(s_sample_q, bench, 0);
    }
}

/*
 * Everything the bench does, at a fixed 5 ms on the core the renderer does
 * not use: touch, STOP, arming, the outputs, the link and the heartbeat.
 *
 * STOP is hit-tested here against the band's own rectangle rather than waiting
 * for the router to report a press, because the router only sees events when
 * a frame is drawn and a frame can cost 50 ms.  app_main forwards the
 * router's own STOP as well, so a press this test misses still latches.
 */
static void control_task(void *arg)
{
    (void)arg;

    telemetry_sim_t sim;
    bench_state_t   bench;
    control_setup(&sim, &bench);

    uint32_t last_poll     = 0;
    uint32_t last_status   = 0;
    uint32_t last_report   = 0;
    uint32_t last_temp     = 0;
    bool     link_up       = false;

    uint32_t last_sample = now_ms();

    for (;;) {
        control_pump();

        /*
         * The policy first, then the screens.
         *
         * A stop must not wait behind a command that talks to the link: an
         * unanswered exchange holds this loop for LINK_HOST_TIMEOUT_MS
         * (1000 ms), and a queue with several such commands in it multiplies
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
        service_arming(link_up);

        drain_commands(link_up, &bench);
        (void)outputs_keepalive(&s_out, PANEL_CH_THROTTLE, now_ms());
        servo_service(link_up);

        log_follow_arming();

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

        /* --- the far end, at 1 Hz until it answers ----------------------- */
        bool new_sample = poll_far_end(&link_up, &bench, &last_poll,
                                       &last_status);

        advance_model_and_log(link_up, emitted, &sim, &bench, &last_sample,
                              &new_sample);

        if ((uint32_t)(now_ms() - last_temp) >= 1000u) {
            last_temp = now_ms();
            tsens_read();
        }

        /* Every 5 s while the link is down, every 60 s while it is up. */
        if ((uint32_t)(now_ms() - last_report)
            >= (link_up ? 60000u : 5000u)) {
            last_report = now_ms();
            link_report();
        }

        /* --- hand the screen what it draws -------------------------------- */
        publish_snapshot(&bench, link_up, new_sample);

        vTaskDelay(pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

/*
 * Queue a command, and do not lose it.  Every command is already taken from
 * its screen by the time it gets here, so a refused send is a discarded
 * disarm or a throttle that never arrives.
 */
static void send_cmd(const panel_cmd_t *pc)
{
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
        atomic_fetch_add(&s_disarms, 1u);
        atomic_store(&s_disarm_request, true);
    } else if (pc->kind == PANEL_CMD_SERVO
               && pc->servo.kind == SERVO_CMD_DISARM) {
        atomic_fetch_add(&s_disarms, 1u);
        atomic_store(&s_disarm_request, true);
        atomic_store(&s_servo_release_request, true);
    }

    if (xQueueSend(s_cmd_q, pc, pdMS_TO_TICKS(5)) == pdTRUE) {
        return;
    }
    panel_cmd_t stale;
    (void)xQueueReceive(s_cmd_q, &stale, 0);
    if (xQueueSend(s_cmd_q, pc, 0) != pdTRUE) {
        ESP_LOGW(TAG, "control queue full; a command was lost");
    }
}

/*
 * The outputs screen's choice, on its way to the control task.
 *
 * The screen runs on app_main and the link belongs to the control task, so
 * this queues rather than writes.  Everything the far end thinks of the
 * choice comes back as a word the control task leaves behind.
 */
static void outputs_apply(const outbind_t *b)
{
    if (b == NULL) {
        return;
    }
    panel_cmd_t pc = { .kind = PANEL_CMD_OUTPUTS, .bind = *b };
    send_cmd(&pc);
}

/* ------------------------------------------------------------------ main */

void app_main(void)
{
    ESP_ERROR_CHECK(board_init());
    heartbeat_init();
    tsens_init();

    ui_theme_set(UI_THEME_DARK);
    ui_router_init();
    /*
     * The outputs screen hands its choice back through here.  It runs on
     * app_main and the link belongs to the control task, so this queues the
     * choice rather than writing it; the write happens where the link lives.
     */
    outputs_screen_set_apply(outputs_apply);
    /*
     * The picker is the same choice seen as the board, so it applies through
     * the same seam and reads its photograph out of the store.
     */
    picker_screen_set_apply(outputs_apply);
    picker_screen_set_artwork_source(art_for_picker);

    const bool healthy = bring_up();

    s_touch_q   = xQueueCreate(TOUCH_Q_LEN, sizeof(touch_event_t));
    s_cmd_q     = xQueueCreate(CMD_Q_LEN, sizeof(panel_cmd_t));
    s_sample_q  = xQueueCreate(SAMPLE_Q_LEN, sizeof(bench_state_t));
    s_snap_lock = xSemaphoreCreateMutex();
    /* Zero is a temperature; the snapshot starts unread, so the strip shows
     * "--" until the control task has published one. */
    s_snap.mcu_temp_c = NAN;
    ESP_ERROR_CHECK((s_touch_q != NULL && s_cmd_q != NULL
                     && s_sample_q != NULL && s_snap_lock != NULL)
                    ? ESP_OK : ESP_ERR_NO_MEM);

    if (!healthy) {
        ui_router_set_alert("touch did not answer -- the bench will not arm");
    }

    /*
     * On the core the renderer does not use, and above it in priority: the
     * bench's timing must not depend on how long a frame takes.
     */
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(control_task, "control", 6144,
                                            NULL, 10, NULL, 1) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);

    uint32_t frames  = 0;
    uint32_t last_us = (uint32_t)esp_timer_get_time();
    bool     was_armed = false;
    uint32_t last_stops = 0;

    for (;;) {
        const uint32_t us = (uint32_t)esp_timer_get_time();
        const float dt_s = (float)(us - last_us) / 1e6f;
        last_us = us;

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
        uint32_t stops_now;
        snap_lock();
        armed_now = s_snap.armed;
        stops_now = s_snap.stops;
        snap_unlock();

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
        }
        last_stops = stops_now;

        /* The slider follows the bench: a disarm returns the command to
         * zero, so the control the operator picks up next is at zero too. */
        if (was_armed && !armed_now) {
            motor_screen_set_throttle(0.0f);
            motor_cmd_t follow;
            (void)motor_screen_poll_cmd(&follow);   /* not a command */
        }
        was_armed = armed_now;
        motor_screen_set_armed(armed_now);
        servo_screen_set_armed(armed_now);

        /* What the control task saw of the panel. */
        touch_event_t evt;
        while (xQueueReceive(s_touch_q, &evt, 0) == pdTRUE) {
            ui_router_event(&evt);
        }

        /* What the screens decided, back to the control task. */
        /*
         * A command is taken from its screen before it is queued, so a queue
         * that refused it would drop it for good -- a disarm among them.  The
         * send waits briefly and, if the queue is still full, drops the
         * OLDEST entry rather than this one: the newest throttle position and
         * a disarm both matter more than a stale step.
         */
        /* What the coprocessor says its outputs are, and what became of the
         * last write.  Screen state stays app_main's; the control task only
         * leaves values behind. */
        /* The flag is read inside the lock that guards the value it refers
         * to.  Testing it outside would let this task cache it and miss a
         * binding the control task had just read off the wire. */
        outbind_t got;
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
            /* Both views of one binding: whichever is on screen, the other
             * is showing the same thing when the operator reaches it. */
            outputs_screen_set_binding(&got);
            picker_screen_set_binding(&got);
        }
        outputs_screen_set_result(
            (outputs_result_t)atomic_load(&s_outputs_result));

        motor_cmd_t mc;
        while (motor_screen_poll_cmd(&mc)) {
            panel_cmd_t pc = { .kind = PANEL_CMD_MOTOR, .motor = mc,
                               .stops = stops_now,
                               .disarms = atomic_load(&s_disarms) };
            send_cmd(&pc);
        }
        servo_cmd_t sv;
        if (servo_screen_take(&sv)) {
            panel_cmd_t pc = { .kind = PANEL_CMD_SERVO, .servo = sv,
                               .stops = stops_now,
                               .disarms = atomic_load(&s_disarms) };
            send_cmd(&pc);
        }
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
        float    mcu_temp_c;
        uint32_t run_seconds;
        char     alert[ALERT_MAX];
        bool     have_alert;
        snap_lock();
        bench       = s_snap.bench;
        link_up     = s_snap.link_up;
        faults      = s_snap.faults;
        link_errors = s_snap.link_errors;
        mcu_temp_c  = s_snap.mcu_temp_c;
        run_seconds = s_snap.run_seconds;
        have_alert  = s_snap.alert_pending;
        if (have_alert) {
            snprintf(alert, sizeof(alert), "%s", s_snap.alert);
            s_snap.alert_pending = false;
        }
        snap_unlock();

        if (have_alert) {
            ui_router_set_alert(alert);
        }
        /* The armed state this frame acted on, read before the touch was
         * dispatched; the band shows what the screens were told. */
        const bool armed = armed_now;
        /* One sample, one plot column, however many frames it took to get
         * here: the queue holds what this loop was too busy to draw. */
        bench_state_t sample;
        while (xQueueReceive(s_sample_q, &sample, 0) == pdTRUE) {
            motor_screen_push(&sample);
        }

        const ui_bench_status_t status = {
            .link_up     = link_up,
            .armed       = armed,
            .faults      = faults,
            .run_seconds = run_seconds,
            .mode        = link_up ? "LINK" : "SIM",
            .simulated   = bench_state_simulated(&bench),
            .capabilities = s_capabilities,
            .link_errors = link_errors,
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
         * A link that was up and has been gone for LINK_LOST_SCREEN_MS says
         * so, with the controller's own counters on it.  The panel has no
         * console an operator can reach while the bench runs, so a fault it
         * cannot show is a fault nobody can report.
         *
         * Never while armed.  This screen carries no band and therefore no
         * STOP, and a bench with something spinning must not have its stop
         * button covered by a diagnosis.  Armed, the alert band already says
         * the link is gone, and the screen waits for the disarm.
         */
        if (!armed && s_link_lost_ms != 0u && !s_link_lost_shown
            && (uint32_t)(now_ms() - s_link_lost_ms) >= LINK_LOST_SCREEN_MS
            && ui_router_current() != SCREEN_SPLASH
            && ui_router_current() != SCREEN_BUSFAULT) {
            busfault_report_t r;
            link_lost_report(&r);
            busfault_screen_set(&r);
            s_link_lost_shown = true;
            ui_router_goto(SCREEN_BUSFAULT);
        }
        ui_router_tick(dt_s);

        /*
         * The save the settings screen asked for, taken at a moment that can
         * afford it.
         *
         * Writing settings commits an NVS page, and a flash operation on this
         * part disables the cache: neither core runs code that is not in
         * IRAM for its duration.  Taken here it costs a frame, which is what
         * it has always cost.  What it must not do is happen while the bench
         * is armed -- the control task beats the safety line and its ceiling
         * is HEARTBEAT_MAX_GAP_MS (150 ms) -- or while the board's
         * photograph is being fetched or written, which is a quarter of a
         * megabyte already spoken for.
         *
         * Waiting costs nothing.  The request stands until a quiet frame
         * comes, and disarmed -- which is where the settings screen is used
         * -- the next frame is one.
         */
        (void)settings_save_tick(!armed && !s_artbusy && !s_keeping);

        gfx_canvas_t *c = display_canvas();
        const int64_t draw_start = esp_timer_get_time();
        ui_router_render(c, display_back_index());
        const uint32_t draw_us = (uint32_t)(esp_timer_get_time() - draw_start);
        display_flip();   /* blocks until the swap has taken effect */

        /*
         * DRAW is the paint time of the frame; WAIT is how long the flip
         * blocked afterwards.  A healthy frame is mostly WAIT, the loop
         * paced by the panel.  DRAW climbing until WAIT reaches zero is the
         * frame budget being spent.  Printed every 300 frames.
         */
        if (++frames % 300u == 0u) {
            ESP_LOGI(TAG, "%.1f fps  DRAW %u us  WAIT %u us",
                     (double)display_fps(), (unsigned)draw_us,
                     (unsigned)display_last_wait_us());
        }
    }
}
