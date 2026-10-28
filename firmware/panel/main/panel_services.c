/**
 * @file
 * @brief The link modules' services, one page of the IOmcu each: the current
 *        monitors (SENSE and SERVO_SENSE), the phase tap (TONE), the
 *        programmer for KST and Chaservo servos (KST) and the programming
 *        ports (PROG).  Each runs from the poll (panel_poll.c), on the control
 *        task.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "arming.h"
#include "kst_link.h"
#include "kst_prog.h"
#include "link_host.h"
#include "link_pages.h"
#include "outputs.h"
#include "panel.h"
#include "prog_link.h"
#include "sense_link.h"
#include "sense_page.h"
#include "servo_link.h"
#include "servo_screen.h"
#include "servo_source.h"
#include "servo_test_io.h"
#include "storage.h"
#include "tone_link.h"
#include "ui_text.h"

/* The current monitors' pages, SENSE and SERVO_SENSE (4.7); see
 * sense_link_service().  Control task only. */
sense_link_t   s_sense_link;
/* Which meter measures the servo rail, decided once per poll.  Control
 * task only. */
servo_source_t s_servo_source;
/* The phase tap's page, TONE (4.8); see tone_link_service().  Control task
 * only. */
tone_link_t    s_tone_link;
/*
 * The page of the programmer for KST and Chaservo servos, KST (4.12); see
 * kst_link_service().  Control task only.
 *
 * The programmer's model (kst_prog.h) runs in the render loop.  It hands
 * its commands over through s_kst_q and reads what became of them in the
 * snapshot: the number of the last command carried out with what
 * kst_link_start() said, and a count of the outcomes handed out with the
 * last of them.
 *
 * kst_commands(), kst_link_service() and kst_publish() are kept out of
 * line.  Inlined, their locals widen control_task()'s frame by 448 bytes,
 * and the task's deepest path, a report from inside an exchange, starts in
 * that frame.
 */
kst_link_t s_kst_link;
/*
 * The programming ports' page, PROG (4.13); see prog_link_service().
 * Control task only.  What the SERVO screen wants of it crosses as two
 * levels the render loop stores every frame, and the pin of port 0 goes
 * back as one for the OUTPUTS screen's picker.
 */
prog_link_t        s_prog_link;
atomic_uint        s_prog_want_target;
atomic_bool        s_prog_want_pwm;
atomic_uint        s_prog_pin = PROG_LINK_NO_PIN;
atomic_uint        s_prog_ports = PROG_PORTS_NONE;
_Static_assert(LINK_PG_PORTS_MAX <= 4u,
               "s_prog_ports packs the count and three more pins in 32 bits");
atomic_uint        s_drive =
    ((unsigned)PROG_LINK_TARGET_NO_LINK << 8) | PROG_LINK_NO_PIN;
_Static_assert(PROG_LINK_NO_PIN == SERVO_SCREEN_NO_PIN
               && PROG_LINK_TARGET_UNKNOWN == SERVO_SCREEN_TARGET_UNKNOWN
               && PROG_LINK_TARGET_NO_LINK == SERVO_SCREEN_TARGET_NO_LINK,
               "the drive crosses to the SERVO screen unchanged");
static uint32_t           s_kst_answer_id;
static kst_link_start_t   s_kst_answer;
static uint32_t           s_kst_outcomes;
static kst_link_outcome_t s_kst_outcome;
static uint32_t           s_kst_stops;    /* stops an abort was sent for */

/**
 * @brief Whether a servo programmer, KST or Hitec, has a port or an
 *        operation under way: the bench takes no arm then (kst_prog.h,
 *        hitec_prog.h), and the supply's set points are the port's.
 *
 * Control task only.  Read once a pass by kst_port_look(), before the arms
 * of the pass are served: no command of a programmer is taken between the
 * two.  A HITEC port another writer holds counts: the bench console's, or
 * one held over a panel restart, until RELEASE PORT closes it.
 */
bool s_prog_taken;

/* What app_main and the control task share of a START in the queue: the
 * model offers its pass, kst_commands() claims it, and a START the model
 * gave up is dropped there (kst_prog.h). */
kst_prog_gate_t s_kst_gate;

__attribute__((noinline)) void kst_port_look(void)
{
    kst_link_readout_t rd;
    kst_link_readout(&s_kst_link, &rd);
    s_prog_taken = kst_prog_port_taken(&rd) || hitec_taken();
}

/** @brief The KST outcomes kst_link_outcome_fault() names, counted as
 *         kst_publish() takes them.  Control task only. */
static uint32_t s_kst_faults;

