/**
 * @file panel.h
 * @brief What the panel's files share: firmware/panel/main/main.c and the
 *        files cut from it.  One section per file, each holding what that
 *        file defines and another file uses.  A file's own state stays static
 *        in that file.
 *
 * Nothing in shared/ includes this header.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "art_store.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "arming.h"
#include "stop_latch.h"
#include "bench_state.h"
#include "dial_ctl.h"
#include "log_cols.h"
#include "bind_link.h"
#include "card_tree.h"
#include "busfault_screen.h"
#include "coproc_update_screen.h"
#include "endpoints_link.h"
#include "files_screen.h"
#include "fm_logname.h"
#include "esc_card.h"
#include "esc_link.h"
#include "esc_prog.h"
#include "hitec_link.h"
#include "hitec_prog.h"
#include "hitec_rx_link.h"
#include "kst_link.h"
#include "kst_prog.h"
#include "link_bringup.h"
#include "link_host.h"
#include "link_pages.h"
#include "link_port.h"
#include "link_update_host.h"
#include "log_cadence.h"
#include "log_take.h"
#include "log_writer.h"
#include "log_viewer_screen.h"
#include "motor_screen.h"
#include "out_bind.h"
#include "outputs.h"
#include "prog_link.h"
#include "prog5v_box.h"
#include "prog5v_lost.h"
#include "prog_supply_link.h"
#include "safety_gate.h"
#include "shot_gate.h"
#include "sense_link.h"
#include "sense_map_link.h"
#include "servo_link.h"
#include "servo_page.h"
#include "servo_screen.h"
#include "servo_source.h"
#include "servo_test_io.h"
#include "supply.h"
#include "supply_link.h"
#include "supply_screen.h"
#include "telemetry_sim.h"
#include "tone_link.h"
#include "touch_loss.h"
#include "touch_types.h"
#include "ui_alert_slot.h"
#include "ui_screen.h"

/* ------------------------------------------------------------------ main.c */

/**
 * @brief Milliseconds since boot, from the microsecond timer.
 * @return the time in ms, truncated to 32 bits; wraps every 49.7 days
 */
static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * @brief The panel's throttle, as a channel in an output bank.
 *
 * The ramp is a proportion of travel per second rather than a percentage, so
 * the same number means the same speed on an output whose travel is not
 * measured in percent.
 */
#define PANEL_CH_THROTTLE     0u

/** @brief Capacity of the band alert's text, bytes (UI_ALERT_MAX). */
#define ALERT_MAX         UI_ALERT_MAX

/**
 * @brief The control task's period; see "the two loops' plumbing" in main.c.
 */
#define CONTROL_PERIOD_MS 5u

/** @brief The link to the IOmcu, used during bring-up and in the loop. */
extern link_host_t    s_host;
/** @brief A link_cap_t bitmap from the IOmcu's identity page; see main.c. */
extern atomic_uint    s_capabilities;
extern link_bringup_t s_bring;  /**< Bring-up state of the IOmcu link. */

/* When the link went, 0 while it is up; whether the screen has had its
 * turn; the recoveries of this outage and since boot.  See main.c. */
/** @brief When the link went, now_ms(); 0 while it is up. */
extern atomic_uint s_link_lost_ms;
/** @brief Whether the screen has had its turn for this outage. */
extern bool        s_link_lost_shown;
extern uint32_t    s_recoveries;  /**< Recoveries of this outage. */
extern uint32_t    s_recoveries_total;  /**< Recoveries since boot. */

/* The panel's die temperature, read by the control task. */
extern float s_mcu_c;

/* What the IOmcu says its outputs are, both under s_snap_lock; the
 * state of the last reading, for the control task alone. */
/** @brief What the IOmcu says its outputs are; under s_snap_lock. */
extern bind_reading_t s_outputs_read;
/** @brief Whether s_outputs_read is a reading; under s_snap_lock. */
extern bool           s_outputs_read_fresh;
/**
 * @brief State of the last reading of the output pages; control task only.
 */
extern bind_read_t    s_bind_read;
/* The IOmcu's protocol minor, and its board identity. */
extern uint16_t       s_far_minor;  /**< The IOmcu's protocol minor. */
extern uint16_t       s_board;  /**< The IOmcu's board identity. */

extern QueueHandle_t s_kst_q;     /**< app_main -> control task */
extern QueueHandle_t s_hitec_q;   /**< app_main -> control task */
extern QueueHandle_t s_sample_q;  /**< control task -> app_main */
extern QueueHandle_t s_enc_q;     /**< control task -> app_main */

/**
 * @brief The INA3221's CH1 windows from the ring, each number once and in
 *        order, each with the servo rail's meter and its change count as
 *        decided in the poll that took it (servo_screen_window()).  Full
 *        means the renderer is SAMPLE_Q_LEN windows behind, and the oldest is
 *        dropped.
 */
typedef struct {
    sense_link_win_t  win;  /**< The window of CH1. */
    /** @brief The servo rail's meter when the poll took the window. */
    servo_source_id_t source;
    /** @brief The meter's change count as of the same poll. */
    uint32_t          changes;
    servo_source_why_t dropped;     /**< why it stopped being the meter */
    /** @brief The change count at which it stopped, as supply_item_t's. */
    uint32_t          dropped_at;
} servo_win_item_t;
extern QueueHandle_t     s_win_q;     /**< control task -> app_main */

/**
 * @brief One window of the INA3221 channel in the ESC's supply line, for a
 *        stick run counting its beeps from them (esc_stick_panel_meter()).
 *
 * Each number once and in order.  Full means the renderer is SAMPLE_Q_LEN
 * windows behind, and the oldest is dropped: the run sees the gap in the
 * numbers.
 */
typedef struct {
    uint8_t  channel;           /**< 0 to 11                              */
    uint16_t window;            /**< the window's number                  */
    int32_t  max_ma;            /**< its highest current, mA; INT16_MAX
                                     for a sample at the range's top      */
    bool     current;           /**< it holds current samples             */
} stick_meter_item_t;
extern QueueHandle_t     s_meter_q;   /**< control task -> app_main */
extern SemaphoreHandle_t s_snap_lock; /**< guards s_snap (panel_snap_t) */

/**
 * @brief The IOmcu's update as the render side needs it: an offer, numbered,
 *        with the version the IOmcu runs; while an update runs, its step and
 *        blocks; and the STARTs the control task has taken, so a progress
 *        screen whose START was spent leaves.
 */
typedef struct {
    uint32_t     offer_gen;  /**< Number of the current offer of an update. */
    bool         offered;  /**< Whether an update is on offer. */
    cu_version_t running;  /**< The version the IOmcu runs. */
    bool         active;  /**< Whether an update runs. */
    uint8_t      step;      /**< the update's step, a cu_step_t */
    uint16_t     block;  /**< The block being written. */
    uint16_t     blocks;  /**< Blocks in the image. */
    uint32_t     starts_taken;  /**< STARTs the control task has taken. */
    uint8_t      check;     /**< the last build check, a cu_check_t */
    bool         mismatch;  /**< an IOmcu of another protocol major
                               answers its identity (the link stays down) */
} cu_snap_t;

/**
 * @brief What the screen reads.  Written by the control task, copied by
 *        app_main.
 */
typedef struct {
    bench_state_t bench;  /**< The bench's last reading. */
    supply_state_t supply;  /**< The supply's last reading. */
    uint32_t      supply_gen;  /**< which ON, for the render side's ack */
    bool          link_up;  /**< Whether the IOmcu answers. */
    bool          armed;  /**< Whether the bench is armed. */
    /** The roles the bank is armed for, OUT_ROLE_BIT()s; 0 disarmed. */
    uint8_t       armed_roles;
    bool          arm_wanted;  /**< armed, or an arm waiting: arming_link_needed() */
    uint32_t      arm_gen;     /**< which arm, for the render side's ack */
    /** @brief The stamp of the newest ARM refused (arming_refused_ask()),
     *         for the screen still waiting on it. */
    uint32_t      arm_refused;
    /** @brief The panel's die temperature, degrees Celsius. */
    float         mcu_temp_c;
    bool          stopped;  /**< Whether the stop latch is set. */
    /** @brief arming_held(): an operator's STOP stands, the band's latched
     *         STOP (ui_bench_status_t.stop_latched). */
    bool          held;
    uint32_t      stops;  /**< STOPs counted since boot. */
    uint32_t      pressed;     /**< of the stops, those pressed */
    uint16_t      faults;      /**< STATUS faults as the band shows them */
    uint32_t      link_errors; /**< the IOmcu's CRC errors and resyncs */
    uint32_t      link_refused; /**< ui_bench_status_t's link_refused */
    uint32_t      run_seconds; /**< arming_run_seconds() */
    /**
     * @brief The control task's alert, numbered: an alert posted and replaced
     *        before a frame took it was never shown (sense_link_alerts()),
     *        and one whose cause is over is taken back
     *        (control_alert_withdraw()).
     */
    ui_alert_slot_t alert;
    /**
     * @brief The phase tap as the control task last read it, for the stick
     *        run's page.
     */
    tone_readout_t tone;
    /**
     * @brief The servo rail's meter as of the last poll (servo_source.h), and
     *        the CH1 windows that left the ring untaken since boot.
     */
    servo_source_id_t servo_source;
    /** @brief Changes of the servo rail's meter since boot. */
    uint32_t      servo_source_changes;
    /**
     * @brief Why the INA3221 is not the meter, and why and when it last
     *        stopped being it (servo_source_why(), servo_source_dropped()).
     */
    servo_source_why_t servo_source_why;
    /** @brief Why the INA3221 last stopped being the meter. */
    servo_source_why_t servo_source_dropped;
    /** @brief The meter's change count at that stop. */
    uint32_t      servo_source_dropped_at;
    /** @brief CH1 windows that left the ring untaken since boot. */
    uint32_t      servo_win_lost;
    /** @brief What the servo programmer's model is fed; see kst_publish(). */
    kst_prog_feed_t kst;
    /** @brief What the Hitec programmer's model is fed; see
     *         hitec_publish(). */
    hitec_prog_feed_t hitec;
    /** @brief What the ESC programmer's model is fed; see esc_publish(). */
    esc_prog_feed_t esc;
    /** @brief The IOmcu's update; see cu_service(). */
    cu_snap_t     cu;
} panel_snap_t;
/**
 * @brief The snapshot the control task writes and app_main copies, under
 *        s_snap_lock.
 */
extern panel_snap_t s_snap;

/** @brief Take s_snap_lock, waiting as long as it takes. */
static inline void snap_lock(void)   { xSemaphoreTake(s_snap_lock, portMAX_DELAY); }
/** @brief Give s_snap_lock back. */
static inline void snap_unlock(void) { xSemaphoreGive(s_snap_lock); }

