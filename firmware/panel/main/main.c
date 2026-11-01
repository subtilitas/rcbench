/**
 * @file main.c
 * @brief The panel application: app_main, the console report and the
 *        main loop's hardware glue.
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
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* sdkconfig.defaults sets the main task's stack to 8192 bytes, which
 * tools/stack_check.py holds to the UI's deepest chain.  ESP-IDF reads the
 * defaults only when it creates sdkconfig, so a checkout whose sdkconfig
 * predates that line builds with 3584 bytes, below what the UI needs.  Such
 * a build stops here. */
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
#include "esc_wire_panel.h"
#include "hitec_panel.h"
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
#include "servo_link.h"
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
#include "esc_stick_panel.h"
#include "rcbench_version.h"
#include "flash_gate.h"
#include "safety_gate.h"
#include "settings.h"
#include "settings_screen.h"
#include "settings_sensors.h"
#include "sensors_rows.h"
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
 * The ramp comes from the `Ramp limit` setting, 5 to 300%/s.  It governs
 * this bank, which is the modelled bench: the value it slews to is read by
 * telemetry_sim_step() and by nothing else, and only while the link is down.
 *
 * An IOmcu that is answering renders the raw command instead.  The
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

#include "touch.h"
#include "touch_map.h"
#include "fw_install.h"
#include "fw_ota.h"
#include "fw_release.h"
#include "fw_trial.h"
#include "upd_screen.h"
#include "wifi_access.h"
#include "wifi_screen.h"
#include "ui_cats.h"
#include "ui_screen.h"
#include "ui_card_text.h"
#include "ui_text.h"
#include "ui_theme.h"

#include "panel.h"
#include "panel_main.h"
#include "panel_trial.h"

static const char *TAG = "rcbench";

/* The screens draw a UI_PANEL_W x UI_PANEL_H canvas (ui_layout.h).  The
 * display's canvas is BOARD_UI_H_RES x BOARD_UI_V_RES (display_canvas()),
 * the whole BOARD_LCD_H_RES x BOARD_LCD_V_RES glass. */
_Static_assert(BOARD_UI_H_RES == UI_PANEL_W && BOARD_UI_V_RES == UI_PANEL_H,
               "the display's canvas is the size the screens draw");
_Static_assert(BOARD_UI_H_RES == BOARD_LCD_H_RES
               && BOARD_UI_V_RES == BOARD_LCD_V_RES,
               "the display's canvas is the whole glass");

/* Used during bring-up as well as in the loop, so file scope. */
link_host_t    s_host;
/* A link_cap_t bitmap from the IOmcu's identity page.  Zero until
 * something answers, which is also what it stays if nothing is fitted.
 * Read at bring-up, at every link-up edge and after a SENSE write is taken
 * (bits 3 and 4 follow the SENSE set-up), by the control task; the render
 * loop reads it for the menu marks, so it is atomic. */
atomic_uint    s_capabilities;
link_bringup_t s_bring;

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

/* --------------------------------------------------- the two loops' plumbing */

/*
 * The bench and the screen run on separate cores.
 *
 * Touch, the outputs, the link and the heartbeat live in the control task at
 * a fixed 5ms; drawing lives in app_main at whatever rate the panel manages.
 * A frame that costs 50ms then delays what is shown and not what the bench
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
/* s_wifi_ok and s_update_back: panel_update.c. */

/* A screen's command (panel_cmd_t): panel.h. */

/*
 * What became of the last output-page write.
 *
 * The control task writes it and app_main hands it to the screen, because
 * screen state is app_main's alone.  Atomic rather than volatile, for the
 * reason given for s_stop_request: volatile orders nothing
 * between processors and promises no atomicity, and these two tasks are
 * pinned to different cores.
 */
atomic_int s_outputs_result;

/*
 * What the IOmcu says its outputs are.
 *
 * Read from the far end rather than remembered here.  A binding describes
 * wiring, and this board is not the one the wires are in: a panel that kept a
 * configuration and pushed it would be applying it to whatever is on the
 * bench now.  The IOmcu keeps it in its own flash, and this asks.
 */
