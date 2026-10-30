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
#include "bench_state.h"
#include "log_cols.h"
#include "bind_link.h"
#include "card_tree.h"
#include "busfault_screen.h"
#include "coproc_update_screen.h"
#include "endpoints_link.h"
#include "files_screen.h"
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
#include "prog_supply_link.h"
#include "safety_gate.h"
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

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/*
 * The panel's throttle, as a channel in an output bank.
 *
 * The ramp is a proportion of travel per second rather than a percentage, so
 * the same number means the same speed on an output whose travel is not
 * measured in percent.
 */
#define PANEL_CH_THROTTLE     0u

#define ALERT_MAX         UI_ALERT_MAX

/* The control task's period; see "the two loops' plumbing" in main.c. */
#define CONTROL_PERIOD_MS 5u

/* The link to the IOmcu, used during bring-up and in the loop. */
extern link_host_t    s_host;
/* A link_cap_t bitmap from the IOmcu's identity page; see main.c. */
extern atomic_uint    s_capabilities;
extern link_bringup_t s_bring;

/* When the link went, 0 while it is up; whether the screen has had its
 * turn; the recoveries of this outage and since boot.  See main.c. */
extern atomic_uint s_link_lost_ms;
extern bool        s_link_lost_shown;
extern uint32_t    s_recoveries;
extern uint32_t    s_recoveries_total;

/* The panel's die temperature, read by the control task. */
extern float s_mcu_c;

/* What the IOmcu says its outputs are, both under s_snap_lock; the
 * state of the last reading, for the control task alone. */
extern bind_reading_t s_outputs_read;
extern bool           s_outputs_read_fresh;
extern bind_read_t    s_bind_read;
/* The IOmcu's protocol minor, and its board identity. */
extern uint16_t       s_far_minor;
extern uint16_t       s_board;

extern QueueHandle_t s_kst_q;     /**< app_main -> control task */
extern QueueHandle_t s_hitec_q;   /**< app_main -> control task */
extern QueueHandle_t s_sample_q;  /**< control task -> app_main */
extern QueueHandle_t s_enc_q;     /**< control task -> app_main */

/* The INA3221's CH1 windows from the ring, each number once and in order,
 * each with the servo rail's meter and its change count as decided in the
 * poll that took it (servo_screen_window()).  Full means the renderer is
 * SAMPLE_Q_LEN windows behind, and the oldest is dropped. */
typedef struct {
    sense_link_win_t  win;
    servo_source_id_t source;
    uint32_t          changes;
    servo_source_why_t dropped;     /* as supply_item_t's */
    uint32_t          dropped_at;
} servo_win_item_t;
extern QueueHandle_t     s_win_q;     /**< control task -> app_main */
extern SemaphoreHandle_t s_snap_lock;

/*
 * The IOmcu's update as the render side needs it: an offer, numbered,
 * with the version the IOmcu runs; while an update runs, its step and
 * blocks; and the STARTs the control task has taken, so a progress screen
 * whose START was spent leaves.
 */
typedef struct {
    uint32_t     offer_gen;
    bool         offered;
    cu_version_t running;
    bool         active;
    uint8_t      step;      /* a cu_step_t */
    uint16_t     block;
    uint16_t     blocks;
    uint32_t     starts_taken;
    uint8_t      check;     /* a cu_check_t: the last build check        */
    bool         mismatch;  /* an IOmcu of another protocol major
                               answers its identity (the link stays down) */
} cu_snap_t;

