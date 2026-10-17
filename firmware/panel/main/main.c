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
atomic_uint s_throttle_ramp;

/*
 * Published by app_main, which owns the settings model, and read by the
 * control task on the other core.  The values themselves are plain floats
 * written by the settings screen, so the control task must not read them: an
 * atomic carries the converted number across instead, and gives the two
 * cores the ordering a bare float does not.
 */
void publish_throttle_ramp(void)
{
    const int pct = settings_get_int(SET_OUT_RAMP);
    const uint32_t per_s = ((uint32_t)OUT_SPAN * (uint32_t)pct) / 100u;
    atomic_store(&s_throttle_ramp, (unsigned)per_s);
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
#define CONTROL_PERIOD_MS 5u
#define TOUCH_Q_LEN       32
#define CMD_Q_LEN         16
#define SAMPLE_Q_LEN      8

/*
 * The panel's own update.  An install about to write the flash asks the
 * control task to stand still (s_fw_quiet_req) and waits for it to say so
 * (s_fw_quiet_ack): no polls, no exchanges, and the heartbeat withheld on
 * purpose, so the coprocessor goes to failsafe as it does at any restart of
 * the panel.  A write that fails lets it go again.
 */
static atomic_bool s_fw_quiet_req;
static atomic_bool s_fw_quiet_ack;
/*
 * An install and the coprocessor's update exclude each other.  An install
 * says so from INSTALL's hold until its write has failed (s_fw_installing);
 * the control task says so for an update, and for the BUY of an image on
 * trial (s_cu_busy).  Each side stores its own flag before it reads the
 * other's, so of two starting at once at least one sees the other.
 */
static atomic_bool s_fw_installing;
static atomic_bool s_cu_busy;
/* What the start check found, for the band once the splash has gone. */
static char        s_boot_alert[ALERT_MAX];
/* False when wifi_access_init() failed: no call into wifi_access then. */
static bool        s_wifi_ok;
/* Where PANEL UPDATE's CLOSE leads: back to FILES & UPDATE's LOGS after an
 * upload's install, to UPDATES or OVERVIEW after a download. */
static ui_screen_id_t s_update_back = SCREEN_LOGS;

/* A screen's command (panel_cmd_t): panel.h. */

/*
 * What became of the last output-page write.
 *
 * The control task writes it and app_main hands it to the screen, because
 * screen state is app_main's alone.  Atomic rather than volatile, for the
 * reason this file already gives for s_stop_live: volatile orders nothing
 * between processors and promises no atomicity, and these two tasks are
 * pinned to different cores.
 */
atomic_int s_outputs_result;

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

/* Fetching the board's drawing, and keeping it: panel_art.c. */

QueueHandle_t            s_touch_q;   /**< control task -> app_main */
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
 * was queued (servo_screen_supply_at()): supply_item_t, panel.h. */
QueueHandle_t     s_supply_q;  /**< control task -> app_main */
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
atomic_bool        s_stop_request;
/* A disarm, and the servo screen's own release, as flags rather than queue
 * entries: neither may be lost to an eviction.  See send_cmd(). */
/* Set when the pump applied a stop for a press the router will also latch,
 * so the backstop does not stop the bench twice for one press. */
atomic_bool        s_stop_counted;
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
atomic_uint s_lets_go;
/*
 * And how many times the supply's output has been asked off, for the same
 * reason: an OFF is a flag as well as a queue entry, so an ON still behind it
 * in the queue would otherwise switch the output back on.  See
 * supply_service().
 */
atomic_bool s_supply_off_request;
atomic_uint s_supply_offs;
/*
 * The supply's set points as the screen holds them, in mV and mA: a level the
 * render loop stores every frame and the control task reads, not a queue
 * entry.  A set point lost to an eviction would leave the screen showing one
 * voltage and the supply holding another.  Stored before any command of the
 * same frame is queued, so an ON taken from the queue is never applied ahead
 * of the set points it was asked under.
 */
atomic_uint s_supply_set_mv;
atomic_uint s_supply_set_ma;
/*
 * And the operator's limits, the same way: caps on the set points in mV and
 * mA, the trips in mA and mV (0 is off) and the trip time in ms.  The screen
 * already keeps its set points under the caps; the control task applies
 * them again, because it is what the supply obeys.
 */
atomic_uint s_supply_vmax_mv;
atomic_uint s_supply_imax_ma;
atomic_uint s_supply_trip_ma;
atomic_uint s_supply_trip_mv;
atomic_uint s_supply_trip_ms;
/*
 * The newest ON the render side has seen applied, from a frame that found
 * the touch stream whole: the supply's counterpart of s_arm_ack.  See
 * supply_watch_service().
 */
atomic_uint s_supply_ack;
/*
 * Whether the output is on, stored by the control task the moment it
 * switches, and the ONs the render side has queued against the ones the
 * control task has taken (applied or dropped).  Together they say, at any
 * moment of a frame, whether the output is live or about to be: the
 * snapshot is a frame old, and an ON queued in this frame is in neither.
 * See supply_live_or_coming().
 */
atomic_bool s_supply_live;
atomic_uint s_supply_ons_sent;
atomic_uint s_supply_ons_taken;
/*
 * Where the PD mini is wired, from SETUP INTERFACES, in one word so a write
 * never pairs one edit's TX with another's RX: bit 24 enabled, bits 23..16
 * the TX GPIO and 15..8 the RX, each plus one with 0 for unset, bits 7..0
 * the baud setting.  Published as the pole count is; see publish_pdmini().
 */
atomic_uint s_pdmini_wiring;
/* And how many times it has been edited: an edit undone before a queued ON
 * is applied leaves the word as it was, but not this count. */
atomic_uint s_pdmini_edits;
/*
 * Whether the SUPPLY screen drives the PD mini rather than the panel's
 * model: SETUP INTERFACES enables it.  Stored by the control task, read by
 * app_main for the screen's header, caps and the menu's badge.
 */
atomic_bool s_supply_real;
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
atomic_uint           s_drv_gaps;

/*
 * s_touch_q, from the control task to the render loop.  The control task is
 * its one producer and numbers what it offers; s_tq_published is the last
 * number offered, stored after the offer.  A loss the control task found in
 * the driver's stream travels down this queue as an item of its own, so the
 * render loop meets it in order, ahead of the events that followed the gap;
 * s_lost_notice_seq is the number of the latest such item, for the check on
 * an arm (touch_loss.h, arm_watch_take_ok()).  Both control-task values.
 * The item is touch_item_t, in panel.h.
 */
static uint32_t    s_tq_seq;
atomic_uint        s_tq_published;
uint32_t           s_lost_notice_seq;

/*
 * The render loop's side.  s_loss_gen counts the times it dropped the
 * screens' gestures for a loss -- written there only, read by the control
 * task for the arm check and the watch.  s_arm_ack is the arm the render
 * loop has seen, set at the end of a frame that began with the bench armed
 * and found no loss; see arm_watch_lost().
 */
atomic_uint s_loss_gen;
atomic_uint s_arm_ack;

/*
 * The arm in flight, control task only.  An arm's two stamps are kept from
 * the moment it is taken, because the exchanges that apply it can take two
 * seconds, and the watch starts once it is applied.
 */
uint32_t           s_arm_take_gen;
uint32_t           s_arm_take_drv;
uint32_t           s_arm_gen;
arm_watch_t        s_arm_watch;

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
float               s_log_t;

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

/* The board drawing: panel_art.c. */

/* The control page: panel_link.c. */


/* ------------------------------------------------------------------- main */

/* --------------------------------------------------------- the control task */

/* The supply: panel_supply.c. */
/* The link modules' state, SENSE to PROG: panel_services.c. */

/* The bench's own state, the arm watch and the bench's totals:
 * panel_arm.c. */

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

/* The outputs screen's choice, onto the coprocessor's two output pages:
 * write_output_binding(), panel_arm.c. */
void servo_rate_reset_note(bool page, bool landed);

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

/* Disarming, and one motor command: panel_arm.c. */

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
void log_follow_runs(void)
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

/* The supply's service, from control_pump(), and its page:
 * panel_supply.c. */

/* The SENSE, TONE, KST and PROG services: panel_services.c.  The poll
 * of the far end, poll_far_end(): panel_poll.c. */


/* -------------------------------------------- the coprocessor's update */

/*
 * The coprocessor image this panel carries: the release's own, embedded by
 * the build option RCBENCH_IOMCU_BIN (main/CMakeLists.txt).  A build without
 * it carries none and offers no update.
 */
#ifdef RCBENCH_IOMCU_IMAGE
extern const uint8_t _binary_rcbench_iomcu_bin_start[];
extern const uint8_t _binary_rcbench_iomcu_bin_end[];
#define CU_IMG _binary_rcbench_iomcu_bin_start
#define CU_LEN ((uint32_t)(_binary_rcbench_iomcu_bin_end \
                           - _binary_rcbench_iomcu_bin_start))