bool prog_writing(void)
{
    return kst_link_writing(&s_kst_link, now_ms())
           || hitec_prog_gate_writing(&s_hitec_gate) || hitec_foreign();
}

uint32_t prog_faults(void)
{
    return s_kst_faults + hitec_prog_gate_faults(&s_hitec_gate);
}

/* The PROG page is not written and the tester commands no port while the
 * programmer has one (prog_link.h): by the link module as it stands now,
 * after the commands of the pass. */
static __attribute__((noinline)) void prog_hold_look(void)
{
    kst_link_readout_t rd;
    kst_link_readout(&s_kst_link, &rd);
    prog_link_hold(&s_prog_link, kst_prog_port_taken(&rd) || hitec_taken());
}

/* The part names as the alerts print them. */
static const char *const k_sense_part[2] = { "INA228", "INA3221" };

/* The sensor alert posted and not yet seen taken: its number in the alert
 * slot (0 none) and its event.  Control task only. */
static uint32_t s_sense_alert_gen;
static uint32_t s_sense_alert_ev;
/* The last alert posted about the set-up asked (SENSE_LINK_EV_SETUP),
 * kept until its cause is over: its number (0 none) and its event. */
static uint32_t s_sense_setup_gen;
static uint32_t s_sense_setup_ev;

/*
 * An alert about a set-up that is over is taken back: one the band shows
 * goes from it, one waiting for a frame is dropped and not put back.  A
 * refused value stepped back on SETUP is over once the IOmcu takes the
 * frame; unset pins and one address are over with the edit.
 */
static void alert_drop(uint32_t *setup_gen, uint32_t *alert_gen)
{
    control_alert_withdraw(*setup_gen);
    if (*alert_gen == *setup_gen) {
        *alert_gen = 0u;
    }
    *setup_gen = 0u;
}

/*
 * What the SENSE and SERVO_SENSE pages said that the operator is told.
 * The band shows one line and the snapshot holds one pending alert, so
 * this takes one event at a time, the most pressing first, and the next no
 * sooner than SENSE_LINK_EVENT_GAP_MS later (sense_link_event()): two parts
 * that both stop answering are both said, one after the other.
 *
 * An event is taken only into a free slot, and followed until a frame has
 * taken it.  Any other alert replaces it -- a NACKed control write's
 * "IOmcu disarmed" in the same pass, say -- and one replaced before
 * a frame took it goes back to the queue (sense_link_event_back()): its
 * read will not raise it again.
 */