/* What the screen reads.  Written by the control task, copied by app_main. */
typedef struct {
    bench_state_t bench;
    supply_state_t supply;
    uint32_t      supply_gen;  /**< which ON, for the render side's ack */
    bool          link_up;
    bool          armed;
    /** The roles the bank is armed for, OUT_ROLE_BIT()s; 0 disarmed. */
    uint8_t       armed_roles;
    bool          arm_wanted;  /**< armed, or an arm waiting: arming_link_needed() */
    uint32_t      arm_gen;     /**< which arm, for the render side's ack */
    float         mcu_temp_c;
    bool          stopped;
    uint32_t      stops;
    uint32_t      pressed;     /**< of the stops, those pressed */
    uint16_t      faults;
    uint32_t      link_errors;
    uint32_t      link_refused; /**< ui_bench_status_t's link_refused */
    uint32_t      run_seconds;
    /* The control task's alert, numbered: an alert posted and replaced
     * before a frame took it was never shown (sense_link_alerts()), and one
     * whose cause is over is taken back (control_alert_withdraw()). */
    ui_alert_slot_t alert;
    /* The phase tap as the control task last read it, for the stick run's
     * page. */
    tone_readout_t tone;
    /* The servo rail's meter as of the last poll (servo_source.h), and the
     * CH1 windows that left the ring untaken since boot. */
    servo_source_id_t servo_source;
    uint32_t      servo_source_changes;
    /* Why the INA3221 is not the meter, and why and when it last stopped
     * being it (servo_source_why(), servo_source_dropped()). */
    servo_source_why_t servo_source_why;
    servo_source_why_t servo_source_dropped;
    uint32_t      servo_source_dropped_at;
    uint32_t      servo_win_lost;
    /* What the servo programmer's model is fed; see kst_publish(). */
    kst_prog_feed_t kst;
    /** @brief What the Hitec programmer's model is fed; see
     *         hitec_publish(). */
    hitec_prog_feed_t hitec;
    /* The IOmcu's update; see cu_service(). */
    cu_snap_t     cu;
} panel_snap_t;
extern panel_snap_t s_snap;

static inline void snap_lock(void)   { xSemaphoreTake(s_snap_lock, portMAX_DELAY); }
static inline void snap_unlock(void) { xSemaphoreGive(s_snap_lock); }

/* Called from the control task, which must not touch the router.  The
 * slot holds one alert; a later one replaces it.  Returns its number. */
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

/* Take back alert number @p gen, whose cause is over: one not yet taken
 * by a frame is dropped, and the last one a frame took is cleared from the
 * band if it is still showing (ui_router_withdraw_alert()).  An older
 * alert is left alone, so withdrawals of several causes in one frame do
 * not hide each other (ui_alert_slot.h).  Control task only. */
void control_alert_withdraw(uint32_t gen);

/* The same, for the callers that do not follow it.  Inlined where it is
 * called, as the compiler chose before the numbering: a frame of its own
 * puts 32 bytes on the control task's deepest chain (drv_lost() through
 * control_pump()) and moves stack_check's walk onto a deeper path through
 * ESP-IDF's logging. */
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

/* The safety loop's own state and the arming policy; see main.c. */
extern outputs_t s_out;
extern arming_t  s_arm;
extern uint16_t  s_throttle_hundredths;   /* 0..LINK_THROTTLE_MAX */
extern atomic_bool s_disarm_request;

/*
 * A disarm returns the command to zero, here and in the output bank.
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
extern atomic_uint s_supply_vin_mv;
extern atomic_uint s_supply_baud;

/* Which arm, for the render side's ack. */
extern uint32_t    s_arm_gen;

/* The pole count the IOmcu holds, and whether it is current. */
extern atomic_bool s_poles_owed;
extern atomic_uint s_poles_value;

/* The throttle endpoints the IOmcu holds (endpoints_link.h): owed, held
 * disarmed at a link-up, the pair and the time of the last edit.  The debt
 * is paid once the pair has rested ENDPOINTS_SETTLE_MS, and a switch to
 * ARMED with the link up is refused while it stands. */
extern endpoints_debt_t s_endpoints;

/* Where the PD mini is wired, from SETUP INTERFACES, in one word; see
 * main.c. */
extern atomic_uint s_pdmini_wiring;

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

static inline supply_wiring_t pdmini_wiring(void)
{
    return wiring_of(atomic_load(&s_pdmini_wiring));
}

/* What SETUP INTERFACES and SETUP > SENSORS left for the current monitors
 * and the phase tap, read by the control task; panel_sense.c publishes the
 * monitors' from the settings observer. */
void            publish_sense(void);
sense_setup_t   sense_wanted(void);
tone_setup_t    tone_wanted(void);

/* False until the control task owns the safety state. */
extern bool s_pump_live;
/* The safety loop, from the top of the control task and from inside every
 * exchange. */
void control_pump(void);


/* The GPIOs of the slots of the channels the SERVO screen's horn drives
 * (servo_link_binding()), and whether a binding was read; see main.c. */
