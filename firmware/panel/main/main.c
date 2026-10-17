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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>          /* fsync(), which is what commits a row to FAT */

#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* sdkconfig.defaults sets the main task's stack to 8192 bytes, and
 * tools/stack_check.py holds the UI's deepest chain to it.  ESP-IDF reads the
 * defaults only when it creates sdkconfig, so a checkout whose sdkconfig
 * predates that line still builds with 3584 bytes and overflows on
 * PROGRAMMER's first frame.  Such a build stops here. */
#if CONFIG_ESP_MAIN_TASK_STACK_SIZE < 8192
#error "CONFIG_ESP_MAIN_TASK_STACK_SIZE is under 8192: delete firmware/panel/sdkconfig so sdkconfig.defaults applies"
#endif

#include "board.h"
#include "ui_band.h"
#include "board_pins.h"
#include "can_twai.h"
#include "busfault_screen.h"
#include "can_selftest.h"
#include "selftest.h"
#include "display.h"
#include "esc_profile.h"
#include "esc_sim.h"
#include "gfx.h"
#include "arming.h"
#include "art_flash_esp.h"
#include "art_fetch.h"
#include "art_store.h"
#include "heartbeat.h"
#include "bind_link.h"
#include "kst_link.h"
#include "prog_link.h"
#include "kst_panel.h"
#include "kst_card.h"
#include "kst_files.h"
#include "kst_prog.h"
#include "link_bringup.h"
#include "link_host.h"
#include "link_pages.h"
#include "link_port.h"
#include "link_update_host.h"
#include "log_name.h"
#include "log_select.h"
#include "log_cadence.h"
#include "log_writer.h"
#include "motor_screen.h"
#include "servo_page.h"
#include "servo_screen.h"
#include "pdmini.h"
#include "sense_link.h"
#include "sense_page.h"
#include "servo_source.h"
#include "tone_link.h"
#include "supply_link.h"
#include "supply_page.h"
#include "supply_screen.h"
#include "outputs_screen.h"
#include "overview_screen.h"
#include "picker_screen.h"
#include "programmer_screen.h"
#include "rcbench_version.h"
#include "flash_gate.h"
#include "safety_gate.h"
#include "settings.h"
#include "settings_screen.h"
#include "splash_screen.h"
#include "knob.h"
#include "knob_task.h"
#include "log_viewer_screen.h"
#include "coproc_update_screen.h"
#include "storage.h"
#include "telemetry_sim.h"
#include "touch_loss.h"
#include "outputs.h"
#include "outputs_pages.h"

/*
 * The ramp comes from the `Ramp limit` setting, 5 to 300 %/s.  It governs
 * this bank, which is the modelled bench: the value it slews to is read by
 * telemetry_sim_step() and by nothing else, and only while the link is down.
 *
 * A coprocessor that is answering renders the raw command instead.  The
 * CONTROL page carries what the slider asked for, and outbind_to_chan_cfg()
 * writes no LINK_CC_SLEW for any channel, so a pin bound as a throttle steps
 * to it.  That is by decision, recorded in STATUS.md under Not planned: the
 * bank ramps a throttle upward only, so a ramp on the wire would slow the
 * rise and nothing else.  docs/Safety.md states both.
 */
static atomic_uint s_throttle_ramp;

/*
 * Published by app_main, which owns the settings model, and read by the
 * control task on the other core.  The values themselves are plain floats
 * written by the settings screen, so the control task must not read them: an
 * atomic carries the converted number across instead, and gives the two
 * cores the ordering a bare float does not.
 */
static void publish_throttle_ramp(void)
{
    const int pct = settings_get_int(SET_OUT_RAMP);
    const uint32_t per_s = ((uint32_t)OUT_SPAN * (uint32_t)pct) / 100u;
    atomic_store(&s_throttle_ramp, (unsigned)per_s);
}

static uint16_t panel_throttle_ramp(void)
{
    return (uint16_t)atomic_load(&s_throttle_ramp);
}

/*
 * The servo bench's output is whichever channels the operator bound as
 * surfaces, and this file names no pin.  The wiring is described once, on the
 * OUTPUTS screen, and the horn asks the binding which channels carry it; a
 * pin named here as well would be a second record of the same wiring, and the
 * two would disagree the first time the operator rebound one.
 *
 * The slots stay the operator's.  This screen writes channels and never the
 * OUTPUTS page, so no drag can take a pin away from what it was bound to, and
 * no channel the binding marked a motor is written by a servo horn.
 */
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
#include "fw_install.h"
#include "fw_ota.h"
#include "fw_release.h"
#include "upd_screen.h"
#include "wifi_access.h"
#include "wifi_screen.h"
#include "ui_screen.h"
#include "ui_text.h"
#include "ui_theme.h"

#include "panel.h"

static const char *TAG = "rcbench";

/* Used during bring-up as well as in the loop, so file scope. */
link_host_t    s_host;
/* A link_cap_t bitmap from the coprocessor's identity page.  Zero until
 * something answers, which is also what it stays if nothing is fitted.
 * Read at bring-up, at every link-up edge and after a SENSE write is taken
 * (bits 3 and 4 follow the SENSE set-up), by the control task; the render
 * loop reads it for the menu marks, so it is atomic. */
atomic_uint    s_capabilities;
link_bringup_t s_bring;


/* Paired with throttle_to_zero() in every path that stops the bench; defined
 * with the rest of the servo's wire handling. */
void        servo_let_go(void);
static void servo_service(bool link_up);
static bool servo_rate_settled(void);
/* The supply's service, defined with the supply.  control_pump() calls it,
 * so a stop or an OFF cuts the output where it lands and the supply keeps
 * its cadence while an exchange waits. */
static void supply_pump(void);
static bool disarm_here(bool link_up);


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
static bool              s_touch_ok = true; /* touch_init() answered    */

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

/*
 * When the link went, 0 while it is up.
 *
 * Atomic because the two ends are different tasks: the control task stamps
 * and clears it, and the render task reads it to decide whether to open the
 * diagnosis screen and to say how long the link has been down.  A plain
 * uint32_t read across those is a data race whatever the silicon does with an
 * aligned word, and copying it to a local does not make the load itself
 * defined.
 *
 * Relaxed ordering is enough.  Nothing is published through this timestamp:
 * it orders no other write, and every reader wants the value alone.
 */
atomic_uint s_link_lost_ms;   /* when it went, 0 while it is up   */
bool     s_link_lost_shown;   /* the screen has had its turn      */
/*
 * Two counts, because they answer two questions.  The per-outage one sits on
 * the screen beside "down N s" and says whether the recovery is working now;
 * the lifetime one goes in the file and says whether this has been happening
 * all session.  One number doing both would be read as the wrong one in at
 * least one of the two places.
 */
uint32_t s_recoveries;        /* this outage                      */
uint32_t s_recoveries_total;  /* since boot                       */

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
float s_mcu_c = NAN;

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
#define TOUCH_Q_LEN       32
#define CMD_Q_LEN         16
#define SAMPLE_Q_LEN      8

/* The updates' flags, s_fw_quiet_req and s_fw_quiet_ack among them:
 * panel_update.c. */
/* What the start check found, for the band once the splash has gone. */
static char        s_boot_alert[ALERT_MAX];
/* s_wifi_ok and s_update_back: panel_update.c. */

/*
 * The bench page is polled every 50 ms, so a sample is 1/20 s of plot.  The
 * motor screen scales its time axis by the same number; the two have to
 * agree or the axis lies about how long ago something happened.
 */
#define PANEL_SAMPLE_HZ   20.0f

typedef enum { PANEL_CMD_MOTOR = 0, PANEL_CMD_SERVO,
               PANEL_CMD_STOP, PANEL_CMD_OUTPUTS,
               PANEL_CMD_SUPPLY } panel_cmd_kind_t;