void sense_link_alerts(void)
{
    if (s_sense_setup_gen != 0u
        && !sense_link_event_stands(&s_sense_link, s_sense_setup_ev)) {
        alert_drop(&s_sense_setup_gen, &s_sense_alert_gen);
    }
    snap_lock();
    const bool     slot_free = !s_snap.alert.pending;
    const uint32_t posted    = s_snap.alert.posted;
    const uint32_t taken     = s_snap.alert.taken;
    snap_unlock();
    if (s_sense_alert_gen != 0u) {
        if ((int32_t)(taken - s_sense_alert_gen) >= 0) {
            s_sense_alert_gen = 0u;                  /* shown */
        } else if (posted != s_sense_alert_gen) {
            sense_link_event_back(&s_sense_link, s_sense_alert_ev);
            s_sense_alert_gen = 0u;                  /* replaced unseen */
        } else {
            return;                                  /* not taken yet */
        }
    }
    if (!slot_free) {
        return;
    }
    const uint32_t ev = sense_link_event(&s_sense_link, now_ms());
    if (ev == 0u) {
        return;
    }
    s_sense_alert_ev = ev;
    char line[ALERT_MAX];
    if ((ev & SENSE_LINK_EV_STORE_OFF) != 0u) {
        s_sense_alert_gen = control_alert_numbered(TR(ALERT_STORE_OFF));
    }
    if ((ev & SENSE_LINK_EV_I3221_CLIPPED) != 0u) {
        /* The full scale in hundredths of an ampere, rounded: 1638mA on
         * the DAOKAI's 0.1Ohm reads 1.64A.  With a map the clipped
         * channel's own shunt. */
        const unsigned ch = sense_link_clipped_channel(&s_sense_link);
        uint16_t dmohm = sense_map_link_mirror_dmohm(&s_sense_map, ch);
        if (dmohm == 0u) {
            dmohm = sense_link_i3221_dmohm(&s_sense_link);
        }
        const uint32_t ca = (sense_i3221_full_scale_ma(dmohm) + 5u) / 10u;
        unsigned addr = sense_link_i3221_addr(&s_sense_link);
        if (addr == 0u) {
            addr = LINK_SN_I3221_ADDR_MIN;
        }
        snprintf(line, sizeof(line), TR(ALERT_SENSE_I3221_CLIPPED), addr, ch,
                 (unsigned)(ca / 100u), (unsigned)(ca % 100u));
        s_sense_alert_gen = control_alert_numbered(line);
    }
    if ((ev & SENSE_LINK_EV_I228_CLIPPED) != 0u) {
        s_sense_alert_gen = control_alert_numbered(TR(ALERT_SENSE_I228_CLIPPED));
    }
    static const uint32_t k_silent[2] = { SENSE_LINK_EV_I228_SILENT,
                                          SENSE_LINK_EV_I3221_SILENT };
    static const uint32_t k_wrong[2]  = { SENSE_LINK_EV_I228_WRONG,
                                          SENSE_LINK_EV_I3221_WRONG };
    for (unsigned p = 0u; p < 2u; ++p) {
        const sense_link_part_t part = (sense_link_part_t)p;
        const unsigned addr = sense_link_addr(&s_sense_link, part);
        if ((ev & k_silent[p]) != 0u) {
            /* With what did answer, where something did: a solder bridge
             * set otherwise, as often as not. */
            const unsigned found = sense_link_event_found(&s_sense_link, part);
            if (found != 0u) {
                snprintf(line, sizeof(line), TR(ALERT_SENSE_SILENT_FOUND),
                         k_sense_part[p], addr, found);
            } else {
                snprintf(line, sizeof(line), TR(ALERT_SENSE_SILENT),
                         k_sense_part[p], addr);
            }
            s_sense_alert_gen = control_alert_numbered(line);
        }
        if ((ev & k_wrong[p]) != 0u) {
            snprintf(line, sizeof(line), TR(ALERT_SENSE_WRONG), addr,
                     (unsigned)sense_link_event_id(&s_sense_link, part),
                     k_sense_part[p]);
            s_sense_alert_gen = control_alert_numbered(line);
        }
    }
    if ((ev & SENSE_LINK_EV_STUCK) != 0u) {
        s_sense_alert_gen = control_alert_numbered(TR(ALERT_SENSE_STUCK));
    }
    if ((ev & SENSE_LINK_EV_I3221_REFUSED) != 0u) {
        s_sense_alert_gen = control_alert_numbered(TR(ALERT_SENSE_I3221_SETUP));
    }
    if ((ev & SENSE_LINK_EV_I228_REFUSED) != 0u) {
        s_sense_alert_gen = control_alert_numbered(TR(ALERT_SENSE_I228_SETUP));
    }
    if ((ev & SENSE_LINK_EV_BUS_REFUSED) != 0u) {
        snprintf(line, sizeof(line), TR(ALERT_SENSE_PINS),
                 sense_link_sda(&s_sense_link), sense_link_scl(&s_sense_link));
        s_sense_alert_gen = control_alert_numbered(line);
    }
    if ((ev & SENSE_LINK_EV_SAME_ADDR) != 0u) {
        snprintf(line, sizeof(line), TR(ALERT_SENSE_SAME_ADDR),
                 (unsigned)s_sense_link.want[LINK_SN_I228_ADDR]);
        s_sense_alert_gen = control_alert_numbered(line);
    }
    if ((ev & SENSE_LINK_EV_PINS_UNSET) != 0u) {
        s_sense_alert_gen = control_alert_numbered(TR(ALERT_SENSE_PINS_UNSET));
    }
    if ((ev & SENSE_LINK_EV_NO_PAGE) != 0u) {
        s_sense_alert_gen = control_alert_numbered(TR(ALERT_NO_SENSE_PAGE));
    }
    if ((ev & SENSE_LINK_EV_ENC_OLD) != 0u) {
        s_sense_alert_gen = control_alert_numbered(TR(ALERT_ENC_OLD));
    }
    if ((ev & SENSE_LINK_EV_ENC_SILENT) != 0u) {
        s_sense_alert_gen = control_alert_numbered(TR(ALERT_ENC_SILENT));
    }
    if ((ev & SENSE_LINK_EV_ENC_MAGNET) != 0u) {
        /* The most pressing of what the last read said: no magnet, then
         * too weak, then too strong. */
        const uint16_t m = sense_link_enc_magnet(&s_sense_link);
        s_sense_alert_gen = control_alert_numbered(
            ((m & LINK_SN_ENC_MD) == 0u) ? TR(ALERT_ENC_NO_MAGNET)
            : ((m & LINK_SN_ENC_ML) != 0u) ? TR(ALERT_ENC_WEAK)
                                           : TR(ALERT_ENC_STRONG));
    }
    if ((ev & SENSE_LINK_EV_SETUP) != 0u) {
        s_sense_setup_gen = s_sense_alert_gen;
        s_sense_setup_ev  = ev;
    }
}

