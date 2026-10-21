/*
 * The link to the IOmcu: exchange(), through which every request to
 * the far end goes, and the control page, which carries ARM, THROTTLE, the
 * pole count, CLEAR and the throttle's endpoints.  The link report is here
 * too.  Control task only, and app_main before that task exists.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "arming.h"
#include "busfault_screen.h"
#include "can_twai.h"
#include "link_bringup.h"
#include "link_host.h"
#include "link_pages.h"
#include "link_port.h"
#include "outputs.h"
#include "outputs_pages.h"
#include "panel.h"
#include "ui_text.h"

static const char *TAG = "rcbench";

/* ---------------------------------------------------- asking the far end */

/*
 * Every request to the far end goes through exchange(), and every way
 * exchange() ends without an answer goes through exchange_unanswered():
 * a frame that did not reach the wire, or a request that waited out
 * LINK_HOST_TIMEOUT_MS (1000 ms).  A refusal is an answer.  No caller
 * reports a missing answer to the link-loss rule and none can forget to.
 * Control task only, and app_main before that task exists.
 *
 * s_unanswered counts them.  An arm compares it across its own exchanges to
 * tell a far end that refused from a link that went quiet; see
 * arm_write_failed().
 *
 * s_link_is_up is whether a far end is held to be answering: set when an
 * identity probe is answered, cleared when poll_far_end() takes the link
 * down.
 *
 * s_link_quiet is set by an exchange that ended unanswered with the link
 * up, and is the only thing poll_far_end() reads to take the link down.
 * While it is set exchange() sends nothing and answers false at once, so
 * the rest of the pass does not wait out a timeout per exchange.
 */
uint32_t s_unanswered;
bool     s_link_is_up;
bool     s_link_quiet;
static void exchange_unanswered(void);

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
bool exchange(link_host_t *host, const link_msg_t *req,
              link_msg_t *reply)
{
    if (s_link_quiet) {
        /* The link went quiet earlier in this pass.  Not sent, not counted
         * and not judged again: the exchange that found it out was. */
        link_host_abandon(host);
        return false;
    }
    link_can_frame_t out[LINK_CAN_MAX_FRAMES];
    const size_t n = link_can_encode(req, out, LINK_CAN_MAX_FRAMES);
    if (n == 0) {
        link_host_abandon(host);
        exchange_unanswered();
        return false;
    }
    const uint32_t sent_us = (uint32_t)esp_timer_get_time();
    for (size_t i = 0; i < n; ++i) {
        if (!can_twai_send(&out[i], 5)) {
            /* Nothing reached the wire, so there is nothing to wait for.
             * Leaving it outstanding would refuse every later request. */
            link_host_abandon(host);
            exchange_unanswered();
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
            exchange_unanswered();
            return false;
        }
    }
}

/* exchange() and the clock, as shared/link asks for them. */
static bool port_exchange(void *ctx, link_host_t *host, const link_msg_t *req,
                          link_msg_t *reply)
{
    (void)ctx;
    return exchange(host, req, reply);
}

uint32_t port_now(void *ctx)
{
    (void)ctx;
    return now_ms();
}

/*
 * Between two frames of a write wider than one.  A page is 8 exchanges back
 * to back and a binding 17, and an exchange answered inside its first 5 ms
 * receive window returns without a pump.  The safety loop runs here once
 * 5 ms have passed since it last did: the heartbeat keeps its 20 ms edges
 * and STOP is read inside a frame of the press for as long as the write
 * takes.  A write of one frame never comes here.
 */
void port_between(void *ctx)
{
    static uint32_t pumped_ms;
    (void)ctx;
    if (s_pump_live && (uint32_t)(now_ms() - pumped_ms) >= 5u) {
        pumped_ms = now_ms();
        control_pump();
    }
}

const link_port_t k_port = {
    port_exchange, port_now, NULL, port_between,
};

static bool write_page(link_host_t *host, uint8_t page, uint8_t count,
                       const uint16_t *regs, link_msg_t *reply)
{
    return write_regs(host, page, 0, count, regs, reply);
}

/* ------------------------------------------------------- the control page */