typedef struct {
    panel_cmd_kind_t kind;
    motor_cmd_t      motor;
    servo_cmd_t      servo;
    outbind_t        bind;   /**< PANEL_CMD_OUTPUTS: the protocols and their pins */
    supply_cmd_t     supply; /**< PANEL_CMD_SUPPLY: set points and the switch */
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
    /** And how many times it had been told to let go; see s_lets_go. */
    uint32_t         lets_go;
    /** And how many times the supply's output had been asked off; see
     *  s_supply_offs. */
    uint32_t         supply_offs;
    /** And how many edits the PD mini's wiring had had (s_pdmini_edits):
     *  an ON queued before an edit on SETUP is not applied after it, even
     *  one undone since. */
    uint32_t         pdmini_edits;
    /**
     * And what the render side knew of the touch stream when it queued
     * this: how many times it had dropped the screens' gestures for a loss
     * (s_loss_gen), and the number of the last event it had taken off
     * s_touch_q.  An arm completes a hold, and a hold that completed on a
     * contact whose events went missing is an arm the operator may not
     * have made.  The render side cancels what it still holds when it
     * finds a loss, but an arm already handed over is past its reach, so
     * the control task compares these when it takes the arm and watches
     * it until the render side has seen the bench armed; see
     * touch_loss.h.  Only an arm: a loss makes no throttle or position
     * suspect, and a disarm is never dropped.
     */
    uint32_t         loss_gen;
    uint32_t         consumed_seq;
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
bind_reading_t s_outputs_read;
bool           s_outputs_read_fresh;    /* both under s_snap_lock */
/*
 * The state of the last reading, for the control task alone: a binding a
 * screen queued is written only over pages that were read
 * (bind_link_may_write()).  BIND_READ_NONE until the first read.
 */
bind_read_t    s_bind_read = BIND_READ_NONE;
/* The coprocessor serves BIND_CFG, BIND_OUT and BIND (protocol 4.10). */
uint16_t       s_far_minor;

/* The coprocessor's board identity, from the identity page at bring-up. */
uint16_t  s_board;

/*
 * Fetching the board's drawing, a slice of a poll at a time.
 *
 * Up to LINK_DRAW_MAX_BYTES (5328 bytes) over a link that carries sixty-two
 * bytes a transaction is up to 86 round trips.  Doing them in a loop would
 * stop the control task for the length of the transfer, so a bounded slice
 * of each poll goes to it and the bench keeps its cadence.
 */
#define ART_SLICE_MS 15u

static const art_flash_t *s_artflash;
static art_fetch_t        s_artfetch;
static uint8_t           *s_artbuf;
static uint16_t           s_artboard;
bool                      s_artbusy;

/*
 * Writing it down: one sector a control pass, and only on a safe bench.
 *
 * A drawing of up to 5328 bytes is up to 3 sector erases (the header's and
 * two of payload) and 2 writes and the header.  Auto-suspend
 * is off, so each one stalls both cores, this task with them, for its length
 * -- tens of milliseconds an erase.  No other task can take that stall off
 * this one.  So the keep runs here, a step a pass (art_keep_service()), and
 * a step is taken only when flash_gate_open() says so: disarmed, no arm
 * waiting, the supply off and no ON queued.  The gate is read on the same
 * task, in the same pass, that applies an arm and an ON, so neither can come
 * in between the check and the sector.
 *
 * An arm or an ON pauses the keep between two sectors; it holds its buffer
 * and carries on from the same step on the next safe pass.  Nothing is
 * findable for the board until the header, written last.
 *
 * The display stalls for each operation too: the panel's bounce-buffer refill
 * reads PSRAM through the cache a flash operation closes.  Settings saves
 * already cost that.
 */
static uint8_t     *s_keepbuf;      /* the keeper's, once handed over */
static art_entry_t  s_keepentry;
static art_keep_t   s_keep;         /* control task only */
atomic_bool  s_keeping;      /* set and cleared by the control task */
static bool         s_keep_waited;  /* the wait is logged once a keep */

/*
 * The drawing the picker may show, published by the control task and copied
 * out by the UI task.
 *
 * Emptied on every link-up edge before anything else is asked, so a drawing
 * outlives neither the coprocessor it came from nor a reflash of it.  Filled
 * only with bytes that match the ARTWORK page read on that link-up: fetched
 * and checked against its CRC and hash, or read from flash under an entry
 * whose every field equals the page (art_store_read_same()).  The board ID
 * alone never selects a kept drawing.
 *
 * s_draw_gen counts publications; the UI task re-asks when it moves, so an
 * open picker follows a link-up instead of waiting for its next visit.
 */
static SemaphoreHandle_t s_draw_lock;
static uint8_t          *s_drawpub;        /* LINK_DRAW_MAX_BYTES, PSRAM */
static uint32_t          s_drawpub_bytes;  /* 0: nothing to show; lock */
static uint16_t          s_drawpub_board;  /* lock */
static atomic_uint       s_draw_gen;
static uint8_t          *s_drawshow;       /* the UI task's copy, PSRAM */
static unsigned          s_draw_seen;      /* UI task only */

static QueueHandle_t     s_touch_q;   /**< control task -> app_main */
static QueueHandle_t     s_cmd_q;     /**< app_main -> control task */
/* The servo programmer's commands (kst_prog_cmd_t), a queue of their own:
 * s_cmd_q drops its oldest entry when it is full. */
#define KST_Q_LEN 4
QueueHandle_t     s_kst_q;     /**< app_main -> control task */
/*
 * One entry per bench sample, not a count of them.  A counter would lose the
 * samples a stalled renderer did not come back for, and the plot's time axis
 * would compress by exactly the frames it missed, which is the frame-rate
 * coupling this task exists to remove.  Full means the renderer is more than
 * SAMPLE_Q_LEN samples behind; the oldest is dropped, because a plot that is
 * 400 ms out of date is worth less than one that is current.
 */
QueueHandle_t     s_sample_q;  /**< control task -> app_main */
/* The supply's samples, one entry each, for the same reason; each with the
 * servo rail's meter and its change count as last decided when the sample
 * was queued (servo_screen_supply_at()). */
typedef struct {
    supply_state_t    st;
    servo_source_id_t source;
    uint32_t          changes;
    /* Why the INA3221 last stopped being the meter, and at which count
     * (servo_source_dropped()), as of the same poll. */
    servo_source_why_t dropped;
    uint32_t          dropped_at;
} supply_item_t;
static QueueHandle_t     s_supply_q;  /**< control task -> app_main */
/* The output encoder's readings (servo_test_enc_t), each once. */
QueueHandle_t     s_enc_q;     /**< control task -> app_main */
QueueHandle_t     s_win_q;     /**< control task -> app_main */
SemaphoreHandle_t s_snap_lock;

/* What the screen reads (panel_snap_t, panel.h). */
panel_snap_t s_snap;


/* Called from the control task, which must not touch the router.  The
 * slot holds one alert; a later one replaces it.  Returns its number. */
uint32_t control_alert_numbered(const char *text)
{
    snap_lock();
    snprintf(s_snap.alert, sizeof(s_snap.alert), "%s", text);
    s_snap.alert_pending = true;
    const uint32_t gen = ++s_snap.alert_gen;
    snap_unlock();
    return gen;
}

/*
 * The safety loop's own state, at file scope because control_pump() services
 * it from inside a link exchange as well as from the top of the control task.
 */
outputs_t s_out;
/*
 * When the bench may be armed.  The policy lives in shared/safety/arming.c so
 * the host suite can hold it; this file drives it and acts on what it says.
 */
arming_t  s_arm;

/* What the servo screen is holding, so it can be said again before the far
 * end times it out.  SERVO_CMD_NONE means nothing is being held. */
/* The stop count this task has already let go for; see service_arming(). */
static uint32_t    s_stops_served;
/*
 * When the outstanding disarm started holding the safety line down, and how
 * long that may go on for.
 *
 * Twice HEARTBEAT_MAX_GAP_MS (150 ms), so a far end that never answers has
 * certainly failed safe by then and the line can go back up: a disarm nobody
 * can deliver must not hold it down for ever.
 */
#define DISARM_INHIBIT_MS (2u * HEARTBEAT_MAX_GAP_MS)
static bool        s_disarm_timing;   /**< whether s_disarm_since means anything */
static uint32_t    s_disarm_since;

servo_cmd_t        s_servo_held;
static uint32_t    s_servo_next_ms;
/*
 * A position the far end still holds and this end has finished with.
 *
 * Kept as a debt rather than written and forgotten: a screen left while the
 * link is down cannot send the release, and the far end keeps the channel
 * command through a failsafe.  A later arm -- from either screen -- would then
 * drive the surface to where it was before.
 */
bool        s_servo_release_owed;
uint16_t  s_throttle_hundredths;   /* 0..LINK_THROTTLE_MAX */

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
atomic_bool        s_disarm_request;
static atomic_bool s_servo_release_request;
/* HOLD asked for: a sweep being written, which can take several exchanges,
 * gives way to it at once.  Cleared when the drain reaches a servo
 * command, the HOLD itself or one after it. */
static atomic_bool s_servo_hold_request;
/*
 * How many times the panel has been told to let go: a disarm, or an explicit
 * release of the servo's pin.
 *
 * The same job the stop count does.  Either is acted on between queue
 * entries, out of the order the queue holds, so a drive command still behind
 * one would otherwise put back what it let go of.
 */
static atomic_uint s_lets_go;
/*
 * And how many times the supply's output has been asked off, for the same
 * reason: an OFF is a flag as well as a queue entry, so an ON still behind it
 * in the queue would otherwise switch the output back on.  See
 * supply_service().
 */
static atomic_bool s_supply_off_request;
static atomic_uint s_supply_offs;
/*
 * The supply's set points as the screen holds them, in mV and mA: a level the
 * render loop stores every frame and the control task reads, not a queue
 * entry.  A set point lost to an eviction would leave the screen showing one
 * voltage and the supply holding another.  Stored before any command of the
 * same frame is queued, so an ON taken from the queue is never applied ahead
 * of the set points it was asked under.
 */
static atomic_uint s_supply_set_mv;
static atomic_uint s_supply_set_ma;
/*
 * And the operator's limits, the same way: caps on the set points in mV and
 * mA, the trips in mA and mV (0 is off) and the trip time in ms.  The screen
 * already keeps its set points under the caps; the control task applies
 * them again, because it is what the supply obeys.
 */
static atomic_uint s_supply_vmax_mv;
static atomic_uint s_supply_imax_ma;
static atomic_uint s_supply_trip_ma;
static atomic_uint s_supply_trip_mv;
static atomic_uint s_supply_trip_ms;
/*
 * The newest ON the render side has seen applied, from a frame that found
 * the touch stream whole: the supply's counterpart of s_arm_ack.  See
 * supply_watch_service().
 */
static atomic_uint s_supply_ack;
/*
 * Whether the output is on, stored by the control task the moment it
 * switches, and the ONs the render side has queued against the ones the
 * control task has taken (applied or dropped).  Together they say, at any
 * moment of a frame, whether the output is live or about to be: the
 * snapshot is a frame old, and an ON queued in this frame is in neither.
 * See supply_live_or_coming().
 */
static atomic_bool s_supply_live;
static atomic_uint s_supply_ons_sent;
static atomic_uint s_supply_ons_taken;
/*
 * Where the PD mini is wired, from SETUP INTERFACES, in one word so a write
 * never pairs one edit's TX with another's RX: bit 24 enabled, bits 23..16
 * the TX GPIO and 15..8 the RX, each plus one with 0 for unset, bits 7..0
 * the baud setting.  Published as the pole count is; see publish_pdmini().
 */
atomic_uint s_pdmini_wiring;
/* And how many times it has been edited: an edit undone before a queued ON
 * is applied leaves the word as it was, but not this count. */
static atomic_uint s_pdmini_edits;
/*
 * Whether the SUPPLY screen drives the PD mini rather than the panel's
 * model: SETUP INTERFACES enables it.  Stored by the control task, read by
 * app_main for the screen's header, caps and the menu's badge.
 */
static atomic_bool s_supply_real;
/* The PD mini's input in mV as its page last read, 0 while not known: the
 * screen keeps the voltage under it (PDMINI_CAP_MV). */
atomic_uint s_supply_vin_mv;
/* The PD mini's UART rate in baud as the coprocessor last reported it, for
 * the SUPPLY screen's header; 0 while not known. */
atomic_uint s_supply_baud;
/*
 * The two touch queues, numbered.  See touch_loss.h for why numbers and not
 * counts: a count raised on one core after the eviction it reports can be
 * read on the other only once a surviving event has already been handled.
 *
 * The driver's queue: the control task is its consumer.  s_drv_gaps is how
 * many events it found missing from that stream, since boot; it only rises,
 * and the arm watch compares it.
 */
static touch_seq_rx_t s_drv_rx = { .next = 1u };   /* touch_seq_rx_init() */
static atomic_uint    s_drv_gaps;

/*
 * s_touch_q, from the control task to the render loop.  The control task is
 * its one producer and numbers what it offers; s_tq_published is the last
 * number offered, stored after the offer.  A loss the control task found in
 * the driver's stream travels down this queue as an item of its own, so the
 * render loop meets it in order, ahead of the events that followed the gap;
 * s_lost_notice_seq is the number of the latest such item, for the check on
 * an arm (touch_loss.h, arm_watch_take_ok()).  Both control-task values.
 */
typedef struct {
    touch_event_t evt;
    uint32_t      seq;
    bool          lost;    /**< a loss notice, not an event */
} touch_item_t;

static uint32_t    s_tq_seq;
static atomic_uint s_tq_published;
static uint32_t    s_lost_notice_seq;

/*
 * The render loop's side.  s_loss_gen counts the times it dropped the
 * screens' gestures for a loss -- written there only, read by the control
 * task for the arm check and the watch.  s_arm_ack is the arm the render
 * loop has seen, set at the end of a frame that began with the bench armed
 * and found no loss; see arm_watch_lost().
 */
static atomic_uint s_loss_gen;
static atomic_uint s_arm_ack;

/*
 * The arm in flight, control task only.  An arm's two stamps are kept from
 * the moment it is taken, because the exchanges that apply it can take two
 * seconds, and the watch starts once it is applied.
 */
static uint32_t    s_arm_take_gen;
static uint32_t    s_arm_take_drv;
uint32_t           s_arm_gen;
static arm_watch_t s_arm_watch;

/*
 * The pole count the coprocessor holds, and whether it is current.
 *
 * Nothing refreshes the register: the far end keeps whatever it was last
 * handed until something writes it again.  So a change to the setting is a
 * debt, in the shape a disarm and a servo release use -- a write nobody
 * answered must not lose it until the next link-up edge, which on a link
 * that stays up never comes.
 *
 * A stale count is not a missing one.  Every value the schema allows is even
 * and inside 2 to 42, so it passes the far end's range check, sets
 * LINK_BN_RPM_OK, and reaches the plot and the CSV as a valid reading.  The
 * speed reported is the actual speed times the motor's poles over the count
 * held: 14.3 % low for a 12-pole motor converted as 14, and a factor of 21
 * at the ends of the range.
 *
 * The value travels beside the flag rather than being read from the settings
 * model when the write goes out.  settings.c holds the values as plain
 * floats and the settings screen writes them from app_main, so a control-task
 * read of one is a data race with no ordering between the two; the flag
 * alone does not order a second edit against a read already under way.
 */
atomic_bool s_poles_owed;
atomic_uint s_poles_value;

/*
 * A setting the far end keeps a copy of has changed.
 *
 * The settings screen runs on app_main and the link belongs to the control
 * task, so this publishes the value and records the debt; the write goes out
 * from there.  Motor poles is the only setting the coprocessor holds a copy
 * of.
 *
 * The value is stored before the flag, so a control task that sees the debt
 * sees at least this value and never an older one.
 */
static void publish_poles(void)
{
    atomic_store(&s_poles_value,
                 (unsigned)settings_get_int(SET_MOTOR_POLES));
    atomic_store(&s_poles_owed, true);
}

/*
 * The throttle's pulse endpoints, Idle pulse and Full pulse, on their way to
 * the coprocessor.
 *
 * Published the way the pole count is, and for the same reason: the settings
 * screen writes the model from app_main and the control task owns the link,
 * so the values travel in an atomic beside the debt rather than being read
 * from settings.c on the control task.  Both in one word, min in the high
 * half, so a write never pairs one edit's minimum with another's maximum.
 *
 * The time of the last edit travels too.  A key held on + or - steps the
 * value every few hundred milliseconds, and each write of CHAN_CFG is a
 * flash write at the far end; the debt is paid once the value has rested for
 * ENDPOINTS_SETTLE_MS.
 */
atomic_bool s_endpoints_owed;
/*
 * Raised at a link-up edge, control task only: the far end is held disarmed
 * until its throttle endpoints have been brought to the settings.  A bench
 * armed with no link -- the simulator, or a cable pulled mid-run -- would
 * otherwise arm a coprocessor that has just answered on whatever range it
 * kept in flash, and keep that range for the whole run.
 */
bool        s_endpoints_hold;
atomic_uint s_endpoints_value;
atomic_uint s_endpoints_at_ms;

static void publish_endpoints(void)
{
    const unsigned lo = (unsigned)settings_get_int(SET_OUT_MIN_US);
    const unsigned hi = (unsigned)settings_get_int(SET_OUT_MAX_US);
    atomic_store(&s_endpoints_value, (lo << 16) | (hi & 0xFFFFu));
    atomic_store(&s_endpoints_at_ms, (unsigned)now_ms());
    atomic_store(&s_endpoints_owed, true);
}

/* The PD mini's wiring, as the settings screen left it; see
 * s_pdmini_wiring. */
static void publish_pdmini(void)
{
    const int tx = settings_get_int(SET_PDMINI_TX);
    const int rx = settings_get_int(SET_PDMINI_RX);
    const unsigned word =
        ((settings_get_int(SET_PDMINI_EN) != 0) ? (1u << 24) : 0u)
        | ((unsigned)((tx < 0) ? 0 : tx + 1) & 0xFFu) << 16
        | ((unsigned)((rx < 0) ? 0 : rx + 1) & 0xFFu) << 8
        | ((unsigned)settings_get_int(SET_PDMINI_BAUD) & 0xFFu);
    /* The count first: an ON stamped before it is dropped by the time the
     * new wiring can be seen. */
    atomic_fetch_add(&s_pdmini_edits, 1u);
    atomic_store(&s_pdmini_wiring, word);
}

/*
 * The current monitors' set-up, as SETUP INTERFACES left it, for the
 * control task's SENSE writes (sense_link.h).
 *
 * Ten values do not fit an atomic word, so the copy is taken under a
 * spinlock both ways: the control task never pairs one edit's shunt with
 * another's address.  Written by the settings observer on app_main, read
 * once a poll by the control task.
 */
static portMUX_TYPE  s_sense_mux = portMUX_INITIALIZER_UNLOCKED;
static sense_setup_t s_sense_want;

static void publish_sense(void)
{
    const int ch = settings_get_int(SET_INA3221_CH);
    const sense_setup_t w = {
        .i228  = settings_get_bool(SET_INA228_EN),
        .i3221 = settings_get_bool(SET_INA3221_EN),
        .as5600 = settings_get_bool(SET_ENC_EN),
        .sda   = (int8_t)settings_get_int(SET_SENSE_SDA),
        .scl   = (int8_t)settings_get_int(SET_SENSE_SCL),
        /* The address options run up from 0x40 in steps of one. */
        .i228_addr   = (uint8_t)(LINK_SN_I228_ADDR_MIN
                                 + (unsigned)settings_get_int(SET_INA228_ADDR)),
        .i228_uohm   = (uint16_t)settings_get_int(SET_INA228_UOHM),
        .i228_max_da = (uint16_t)lrintf(settings_get(SET_INA228_MAX_A) * 10.0f),
        .i3221_addr  = (uint8_t)(LINK_SN_I3221_ADDR_MIN
                                 + (unsigned)settings_get_int(SET_INA3221_ADDR)),
        .i3221_dmohm = (uint16_t)lrintf(settings_get(SET_INA3221_MOHM) * 10.0f),
        .i3221_ch    = (uint8_t)((ch != 0) ? LINK_SN_I3221_CH_ALL : 0x01u),
    };
    taskENTER_CRITICAL(&s_sense_mux);
    s_sense_want = w;
    taskEXIT_CRITICAL(&s_sense_mux);
}

sense_setup_t sense_wanted(void)
{
    taskENTER_CRITICAL(&s_sense_mux);
    const sense_setup_t w = s_sense_want;
    taskEXIT_CRITICAL(&s_sense_mux);
    return w;
}

/*
 * The phase tap's set-up, as SETUP INTERFACES left it, for the control
 * task's TONE writes (tone_link.h).  Seven values do not fit an atomic
 * word, so the copy is taken under a spinlock both ways, as the current
 * monitors' is.  Written by the settings observer on app_main, read once a
 * poll by the control task.
 */
static portMUX_TYPE  s_tone_mux = portMUX_INITIALIZER_UNLOCKED;
static tone_setup_t  s_tone_want;

static void publish_tone(void)
{
    const tone_setup_t w = {
        .enable      = settings_get_bool(SET_TONE_EN),
        .pin         = (uint8_t)settings_get_int(SET_TONE_PIN),
        .f_min_hz    = (uint16_t)settings_get_int(SET_TONE_F_MIN),
        .f_max_hz    = (uint16_t)settings_get_int(SET_TONE_F_MAX),
        .split_pct   = (uint8_t)settings_get_int(SET_TONE_SPLIT),
        .gap_ms      = (uint8_t)settings_get_int(SET_TONE_GAP),
        .min_periods = (uint8_t)settings_get_int(SET_TONE_PERIODS),
    };
    taskENTER_CRITICAL(&s_tone_mux);
    s_tone_want = w;
    taskEXIT_CRITICAL(&s_tone_mux);
}

tone_setup_t tone_wanted(void)
{
    taskENTER_CRITICAL(&s_tone_mux);
    const tone_setup_t w = s_tone_want;
    taskEXIT_CRITICAL(&s_tone_mux);
    return w;
}

static void settings_changed(setting_id_t id)
{
    if (id == SET_MOTOR_POLES) {
        publish_poles();
    } else if (id == SET_OUT_MIN_US || id == SET_OUT_MAX_US) {
        publish_endpoints();
    } else if (id == SET_PDMINI_EN || id == SET_PDMINI_TX
               || id == SET_PDMINI_RX || id == SET_PDMINI_BAUD) {
        publish_pdmini();
    } else {
        switch (id) {
        case SET_INA228_EN:  case SET_INA228_ADDR:  case SET_INA228_UOHM:
        case SET_INA228_MAX_A:
        case SET_INA3221_EN: case SET_INA3221_ADDR: case SET_INA3221_MOHM:
        case SET_INA3221_CH:
        case SET_SENSE_SDA:  case SET_SENSE_SCL:
        case SET_ENC_EN:
            publish_sense();
            break;
        case SET_TONE_EN:    case SET_TONE_PIN:   case SET_TONE_F_MIN:
        case SET_TONE_F_MAX: case SET_TONE_SPLIT: case SET_TONE_GAP:
        case SET_TONE_PERIODS:
            publish_tone();
            break;
        default:
            break;
        }
    }
}

/* False until the control task owns the safety state; bring-up polls the
 * link before that, with nothing to service. */
bool s_pump_live;

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
 * And this task's own record of a STOP press is answered here.  It owns
 * s_stop_press independently of the screens, and the driver can drop that
 * press's release: the controller then reuses the track id, and a contact
 * that begins elsewhere and lifts over STOP would satisfy the release branch
 * and stop a run nobody asked to stop.  A press standing when the stream
 * broke stops the bench instead.  Dropping the ownership on its own is the
 * wrong direction: for every other gesture abandoning it asks for nothing,
 * and for this one abandoning it is the failure.  The release that would
 * have stopped the bench may be the event that went missing, and the render
 * side cancels the band's press for the same loss, so nothing else would
 * stop it, and the operator has already pressed STOP.  What this gives up: a
 * press that began on STOP and would have been carried off it before
 * lifting, which asks for nothing, stops the bench instead.  That is the
 * direction to be wrong in.
 *
 * Losses in s_touch_q itself cannot orphan this press: this task evicts from
 * that queue only after the event has been through the STOP branch.
 */
static void drv_lost(uint32_t n)
{
    atomic_fetch_add(&s_drv_gaps, (unsigned)n);
    (void)touch_q_put(NULL, true);
    if (!s_stop_press) {
        return;
    }
    s_stop_press = false;
    arming_stop(&s_arm);
    control_alert(TR(ALERT_TOUCH_LOST_STOP));
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
         * A gap is answered before this event is looked at.  The events after
         * it were captured around the one that went missing: a contact
         * reusing the track id of a STOP press whose release the driver
         * dropped would otherwise lift over STOP below and abort a run nobody
         * asked to abort.
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
                 * Counted as pressed: an operator's STOP, which a stick run
                 * tells apart from the bench's own.
                 */
                arming_stop_pressed(&s_arm);
                counted_here = true;
            }
        }
        /*
         * The screen still sees every event: it draws the press.
         *
         * A still finger emits nothing, so filling 32 slots takes either
         * coordinate wobble on the held contact or a second one; a palm
         * resting on the glass reaches it in about 90 ms of undrained
         * frame.  What happens when it does fill is touch_q_put()'s.
         *
         * The router will latch this same release and the backstop would
         * then stop the bench a second time, so a stop applied here is
         * marked -- before the event is offered, so the render loop cannot
         * take the release and find a loss ahead of it (which clears the
         * marker) before the marker exists.  Taken back if the queue did not
         * keep the event: a marker left standing for an event nobody saw is
         * consumed by the next stop the backstop really does have to apply,
         * and that stop would be ignored.
         */
        if (counted_here) {
            atomic_store(&s_stop_counted, true);
        }
        const bool routed = touch_q_put(&evt, false);
        if (counted_here && !routed) {
            atomic_store(&s_stop_counted, false);
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
     * arming_stop() drops the heartbeat now, the coprocessor fails safe
     * within HEARTBEAT_MAX_GAP_MS (150 ms) whatever this task is waiting
     * for, and service_arming() lets go of the rest when the loop is free.
     * A queued drive command carries the old stop count and is dropped.
     */
    if (outputs_armed(&s_out)
        && arm_watch_lost(&s_arm_watch, atomic_load(&s_loss_gen),
                          atomic_load(&s_drv_gaps), atomic_load(&s_arm_ack))) {
        arming_stop(&s_arm);
        control_alert(TR(ALERT_TOUCH_LOST_ARMING));
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
     * LINK_HOST_TIMEOUT_MS (1000 ms) with the request unserved and the
     * output driving.  The line is the one channel that does not need the
     * link, so it carries the disarm instead.
     *
     * On a healthy link this costs nothing -- the request is served on the
     * next pass, well inside HEARTBEAT_MAX_GAP_MS (150 ms).  On a link that
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
     * on the bench would be the coprocessor's.
     *
     * "panel fw" and not "fw": the CH422G beside it is the I2C expander, a
     * fixed-function part with no firmware of its own, and a line reading
     * "CH422G fw 0.8.0" says the expander is running a build that does not
     * exist. */
    splash_screen_set(SPLASH_STEP_BOARD, SPLASH_OK,
                      "CH422G, panel fw " RCBENCH_VERSION_STRING);

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
     * operation closes that cache.  ESP-IDF masks the handler for the length
     * of the operation (CONFIG_LCD_RGB_ISR_IRAM_SAFE is off), so the picture
     * stalls, and past 512 us the scan restarts at the first VBlank
     * (vertical blanking interval) interrupt after the operation ends,
     * which can be mid-frame (display.h).
     */
    const settings_store_t *store = settings_nvs_store();
    settings_set_store(store);
    /* Installed before the load, so the count the store returns is owed to
     * the coprocessor the same way an edit made later is. */
    settings_set_observer(settings_changed);
    settings_init();
    /* And once unconditionally, whatever settings_init() told the observer:
     * a bench left at the defaults still has to tell the far end what it
     * is, and each snapshot the control task reads starts zeroed.  The
     * pulse endpoints the same way: the far end keeps CHAN_CFG in flash, so
     * a range written by another panel, or before a reset to defaults,
     * would otherwise stay on the motor channels.  The current monitors'
     * set-up too: a zeroed one is no set-up at all.  Nothing is written
     * when the page already agrees. */
    publish_poles();
    publish_endpoints();
    publish_pdmini();
    publish_sense();
    publish_tone();
    settings_apply_ui();
    /*
     * The panel's Wi-Fi reads its kept home network now, while the panel is
     * not scanning yet, for the reason the settings are read here.  The
     * radio stays off.
     */
    {
        fw_image_info_t running;
        if (!fw_ota_running(&running)) {
            ESP_LOGW(TAG, "running version \"%s\" does not parse",
                     running.version_text);
        }
        s_wifi_ok = (wifi_access_init(fw_ota_slot_bytes(), &running)
                     == ESP_OK);
        if (!s_wifi_ok) {
            ESP_LOGE(TAG, "Wi-Fi not set up: unavailable until a restart");
        }
    }

    display_config_t dcfg = DISPLAY_CONFIG_DEFAULT();
    if (display_init(&dcfg) == ESP_OK) {
        splash_screen_set(SPLASH_STEP_DISPLAY, SPLASH_OK, "800x480 39Hz");
    } else {
        splash_screen_set(SPLASH_STEP_DISPLAY, SPLASH_FAIL, TR(SPLASH_NO_PANEL));
        /* A build on its first start after an update goes back to the one
         * before it, here and now; any other start goes on as before. */
        (void)fw_ota_boot_check(false, NULL, 0u);
        return false;   /* nothing can be reported after this */
    }
    pump();

    touch_config_t tcfg = TOUCH_CONFIG_DEFAULT();
    if (touch_init(&tcfg) == ESP_OK) {
        splash_screen_set(SPLASH_STEP_TOUCH, SPLASH_OK, "GT911 5pt");
    } else {
        /* A bench with no touch has no STOP button, so this is fatal rather
         * than degraded -- but it is reported first. */
        splash_screen_set(SPLASH_STEP_TOUCH, SPLASH_FAIL, TR(SPLASH_NO_ANSWER));
        s_touch_ok = false;
        ok = false;
    }
    pump();

    /*
     * A build's first start after an update is a trial until here: with
     * the display and touch up it is kept, and without touch -- no STOP --
     * the panel restarts into the build before it.  Keeping it writes
     * otadata, a flash write against a scanning panel: one disturbed frame,
     * once.
     */
    {
        char other[FW_TEXT_MAX];
        switch (fw_ota_boot_check(s_touch_ok, other, sizeof(other))) {
        case FW_BOOT_UPDATED:
            snprintf(s_boot_alert, sizeof(s_boot_alert), TR(ALERT_FW_UPDATED),
                     RCBENCH_VERSION_STRING);
            break;
        case FW_BOOT_ROLLED_BACK:
            snprintf(s_boot_alert, sizeof(s_boot_alert),
                     TR(ALERT_FW_ROLLED_BACK), other, RCBENCH_VERSION_STRING);
            break;
        case FW_BOOT_UNCONFIRMED:
            snprintf(s_boot_alert, sizeof(s_boot_alert),
                     TR(ALERT_FW_UNCONFIRMED), RCBENCH_VERSION_STRING);
            break;
        case FW_BOOT_NORMAL:
        default:
            break;
        }
    }

    /* A card is optional: the log viewer wants one, nothing else does, so a
     * missing card is a warning the operator reads on the way past rather
     * than a boot failure. */
    (void)storage_init();
    /* And the viewer is told how to reach it.  Without this it has no way to
     * list anything and reports no card whatever is mounted. */
    log_viewer_set_io(&k_card_io);
    /* Before anything can ask for a profile; with no card, the built-in
     * ones alone. */
    esc_profiles_load();
    /* The component's "no card" in the language showing; its other words
     * are ESP-IDF's error names, which stay as they are. */
    splash_screen_set(SPLASH_STEP_STORAGE,
                      storage_mounted() ? SPLASH_OK : SPLASH_WARN,
                      (strcmp(storage_status(), "no card") == 0)
                          ? TR(SPLASH_NO_CARD) : storage_status());
    pump();

    /* Loaded above, before the panel started scanning. */
    splash_screen_set(SPLASH_STEP_SETTINGS,
                      store != NULL ? SPLASH_OK : SPLASH_WARN,
                      store != NULL ? "NVS" : TR(SPLASH_NVS_MISSING));
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
                      link_open ? "CAN 1 Mbit/s" : TR(SPLASH_NOT_OPENED));
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
                                   : busfault_verdict_text(
                                         s_busfault.verdict));
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
    /* The partition the drawings live in, looked up once. */
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
            atomic_store(&s_capabilities,
                         (unsigned)reply.regs[LINK_ID_CAPABILITIES]);
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
            const char *why = TR(SPLASH_NO_ANSWER);
            if (s_host.nacks > 0) {
                why = TR(SPLASH_REFUSED);
            } else if (s_host.mismatches > 0) {
                why = TR(SPLASH_WRONG_REPLY);
            }
            splash_screen_set(SPLASH_STEP_IOMCU, SPLASH_WARN, why);
        }
    } else {
        splash_screen_set(SPLASH_STEP_IOMCU, SPLASH_WARN, TR(SPLASH_NO_LINK));
    }
    pump();

    return ok;
}