extern atomic_ullong s_servo_pins;
extern atomic_bool   s_servo_pins_known;
/* For the SERVO screen's runs: the SENSE page holds the encoder enabled. */
extern atomic_bool   s_enc_held;

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

/* Read once at start-up, from bring_up(). */
void esc_profiles_load(void);
/* The log viewer's way to the card. */
extern const log_viewer_io_t k_card_io;

/* The runs the logger has open, held back from the viewer: the automatic
 * log's in AUTO LOG FOLDER, a test's CSV and report in USER LOG FOLDER,
 * each in the folder it was opened in (log_open_marks()). */
extern atomic_uint s_log_open_run;
extern atomic_uint s_test_open_run;
extern atomic_uint s_test_open_txt;
/* The files the logger has created since the start, and the LOGS tab of
 * the last one (log_viewer_tab_t): stored by the logger, the tab first. */
extern atomic_uint s_card_written;
extern atomic_int  s_card_written_tab;

/* One level of a folder of the card (card_make_fn, card_tree.h): the
 * logs', the fault log's and the servo files' folders are made with it. */
card_dir_t panel_card_make_dir(void *ctx, const char *level);

/* The programmer's record, KST.TXT: filled by the render loop, written by
 * the logger. */
extern char        s_kst_file[KST_PROG_FILE_MAX];
extern size_t      s_kst_file_len;
/* The mount whose record the model holds (storage_mount_count()): the
 * record is written to that volume or not at all. */
extern unsigned    s_kst_file_mount;
extern atomic_uint s_kst_file_sent;
extern atomic_uint s_kst_file_done;
extern atomic_bool s_kst_file_ok;
void kst_record_write(void);
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
extern kst_prog_card_req_t  s_kst_card_req;
/* The mount the request was taken under (storage_mount_count()): it is
 * run on that volume or finds no card. */
extern unsigned             s_kst_card_mount;
extern kst_prog_card_done_t s_kst_card_done;
extern atomic_uint          s_kst_card_sent;
extern atomic_uint          s_kst_card_answered;
void kst_card_serve(void);

/* ------------------------------------------------------------- panel_log.c */

/* A run is one arming of the bench, or one switch-on of the supply's output
 * while the bench is not armed; log_run_kind_t and the rule of which run
 * the log records are in log_cadence.h.  One run is one file, and a file
 * holds one kind of row, so its header names every column it has.  All
 * runs are written to AUTO LOG FOLDER, none while AUTO LOGS is off. */

/* The two folders SETUP > SD CARD names, AUTO LOG FOLDER and USER LOG
 * FOLDER. */
typedef enum {
    LOG_FOLDER_AUTO = 0,
    LOG_FOLDER_USER,
    LOG_FOLDERS
} log_folder_t;

/* The folder the next file of @p kind is written to: one card_folder_check()
 * takes (card_folder_use()).  Called by the text observer on app_main;
 * the logger reads it at each take, so a file open stays where it is and
 * the next one is numbered in the new folder. */
void log_set_folder(log_folder_t kind, const char *dir);

/* The open markers and the folder each was opened in, read as one, for the
 * viewer's card_busy(): @p run_dir and @p test_dir take CARD_DIR_MAX bytes
 * and @p marks points into them. */
void log_open_marks(log_marks_t *marks, char *run_dir, char *test_dir);

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

/*
 * Sixty-four rows is 3.2s of run at PANEL_SAMPLE_HZ, against the 250ms an
 * SD card may take over one write: a card that pauses costs no rows.
 */
#define LOG_Q_LEN      64

/* The report link_report() writes to the card, and how many may be in
 * flight.  The report is made every 5s at its fastest. */
#define LOG_NOTE_MAX   208
#define LOG_NOTE_Q_LEN 4

/** @brief The console reports link_report() hands the logger to print,
 *         10s of them at the 5s cadence: up to 7 lines of up to 167
 *         characters each, 50ms and more on the UART (universal
 *         asynchronous receiver-transmitter) console, which would
 *         otherwise stall the task that beats the safety line.  A full
 *         queue is printed where it is made: no line is dropped. */
#define CONSOLE_Q_LEN 2

