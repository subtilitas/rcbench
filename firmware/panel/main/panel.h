/*
 * What the panel's files share: firmware/panel/main/main.c and the files
 * cut from it.  One section per file, each holding what that file defines
 * and another file uses.  A file's own state stays static in that file.
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

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "arming.h"
#include "bench_state.h"
#include "bind_link.h"
#include "busfault_screen.h"
#include "coproc_update_screen.h"
#include "kst_link.h"
#include "kst_prog.h"
#include "link_bringup.h"
#include "link_host.h"
#include "link_pages.h"
#include "link_port.h"
#include "log_cadence.h"
#include "log_viewer_screen.h"
#include "outputs.h"
#include "prog_link.h"
#include "safety_gate.h"
#include "sense_link.h"
#include "servo_page.h"
#include "servo_screen.h"
#include "servo_source.h"
#include "servo_test_io.h"
#include "supply.h"
#include "supply_link.h"
#include "telemetry_sim.h"
#include "tone_link.h"
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

/* The link to the coprocessor, used during bring-up and in the loop. */
extern link_host_t    s_host;
/* A link_cap_t bitmap from the coprocessor's identity page; see main.c. */
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

/* What the coprocessor says its outputs are, both under s_snap_lock; the
 * state of the last reading, for the control task alone. */
extern bind_reading_t s_outputs_read;
extern bool           s_outputs_read_fresh;
extern bind_read_t    s_bind_read;
/* The coprocessor's protocol minor, and its board identity. */
extern uint16_t       s_far_minor;
extern uint16_t       s_board;

/* A drawing fetch is under way; see art_begin(). */
extern bool s_artbusy;

extern QueueHandle_t s_kst_q;     /**< app_main -> control task */
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
 * The coprocessor's update as the render side needs it: an offer, numbered,
 * with the version the coprocessor runs; while an update runs, its step and
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
    char          alert[ALERT_MAX];
    bool          alert_pending;
    /* Every alert posted is numbered, and the render side records the
     * number of the one it took: an alert posted and replaced before a
     * frame took it was never shown.  See sense_link_alerts(). */
    uint32_t      alert_gen;
    uint32_t      alert_taken;
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
    /* The coprocessor's update; see cu_service(). */
    cu_snap_t     cu;
} panel_snap_t;
extern panel_snap_t s_snap;

static inline void snap_lock(void)   { xSemaphoreTake(s_snap_lock, portMAX_DELAY); }
static inline void snap_unlock(void) { xSemaphoreGive(s_snap_lock); }

/* Called from the control task, which must not touch the router.  The
 * slot holds one alert; a later one replaces it.  Returns its number. */
uint32_t control_alert_numbered(const char *text);

/* The same, for the callers that do not follow it.  Inlined where it is
 * called, as the compiler chose before the numbering: a frame of its own
 * puts 32 bytes on the control task's deepest chain (drv_lost() through
 * control_pump()) and moves stack_check's walk onto a deeper path through
 * ESP-IDF's logging. */