/* ------------------------------------------------------------- the logger */

/* The control task's side of the run log.  The logger task, which makes
 * every card call, is panel_log.c; the run's kinds, its row and the queue
 * sizes are in panel.h. */

/*
 * The run's rows, counted by the task that posts them.
 *
 * Not shared with the logger, and not per run in any table: the control
 * task is the one that drops a row, it is the one that knows which run it
 * was dropping from, and it sees the run's end exactly.  Handing the count
 * across to be reported at the close instead needs a slot per run in
 * flight, and a stalled card is what puts three of them in flight -- the run
 * whose rows are still queued, and two the operator has run since.
 *
 * So the count is reported where it is kept, at the run's end.  The logger
 * reports what the logger knows: rows written, and a write that failed.
 */
log_cadence_t s_log_cad;

/* The model's gate is the sample rate's period, and its longest step the
 * totals' longest. */
_Static_assert((uint32_t)(1000.0f / PANEL_SAMPLE_HZ) == LOG_CADENCE_MODEL_MS,
               "the model's period is 1/PANEL_SAMPLE_HZ");
_Static_assert((uint32_t)(BENCH_TOTALS_MAX_STEP_S * 1000.0f)
               == LOG_CADENCE_MODEL_MAX_MS,
               "the model's longest step is the totals' longest");

/* The control task's own: the run it numbers rows with, and which kind of
 * run it has seen start.  The counter never takes the value 0, because 0 is
 * what s_log_run_now says for no run at all. */
static uint32_t       s_log_run_ctr;
log_run_kind_t        s_log_kind;

/* The control task's: the timestamp a supply run's next row carries.  A
 * bench run's is s_log_cad's. */
static float        s_log_t;

/* An automatic servo test or a limit run is running, for the control
 * task's bench and supply logs:
 * stored by the SERVO screen in the call that starts or ends a run
 * (servo_screen_on_testing()), so a row is neither written into a run nor
 * left out after it by a frame's length. */
atomic_bool s_servo_testing;

static void servo_testing_told(bool running)
{
    atomic_store(&s_servo_testing, running);
}

/*
 * A row on its way to the card, from the control task, without waiting.
 *
 * A full queue is the card falling behind the run.  The row is dropped and
 * counted rather than waited for: waiting here would put the card's latency
 * back on the safety line by a longer road.  The newest row is the one
 * dropped, so what the file holds is the run up to the stall.  A bench run's
 * time column is the wall time since the arm and shows the gap.
 */
void log_post(log_row_t *row)
{
    if (s_log_q == NULL) {
        return;
    }
    row->run = s_log_run_ctr;
    log_cadence_posted(&s_log_cad, xQueueSend(s_log_q, row, 0) == pdTRUE);
}

/* Asking the far end, exchange(): panel_link.c. */

/* ------------------------------------------------------ the board drawing */

bool supply_live_or_coming(void);

/* Publish @p bytes of @p src for @p board, or nothing for a NULL @p src. */
static void draw_publish(uint16_t board, const uint8_t *src, uint32_t bytes)
{
    if (s_draw_lock == NULL || s_drawpub == NULL) {
        return;
    }
    xSemaphoreTake(s_draw_lock, portMAX_DELAY);
    if (src != NULL && bytes != 0u && bytes <= LINK_DRAW_MAX_BYTES) {
        memcpy(s_drawpub, src, bytes);
        s_drawpub_bytes = bytes;
        s_drawpub_board = board;
    } else {
        s_drawpub_bytes = 0u;
    }
    xSemaphoreGive(s_draw_lock);
    atomic_fetch_add(&s_draw_gen, 1u);
}

/*
 * The picker asking for a board's drawing: on the way into the screen, and
 * from the UI loop whenever a link-up edge published or withdrew one.  UI
 * task only.  The copy is the picker's until the next call, and the picker
 * checks it against the board's catalogue before showing it.
 */
static void drawing_for_picker(uint16_t board)
{
    uint32_t bytes = 0u;
    if (s_draw_lock != NULL && s_drawpub != NULL && s_drawshow != NULL) {
        xSemaphoreTake(s_draw_lock, portMAX_DELAY);
        if (s_drawpub_bytes != 0u && s_drawpub_board == board) {
            memcpy(s_drawshow, s_drawpub, s_drawpub_bytes);
            bytes = s_drawpub_bytes;
        }
        xSemaphoreGive(s_draw_lock);
    }
    if (bytes == 0u) {
        (void)picker_screen_set_drawing(NULL, 0u, NULL);
        return;
    }
    const char *why = "";
    if (!picker_screen_set_drawing(s_drawshow, bytes, &why)) {
        ESP_LOGW(TAG, "hardware %u's drawing is not shown: %s",
                 (unsigned)board, why);
    }
}

/* A publication since the picker last asked: ask again. UI task only. */
static void drawing_follow(void)
{
    const unsigned gen = atomic_load(&s_draw_gen);
    if (gen != s_draw_seen) {
        s_draw_seen = gen;
        drawing_for_picker(picker_screen_binding()->board);
    }
}

static void art_keep_end(void)
{
    heap_caps_free(s_keepbuf);
    s_keepbuf = NULL;
    atomic_store(&s_keeping, false);
}

/*
 * One step of the keep, from the control task after the pass has applied
 * its arm, disarm and supply commands.  At most one sector, and none unless
 * the bench is disarmed with no arm waiting and the supply off and not
 * coming on.
 */
static void art_keep_service(void)
{
    if (!atomic_load(&s_keeping)) {
        return;
    }
    const bool open = flash_gate_open(
        arming_link_needed(&s_arm, outputs_armed(&s_out)),
        supply_live_or_coming());
    switch (art_keep_step(&s_keep, open)) {
    case ART_KEEP_WAITING:
        if (!s_keep_waited) {
            s_keep_waited = true;
            ESP_LOGI(TAG, "keeping hardware %u's drawing waits for a "
                          "bench in SAFE with the supply off",
                     (unsigned)s_keepentry.board);
        }
        break;
    case ART_KEEP_DONE:
        ESP_LOGI(TAG, "hardware %u's drawing kept",
                 (unsigned)s_keepentry.board);
        art_keep_end();
        break;
    case ART_KEEP_FAILED:
        ESP_LOGW(TAG, "hardware %u's drawing could not be kept; it will be "
                      "fetched again", (unsigned)s_keepentry.board);
        art_keep_end();
        break;
    case ART_KEEP_IDLE:
        art_keep_end();             /* not reached: s_keeping means busy */
        break;
    default:
        break;
    }
}