/* One line of a servo test's files, render loop -> logger. */
typedef struct {
    uint8_t kind;                        /* servo_test_out_t */
    char    text[SERVO_TEST_LINE_MAX];
} test_line_t;

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
/* The run being recorded, or 0: the control task's level, which the logger
 * follows. */
extern atomic_uint s_log_run_now;

/* A servo test's OPENs and ENDs, queued by the render loop and taken by
 * the logger, and what the logger made of them. */
extern atomic_uint s_test_opens_sent;
extern atomic_uint s_test_opens_done;
extern unsigned    s_test_ends_sent;
extern atomic_uint s_test_ends_done;
extern atomic_int  s_test_file;
extern atomic_bool s_test_report;

/* The runlog task: every fopen, fwrite, fsync and fclose on the card. */
void log_task(void *arg);

/* main.c: the control task's side of the log, which numbers the runs and
 * posts the rows. */
extern log_cadence_t  s_log_cad;
extern log_run_kind_t s_log_kind;
extern atomic_bool    s_servo_testing;
void log_post(log_row_t *row);

/* ------------------------------------------------------------ panel_link.c */

/* Exchanges that ended unanswered; a far end held to be answering; an
 * exchange that ended unanswered with the link up.  See panel_link.c. */
extern uint32_t s_unanswered;
extern bool     s_link_is_up;
extern bool     s_link_quiet;

/* Put a built request on the wire and wait for its answer. */
bool exchange(link_host_t *host, const link_msg_t *req, link_msg_t *reply);

static inline bool poll_page(link_host_t *host, uint8_t page, uint8_t count,
                             link_msg_t *reply)
{
    link_msg_t req;
    if (!link_host_read(host, page, 0, count, now_ms(), &req)) {
        return false;
    }
    return exchange(host, &req, reply);
}

/* A window of a page from @p offset; the reply's registers start there. */
static inline bool read_regs(link_host_t *host, uint8_t page, uint8_t offset,
                             uint8_t count, link_msg_t *reply)
{
    link_msg_t req;
    if (!link_host_read(host, page, offset, count, now_ms(), &req)) {
        return false;
    }
    return exchange(host, &req, reply);
}

/* exchange() and the clock, as shared/link asks for them. */
uint32_t port_now(void *ctx);
void     port_between(void *ctx);
extern const link_port_t k_port;

/*
 * A write of any width goes out one frame per exchange, each acknowledged
 * before the next is sent (link_write_acked()): the far end's controller
 * buffers 2 frames and is polled, so frames sent back to back can find it
 * full.  A write of up to 4 registers is one exchange.
 */
static inline bool write_regs(link_host_t *host, uint8_t page, uint8_t offset,
                              uint8_t count, const uint16_t *regs,
                              link_msg_t *reply)
{
    return link_write_acked(host, &k_port, page, offset, count, regs, reply);
}

/* The control page: ARM and THROTTLE, the frame that arms, CLEAR, the pole
 * count and the throttle's endpoints.  See panel_link.c. */
extern uint16_t s_arm_value;

/*
 * ARM and THROTTLE, the write every 50ms poll and every disarm makes.
 *
 * Two registers rather than the three the frame that arms carries.  This
 * write keeps a running bench running and stops a stopping one, and the
 * pole count is neither: while a run is on, an edit to it is a correction
 * paid on its own transaction by poles_service(), and a disarm has nothing
 * to convert.  Kept apart so a count the far end refuses -- or one that has
 * not changed -- never stands between the throttle and the wire.
 *
 * True when the IOmcu acknowledged; a NACK (negative acknowledge) or
 * no answer is false.
 */
static inline bool control_write(bool armed, link_msg_t *reply)
{
    /* The value the far end was armed with, written again: a different
     * non-zero ARM is refused while it is armed (LINK_MINOR_ARM_ROLE). */
    const uint16_t regs[2] = { armed ? s_arm_value : 0u,
                               s_throttle_hundredths };
    return write_regs(&s_host, LINK_PAGE_CONTROL, LINK_CT_ARM, 2u, regs, reply)
           && reply->op == LINK_OP_ACK;
}

bool control_arm(link_msg_t *reply);
bool poles_service(void);
void endpoints_service(bool far_disarmed);