#else
#define CU_IMG ((const uint8_t *)NULL)
#define CU_LEN 0u
#endif

/* Steps run back to back in one pass of the control task while an update
 * sends: a block is 9 exchanges, and the pass's own work and its 5 ms
 * delay come between two slices. */
#define CU_SLICE_MS 20u

typedef enum {
    CU_IDLE = 0,
    CU_CHECKING,    /* does it run the carried build: link_uph_check()   */
    CU_OFFERED,     /* it does not; the question waits for START         */
    CU_DECLINED,    /* CANCEL, until the next link-up                    */
    CU_UPDATING,    /* link_uph_update(): the link is the update's       */
    CU_CONFIRMING,  /* an image found on trial is bought                 */
} cu_state_t;

/* Control task only. */
static link_uph_t s_cu;
static cu_state_t s_cu_state;
static cu_version_t s_cu_running;
static bool       s_cu_missed;     /* the update's last exchange went unanswered */
static bool       s_cu_took_down;  /* the update has taken the link down  */
static uint32_t   s_cu_starts_taken;
/* CANCEL holds for the version it was given for, until the panel restarts. */
static bool         s_cu_declined;
static cu_version_t s_cu_declined_for;
/* Render side -> control task: STARTs given on the question, and the offer
 * the question was declined for (its offer_gen; 0 none). */
static atomic_uint s_cu_starts;
static atomic_uint s_cu_declined_gen;
/* Control task: the offer_gen of the offer published last. */
static uint32_t    s_cu_offer_gen;

/* Control task: every change of s_cu_state, and with it s_cu_busy. */
static void cu_enter(cu_state_t st)
{
    s_cu_state = st;
    atomic_store(&s_cu_busy, st == CU_UPDATING || st == CU_CONFIRMING);
}

/* Whether the update may start: see fw_coproc_may_start().  From any task. */
static bool cu_may_start(void)
{
    return fw_coproc_may_start(!s_wifi_ok || wifi_access_down(),
                               atomic_load(&s_fw_installing));
}

static cu_version_t cu_carried(void)
{
    return (cu_version_t){ RCBENCH_VERSION_MAJOR, RCBENCH_VERSION_MINOR,
                           RCBENCH_VERSION_PATCH };
}