void art_stop(const char *why)
{
    if (s_artbuf != NULL) {
        heap_caps_free(s_artbuf);
        s_artbuf = NULL;
    }
    if (s_artbusy && why != NULL) {
        ESP_LOGW(TAG, "gave up on hardware %u's drawing: %s",
                 (unsigned)s_artboard, why);
    }
    s_artbusy = false;
}

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
 * On the link-up edge: withdraw the last drawing, ask what this one is, and
 * show the kept copy only when it is this drawing -- else fetch it.
 *
 * Two transactions and at most one flash read here; a fetch happens a slice
 * of a poll at a time below.  A coprocessor older than 4.20 serves a
 * photograph or nothing, and is drawn from its shape.
 */
void art_begin(uint16_t board)
{
    art_stop(NULL);
    draw_publish(board, NULL, 0u);
    s_artboard = board;

    if (s_far_minor < LINK_MINOR_DRAWING) {
        ESP_LOGI(TAG, "hardware %u serves no board drawing (protocol 4.%u); "
                      "PICK A PIN draws its shape", (unsigned)board,
                 (unsigned)s_far_minor);
        return;
    }
    uint16_t meta[LINK_AW_COUNT];
    uint32_t bytes = 0u;
    const char *why = "";
    if (!art_fetch_meta(&s_art_link, meta, &bytes, &why)) {
        /* Three different things end here -- no drawing, no page, and a page
         * that disagrees with itself -- and only the last is a fault. The
         * line says which rather than reporting all three as the first. */
        ESP_LOGI(TAG, "no drawing from hardware %u: %s", (unsigned)board,
                 why);
        return;
    }
    art_entry_t key;
    if (!art_fetch_key(meta, board, &key)) {
        return;         /* not reached: art_fetch_meta() checked the page */
    }

    s_artbuf = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (s_artbuf == NULL) {
        ESP_LOGW(TAG, "no room for a %u byte drawing", (unsigned)bytes);
        return;
    }
    if (s_artflash != NULL && !atomic_load(&s_keeping)
        && art_store_read_same(s_artflash, &key, s_artbuf, bytes)) {
        draw_publish(board, s_artbuf, bytes);
        ESP_LOGI(TAG, "hardware %u's drawing is kept: %u bytes, hash %08lx",
                 (unsigned)board, (unsigned)bytes, (unsigned long)key.hash);
        art_stop(NULL);
        return;
    }
    if (!art_fetch_begin(&s_artfetch, meta, s_artbuf, bytes)) {
        ESP_LOGW(TAG, "hardware %u: %s", (unsigned)board,
                 art_fetch_why(&s_artfetch));
        art_stop(NULL);
        return;
    }
    s_artbusy = true;
    ESP_LOGI(TAG, "fetching hardware %u's drawing: %u bytes, hash %08lx",
             (unsigned)board, (unsigned)bytes, (unsigned long)key.hash);
}

/* It arrived and it is the drawing it claimed.  Show it, then keep it. */
static void art_finish(void)
{
    s_artbusy = false;
    const uint32_t bytes = art_fetch_bytes(&s_artfetch);
    draw_publish(s_artboard, s_artbuf, bytes);
    if (s_artflash == NULL || atomic_load(&s_keeping)) {
        ESP_LOGW(TAG, "hardware %u's drawing arrived; it is not kept, so it "
                      "will be fetched again", (unsigned)s_artboard);
        art_stop(NULL);
        return;
    }
    s_keepentry.board  = s_artboard;
    s_keepentry.format = art_fetch_format(&s_artfetch);
    s_keepentry.width  = art_fetch_width(&s_artfetch);
    s_keepentry.height = art_fetch_height(&s_artfetch);
    s_keepentry.crc    = art_fetch_crc(&s_artfetch);
    s_keepentry.hash   = art_fetch_hash(&s_artfetch);
    s_keepentry.bytes  = bytes;
    /*
     * Hand the buffer over and stop owning it.  Nothing is written here:
     * art_keep_service() takes it a sector a pass, on a safe bench.  The
     * fetch checked the payload's CRC and hash; the keep's read-back checks
     * the flash against them before the header goes in.
     */
    s_keepbuf = s_artbuf;
    s_artbuf  = NULL;
    if (!art_keep_begin(&s_keep, s_artflash, s_keepentry.board, &s_keepentry,
                        s_keepbuf)) {
        ESP_LOGW(TAG, "hardware %u's drawing does not fit a slot; it will "
                      "be fetched again", (unsigned)s_artboard);
        heap_caps_free(s_keepbuf);
        s_keepbuf = NULL;
        return;
    }
    s_keep_waited = false;
    atomic_store(&s_keeping, true);
}

/*
 * One bounded slice of the fetch.
 *
 * A block at a time until the slice is spent: this task also beats the
 * safety line, and its ceiling is 150 ms.  The sequence does exactly the
 * blocks it is given, so how much of a poll goes to a drawing is decided
 * here and nowhere else.
 */
void art_slice(void)
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

/* The control page: panel_link.c. */

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


/* ------------------------------------------------------------------- main */

/* --------------------------------------------------------- the control task */

/* ------------------------------------------------------------- the supply */

/*
 * The programmable supply.
 *
 * The PD mini, driven by the coprocessor over the SUPPLY page (protocol 4.3)
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
static supply_sim_t   s_supply_sim;
supply_link_t         s_supply_link;
/* The link modules' state, SENSE to PROG: panel_services.c. */
/*
 * The supply command a programming port left on the SUPPLY page, for the
 * SUPPLY screen's set points: the values, and a count the control task
 * raises with each.  The render loop puts them on the screen and stores the
 * count it served; until then the screen's older set points are not sent.
 */
static atomic_uint        s_supply_adopt_mv;
static atomic_uint        s_supply_adopt_ma;
static atomic_uint        s_supply_adopt_gen;
static atomic_uint        s_supply_adopt_done;
/*
 * The ESC a stick run programs, when the supply is the panel's model.
 *
 * The PROGRAMMER screen publishes the profile of a run under way, and how
 * many runs it has started, from the render loop; the control task makes
 * the model's current the simulated ESC's, so the run counts beeps from the
 * same samples the PD mini would give it.  The ESC sees the throttle the
 * control page carries -- the raw command, as a pin bound on the
 * coprocessor does -- and no signal while the bank is disarmed.
 */
static atomic_uintptr_t s_escsim_profile;
static atomic_uint      s_escsim_runs;
static esc_sim_t        s_escsim;          /* control task only */
static unsigned         s_escsim_runs_seen;
static bool             s_escsim_live;
/* The coprocessor that answered speaks protocol 4.3: it has the page. */
bool s_supply_page;
/* And speaks 4.4: AUTO baud and the BAUD_FOUND register. */
bool s_supply_auto;
/* The PD mini, not the model, is the supply; see supply_real_follow(). */
bool                  s_supply_is_real;
static bool           s_supply_on;
supply_state_t        s_supply;
static uint32_t       s_supply_ms;
static bool           s_supply_fresh;
/* The stop count the supply has answered; see supply_follow_stops(). */
static uint32_t       s_supply_stops_served;
/* The trips' time over their thresholds; see supply_trip_step(). */
static supply_trip_t  s_supply_trip;
/* When the supply was last stepped, on its own cadence; see supply_pump(). */
static uint32_t       s_supply_step_ms;
/*
 * The ON in flight, watched as an arm is (s_arm_watch): a hold that completed
 * on a contact whose events went missing is an ON the operator may not have
 * made, and the render side can cancel only an ON it has not yet handed
 * over.  s_supply_gen numbers the ONs applied; the snapshot carries it.
 */
static arm_watch_t    s_supply_watch;
uint32_t              s_supply_gen;

/*
 * The interval since the last step, up to now, on the reading that held
 * through it: counted into the totals and, while a supply run is being
 * logged, written as the run's last row, so the log ends on the totals the
 * screen shows.  For the moments a supply run ends between two steps -- the
 * output switched off, or the bench taking the log over.
 */
static void supply_log_tail(void)
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

static void supply_switch(bool on)
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

/*
 * Every stop switches the output off: STOP, touch that died, an arm the
 * watch undid, the far end's refusal.  Called from control_pump(), which
 * runs inside every exchange's wait, and again wherever the supply is
 * stepped or switched, so a stop counted anywhere cuts the output before
 * the next reading is taken or logged.
 */
static void supply_follow_stops(void)
{
    const uint32_t stops = arming_stop_count(&s_arm);
    if (stops != s_supply_stops_served) {
        s_supply_stops_served = stops;
        supply_switch(false);
    }
}