/*
 * The output encoder's readings to the SERVO screen: one per SENSE read
 * that moved the sample count, so a run sees each angle once, and one
 * "none" when the angle stops being readable, and another when its reason
 * changes between "no magnet" and any other.  sense_link_enc() hands out
 * an angle only while the sensor detects its magnet.  Control task only.
 */
static void enc_queue(void)
{
    static bool     given;
    static bool     was_valid;
    static bool     was_no_magnet;
    static uint16_t last_samples;
    static uint32_t last_taken;
    sense_link_enc_t e;
    servo_test_enc_t q;
    memset(&q, 0, sizeof(q));
    if (sense_link_enc(&s_sense_link, now_ms(), &e)) {
        /* The same read is not sent twice: the page is read every 40ms
         * and the count moves 20 times in that. */
        if (given && was_valid && e.taken_ms == last_taken
            && e.samples == last_samples) {
            return;
        }
        q.valid    = true;
        q.raw      = e.raw;
        q.still_ms = e.still_ms;
        q.taken_ms = e.taken_ms;
        q.weak     = e.weak;
        q.strong   = e.strong;
        given        = true;
        was_valid    = true;
        last_taken   = e.taken_ms;
        last_samples = e.samples;
    } else {
        q.no_magnet = sense_link_enc_no_magnet(&s_sense_link, now_ms());
        if (given && !was_valid && q.no_magnet == was_no_magnet) {
            return;
        }
        given         = true;
        was_valid     = false;
        was_no_magnet = q.no_magnet;
    }
    if (xQueueSend(s_enc_q, &q, 0) != pdTRUE) {
        /* The screen has not drained 8 readings (320ms): what is queued
         * may hold the only "none" marker, and dropping one entry would
         * keep the readings after it.  All of it goes, and the reading
         * that follows says readings were lost. */
        servo_test_enc_t stale;
        while (xQueueReceive(s_enc_q, &stale, 0) == pdTRUE) { }
        q.gap = true;
        (void)xQueueSend(s_enc_q, &q, 0);
    }
}

/* The servo meter's alert posted and not yet seen taken: its number in the
 * alert slot (0 none) and its event.  Control task only. */
static uint32_t s_source_alert_gen;
static uint32_t s_source_alert_ev;

/*
 * What the choice of the servo rail's meter says to the operator, one
 * event at a time into a free alert slot and followed until a frame has
 * taken it, as sense_link_alerts() does for the current monitors.
 */
static void servo_source_alerts(void)
{
    snap_lock();
    const bool     slot_free = !s_snap.alert.pending;
    const uint32_t posted    = s_snap.alert.posted;
    const uint32_t taken     = s_snap.alert.taken;
    snap_unlock();
    if (s_source_alert_gen != 0u) {
        if ((int32_t)(taken - s_source_alert_gen) >= 0) {
            s_source_alert_gen = 0u;                 /* shown */
        } else if (posted != s_source_alert_gen) {
            servo_source_event_back(&s_servo_source, s_source_alert_ev);
            s_source_alert_gen = 0u;                 /* replaced unseen */
        } else {
            return;                                  /* not taken yet */
        }
    }
    if (!slot_free) {
        return;
    }
    const uint32_t ev = servo_source_event(&s_servo_source, now_ms());
    if (ev == 0u) {
        return;
    }
    s_source_alert_ev = ev;
    if ((ev & SERVO_SOURCE_EV_OLD) != 0u) {
        s_source_alert_gen = control_alert_numbered(TR(ALERT_SERVO_METER_OLD));
    } else {
        s_source_alert_gen = control_alert_numbered(TR(ALERT_I3221_RESET));
    }
}

/*
 * The current monitors' pages: the set-up SETUP names written when it
 * differs from what SENSE holds, the identity read again after a write is
 * taken, and SENSE's, SERVO_SENSE's and SERVO_WIN's readings read at their
 * rates (sense_link.h).  Then the servo rail's meter is chosen from what
 * was read (servo_source.h).  From poll_bench(), after the control write,
 * so @p idle -- the bank disarmed here and ARM written 0 there -- is this
 * pass's.  An IOmcu older than 4.7 is sent nothing, and one older
 * than 4.11 nothing on SERVO_WIN.
 *
 * With the INA228 as BENCH's source, @p bench gets the ESC's own figures
 * from the last SENSE read, the INA228's clipped flag, and its totals in
 * SENSE's finer steps while that read is younger than two polls.
 */