/*
 * The update's own wire: exchange() after the bus has been asked back.  A
 * coprocessor that restarts answers nothing for a few hundred milliseconds,
 * and a transmitter nobody acknowledges is off the bus in about 4 ms and
 * does not come back on its own (probe_identity()).  Asked only after an
 * exchange went unanswered, so a block costs no status read.
 */
static bool cu_port_exchange(void *ctx, link_host_t *host,
                             const link_msg_t *req, link_msg_t *reply)
{
    (void)ctx;
    if (s_cu_missed && can_twai_recover() == CAN_TWAI_RECOVERING) {
        ++s_recoveries;
        ++s_recoveries_total;
    }
    s_cu_missed = !exchange(host, req, reply);
    return !s_cu_missed;
}

static const link_port_t k_cu_port = {
    cu_port_exchange, port_now, NULL, port_between,
};

static void cu_publish_offer(bool offered)
{
    snap_lock();
    s_snap.cu.offered = offered;
    s_snap.cu.running = s_cu_running;
    s_snap.cu.starts_taken = s_cu_starts_taken;
    if (offered) {
        ++s_snap.cu.offer_gen;
    }
    s_cu_offer_gen = s_snap.cu.offer_gen;
    snap_unlock();
}

static cu_step_t cu_step_of(uint8_t phase)
{
    switch (phase) {
    case LINK_UPH_BEGIN:
    case LINK_UPH_SEND:
    case LINK_UPH_RESYNC:
    case LINK_UPH_FINISH:      return CU_STEP_SEND;
    case LINK_UPH_VERIFY:
    case LINK_UPH_ACTIVATE:    return CU_STEP_VERIFY;
    case LINK_UPH_RESTARTING:  return CU_STEP_RESTART;
    case LINK_UPH_BUY:
    case LINK_UPH_DONE:        return CU_STEP_BUY;
    case LINK_UPH_PREPARE:
    default:                   return CU_STEP_PREPARE;
    }
}

static void cu_publish_progress(bool active)
{
    snap_lock();
    s_snap.cu.active = active;
    s_snap.cu.step   = (uint8_t)cu_step_of(s_cu.phase);
    s_snap.cu.block  = s_cu.block;
    s_snap.cu.blocks = s_cu.blocks;
    s_snap.cu.starts_taken = s_cu_starts_taken;
    snap_unlock();
}

/*
 * A coprocessor answered its identity: a link-up, or a probe from a
 * coprocessor of another major.  Its build is checked against the carried
 * image, whatever was decided for the coprocessor before it.
 */
void cu_link_up(const link_msg_t *reply)
{
    if (CU_LEN == 0u || reply == NULL || s_cu_state == CU_UPDATING) {
        return;
    }
    s_cu_running = (cu_version_t){ reply->regs[LINK_ID_FIRMWARE_MAJOR],
                                   reply->regs[LINK_ID_FIRMWARE_MINOR],
                                   reply->regs[LINK_ID_FIRMWARE_PATCH] };
    cu_publish_offer(false);
    link_uph_check(&s_cu, CU_IMG, CU_LEN, now_ms());
    cu_enter(CU_CHECKING);
}

/* Why an update stopped, in the alert's words. */
static void cu_alert_failed(void)
{
    char why[48];
    switch (s_cu.result) {
    case LINK_UPH_NO_ANSWER:
        snprintf(why, sizeof(why), "%s", TR(CU_WHY_NO_ANSWER));
        break;
    case LINK_UPH_BAD_DIGEST:
        snprintf(why, sizeof(why), "%s", TR(CU_WHY_DIGEST));
        break;
    case LINK_UPH_BUSY:
        snprintf(why, sizeof(why), "%s", TR(CU_WHY_BUSY));
        break;
    case LINK_UPH_RESTARTED_OUT:
        snprintf(why, sizeof(why), "%s", TR(CU_WHY_RESTARTS));
        break;
    case LINK_UPH_NOT_STARTED:
        snprintf(why, sizeof(why), "%s", TR(CU_WHY_NOT_STARTED));
        break;
    case LINK_UPH_REFUSED:
    default:
        snprintf(why, sizeof(why), TR(CU_WHY_REFUSED),
                 (unsigned)s_cu.error);
        break;
    }
    char line[ALERT_MAX];
    snprintf(line, sizeof(line), TR(ALERT_CU_FAILED), why);
    control_alert(line);
}

/* The check has ended: what it found decides the offer. */
static void cu_checked(void)
{
    switch (s_cu.result) {
    case LINK_UPH_SAME:
        ESP_LOGI(TAG, "coprocessor runs the build this panel carries");
        cu_enter(CU_IDLE);
        break;
    case LINK_UPH_DIFFERENT:
        if (s_cu_declined
            && memcmp(&s_cu_declined_for, &s_cu_running,
                      sizeof(s_cu_running)) == 0) {
            ESP_LOGI(TAG, "coprocessor runs another build; its update was "
                          "declined");
            cu_enter(CU_DECLINED);
            break;
        }
        ESP_LOGI(TAG, "coprocessor %u.%u.%u runs another build; %u.%u.%u "
                      "offered", (unsigned)s_cu_running.major,
                 (unsigned)s_cu_running.minor, (unsigned)s_cu_running.patch,
                 (unsigned)RCBENCH_VERSION_MAJOR,
                 (unsigned)RCBENCH_VERSION_MINOR,
                 (unsigned)RCBENCH_VERSION_PATCH);
        cu_enter(CU_OFFERED);
        control_alert(TR(ALERT_CU_OFFER));
        cu_publish_offer(true);
        break;
    case LINK_UPH_TRIAL:
        /* A new image runs on trial, and answered: that is the condition
         * for keeping it (link_pages.h), whichever panel sent it. */
        ESP_LOGI(TAG, "coprocessor runs an image on trial; buying it");
        link_uph_confirm(&s_cu, now_ms());
        cu_enter(CU_CONFIRMING);
        break;
    case LINK_UPH_NEEDS_USB:
        ESP_LOGW(TAG, "coprocessor takes no update over CAN");
        control_alert(TR(ALERT_CU_USB));
        cu_enter(CU_IDLE);
        break;
    default:
        ESP_LOGW(TAG, "coprocessor build check ended %u, error %u",
                 (unsigned)s_cu.result, (unsigned)s_cu.error);
        cu_enter(CU_IDLE);
        break;
    }
}