/*
 * Whether the far end trusts the heartbeat, asked of its STATUS page.
 *
 * An arm waits for this before CLEAR is written.  The heartbeat is withheld
 * while a stop is latched, and after it resumes the far end's monitor needs
 * five edges, 100 to 125ms at a 5ms pass and longer on a slower one: a
 * fixed wait is right for one pass period and refused at the next.  One
 * register, one frame each way.
 *
 * @p answered is false when nobody answered.  That exchange has then waited
 * LINK_HOST_TIMEOUT_MS (1000ms), past the arm's whole bound, and it is the
 * link that is gone and not the line that is distrusted.
 */
static inline bool far_line_trusted(bool *answered)
{
    link_msg_t st = { 0 };
    *answered = read_regs(&s_host, LINK_PAGE_STATUS, LINK_ST_FAULTS, 1u, &st);
    return *answered && st.op == LINK_OP_DATA
           && safety_gate_line_trusted(st.regs[0]);
}

void far_end_stop_here(void);
void arm_write_failed(uint32_t quiet_before, const char *alert);

static inline bool control_clear_failsafe(link_msg_t *reply)
{
    const uint16_t magic = LINK_CLEAR_MAGIC;
    return write_regs(&s_host, LINK_PAGE_CONTROL, LINK_CT_CLEAR, 1u, &magic,
                      reply)
           && reply->op == LINK_OP_ACK;
}

/* What the panel knows about a link that stopped; the link report, every
 * 5s while the link is down and every 60s while it is up, handed to the
 * logger to print (console_reports_print()). */
void link_lost_report(busfault_report_t *r);
void link_report(void);

/*
 * The seam between measured and modelled numbers.  When the IOmcu
 * answers, its numbers are used, and the LINK_BN_SIMULATED flag travels with
 * them.  When it does not, the panel models locally and sets the same flag.
 * Nothing above this function knows the difference.
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
extern sense_link_t   s_sense_link;
extern servo_source_t s_servo_source;
extern tone_link_t    s_tone_link;
extern kst_link_t     s_kst_link;
extern prog_link_t    s_prog_link;
/* What the SERVO screen wants of the PROG page, and the pin of port 0. */
extern atomic_uint    s_prog_want_target;
extern atomic_bool    s_prog_want_pwm;
extern atomic_uint    s_prog_pin;
/* The ports the PROG page reports (prog_link_count(), 0xFF while not
 * known) in bits 0 to 7, and the pins of ports 1, 2 and 3 in bits 8 to 15,
 * 16 to 23 and 24 to 31, for the OUTPUTS screen's PROG PORT entries and
 * the pin picker; one word, so the screens never read one page's count
 * with another's pins. */
#define PROG_PORTS_NONE 0xFFFFFFFFu
extern atomic_uint    s_prog_ports;
/* panel_ports.c: the ports' view and the screens' asks (port_view.h).
 * prog_ports_publish() and prog_ports_lost() on the control task, the
 * other two on the render loop. */
void prog_ports_publish(void);
void prog_ports_lost(void);
void prog_ports_init_screens(void);
void prog_ports_to_screens(void);
/* Where the tester's commands go, for the SERVO screen's TARGET row:
 * prog_link_drive()'s pin in bits 0 to 7, its target in bits 8 to 15 and its
 * pulse in bit 16.  One word, so the screen never reads one drive's target
 * with another's pin or pulse.  No link until an IOmcu answers. */
#define DRIVE_PWM_BIT (1u << 16)
/* Bits 20 to 23 of s_drive: the ports the HITEC page holds
 * (prog_link_hitec_held()), for the PROGRAM list's PORT IN USE. */
#define DRIVE_HELD_SHIFT 20u
extern atomic_uint    s_drive;
/** @brief Whether a servo programmer, KST or Hitec, has a port or an
 *         operation under way: the bench takes no arm and no supply
 *         change then.  Not whether a port holds the supply: the error
 *         rule reads that from the SUPPLY page (supply_link_port_holds()).
 *         Control task only; kst_port_look(). */