/**
 * @brief Called from the control task, which must not touch the router.  The
 *        slot holds one alert; a later one replaces it.  Returns its number.
 * @param text the alert's text
 * @return the alert's number in the slot, never 0
 */
uint32_t control_alert_numbered(const char *text);

/**
 * @brief The canvas the next frame is drawn on (display_canvas()), after
 *        what Screen rotation asks of the UI: a whole repaint when the
 *        canvas changed between a logical buffer and a frame buffer, and
 *        the band's alert when 180° fell back to NORMAL.  Every loop that
 *        renders takes its canvas here, on the task that flips
 *        (panel_frame.c).
 * @return the canvas
 */
gfx_canvas_t *panel_canvas(void);

/**
 * @brief Take back alert number @p gen, whose cause is over: one not yet
 *        taken by a frame is dropped, and the last one a frame took is
 *        cleared from the band if it is still showing
 *        (ui_router_withdraw_alert()).  An older alert is left alone, so
 *        withdrawals of several causes in one frame do not hide each other
 *        (ui_alert_slot.h).  Control task only.
 * @param gen the number control_alert_numbered() returned
 */
void control_alert_withdraw(uint32_t gen);

/**
 * @brief Post an alert as control_alert_numbered() does, for callers that do
 *        not follow it.
 *
 * Inlined where it is called: a frame of its own puts 32 bytes on the control
 * task's deepest chain (drv_lost() through control_pump()) and moves
 * stack_check's walk onto a deeper path through ESP-IDF's logging.
 * @param text the alert's text
 */
__attribute__((always_inline)) static inline void
control_alert(const char *text)
{
    snap_lock();
    snprintf(s_snap.alert.text, sizeof(s_snap.alert.text), "%s", text);
    s_snap.alert.pending = true;
    /* ui_alert_slot_post(), written out for the same reason. */
    ++s_snap.alert.posted;
    if (s_snap.alert.posted == 0u) {
        s_snap.alert.posted = 1u;
    }
    snap_unlock();
}

/**
 * @brief control_alert(), and the number the slot gave it: the alert a
 *        caller takes back once its cause is over
 *        (ui_alert_slot_withdraw_over()).  Inlined for the same reason; the
 *        control task is the slot's only writer, so the number is read after
 *        the lock.
 * @param text the alert
 * @return its number in the slot, never 0
 */
__attribute__((always_inline)) static inline uint32_t
control_alert_kept(const char *text)
{
    control_alert(text);
    return s_snap.alert.posted;
}

/* The safety loop's own state and the arming policy; see main.c. */
extern outputs_t s_out;  /**< The output bank. */
extern arming_t  s_arm;  /**< The arming policy's state. */
extern uint16_t  s_throttle_hundredths;   /**< the throttle command, 0 to LINK_THROTTLE_MAX */
/**
 * @brief A disarm asked for; set by the render side, taken by the control
 *        task.
 */
extern atomic_bool s_disarm_request;
/** @brief The reset of a latched STOP (stop_latch.h); control task only.
 *         See panel_stop.c. */
extern stop_latch_t s_stop_latch;
/** @brief The number of the alert ALERT_STOP_HELD that an ARM or an ON
 *         refused while the STOP stands last posted, 0 for none: taken
 *         back when the reset completes (stop_reset_service()).  Control
 *         task only. */
extern uint32_t     s_stop_alert;
/** @brief The reset's hold completed (stop_hold_take_reset()): render side
 *         sets, control task takes (stop_reset_service()). */
extern atomic_bool  s_stop_reset_request;
/** @brief When the render side took the completed hold, now_ms(); stored
 *         before s_stop_reset_request. */
extern atomic_uint  s_stop_reset_ms;
/** @brief Contact breaks counted (stop_touch_watch()): control task counts,
 *         the render side ends the reset's hold at each. */
extern atomic_uint  s_touch_breaks;
/** @brief The reset's machine at the start: unlatched (stop_latch_init()).
 *         Control task. */
void stop_reset_init(void);
/** @brief One transition of the reset's machine (stop_latch_run()), once a
 *         pass after the stops are served.  Control task.
 *  @param link_up whether the link to the IOmcu answers */
void stop_reset_service(bool link_up);
/** @brief An operator's STOP, applied now: arming_stop_pressed(), with its
 *         time kept for the reset.  Control task only. */
void stop_pressed_here(void);
/** @brief Counts a contact break when the touch controller has gone
 *         unanswered for STOP_LATCH_FRESH_MS (100ms).  Control task, at the
 *         rate touch is judged. */
void stop_touch_watch(void);

/**
 * @brief A disarm returns the command to zero, here and in the output bank.
 *
 * The throttle used to survive a disarm, so the next arm wrote the previous
 * position to the control page in the same transaction that armed: the bench
 * went from stopped to whatever it was last set to, with the ramp on the far
 * side of the arm rather than in front of it.  An arm starts from nothing.
 */
static inline void throttle_to_zero(void)
{
    s_throttle_hundredths = 0u;
    (void)outputs_set(&s_out, PANEL_CH_THROTTLE, 0u, now_ms());
}

/* The PD mini's input in mV and its UART rate in baud, as its page last
 * read; 0 while not known. */
/**
 * @brief The PD mini's input, mV, as its page last read; 0 while not known.
 */
extern atomic_uint s_supply_vin_mv;
/**
 * @brief The PD mini's UART rate, baud, as the IOmcu last reported it; 0
 *        while not known.
 */
extern atomic_uint s_supply_baud;

/** @brief Which arm, for the render side's ack. */
extern uint32_t    s_arm_gen;

/* The pole count the IOmcu holds, and whether it is current. */
/**
 * @brief Whether the IOmcu's pole count is not current and a write is owed.
 */
extern atomic_bool s_poles_owed;
extern atomic_uint s_poles_value;  /**< The pole count the IOmcu is to hold. */

/* The throttle endpoints the IOmcu holds (endpoints_link.h): owed, held
 * disarmed at a link-up, the pair and the time of the last edit.  The debt
 * is paid once the pair has rested ENDPOINTS_SETTLE_MS, and a switch to
 * ARMED with the link up is refused while it stands. */
extern endpoints_debt_t s_endpoints;

/**
 * @brief Where the PD mini is wired, from SUPPLY's PD MINI tab, in one word;
 *        see main.c.
 */
extern atomic_uint s_pdmini_wiring;

/**
 * @brief Unpack a PD mini wiring word (s_pdmini_wiring).
 * @param word bit 24 enabled, bits 23 to 16 the TX GPIO plus 1, bits 15 to 8
 *        the RX GPIO plus 1, bits 7 to 0 the baud setting
 * @return the wiring; a GPIO of -1 is unset
 */
static inline supply_wiring_t wiring_of(unsigned word)
{
    const supply_wiring_t w = {
        .en   = (word & (1u << 24)) != 0u,
        .tx   = (int8_t)((int)((word >> 16) & 0xFFu) - 1),
        .rx   = (int8_t)((int)((word >> 8) & 0xFFu) - 1),
        .baud = (uint8_t)(word & 0xFFu),
    };
    return w;
}

/**
 * @brief The PD mini's wiring as SUPPLY's PD MINI tab left it.
 * @return the unpacked s_pdmini_wiring
 */
static inline supply_wiring_t pdmini_wiring(void)
{
    return wiring_of(atomic_load(&s_pdmini_wiring));
}

/* What SETUP > SENSORS left for the current monitors
 * and the phase tap, read by the control task; panel_sense.c publishes the
 * monitors' from the settings observer. */
/**
 * @brief Take the current monitors' set-up from the settings for the control
 *        task; app_main, from the settings observer.
 */
void            publish_sense(void);
/**
 * @brief The current monitors' set-up SETUP > SENSORS left.
 * @return the set-up
 */
sense_setup_t   sense_wanted(void);
tone_setup_t    tone_wanted(void);

/** @brief False until the control task owns the safety state. */
extern bool s_pump_live;
/* The safety loop, from the top of the control task and from inside every
 * exchange. */
void control_pump(void);


/* The GPIOs of the slots of the channels the SERVO screen's horn drives
 * (servo_link_binding()), and whether a binding was read; see main.c. */
/**
 * @brief GPIOs of the slots the SERVO screen's horn drives, packed in one word
 *        (servo_link_binding()).
 */
extern atomic_ullong s_servo_pins;
extern atomic_bool   s_servo_pins_known;  /**< Whether a binding was read. */
/**
 * @brief For the SERVO screen's runs: the SENSE page holds the encoder
 *        enabled.
 */
extern atomic_bool   s_enc_held;

/**
 * @brief Whether a run's log or a servo run's files are open or about to be.
 * @return true while one is (panel_update.c)
 */
bool panel_run_open(void);

/**
 * @brief Whether an update runs: the IOmcu's, the panel's install or its
 *        download.  Render task.
 * @return true while one does (panel_update.c)
 */
bool panel_update_busy(void);

/**
 * @brief Whether the IOmcu's update runs, or the BUY of an image on trial.
 *        Any task.
 * @return true while one does (panel_update.c)
 */
bool panel_cu_busy(void);

/**
 * @brief Whether the panel's own download or install runs.  Render task.
 * @return true from its hold until it has failed (panel_update.c)
 */
bool panel_fw_busy(void);

/* ------------------------------------------------------------- panel_c6.c */

/**
 * @brief SETUP > UPDATES' C6 RADIO row and the C6 radio's update, once a
 *        frame on app_main, after upd_service().
 * @param armed ARMED, or an arm waiting
 */
void c6_service(bool armed);

/**
 * @brief Whether the C6 radio's update runs, a step of it is still in a
 *        call, or its restart is due: Wi-Fi does not start, ARM is refused
 *        and the other updates wait.  Any task.
 * @return true while one of these holds
 */
bool panel_c6_busy(void);

/* ----------------------------------------------------------- panel_files.c */

/** @brief FILES' way to the card: its requests run on the task cardjob. */
extern const files_io_t k_files_io;

/**
 * @brief The card as FILES' core sees it (fm_io.h), for the Wi-Fi page's
 *        file API, which calls it from the page server's task.
 * @return the card's table of functions
 */
const fm_io_t *panel_files_card(void);

/**
 * @brief Make the card's job lock, which FILES' task and the Wi-Fi page's
 *        writes share; once at start-up, again from FILES' first request
 *        if it failed then.
 * @return false with no RAM for it: FILES does not start, the page's
 *         writes answer PANEL_BUSY
 */
bool panel_files_lock_init(void);

/**
 * @brief Take the card's job lock for one write of the Wi-Fi page.
 * @param wait_ms  how long to wait for a FILES job to end
 * @return false when FILES held it that long, or no lock was made
 */
bool panel_files_lock(uint32_t wait_ms);

/**
 * @brief Give the card's job lock back after panel_files_lock().
 * @param wrote  the write changed the card: the logs' numbering is read
 *               again at the next run
 */