/* The update has ended: the link goes back to the poll, which finds the
 * coprocessor again by its identity. */
static void cu_ended(void)
{
    if (s_cu.result == LINK_UPH_UPDATED) {
        ESP_LOGI(TAG, "coprocessor updated to %u.%u.%u",
                 (unsigned)RCBENCH_VERSION_MAJOR,
                 (unsigned)RCBENCH_VERSION_MINOR,
                 (unsigned)RCBENCH_VERSION_PATCH);
        char line[ALERT_MAX];
        snprintf(line, sizeof(line), TR(ALERT_CU_DONE),
                 (unsigned)RCBENCH_VERSION_MAJOR,
                 (unsigned)RCBENCH_VERSION_MINOR,
                 (unsigned)RCBENCH_VERSION_PATCH);
        control_alert(line);
    } else if (s_cu.result == LINK_UPH_NEEDS_USB) {
        control_alert(TR(ALERT_CU_USB));
    } else {
        ESP_LOGW(TAG, "coprocessor update ended %u, error %u, block %u of %u",
                 (unsigned)s_cu.result, (unsigned)s_cu.error,
                 (unsigned)s_cu.block, (unsigned)s_cu.blocks);
        cu_alert_failed();
    }
    cu_enter(CU_IDLE);
    s_cu_took_down = false;
    s_cu_missed = false;
    /* The link-lost screen counts from here, not from the update's start:
     * the poll finds the coprocessor inside its LINK_LOST_SCREEN_MS. */
    if (atomic_load(&s_link_lost_ms) != 0u) {
        atomic_store(&s_link_lost_ms, now_ms());
    }
    cu_publish_progress(false);
}

/*
 * One pass of the update, before the poll.  Returns true while the update
 * holds the link, and the poll is then not run.
 *
 * The check and the BUY of an image on trial share the link with the poll,
 * one step a pass.  An update takes the link down first -- the poll's own
 * edge, with everything that ends there: the arm, the supply, the pages'
 * state -- and then runs alone for CU_SLICE_MS a pass.  The coprocessor
 * refuses ARM for the whole session, and the panel shows no band.
 */