bind_reading_t s_outputs_read;
bool           s_outputs_read_fresh;    /* both under s_snap_lock */
/*
 * The state of the last reading, for the control task alone: a binding a
 * screen queued is written only over pages that were read
 * (bind_link_may_write()).  BIND_READ_NONE until the first read.
 */
bind_read_t    s_bind_read = BIND_READ_NONE;
/* The IOmcu serves BIND_CFG, BIND_OUT and BIND (protocol 4.10). */
uint16_t       s_far_minor;

/* The IOmcu's board identity, from the identity page at bring-up. */
uint16_t  s_board;

/* Fetching the board's drawing, and keeping it: panel_art.c. */

QueueHandle_t            s_touch_q;   /**< control task -> app_main */
QueueHandle_t            s_cmd_q;     /**< app_main -> control task */
/* The servo programmer's commands (kst_prog_cmd_t), a queue of their own:
 * s_cmd_q drops its oldest entry when it is full. */
#define KST_Q_LEN 4
QueueHandle_t     s_kst_q;     /**< app_main -> control task */
/** @brief The Hitec programmer's commands (hitec_prog_cmd_t) the queue
 *         holds, as KST_Q_LEN. */
#define HITEC_Q_LEN 4
/** @brief The ESC programmer's commands (esc_prog_cmd_t) the queue holds. */
#define ESC_Q_LEN 4
/*
 * One entry per bench sample, not a count of them.  A counter would lose the
 * samples a stalled renderer did not come back for, and the plot's time axis
 * would compress by exactly the frames it missed, which is the frame-rate
 * coupling this task exists to remove.  Full means the renderer is more than
 * SAMPLE_Q_LEN samples behind; the oldest is dropped, because a plot that is
 * 400ms out of date is worth less than one that is current.
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
    const uint32_t gen = ui_alert_slot_post(&s_snap.alert, text);
    snap_unlock();
    return gen;
}

void control_alert_withdraw(uint32_t gen)
{
    snap_lock();
    ui_alert_slot_withdraw(&s_snap.alert, gen);
    snap_unlock();
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

uint16_t  s_throttle_hundredths;   /* 0..LINK_THROTTLE_MAX */

/*
 * Crosses the two cores, so it is atomic rather than volatile, as
 * s_stop_live (panel_control.c) is: volatile orders nothing between
 * processors and promises no atomicity.
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
atomic_bool        s_stop_request;
/* A disarm, and the servo screen's own release, as flags rather than queue
 * entries: neither may be lost to an eviction.  See send_cmd(). */
/* The s_touch_q number of a press the pump applied a stop for and the
 * router will also latch, so the backstop does not stop the bench twice for
 * one press; 0 for none (stop_mark_keeps()). */
atomic_uint        s_stop_counted;
atomic_bool        s_disarm_request;
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
/** @brief The PROG 5V box's OFFs, counted as s_supply_offs counts the
 *         supply's (panel.h). */
atomic_uint s_prog5v_offs;
/** @brief A PROG 5V OFF asked, as s_supply_off_request is (panel.h). */
atomic_bool s_prog5v_off_request;
/** @brief The PROG 5V ONs queued (panel.h). */
atomic_uint s_psup_ons_sent;
/** @brief The PROG 5V ONs the control task is done with (panel.h). */
atomic_uint s_psup_ons_taken;
/** @brief The PROG 5V ONs applied, numbered (panel.h). */
atomic_uint s_psup_gen;
/** @brief The newest PROG 5V ON a whole frame has seen on (panel.h). */
atomic_uint s_psup_ack;
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
 * Where the PD mini is wired, from SUPPLY's PD MINI tab, in one word so a write
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
 * model: SUPPLY's PD MINI tab enables it.  Stored by the control task, read by
 * app_main for the screen's header, caps and the menu's badge.
 */