/* The INTERFACES rows the IOmcu does not hold, for SETUP's marks:
 * sense_link_unheld() and tone_link_unheld() as of the last poll, and 0
 * with the link down.  Control task writes, app_main reads. */
atomic_uint s_sense_unheld;
atomic_uint s_tone_unheld;

hitec_rx_link_t s_hitec_rx_link;
atomic_uint     s_hitec_rx_want;
atomic_uint     s_hitec_rx_mark;

/*
 * Hitec D receive to the HITEC page's register 23 (hitec_rx_link.h): one
 * write a poll while it is owed, armed or not, since the IOmcu takes it at
 * its next OPEN and nothing that runs changes.  From poll_bench(); an
 * IOmcu older than 4.24 is sent nothing.
 */
void hitec_rx_service(void)
{
    uint16_t v = 0u;

    hitec_rx_link_want(&s_hitec_rx_link,
                       (uint8_t)atomic_load(&s_hitec_rx_want));
    if (hitec_rx_link_next(&s_hitec_rx_link, now_ms(), &v)) {
        link_msg_t reply = { 0 };
        const bool answered = write_regs(&s_host, LINK_PAGE_HITEC,
                                         LINK_HT_W_RX_MODE, 1, &v, &reply);
        int result = HITEC_RX_LINK_NO_ANSWER;
        if (answered) {
            result = (reply.op == LINK_OP_NACK) ? (int)reply.regs[0]
                                                : HITEC_RX_LINK_ACK;
        }
        hitec_rx_link_done(&s_hitec_rx_link, result, now_ms());
    }
    atomic_store(&s_hitec_rx_mark,
                 (unsigned)hitec_rx_link_mark(&s_hitec_rx_link));
}

void sense_link_service(bool idle, bench_state_t *bench)
{
    const sense_setup_t w = sense_wanted();
    sense_link_want(&s_sense_link, &w, now_ms());
    for (int k = 0; k < 6; ++k) {
        sense_link_op_t op;
        if (!sense_link_next(&s_sense_link, now_ms(), idle, &op)) {
            break;
        }
        link_msg_t reply = { 0 };
        const bool answered =
            op.write ? write_regs(&s_host, op.page, op.off, op.n, op.regs,
                                  &reply)
                     : read_regs(&s_host, op.page, op.off, op.n, &reply);
        int result = SENSE_LINK_NO_ANSWER;
        if (answered) {
            result = (reply.op == LINK_OP_NACK) ? (int)reply.regs[0]
                                                : SENSE_LINK_ACK;
        }
        sense_link_done(&s_sense_link, result,
                        (answered && reply.op == LINK_OP_DATA) ? reply.regs
                                                               : NULL,
                        now_ms());
        if (result == SENSE_LINK_NO_ANSWER) {
            break;
        }
    }
    uint16_t caps = 0u;
    if (sense_link_take_caps(&s_sense_link, &caps)) {
        atomic_store(&s_capabilities, (unsigned)caps);
    }
    sense_link_alerts();
    atomic_store(&s_enc_held, sense_link_enc_on(&s_sense_link));
    atomic_store(&s_sense_unheld,
                 (unsigned)sense_link_unheld(&s_sense_link));
    enc_queue();
    sense_link_meter_t meter;
    sense_link_meter(&s_sense_link, &meter);
    (void)servo_source_step(&s_servo_source, now_ms(), true, s_far_minor,
                            &meter);
    servo_source_alerts();

    const bool sensed = (bench->flags & (uint16_t)LINK_BN_SENSED) != 0u;
    bool v_ok = false;
    bool i_ok = false;
    float volts = 0.0f;
    float amps = 0.0f;
    if (sensed) {
        (void)sense_link_esc(&s_sense_link, now_ms(), &v_ok, &volts, &i_ok,
                             &amps);
    }
    bench_state_set_esc(bench, v_ok, volts, i_ok, amps,
                        sensed && (sense_link_flags(&s_sense_link)
                                   & LINK_SN_I228_CLIPPED) != 0u);
    /* The totals from the last SENSE read while it is younger than two
     * polls: one read that went unanswered keeps the finer figure rather
     * than stepping back to BENCH's rounding for a sample. */
    int32_t charge = 0;
    uint32_t energy = 0u;
    if (sensed
        && sense_link_totals(&s_sense_link, now_ms(), &charge, &energy)) {
        bench_state_fine_totals(bench, charge, energy);
    }
}