/*
 * ARM and THROTTLE travel together, offset 0 and count 2, at every poll while
 * the link is up: the IOmcu's throttle channel goes to its rest, which
 * for a throttle is stopped, after OUT_DEFAULT_TIMEOUT_MS (500 ms) without a
 * write, so the panel keeps writing while armed.  The write that arms is
 * the same window widened to three, taking MOTOR_POLES (register 2) with
 * it.  CLEAR (register 3) is written on its own and only on an explicit
 * arm: a write that touches it must carry LINK_CLEAR_MAGIC, and it lifts a
 * latched failsafe, which no other write may do.
 */

/*
 * What the far end was last armed with, the ARM value of the frame that
 * armed it.  The control task's alone.
 */
uint16_t s_arm_value;

/*
 * The ARM value for the roles the policy asked for: the screen's own role
 * (MOTOR & ESC, a stick run included, the throttle; SERVO the surfaces).  An
 * IOmcu older than LINK_MINOR_ARM_ROLE takes LINK_ARM_ALL only, and
 * arms its whole bank on it.
 */
static uint16_t arm_value_wanted(void)
{
    return outputs_arm_value_for(s_far_minor, arming_roles(&s_arm));
}

/*
 * The frame that arms: ARM, THROTTLE and MOTOR_POLES, registers 0 to 2 of
 * the control page in one CAN frame, all-or-nothing at the far end.
 *
 * The count travels with the arm because it is the divisor the far end
 * starts sampling with: a run begun on a stale one puts a wrong speed into
 * its sticky rpm_max, which no later correction removes.  In the same frame
 * the run starts on the count that was sent, or does not start.
 *
 * The debt is taken before the value is loaded.  An edit landing after the
 * load raises it again on its own and is paid at the next poll, which is
 * right: the run started on the value that went out, and the poll corrects
 * it.  A write that does not land gives the debt back if one was taken, so
 * the next poll pays it.  The count cannot refuse an arm on its own: every
 * value the setting allows is inside the range the page takes, and an
 * IOmcu whose page differs never links up (see probe_identity()).
 */
bool control_arm(link_msg_t *reply)
{
    const bool owed = atomic_exchange(&s_poles_owed, false);
    const uint16_t arm = arm_value_wanted();
    const uint16_t regs[LINK_CT_ARM_FRAME] = {
        [LINK_CT_ARM]         = arm,
        [LINK_CT_THROTTLE]    = s_throttle_hundredths,
        [LINK_CT_MOTOR_POLES] = (uint16_t)atomic_load(&s_poles_value),
    };
    const bool ok = write_regs(&s_host, LINK_PAGE_CONTROL, LINK_CT_ARM,
                               LINK_CT_ARM_FRAME, regs, reply)
                    && reply->op == LINK_OP_ACK;
    if (!ok && owed) {
        atomic_store(&s_poles_owed, true);
    }
    if (ok) {
        s_arm_value = arm;
    }
    return ok;
}

/*
 * The magnet count of the motor under test, read from the setting at the
 * moment of the write.
 *
 * A bidirectional DShot ESC (electronic speed controller) reports electrical
 * periods and has no idea what it is bolted to, so this is the one number the
 * far end cannot work out and the near end already has, in the Motor poles
 * setting.  Until it arrives the IOmcu reports no speed at all rather
 * than a speed derived from a guess.
 */
static bool control_write_poles(link_msg_t *reply)
{
    const uint16_t poles = (uint16_t)atomic_load(&s_poles_value);
    return write_regs(&s_host, LINK_PAGE_CONTROL, LINK_CT_MOTOR_POLES, 1u,
                      &poles, reply)
           && reply->op == LINK_OP_ACK;
}

/*
 * Pay the debt, when one is owed and only then.
 *
 * The flag is taken before the write, so a value edited during the
 * transaction is owed again rather than dropped.
 *
 * A write nobody answered is put back and costs one more poll.  A write the
 * far end refused is not: the same request refused once is refused every
 * time, and a debt that stands through a refusal is a control write on every
 * 50 ms poll for as long as the link is up, against an IOmcu that has
 * already said no -- the missing-register case link_came_up() names is
 * exactly that. A refusal waits for a new edit or the next link-up edge.
 *
 * Returns whether the far end holds the current count.
 */