atomic_bool s_supply_real;
/* The PD mini's input in mV as its page last read, 0 while not known: the
 * screen keeps the voltage under it (PDMINI_CAP_MV). */
atomic_uint s_supply_vin_mv;
/* The PD mini's UART rate in baud as the IOmcu last reported it, for
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

/**
 * @brief The render loop's progress: moved once a pass of panel_frame_loop()
 *        and once per card read the loop makes on LOGS (panel_card.c), read
 *        by the control task's watchdog (frame_watch.h, control_pump()).
 */
atomic_uint s_render_progress;

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
 * The pole count the IOmcu holds, and whether it is current.
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
 * held: 14.3% low for a 12-pole motor converted as 14, and a factor of 21
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


/* False until the control task owns the safety state; bring-up polls the
 * link before that, with nothing to service. */
bool s_pump_live;



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
_Static_assert(PROG_LINK_COUNT_UNKNOWN == OUTPUTS_PORTS_UNKNOWN
               && (PROG_PORTS_NONE & 0xFFu) == PROG_LINK_COUNT_UNKNOWN
               && PROG_LINK_NO_PIN == OUTPUTS_NO_PIN
               && PROG_LINK_NO_PIN == PICKER_NO_PIN,
               "the PROG page's unknown count and missing pin reach the "
               "OUTPUTS screen and the pin picker unchanged");
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

/* --------------------------------------------------------- the control task */

/* The task itself, control_pump(), the heartbeat and the die temperature:
 * panel_control.c. */

/* The supply: panel_supply.c. */
/* The link modules' state, SENSE to PROG: panel_services.c. */

/* The bench's own state, the arm watch, the bench's totals, the latched
 * stop and the arming policy's act: panel_arm.c. */

/* The outputs screen's choice, onto the IOmcu's two output pages:
 * write_output_binding(), panel_arm.c. */

/* The GPIOs of the slots of the channels the horn drives
 * (servo_link_binding()), for the SERVO screen's TARGET row; whether a
 * binding was read. */
atomic_ullong s_servo_pins;
atomic_bool   s_servo_pins_known;
/* For the screen's runs: the SENSE page holds the encoder enabled. */
atomic_bool s_enc_held;

/* Disarming, and one motor command: panel_arm.c. */

/*
 * The SERVO screen's link (shared/outputs/servo_link.c) reaches this task's
 * state through these: the output bank, the arming policy, the alerts and
 * the disarm.  stack_check.py follows each by name (SERVO_CALLS).
 */
static bool servo_ctx_armed(void *ctx)
{
    (void)ctx;
    return outputs_armed(&s_out);
}

/* A stop applied by the pump, or a disarm posted while an exchange was on
 * the wire. */
static bool servo_ctx_stopped(void *ctx)
{
    (void)ctx;
    return arming_stopped(&s_arm) || atomic_load(&s_disarm_request);
}

static void servo_ctx_alert(void *ctx, servo_link_alert_t a)
{
    (void)ctx;
    switch (a) {
    case SERVO_LINK_ALERT_RESUME_OLD:
        control_alert(TR(ALERT_SWEEP_RESUME_OLD));
        break;
    case SERVO_LINK_ALERT_RESUME_REFUSED:
        control_alert(TR(ALERT_SWEEP_RESUME_REFUSED));
        break;
    case SERVO_LINK_ALERT_RESUME_UNTIMED:
        control_alert(TR(ALERT_SWEEP_RESUME_UNTIMED));
        break;
    default:
        break;
    }
}

static void servo_ctx_disarm(void *ctx, bool link_up)
{
    (void)ctx;
    (void)disarm_here(link_up);
}

static uint32_t servo_ctx_stop_count(void *ctx)
{
    (void)ctx;
    return arming_stop_count(&s_arm);
}

/* The surfaces and the programming ports, and no motor. */
static void servo_ctx_arm_surfaces(void *ctx)
{
    (void)ctx;
    arming_request_arm_roles(&s_arm, now_ms(),
                             OUT_ROLE_BIT(OUT_ROLE_SURFACE));
}