/* The tone alert posted and not yet seen taken: its number in the alert
 * slot (0 none) and its event.  Control task only. */
static uint32_t s_tone_alert_gen;
static uint16_t s_tone_alert_ev;
/* The last alert posted about the set-up asked (TONE_LINK_EV_SETUP), kept
 * until its cause is over, as s_sense_setup_gen is. */
static uint32_t s_tone_setup_gen;
static uint16_t s_tone_setup_ev;

/*
 * What the TONE page said that the operator is told, one event at a time
 * into a free alert slot and followed until a frame has taken it, as
 * sense_link_alerts() does for the current monitors.
 */
void tone_link_alerts(void)
{
    if (s_tone_setup_gen != 0u
        && !tone_link_event_stands(&s_tone_link, s_tone_setup_ev)) {
        alert_drop(&s_tone_setup_gen, &s_tone_alert_gen);
    }
    snap_lock();
    const bool     slot_free = !s_snap.alert.pending;
    const uint32_t posted    = s_snap.alert.posted;
    const uint32_t taken     = s_snap.alert.taken;
    snap_unlock();
    if (s_tone_alert_gen != 0u) {
        if ((int32_t)(taken - s_tone_alert_gen) >= 0) {
            s_tone_alert_gen = 0u;                   /* shown */
        } else if (posted != s_tone_alert_gen) {
            tone_link_event_back(&s_tone_link, s_tone_alert_ev);
            s_tone_alert_gen = 0u;                   /* replaced unseen */
        } else {
            return;                                  /* not taken yet */
        }
    }
    if (!slot_free) {
        return;
    }
    const uint16_t ev = tone_link_event(&s_tone_link, now_ms());
    if (ev == 0u) {
        return;
    }
    s_tone_alert_ev = ev;
    char line[ALERT_MAX];
    if ((ev & TONE_LINK_EV_NO_PAGE) != 0u) {
        s_tone_alert_gen = control_alert_numbered(TR(ALERT_NO_TONE_PAGE));
    } else if ((ev & TONE_LINK_EV_PIN_REFUSED) != 0u) {
        snprintf(line, sizeof(line), TR(ALERT_TONE_PIN),
                 tone_link_pin(&s_tone_link), tone_link_f_min(&s_tone_link),
                 tone_link_f_max(&s_tone_link));
        s_tone_alert_gen = control_alert_numbered(line);
    } else if ((ev & TONE_LINK_EV_SETUP_REFUSED) != 0u) {
        s_tone_alert_gen = control_alert_numbered(TR(ALERT_TONE_SETUP));
    } else if ((ev & TONE_LINK_EV_PIN_BUSY) != 0u) {
        snprintf(line, sizeof(line), TR(ALERT_TONE_BUSY),
                 tone_link_pin(&s_tone_link));
        s_tone_alert_gen = control_alert_numbered(line);
    } else if ((ev & TONE_LINK_EV_OVERRUN) != 0u) {
        s_tone_alert_gen = control_alert_numbered(TR(ALERT_TONE_OVERRUN));
    }
    if ((ev & TONE_LINK_EV_SETUP) != 0u) {
        s_tone_setup_gen = s_tone_alert_gen;
        s_tone_setup_ev  = ev;
    }
}

/*
 * The phase tap's page: the set-up SETUP names written when it differs
 * from what TONE holds, registers 8 to 23 read at 20Hz and the beeps
 * taken by number (tone_link.h).  From poll_bench(); an IOmcu older
 * than 4.8 is sent nothing.  A tap change is taken armed or not, so no
 * idle gate.
 */
void tone_link_service(void)
{
    const tone_setup_t w = tone_wanted();
    tone_link_want(&s_tone_link, &w, now_ms());
    for (int k = 0; k < 8; ++k) {
        tone_link_op_t op;
        if (!tone_link_next(&s_tone_link, now_ms(), &op)) {
            break;
        }
        link_msg_t reply = { 0 };
        const bool answered =
            op.write ? write_regs(&s_host, op.page, op.off, op.n, op.regs,
                                  &reply)
                     : read_regs(&s_host, op.page, op.off, op.n, &reply);
        int result = TONE_LINK_NO_ANSWER;
        if (answered) {
            result = (reply.op == LINK_OP_NACK) ? (int)reply.regs[0]
                                                : TONE_LINK_ACK;
        }
        tone_link_done(&s_tone_link, result,
                       (answered && reply.op == LINK_OP_DATA) ? reply.regs
                                                              : NULL,
                       now_ms());
        if (result == TONE_LINK_NO_ANSWER) {
            break;
        }
    }
    tone_link_alerts();
    atomic_store(&s_tone_unheld, (unsigned)tone_link_unheld(&s_tone_link));
}