/* The pole count's refusal posted, kept until a count is taken: its
 * number in the alert slot, 0 none.  Control task only. */
static uint32_t s_poles_alert_gen;

bool poles_service(void)
{
    /*
     * One exchange, which can wait LINK_HOST_TIMEOUT_MS.  An edit made while
     * it is on the wire raises the debt again behind the value already
     * loaded and is paid at the next 50 ms poll; nothing waits on it here,
     * because the write that arms carries the count itself (control_arm())
     * and this is the path for a count edited at any other time.  A loop
     * until the debt clears would let an operator holding a key down keep
     * the control task on the wire for as long as the finger rests.
     */
    if (!atomic_exchange(&s_poles_owed, false)) {
        return true;
    }
    link_msg_t pr;
    memset(&pr, 0, sizeof(pr));
    if (!control_write_poles(&pr)) {
        if (pr.op == LINK_OP_NACK) {
            s_poles_alert_gen = control_alert_numbered(TR(ALERT_POLES));
            return false;
        }
        atomic_store(&s_poles_owed, true);
        return false;
    }
    /* A count taken: a refusal still on the band is over. */
    control_alert_withdraw(s_poles_alert_gen);
    s_poles_alert_gen = 0u;
    return true;
}

/*
 * Bring the throttle channels' endpoints at the far end to the settings.
 *
 * Read, change, write: the page is read back, the throttle channels take the
 * two values (outputs_chan_cfg_set_throttle_range()) and every other channel
 * keeps what it holds -- a servo's range the SERVO screen named included.
 * Nothing is written when nothing changed, which is the case at every
 * link-up after the first: the far end keeps the page in flash.
 *
 * Returns how it went; the alerts for a refusal are said here.
 */
/* The pulse range's refusal or order alert posted, kept until a pair is
 * taken or found held: its number in the alert slot, 0 none.  Control task
 * only. */
static uint32_t s_pulse_alert_gen;

static endpoints_outcome_t endpoints_pay(uint16_t lo, uint16_t hi)
{
    link_msg_t ccr;
    memset(&ccr, 0, sizeof(ccr));
    if (!poll_page(&s_host, LINK_PAGE_CHAN_CFG, LINK_CC_COUNT, &ccr)) {
        return ENDPOINTS_NO_ANSWER;
    }
    if (ccr.op == LINK_OP_NACK) {
        control_alert(TR(ALERT_PULSE_RANGE_UNKNOWN));
        return ENDPOINTS_READ_REFUSED;
    }
    uint16_t cfg[LINK_CC_COUNT];
    memcpy(cfg, ccr.regs, sizeof(cfg));
    if (!outputs_chan_cfg_set_throttle_range(cfg, lo, hi)) {
        /* Idle pulse and Full pulse overlap in their ranges (800 to 1600 and
         * 1400 to 2400 us), so the pair can be inverted while it is being
         * edited.  Said, and the far end keeps the last pair it took. */
        s_pulse_alert_gen = control_alert_numbered(TR(ALERT_PULSE_ORDER));
        return ENDPOINTS_ORDER;
    }
    if (memcmp(cfg, ccr.regs, sizeof(cfg)) == 0) {
        return ENDPOINTS_HELD;
    }
    link_msg_t reply;
    memset(&reply, 0, sizeof(reply));
    if (!write_page(&s_host, LINK_PAGE_CHAN_CFG, LINK_CC_COUNT, cfg, &reply)) {
        return ENDPOINTS_NO_ANSWER;
    }
    if (reply.op != LINK_OP_ACK) {
        s_pulse_alert_gen = control_alert_numbered(TR(ALERT_PULSE_REFUSED));
        return ENDPOINTS_WRITE_REFUSED;
    }
    /* The acknowledgement carries what the far end stored. */
    if (!endpoints_page_holds(reply.regs, lo, hi)) {
        return ENDPOINTS_DIFFERS;
    }
    return ENDPOINTS_TAKEN;
}