static bool cu_service(bool *link_up, bench_state_t *bench,
                       uint32_t *last_poll, uint32_t *last_status)
{
    switch (s_cu_state) {
    case CU_CHECKING:
        if (link_uph_step(&s_cu, &s_host, &k_port) == LINK_UPH_DONE) {
            cu_checked();
        }
        return false;
    case CU_CONFIRMING:
        if (link_uph_step(&s_cu, &s_host, &k_port) == LINK_UPH_DONE) {
            if (s_cu.result == LINK_UPH_UPDATED) {
                control_alert(TR(ALERT_CU_KEPT));
            } else {
                ESP_LOGW(TAG, "the image on trial was not bought: %u, "
                              "error %u", (unsigned)s_cu.result,
                         (unsigned)s_cu.error);
            }
            cu_enter(CU_IDLE);
        }
        return false;
    case CU_OFFERED:
        /* A decline counts for the offer it was given on only: one that
         * crossed a newer link-up's check is for an offer gone. */
        if (atomic_load(&s_cu_declined_gen) == s_cu_offer_gen) {
            ESP_LOGI(TAG, "coprocessor update declined");
            cu_enter(CU_DECLINED);
            s_cu_declined = true;
            s_cu_declined_for = s_cu_running;
            cu_publish_offer(false);
            return false;
        }
        if (atomic_load(&s_cu_starts) == s_cu_starts_taken) {
            return false;
        }
        s_cu_starts_taken = atomic_load(&s_cu_starts);
        /* Busy first, then the install's flag: see s_fw_installing. */
        atomic_store(&s_cu_busy, true);
        if (!cu_may_start()) {
            /* The panel's radio is up or an install of the panel has
             * begun: the START is spent and the offer made again, so the
             * question opens once both have ended. */
            ESP_LOGW(TAG, "coprocessor update refused: the radio is on or "
                          "the panel installs");
            atomic_store(&s_cu_busy, false);
            cu_publish_offer(true);
            return false;
        }
        ESP_LOGI(TAG, "coprocessor update: %lu bytes",
                 (unsigned long)CU_LEN);
        cu_publish_offer(false);
        link_uph_update(&s_cu, CU_IMG, CU_LEN, now_ms());
        cu_enter(CU_UPDATING);
        s_cu_took_down = false;
        s_cu_missed = false;
        cu_publish_progress(true);
        return true;
    case CU_UPDATING:
        break;
    case CU_IDLE:
    case CU_DECLINED:
    default:
        /* A START given while nothing is offered is spent. */
        if (atomic_load(&s_cu_starts) != s_cu_starts_taken) {
            s_cu_starts_taken = atomic_load(&s_cu_starts);
            cu_publish_offer(false);
        }
        return false;
    }

    if (!s_cu_took_down) {
        /* The poll's edge, once: s_link_quiet takes the link down at this
         * poll without a request. */
        if (*link_up) {
            s_link_quiet = true;
            (void)poll_far_end(link_up, bench, last_poll, last_status);
        }
        s_cu_took_down = true;
    }
    const uint32_t t0 = now_ms();
    do {
        if (link_uph_step(&s_cu, &s_host, &k_cu_port) == LINK_UPH_DONE) {
            cu_ended();
            return false;
        }
    } while ((uint32_t)(now_ms() - t0) < CU_SLICE_MS);
    cu_publish_progress(true);
    return true;
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

/* The render side's services, the touch drain and the screens'
 * commands: panel_render.c. */

/*
 * Queue a command, and do not lose it.  Every command is already taken from
 * its screen by the time it gets here, so a refused send is a discarded
 * disarm or a throttle that never arrives.
 */
void send_cmd(const panel_cmd_t *in)
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

/* ------------------------------------------------- Wi-Fi and the update */

/* Whether a run's log, or a servo run's CSV or report, is open or about
 * to be: a servo run under way, an OPEN queued for the logger and not yet
 * answered, and the file from its open to its close, without a gap. */
static bool run_open_now(void)
{
    return atomic_load(&s_log_run_now) != 0u
           || atomic_load(&s_log_open_run) != 0u
           || atomic_load(&s_test_open_run) != 0u
           || atomic_load(&s_test_open_txt) != 0u
           || atomic_load(&s_servo_testing)
           || atomic_load(&s_test_opens_sent)
                  != atomic_load(&s_test_opens_done);
}

/* Whether flash may be written now, and if not why: see fw_gate(). */
static fw_gate_t fw_gate_now(bool armed)
{
    return fw_gate(armed, supply_live_or_coming(), run_open_now(),
                   s_artbusy || atomic_load(&s_keeping),
                   atomic_load(&s_cu_busy));
}

/* The update screen, drawn now: the install's loop runs no router tick. */
static void update_frame(update_state_t st, const char *version,
                         uint32_t written, uint32_t total)
{
    update_view_t v = { .state = st, .written = written, .total = total };
    snprintf(v.version, sizeof(v.version), "%s", version);
    update_screen_set(&v);
    gfx_canvas_t *c = display_canvas();
    if (c != NULL) {
        ui_router_render(c, display_back_index());
        display_flip();
    }
}

/*
 * INSTALL: the 2 s hold completed on the question.
 *
 * In order: the update screen, the radio off and waited for, the image
 * checked once more against the slot and the running build, the gate once
 * more, the control task stood still, then the write a piece at a time with
 * the bytes on screen, the check of what the slot holds, otadata, and the
 * restart.  Nothing touches the flash before the control task stands still
 * and the radio is off.  A failure before the first erase changes nothing;
 * one after it leaves the running build as the one that starts.
 */
static void fw_install_run(void)
{
    const uint8_t  *img = NULL;
    uint32_t        bytes = 0u;
    fw_image_info_t info;
    if (!wifi_access_take(&img, &bytes, &info)) {
        return;
    }
    /* Before the gate is read: see s_fw_installing. */
    atomic_store(&s_fw_installing, true);
    ui_router_goto(SCREEN_UPDATE);
    (void)wifi_screen_take();        /* the screen's leave: radio off */
    update_frame(UPDATE_WRITING, info.version_text, 0u, bytes);

    /* The radio goes first, and wholly: no request may be under way. */
    wifi_access_close(WA_OFF_INSTALL);
    bool down;
    const uint32_t t0 = now_ms();
    do {
        vTaskDelay(pdMS_TO_TICKS(20));
        down = wifi_access_down();
    } while (!down && (uint32_t)(now_ms() - t0) < 20000u);

    static fw_install_t in;
    fw_image_info_t running;
    (void)fw_ota_running(&running);
    bool armed;
    snap_lock();
    armed = s_snap.arm_wanted || s_snap.armed;
    snap_unlock();
    fw_err_t e = FW_ERR_BUSY;
    if (down && fw_gate_now(armed) == FW_GATE_OPEN) {
        e = fw_install_begin(&in, fw_ota_flash(), img, bytes,
                             fw_ota_slot_bytes(), &running);
    }
    fw_install_status_t st = FW_INSTALL_FAILED;
    if (e == FW_OK) {
        atomic_store(&s_fw_quiet_req, true);
        const uint32_t t1 = now_ms();
        while (!atomic_load(&s_fw_quiet_ack)
               && (uint32_t)(now_ms() - t1) < 1000u) {
            vTaskDelay(pdMS_TO_TICKS(CONTROL_PERIOD_MS));
        }
        const bool quiet = atomic_load(&s_fw_quiet_ack);
        ESP_LOGI(TAG, "writing %s, %lu bytes", info.version_text,
                 (unsigned long)bytes);
        do {
            st = fw_install_step(&in, quiet);
            update_frame(UPDATE_WRITING, info.version_text, in.done, bytes);
        } while (st == FW_INSTALL_RUNNING);
        if (st == FW_INSTALL_DONE) {
            update_frame(UPDATE_RESTARTING, info.version_text, bytes, bytes);
            fw_ota_note_restart(info.version_text);
            ESP_LOGI(TAG, "written and checked; restarting");
            vTaskDelay(pdMS_TO_TICKS(1500));
            esp_restart();
        }
    }
    ESP_LOGE(TAG, "install of %s failed: %s", info.version_text,
             fw_err_str((e != FW_OK) ? e : in.err));
    atomic_store(&s_fw_quiet_req, false);
    atomic_store(&s_fw_installing, false);
    wifi_access_release();
    update_frame(UPDATE_FAILED, info.version_text, in.done, bytes);
}

/*
 * FILES & UPDATE without Wi-Fi: the screen says so and offers nothing, and
 * what it asks for is dropped.
 */
static void wifi_unavailable_service(void)
{
    (void)wifi_screen_take();
    if (ui_router_current() == SCREEN_WIFI) {
        wifi_screen_set_unavailable();
    }
}

/*
 * FILES & UPDATE, once a frame: the radio's gate, STOP, what the screen
 * asked for, and what it shows.
 *
 * The radio's gate is wa_gate(): SAFE, the supply off and no log run open.
 * STOP and ARM end the session and drop an image that waits; the supply
 * coming on or a run opening ends the session and leaves the image, which
 * the question then says cannot be installed yet.
 */
static void wifi_service(bool armed, uint32_t stops_now)
{
    if (!s_wifi_ok) {
        wifi_unavailable_service();
        return;
    }
    static uint32_t seen_stops;
    static bool     started;
    if (!started) {
        seen_stops = stops_now;
        started = true;
    }
    const bool supply = supply_live_or_coming();
    const bool run = run_open_now();
    const wa_off_t gate = wa_gate(armed, supply, run);
    const bool stop = (stops_now != seen_stops);
    seen_stops = stops_now;
    const fw_gate_t fgate = fw_gate_now(armed);

    wifi_access_set_german(ui_text_language() == UI_LANG_DE);
    (void)wifi_access_tick(gate, stop,
                           fgate == FW_GATE_OPEN || fgate == FW_GATE_RUN);
    if (stop || gate == WA_OFF_ARMED) {
        wifi_access_discard();
    }

    const unsigned req = wifi_screen_take();
    if ((req & WIFI_REQ_OFF) != 0u) {
        wifi_access_close(WA_OFF_ASKED);
    }
    if ((req & WIFI_REQ_DISCARD) != 0u) {
        wifi_access_discard();
    }
    if ((req & WIFI_REQ_SAVE_NET) != 0u) {
        char ssid[WIFI_SSID_MAX + 1], pass[WIFI_PASS_MAX + 1];
        wifi_screen_net(ssid, pass);
        (void)wifi_access_save_net(ssid, pass);
        memset(pass, 0, sizeof(pass));
    }
    if ((req & WIFI_REQ_FORGET) != 0u) {
        wifi_access_forget();
    }
    if ((req & WIFI_REQ_CODE_ON) != 0u) {
        wifi_access_set_code(true);
    }
    if ((req & WIFI_REQ_CODE_OFF) != 0u) {
        wifi_access_set_code(false);
    }
    if ((req & WIFI_REQ_AP) != 0u) {
        (void)wifi_access_start(WA_MODE_AP, gate);
    }
    if ((req & WIFI_REQ_JOIN) != 0u) {
        (void)wifi_access_start(WA_MODE_JOIN, gate);
    }
    if ((req & WIFI_REQ_SCAN) != 0u) {
        (void)wifi_access_start(WA_MODE_SCAN, gate);
    }
    if ((req & WIFI_REQ_INSTALL) != 0u) {
        s_update_back = SCREEN_LOGS;
        fw_install_run();
    }
    if (update_screen_take_close()) {
        ui_router_goto(s_update_back);
    }

    if (ui_router_current() != SCREEN_WIFI) {
        return;
    }
    /* What the screen shows: the session's and the receiver's enums map
     * one to one onto the screen's, which test_wifi_screen holds. */
    /* 2 kB of copies, in PSRAM: internal RAM is what the radio is short of. */
    static wifi_access_state_t *ap;
    static wifi_view_t         *vp;
    if (ap == NULL || vp == NULL) {
        /* A pair or neither: a half kept would be read as both next frame. */
        free(ap);
        free(vp);
        ap = heap_caps_malloc(sizeof(*ap), MALLOC_CAP_SPIRAM);
        vp = heap_caps_malloc(sizeof(*vp), MALLOC_CAP_SPIRAM);
        if (ap == NULL || vp == NULL) {
            free(ap);
            free(vp);
            ap = NULL;
            vp = NULL;
            return;
        }
    }
    wifi_access_state_t *const a = ap;
    wifi_view_t *const v = vp;
    wifi_access_get(a);
    memset(v, 0, sizeof(*v));
    v->block = (wifi_why_t)gate;
    v->have_net = a->conf.have_net;
    snprintf(v->net, sizeof(v->net), "%s", a->conf.ssid);
    v->code_on = a->conf.code_on;
    v->save_failed = a->save_failed;
    v->radio = (wifi_radio_t)a->w.state;
    v->off = (wifi_why_t)a->w.off;
    v->join = (wifi_join_t)a->w.join_err;
    snprintf(v->name, sizeof(v->name), "%s", a->name);
    snprintf(v->pass, sizeof(v->pass), "%s", a->w.pass);
    snprintf(v->code, sizeof(v->code), "%s", a->w.code);
    memcpy(v->ip, &a->w.ip, sizeof(v->ip));
    v->channel = a->channel;
    v->idle_left_s = a->idle_left_ms / 1000u;
    v->scanned = a->scanned;
    v->scan_n = (int)((a->scan.n < WIFI_SCAN_ROWS) ? a->scan.n : WIFI_SCAN_ROWS);
    for (int i = 0; i < v->scan_n; ++i) {
        snprintf(v->scan[i], sizeof(v->scan[i]), "%s", a->scan.net[i].ssid);
        v->rssi[i] = a->scan.net[i].rssi;
    }
    v->up = (wifi_up_t)a->up;
    v->received = a->received;
    v->total = a->total;
    v->offer = (wifi_offer_t)a->offer;
    v->gate = (wifi_gate_t)fgate;
    snprintf(v->new_ver, sizeof(v->new_ver), "%s", a->info.version_text);
    snprintf(v->run_ver, sizeof(v->run_ver), "%s", RCBENCH_VERSION_STRING);
    wifi_screen_set(v);
}

/* ------------------------------------------- GitHub: check and download */

/*
 * SETUP > UPDATES, the start-up check and the reminder (upd_screen.h), on
 * wifi_access's fetch jobs (wa_fetch.h).
 *
 * A check needs what the radio needs: SAFE, the supply off, no log run,
 * and neither a panel install nor the coprocessor's update.  A download
 * needs what an install needs (fw_gate_now()): it is one, from its hold
 * on, and says so in s_fw_installing before it reads the coprocessor's
 * flag.  Each frame a running job is ended for STOP, for FILES & UPDATE
 * opening, for CANCEL, and for its gate closing.
 */
static upd_driver_t s_upd;
static uint32_t     s_upd_boot_gen;   /* the start-up check's job; 0 none */
static bool         s_upd_dl;         /* a download, from its hold to its end */

static wa_why_t why_of_fw_gate(fw_gate_t g)
{
    switch (g) {
    case FW_GATE_ARMED:   return WA_WHY_ARMED;
    case FW_GATE_SUPPLY:  return WA_WHY_SUPPLY;
    case FW_GATE_RUN:     return WA_WHY_RUN;
    case FW_GATE_DRAWING: return WA_WHY_DRAWING;
    case FW_GATE_COPROC:  return WA_WHY_COPROC;
    case FW_GATE_OPEN:
    default:              return WA_WHY_NONE;
    }
}

static wa_why_t upd_check_gate(bool armed)
{
    return wa_fetch_gate(armed, supply_live_or_coming(), run_open_now(),
                         atomic_load(&s_cu_busy),
                         atomic_load(&s_fw_installing));
}

static bool upd_found_newer(const wa_fetch_t *f)
{
    return f->job == WA_JOB_CHECK && f->phase == WA_PH_ENDED
           && f->got == WA_GOT_NEWER;
}

/* @p unsafe: ARMED, an arm waiting, or the supply's output on or coming. */
static upd_feed_t upd_feed_now(bool armed, bool unsafe, const cu_snap_t *cs,
                               const wa_fetch_t *f, bool have_net)
{
    return (upd_feed_t){
        .now_ms     = now_ms(),
        .remind     = settings_get_bool(SET_UPD_REMIND),
        .available  = s_wifi_ok,
        .have_net   = have_net,
        .gate_open  = upd_check_gate(armed) == WA_WHY_NONE,
        .files_open = ui_router_current() == SCREEN_WIFI,
        .coproc     = cs->offered || cs->active || atomic_load(&s_cu_busy),
        .unsafe     = unsafe,
        .newer      = s_upd_boot_gen != 0u && f->gen == s_upd_boot_gen
                      && upd_found_newer(f),
    };
}

/*
 * The reminder, before the coprocessor's question is served: it yields to
 * that question in the same frame the question may open.
 */
static void upd_remind_service(bool armed, bool unsafe, const cu_snap_t *cs)
{
    if (!s_wifi_ok) {
        return;
    }
    wa_fetch_t f;
    wifi_access_fetch_get(&f);
    char ssid[WIFI_SSID_MAX + 1];
    const bool have_net = wifi_access_home(ssid, sizeof(ssid));
    const upd_feed_t feed = upd_feed_now(armed, unsafe, cs, &f, have_net);
    upd_remind_drive(&s_upd, &feed);
}

static void upd_ver_text(fw_version_t v, char *out, size_t n)
{
    char t[FW_REL_VER_TEXT];
    fw_release_version_text(v, t);
    snprintf(out, n, "%s", t);
}

/*
 * Once a frame, after wifi_service(): the start-up check, what UPDATES and
 * the reminder ask for, the job's aborts, the download's screen, and what
 * UPDATES shows.  @p armed includes an arm waiting.
 */
static void upd_service(bool armed, bool unsafe, uint32_t stops_now,
                        const cu_snap_t *cs)
{
    static uint32_t seen_stops;
    static bool     started;
    if (!started) {
        seen_stops = stops_now;
        started = true;
    }
    const bool stop = (stops_now != seen_stops);
    seen_stops = stops_now;

    static upd_view_t v;     /* off the stack: the loop's is the display's */
    memset(&v, 0, sizeof(v));
    snprintf(v.running, sizeof(v.running), "%s", RCBENCH_VERSION_STRING);
    if (!s_wifi_ok) {
        (void)upd_screen_take();
        v.unavailable = true;
        upd_screen_set(&v);
        return;
    }

    v.have_net = wifi_access_home(v.net, sizeof(v.net));
    wa_fetch_t f;
    wifi_access_fetch_get(&f);
    const unsigned req = upd_screen_take();
    const fw_version_t none = { 0u, 0u, 0u };

    /* The start-up check, once, at a moment the bench allows it. */
    const upd_feed_t feed = upd_feed_now(armed, unsafe, cs, &f, v.have_net);
    if (upd_startup_due(&s_upd, &feed)
        && wifi_access_fetch_start(WA_JOB_CHECK, none, upd_check_gate(armed))
           == WA_GOT_NONE) {
        wifi_access_fetch_get(&f);
        s_upd_boot_gen = f.gen;
        ESP_LOGI(TAG, "GitHub: start-up check");
    }
    if ((req & UPD_REQ_CHECK) != 0u && !wa_fetch_busy(&f)) {
        (void)wifi_access_fetch_start(WA_JOB_CHECK, none,
                                      upd_check_gate(armed));
        wifi_access_fetch_get(&f);
    }
    if ((req & UPD_REQ_DOWNLOAD) != 0u && !wa_fetch_busy(&f) && !s_upd_dl
        && upd_found_newer(&f)) {
        /* Before the gate is read: see s_fw_installing. */
        atomic_store(&s_fw_installing, true);
        s_upd_dl = true;
        s_update_back = (ui_router_current() == SCREEN_UPDATES)
                            ? SCREEN_UPDATES : SCREEN_OVERVIEW;
        (void)wifi_access_fetch_start(WA_JOB_DOWNLOAD, f.found,
                                      why_of_fw_gate(fw_gate_now(armed)));
        (void)update_screen_take_cancel();
        ui_router_goto(SCREEN_UPDATE);
        wifi_access_fetch_get(&f);
    }

    /* A job under way ends for any of these. */
    wa_why_t abort = WA_WHY_NONE;
    const bool cancel = (req & UPD_REQ_CANCEL) != 0u
                        || (s_upd_dl && update_screen_take_cancel());
    if (wa_fetch_busy(&f)) {
        if (stop) {
            abort = WA_WHY_STOP;
        } else if (ui_router_current() == SCREEN_WIFI) {
            abort = WA_WHY_FILES;
        } else if (cancel) {
            abort = WA_WHY_CANCEL;
        } else if (f.job == WA_JOB_DOWNLOAD) {
            abort = why_of_fw_gate(fw_gate_now(armed));
        } else {
            abort = upd_check_gate(armed);
        }
    }
    wifi_access_fetch_tick(abort);
    wifi_access_fetch_get(&f);

    /* What UPDATES and the download's screen show. */
    const bool busy = wa_fetch_busy(&f);
    v.block = (upd_why_t)upd_check_gate(armed);
    if (s_upd_dl || busy) {
        v.dl_block = UPD_WHY_BUSY;
    } else {
        v.dl_block = (upd_why_t)why_of_fw_gate(fw_gate_now(armed));
        if (v.dl_block == UPD_WHY_NONE && atomic_load(&s_fw_installing)) {
            v.dl_block = UPD_WHY_INSTALL;
        }
    }
    v.job = (upd_job_t)f.job;
    v.phase = (upd_phase_t)f.phase;
    v.got = (upd_got_t)f.got;
    v.why = (upd_why_t)f.why;
    v.join = (wifi_join_t)f.join;
    v.rel = (upd_rel_t)f.rel;
    v.status = f.status;
    v.budget_s = f.budget_ms / 1000u;
    if (f.job == WA_JOB_DOWNLOAD || f.got == WA_GOT_NEWER
        || f.got == WA_GOT_CURRENT) {
        upd_ver_text(f.found, v.found, sizeof(v.found));
    }
    const uint32_t left_ms = wa_fetch_left(&f, now_ms());
    v.left_s = (left_ms + 999u) / 1000u;
    v.done = f.done;
    v.total = f.total;
    upd_screen_set(&v);

    if (!s_upd_dl) {
        return;
    }
    static update_view_t u;
    memset(&u, 0, sizeof(u));
    snprintf(u.version, sizeof(u.version), "%s", v.found);
    u.written = f.done;
    u.total = f.total;
    upd_result_text(&v, u.what, sizeof(u.what), u.why, sizeof(u.why));
    if (busy) {
        u.state = UPDATE_FETCHING;
        update_screen_set(&u);
        return;
    }
    s_upd_dl = false;
    if (f.got == WA_GOT_STAGED) {
        /* The installer's own flag from here: it sets it before it reads
         * the coprocessor's again. */
        atomic_store(&s_fw_installing, false);
        ESP_LOGI(TAG, "GitHub: %s downloaded and checked", v.found);
        fw_install_run();
        return;
    }
    atomic_store(&s_fw_installing, false);
    ESP_LOGW(TAG, "GitHub: the download of %s ended: %d", v.found,
             (int)f.got);
    u.state = UPDATE_FETCH_FAILED;
    update_screen_set(&u);
}

/* ------------------------------------------------------------------ main */

/*
 * The coprocessor update's screen, once a frame on the render side:
 * coproc_update_drive() moves the router and drains the screen's latches,
 * and its STARTs and decline go to the control task.
 * @p unsafe: armed, an arm waiting, or the supply's output on or coming.
 */
static void cu_render_service(bool unsafe, const cu_snap_t *cs)
{
    static cu_driver_t driver;

    const cu_feed_t feed = {
        .offered      = cs->offered,
        .offer_gen    = cs->offer_gen,
        .active       = cs->active,
        .step         = (cu_step_t)cs->step,
        .block        = cs->block,
        .blocks       = cs->blocks,
        .starts_taken = cs->starts_taken,
        .running      = cs->running,
        .carried      = cu_carried(),
    };
    cu_hand_t hand;
    coproc_update_drive(&driver, &feed, unsafe, &hand);
    if (hand.starts > 0u) {
        atomic_fetch_add(&s_cu_starts, hand.starts);
    }
    if (hand.decline) {
        atomic_store(&s_cu_declined_gen, feed.offer_gen);
    }
}

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