void panel_files_unlock(bool wrote);

/**
 * @brief The bench now: ARMED or an arm asked, the IOmcu link and the
 *        logger's open files, into a task's own marks.
 * @param out       takes the bench
 * @param marks     takes the open files; outlives the bench's use
 * @param run_dir   takes their run's folder, CARD_DIR_MAX bytes
 * @param test_dir  takes their test's folder, CARD_DIR_MAX bytes
 */
void panel_files_bench(fm_bench_t *out, log_marks_t *marks, char *run_dir,
                       char *test_dir);

/**
 * @brief The bench for the page server's task: ARMED, the IOmcu link and
 *        the logger's open files now, into marks of that task's own.
 * @param out  takes the bench
 * @param ctx  unused
 */
void panel_files_web_bench(fm_bench_t *out, void *ctx);

/**
 * @brief Give the Wi-Fi page's file manager the words, the span cap and
 *        the colours the text viewer classes and draws a line with
 *        (wifi_access_set_hl()), once at start-up.
 */
void panel_files_web_hl(void);

/* ------------------------------------------------------------ panel_card.c */

/** @brief Read once at start-up, from bring_up(). */
void esc_profiles_load(void);
/** @brief The log viewer's way to the card. */
extern const log_viewer_io_t k_card_io;

/* The runs the logger has open, held back from the viewer: the automatic
 * log's in AUTO LOG FOLDER, a test's CSV and report in USER LOG FOLDER,
 * each in the folder it was opened in (log_open_marks()). */
/**
 * @brief The run of the automatic log open, in AUTO LOG FOLDER; 0 for none.
 */
extern atomic_uint s_log_open_run;
/** @brief The run of a test's CSV open, in USER LOG FOLDER; 0 for none. */
extern atomic_uint s_test_open_run;
/** @brief The run of a test's report open, in USER LOG FOLDER; 0 for none. */
extern atomic_uint s_test_open_txt;
/* The files the logger has created since the start, and the LOGS tab of
 * the last one (log_viewer_tab_t): stored by the logger, the tab first. */
/** @brief Files the logger has created since the start. */
extern atomic_uint s_card_written;
/** @brief The LOGS tab of the last file created (log_viewer_tab_t). */
extern atomic_int  s_card_written_tab;

/**
 * @brief One level of a folder of the card (card_make_fn, card_tree.h): the
 *        logs', the fault log's and the servo files' folders are made with
 *        it.
 * @param ctx unused
 * @param level the folder to make, a path from the card's root
 * @return the result of making the folder; CARD_DIR_OK for one that exists
 */
card_dir_t panel_card_make_dir(void *ctx, const char *level);

/* The programmer's record, KST.TXT: filled by the render loop, written by
 * the logger. */
/** @brief The record's text, filled by the render loop. */
extern char        s_kst_file[KST_PROG_FILE_MAX];
extern size_t      s_kst_file_len;  /**< Bytes of s_kst_file to write. */
/* The mount whose record the model holds (storage_mount_count()): the
 * record is written to that volume or not at all. */
/** @brief The mount whose record the model holds (storage_mount_count()). */
extern unsigned    s_kst_file_mount;
/** @brief Records the render loop handed over. */
extern atomic_uint s_kst_file_sent;
/** @brief Records the logger has written or given up on. */
extern atomic_uint s_kst_file_done;
/** @brief Whether the card took the last record whole. */
extern atomic_bool s_kst_file_ok;
/**
 * @brief Write the record the render loop handed over, on the logger task;
 *        nothing when none waits.
 */
void kst_record_write(void);
/**
 * @brief Read KST.TXT from the card into the servo programmer's model.
 * @param late true for a card mounted after start-up, where a record the
 *        model holds already stands; false at start-up
 */
void kst_record_load(bool late);

/* The Hitec programmer's record, HITEC.TXT (hitec_record.c): filled by
 * the render loop, written by the logger, read at the start. */
/** @brief The record's text, as hitec_card_text() writes it. */
extern char        s_hitec_file[HITEC_CARD_TEXT_MAX];
/** @brief The bytes of s_hitec_file to write; 0 removes the record. */
extern size_t      s_hitec_file_len;
/** @brief The mount the text is written to (storage_mount_count()). */
extern unsigned    s_hitec_file_mount;
/** @brief Texts the render loop handed over; one ahead of
 *         s_hitec_file_done while one waits. */
extern atomic_uint s_hitec_file_sent;
/** @brief Texts the logger has written or given up on. */
extern atomic_uint s_hitec_file_done;
/** @brief Whether the card took the last text whole. */
extern atomic_bool s_hitec_file_ok;
/**
 * @brief Write the text the render loop handed over, on the logger task:
 *        HITEC.NEW, then its name HITEC.TXT.  Nothing when none waits.
 */
void hitec_record_write(void);
/**
 * @brief Read the record from the card into the Hitec programmer, once,
 *        at the start before the tasks run.
 */
void hitec_record_load(void);

/* The Hitec programmer's servo file request (hitec_files.c): taken from
 * the model by the render loop, run on the card by the logger. */
/** @brief The request. */
extern hitec_card_req_t  s_hitec_card_req;
/** @brief The mount it was taken under (storage_mount_count()): it is run
 *         on that volume or finds no card. */
extern unsigned          s_hitec_card_mount;
/** @brief The answer. */
extern hitec_card_done_t s_hitec_card_done;
/** @brief Requests the render loop handed over. */
extern atomic_uint       s_hitec_card_sent;
/** @brief Requests the logger answered. */
extern atomic_uint       s_hitec_card_answered;
/**
 * @brief Run the request handed over on the logger task.  Nothing when
 *        none waits.
 */
void hitec_card_serve(void);

/* A servo file request: taken from the model by the render loop, run on
 * the card by the logger. */
extern kst_prog_card_req_t  s_kst_card_req;  /**< The servo file request. */
/* The mount the request was taken under (storage_mount_count()): it is
 * run on that volume or finds no card. */
/** @brief The mount the request was taken under (storage_mount_count()). */
extern unsigned             s_kst_card_mount;
extern kst_prog_card_done_t s_kst_card_done;  /**< The answer. */
/** @brief Requests the render loop handed over. */
extern atomic_uint          s_kst_card_sent;
/** @brief Requests the logger answered. */
extern atomic_uint          s_kst_card_answered;
/**
 * @brief Run the servo file request handed over, on the logger task; nothing
 *        when none waits.
 */
void kst_card_serve(void);

/* ------------------------------------------------------------- panel_log.c */

/* A run is one arming of the bench, or one switch-on of the supply's output
 * while the bench is not armed; log_run_kind_t and the rule of which run
 * the log records are in log_cadence.h.  One run is one file, and a file
 * holds one kind of row, so its header names every column it has.  All
 * runs are written to AUTO LOG FOLDER, none while AUTO LOGS is off. */

/**
 * @brief The two folders SETUP > SD CARD names, AUTO LOG FOLDER and USER LOG
 *        FOLDER.
 */
typedef enum {
    LOG_FOLDER_AUTO = 0,
    LOG_FOLDER_USER,
    LOG_FOLDERS
} log_folder_t;

/**
 * @brief The folder the next file of @p kind is written to: one
 *        card_folder_check() takes (card_folder_use()).  Called by the text
 *        observer on app_main; the logger reads it at each take, so a file
 *        open stays where it is and the next one is numbered in the new
 *        folder.
 * @param kind which folder the setting names
 * @param dir a folder card_folder_check() takes, a path from the card's root
 */
void log_set_folder(log_folder_t kind, const char *dir);

/**
 * @brief The open markers and the folder each was opened in, read as one, for
 *        the viewer's card_busy(): @p run_dir and @p test_dir take
 *        CARD_DIR_MAX bytes and @p marks points into them.
 * @param marks takes the open markers; points into @p run_dir and @p test_dir
 * @param run_dir takes the folder the automatic log's run was opened in,
 *        CARD_DIR_MAX bytes
 * @param test_dir takes the folder a test's files were opened in,
 *        CARD_DIR_MAX bytes
 */
void log_open_marks(log_marks_t *marks, char *run_dir, char *test_dir);

/**
 * @brief The MOTOR run log the logger closed last since power-on: its
 *        folder, its name now and the mount it was written on; name "" for
 *        none.  Any task.
 * @param out  takes the record
 */
void log_last_motor(fm_logname_t *out);

/**
 * @brief The MOTOR log closed last carries @p name now: RENAME LOG renamed
 *        it.  The logger task's.
 * @param name  its new name
 */
void log_last_motor_named(const char *name);

/**
 * @brief RENAME LOG's request, run on the logger task when one waits: the
 *        MOTOR log closed last renamed under the card's job lock
 *        (panel_log_rename.c).
 */
void log_rename_serve(void);

/**
 * @brief RENAME LOG in the render loop: the record handed to MOTOR & ESC,
 *        its request handed to the logger and the answer back.  No card
 *        call is made here.
 */
void motor_log_ui_service(void);

/**
 * @brief Whether the logger may write a fault log line now: one queued in
 *        s_note_q, or under way.  The file manager's bench reads it
 *        (fm_bench_t.fault_busy).
 * @return true while a line waits or is written
 */
bool log_fault_busy(void);

/**
 * @brief Take the fault log's lock, which the logger holds across each
 *        line's open, write and close.  The file manager holds it across an
 *        unlink and a rename: FatFs runs without its file lock
 *        (CONFIG_FATFS_FS_LOCK 0) and removes a file open for append.
 *        Neither holder takes another lock under it.
 */
void log_fault_lock(void);

/** @brief Give the fault log's lock back. */
void log_fault_unlock(void);

/**
 * @brief One sample on its way to the card, filled by the control task.
 *
 * It carries which run it belongs to, because that is the only thing that
 * says which file it goes in: a disarm and an arm inside one pass of the
 * logger would otherwise put two runs in one file, with a time column that
 * goes backwards halfway down.
 */
typedef struct {
    uint32_t       run;      /**< the run the row was sampled in */
    float          t_s;      /**< the row's time since the run's start, s */
    log_run_kind_t kind;     /**< which of the two below: SUPPLY's or the
                                  bench's, for SERVO and MOTOR */
    union {
        bench_state_t  bench;   /**< a SERVO or MOTOR run's sample */
        supply_state_t supply;  /**< a supply run's sample */
    } u;                     /**< the sample, by kind */
    /** @brief The supply's reading and the run's totals of it, on a SERVO
     *         run's row that carries the sample; all 0 on any other row. */
    log_rail_t     rail;
    /** @brief The pulse and angle, on the same rows as rail. */
    log_servo_t    servo;
} log_row_t;

/**
 * @brief Sixty-four rows is 3.2s of run at PANEL_SAMPLE_HZ, against the 250ms
 *        an SD card may take over one write: a card that pauses costs no
 *        rows.
 */