/*
 * The endpoint debt, once a poll; endpoints_take() decides when it is due
 * and endpoints_paid() what an attempt leaves.
 *
 * Only when @p far_disarmed: the control write of this same pass put
 * ARM = 0 at the far end and was acknowledged.  The bank's own flag is not
 * enough -- a disarm whose write never left leaves the far end driving
 * while this end reads disarmed -- and a pulse range that moves under a
 * running motor moves its throttle.  An edit made during a run waits for
 * the disarm; the hold after a link-up (endpoints_link_up()) is what
 * makes the far end disarmed for that pass on a bench armed here.
 *
 * A write nobody answered is owed again.  Any other answer ends the debt
 * and the hold; a refusal, or another pair stored, blocks a switch to
 * ARMED until a new edit, a binding or a link-up pays the pair
 * (endpoints_block()).  The alert for a refusal is said here, once; the
 * switch to ARMED says what to change.
 *
 * The debt is taken before the exchanges and given back on a missing
 * answer, so an edit the settings screen publishes while they run stays
 * owed.  A switch to ARMED asks it on this task (service_arming()), so
 * never between the taking and the outcome.
 */
void endpoints_service(bool far_disarmed)
{
    if (endpoints_take(&s_endpoints, far_disarmed, now_ms())
        != ENDPOINTS_PAY) {
        return;
    }
    const endpoints_outcome_t out = endpoints_pay(endpoints_min(&s_endpoints),
                                                  endpoints_max(&s_endpoints));
    if (out == ENDPOINTS_TAKEN || out == ENDPOINTS_HELD) {
        /* The pair asked is the far end's: a refusal or an inverted pair
         * still on the band is over. */
        control_alert_withdraw(s_pulse_alert_gen);
        s_pulse_alert_gen = 0u;
    }
    endpoints_paid(&s_endpoints, out);
}

/*
 * A stop the policy has just latched for the far end -- a refused ARM, a
 * link that went quiet, a far end that appeared under an armed bank -- as
 * it is finished at this end.
 *
 * The command goes to zero here rather than through the policy:
 * arming_stop_from_far_end() clears a->armed itself, and arming_step()'s
 * disarm is gated on a->armed, so ARMING_ACT_DISARM cannot follow and the
 * throttle would keep its last value, to be written again with the next
 * ARM = 1.
 */
void far_end_stop_here(void)
{
    outputs_arm(&s_out, false, now_ms());
    throttle_to_zero();
    servo_link_let_go();
    control_alert(TR(ALERT_COPRO_DISARMED));
}

/*
 * An arm that did not go through: a release or a rate the far end did not
 * take, a CLEAR or an arming frame that did not come back acknowledged.
 *
 * @p quiet_before is s_unanswered as the arm began.  Unchanged, every
 * exchange of the arm was answered and the far end refused: the panel stays
 * disarmed and says @p alert.  Changed, one of them waited out its timeout
 * or never reached the wire: the link went quiet under this arm, and the
 * far end may have taken a frame and lost only the acknowledgement.  That
 * is a stop, heartbeat withheld, and exchange_unanswered() has already made
 * it where the exchange ended; what is left here is the arm the policy
 * handed out with ARMING_ACT_ARM, which arming_link_lost() takes back
 * without counting a second stop.  A refusal is arming_refused().
 */
void arm_write_failed(uint32_t quiet_before, const char *alert)
{
    if (s_unanswered == quiet_before && !s_link_quiet) {
        (void)arming_write_failed(&s_arm, true);
        control_alert(alert);
    } else if (arming_link_lost(&s_arm, outputs_armed(&s_out))) {
        far_end_stop_here();
    }
}

/*
 * An exchange ended with no answer: exchange()'s one way out for it.
 *
 * What it means is arming_exchange_unanswered()'s decision, host-tested for
 * every kind of exchange, state and place in a pass.  With the link up the
 * link is gone, and s_link_quiet says so to the rest of the pass and to
 * poll_far_end(); under an armed bench or a waiting arm the stop is latched
 * here as well, where the exchange ended, so the bank and the heartbeat do
 * not wait for the poll.  With the link down it was a probe, and a bank
 * armed with no IOmcu -- the simulated bench -- runs on through it.
 * Only memory is touched; nothing here sends.
 */