static void supply_follow_set(void)
{
    unsigned mv = atomic_load(&s_supply_set_mv);
    unsigned ma = atomic_load(&s_supply_set_ma);
    /* Under the caps, whatever the screen sent: the screen keeps its set
     * points under them already, and this is what the supply obeys. */
    const unsigned vmax = atomic_load(&s_supply_vmax_mv);
    const unsigned imax = atomic_load(&s_supply_imax_ma);
    /* A programming port holds the supply: the set points are the port's,
     * and neither the screen's nor a limit that moved is written over
     * them.  The coprocessor refuses it too (supply_page.h). */
    if (s_kst_taken) {
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
 * An OFF asked for, whether or not its queue entry survived; applying it
 * twice costs nothing.  Called before the drain, so an OFF is never left
 * waiting behind the queue's backlog.  And the set points, every pass.
 */
static void supply_service(void)
{
    if (atomic_exchange(&s_supply_off_request, false)) {
        supply_switch(false);
    }
    supply_follow_set();
}

/*
 * What the supply screen asked for.  An ON completes a hold, so it goes the
 * way an arm does: not past a stop, an OFF, a change to the PD mini's
 * wiring or a touch loss that came after the screen posted it, and watched after it is taken until the render side
 * has seen the output on (supply_watch_service()).  The operator repeats the
 * hold.
 */
static void apply_supply_cmd(const panel_cmd_t *pc)
{
    const supply_cmd_t *c = &pc->supply;
    if (c->reset) {
        supply_reset_peaks(&s_supply);
    }
    if (c->module_reset && s_supply_is_real) {
        supply_switch(false);
        /* RESET is a 4.4 register: an older coprocessor would refuse it
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
        if (!s_kst_taken
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
 * enables it -- not answering while no coprocessor that speaks the SUPPLY
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
     * answers again.  The model always answers.
     */
    if (!s_supply.online && s_supply_on) {
        supply_switch(false);
        control_alert(TR(ALERT_SUPPLY_SILENT));
    } else if (s_supply_is_real && s_supply_on
               && supply_link_blind(&s_supply_link, now_ms())) {
        /* The page answers and the module reads ONLINE, but its reading
         * count has stood for SUPPLY_LINK_BLIND_MS: readings marked not
         * valid, which no trip acts on.  Off, as for a supply gone. */
        supply_switch(false);
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
        supply_switch(false);
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

static void arm_applied(void)
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
static void arm_watch_service(bool link_up)
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
    bool owed = false;

    if (atomic_exchange(&s_servo_release_request, false)) {
        s_servo_held.kind = SERVO_CMD_NONE;
        s_servo_release_owed = true;
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
        arming_stop_pressed(&s_arm);        /* the band's STOP, pressed */
    }

    /*
     * Every stop lets go of what was being driven, whether or not the bench
     * was armed and whichever thing raised it -- a press, the far end, touch
     * that stopped answering.  A bench that was not armed still had a servo
     * held, and nothing else would have released it: the position would go on
     * being refreshed every 100 ms, over the top of an outputs binding made
     * later.
     */
    service_disarm(link_up);

    if (arming_stop_unserved(&s_arm, &s_stops_served)) {
        throttle_to_zero();
        servo_let_go();
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
        servo_let_go();
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
                                    .port_taken = s_kst_taken };
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
         * And the surfaces are centred before the arm, not after it.  The far
         * end applies ARM and stamps every channel's clock before it steps its
         * outputs, so a channel still holding a position at that moment
         * renders it for as long as the centre takes to arrive.
         */
        servo_service(link_up);
        facts.release_owed = link_up && s_servo_release_owed;
        /* Asked only where the answer decides: asking can take an
         * exchange. */
        facts.rate_settled = !link_up || facts.release_owed
                             || servo_rate_settled();
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
             * the simulator included -- until a coprocessor answered again.
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
             * (1000 ms) late.  Past it the arm is given up and nothing
             * further is written (arming_handshake_open()): before CLEAR
             * as a refusal, after it as a stop, whose withheld heartbeat
             * sets the far end's latch again.  The frame that arms is the
             * limit: acknowledged late, it has armed the far end, at rest.
             */
            if (!control_clear_failsafe(&ack)) {
                arm_write_failed(quiet, TR(ALERT_ARM_REFUSED));
                break;
            }
            facts.disarm_asked = atomic_load(&s_disarm_request);
            const arming_hs_t cleared =
                arming_handshake_step(&s_arm, ARMING_HS_CLEARED, &facts,
                                      now_ms());
            if (cleared == ARMING_HS_WITHDRAWN) {
                /* Stopped or disarmed while the clear was in flight.  No
                 * alert: the operator asked for this and knows. */
                break;
            }
            if (cleared != ARMING_HS_GO) {
                control_alert(TR(ALERT_ARM_REFUSED));
                break;
            }
            if (!control_arm(&ack)) {
                arm_write_failed(quiet, TR(ALERT_ARM_REFUSED));
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
 * The outputs screen's choice, onto the coprocessor's two output pages.
 *
 * Leaves what became of it in s_outputs_result, which app_main hands to the
 * screen; screen state is app_main's alone.  True when a page may have been
 * written: the rate reset that leads the sequence landed, or there is none.
 */
void servo_rate_reset_note(bool page, bool landed);

static bool write_output_binding(const outbind_t *bind)
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
 * Which channels the horn drives: the ones the binding marks as surfaces.
 *
 * Written and read by the control task alone, which is the only task that
 * puts anything on the wire, and refreshed from the binding read back in
 * read_outputs_binding().
 *
 * Zero is a bench with nothing bound as a surface, and then the horn drives
 * nothing.  Falling back to a channel number instead is how this screen came
 * to command whatever was bound first, which on a bench with an ESC on the
 * lowest pin is the motor.
 */
uint8_t s_servo_channels;
/* The GPIOs of those channels' slots, for the SERVO screen's TARGET row;
 * whether a binding was read. */
atomic_ullong s_servo_pins;
atomic_bool   s_servo_pins_known;
/* For the screen's SWEEP: a binding read, with a surface in it. */
static atomic_bool s_servo_surfaces;
/* For the screen's runs: the SENSE page holds the encoder enabled. */
atomic_bool s_enc_held;
/* A sweep command with no surface to sweep: the screen stops waiting. */
static atomic_bool s_sweep_refused;
/* The pause the panel last let go of: commands asked during it and queued
 * behind the HOLD that ended it are not sent (servo_cmd_stale()). */
static servo_pause_end_t s_pause_ended;

/*
 * Whether that mask is an answer at all.
 *
 * Empty and unknown are different benches.  Empty is a binding that was read
 * and names no surface -- a bench carrying only a motor, which must still
 * arm.  Unknown is a binding nobody has read, or one whose write did not come
 * back: the far end may be rendering surfaces this end cannot name, and
 * treating that as empty would let a release report success without settling
 * them and let the arm that follows drive them.
 *
 * False until a binding has been read from the far end.  Nothing this end
 * merely sent counts: a write whose acknowledgement was lost was applied over
 * there all the same.
 */
bool s_servo_known;

/*
 * And whether the last position actually reached any of them.
 *
 * Separate from s_servo_channels because the two can differ: a write that
 * failed part way through leaves some surfaces holding a position and not
 * others.  It says a release is owed, not where the release goes -- that is
 * always the channels bound as surfaces now, because a channel that has
 * stopped being one must not be written by this screen.
 */
uint8_t s_servo_written;

/*
 * For the screen's SWEEP and its horn: there is something to move.  A
 * programming port as target is one output; with the standard outputs it is
 * a binding read with a surface in it.
 */
void servo_surfaces_publish(void)
{
    atomic_store(&s_servo_surfaces,
                 prog_link_target(&s_prog_link) != 0u
                 || (s_servo_known && s_servo_channels != 0u));
}

/*
 * One run's range and speed, to the standard outputs' CHAN_CFG or, with
 * programming port @p port (1 + n) as target, to that port's registers.  A
 * port is written as channel 0 of @p cfg.
 */
bool servo_cfg_write(uint8_t port, uint8_t first, uint8_t count,
                     const uint16_t *cfg, link_msg_t *reply)
{
    if (port != 0u) {
        uint16_t regs[PROG_LINK_CFG_REGS];
        prog_link_cfg(cfg, regs);
        return write_regs(&s_host, LINK_PAGE_PROG,
                          LINK_PG_REG(port - 1u, LINK_PG_SLEW),
                          PROG_LINK_CFG_REGS, regs, reply)
               && reply->op == LINK_OP_ACK;
    }
    return write_regs(&s_host, LINK_PAGE_CHAN_CFG,
                      (uint8_t)(first * LINK_CC_STRIDE),
                      (uint8_t)(count * LINK_CC_STRIDE),
                      &cfg[(size_t)first * LINK_CC_STRIDE], reply)
           && reply->op == LINK_OP_ACK;
}

/* And its command, 0 to LINK_CH_SPAN: CHANNELS, or the port's CMD. */
static bool servo_cmd_write(uint8_t port, uint8_t first, uint8_t count,
                            const uint16_t *cmd, link_msg_t *reply)
{
    if (port != 0u) {
        return write_regs(&s_host, LINK_PAGE_PROG,
                          LINK_PG_REG(port - 1u, LINK_PG_CMD), 1u, cmd, reply)
               && reply->op == LINK_OP_ACK;
    }
    return write_regs(&s_host, LINK_PAGE_CHANNELS, first, count, &cmd[first],
                      reply)
           && reply->op == LINK_OP_ACK;
}

/* The channels of one run, as bits. */
static uint8_t servo_run_bits(uint8_t first, uint8_t count)
{
    uint8_t bits = 0u;
    for (uint8_t c = first; c < first + count; ++c) {
        bits |= (uint8_t)(1u << c);
    }
    return bits;
}

/*
 * The next run of set channels in @p mask at or after @p from.
 *
 * Runs rather than single channels: a binding's surfaces are contiguous
 * unless a motor sits between them, and each exchange on this wire can wait a
 * second.  Returns false when there is no run left.
 */
static bool servo_next_run(uint8_t mask, uint8_t from, uint8_t *first,
                           uint8_t *count)
{
    uint8_t i = from;
    while (i < LINK_OUT_CHANNELS && (mask & (uint8_t)(1u << i)) == 0u) {
        ++i;
    }
    if (i >= LINK_OUT_CHANNELS) {
        return false;
    }
    *first = i;
    while (i < LINK_OUT_CHANNELS && (mask & (uint8_t)(1u << i)) != 0u) {
        ++i;
    }
    *count = (uint8_t)(i - *first);
    return true;
}

/*
 * One servo command, as configuration and pulse.
 */
/* Whether the operator has asked, since this began, for the thing being
 * written to stop: a stop applied by the pump, or a disarm or release posted
 * while an exchange was on the wire. */
static bool servo_countermanded(void)
{
    return arming_stopped(&s_arm)
           || atomic_load(&s_disarm_request)
           || atomic_load(&s_servo_release_request)
           || atomic_load(&s_servo_hold_request);
}

/*
 * The range and speed the SERVO screen last named.  A release goes to the
 * midpoint of the range the far end holds, which is the one the last
 * position carried; a profile chosen since, with no position under it yet,
 * would be released at the old profile's centre.  So a release restates the
 * range first.  Kind NONE until a command names one.
 */
servo_cmd_t s_servo_range;

/* Whether a command names a range the far end takes.  A command that names
 * none keeps the standard one. */
bool servo_named(const servo_cmd_t *sv)
{
    return sv->max_us > sv->min_us
           && sv->min_us >= LINK_CC_FLOOR_US
           && sv->max_us <= LINK_CC_CEILING_US;
}

/* The CHAN_CFG page for the surfaces under @p sv's range and speed. */
void servo_cfg(const servo_cmd_t *sv, uint16_t *cfg)
{
    const bool named = servo_named(sv);
    for (uint8_t i = 0; i < LINK_OUT_CHANNELS; ++i) {
        uint16_t *r = &cfg[(size_t)i * LINK_CC_STRIDE];
        r[LINK_CC_ROLE] = LINK_CC_ROLE_SURFACE;
        /* What the screen's SPEED means at this end: the rate the bench
         * is allowed to move the output, rather than a number that only
         * changed the drawing. */
        r[LINK_CC_SLEW]   = sv->slew_per_s;
        r[LINK_CC_MIN_US] = named ? sv->min_us : (uint16_t)SERVO_MIN_US;
        r[LINK_CC_MAX_US] = named ? sv->max_us : (uint16_t)SERVO_MAX_US;
    }
}

/*
 * The sweep on the SERVO page (protocol 4.2).  While one runs the
 * coprocessor commands the surfaces itself every pass, so a centre or a
 * position written under it would last one pass: anything but a sweep stops
 * it first.  Not known after the link comes up: then the next command, a
 * sweep included, writes a stop first, which also clears a finished sweep
 * that the same curve would otherwise not start again.
 */
bool               s_servo_sweep_page;
bool               s_servo_sweeping;
bool               s_servo_holding;      /* LINK_SV_HOLD in force there */
static uint32_t    s_servo_hold_ms;      /* when the far end last took it */
static atomic_bool s_servo_hold_lost;    /* for the screen: it let go */
static atomic_uint s_servo_hold_lost_seq; /* of this pause */
bool               s_servo_sweep_unknown;
atomic_bool        s_servo_sweep_able;   /* for the screen's SWEEP */
uint16_t           s_servo_minor;        /* its protocol minor: 6 resumes */
/* The curve the far end last took, and when it started one: a curve that
 * differs from the one running starts over there, and the screen draws the
 * horn from that moment rather than from its tap. */
static uint16_t    s_servo_curve[4];
static uint32_t    s_servo_curve_ms;   /* when the far end last took it */
static atomic_uint s_sweep_start_ms;
static atomic_uint s_sweep_start_from;   /* servo_sweep_from_t */
static atomic_uint s_sweep_frozen_ms;    /* when it froze, for FROZEN */
static atomic_bool s_sweep_start_new;
static atomic_uint s_sweep_start_seq;    /* the command it acknowledges */
/* The far end's phase as acknowledgements time it, and the HOLD that
 * paused a running sweep: which pause, and the phase it kept. */
servo_phase_t s_far_phase;
/* A HOLD write that went unanswered: it may have reached the far end, so
 * the phase it kept is not known here when a retry is acknowledged. */
bool          s_hold_unanswered;
static atomic_uint s_sweep_held_seq;
static atomic_uint s_sweep_held_kept;
static atomic_bool s_sweep_held_new;

/* The held sweep's resume has been taken: its repeats say the curve, and
 * publish no further resumed start. */
static void servo_resume_taken(uint16_t start_seq)
{
    if (s_servo_held.kind == SERVO_CMD_SWEEP
        && s_servo_held.start_seq == start_seq) {
        s_servo_held.resume = false;
    }
}

static bool write_servo(const servo_cmd_t sv)
{
    link_msg_t reply;
    /*
     * What a command moves is the target the PROG page holds (4.13): the
     * standard outputs or a programming port.  Nothing is sent before the
     * page is read, because whose pulse it would be is not known.  A release
     * goes to the target in force, which is where the position was written;
     * every other command waits until that target is the one the screen
     * asks for (prog_link_service()).  A coprocessor without the page has
     * the standard outputs.
     */
    if (!prog_link_command(&s_prog_link, sv.kind == SERVO_CMD_RELEASE)) {
        return false;
    }
    const uint8_t port = prog_link_target(&s_prog_link);
    if (s_servo_sweep_unknown
        || ((s_servo_sweeping || s_servo_holding)
            && sv.kind != SERVO_CMD_SWEEP && sv.kind != SERVO_CMD_HOLD)) {
        const uint16_t stop = 0u;
        if (!write_regs(&s_host, LINK_PAGE_SERVO, LINK_SV_SWEEP, 1u, &stop,
                        &reply)
            || reply.op != LINK_OP_ACK) {
            return false;
        }
        s_servo_sweeping      = false;
        s_servo_holding       = false;
        s_servo_sweep_unknown = false;
        servo_phase_stopped(&s_far_phase);
    }
    if (sv.kind == SERVO_CMD_HOLD) {
        /*
         * The far end freezes every surface where its output is and keeps
         * it there while this is repeated.  A coprocessor that cannot sweep
         * has nothing to hold.
         */
        if (!s_servo_sweep_page) {
            return true;
        }
        /*
         * Unrepeated for as long as a channel command is trusted, the far
         * end ended the hold and the surfaces rested; a HOLD now would hold
         * wherever resting had got them.  So the hold is over: the surfaces
         * are released to their centre, a state both ends know, and the
         * screen is told.
         */
        const uint32_t since = now_ms() - s_servo_hold_ms;
        if (s_servo_holding && since > OUT_DEFAULT_TIMEOUT_MS) {
            s_pause_ended.on        = true;
            s_pause_ended.pause_seq = servo_cmd_pause_root(&sv);
            s_servo_holding      = false;
            s_servo_held.kind    = SERVO_CMD_NONE;
            s_servo_release_owed = true;
            atomic_store(&s_servo_hold_lost_seq,
                         (unsigned)servo_cmd_pause_root(&sv));
            atomic_store(&s_servo_hold_lost, true);
            return true;
        }
        const uint16_t hold = LINK_SV_HOLD;
        const uint32_t sent = now_ms();
        if (!write_regs(&s_host, LINK_PAGE_SERVO, LINK_SV_SWEEP, 1u, &hold,
                        &reply)) {
            s_hold_unanswered = s_hold_unanswered
                                || (s_servo_sweeping && !s_servo_holding);
            return false;
        }
        if (reply.op != LINK_OP_ACK) {
            return false;
        }
        const uint32_t took = now_ms();
        /*
         * A pause whose HOLD was answered only at a retry may have frozen at
         * an earlier attempt, been let go 500 ms later and frozen again
         * where resting had got it; an exchange that outlasted a HOLD's life
         * may have been let go meanwhile too.  Where the output is held is
         * then not known here, and an angle drawn from the tap would be
         * said again to the servo by the next change of profile.  So the
         * hold is over: the surfaces are released to their centre, a state
         * both ends know, and the screen is told, as for a HOLD left
         * unrepeated.
         */
        if ((s_hold_unanswered && s_servo_sweeping && !s_servo_holding)
            || (uint32_t)(took - sent) > OUT_DEFAULT_TIMEOUT_MS) {
            /*
             * The HOLD just acknowledged is live there, and while it is the
             * far end stamps every surface with that HOLD's arrival: a
             * position written under it would go to rest 500 ms after the
             * arrival, at once and unslewed.  So it is ended first with a
             * stop; one that does not land is written before the next
             * command instead.
             */
            const uint16_t stop = 0u;
            if (!write_regs(&s_host, LINK_PAGE_SERVO, LINK_SV_SWEEP, 1u,
                            &stop, &reply)
                || reply.op != LINK_OP_ACK) {
                s_servo_sweep_unknown = true;
            }
            s_pause_ended.on        = true;
            s_pause_ended.pause_seq = servo_cmd_pause_root(&sv);
            s_hold_unanswered    = false;
            s_servo_sweeping     = false;
            s_servo_holding      = false;
            servo_phase_stopped(&s_far_phase);
            s_servo_held.kind    = SERVO_CMD_NONE;
            s_servo_release_owed = true;
            atomic_store(&s_servo_hold_lost_seq,
                         (unsigned)servo_cmd_pause_root(&sv));
            atomic_store(&s_servo_hold_lost, true);
            /* Not taken as held: a caller that voids an owed release on
             * success would cancel the one just owed. */
            return false;
        }
        /* The hold that paused a running sweep: the far end keeps the
         * curve's phase as of now, and the screen draws the pause from it
         * rather than from the tap. */
        if (s_servo_sweeping && !s_servo_holding) {
            const uint32_t kept = servo_phase_held(&s_far_phase, took);
            atomic_store(&s_sweep_held_seq, (unsigned)sv.pause_seq);
            atomic_store(&s_sweep_held_kept, kept);
            atomic_store(&s_sweep_held_new, true);
        }
        if (!s_servo_sweeping && !s_servo_holding) {
            /* A hold of no sweep: nothing to resume. */
            servo_phase_stopped(&s_far_phase);
        }
        s_hold_unanswered = false;
        s_servo_sweeping = false;
        s_servo_holding  = true;
        /* From the send: the far end heard it no earlier than that. */
        s_servo_hold_ms  = sent;
        s_servo_written |= port != 0u ? (uint8_t)0x01u : s_servo_channels;
        return true;
    }
    /*
     * Every command, a release included, goes to the channels the binding
     * marks as surfaces now -- never to the ones this process wrote earlier.
     *
     * A channel that has stopped being a surface is not this screen's to
     * settle, and centring it would be the defect this file just stopped
     * committing from the other side: mid-travel is a surface's rest and half
     * power on a throttle, so a channel rebound as a motor between the drag
     * and the release would be commanded to half throttle.  What settles it
     * instead is the rebinding itself, which carries the new role and the
     * rest that goes with it, or the unbinding, after which no slot renders
     * the channel at all.
     *
     * Reaching what this process never wrote is deliberate: after a panel
     * restart the far end still holds whatever it was left at, and centring a
     * surface that is not holding anything costs nothing.
     */
    /*
     * An unknown binding is not an empty one.  Returning true here would pay
     * a release that settled nothing and let the bench arm onto surfaces the
     * far end is still rendering; returning false keeps the debt, and
     * ARMING_ACT_ARM refuses the arm and says so.  It is paid as soon as a
     * read succeeds, which the poll loop retries every second.
     */
    if (port == 0u && !s_servo_known) {
        return false;
    }
    /* A port is one output, kept as bit 0 of what was written. */
    const uint8_t mask = port != 0u ? (uint8_t)0x01u : s_servo_channels;
    /*
     * Nothing is bound as a surface, or nothing is holding a position, so
     * there is nothing to say.  True rather than false: a false here would
     * leave a release owed for ever, and ARMING_ACT_ARM refuses to arm while
     * one is -- a bench with only a motor on it would stop arming.
     */
    if (mask == 0u) {
        /* A sweep of nothing never starts, so no start is published for it:
         * the screen is told instead, or it would wait for one. */
        if (sv.kind == SERVO_CMD_SWEEP) {
            atomic_store(&s_sweep_refused, true);
            /* And not kept to be said again: a binding with a surface,
             * after a reconnect, would sweep it with no tap. */
            s_servo_held.kind = SERVO_CMD_NONE;
        }
        return true;
    }
    uint8_t first = 0u, count = 0u;
    if (sv.kind == SERVO_CMD_RELEASE) {
        /*
         * Stop holding the surfaces where the horn put them: each is
         * commanded to the centre it rests at.  The slots are the operator's,
         * written from the OUTPUTS screen, and are not touched -- a screen
         * that cleared one would take away the wiring the operator described,
         * and on a bench where a motor holds the lowest pin it would take
         * away the motor's.
         *
         * Whether the far end took it is returned rather than assumed: a
         * write that did not land leaves the surface where it was, and the
         * caller's record of owing the release is the only thing that would
         * notice.
         */
        uint16_t centre[LINK_OUT_CHANNELS];
        for (uint8_t i = 0; i < LINK_OUT_CHANNELS; ++i) {
            centre[i] = (uint16_t)(LINK_CH_SPAN / 2u);
        }
        /* The midpoint of the range the screen last named, which the
         * screen centres on its PULSE CENTRE. */
        const bool ranged = s_servo_range.kind != SERVO_CMD_NONE;
        uint16_t cfg[LINK_CC_COUNT];
        servo_cfg(&s_servo_range, cfg);
        for (uint8_t at = 0u; servo_next_run(mask, at, &first, &count);
             at = (uint8_t)(first + count)) {
            if (ranged
                && !servo_cfg_write(port, first, count, cfg, &reply)) {
                return false;
            }
            if (!servo_cmd_write(port, first, count, centre, &reply)) {
                return false;
            }
            /* Settled, so no longer holding anything.  Run by run, because a
             * later one can still fail and the debt is what is left. */
            s_servo_written &= (uint8_t)~servo_run_bits(first, count);
        }
        /*
         * And nothing is held anywhere else either.  A bit left over names a
         * channel that has stopped being a surface, which this screen must
         * not write and whose own rebinding has already given it a rest;
         * keeping it would owe a release that no write can ever pay.
         */
        s_servo_written = 0u;
        return true;
    } else {
        /*
         * Configuration and command, sent whole every time.  The coprocessor
         * may have reset since the last write, so the range the pulse is
         * clamped against is restated with each pulse.
         */
        /*
         * The endpoints the screen named, not this file's.  A narrow servo
         * runs 660 to 860 us and its centre is below a standard servo's
         * floor, so clamping it against 1000 to 2000 would send its whole
         * travel to one end.  A command that names no range keeps the
         * standard one.
         */
        const bool named = servo_named(&sv);
        const uint16_t min_us = named ? sv.min_us : (uint16_t)SERVO_MIN_US;
        const uint16_t max_us = named ? sv.max_us : (uint16_t)SERVO_MAX_US;
        const uint16_t span = us_to_span(sv.value_us, min_us, max_us);

        /*
         * Only the channels the binding marked surfaces are written, and the
         * role written to them is the one they already carry.  A write that
         * reached further would be this screen deciding what a channel is
         * for, and a motor channel told it is a surface rests at mid-travel,
         * which on a throttle is half power.
         */
        uint16_t cfg[LINK_CC_COUNT];
        uint16_t cmd[LINK_OUT_CHANNELS];
        servo_cfg(&sv, cfg);
        for (uint8_t i = 0; i < LINK_OUT_CHANNELS; ++i) {
            cmd[i] = span;
        }
        if (sv.kind == SERVO_CMD_SWEEP) {
            /*
             * The range first, as for a position, then the curve in one
             * frame.  A sweep that is starting first clears the movement
             * count, so it runs until it is stopped; one that is running is
             * only repeated, which keeps it going.
             */
            for (uint8_t at = 0u; servo_next_run(mask, at, &first, &count);
                 at = (uint8_t)(first + count)) {
                if (!servo_cfg_write(port, first, count, cfg, &reply)) {
                    return false;
                }
                if (servo_countermanded()) {
                    return false;
                }
            }
            const uint16_t curve[4] = { sv.sweep_kind, sv.sweep_mhz,
                                        sv.sweep_span, sv.sweep_dwell_ms };
            /*
             * A resume of the sweep held there carries it on from its phase
             * (4.6).  An older coprocessor, or one that refuses -- the hold
             * ended there, or the curve changed -- gets the curve whole and
             * starts it over, and the operator is told once it has.
             */
            /*
             * A HOLD that went unanswered and was never acknowledged may or
             * may not have reached the far end, so it is either running or
             * holding: the resume starts the curve over from a stop, and the
             * operator is told.
             */
            const bool held_sweep = (s_servo_holding
                                     && servo_phase_resumable(&s_far_phase))
                                    || s_hold_unanswered;
            const bool timed = !s_hold_unanswered;
            servo_resume_t plan = servo_page_resume_plan(
                sv.resume, held_sweep, timed, s_servo_minor, false);
            if (plan == SERVO_RESUME_WRITE) {
                const uint16_t resume = LINK_SV_RESUME;
                if (!write_regs(&s_host, LINK_PAGE_SERVO, LINK_SV_SWEEP, 1u,
                                &resume, &reply)) {
                    /* It may have taken: the next command stops first. */
                    s_servo_sweep_unknown = true;
                    return false;
                }
                if (reply.op == LINK_OP_ACK) {
                    const uint32_t took = now_ms();
                    memcpy(s_servo_curve, curve, sizeof(curve));
                    s_servo_curve_ms = took;
                    s_servo_sweeping = true;
                    s_servo_holding  = false;
                    /* Phase 0 moved on by the hold; the screen draws from
                     * it whatever phase it took the pause to have. */
                    uint32_t start = took;
                    (void)servo_phase_resumed(&s_far_phase, took, &start);
                    s_hold_unanswered = false;
                    /* And when it began to move again, for the drawing. */
                    atomic_store(&s_sweep_frozen_ms, took);
                    atomic_store(&s_sweep_start_ms, start);
                    atomic_store(&s_sweep_start_seq, (unsigned)sv.start_seq);
                    atomic_store(&s_sweep_start_from,
                                 (unsigned)SERVO_SWEEP_RESUMED);
                    atomic_store(&s_sweep_start_new, true);
                    servo_resume_taken(sv.start_seq);
                    s_servo_written |= mask;
                    return true;
                }
                plan = servo_page_resume_plan(sv.resume, held_sweep, timed,
                                              s_servo_minor, true);
            }
            if (plan == SERVO_RESUME_TOO_OLD || plan == SERVO_RESUME_REFUSED
                || plan == SERVO_RESUME_UNTIMED) {
                /*
                 * Over from the beginning whatever the far end is doing.  A
                 * RESUME whose acknowledgement was lost has it running from
                 * the kept phase, and a refusal of the retry would leave the
                 * curve written next carrying that on while the screen draws
                 * it from the start.  A stop first makes it a start there.
                 */
                const uint16_t stop = 0u;
                if (!write_regs(&s_host, LINK_PAGE_SERVO, LINK_SV_SWEEP, 1u,
                                &stop, &reply)
                    || reply.op != LINK_OP_ACK) {
                    return false;
                }
                /* Stopped there: what follows is a start, from rest. */
                s_servo_sweeping = false;
                s_servo_holding  = false;
                servo_phase_stopped(&s_far_phase);
            }
            if (!s_servo_sweeping) {
                const uint16_t endless = 0u;
                if (!write_regs(&s_host, LINK_PAGE_SERVO, LINK_SV_SWEEP_MOVES,
                                1u, &endless, &reply)
                    || reply.op != LINK_OP_ACK) {
                    return false;
                }
                /* And asked again before the write that starts it moving:
                 * the pump runs inside that wait as inside the others. */
                if (servo_countermanded()) {
                    return false;
                }
            }
            if (!write_regs(&s_host, LINK_PAGE_SERVO, LINK_SV_SWEEP, 4u, curve,
                            &reply)) {
                /* It may have taken: the next command stops first. */
                s_servo_sweep_unknown = true;
                return false;
            }
            if (reply.op != LINK_OP_ACK) {
                return false;
            }
            if (plan == SERVO_RESUME_TOO_OLD) {
                control_alert(TR(ALERT_SWEEP_RESUME_OLD));
            } else if (plan == SERVO_RESUME_REFUSED) {
                control_alert(TR(ALERT_SWEEP_RESUME_REFUSED));
            } else if (plan == SERVO_RESUME_UNTIMED) {
                control_alert(TR(ALERT_SWEEP_RESUME_UNTIMED));
            }
            /* A start there: no sweep running, a changed curve, or one the
             * far end stopped because nothing repeated it for as long as a
             * channel command is trusted, and starts again from zero. */
            const uint32_t took = now_ms();
            const uint32_t gap = took - s_servo_curve_ms;
            servo_sweep_from_t from = SERVO_SWEEP_FROM_HERE;
            bool started = true;
            if (!s_servo_sweeping) {
                from = SERVO_SWEEP_FROM_REST;
            } else if (gap > OUT_DEFAULT_TIMEOUT_MS) {
                /* It froze when the last write went unrepeated for that
                 * long; this starts it again from there. */
                from = SERVO_SWEEP_FROM_FROZEN;
                atomic_store(&s_sweep_frozen_ms,
                             s_servo_curve_ms + OUT_DEFAULT_TIMEOUT_MS);
            } else if (memcmp(curve, s_servo_curve, sizeof(curve)) == 0) {
                started = false;   /* repeated: it carries on */
            }
            if (!started && sv.resume) {
                /*
                 * A resume whose HOLD never left: the far end ran on, and
                 * this write only repeats its curve.  The screen waits for a
                 * start to draw from, so it gets one, at the curve's own
                 * phase 0, from where the output is.
                 */
                atomic_store(&s_sweep_start_ms, s_far_phase.start_ms);
                atomic_store(&s_sweep_start_seq, (unsigned)sv.start_seq);
                atomic_store(&s_sweep_frozen_ms, took);
                atomic_store(&s_sweep_start_from,
                             (unsigned)SERVO_SWEEP_RESUMED);
                atomic_store(&s_sweep_start_new, true);
                s_hold_unanswered = false;
                servo_resume_taken(sv.start_seq);
            }
            if (started) {
                servo_phase_started(&s_far_phase, took);
                s_hold_unanswered = false;
                /* A resume that fell back to this start is used up: its
                 * repeats are the curve, not resumes. */
                servo_resume_taken(sv.start_seq);
                memcpy(s_servo_curve, curve, sizeof(curve));
                atomic_store(&s_sweep_start_ms, took);
                atomic_store(&s_sweep_start_seq, (unsigned)sv.start_seq);
                atomic_store(&s_sweep_start_from, (unsigned)from);
                atomic_store(&s_sweep_start_new, true);
            }
            s_servo_curve_ms = took;
            s_servo_sweeping = true;
            s_servo_holding  = false;
            /* Every surface is moving, so a release owes each a centre. */
            s_servo_written |= mask;
            return true;
        }
        /*
         * What the channel is, then what it is to do.  Each is its own
         * transaction and the far end steps its outputs between them, so a
         * range that had not arrived would clamp the pulse against the one
         * before it: a narrow servo selected against a standard configuration
         * renders 1500 us, past its 860 us maximum.  The 100 ms refresh is
         * the retry.
         *
         * Both waits can take a second and the pump runs inside them, so a
         * stop can be applied and a disarm posted between one and the next.
         * Giving up part way leaves the surfaces already written holding what
         * was asked for, and the next refresh or the release settles them;
         * no pin changes hands either way, because no slot is written here.
         */
        for (uint8_t at = 0u; servo_next_run(mask, at, &first, &count);
             at = (uint8_t)(first + count)) {
            if (!servo_cfg_write(port, first, count, cfg, &reply)) {
                return false;
            }
            if (servo_countermanded()) {
                return false;
            }
            if (!servo_cmd_write(port, first, count, cmd, &reply)) {
                return false;
            }
            /* Landed, so this run is holding a position and a release owes
             * it a centre.  Recorded before the next run is attempted: one
             * that fails must not lose what an earlier one did. */
            s_servo_written |= servo_run_bits(first, count);
            if (servo_countermanded()) {
                return false;
            }
        }
        return true;
    }
}

/*
 * The SERVO page: the frame rate of the PWM target -- every PWM output
 * rendering a surface for the standard target, the one programming port
 * for a port target (protocol 4.19).  A coprocessor before protocol 4.1 has
 * no such page and runs each slot at its binding's rate, 50 Hz for a servo.
 * From 4.19 a TARGET change sets the page back to 0; prog_link_rate() says
 * so and servo_rate_reset_note() records it here.
 *
 * The rate is written with every held position, not once: a coprocessor that
 * restarts between two polls goes back to each slot's own rate without the
 * link ever going down, and only a write that lands says what the pins run
 * at.  A refusal is the binding's answer, not the wire's -- a surface sharing
 * a PWM slice with an output at another rate -- so the same rate is not
 * asked again until it changes, the binding is rewritten or the link comes
 * back.
 */
#define SERVO_BIND_HZ    50u      /* the binding's SERVO PWM rate         */
bool            s_servo_rate_page;
uint16_t        s_servo_hz_sent = SERVO_HZ_UNKNOWN;  /* acknowledged; 0 own */
static uint16_t s_servo_hz_refused;  /* the last rate it refused; 0 none     */
static atomic_uint s_servo_rate_shown;  /* (servo_rate_state_t << 16) | Hz */

static void servo_rate_show(servo_rate_state_t st, uint16_t hz)
{
    atomic_store(&s_servo_rate_shown, ((unsigned)st << 16) | hz);
}

/* What became of a rate written: the pins run at it, the binding will not
 * have it -- final until something changes -- or nothing answered. */
typedef enum { RATE_LANDED, RATE_REFUSED, RATE_NO_ANSWER } rate_result_t;

static rate_result_t write_servo_rate(uint16_t hz)
{
    if (hz == 0u) {
        return RATE_LANDED;   /* a command that names no rate */
    }
    if (!s_servo_rate_page) {
        /* Every PWM output at its binding's rate, which no profile is
         * faster than. */
        servo_rate_show(SERVO_RATE_UNSUPPORTED, hz);
        return RATE_LANDED;
    }
    if (hz == s_servo_hz_refused) {
        return RATE_REFUSED;
    }
    link_msg_t reply;
    if (!write_regs(&s_host, LINK_PAGE_SERVO, LINK_SV_FRAME_HZ, 1u, &hz,
                    &reply)) {
        return RATE_NO_ANSWER;
    }
    if (reply.op == LINK_OP_ACK) {
        s_servo_hz_sent    = hz;
        s_servo_hz_refused = 0u;
        servo_rate_show(servo_rate_in_force(s_far_minor), hz);
        return RATE_LANDED;
    }
    if (reply.op == LINK_OP_NACK) {
        s_servo_hz_refused = hz;
        servo_rate_show(SERVO_RATE_REFUSED, hz);
        return RATE_REFUSED;
    }
    return RATE_NO_ANSWER;
}

/* The rate the PWM target runs at as far as this end knows, or
 * SERVO_HZ_UNKNOWN. */
static uint16_t servo_rate_now(void)
{
    if (s_servo_hz_sent == SERVO_HZ_UNKNOWN) {
        return SERVO_HZ_UNKNOWN;
    }
    return (s_servo_hz_sent != 0u) ? s_servo_hz_sent : (uint16_t)SERVO_BIND_HZ;
}

/*
 * One position with its rate.  The pins must never carry a fast rate with
 * the wider pulses of a slower profile: a 760 us tail servo going back to
 * STANDARD PWM takes 50 Hz first and then 1000 to 2000 us, never 1500 us at
 * 560 Hz.  So a rate going up follows the pulse widths, and only once every
 * one of them landed; any other rate leads them, and a position waits for
 * it unless the pins are known to run no faster than it.  A rate not known
 * counts as fast.  A position held back is asked again with the next hold.
 *
 * Done only when the rate is settled too: a rate nobody answered leaves a
 * release owed, so it is written again.  A refused one is settled -- the
 * screen says REFUSED and the pins keep the slower rate -- because a release
 * owed for ever would stop the bench arming.
 */
static bool write_servo_and_rate(const servo_cmd_t sv)
{
    const uint16_t now_hz = servo_rate_now();
    if (now_hz != SERVO_HZ_UNKNOWN && sv.frame_hz > now_hz) {
        if (!write_servo(sv)) {
            return false;
        }
        return write_servo_rate(sv.frame_hz) != RATE_NO_ANSWER;
    }
    if (write_servo_rate(sv.frame_hz) != RATE_LANDED
        && (now_hz == SERVO_HZ_UNKNOWN || now_hz > sv.frame_hz)) {
        return false;
    }
    return write_servo(sv);
}

/*
 * Each slot back at its own rate.  Before a binding: the rate the SERVO page
 * holds was checked against the binding being replaced, and left in place a
 * motor bound beside a 560 Hz surface would be refused by the silicon and
 * stay still.  At link-up: a panel that restarted, or a link that only went
 * quiet, can leave the far end holding a heli rate the screen no longer
 * shows.  True when it landed, or there is no page to reset; otherwise the
 * rate in force is not known.
 *
 * servo_rate_reset_note() is what a reset leaves behind at this end, for the
 * reset a binding's own sequence sends (bind_link_write()) as well.
 */
void servo_rate_reset_note(bool page, bool landed)
{
    s_servo_hz_refused = 0u;
    if (!page) {
        s_servo_hz_sent = 0u;
        servo_rate_show(SERVO_RATE_UNSUPPORTED, 0u);
        return;
    }
    s_servo_hz_sent = landed ? 0u : (uint16_t)SERVO_HZ_UNKNOWN;
    servo_rate_show(SERVO_RATE_UNSENT, 0u);
}

bool servo_rate_reset(void)
{
    if (!s_servo_rate_page) {
        servo_rate_reset_note(false, true);
        return true;
    }
    const uint16_t own = 0u;
    link_msg_t reply;
    const bool landed = write_regs(&s_host, LINK_PAGE_SERVO, LINK_SV_FRAME_HZ,
                                   1u, &own, &reply)
                        && reply.op == LINK_OP_ACK;
    servo_rate_reset_note(true, landed);
    return landed;
}

/*
 * Whether the rate the surfaces run at is known, after one more try at
 * putting each slot back at its own rate if it is not.  Asked before every
 * arm: a panel that restarted while the far end held a heli rate, and whose
 * reset at link-up went unanswered, would otherwise arm the surfaces at it
 * from any screen, while the SERVO screen shows STANDARD PWM.
 */
static bool servo_rate_settled(void)
{
    if (s_servo_hz_sent == SERVO_HZ_UNKNOWN) {
        (void)servo_rate_reset();
    }
    return s_servo_hz_sent != SERVO_HZ_UNKNOWN;
}

/*
 * Disarming, wherever it was asked for.
 *
 * Two screens can arm the bench and both disarm it the same way: the policy
 * is told, this end's own bank stops driving, the throttle goes to zero so
 * the next arm cannot carry the last one's command, and the far end is told
 * while there is a link to tell it on.
 */
static bool disarm_here(bool link_up)
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
 * One servo command.
 *
 * Arming and disarming go to the same policy the motor screen's do -- the
 * bench has one armed state and one set of rules for reaching it, whichever
 * screen is up.  A disarm is acted on with or without a link, because the
 * part of it that matters most is at this end.
 */
/*
 * A servo command, onto the link: a position goes with its frame rate, on
 * the SERVO page (see write_servo_and_rate()).
 */
static void apply_servo_cmd(const servo_cmd_t sv, bool link_up, uint32_t stops)
{
    /* The drain has reached a command at least as new as any HOLD. */
    atomic_store(&s_servo_hold_request, false);
    /* Every command from the screen carries its range, a release included,
     * and the release the arm owes is paid with it. */
    if (servo_named(&sv)) {
        s_servo_range = sv;
    }
    if (sv.kind == SERVO_CMD_ARM) {
        /*
         * The surfaces are centred first, whether or not this process cached
         * a position.  After a panel restart the far end can still hold the
         * command from before it, and arming would render that: the same
         * reason DISARM and RELEASE ask unconditionally.  The arm itself
         * waits for it -- see ARMING_ACT_ARM.
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
        /* The surfaces and the programming ports, and no motor. */
        arming_request_arm_roles(&s_arm, now_ms(),
                                 OUT_ROLE_BIT(OUT_ROLE_SURFACE));
        return;
    }
    if (sv.kind == SERVO_CMD_DISARM) {
        /*
         * Asked for by this screen, so the surfaces are centred whether or not
         * this process cached a position for them -- the far end keeps its
         * channel commands across a panel restart, and the screen promises to
         * let go of the output.
         */
        s_servo_held.kind = SERVO_CMD_NONE;
        s_servo_release_owed = true;
        (void)disarm_here(link_up);
        servo_service(link_up);
        return;
    }
    if (sv.kind == SERVO_CMD_RELEASE) {
        /*
         * Asked for, so the surfaces are centred whether or not this process
         * remembers commanding them: after a panel restart the far end holds
         * a position this end never wrote, and the button says it releases
         * the output.
         */
        s_servo_held.kind = SERVO_CMD_NONE;
        s_servo_release_owed = true;
        servo_service(link_up);
        return;
    }
    /*
     * A sweep or a hold reaching here with the link down was ended with the
     * link on the screen: kept, it would be said when the link comes back
     * and start motion nobody asked for.
     */
    if (!link_up && !servo_cmd_survives_link_loss(&sv)) {
        /*
         * It superseded the drive held before it on the screen, which ends
         * it with the link and draws the surfaces at rest: that one is not
         * said again either, and a position written out there is owed its
         * release, as servo_let_go() owes it.
         */
        if (s_servo_held.kind != SERVO_CMD_NONE || s_servo_written != 0u) {
            s_servo_release_owed = true;
        }
        s_servo_held.kind = SERVO_CMD_NONE;
        return;
    }
    /*
     * A command asked during a pause the panel has let go of -- queued
     * behind the HOLD whose late acknowledgement ended it: a resume, or a
     * position said again under a changed profile -- is stale.  The screen
     * shows the surfaces released to rest; sent, the resume would start
     * motion it does not show and the position would void the release and
     * hold an angle drawn from the abandoned pause.
     */
    if (servo_cmd_stale(&s_pause_ended, &sv)) {
        return;
    }
    s_servo_held = sv;
    s_servo_next_ms = now_ms() + SERVO_HOLD_MS;
    /*
     * And a sweep goes only once an owed release is paid: the surfaces at
     * their centre, a start both ends know, not wherever a stop froze them.
     * One not yet paid keeps the sweep held, and the refresh pays it first.
     */
    if (sv.kind == SERVO_CMD_SWEEP && s_servo_release_owed) {
        servo_service(link_up);
        if (s_servo_release_owed) {
            return;
        }
    }
    if (link_up && write_servo_and_rate(sv)) {
        /*
         * The surfaces are holding this position now, so an older release
         * still owed for them is void.  Paying it afterwards would centre
         * what was just asked for and leave the output at rest until the next
         * refresh.  A write that failed leaves the debt where it was.
         */
        s_servo_release_owed = false;
    }
}

/*
 * Stop holding the servo, the way throttle_to_zero() stops holding the
 * throttle, and for the same reason: a disarm that leaves a position behind
 * is an arm that steps straight back to it.
 *
 * The far end keeps the channel command through a disarm and a failsafe, and
 * outputs_arm() stamps every channel's clock, so on the next arm the surface
 * is neither overdue nor at rest -- it is at its old position, with nobody
 * having touched anything.  So the position has to be returned to centre, and
 * until it can be the release is owed.
 *
 * Owed for what was written rather than for what the screen is holding: a
 * position that landed and then stopped being held is still out there, and it
 * is the far end's copy that arms the bench into it.
 */
void servo_let_go(void)
{
    if (s_servo_held.kind != SERVO_CMD_NONE || s_servo_written != 0u) {
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
    /*
     * A rate not known is settled before anything that depends on it: while
     * a release is owed, whose rate a binding may refuse until the slots are
     * back at their own, and while the bank is armed, which poll_bench()
     * does not pass on to the far end until the rate is known.  Each slot
     * back at its own rate; a screen holding a position says its own again
     * with the next hold.
     */
    if (s_servo_hz_sent == SERVO_HZ_UNKNOWN
        && (s_servo_release_owed || outputs_armed(&s_out))) {
        (void)servo_rate_reset();
    }
    if (s_servo_release_owed) {
        /* At the screen's rate, in the same order as a position: a release
         * restates the range, and a profile chosen since the last position
         * may be slower than the rate the pins still run at. */
        servo_cmd_t release = { .kind = SERVO_CMD_RELEASE };
        release.frame_hz = s_servo_range.frame_hz;
        /* Only a write the far end acknowledged pays it off.  link_up is a
         * snapshot and the link can go during the transaction; forgetting an
         * unacknowledged clear would leave the slot bound with nothing left
         * to remember it. */
        s_servo_release_owed = !write_servo_and_rate(release);
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
    (void)write_servo_and_rate(s_servo_held);
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
        supply_service();

        /* The supply is the panel's own and touches no link; its ON carries
         * its own checks.  See apply_supply_cmd(). */
        if (pc.kind == PANEL_CMD_SUPPLY) {
            apply_supply_cmd(&pc);
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
        if (arms && s_kst_taken) {
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
            s_servo_known = false;
            servo_let_go();
            read_outputs_binding();
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
 * Which run the log should be recording now; see log_run_kind_t.  The bench
 * comes first: an armed bench is the run whatever the supply is doing.
 */
static log_run_kind_t log_run_wanted(void)
{
    if (outputs_armed(&s_out)) {
        return LOG_RUN_BENCH;
    }
    return s_supply.output ? LOG_RUN_SUPPLY : LOG_RUN_NONE;
}

/*
 * A run's start and end: the log opens when the bank arms or the supply's
 * output comes on, and closes when that ends.  s_log_kind is the record of
 * which run started last, and s_log_run_now is the level the logger follows;
 * nothing here opens or writes a file.  A change from one kind to the other
 * is an end and a start in the same call, so the two land in two files.
 */
static void log_follow_runs(void)
{
    const log_run_kind_t wanted = log_run_wanted();
    if (wanted == s_log_kind) {
        return;
    }
    if (s_log_kind == LOG_RUN_SUPPLY && s_supply.output) {
        /* The bench takes the log from a supply that stays on: the supply
         * run's file ends on its totals up to now. */
        supply_log_tail();
    }
    if (s_log_kind != LOG_RUN_NONE) {
        atomic_store(&s_log_run_now, 0u);
        /*
         * The run's rows, answered here because the run has just ended and
         * this task has both numbers.  Every row of a run is posted between
         * the two edges, so the counts are final at this point whatever the
         * card is still doing with the queue.
         *
         * A run that got nothing through is the one the logger cannot report
         * at all: no row of it ever reached the logger, so no file was opened
         * and no close will happen for it.  That is the case a full queue
         * left over from the run before produces, and it is the case where an
         * operator would otherwise look for a CSV that is not there.
         */
        if (log_cadence_lost(&s_log_cad) > 0u
            && log_cadence_sent(&s_log_cad) == 0u) {
            control_alert(TR(ALERT_CARD_SLOW));
        } else if (log_cadence_lost(&s_log_cad) > 0u) {
            control_alert(TR(ALERT_CARD_GAPS));
        }
    }
    s_log_kind = wanted;
    if (wanted == LOG_RUN_NONE) {
        return;
    }
    if (wanted == LOG_RUN_SUPPLY) {
        /* The run's first row counts from here, not from the last supply
         * step, which a bench run that has just ended may have taken. */
        s_supply_step_ms = now_ms();
    }
    /* The run's clock, its row counts and its number, all set before the
     * level goes up so the logger cannot see a run half started.  Zero means
     * no run, so the count skips it on the one wrap in 2^32 runs. */
    s_log_t = 0.0f;
    log_cadence_run_start(&s_log_cad, now_ms());
    if (++s_log_run_ctr == 0u) {
        s_log_run_ctr = 1u;
    }
    atomic_store(&s_log_run_now, s_log_run_ctr);
}

/*
 * The ON in flight, against the touch stream: the arm watch's rules on the
 * supply's output.  Until the render side has acknowledged this ON from a
 * frame that began with the output on and found the stream whole, a loss of
 * either kind switches the output off again.
 */
static void supply_watch_service(void)
{
    if (!s_supply.output) {
        arm_watch_end(&s_supply_watch);
        return;
    }
    if (arm_watch_lost(&s_supply_watch, atomic_load(&s_loss_gen),
                       atomic_load(&s_drv_gaps), atomic_load(&s_supply_ack))) {
        supply_switch(false);
        control_alert(TR(ALERT_TOUCH_SWITCH_ON));
    }
}

/*
 * The supply, from control_pump(): at the top of every pass and inside every
 * exchange's wait, which can last LINK_HOST_TIMEOUT_MS (1000 ms) a time.  A
 * stop, an OFF and a touch loss cut the output here, and the supply is
 * stepped, logged and published every 1/PANEL_SAMPLE_HZ whatever the link is
 * doing -- the supply is the panel's own and an unanswered coprocessor is
 * no reason for its plot or its log to thin out.
 */
static void supply_pump(void)
{
    /* The cuts first -- a stop, then a lost ON -- and only then the set
     * points, so a level stored after a touch loss never reaches an output
     * that loss is about to switch off. */
    supply_follow_stops();
    supply_watch_service();
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
    if (s_log_kind != LOG_RUN_BENCH) {
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

/*
 * What the SUPPLY page said that the operator is told, and an ON it refused
 * or let go switched off here too.
 */
static void supply_link_alerts(void)
{
    const uint16_t ev = supply_link_events(&s_supply_link);
    if ((ev & (SUPPLY_LINK_EV_ON_REFUSED | SUPPLY_LINK_EV_ON_LOST
               | SUPPLY_LINK_EV_TRIPPED)) != 0u) {
        supply_switch(false);
        control_alert(((ev & SUPPLY_LINK_EV_TRIPPED) != 0u)
                          ? TR(ALERT_PDMINI_OFF)
                      : ((ev & SUPPLY_LINK_EV_ON_LOST) != 0u)
                          ? TR(ALERT_SUPPLY_OFF_REMOTE)
                          : TR(ALERT_SUPPLY_REFUSED_ON));
    }
    if ((ev & SUPPLY_LINK_EV_SAGGED) != 0u) {
        /* Switched off at the coprocessor for an input under the set point
         * and the headroom: the input and the set point it was read with. */
        supply_switch(false);
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
        control_alert(TR(ALERT_PDMINI_PINS));
    }
    if ((ev & SUPPLY_LINK_EV_WIRING_LIVE) != 0u) {
        control_alert(TR(ALERT_PDMINI_WIRING_LIVE));
    }
    if ((ev & SUPPLY_LINK_EV_STUCK) != 0u) {
        control_alert(TR(ALERT_PDMINI_SWITCH));
    }
    if ((ev & SUPPLY_LINK_EV_SET_STUCK) != 0u) {
        control_alert(TR(ALERT_PDMINI_SET));
    }
    /* A rate AUTO found is not a fault: it is in the SUPPLY screen's
     * header, not in the alert band, which stays until another alert
     * replaces it. */
}

/*
 * The SUPPLY page: what is owed written -- an OFF first -- and the page read
 * every SUPPLY_LINK_READ_MS.  From poll_bench(), outside the pump: the pump
 * runs inside an exchange and switches the supply by asking
 * (supply_switch()), which this pays.  A coprocessor without the page is
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
            w.baud = 1u;   /* a 4.3 coprocessor has no AUTO: the as-shipped 19200 */
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
        /* A 4.3 coprocessor's page ends before BAUD_FOUND. */
        const bool read = poll_page(&s_host, LINK_PAGE_SUPPLY,
                                    s_supply_auto ? (uint8_t)LINK_SP_COUNT
                                                  : (uint8_t)LINK_SP_BAUD_FOUND,
                                    &reply)
                          && reply.op == LINK_OP_DATA;
        if (read && !s_supply_auto) {
            reply.regs[LINK_SP_BAUD_FOUND] = SUPPLY_LINK_BAUD_AUTO;
        }
        supply_link_read(&s_supply_link, read ? reply.regs : NULL, now_ms());
        {
            /*
             * A programming port commanded the supply (4.13): this end's
             * copy follows the page, so the next command written is the
             * port's and not the one from before it, and the SUPPLY screen
             * shows the set points in force.
             */
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
        if (read) {
            atomic_store(&s_supply_vin_mv, reply.regs[LINK_SP_VIN_MV]);
            /* 0 for a rate still being looked for: no rate in the header.
             * A 4.3 coprocessor has no BAUD_FOUND and runs at the BAUD it
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

/* The SENSE, TONE, KST and PROG services: panel_services.c.  The poll
 * of the far end, poll_far_end(): panel_poll.c. */


/* The coprocessor's update, cu_service(): panel_update.c. */

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
    /* Before anything that can start a run: a run's start sets its clock. */
    log_cadence_init(&s_log_cad, now_ms());
    control_setup(&sim, &bench);

    uint32_t last_poll     = 0;
    uint32_t last_status   = 0;
    uint32_t last_report   = 0;
    uint32_t last_temp     = 0;
    bool     link_up       = false;

    for (;;) {
        /*
         * An install about to write the flash: this loop stands still.  No
         * poll and no exchange starts, and the heartbeat is withheld on
         * purpose, so the coprocessor goes to failsafe as it does at any
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
        kst_port_look();
        service_arming(link_up);
        supply_service();

        drain_commands(link_up, &bench);
        kst_commands();
        (void)outputs_keepalive(&s_out, PANEL_CH_THROTTLE, now_ms());
        servo_service(link_up);

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

        /* --- the far end, at 1 Hz until it answers ----------------------- */
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

        /* Every 5 s while the link is down, every 60 s while it is up. */
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

/*
 * Queue a command, and do not lose it.  Every command is already taken from
 * its screen by the time it gets here, so a refused send is a discarded
 * disarm or a throttle that never arrives.
 */
/*
 * The programmer's model, once a frame: what the control task last
 * published, the commands it has decided on, and its file.
 *
 * A command the queue does not take is not carried out, and the model says
 * so when its answer does not come (KST_PROG_MSG_TIMEOUT).
 */
static void kst_ui_service(void)
{
    static kst_prog_feed_t feed;
    static bool            file_out;

    snap_lock();
    feed = s_snap.kst;
    snap_unlock();
    kst_panel_feed(&feed);

    /* A card put in after the boot is mounted here, on the task RESCAN
     * mounts one on: once for an opening of the programmer or a tap on one
     * of its actions while none is mounted. */
    if (!file_out && !storage_mounted() && kst_panel_card_look()) {
        (void)storage_init();
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
static void flush_screen_commands(uint32_t stops_now);

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
        atomic_store(&s_servo_release_request, true);
    } else if (pc->kind == PANEL_CMD_SERVO
               && pc->servo.kind == SERVO_CMD_RELEASE) {
        /*
         * RELEASE lets go of the pin without disarming the bench, and it is
         * as much a safety command as the disarm: waiting behind a backlog
         * of positions, three exchanges each, would hold the servo for
         * seconds after the operator asked it to stop.
         */
        atomic_fetch_add(&s_lets_go, 1u);
        atomic_store(&s_servo_release_request, true);
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
        atomic_store(&s_servo_hold_request, true);
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
static unsigned       s_tq_lost;

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
static void publish_supply_levels(void)
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
static void test_lines_service(void)
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
typedef struct {
    int            steps;
    ui_screen_id_t route;
    uint32_t       navigations;
} knob_turn_t;

static knob_turn_t knob_take(void)
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

static void knob_apply(const knob_turn_t *turn, bool frame_lost)
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
static void flush_screen_commands(uint32_t stops_now)
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
    /* A stick run on PROGRAMMER moves the throttle and arms as the MOTOR
     * screen does, through the same commands and the same policy. */
    while (programmer_screen_poll_cmd(&mc)) {
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
static bool drain_touch(uint32_t stops_now)
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
static void outputs_apply(const outbind_t *b)
{
    if (b == NULL) {
        return;
    }
    panel_cmd_t pc = { .kind = PANEL_CMD_OUTPUTS, .bind = *b };
    send_cmd(&pc);
}

/* Wi-Fi and the update, and GitHub's check and download: panel_update.c. */

/* ------------------------------------------------------------------ main */

/* The coprocessor update's screen, cu_render_service(): panel_update.c. */

void app_main(void)
{
    ESP_ERROR_CHECK(board_init());
    heartbeat_init();
    tsens_init();

    ui_theme_set(UI_THEME_DARK);
    ui_router_init();
    servo_screen_on_testing(servo_testing_told);
    /*
     * The outputs screen hands its choice back through here.  It runs on
     * app_main and the link belongs to the control task, so this queues the
     * choice rather than writing it; the write happens where the link lives.
     */
    outputs_screen_set_apply(outputs_apply);
    /*
     * The picker is the same choice seen as the board, so it applies through
     * the same seam, selects its protocol on the outputs screen, and takes
     * the board's drawing the control task published.
     */
    picker_screen_set_apply(outputs_apply);
    picker_screen_set_select(outputs_screen_select);
    picker_screen_set_drawing_source(drawing_for_picker);
    /* The supply screen shows the panel's model, and the range a PPS
     * (Programmable Power Supply) source offers; see supply.h. */
    const supply_caps_t supply_caps = SUPPLY_CAPS_PPS_DEFAULT;
    supply_screen_set_caps(&supply_caps);
    supply_screen_set_model(true);

    const bool healthy = bring_up();
    /* The settings are loaded now: the supply screen takes its limits and
     * starts its set points from them, and both reach the control task's
     * levels before that task exists. */
    supply_screen_settings_loaded();
    publish_supply_levels();
    /* The servo programmer's record of a write that a restart cut
     * short. */
    kst_record_load(false);

    s_touch_q   = xQueueCreate(TOUCH_Q_LEN, sizeof(touch_item_t));
    s_cmd_q     = xQueueCreate(CMD_Q_LEN, sizeof(panel_cmd_t));
    s_kst_q     = xQueueCreate(KST_Q_LEN, sizeof(kst_prog_cmd_t));
    kst_prog_gate_init(&s_kst_gate);
    kst_panel_gate(&s_kst_gate);
    s_sample_q  = xQueueCreate(SAMPLE_Q_LEN, sizeof(bench_state_t));
    s_supply_q  = xQueueCreate(SAMPLE_Q_LEN, sizeof(supply_item_t));
    s_enc_q     = xQueueCreate(SAMPLE_Q_LEN, sizeof(servo_test_enc_t));
    s_win_q     = xQueueCreate(SAMPLE_Q_LEN, sizeof(servo_win_item_t));
    s_log_q     = xQueueCreate(LOG_Q_LEN, sizeof(log_row_t));
    s_note_q    = xQueueCreate(LOG_NOTE_Q_LEN, LOG_NOTE_MAX);
    s_test_q    = xQueueCreate(TEST_Q_LEN, sizeof(test_line_t));
    s_snap_lock = xSemaphoreCreateMutex();
    s_draw_lock = xSemaphoreCreateMutex();
    s_drawpub   = heap_caps_malloc(LINK_DRAW_MAX_BYTES, MALLOC_CAP_SPIRAM);
    s_drawshow  = heap_caps_malloc(LINK_DRAW_MAX_BYTES, MALLOC_CAP_SPIRAM);
    /* Zero is a temperature; the snapshot starts unread, so the strip shows
     * "--" until the control task has published one. */
    s_snap.mcu_temp_c = NAN;
    ESP_ERROR_CHECK((s_touch_q != NULL && s_cmd_q != NULL && s_kst_q != NULL
                     && s_sample_q != NULL && s_supply_q != NULL
                     && s_enc_q != NULL && s_win_q != NULL
                     && s_log_q != NULL && s_test_q != NULL
                     && s_note_q != NULL && s_snap_lock != NULL)
                    ? ESP_OK : ESP_ERR_NO_MEM);

    if (!healthy) {
        ESP_LOGW(TAG, "bring-up incomplete: the splash names each step");
    }
    /* Held for the touch alone: bring_up() fails for other reasons too,
     * a protocol mismatch among them, which has its own passing alert. */
    if (!s_touch_ok) {
        ui_router_hold_alert(TR(ALERT_TOUCH_NO_ANSWER));
    }

    /* The knob's sensor joins the bus before the control task starts using it. */
    knob_task_attach();

    /*
     * On the core the renderer does not use, and above it in priority: the
     * bench's timing must not depend on how long a frame takes.
     */
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(control_task, "control", 6144,
                                            NULL, 10, NULL, 1) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);

    /*
     * And the card, on the same core and below it: a card write must not be
     * able to delay the safety line, so it runs in the gaps the control task
     * leaves rather than beside it on the core the renderer uses.  Its
     * stack holds one line of a servo test, SERVO_TEST_LINE_MAX bytes.
     */
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(log_task, "runlog", 4352,
                                            NULL, 3, NULL, 1) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);

    knob_task_start();

    /* What internal RAM holds with every task of the panel running and the
     * radio off.  A Wi-Fi start takes about 100 KB of it, 51 KB of that as
     * the driver's DMA (direct memory access) buffers
     * (tools/ram_budget.py). */
    ESP_LOGI(TAG, "internal RAM: %u B free, largest block %u B; "
             "PSRAM: %u B free",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    uint32_t frames  = 0;
    uint32_t last_us = (uint32_t)esp_timer_get_time();
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
        /* And a stick run on PROGRAMMER, which ends on a stop, a disarm or
         * a link that went, and steps on the frame's time.  The pressed
         * count tells an operator's STOP from the bench's own. */
        /*
         * Each screen is told whether the bench is armed for its own role:
         * an arm covers the role of the screen that asked for it (MOTOR and
         * the PROGRAMMER stick run the throttle, SERVO the surfaces), and a
         * bench armed for the other role is a disarmed one to it.
         */
        const bool motor_armed_now =
            (armed_roles_now & OUT_ROLE_BIT(OUT_ROLE_THROTTLE)) != 0u;
        const bool servo_armed_now =
            (armed_roles_now & OUT_ROLE_BIT(OUT_ROLE_SURFACE)) != 0u;
        programmer_screen_bench(now_ms(), motor_armed_now, stops_now,
                                pressed_now, link_now);
        /* The phase tap's readout for the run's page, copied under the
         * lock from the snapshot. */
        snap_lock();
        programmer_screen_tone(&s_snap.tone);
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
         * That is a run shorter than one frame, about 26 ms at 39 Hz.
         */
        if (motor_armed_now && !motor_armed) {
            /*
             * A run starts at the arm, so what the queue is holding from
             * before it is not part of it.  Dropped rather than pushed: the
             * queue can hold SAMPLE_Q_LEN samples, 400 ms at
             * PANEL_SAMPLE_HZ, and without this the first 400 ms of a run's
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
                r = atomic_load(&s_servo_rate_shown);
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
            servo_screen_set_sweep(atomic_load(&s_servo_sweep_able));
            servo_screen_set_surfaces(atomic_load(&s_servo_surfaces));
            /* What the tester drives and whether the port renders its
             * pulse, to the control task; the port's pin, to both views of
             * the binding, from one read. */
            atomic_store(&s_prog_want_target,
                         (unsigned)settings_get_int(SET_SERVO_TARGET));
            atomic_store(&s_prog_want_pwm,
                         settings_get_bool(SET_SERVO_PORT_PWM));
            const uint8_t prog_pin = (uint8_t)atomic_load(&s_prog_pin);
            outputs_screen_set_prog_pin(prog_pin);
            picker_screen_set_prog_pin(prog_pin);
            motor_screen_set_throttle_pins(atomic_load(&s_throttle_pins),
                                           atomic_load(&s_throttle_pins_known));
            servo_screen_set_pins(atomic_load(&s_servo_pins),
                                  atomic_load(&s_servo_pins_known));
            servo_screen_set_enc_held(atomic_load(&s_enc_held));
            settings_screen_set_unheld(
                (uint16_t)atomic_load(&s_sense_unheld),
                (uint8_t)atomic_load(&s_tone_unheld));
            if (atomic_exchange(&s_sweep_refused, false)) {
                servo_screen_sweep_refused();
            }
            if (atomic_exchange(&s_servo_hold_lost, false)) {
                servo_screen_released(
                    (uint16_t)atomic_load(&s_servo_hold_lost_seq));
            }
            /* The pause's phase before a resume that rebases on it. */
            if (atomic_exchange(&s_sweep_held_new, false)) {
                servo_screen_sweep_held(
                    (uint16_t)atomic_load(&s_sweep_held_seq),
                    atomic_load(&s_sweep_held_kept));
            }
            if (atomic_exchange(&s_sweep_start_new, false)) {
                const uint32_t now = now_ms();
                servo_screen_sweep_started(
                    (uint16_t)atomic_load(&s_sweep_start_seq),
                    now - atomic_load(&s_sweep_start_ms),
                    (servo_sweep_from_t)atomic_load(&s_sweep_start_from),
                    now - atomic_load(&s_sweep_frozen_ms));
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
         * moved only on a change of 100 mV or more. */
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
            programmer_screen_supply(&sup);
            supply_seen = sup.output;
        }
        /* The simulated ESC follows the run, while the model is the
         * supply; see escsim_load(). */
        atomic_store(&s_escsim_runs, (unsigned)programmer_screen_stick_runs());
        atomic_store(&s_escsim_profile,
                     (uintptr_t)programmer_screen_stick_profile());

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
        /* What the coprocessor says its outputs are, and what became of the
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
        programmer_screen_set_throttle(got_read, got_throttle);
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
        have_alert  = s_snap.alert_pending;
        if (have_alert) {
            snprintf(alert, sizeof(alert), "%s", s_snap.alert);
            s_snap.alert_pending = false;
            s_snap.alert_taken   = s_snap.alert_gen;
        }
        snap_unlock();

        if (have_alert) {
            ui_router_set_alert(alert);
        }
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
            /* A bench build names an attached touch injector where the
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
         * The coprocessor's update.  The question opens on the menu, on a
         * bench that is SAFE with the supply off, the panel's radio wholly
         * off and no install of the panel under way, once per offer; START
         * and CANCEL go to the control task.  While the update runs the screen
         * shows its progress, and the menu comes back when it has ended --
         * the alert says how.
         */
        /* The release reminder first: it yields to the coprocessor's
         * question, which comes first. */
        upd_remind_service(armed || arm_wanted_now,
                           armed || arm_wanted_now || supply_now
                               || supply_live_or_coming(),
                           &cu);
        cu_render_service(armed || arm_wanted_now || supply_now
                              || supply_live_or_coming() || !cu_may_start(),
                          &cu);

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
         * OFF stay on screen for as long as it is live.  Nor over FILES &
         * UPDATE, whose Wi-Fi does not need the link, nor over PANEL
         * UPDATE, which withholds the heartbeat on purpose while it writes.
         *
         * The timestamp is read once and tested twice.  The control task
         * clears it on the other core the moment the link answers, and a
         * second read that caught the zero would test now_ms() - 0, the
         * uptime, against LINK_LOST_SCREEN_MS: past 4000 ms of uptime that
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
         * HEARTBEAT_MAX_GAP_MS (150 ms) -- and not while the supply's output
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

        gfx_canvas_t *c = display_canvas();
        const int64_t draw_start = esp_timer_get_time();
        ui_router_render(c, display_back_index());
        const uint32_t draw_us = (uint32_t)(esp_timer_get_time() - draw_start);
        display_flip();   /* blocks until the swap has taken effect */

        /* A restart of the scan, as it happens: the line before it in the
         * log says what held the refill interrupt off (display.h). */
        {
            static uint32_t seen_restarts;
            const uint32_t restarts = display_restarts();
            if (restarts != seen_restarts) {
                seen_restarts = restarts;
                ESP_LOGW(TAG, "display: scan restarted, %u since start",
                         (unsigned)restarts);
            }
        }

        /*
         * DRAW is the paint time of the frame; WAIT is how long the flip
         * blocked afterwards.  A healthy frame is mostly WAIT, the loop
         * paced by the panel.  DRAW climbing until WAIT reaches zero is the
         * frame budget being spent.  Printed every 300 frames.
         */
        if (++frames % 300u == 0u) {
            ESP_LOGI(TAG,
                     "%.1f fps  DRAW %u us  WAIT %u us  TOUCHLOST %u/%u",
                     (double)display_fps(), (unsigned)draw_us,
                     (unsigned)display_last_wait_us(),
                     s_tq_lost, atomic_load(&s_drv_gaps));
        }
    }
}