#define LOG_Q_LEN      64

/* The report link_report() writes to the card, and how many may be in
 * flight.  The report is made every 5s at its fastest. */
/** @brief Capacity of one report link_report() writes to the card, bytes. */
#define LOG_NOTE_MAX   208
/** @brief Reports in flight, the depth of the queue to the logger. */
#define LOG_NOTE_Q_LEN 4

/** @brief The console reports link_report() hands the logger to print,
 *         10s of them at the 5s cadence: up to 7 lines of up to 167
 *         characters each, 50ms and more on the UART (universal
 *         asynchronous receiver-transmitter) console, which would
 *         otherwise stall the task that beats the safety line.  A full
 *         queue is printed where it is made: no line is dropped. */
#define CONSOLE_Q_LEN 2

/** @brief One line of a servo test's files, render loop -> logger. */
typedef struct {
    uint8_t kind;                        /**< a servo_test_out_t */
    uint8_t prefix;                      /**< an OPEN's log_prefix_t */
    char    text[SERVO_TEST_LINE_MAX];  /**< The line's text. */
} test_line_t;

/**
 * @brief Depth of the queue of a servo test's lines, render loop to logger.
 */
#define TEST_Q_LEN 24

extern QueueHandle_t s_log_q;
extern QueueHandle_t s_note_q;
/** @brief control task -> logger: link_report_in_t, CONSOLE_Q_LEN deep. */
extern QueueHandle_t s_console_q;
/** @brief The logger prints every console report waiting (panel_log.c). */
void console_reports_print(void);
/** @brief Print one console report on the calling task (panel_link.c). */
void console_report_print(const link_report_in_t *r);
/** @brief The fault log's lock (log_fault_lock()). */
extern SemaphoreHandle_t s_note_mx;
extern QueueHandle_t s_test_q;
/**
 * @brief The run being recorded, or 0: the control task's level, which the
 *        logger follows.
 */
extern atomic_uint s_log_run_now;

/* A servo test's OPENs and ENDs, queued by the render loop and taken by
 * the logger, and what the logger made of them. */
/** @brief OPENs of a servo test's files the render loop queued. */
extern atomic_uint s_test_opens_sent;
/** @brief OPENs the logger has answered with a number in s_test_file. */
extern atomic_uint s_test_opens_done;
/** @brief ENDs of a servo test the render loop queued. */
extern unsigned    s_test_ends_sent;
extern atomic_uint s_test_ends_done;  /**< ENDs the logger has taken. */
/** @brief The test file's number, -1 when it is not recorded. */
extern atomic_int  s_test_file;
/**
 * @brief Whether the last run's report reached the card whole; set at its
 *        END.
 */
extern atomic_bool s_test_report;

/* The runlog task: every fopen, fwrite, fsync and fclose on the card. */
void log_task(void *arg);

/* main.c: the control task's side of the log, which numbers the runs and
 * posts the rows. */
/** @brief The run's row counts and cadence; control task. */
extern log_cadence_t  s_log_cad;
/** @brief Which kind of run started last; control task. */
extern log_run_kind_t s_log_kind;
/**
 * @brief An automatic servo test, a limit run or AUTO on RUDDER runs; stored
 *        by the SERVO screen.
 */
extern atomic_bool    s_servo_testing;
/**
 * @brief Queue one row for the logger without waiting; a full queue drops it.
 * @param row the row; its run is set here
 */
void log_post(log_row_t *row);

/* ------------------------------------------------------------ panel_link.c */

/* Exchanges that ended unanswered; a far end held to be answering; an
 * exchange that ended unanswered with the link up.  See panel_link.c. */
extern uint32_t s_unanswered;  /**< Exchanges that ended unanswered. */
/** @brief Whether a far end is held to be answering. */
extern bool     s_link_is_up;
/** @brief Set by an exchange that ended unanswered with the link up. */
extern bool     s_link_quiet;

/**
 * @brief Put a built request on the wire and wait for its answer.
 * @param host the link host state
 * @param req the built request
 * @param reply takes the answer
 * @return true when an answer arrived, also a refusal; false when the
 *         exchange ended unanswered
 */
bool exchange(link_host_t *host, const link_msg_t *req, link_msg_t *reply);


/**
 * @brief The clock, as shared/link asks for it.
 * @param ctx Unused.
 * @return now_ms().
 */
uint32_t port_now(void *ctx);
/**
 * @brief Between two exchanges of one sequence: the control pump, at most
 *        every 5ms while it is live.
 * @param ctx Unused.
 */
void     port_between(void *ctx);
/** @brief exchange(), port_now() and port_between(), as shared/link asks for
 *         them. */
extern const link_port_t k_port;

/**
 * @brief Read @p count registers of @p page from @p offset over the link.
 *
 * Through link_read_split(): a read asks for at most LINK_READ_REGS_MAX (16)
 * registers an exchange from an IOmcu of 4.35 or later, so a reply of 4
 * frames fits the TWAI controller's receive FIFO and an interrupt held while
 * it arrives delays it and cannot overrun the FIFO.  A wider read is two
 * exchanges and one state of the page (LINK_OP_READ_HELD).
 *
 * @param host   The link host state.
 * @param page   Page to read.
 * @param offset First register.
 * @param count  Registers, at most LINK_MAX_REGS (32).
 * @param reply  Out: the answer, data or a refusal.
 * @return False when an exchange got no answer or @p count exceeds
 *         LINK_MAX_REGS; true otherwise.
 */
static inline bool read_regs(link_host_t *host, uint8_t page, uint8_t offset,
                             uint8_t count, link_msg_t *reply)
{
    return link_read_split(host, &k_port, page, offset, count, reply);
}

/**
 * @brief Read the first @p count registers of @p page, as read_regs().
 *
 * @param host  The link host state.
 * @param page  Page to read.
 * @param count Registers, at most LINK_MAX_REGS (32).
 * @param reply Out: the answer, data or a refusal.
 * @return False when an exchange got no answer; true otherwise.
 */
static inline bool poll_page(link_host_t *host, uint8_t page, uint8_t count,
                             link_msg_t *reply)
{
    return read_regs(host, page, 0u, count, reply);
}

/**
 * @brief A write of any width goes out one frame per exchange, each
 *        acknowledged before the next is sent (link_write_acked()): the far
 *        end's controller buffers 2 frames and is polled, so frames sent back
 *        to back can find it full.  A write of up to 4 registers is one
 *        exchange.
 * @param host the link host state
 * @param page page to write
 * @param offset first register
 * @param count registers, at most LINK_MAX_REGS (32)
 * @param regs the values
 * @param reply takes the ACK or the first frame's NACK
 * @return true when every frame sent was answered; false when one ended
 *         unanswered
 */
static inline bool write_regs(link_host_t *host, uint8_t page, uint8_t offset,
                              uint8_t count, const uint16_t *regs,
                              link_msg_t *reply)
{
    return link_write_acked(host, &k_port, page, offset, count, regs, reply);
}

/**
 * @brief The control page: ARM and THROTTLE, the frame that arms, CLEAR, the
 *        pole count and the throttle's endpoints.  See panel_link.c.
 */
extern uint16_t s_arm_value;

/**
 * @brief A control page write of ARM @p arm and s_throttle_hundredths, and
 *        whether the IOmcu acknowledged it, for the log's throttle
 *        (bench_throttle_written()).  Control task only.
 * @param acked true when the IOmcu acknowledged the write
 * @param arm   the ARM written
 */
void control_throttle_written(bool acked, uint16_t arm);

/**
 * @brief ARM and THROTTLE, the write every 50ms poll and every disarm makes.
 *
 * Two registers rather than the three the frame that arms carries.  This
 * write keeps a running bench running and stops a stopping one, and the
 * pole count is neither: while a run is on, an edit to it is a correction
 * paid on its own transaction by poles_service(), and a disarm has nothing
 * to convert.  Kept apart so a count the far end refuses -- or one that has
 * not changed -- never stands between the throttle and the wire.
 * @param armed whether the bench is armed
 * @param reply takes the answer
 * @return true when the IOmcu acknowledged; a NACK or no answer is false
 */
static inline bool control_write(bool armed, link_msg_t *reply)
{
    /* The value the far end was armed with, written again: a different
     * non-zero ARM is refused while it is armed (LINK_MINOR_ARM_ROLE). */
    const uint16_t regs[2] = { armed ? s_arm_value : 0u,
                               s_throttle_hundredths };
    const bool ok = write_regs(&s_host, LINK_PAGE_CONTROL, LINK_CT_ARM, 2u,
                               regs, reply)
                    && reply->op == LINK_OP_ACK;
    control_throttle_written(ok, regs[0]);
    return ok;
}

/**
 * @brief Write the frame that arms: ARM, THROTTLE and MOTOR_POLES in one
 *        frame.
 * @param reply takes the answer
 * @return true when the IOmcu acknowledged
 */
bool control_arm(link_msg_t *reply);
/**
 * @brief Pay the pole count's debt when one is owed.
 * @return true when the far end holds the current count
 */
bool poles_service(void);
/**
 * @brief Pay the throttle endpoints' debt when one is owed
 *        (endpoints_link.h).
 * @param far_disarmed whether the far end reads disarmed
 */
void endpoints_service(bool far_disarmed);

/**
 * @brief Whether the far end trusts the heartbeat, asked of its STATUS page.
 *
 * An arm waits for this before CLEAR is written.  The heartbeat is withheld
 * while a stop is latched, and after it resumes the far end's monitor needs
 * five edges, 100 to 125ms at a 5ms pass and longer on a slower one: a
 * fixed wait is right for one pass period and refused at the next.  One
 * register, one frame each way.
 *
 * @p answered is false when nobody answered.  That exchange has then waited
 * LINK_HOST_TIMEOUT_MS (1000ms), past the arm's whole bound, and it is the
 * link that is gone and not the line that is distrusted.  @p faults, where
 * not NULL, takes the fault bitmap the answer carried, and is left as it is
 * without one: the alert of an arm given up names its cause from it.
 * @param answered takes whether anybody answered
 * @param faults   takes the fault bitmap of a DATA answer; may be NULL
 * @return true when the answer clears the heartbeat bit
 */
static inline bool far_line_trusted(bool *answered, uint16_t *faults)
{
    link_msg_t st = { 0 };
    *answered = read_regs(&s_host, LINK_PAGE_STATUS, LINK_ST_FAULTS, 1u, &st);
    const bool data = *answered && st.op == LINK_OP_DATA;
    if (data && faults != NULL) {
        *faults = st.regs[0];
    }
    return data && safety_gate_line_trusted(st.regs[0]);
}

/**
 * @brief Finish at this end a stop the policy latched for the far end: bank,
 *        throttle, servo pin and alert.
 */