static const servo_link_ctx_t k_servo_ctx = {
    .host         = &s_host,
    .port         = &k_port,
    .prog         = &s_prog_link,
    .ctx          = NULL,
    .armed        = servo_ctx_armed,
    .stopped      = servo_ctx_stopped,
    .alert        = servo_ctx_alert,
    .disarm       = servo_ctx_disarm,
    .stop_count   = servo_ctx_stop_count,
    .arm_surfaces = servo_ctx_arm_surfaces,
};

/* What the screens asked for, drain_commands(): panel_arm.c. */

/*
 * Which run the log should be recording now; see log_run_kind_t.  None
 * while AUTO LOGS is off: no run is opened, no row posted, no number taken
 * and no card alert of a run can follow.  Switched off during a run, the
 * run ends there and its file closes; switched on during an ARMED or a
 * supply run, a run starts there.
 *
 * The bench comes first: an armed bench is the run whatever the supply is
 * doing.  Its name is the screen that armed it, read off the roles the arm
 * asked for (log_name_armed_prefix()), and kept for the run: a run's roles
 * change only through a disarm, which ends it.
 */
static log_run_kind_t log_run_wanted(void)
{
    const uint8_t roles = arming_roles(&s_arm);
    const log_prefix_t p = log_name_armed_prefix(
        roles != 0u, (roles & OUT_ROLE_BIT(OUT_ROLE_THROTTLE)) != 0u,
        outputs_role_armed(&s_out, OUT_ROLE_THROTTLE));
    return log_cadence_wanted(atomic_load(&s_log_auto), outputs_armed(&s_out),
                              p == LOG_PREFIX_MOTOR, s_supply.output,
                              s_log_kind);
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


/* The IOmcu's update, cu_service(): panel_update.c. */

/* The render side's services, the touch drain and the screens'
 * commands, send_cmd() among them: panel_render.c. */

/* Wi-Fi and the update, and GitHub's check and download: panel_update.c. */

/* ------------------------------------------------------------------ main */

/* The IOmcu update's screen, cu_render_service(): panel_update.c.  The
 * start-up, bring_up(): panel_boot.c.  The settings the control task
 * follows: panel_settings.c.  The render loop: panel_frame.c. */

/**
 * @brief Every task with its core and priority, once at start-up.
 *
 * A task created through xTaskCreate() or xTaskCreatePinnedToCore()
 * without a core runs on core 0 (wifi_radio.c); core 1 belongs to the
 * control task and the tasks pinned there.  "any" would be a task that
 * escaped that rule, such as one made with xTaskCreateStatic() without a
 * core.
 */
static void task_report(void)
{
    const UBaseType_t n = uxTaskGetNumberOfTasks() + 4u;
    TaskStatus_t *t = heap_caps_malloc(n * sizeof(*t), MALLOC_CAP_SPIRAM);
    if (t == NULL) {
        ESP_LOGW(TAG, "tasks: no room for the list");
        return;
    }
    const UBaseType_t got = uxTaskGetSystemState(t, n, NULL);
    ESP_LOGI(TAG, "tasks: %u", (unsigned)got);
    for (UBaseType_t i = 0; i < got; ++i) {
        const BaseType_t core = xTaskGetCoreID(t[i].xHandle);
        const char *where = (core == 0) ? "0" : (core == 1) ? "1" : "any";
        ESP_LOGI(TAG, "task %-16s core %-3s priority %2u", t[i].pcTaskName,
                 where, (unsigned)t[i].uxCurrentPriority);
    }
    heap_caps_free(t);
}

void app_main(void)
{
    servo_link_init(&k_servo_ctx);
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
     * OUTPUTS' board view is the same choice seen as the board, so it applies
     * through the same seam and takes the board's drawing the control task
     * published.
     */
    picker_screen_set_apply(outputs_apply);
    picker_screen_set_drawing_source(drawing_for_picker);
    /* The cat on OUTPUTS' board view is picked with the hardware random source. */
    ui_cats_set_random(esp_random);
    /* A port's PIN or FUNC asked on either screen goes to the control
     * task, which writes it on a bench at rest. */
    prog_ports_init_screens();
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
    hitec_record_load();
    /* The card was tried, with its reads at the start, and the settings
     * are handed on: two of the steps a new build's trial waits for
     * (panel_trial.h). */
    panel_trial_mark(FW_TRIAL_CARD | FW_TRIAL_SETTINGS);

    s_touch_q   = xQueueCreate(TOUCH_Q_LEN, sizeof(touch_item_t));
    s_cmd_q     = xQueueCreate(CMD_Q_LEN, sizeof(panel_cmd_t));
    s_kst_q     = xQueueCreate(KST_Q_LEN, sizeof(kst_prog_cmd_t));
    kst_prog_gate_init(&s_kst_gate);
    kst_panel_gate(&s_kst_gate);
    s_hitec_q   = xQueueCreate(HITEC_Q_LEN, sizeof(hitec_prog_cmd_t));
    hitec_prog_gate_init(&s_hitec_gate);
    hitec_panel_gate(&s_hitec_gate);
    s_esc_q     = xQueueCreate(ESC_Q_LEN, sizeof(esc_prog_cmd_t));
    esc_prog_gate_init(&s_esc_gate);
    esc_wire_panel_gate(&s_esc_gate);
    s_sample_q  = xQueueCreate(SAMPLE_Q_LEN, sizeof(bench_state_t));
    s_supply_q  = xQueueCreate(SAMPLE_Q_LEN, sizeof(supply_item_t));
    s_enc_q     = xQueueCreate(SAMPLE_Q_LEN, sizeof(servo_test_enc_t));
    s_win_q     = xQueueCreate(SAMPLE_Q_LEN, sizeof(servo_win_item_t));
    s_log_q     = xQueueCreate(LOG_Q_LEN, sizeof(log_row_t));
    s_note_q    = xQueueCreate(LOG_NOTE_Q_LEN, LOG_NOTE_MAX);
    s_console_q = xQueueCreate(CONSOLE_Q_LEN, sizeof(link_report_in_t));
    s_note_mx   = xSemaphoreCreateMutex();
    s_test_q    = xQueueCreate(TEST_Q_LEN, sizeof(test_line_t));
    s_snap_lock = xSemaphoreCreateMutex();
    s_draw_lock = xSemaphoreCreateMutex();
    s_drawpub   = heap_caps_malloc(LINK_DRAW_MAX_BYTES, MALLOC_CAP_SPIRAM);
    s_drawshow  = heap_caps_malloc(LINK_DRAW_MAX_BYTES, MALLOC_CAP_SPIRAM);
    /* Zero is a temperature; the snapshot starts unread, so the strip shows
     * "--" until the control task has published one. */
    s_snap.mcu_temp_c = NAN;
    ESP_ERROR_CHECK((s_touch_q != NULL && s_cmd_q != NULL && s_kst_q != NULL
                     && s_hitec_q != NULL && s_esc_q != NULL
                     && s_sample_q != NULL && s_supply_q != NULL
                     && s_enc_q != NULL && s_win_q != NULL
                     && s_log_q != NULL && s_test_q != NULL
                     && s_note_q != NULL && s_note_mx != NULL
                     && s_console_q != NULL
                     && s_snap_lock != NULL)
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
     * stack holds one line of a servo test, SERVO_TEST_LINE_MAX bytes, and
     * the deepest card chain: the card mounted again under a servo file's
     * walk, as a run's start mounts it (storage_check()).
     * tools/stack_check.py holds 6656 bytes to that chain.
     */
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(log_task, "runlog", 6656,
                                            NULL, 3, NULL, 1) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);

    knob_task_start();

    /* What internal RAM holds with every task of the panel running and the
     * radio off. */
    ESP_LOGI(TAG, "internal RAM: %u B free, largest block %u B; "
             "PSRAM: %u B free",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    task_report();

    panel_frame_loop();   /* does not return */
}