/**
 * @brief The programmer's commands, every pass and whatever the link does: a
 * command with no link is answered KST_LINK_START_DOWN, and the model says
 * so.  An ENTER or a REBOOT on an armed bench is answered BUSY and not
 * sent.
 *
 * And STOP.  Every stop this end latches, pressed or the bench's own, ends
 * an operation: kst_link_abort() drops a command not sent yet, sends no
 * command again that went without an answer, and sends ABORT for one the
 * IOmcu runs.  After the commands, so a start queued before the stop
 * is dropped by it.
 */
__attribute__((noinline)) void kst_commands(void)
{
    kst_prog_cmd_t cmd;
    while (xQueueReceive(s_kst_q, &cmd, 0) == pdTRUE) {
        if (cmd.kind == KST_PROG_CMD_START
            && !kst_prog_gate_claim(&s_kst_gate, cmd.id)) {
            /* Given up by the model while it waited here: not carried
             * out and not answered. */
            continue;
        }
        if (cmd.kind == KST_PROG_CMD_START) {
            /* No port is taken on an armed bench: nothing is sent.  A
             * REBOOT is the CONNECT that switches the supply off first.
             * No write goes out once an error's cut has switched the
             * port's supply off (port_cut_blocking()). */
            const bool writes = cmd.req.op == LINK_KST_OP_WRITE
                                || cmd.req.op == LINK_KST_OP_RESTORE
                                || cmd.req.op == LINK_KST_OP_RELEASE;
            s_kst_answer    = (((cmd.req.op == LINK_KST_OP_ENTER
                                 || cmd.req.op == LINK_KST_OP_REBOOT)
                                && outputs_armed(&s_out))
                               || (writes && port_cut_blocking()))
                                  ? KST_LINK_START_BUSY
                                  : kst_link_start(&s_kst_link, &cmd.req);
            s_kst_answer_id = cmd.id;
        } else if (cmd.kind == KST_PROG_CMD_FETCH) {
            (void)kst_link_fetch(&s_kst_link);
        } else {
            kst_link_abort(&s_kst_link);
        }
    }
    const uint32_t stops = arming_stop_count(&s_arm);
    if (stops != s_kst_stops) {
        s_kst_stops = stops;
        kst_link_abort(&s_kst_link);
    }
    prog_hold_look();
}

/*
 * The programmer's page: status read every KST_LINK_POLL_MS (50ms) while
 * an operation runs and every KST_LINK_IDLE_MS (500ms) otherwise, a
 * request's staged registers and command written, the results and the
 * images read (kst_link.h).  From poll_bench(), at most 4 exchanges a pass;
 * an IOmcu older than 4.12 is sent nothing.
 */
__attribute__((noinline)) void kst_link_service(void)
{
    for (int k = 0; k < 4; ++k) {
        kst_link_xfer_t x;
        if (!kst_link_next(&s_kst_link, now_ms(), &x)) {
            break;
        }
        link_msg_t reply = { 0 };
        const bool answered =
            x.write ? write_regs(&s_host, x.page, x.off, x.n, x.regs, &reply)
                    : read_regs(&s_host, x.page, x.off, x.n, &reply);
        int result = KST_LINK_NO_ANSWER;
        if (answered) {
            result = (reply.op == LINK_OP_NACK) ? (int)reply.regs[0]
                                                : KST_LINK_ACK;
        }
        kst_link_done(&s_kst_link, result,
                      (answered && reply.op == LINK_OP_DATA) ? reply.regs
                                                             : NULL);
        if (result == KST_LINK_NO_ANSWER) {
            break;
        }
    }
}

/* One read of the programming ports' page, into s_prog_link. */
static __attribute__((noinline)) void prog_page_read(void)
{
    link_msg_t reply = { 0 };
    const bool read = read_regs(&s_host, LINK_PAGE_PROG, 0u,
                                prog_link_regs(&s_prog_link), &reply)
                      && reply.op == LINK_OP_DATA;
    prog_link_read(&s_prog_link, read ? reply.regs : NULL, now_ms());
    /* And the HITEC page's STATE: a session the bench console opened
     * holds a port the PROGRAM list leaves alone. */
    if (prog_link_hitec_due(&s_prog_link)) {
        const bool st = read_regs(&s_host, LINK_PAGE_HITEC, LINK_HT_STATE,
                                  1u, &reply)
                        && reply.op == LINK_OP_DATA;
        prog_link_hitec_read(&s_prog_link, st ? reply.regs : NULL);
    }
}