void far_end_stop_here(void);
/**
 * @brief Handle an arm whose write was refused or went unanswered.
 * @param quiet_before s_unanswered as the arm began
 * @param alert the alert for a refusal
 */
void arm_write_failed(uint32_t quiet_before, const char *alert);

/**
 * @brief Write CLEAR with LINK_CLEAR_MAGIC to the control page, lifting a
 *        latched failsafe.
 * @param reply takes the answer
 * @return true when the IOmcu acknowledged
 */
static inline bool control_clear_failsafe(link_msg_t *reply)
{
    const uint16_t magic = LINK_CLEAR_MAGIC;
    return write_regs(&s_host, LINK_PAGE_CONTROL, LINK_CT_CLEAR, 1u, &magic,
                      reply)
           && reply->op == LINK_OP_ACK;
}

/**
 * @brief What the panel knows about a link that stopped; the link report,
 *        every 5s while the link is down and every 60s while it is up, handed
 *        to the logger to print (console_reports_print()).
 * @param r takes the report
 */
void link_lost_report(busfault_report_t *r);
void link_report(void);

/**
 * @brief The seam between measured and modelled numbers.  When the IOmcu
 *        answers, its numbers are used, and the LINK_BN_SIMULATED flag
 *        travels with them.  When it does not, the panel models locally and
 *        sets the same flag. Nothing above this function knows the
 *        difference.
 * @param host the link host state
 * @param out takes the reading
 * @return true when the far end answered with data; false for no answer or a
 *         NACK
 */
static inline bool read_bench(link_host_t *host, bench_state_t *out)
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

/* -------------------------------------------------------- panel_services.c */

/* The link modules, SENSE (4.7), TONE (4.8), KST (4.12) and PROG (4.13),
 * and the servo rail's meter.  Control task only. */
extern sense_link_t   s_sense_link;  /**< The SENSE page's module (4.7). */
extern servo_source_t s_servo_source;  /**< The servo rail's meter. */
/** @brief The TONE page's module (4.8), the phase tap. */
extern tone_link_t    s_tone_link;
extern kst_link_t     s_kst_link;  /**< The KST page's module (4.12). */
extern prog_link_t    s_prog_link;  /**< The PROG page's module (4.13). */
/* What the SERVO screen wants of the PROG page, and the pin of port 0. */
/** @brief The PROG target the SERVO screen wants. */
extern atomic_uint    s_prog_want_target;
/** @brief Whether the SERVO screen wants PWM on the PROG port. */
extern atomic_bool    s_prog_want_pwm;
extern atomic_uint    s_prog_pin;  /**< The pin of PROG port 0. */
/* The ports the PROG page reports (prog_link_count(), 0xFF while not
 * known) in bits 0 to 7, and the pins of ports 1, 2 and 3 in bits 8 to 15,
 * 16 to 23 and 24 to 31, for the OUTPUTS screen's PROG PORT entries and
 * the pin picker; one word, so the screens never read one page's count
 * with another's pins. */
/**
 * @brief No ports reported: the PROG page's count is unknown and no pin is
 *        set.
 */
#define PROG_PORTS_NONE 0xFFFFFFFFu
/**
 * @brief The ports the PROG page reports in bits 0 to 7 and the pins of ports
 *        1 to 3 in bits 8 to 31.
 */
extern atomic_uint    s_prog_ports;
/**
 * @brief Publish the PROG page's ports to the screens' mail box; control
 *        task.
 */
void prog_ports_publish(void);
/** @brief Tell the mail box the PROG page is gone; control task. */
void prog_ports_lost(void);
/**
 * @brief Hand the screens the hook they post their asks with; render loop.
 */
void prog_ports_init_screens(void);
/**
 * @brief Hand the screens the ports' view and the PROGRAM list's ports;
 *        render loop.
 */
void prog_ports_to_screens(void);
/* Where the tester's commands go, for the SERVO screen's TARGET row:
 * prog_link_drive()'s pin in bits 0 to 7, its target in bits 8 to 15 and its
 * pulse in bit 16.  One word, so the screen never reads one drive's target
 * with another's pin or pulse.  No link until an IOmcu answers. */
/** @brief Bit 16 of s_drive: the drive is a pulse. */
#define DRIVE_PWM_BIT (1u << 16)
/* Bits 20 to 23 of s_drive: the ports the HITEC page holds
 * (prog_link_hitec_held()), for the PROGRAM list's PORT IN USE. */
/** @brief Bit position of the ports the HITEC page holds in s_drive. */
#define DRIVE_HELD_SHIFT 20u
/**
 * @brief Where the tester's commands go: pin in bits 0 to 7, target in bits 8
 *        to 15, pulse in bit 16.
 */
extern atomic_uint    s_drive;
/** @brief The programming port a servo programmer holds, for the band's
 *         SAFE chip: prog5v_box_held_port() of the KST and Hitec sessions,
 *         0 to 3, PROG5V_HELD_SOME or PROG5V_HELD_NONE.  Written by
 *         kst_port_look() on the control task, read by the frame. */
extern atomic_uint    s_prog_band;
/** @brief Whether a servo programmer, KST or Hitec, has a port or an
 *         operation under way: the bench takes no arm then, and no PD mini
 *         change on a board whose ports run from it
 *         (supply_link_session_blocks()).  The error rule's hold needs the
 *         SUPPLY page's word as well (supply_link_port_holds()).  Control
 *         task only; kst_port_look(). */
extern bool            s_prog_taken;
/** @brief The gate the servo programmer's commands are claimed at. */
extern kst_prog_gate_t s_kst_gate;
/** @brief The programmers' session faults: the KST outcomes
 *         kst_link_outcome_fault() names, counted as kst_publish() takes
 *         them (kst_link_faults()), and the Hitec model's
 *         (hitec_prog_gate_faults()).  Control task only.
 *  @return the sum, modulo 2^32 */
uint32_t prog_faults(void);
/* The SENSE and TONE set-ups that are not held at the far end. */
/** @brief The SENSE set-ups the far end does not hold. */
extern atomic_uint s_sense_unheld;
/** @brief The TONE set-ups the far end does not hold. */
extern atomic_uint s_tone_unheld;
/* Hitec D receive (HITEC, 4.24): the module and the mode the setting asks
 * (link_hitec_rx_t, from the render loop). */
/** @brief The Hitec D receive module and the mode the setting asks. */
extern hitec_rx_link_t s_hitec_rx_link;
/** @brief The mode SET_HITEC_RX asks, link_hitec_rx_t. */
extern atomic_uint     s_hitec_rx_want;

/**
 * @brief Read what the servo programmers hold, for s_prog_band and
 *        s_prog_taken; control task.
 */
void kst_port_look(void);
/**
 * @brief Tell the operator what the SENSE and SERVO_SENSE pages said, one
 *        event at a time; control task.
 */
void sense_link_alerts(void);
/**
 * @brief Serve the SENSE and SERVO_SENSE pages; from the poll.
 * @param idle whether the bench is disarmed and the exchange may be spent on
 *        a write
 * @param bench the poll's bench sample
 */
void sense_link_service(bool idle, bench_state_t *bench);

/* ----------------------------------------------------------- panel_sense.c */

/* The SENSE_MAP page (4.25) and the INA3221 parts' states for SETUP >
 * SENSORS (sense_map_link_status()'s word).  Control task only, but
 * s_sense_parts, which app_main reads. */
/** @brief The SENSE_MAP page's module (4.25). */
extern sense_map_link_t s_sense_map;
/**
 * @brief The INA3221 parts' states for SETUP > SENSORS
 *        (sense_map_link_status()'s word).
 */
extern atomic_uint      s_sense_parts;
/** @brief The INA228's state in the same words, sense_map_link_i228()'s. */
extern atomic_uint      s_sense_i228;
/* SENSE_LIMIT (4.27, panel_limit.c): SETUP > SENSORS' LIMIT rows taken on
 * app_main by publish_limits(), the page served by the control task, and
 * s_sense_limits, sensors_rows_set_limits()'s word, for app_main. */

/** @brief Takes the LIMIT and WATCHES rows for the control task; app_main,
 *         from the SENSORS observer. */
void publish_limits(void);
/**
 * @brief The link came up; control task.
 * @param minor the IOmcu's protocol minor; below 27 nothing is sent to
 *              SENSE_LIMIT.
 */
void sense_limit_came_up(uint16_t minor);
/** @brief The link went; control task.  Clears s_sense_limits. */
void sense_limit_lost(void);
/** @brief Serves SENSE_LIMIT, at most 2 exchanges, and sends the next band
 *         alert; control task, every poll. */
void sense_limit_service(void);
/** @brief sensors_rows_set_limits()'s word; control task writes, app_main
 *         reads. */
extern atomic_uint s_sense_limits;
/**
 * @brief The link came up; hand the SENSE_MAP module the minor.
 * @param minor the IOmcu's protocol minor; below 25 nothing is sent
 */
void sense_map_came_up(uint16_t minor);
/** @brief The link went; clear the parts' states and the log's columns. */
void sense_map_lost(void);
/**
 * @brief Serve the SENSE_MAP page, from the poll before sense_link_service();
 *        control task.
 * @param idle whether the map SENSORS asks for may be written
 */
void sense_map_service(bool idle);
/**
 * @brief SENSE_WIN_A and SENSE_WIN_B (4.25): read on every poll after
 *        SERVO_WIN; the window number @p b's servo_window names attached as
 *        its chw, FAST's channel from the ring's window with @p ring.
 *        Control task.
 */
void sense_win_service(void);
/**
 * @brief Attach the SENSE windows to the bench sample.
 * @param b the sample; its servo_window names the window attached as chw
 * @param ring whether FAST's channels come from the ring
 */
void sense_win_attach(bench_state_t *b, bool ring);
/**
 * @brief Each channel's highest current of the windows read since the last
 *        call, and cleared (sense_win_link_take_peaks()); control task.
 * @param out Receives 12 figures, mA, signed.
 * @return Bit c set when channel c has a window in @p out.
 */
uint16_t sense_win_take_peaks(int16_t *out);
/**
 * @brief The log's INA3221 columns for a file opened now (log_cols.h); false
 *        for CH1 to CH3.  Any task.
 * @param out takes the columns; may be NULL
 * @return true when a map in force made the columns, false for CH1 to CH3
 */
bool sense_log_cols(log_cols_t *out);
/**
 * @brief Tell the operator what the TONE page said, one event at a time;
 *        control task.
 */
void tone_link_alerts(void);
/** @brief Serve the TONE page, the phase tap; from poll_bench(). */
void tone_link_service(void);
void kst_commands(void);
/** @brief One pass of the KST page's module; control task. */
void kst_link_service(void);
/** @brief One pass of the PROG page's module; control task, from the poll. */
void prog_link_service(void);
/**
 * @brief Write the Hitec D receive mode to the HITEC page when one is owed;
 *        from poll_bench().
 */