extern bool            s_prog_taken;
extern kst_prog_gate_t s_kst_gate;
/** @brief The programmers' session faults: the KST outcomes
 *         kst_link_outcome_fault() names, counted as kst_publish() takes
 *         them (kst_link_faults()), and the Hitec model's
 *         (hitec_prog_gate_faults()).  Control task only.
 *  @return the sum, modulo 2^32 */
uint32_t prog_faults(void);
/* The SENSE and TONE set-ups that are not held at the far end. */
extern atomic_uint s_sense_unheld;
extern atomic_uint s_tone_unheld;
/* Hitec D receive (HITEC, 4.24): the module, the mode SETUP asks
 * (link_hitec_rx_t, from the render loop) and what its row says
 * (hitec_rx_link_mark_t, to the render loop). */
extern hitec_rx_link_t s_hitec_rx_link;
extern atomic_uint     s_hitec_rx_want;
extern atomic_uint     s_hitec_rx_mark;

void kst_port_look(void);
void sense_link_alerts(void);
void sense_link_service(bool idle, bench_state_t *bench);

/* ----------------------------------------------------------- panel_sense.c */

/* The SENSE_MAP page (4.25) and the INA3221 parts' states for SETUP >
 * SENSORS (sense_map_link_status()'s word).  Control task only, but
 * s_sense_parts, which app_main reads. */
extern sense_map_link_t s_sense_map;
extern atomic_uint      s_sense_parts;
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
void sense_map_came_up(uint16_t minor);
void sense_map_lost(void);
void sense_map_service(bool idle);
/* SENSE_WIN_A and SENSE_WIN_B (4.25): read on every poll after SERVO_WIN;
 * the window number @p b's servo_window names attached as its chw, FAST's
 * channel from the ring's window with @p ring.  Control task. */
void sense_win_service(void);
void sense_win_attach(bench_state_t *b, bool ring);
/* The log's INA3221 columns for a file opened now (log_cols.h); false for
 * CH1 to CH3.  Any task. */
bool sense_log_cols(log_cols_t *out);
void tone_link_alerts(void);
void tone_link_service(void);
void kst_commands(void);
void kst_link_service(void);
void prog_link_service(void);
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

/* Where the tester's commands go, into s_drive for the SERVO screen: from
 * each pass of prog_link_service() and from the link-lost edge. */
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
extern atomic_ullong s_throttle_pins;
extern atomic_bool   s_throttle_pins_known;
extern atomic_uint   s_throttle_proto;

/* The OUTPUTS page as the IOmcu holds it; the OUTPUTS screen's write
 * asks for one straight afterwards. */
void read_outputs_binding(void);
void post_no_reading(void);

/* One pass of the poll: the bench page while the far end answers, the
 * identity page while it does not. */
bool poll_far_end(bool *link_up, bench_state_t *bench,
                  uint32_t *last_poll, uint32_t *last_status);
void advance_model_and_log(bool link_up, float emitted,
                           telemetry_sim_t *sim, bench_state_t *bench,
                           bool *new_sample);
void publish_snapshot(const bench_state_t *bench, bool link_up,
                      bool new_sample);
/* The IOmcu's STATUS fault bitmap at its last read, LINK_FAULT_* bits.
 * Control task only. */
uint16_t far_end_faults(void);

/* ------------------------------------------------------- main.c, continued */

/* The throttle's ramp, published by app_main for the control task. */
extern atomic_uint s_throttle_ramp;
void publish_throttle_ramp(void);
static inline uint16_t panel_throttle_ramp(void)
{
    return (uint16_t)atomic_load(&s_throttle_ramp);
}

/*
 * The bench page is polled every 50ms, so a sample is 1/20s of plot.  The
 * motor screen scales its time axis by the same number; the two have to
 * agree or the axis lies about how long ago something happened.
 */
#define PANEL_SAMPLE_HZ   20.0f

typedef enum { PANEL_CMD_MOTOR = 0, PANEL_CMD_SERVO,
               PANEL_CMD_STOP, PANEL_CMD_OUTPUTS,
               PANEL_CMD_SUPPLY, PANEL_CMD_PROG5V } panel_cmd_kind_t;

typedef struct {
    panel_cmd_kind_t kind;
    motor_cmd_t      motor;
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
    uint32_t         consumed_seq;
} panel_cmd_t;

/* What became of the last output-page write. */
extern atomic_int s_outputs_result;