/*
 * The programming ports' page: read every PROG_LINK_POLL_MS (500ms), and
 * at once after a written ask, and at most 1 write a pass towards what the
 * SERVO screen wants and the operator asked of a port's PIN or FUNC
 * (prog_link.h).
 * From poll_bench(); an IOmcu older than 4.13 is sent nothing.
 *
 * A pulse bit goes off whatever the bench does.  The target changes, and a
 * pulse comes on, only on a bench at rest: disarmed, no sweep, no hold, no
 * position held and no release owed.  A position still out is let go first,
 * so the release reaches the target that renders it.
 */
__attribute__((noinline)) void prog_link_service(void)
{
    prog_hold_look();
    prog_link_want(&s_prog_link, (uint8_t)atomic_load(&s_prog_want_target),
                   atomic_load(&s_prog_want_pwm));
    /* The read in a frame of its own, apart from the one the SERVO
     * screen's writes take after it (servo_link_prog_service()). */
    if (prog_link_read_due(&s_prog_link, now_ms())) {
        prog_page_read();
    }
    servo_link_prog_service();
    atomic_store(&s_prog_pin, prog_link_pin(&s_prog_link, 0u));
    unsigned ports = prog_link_count(&s_prog_link);
    for (uint8_t port = 1; port < LINK_PG_PORTS_MAX; ++port) {
        ports |= (unsigned)prog_link_pin(&s_prog_link, port) << (8u * port);
    }
    atomic_store(&s_prog_ports, ports);
    prog_ports_publish();
    prog_drive_publish();
    servo_link_surfaces_publish();
}

/**
 * @brief What the programmer's model is fed, as it stands: the PD mini's
 *        output, its set voltage and its current limit where it is the
 *        supply, answers and is on.  Under the snapshot's lock.
 * @param f the feed
 */
__attribute__((noinline)) void kst_publish(kst_prog_feed_t *f)
{
    kst_link_outcome_t o;
    if (kst_link_outcome(&s_kst_link, &o)) {
        s_kst_outcome = o;
        ++s_kst_outcomes;
        if (kst_link_outcome_fault(&o)) {
            /* An error: taken by port_cut_follow() in the next pump. */
            ++s_kst_faults;
        }
    }
    f->now_ms = now_ms();
    kst_link_readout(&s_kst_link, &f->rd);
    f->have_image  = kst_link_image(&s_kst_link, &f->image);
    f->have_backup = kst_link_backup(&s_kst_link, &f->backup);
    f->answer_id   = s_kst_answer_id;
    f->answer      = s_kst_answer;
    f->outcomes    = s_kst_outcomes;
    f->outcome     = s_kst_outcome;
    f->stopped     = arming_stopped(&s_arm);
    f->armed       = outputs_armed(&s_out);
    f->card        = storage_mounted();
    f->card_state  = (uint8_t)storage_card_state();
    f->target      = prog_link_target(&s_prog_link);
    /* The pin of each programming port, for the journal of a write. */
    f->port_pin_known = 0u;
    for (uint8_t port = 0; port < LINK_PG_PORTS_MAX; ++port) {
        const uint8_t pin = prog_link_pin(&s_prog_link, port);
        f->port_pin[port] = pin;
        if (pin != PROG_LINK_NO_PIN) {
            f->port_pin_known |= (uint8_t)(1u << port);
        }
    }
    /* The PD mini's output while it is the supply, answers and is on. */
    f->supply_read = s_supply_is_real && s_supply.online && s_supply.output;
    f->out_mv      = 0u;
    f->out_ma      = 0u;
    f->set_mv      = 0u;
    f->set_ma      = 0u;
    if (f->supply_read) {
        const float set = s_supply.set_v * 1000.0f + 0.5f;
        const float lim = s_supply.set_i * 1000.0f + 0.5f;

        f->set_mv = set <= 0.0f ? 0u
                  : set >= 65535.0f ? 65535u : (uint16_t)set;
        f->set_ma = lim <= 0.0f ? 0u
                  : lim >= 65535.0f ? 65535u : (uint16_t)lim;
        const float mv = s_supply.v * 1000.0f + 0.5f;
        const float ma = s_supply.i * 1000.0f + 0.5f;
        f->out_mv = mv <= 0.0f ? 0u : mv >= 65535.0f ? 65535u : (uint16_t)mv;
        f->out_ma = ma <= 0.0f ? 0u : ma >= 65535.0f ? 65535u : (uint16_t)ma;
    }
}