void hitec_rx_service(void);
void kst_publish(kst_prog_feed_t *f);

/* ------------------------------------------------------------ panel_hitec.c */

/** @brief The HITEC page's module (4.23); control task only. */
extern hitec_link_t      s_hitec_link;
/** @brief The gate the Hitec programmer's commands are claimed at; the one
 *         part both tasks touch. */
extern hitec_prog_gate_t s_hitec_gate;
/** @brief The Hitec programmer's commands, at most 4 a pass; control task. */
void hitec_commands(void);
/** @brief One pass of the HITEC page's module; control task, every pass.
 *  @param link_up whether the IOmcu answers */
void hitec_pass(bool link_up);
/** @brief Whether the HITEC page holds a port or a request runs.
 *  @return true while it does */
bool hitec_taken(void);
/** @brief The port the HITEC page holds, as last read.
 *  @return 0 to 3; -1 while it holds none */
int hitec_taken_port(void);
/** @brief What the Hitec programmer's model is fed; under the snapshot's
 *         lock.
 *  @param f receives the feed */
void hitec_publish(hitec_prog_feed_t *f);

/** @brief The ESC_WIRE page's module (4.34); control task only. */
extern esc_link_t      s_esc_link;
/** @brief The gate the ESC programmer's commands are claimed at. */
extern esc_prog_gate_t s_esc_gate;
/** @brief The ESC programmer's commands, app_main -> control task. */
extern QueueHandle_t   s_esc_q;
/** @brief The ESC programmer's commands, and a stop; control task. */
void esc_commands(void);
/** @brief One pass of the ESC_WIRE page's module; control task, every pass.
 *  @param link_up whether the IOmcu answers */
void esc_pass(bool link_up);
/** @brief Whether the ESC_WIRE page holds a pin.
 *  @return true while it does */
bool esc_taken(void);
/** @brief What the ESC programmer's model is fed; under the snapshot's lock.
 *  @param f receives the feed */
void esc_publish(esc_prog_feed_t *f);

/**
 * @brief Where the tester's commands go, into s_drive for the SERVO screen:
 *        from each pass of prog_link_service() and from the link-lost edge.
 */
static inline void prog_drive_publish(void)
{
    const prog_link_drive_t drive = prog_link_drive(&s_prog_link);
    atomic_store(&s_drive, ((unsigned)drive.target << 8) | drive.pin
                           | (drive.pwm ? DRIVE_PWM_BIT : 0u)
                           | ((unsigned)prog_link_hitec_held(&s_prog_link)
                              << DRIVE_HELD_SHIFT));
}

/* ------------------------------------------------------------ panel_poll.c */

/* The GPIOs of the throttle's slots, whether a binding was read, and the
 * protocol they are bound as (outbind_role_proto()). */
/** @brief GPIOs of the throttle's slots, packed in one word. */
extern atomic_ullong s_throttle_pins;
/** @brief Whether a binding was read. */
extern atomic_bool   s_throttle_pins_known;
/** @brief The protocol the throttle is bound as (outbind_role_proto()). */
extern atomic_uint   s_throttle_proto;
/** @brief The THROTTLE in force at the IOmcu, for the log's rows.  Control
 *         task only. */
extern bench_throttle_t s_bench_throttle;

/**
 * @brief The OUTPUTS page as the IOmcu holds it; the OUTPUTS screen's write
 *        asks for one straight afterwards.
 */
void read_outputs_binding(void);
/** @brief Tell the screens no reading is to be had: the link is down. */
void post_no_reading(void);

/**
 * @brief One pass of the poll: the bench page while the far end answers, the
 *        identity page while it does not.
 * @param link_up in: whether the link is up; out: whether it is after the
 *        pass
 * @param bench takes the bench sample
 * @param last_poll the bench poll gate's clock
 * @param last_status the status gate's clock
 * @return true when this pass produced a bench sample
 */
bool poll_far_end(bool *link_up, bench_state_t *bench,
                  uint32_t *last_poll, uint32_t *last_status);
/**
 * @brief Step the model, count the run's totals and post the bench log's rows
 *        for this pass.
 * @param link_up whether the link is up
 * @param emitted the command the model follows
 * @param sim the bench model
 * @param bench the sample; totals are written into it
 * @param new_sample in: a sample arrived from the far end; out: this pass
 *        produced one
 */
void advance_model_and_log(bool link_up, float emitted,
                           telemetry_sim_t *sim, bench_state_t *bench,
                           bool *new_sample);
/**
 * @brief Publish the numbers under the snapshot's lock and queue the sample
 *        for the renderer.
 * @param bench the bench sample
 * @param link_up whether the link is up
 * @param new_sample whether the sample is new
 */
void publish_snapshot(const bench_state_t *bench, bool link_up,
                      bool new_sample);
/* The IOmcu's STATUS fault bitmap at its last read, LINK_FAULT_* bits.
 * Control task only. */
uint16_t far_end_faults(void);
/**
 * @brief The bitmap as the band shows it: far_end_faults() with the STOP
 *        latch's grace applied (stop_latch_faults()).  Control task only.
 * @return LINK_FAULT_* bits
 */
uint16_t far_end_faults_shown(void);
/**
 * @brief STATUS read now, outside the poll's 500ms period: after a CLEAR,
 *        so the bitmap the screens see follows it.  Control task only.
 */
void far_end_status_now(void);

/* ------------------------------------------------------- main.c, continued */

/* The throttle's ramp, published by app_main for the control task. */
/**
 * @brief The throttle's ramp in proportion of travel per second; OUT_SPAN is
 *        full travel.
 */
extern atomic_uint s_throttle_ramp;
/**
 * @brief Convert the Ramp limit setting and publish it for the control task;
 *        app_main.
 */
void publish_throttle_ramp(void);
/**
 * @brief The throttle's ramp as app_main published it.
 * @return the ramp in OUT_SPAN per second
 */
static inline uint16_t panel_throttle_ramp(void)
{
    return (uint16_t)atomic_load(&s_throttle_ramp);
}

/*
 * The bench page is polled every 50ms, so a sample is 1/20s of plot.  The
 * motor screen scales its time axis by the same number; the two have to
 * agree or the axis lies about how long ago something happened.
 */
#define PANEL_SAMPLE_HZ   20.0f  /**< Bench samples per second. */

/**
 * @brief The kinds of command a screen queues for the control task.
 *
 * PANEL_CMD_CLEAR is ARM + START's FAILSAFE CLEAR
 * (servo_screen_take_clear()): CLEAR on the control page while SAFE; see
 * apply_clear_cmd().
 */
typedef enum { PANEL_CMD_MOTOR = 0, PANEL_CMD_SERVO,
               PANEL_CMD_STOP, PANEL_CMD_OUTPUTS,
               PANEL_CMD_SUPPLY, PANEL_CMD_PROG5V,
               PANEL_CMD_CLEAR } panel_cmd_kind_t;

/** @brief A screen's command, app_main to the control task over s_cmd_q. */
typedef struct {
    panel_cmd_kind_t kind;  /**< Which of the fields below is used. */
    /** @brief PANEL_CMD_MOTOR: the MOTOR & ESC screen's command. */
    motor_cmd_t      motor;
    /** @brief PANEL_CMD_SERVO: the SERVO screen's command. */
    servo_cmd_t      servo;
    outbind_t        bind;   /**< PANEL_CMD_OUTPUTS: the protocols and their pins */
    supply_cmd_t     supply; /**< PANEL_CMD_SUPPLY: set points and the switch */
    prog5v_cmd_t     prog5v; /**< PANEL_CMD_PROG5V: the ports' 5V switch    */
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
    /** @brief And how many STOP latches their reset had released
     *  (s_stop_resets): an arm or an ON of the supply or the ports' 5V
     *  queued while a STOP stood is not applied after the release. */
    uint32_t         resets;
    /** And how many times the supply's output had been asked off; see
     *  s_supply_offs. */
    uint32_t         supply_offs;
    /** @brief And how many times the ports' 5V had been asked off
     *  (s_prog5v_offs): an ON queued before an OFF is not applied after
     *  it. */
    uint32_t         prog5v_offs;
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
    /**
     * @brief The number of the last touch event the render side had taken off
     *        s_touch_q.
     */
    uint32_t         consumed_seq;
} panel_cmd_t;

/** @brief What became of the last output-page write. */
extern atomic_int s_outputs_result;

/* The queues between the two tasks, and their items; see main.c. */
/**
 * @brief One entry of s_touch_q: a touch event with its number, or a loss
 *        notice.
 */
typedef struct {
    touch_event_t evt;  /**< The touch event. */
    /** @brief The number the control task gave it (touch_loss.h). */
    uint32_t      seq;
    bool          lost;    /**< a loss notice, not an event */
} touch_item_t;
extern QueueHandle_t s_touch_q;   /**< control task -> app_main */
/**
 * @brief One entry of s_supply_q: a supply sample with the servo rail's meter
 *        as of the same poll.
 */
typedef struct {
    supply_state_t    st;  /**< The supply's sample. */
    /** @brief The servo rail's meter when the sample was queued. */
    servo_source_id_t source;
    /** @brief The meter's change count as of the same poll. */
    uint32_t          changes;
    /**
     * @brief Why the INA3221 last stopped being the meter, and at which count
     *        (servo_source_dropped()), as of the same poll.
     */
    servo_source_why_t dropped;
    /** @brief The change count at which it stopped. */
    uint32_t          dropped_at;
} supply_item_t;
extern QueueHandle_t s_supply_q;  /**< control task -> app_main */

/* STOP's request and its count; the let-goes asked for. */
/**
 * @brief A press the control task's own hit test did not see: the router's
 *        backstop STOP; app_main sets, control task takes.
 */
extern atomic_bool s_stop_request;
extern atomic_uint s_stop_counted;  /**< s_touch_q number of a counted STOP, 0 none */
/**
 * @brief Times the panel has been told to let go: a disarm or a release of
 *        the servo's pin.
 */
extern atomic_uint s_lets_go;
/** @brief STOP latches released by their reset (stop_reset_service()); a
 *  command queued before the latest release drives nothing after it.
 *  Control task writes. */
extern atomic_uint s_stop_resets;
/** @brief s_stop_resets as the render task's frame began; the commands it
 *  flushes carry this.  Render task only. */
extern uint32_t s_render_resets;

/* The supply's switch and set points, from the render side. */
/**
 * @brief An OFF of the supply's output asked for, as a flag a full queue
 *        cannot lose.
 */