/* The queues between the two tasks, and their items; see main.c. */
typedef struct {
    touch_event_t evt;
    uint32_t      seq;
    bool          lost;    /**< a loss notice, not an event */
} touch_item_t;
extern QueueHandle_t s_touch_q;   /**< control task -> app_main */
typedef struct {
    supply_state_t    st;
    servo_source_id_t source;
    uint32_t          changes;
    /* Why the INA3221 last stopped being the meter, and at which count
     * (servo_source_dropped()), as of the same poll. */
    servo_source_why_t dropped;
    uint32_t          dropped_at;
} supply_item_t;
extern QueueHandle_t s_supply_q;  /**< control task -> app_main */

/* STOP's request and its count; the let-goes asked for. */
extern atomic_bool s_stop_request;
extern atomic_uint s_stop_counted;  /**< s_touch_q number of a counted STOP, 0 none */
extern atomic_uint s_lets_go;

/* The supply's switch and set points, from the render side. */
extern atomic_bool s_supply_off_request;
extern atomic_uint s_supply_offs;
extern atomic_uint s_supply_set_mv;
extern atomic_uint s_supply_set_ma;
extern atomic_uint s_supply_vmax_mv;
extern atomic_uint s_supply_imax_ma;
extern atomic_uint s_supply_trip_ma;
extern atomic_uint s_supply_trip_mv;
extern atomic_uint s_supply_trip_ms;
extern atomic_uint s_supply_ack;
/** @brief The ports' 5V asked off from the PROG 5V box, counted; render
 *         side writes, control task reads. */
extern atomic_uint s_prog5v_offs;
/** @brief A PROG 5V OFF asked, whether or not its queue entry survives a
 *         full queue; render side sets, control task takes
 *         (prog5v_off_service()). */
extern atomic_bool s_prog5v_off_request;
/** @brief The PROG 5V ONs applied, numbered; control task writes, see
 *         prog_supply_watch_service(). */
extern atomic_uint s_psup_gen;
/** @brief The newest PROG 5V ON a whole frame has seen on; render side
 *         writes, see prog_supply_watch_service(). */
extern atomic_uint s_psup_ack;
extern atomic_bool s_supply_live;
extern atomic_uint s_supply_ons_sent;
extern atomic_uint s_supply_ons_taken;
extern atomic_uint s_pdmini_edits;
extern atomic_bool s_supply_real;

/* The touch stream's numbering and losses, and the arm watch; see
 * touch_loss.h. */
extern atomic_uint s_drv_gaps;
extern atomic_uint s_tq_published;
extern uint32_t    s_lost_notice_seq;
extern atomic_uint s_loss_gen;
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
extern uint32_t    s_arm_take_gen;
extern uint32_t    s_arm_take_drv;
extern arm_watch_t s_arm_watch;

/* A supply run's row timestamp, and the log's run edges; see main.c. */
extern float s_log_t;
void log_follow_runs(void);

/* The screens' commands, app_main -> control task; see main.c. */
extern QueueHandle_t s_cmd_q;

/* ---------------------------------------------------------- panel_supply.c */

/* The modelled supply and the PD mini's link. */
extern supply_sim_t   s_supply_sim;
extern supply_link_t  s_supply_link;
/* Set points the supply reported, for the screen to adopt. */
extern atomic_uint    s_supply_adopt_mv;
extern atomic_uint    s_supply_adopt_ma;
extern atomic_uint    s_supply_adopt_gen;
extern atomic_uint    s_supply_adopt_done;
/** @brief The last read of the supply: no port holds it and its output is
 *         off (supply_link_port_free_off()). */
extern atomic_bool    s_supply_free_off;
/* The ESC (electronic speed controller) model's profile and its runs. */
extern atomic_uintptr_t s_escsim_profile;
extern atomic_uint    s_escsim_runs;
extern bool           s_supply_page;
extern bool           s_supply_auto;
extern bool           s_supply_is_real;
extern supply_state_t s_supply;
extern uint32_t       s_supply_ms;
extern uint32_t       s_supply_stops_served;
extern uint32_t       s_supply_step_ms;
extern uint32_t       s_supply_gen;