static void exchange_unanswered(void)
{
    ++s_unanswered;
    switch (arming_exchange_unanswered(&s_arm, s_link_is_up,
                                       outputs_armed(&s_out))) {
    case ARMING_QUIET_STOP:
        far_end_stop_here();
        s_link_quiet = true;
        break;
    case ARMING_QUIET_LINK_DOWN:
        s_link_quiet = true;
        break;
    case ARMING_QUIET_NOTHING:
    default:
        break;
    }
}

/*
 * One block naming the most fundamental fault rather than the loudest.
 * Printed every 5 s while the link is down and every 60 s while it is up.
 */
/*
 * The same report, on its way to the card.
 *
 * A tester can send a file; a tester cannot send a console this board does
 * not have.  Only while the link is down and only at link_report()'s 5 s
 * cadence, so the volume is a few hundred bytes a minute.
 *
 * Handed to the logger task rather than written here.  This runs on the task
 * that beats the safety line, and an append is three card transactions --
 * open, write, close -- each of which can block for as long as the card
 * takes.  A queue that is full drops the line: a report is worth less than
 * the beat, and the next one is 5 s away.
 */
static void debug_log(const char *line)
{
    if (s_note_q == NULL) {
        return;
    }
    char note[LOG_NOTE_MAX];
    snprintf(note, sizeof(note), "%s", line);
    (void)xQueueSend(s_note_q, note, 0);
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
void link_lost_report(busfault_report_t *r)
{
    memset(r, 0, sizeof(*r));
    r->kind       = BUSFAULT_LINK_LOST;
    r->bus        = bus_state();
    /*
     * The timestamp is read once.  The control task clears it on the other
     * core the moment the link answers, and a second read that caught the
     * zero would make this now_ms() / 1000 -- the uptime, printed as how
     * long the link has been down.
     */
    const uint32_t lost_ms = atomic_load(&s_link_lost_ms);
    r->down_s     = lost_ms == 0u
                        ? 0u
                        : (uint32_t)(now_ms() - lost_ms) / 1000u;
    r->recoveries = s_recoveries;
    r->polls      = s_host.polls;
    r->timeouts   = s_host.timeouts;
    (void)can_twai_errors(&r->tx_errors, &r->rx_errors, &r->bus_errors,
                          &r->bus_off);
}

void link_report(void)
{
    s_bring.polls         = s_host.polls;
    s_bring.replies       = s_host.replies;
    s_bring.timeouts      = s_host.timeouts;
    s_bring.mismatches    = s_host.mismatches;
    s_bring.nacks         = s_host.nacks;
    /*
     * Left at zero.  link_bringup reads rx_crc_errors as "frames this end
     * received and found corrupt", and TWAI (Two-Wire Automotive Interface,
     * the ESP32-P4's CAN controller) has no such counter: bus_error_count is
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
        ESP_LOGI(TAG, "  iomcu  frames %lu crc %lu resync %lu refused %lu",
                 (unsigned long)s_bring.dev_frames,
                 (unsigned long)s_bring.dev_crc_errors,
                 (unsigned long)s_bring.dev_resyncs,
                 (unsigned long)s_bring.dev_refused);
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
    /* Read once, like every other reader of it: the control task can clear
     * it between the test and the subtraction. */
    const uint32_t lost_ms = atomic_load(&s_link_lost_ms);
    char row[208];
    snprintf(row, sizeof(row),
             "t=%lus link=down for %lus  bus=%s tx_err=%s rx_err=%s "
             "bus_err=%s rejoins=%lu/%lu  polls=%lu replies=%lu timeouts=%lu",
             (unsigned long)(now_ms() / 1000u),
             (unsigned long)(lost_ms == 0u
                                 ? 0u
                                 : (now_ms() - lost_ms) / 1000u),
             !have_bus ? "not running" : (off ? "OFF" : "on"),
             have_bus ? u32(n1, sizeof(n1), tec) : "?",
             have_bus ? u32(n2, sizeof(n2), rec) : "?",
             have_bus ? u32(n3, sizeof(n3), bus) : "?",
             (unsigned long)s_recoveries, (unsigned long)s_recoveries_total,
             (unsigned long)s_bring.polls, (unsigned long)s_bring.replies,
             (unsigned long)s_bring.timeouts);
    debug_log(row);
}