extern atomic_bool s_supply_off_request;
/** @brief OFFs of the supply's output asked for, counted. */
extern atomic_uint s_supply_offs;
/** @brief The supply's voltage set point, mV, as the screen holds it. */
extern atomic_uint s_supply_set_mv;
/** @brief The supply's current set point, mA, as the screen holds it. */
extern atomic_uint s_supply_set_ma;
/** @brief The operator's cap on the voltage set point, mV. */
extern atomic_uint s_supply_vmax_mv;
/** @brief The operator's cap on the current set point, mA. */
extern atomic_uint s_supply_imax_ma;
/** @brief The overcurrent trip, mA; 0 is off. */
extern atomic_uint s_supply_trip_ma;
extern atomic_uint s_supply_trip_mv;  /**< The voltage trip, mV; 0 is off. */
/** @brief The time over a trip threshold before it cuts, ms. */
extern atomic_uint s_supply_trip_ms;
/**
 * @brief The newest ON the render side has seen applied, from a frame that
 *        found the touch stream whole.
 */
extern atomic_uint s_supply_ack;
/** @brief The ports' 5V asked off from the PROG 5V box, counted; render
 *         side writes, control task reads. */
extern atomic_uint s_prog5v_offs;
/** @brief A PROG 5V OFF asked, whether or not its queue entry survives a
 *         full queue; render side sets, control task takes
 *         (prog5v_off_service()). */
extern atomic_bool s_prog5v_off_request;
/** @brief The PROG 5V ONs the render side has queued; with s_psup_ons_taken
 *         the gates count an ON still in the queue as live
 *         (supplies_live_or_coming()). */
extern atomic_uint s_psup_ons_sent;
/** @brief The PROG 5V ONs refused, dropped from a full queue, or taken and
 *         published in s_psup_live; control task, and send_cmd() for an
 *         eviction. */
extern atomic_uint s_psup_ons_taken;
/** @brief The PROG 5V ONs applied, numbered; control task writes, see
 *         prog_supply_watch_service(). */
extern atomic_uint s_psup_gen;
/** @brief The newest PROG 5V ON a whole frame has seen on; render side
 *         writes, see prog_supply_watch_service(). */
extern atomic_uint s_psup_ack;
/**
 * @brief Whether the supply's output is on, stored by the control task when
 *        it switches.
 */
extern atomic_bool s_supply_live;
/** @brief ONs of the supply the render side has queued. */
extern atomic_uint s_supply_ons_sent;
/**
 * @brief ONs of the supply the control task has taken, applied or dropped.
 */
extern atomic_uint s_supply_ons_taken;
/** @brief Edits of the PD mini's wiring, counted. */
extern atomic_uint s_pdmini_edits;
/**
 * @brief Whether the SUPPLY screen drives the PD mini rather than the panel's
 *        model.
 */
extern atomic_bool s_supply_real;

/* The touch stream's numbering and losses, and the arm watch; see
 * touch_loss.h. */
/**
 * @brief Events the control task found missing from the driver's touch stream
 *        since boot.
 */
extern atomic_uint s_drv_gaps;
/** @brief The last number offered to s_touch_q, stored after the offer. */
extern atomic_uint s_tq_published;
/** @brief s_touch_q's producer: the numbers offered and the last loss
 *         notice's (touch_loss.h); the control task's alone. */
extern touch_seq_tx_t s_tq_tx;
/**
 * @brief Times the render loop dropped the screens' gestures for a touch
 *        loss.
 */
extern atomic_uint s_loss_gen;
/**
 * @brief The arm the render loop has seen, set by a frame that began armed
 *        and found no loss.
 */
extern atomic_uint s_arm_ack;
/** @brief The render loop's progress, for the control task's watchdog; see
 *         frame_watch.h and main.c. */
extern atomic_uint s_render_progress;

/**
 * @brief The render task made progress: a pass of its loop, or one card read
 *        inside a pass.  Render task only.
 */
static inline void render_progress(void)
{
    atomic_fetch_add(&s_render_progress, 1u);
}
/**
 * @brief The command's loss_gen when the arm in flight was taken; control
 *        task only.
 */
extern uint32_t    s_arm_take_gen;
/** @brief s_drv_gaps when the arm in flight was taken; control task only. */
extern uint32_t    s_arm_take_drv;
/** @brief The watch on the arm in flight; control task only. */
extern arm_watch_t s_arm_watch;

/* A supply run's row timestamp, and the log's run edges; see main.c. */
extern float s_log_t;  /**< The time stamp of a supply run's next row, s. */
/**
 * @brief End and start the log's runs as the arm and the supply change;
 *        control task.
 */
void log_follow_runs(void);

/* The screens' commands, app_main -> control task; see main.c. */
extern QueueHandle_t s_cmd_q;

/* ---------------------------------------------------------- panel_supply.c */

/* The modelled supply and the PD mini's link. */
extern supply_sim_t   s_supply_sim;  /**< The panel's model of the supply. */
/** @brief The PD mini's link, the SUPPLY page's module. */
extern supply_link_t  s_supply_link;
/* Set points the supply reported, for the screen to adopt. */
/** @brief A set point the supply reported, mV, for the screen to adopt. */
extern atomic_uint    s_supply_adopt_mv;
/** @brief A set point the supply reported, mA, for the screen to adopt. */
extern atomic_uint    s_supply_adopt_ma;
/** @brief Number of the report; raised by the control task with each. */
extern atomic_uint    s_supply_adopt_gen;
/** @brief The report the render loop has put on the screen. */
extern atomic_uint    s_supply_adopt_done;
/** @brief The last read of the supply: no port holds it and its output is
 *         off (supply_link_port_free_off()). */
extern atomic_bool    s_supply_free_off;
/* The ESC (electronic speed controller) model's profile and its runs. */
/**
 * @brief The profile of the ESC a stick run programs, as a pointer; render
 *        loop stores.
 */
extern atomic_uintptr_t s_escsim_profile;
/** @brief Runs the stick screen has started, counted. */
extern atomic_uint    s_escsim_runs;
/** @brief Whether the IOmcu speaks protocol 4.3, the SUPPLY page. */
extern bool           s_supply_page;
/** @brief Whether the IOmcu speaks protocol 4.4, AUTO baud and BAUD_FOUND. */
extern bool           s_supply_auto;
/** @brief Whether the PD mini, not the model, is the supply. */
extern bool           s_supply_is_real;
extern supply_state_t s_supply;  /**< The supply's last reading. */
/** @brief When the supply's totals last counted, now_ms(). */
extern uint32_t       s_supply_ms;
/** @brief The stop count the supply has answered. */
extern uint32_t       s_supply_stops_served;
/** @brief When the supply was last stepped, now_ms(). */
extern uint32_t       s_supply_step_ms;
/** @brief Number of the ONs applied; the snapshot carries it. */
extern uint32_t       s_supply_gen;

/**
 * @brief Count the interval since the last step into the totals and write it
 *        as a supply run's last row.
 */
void supply_log_tail(void);
/**
 * @brief Switch the supply's output.
 * @param on true for on, false for off
 */
void supply_switch(bool on);
/**
 * @brief Apply the screen's set points, limits and trips to the supply; every
 *        pass.
 */
void supply_follow_set(void);
/**
 * @brief Every stop switches the output off: STOP, touch that died, an arm
 *        the watch undid, the far end's refusal.  Called from control_pump(),
 *        which runs inside every exchange's wait, and again wherever the
 *        supply is stepped or switched, so a stop counted anywhere cuts the
 *        output before the next reading is taken or logged.
 */
static inline void supply_follow_stops(void)
{
    const uint32_t stops = arming_stop_count(&s_arm);
    if (stops != s_supply_stops_served) {
        s_supply_stops_served = stops;
        supply_switch(false);
    }
}

/**
 * @brief A PROG 5V OFF asked (s_prog5v_off_request), taken before the
 *        queue's drain: the OFF owed on the PROG_SUPPLY page.  Control
 *        task.
 */
void prog5v_off_service(void);

/**
 * @brief An OFF asked for, whether or not its queue entry survived; applying
 *        it twice costs nothing.  Called before the drain, so an OFF is never
 *        left waiting behind the queue's backlog.  And the set points, every
 *        pass.
 */
static inline void supply_service(void)
{
    if (atomic_exchange(&s_supply_off_request, false)) {
        supply_switch(false);
    }
    prog5v_off_service();
    supply_follow_set();
}

/**
 * @brief Apply a supply command a screen queued: the switch and the set
 *        points; control task.
 * @param pc the command
 */
void apply_supply_cmd(const panel_cmd_t *pc);
/**
 * @brief An error of the bench (error_sites_stop()): the bench to SAFE
 *        when it is ARMED or a switch to ARMED waits, and the supply's
 *        output off; with an IOmcu older than link 4.31, not when a
 *        programming port holds it.  Control task, in the pump as well.
 */
void bench_error_stop(void);
/** @brief The card faults the logger counted, each an error once
 *         (bench_error_stop()); from the pump. */
void card_faults_follow(void);
/** @brief The programmers' session faults (prog_faults()), each an error
 *         once; from the pump. */
void prog_faults_follow(void);
/** @brief Card writes that failed during a run, counted by the logger at
 *         each failure's edge (panel_log.c); read by the control task. */
extern atomic_uint s_card_faults;
/** @brief The supply's service, from control_pump(). */
void supply_pump(void);
/**
 * @brief Serve the SUPPLY page: write what is owed, read the page; from
 *        poll_bench().
 */
void supply_link_service(void);
/** @brief The PROG_SUPPLY page (link 4.32) on a board that switches the
 *         programming ports' own supply: its state read, and the OFF an
 *         error owes written; control task only. */
extern prog_supply_link_t s_psup_link;
/** @brief The PROG_SUPPLY page's service: the owed OFF written, the page
 *         read every PROG_SUPPLY_LINK_READ_MS, its state published to
 *         SUPPLY; from poll_bench(), after supply_link_service(). */
void prog_supply_service(void);
/** @brief The ports' own supply is the programmers' supply
 *         (prog_supply_link_owns()), as the control task last published. */
extern atomic_bool s_psup_owns;
/** @brief The ports' own supply counts as live for the install and flash
 *         gates (prog_supply_link_live()), as last published. */
extern atomic_bool s_psup_live;
/** @brief Publish s_psup_owns and s_psup_live from s_psup_link, the board
 *         and s_prog_taken; control task only. */
void prog_supply_publish(void);
/**
 * @brief The PROG 5V box's command (PANEL_CMD_PROG5V): its OFF owed at once;
 *        its ON under the PD mini's OUTPUT ON rules -- no programmer holding
 *        a port, no stop, no OFF and no touch loss since it was queued --
 *        then owed to the page and watched until a whole frame has seen it.
 *        Control task only.
 * @param pc the command
 */
void apply_prog5v_cmd(const panel_cmd_t *pc);
/** @brief An ON of the ports' 5V taken across lost touch events is an
 *         error (the PD mini's rule); from the pump. */