void supply_log_tail(void);
void supply_switch(bool on);
void supply_follow_set(void);
/*
 * Every stop switches the output off: STOP, touch that died, an arm the
 * watch undid, the far end's refusal.  Called from control_pump(), which
 * runs inside every exchange's wait, and again wherever the supply is
 * stepped or switched, so a stop counted anywhere cuts the output before
 * the next reading is taken or logged.
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

/*
 * An OFF asked for, whether or not its queue entry survived; applying it
 * twice costs nothing.  Called before the drain, so an OFF is never left
 * waiting behind the queue's backlog.  And the set points, every pass.
 */
static inline void supply_service(void)
{
    if (atomic_exchange(&s_supply_off_request, false)) {
        supply_switch(false);
    }
    prog5v_off_service();
    supply_follow_set();
}

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
/* The supply's service, from control_pump(). */
void supply_pump(void);
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
 *        the held map has watching the ports' 5V; control task.
 */
void prog5v_publish_reading(void);

/* ------------------------------------------------------------- panel_arm.c */

void control_setup(telemetry_sim_t *sim, bench_state_t *bench);
/* The bench's totals, and when they last counted. */
extern bench_totals_t s_totals;
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
void arm_watch_service(bool link_up);
/* The latched stop, and the act the arming policy asks for. */
void service_arming(bool link_up);
bool disarm_here(bool link_up);
/* What the screens asked for, in the order they asked it. */
void drain_commands(bool link_up, bench_state_t *bench);

/* ---------------------------------------------------------- panel_render.c */

void kst_ui_service(void);
/** @brief The Hitec programmer's model, once a frame on app_main. */
void hitec_ui_service(void);
extern unsigned s_tq_lost;
bool supply_live_or_coming(void);
/** @brief supply_live_or_coming(), or the programming ports' own supply
 *         live (s_psup_live): what the install, update and flash gates
 *         wait on.
 *  @return true while either supply is on or coming */
bool supplies_live_or_coming(void);
void publish_supply_levels(void);
void test_lines_service(void);
typedef struct {
    int            steps;
    ui_screen_id_t route;
    uint32_t       navigations;
} knob_turn_t;

knob_turn_t knob_take(void);
void knob_apply(const knob_turn_t *turn, bool frame_lost);
void flush_screen_commands(uint32_t stops_now);
bool drain_touch(uint32_t stops_now);
void outputs_apply(const outbind_t *b);

/* ------------------------------------------------------------- panel_art.c */

extern const art_flash_t *s_artflash;
/* A drawing fetch is under way; see art_begin(). */
extern bool               s_artbusy;
extern atomic_bool        s_keeping;
extern SemaphoreHandle_t  s_draw_lock;
extern uint8_t           *s_drawpub;
extern uint8_t           *s_drawshow;
void drawing_for_picker(uint16_t board);
void drawing_follow(void);
void art_keep_service(void);
/* The board drawing, a slice of a poll at a time. */
void art_stop(const char *why);
void art_begin(uint16_t board);
void art_slice(void);

/* ---------------------------------------------------------- panel_update.c */

/* An install about to write the flash asks the control task to stand
 * still (s_fw_quiet_req), and the control task says it does
 * (s_fw_quiet_ack).  False when wifi_access_init() failed (s_wifi_ok). */
extern atomic_bool s_fw_quiet_req;
extern atomic_bool s_fw_quiet_ack;
extern bool        s_wifi_ok;

/* An IOmcu of another protocol major answering the probe; control
 * task.  panel_poll.c. */
extern link_uph_mismatch_t s_mismatch;

/* Whether the IOmcu's update may start: see fw_coproc_may_start().
 * From any task. */
bool cu_may_start(void);
/* The IOmcu update's check, for an IOmcu that just answered. */
void cu_link_up(const link_msg_t *reply);
/* One pass of the IOmcu's update, before the poll: true while the
 * update holds the link.  Control task. */
bool cu_service(bool *link_up, bench_state_t *bench,
                uint32_t *last_poll, uint32_t *last_status);

/* Once a frame on app_main: WI-FI, the reminder, SETUP > UPDATES
 * and the IOmcu update's screen. */
void wifi_service(bool armed, uint32_t stops_now);
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
void cu_render_service(bool unsafe, bool link_up, const cu_snap_t *cs);