__attribute__((always_inline)) static inline void
control_alert(const char *text)
{
    snap_lock();
    snprintf(s_snap.alert, sizeof(s_snap.alert), "%s", text);
    s_snap.alert_pending = true;
    ++s_snap.alert_gen;
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

/* The pole count the coprocessor holds, and whether it is current. */
extern atomic_bool s_poles_owed;
extern atomic_uint s_poles_value;

/* The throttle endpoints the coprocessor holds: owed, held disarmed at a
 * link-up, and the time of the last edit.  The debt is paid once the value
 * has rested for ENDPOINTS_SETTLE_MS. */
#define ENDPOINTS_SETTLE_MS 300u
extern atomic_bool s_endpoints_owed;
extern bool        s_endpoints_hold;
extern atomic_uint s_endpoints_value;
extern atomic_uint s_endpoints_at_ms;

static inline uint16_t endpoints_min(void)
{
    return (uint16_t)(atomic_load(&s_endpoints_value) >> 16);
}

static inline uint16_t endpoints_max(void)
{
    return (uint16_t)(atomic_load(&s_endpoints_value) & 0xFFFFu);
}

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

/* What SETUP INTERFACES left for the current monitors and the phase tap,
 * read by the control task. */
sense_setup_t   sense_wanted(void);
tone_setup_t    tone_wanted(void);

/* False until the control task owns the safety state. */
extern bool s_pump_live;
/* The safety loop, from the top of the control task and from inside every
 * exchange. */
void control_pump(void);

/* The board drawing, a slice of a poll at a time. */
void art_stop(const char *why);
void art_begin(uint16_t board);
void art_slice(void);

/* The supply. */
extern supply_link_t  s_supply_link;
extern bool           s_supply_page;
extern bool           s_supply_auto;
extern bool           s_supply_is_real;
extern supply_state_t s_supply;
extern uint32_t       s_supply_gen;
void supply_link_service(void);

/* The bench's totals, and when they last counted. */
extern bench_totals_t s_totals;
extern uint32_t       s_totals_ms;

/* The coprocessor update's check, for a coprocessor that just answered. */
void cu_link_up(const link_msg_t *reply);

/* The servo's wire handling; see main.c. */
#define SERVO_HZ_UNKNOWN 0xFFFFu  /* not known: perhaps a faster rate        */
extern servo_cmd_t   s_servo_held;
extern bool          s_servo_release_owed;
extern uint8_t       s_servo_channels;
extern atomic_ullong s_servo_pins;
extern atomic_bool   s_servo_pins_known;
extern atomic_bool   s_enc_held;
extern bool          s_servo_known;
extern uint8_t       s_servo_written;
extern servo_cmd_t   s_servo_range;
extern bool          s_servo_sweep_page;
extern bool          s_servo_sweeping;
extern bool          s_servo_holding;
extern bool          s_servo_sweep_unknown;
extern atomic_bool   s_servo_sweep_able;
extern uint16_t      s_servo_minor;
extern servo_phase_t s_far_phase;
extern bool          s_hold_unanswered;
extern bool          s_servo_rate_page;
extern uint16_t      s_servo_hz_sent;
/* Paired with throttle_to_zero() in every path that stops the bench. */
void servo_let_go(void);
void servo_surfaces_publish(void);
bool servo_cfg_write(uint8_t port, uint8_t first, uint8_t count,
                     const uint16_t *cfg, link_msg_t *reply);
bool servo_named(const servo_cmd_t *sv);
void servo_cfg(const servo_cmd_t *sv, uint16_t *cfg);
void servo_rate_reset_note(bool page, bool landed);
bool servo_rate_reset(void);

/* ------------------------------------------------------------ panel_card.c */

#define CARD_DIR      ""          /* the root of the mount point */

/* Read once at start-up, from bring_up(). */
void esc_profiles_load(void);
/* The log viewer's way to the card. */
extern const log_viewer_io_t k_card_io;

/* The runs the logger has open, held back from the viewer. */
extern atomic_uint s_log_open_run;
extern atomic_uint s_test_open_run;
extern atomic_uint s_test_open_txt;

/* The programmer's record, KST.TXT: filled by the render loop, written by
 * the logger. */
extern char        s_kst_file[KST_PROG_FILE_MAX];
extern size_t      s_kst_file_len;
extern atomic_uint s_kst_file_sent;
extern atomic_uint s_kst_file_done;
extern atomic_bool s_kst_file_ok;
void kst_record_write(void);
void kst_record_load(bool late);

/* A servo file request: taken from the model by the render loop, run on
 * the card by the logger. */
extern kst_prog_card_req_t  s_kst_card_req;
extern kst_prog_card_done_t s_kst_card_done;
extern atomic_uint          s_kst_card_sent;
extern atomic_uint          s_kst_card_answered;
void kst_card_serve(void);

/* ------------------------------------------------------------- panel_log.c */

/*
 * A run is one arming of the bench, or one switch-on of the supply's output
 * while the bench is not armed.  The bench takes the log: arming during a
 * supply run ends that run, and a supply output still on at the disarm starts
 * a run of its own.  One run is one file, and a file holds one kind of row,
 * so its header names every column it has.
 */
typedef enum {
    LOG_RUN_NONE = 0,
    LOG_RUN_BENCH,
    LOG_RUN_SUPPLY,
} log_run_kind_t;

/*
 * One sample on its way to the card, filled by the control task.
 *
 * It carries which run it belongs to, because that is the only thing that
 * says which file it goes in: a disarm and an arm inside one pass of the
 * logger would otherwise put two runs in one file, with a time column that
 * goes backwards halfway down.
 */
typedef struct {
    uint32_t       run;
    float          t_s;
    log_run_kind_t kind;     /**< which of the two below */
    union {
        bench_state_t  bench;
        supply_state_t supply;
    } u;
} log_row_t;

/*
 * Sixty-four rows is 3.2 s of run at PANEL_SAMPLE_HZ, against the 250 ms an
 * SD card may take over one write: a card that pauses costs no rows.
 */
#define LOG_Q_LEN      64

/* The report link_report() writes to the card, and how many may be in
 * flight.  The report is made every 5 s at its fastest. */
#define LOG_NOTE_MAX   208
#define LOG_NOTE_Q_LEN 4

/* One line of a servo test's files, render loop -> logger. */
typedef struct {
    uint8_t kind;                        /* servo_test_out_t */
    char    text[SERVO_TEST_LINE_MAX];
} test_line_t;

#define TEST_Q_LEN 24

extern QueueHandle_t s_log_q;
extern QueueHandle_t s_note_q;
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
 * ARM and THROTTLE, the write every 50 ms poll and every disarm makes.
 *
 * Two registers rather than the three the frame that arms carries.  This
 * write keeps a running bench running and stops a stopping one, and the
 * pole count is neither: while a run is on, an edit to it is a correction
 * paid on its own transaction by poles_service(), and a disarm has nothing
 * to convert.  Kept apart so a count the far end refuses -- or one that has
 * not changed -- never stands between the throttle and the wire.
 *
 * True when the coprocessor acknowledged; a NACK (negative acknowledge) or
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
 * five edges, 100 to 125 ms at a 5 ms pass and longer on a slower one: a
 * fixed wait is right for one pass period and refused at the next.  One
 * register, one frame each way.
 *
 * @p answered is false when nobody answered.  That exchange has then waited
 * LINK_HOST_TIMEOUT_MS (1000 ms), past the arm's whole bound, and it is the
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
 * 5 s while the link is down and every 60 s while it is up. */
void link_lost_report(busfault_report_t *r);
void link_report(void);

/*
 * The seam between measured and modelled numbers.  When the coprocessor
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
/* Where the tester's commands go, for the SERVO screen's TARGET row:
 * prog_link_drive()'s pin in bits 0 to 7, its target in bits 8 to 15 and its
 * pulse in bit 16.  One word, so the screen never reads one drive's target
 * with another's pin or pulse.  No link until a coprocessor answers. */
#define DRIVE_PWM_BIT (1u << 16)
extern atomic_uint    s_drive;
/* Whether the servo programmer has a port, or an operation under way. */
extern bool            s_kst_taken;
extern kst_prog_gate_t s_kst_gate;
/* The SENSE and TONE set-ups that are not held at the far end. */
extern atomic_uint s_sense_unheld;
extern atomic_uint s_tone_unheld;

void kst_port_look(void);
void sense_link_alerts(void);
void sense_link_service(bool idle, bench_state_t *bench);
void tone_link_alerts(void);
void tone_link_service(void);
void kst_commands(void);
void kst_link_service(void);
void prog_link_service(void);
void kst_publish(kst_prog_feed_t *f);

/* Where the tester's commands go, into s_drive for the SERVO screen: from
 * each pass of prog_link_service() and from the link-lost edge. */
static inline void prog_drive_publish(void)
{
    const prog_link_drive_t drive = prog_link_drive(&s_prog_link);
    atomic_store(&s_drive, ((unsigned)drive.target << 8) | drive.pin
                           | (drive.pwm ? DRIVE_PWM_BIT : 0u));
}

/* ------------------------------------------------------------ panel_poll.c */

/* The GPIOs of the throttle's slots, and whether a binding was read. */
extern atomic_ullong s_throttle_pins;
extern atomic_bool   s_throttle_pins_known;

/* The OUTPUTS page as the coprocessor holds it; the OUTPUTS screen's write
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