void prog_supply_watch_service(void);
/**
 * @brief Publish to the PROG 5V box the last window of the INA3221 channel
 *        the held map has watching the ports' 5V, and step the PROG 5V
 *        watch with it (prog5v_lost.h): a 5V that does not come after an
 *        ON, or collapses under it, is an error (bench_error_stop()), with
 *        one alert and one console line; control task.
 */
void prog5v_publish_reading(void);
/**
 * @brief Queue to the stick run the last window of the INA3221 channel in the
 *        ESC's supply line, each window once: the lowest channel of a fitted
 *        part with a shunt that the held map has watching SUPPLY; control
 *        task.
 */
void stick_meter_publish(void);
/**
 * @brief The ON of the ports' 5V for the PROG 5V watch: the page reads it
 *        on or acknowledged the panel's ON, and the heartbeat edges (a
 *        withheld heartbeat switches it off at the IOmcu before the page
 *        reads so); whether a programming port holds it, the cuts and the
 *        page's reads that showed it on.  Control task only.
 * @param out receives it
 */
void prog5v_watch_on(prog5v_on_t *out);

/* ------------------------------------------------------------- panel_arm.c */

/**
 * @brief Set up the bench's own state before the control loop; ends by
 *        setting s_pump_live.
 * @param sim the bench model
 * @param bench the bench sample
 */
void control_setup(telemetry_sim_t *sim, bench_state_t *bench);
/* The bench's totals, and when they last counted. */
extern bench_totals_t s_totals;  /**< The bench's totals. */
/** @brief When the totals last counted, now_ms(). */
extern uint32_t       s_totals_ms;
/** @brief The supply's last reading for a SERVO run's rows (log_rail_t).
 *         Control task only. */
extern log_rail_t     s_rail;
/** @brief The run's totals of s_rail since the arm.  Control task only. */
extern bench_totals_t s_rail_totals;
/** @brief The SERVO screen's pulse and angle for a SERVO run's rows,
 *         packed (log_servo_pack()): stored by the render side each frame,
 *         read by the control task. */
extern atomic_uint    s_servo_log;
/**
 * @brief Look at the touch stream for an arm being handed over, once a pass
 *        after the snapshot.
 * @param link_up whether the link to the IOmcu answers
 */
void arm_watch_service(bool link_up);
/* The latched stop, and the act the arming policy asks for. */
void service_arming(bool link_up);
/**
 * @brief Disarm the bench, and tell the far end while there is a link.
 * @param link_up whether the link to the IOmcu answers
 * @return true when a disarm was applied
 */
bool disarm_here(bool link_up);
/**
 * @brief What the screens asked for, in the order they asked it.
 * @param link_up whether the link to the IOmcu answers
 * @param bench the bench sample
 */
void drain_commands(bool link_up, bench_state_t *bench);

/* ---------------------------------------------------------- panel_render.c */

/** @brief Serve the servo programmer's model, once a frame on app_main. */
void kst_ui_service(void);
/** @brief The Hitec programmer's model, once a frame on app_main. */
void hitec_ui_service(void);
/** @brief The ESC programmer in the render loop: fed, and its commands queued. */
void esc_wire_ui_service(void);
/** @brief The ESC programmer's card request, filled by the render loop. */
extern esc_card_req_t  s_esc_card_req;
/** @brief The mount it was taken under. */
extern unsigned        s_esc_card_mount;
/** @brief The answer, filled by the log task. */
extern esc_card_done_t s_esc_card_done;
/** @brief Requests handed over. */
extern atomic_uint     s_esc_card_sent;
/** @brief Requests answered. */
extern atomic_uint     s_esc_card_answered;
/** @brief Runs the request handed over, on the log task. */
void esc_card_serve(void);
extern unsigned s_tq_lost;  /**< Touch events lost from s_touch_q, counted. */
/**
 * @brief Whether the supply's output is on, or an ON for it waits in the
 *        queue.
 * @return true while the output is on or an ON waits in the queue
 */
bool supply_live_or_coming(void);
/** @brief supply_live_or_coming(), or the programming ports' own supply
 *         live (s_psup_live): what the install, update and flash gates
 *         wait on.
 *  @return true while either supply is on or coming */
bool supplies_live_or_coming(void);
/**
 * @brief Publish the supply screen's limits and set points for the control
 *        task; every frame.
 */
void publish_supply_levels(void);
/** @brief Hand a servo test's lines to the logger; every frame. */
void test_lines_service(void);
/** @brief The rotary knob's motion, taken at the start of a frame. */
typedef struct {
    int            steps;  /**< Steps turned; signed. */
    ui_screen_id_t route;  /**< The screen the knob was turned on. */
    /** @brief The router's navigation count when the motion was taken. */
    uint32_t       navigations;
    /** @brief The M5Stack Dial's detents and gestures of the frame
     *         (panel_dial_take()). */
    dial_input_t   dial;
} knob_turn_t;

/**
 * @brief Take the rotary knob's motion with the screen it was turned on and
 *        the router's navigation count; start of a frame.
 * @return the motion
 */
knob_turn_t knob_take(void);
/**
 * @brief Apply a motion taken at the start of the frame to the slider of the
 *        screen on top; end of the frame.
 * @param turn what knob_take() returned
 * @param frame_lost whether the frame lost touch events
 * @param facts the frame's facts of the bench, for Dim after's rule on the
 *        Dial's input
 */
void knob_apply(const knob_turn_t *turn, bool frame_lost,
                const shot_facts_t *facts);
/**
 * @brief Hand the screens' queued commands to the control task; once a frame.
 * @param stops_now the stop count the commands carry
 */
void flush_screen_commands(uint32_t stops_now);
bool drain_touch(uint32_t stops_now, const shot_facts_t *facts);
/**
 * @brief Once a frame, after the frame's touch: the backlight at the duty
 *        Dim after gives (screen_dim_step()), set only when it changes.
 * @param facts  this frame's facts of the screenshot gate (panel_shot_frame())
 */
void backlight_service(const shot_facts_t *facts);
/**
 * @brief Print Dim after's state on the console, one line
 *        (screen_dim_line()).  Render loop only; the console's schedule
 *        calls it once every PERF_WINDOW_FRAMES frames (panel_perf.h).
 */
void backlight_line(void);
/**
 * @brief Queue the outputs screen's choice for the control task.
 * @param b the choice
 */
void outputs_apply(const outbind_t *b);

/* ------------------------------------------------------------- panel_art.c */

/**
 * @brief The flash store of the boardart partition, NULL when there is none.
 */
extern const art_flash_t *s_artflash;
/* A drawing fetch is under way; see art_begin(). */
/** @brief Whether a drawing fetch is under way. */
extern bool               s_artbusy;
/** @brief Whether the kept copy of the drawing is being written. */
extern atomic_bool        s_keeping;
/** @brief Guards the published drawing: s_drawpub, its size and its board. */
extern SemaphoreHandle_t  s_draw_lock;
/**
 * @brief The drawing the control task published, LINK_DRAW_MAX_BYTES in
 *        PSRAM.
 */
extern uint8_t           *s_drawpub;
/** @brief The UI task's copy of the drawing, in PSRAM. */
extern uint8_t           *s_drawshow;
/**
 * @brief The picker asks for a board's drawing; UI task only.
 * @param board the board identity
 */
void drawing_for_picker(uint16_t board);
/**
 * @brief Ask again for the drawing when a publication came since the picker
 *        last asked; UI task only.
 */
void drawing_follow(void);
/** @brief One step of the keep of the board's drawing; control task. */
void art_keep_service(void);
/**
 * @brief End the fetch of the board's drawing.
 * @param why the reason, for the log
 */
void art_stop(const char *why);
/**
 * @brief On the link-up edge, withdraw the last drawing and begin the fetch
 *        of this one.
 * @param board the board identity the IOmcu reported
 */
void art_begin(uint16_t board);
/** @brief One bounded slice of the drawing's fetch; control task. */
void art_slice(void);

/* ---------------------------------------------------------- panel_update.c */

/* An install about to write the flash asks the control task to stand
 * still (s_fw_quiet_req), and the control task says it does
 * (s_fw_quiet_ack).  False when wifi_access_init() failed (s_wifi_ok). */
/**
 * @brief An install about to write the flash asks the control task to stand
 *        still.
 */
extern atomic_bool s_fw_quiet_req;
extern atomic_bool s_fw_quiet_ack;  /**< The control task stands still. */
extern bool        s_wifi_ok;  /**< False when wifi_access_init() failed. */

/**
 * @brief An IOmcu of another protocol major answering the probe; control
 *        task.  panel_poll.c.
 */
extern link_uph_mismatch_t s_mismatch;

/**
 * @brief Whether the IOmcu's update may start: see fw_coproc_may_start().
 *        From any task.
 * @return true when the IOmcu's update may start
 */
bool cu_may_start(void);
/**
 * @brief The IOmcu update's check, for an IOmcu that just answered.
 * @param reply the identity answer
 */
void cu_link_up(const link_msg_t *reply);
/**
 * @brief One pass of the IOmcu's update, before the poll: true while the
 *        update holds the link.  Control task.
 * @param link_up the poll's link flag
 * @param bench the bench sample
 * @param last_poll the bench poll gate's clock
 * @param last_status the status gate's clock
 * @return true while the update holds the link
 */
bool cu_service(bool *link_up, bench_state_t *bench,
                uint32_t *last_poll, uint32_t *last_status);

/**
 * @brief Serve the WI-FI screen's radio, STOP and requests; once a frame on
 *        app_main.
 * @param armed ARMED, or an arm waiting
 * @param stops_now STOP presses since the start; a change is a STOP
 */
void wifi_service(bool armed, uint32_t stops_now);
/**
 * @brief Serve the update reminder; once a frame on app_main.
 * @param armed ARMED, or an arm waiting
 * @param unsafe ARMED, an arm waiting, or the supply's output on or coming on
 * @param cs the IOmcu update's state this frame
 */
void upd_remind_service(bool armed, bool unsafe, const cu_snap_t *cs);
/**
 * @brief SETUP > UPDATES, the reminder and the download, once a frame.
 * @param armed     ARMED, or an arm waiting
 * @param unsafe    ARMED, an arm waiting, or the supply's output on or
 *                  coming on
 * @param stops_now STOP presses since the start; a change is a STOP
 * @param cs        the IOmcu update's state this frame
 */
void upd_service(bool armed, bool unsafe, uint32_t stops_now,
                 const cu_snap_t *cs);
/**
 * @brief Drive the IOmcu update's screen; once a frame on the render side.
 * @param unsafe ARMED, an arm waiting, or the supply's output on or coming
 * @param link_up whether the link to the IOmcu answers
 * @param cs the IOmcu update's state this frame
 */
void cu_render_service(bool unsafe, bool link_up, const cu_snap_t *cs);
