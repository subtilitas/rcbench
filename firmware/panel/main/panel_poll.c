/**
 * @file panel_poll.c
 * @brief The poll of the far end, once a control pass: the bench page and the
 * services while the IOmcu answers, its identity while it does not,
 * the link-up and link-lost edges, the model and the log while nothing
 * answers, and the snapshot the screen reads.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "arming.h"
#include "bind_link.h"
#include "can_twai.h"
#include "link_bringup.h"
#include "link_host.h"
#include "link_pages.h"
#include "out_bind.h"
#include "outputs.h"
#include "outputs_screen.h"
#include "outputs_pages.h"
#include "panel.h"
#include "prog_link.h"
#include "sense_link.h"
#include "servo_page.h"
#include "servo_screen.h"
#include "servo_source.h"
#include "supply_link.h"
#include "telemetry_sim.h"
#include "tone_link.h"
#include "ui_text.h"

static const char *TAG = "rcbench";

/* LINK_ST_FAULTS from the IOmcu's last status poll, shown in the band. */
static uint16_t s_dev_faults;

/*
 * What the sensor bus, the PD mini and the phase tap hold on the IOmcu's
 * pins by their pages' last reads, for OUTPUTS and PICK A PIN: a refused
 * part or relocation leaves the pins the page keeps.  With the link down
 * none is known, and the screens follow the settings.  An IOmcu without
 * the SUPPLY page holds no PD mini pin.
 */
static void iface_pins_publish(bool up)
{
    int a = -1;
    int b = -1;
    bool known = up && sense_link_held_pins(&s_sense_link, &a, &b);
    outputs_screen_set_iface_held(OUTPUTS_IFACE_SENSE, known, a, b);
    a = -1;
    b = -1;
    known = up && (!s_supply_page
                   || supply_link_held_pins(&s_supply_link, &a, &b));
    outputs_screen_set_iface_held(OUTPUTS_IFACE_PDMINI, known, a, b);
    a = -1;
    known = up && tone_link_held_pin(&s_tone_link, &a);
    outputs_screen_set_iface_held(OUTPUTS_IFACE_TAP, known, a, -1);
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
         * The pole count, when an edit or a write that did not land leaves
         * one owed.  Between arms this is the path an edit takes to the far
         * end: nothing else writes the register while the link stays up, and
         * the write that arms carries the count of its own accord
         * (control_arm()).  STOP and a re-arm produce no link-down edge, so
         * without this a count edited on a live link would wait for the
         * cable, an IOmcu reset or a panel reboot.
         *
         * Costs a transaction only while the debt stands, which is one poll
         * per edit.
         */
        (void)poles_service();
        /*
         * Not while a surface at the far end is still holding a position and
         * owed a release: the far end would render that old command before the
         * centre arrived.  servo_link_service() pays the debt every pass, so
         * this holds for one.  A bank armed with no link does not reach this:
         * poll_far_end() stops it on the edge where one answers, as it stops
         * an armed bench on the edge where the link goes.  Nor while the rate
         * the surfaces run at is not known: the far end may hold a heli rate
         * from before a panel restart, and servo_link_service() settles that
         * every pass too.  And not after a stop or a disarm asked for since
         * the bank armed: the exchanges that settle those debts let the pump
         * apply a STOP, and the bank itself is disarmed only on the next
         * service_arming() pass.
         *
         * Decided after that exchange and straight before the write that
         * carries it: the pump runs inside every exchange, so a STOP or a
         * disarm can land in poles_service() as well.
         */
        const bool armed = outputs_armed(&s_out) && !servo_link_release_owed()
                           && !endpoints_held(&s_endpoints)
                           && servo_link_hz_sent() != SERVO_HZ_UNKNOWN
                           && !arming_stopped(&s_arm)
                           && !atomic_load(&s_disarm_request);
        const bool written = control_write(armed, &ack);
        /*
         * The throttle's endpoints, when an edit or a link-up leaves them
         * owed -- after the control write, and only when that write put
         * ARM = 0 at the far end and was acknowledged, with the bench
         * disarmed here or held for this very debt.  Costs a read, and a
         * write only when the page differs.
         */
        endpoints_service(written && !armed
                          && (!outputs_armed(&s_out)
                              || endpoints_held(&s_endpoints)));
        /* And the supply's page, a write only when one is owed. */
        supply_link_service();
        /* And the current monitors': a set-up only with ARM written 0 and
         * acknowledged and the bank disarmed here, as the page refuses one
         * while the bank drives.  SENSE_MAP first: SENSE's mirror
         * registers follow the map it holds. */
        sense_map_service(written && !armed && !outputs_armed(&s_out));
        sense_link_service(written && !armed && !outputs_armed(&s_out),
                           bench);
        /* And every channel's last window, armed or not: readings.  After
         * SERVO_WIN, so the pages hold the ring's newest window or a newer
         * one, and a row of the ring finds its window on them. */
        sense_win_service();
        /* And the channels' limits, armed or not: an alert only. */
        sense_limit_service();
        /* And the phase tap's: an input, taken armed or not. */
        tone_link_service();
        iface_pins_publish(true);
        /* And the servo programmer's: the IOmcu decides what it
         * starts, armed or not. */
        kst_link_service();
        /* And the programming ports': the SERVO screen's target and the
         * port's pulse switch. */
        prog_link_service();
        /* And how a Hitec D series line receives. */
        hitec_rx_service();
        if (!written && armed && ack.op == LINK_OP_NACK) {
            /*
             * The IOmcu is in failsafe, has lost the heartbeat or
             * holds its arm latch -- it started again, or the line was
             * distrusted for a moment.  A stop latches at this end too.
             */
            arming_stop_from_far_end(&s_arm);
            far_end_stop_here();
        }
    }
    return answered;
}

/*
 * Who is there, while nothing is answering.
 *
 * True only for a DATA identity page from an IOmcu speaking
 * LINK_PROTOCOL_MAJOR; reply then holds that page.
 */
/* A protocol mismatch: reported, and whether its build check is owed. */
link_uph_mismatch_t s_mismatch;

static bool probe_identity(link_msg_t *reply)
{
    bool answered;
    /*
     * The bus first, because a controller that has fallen off it cannot ask
     * anything.  A transmitter nobody answers -- an IOmcu not powered
     * yet, or one that has reset -- adds 8 to its error counter per attempt
     * and is off the bus in about four milliseconds, and the ESP32-P4's TWAI
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
     * identity page says there is an IOmcu there to talk to.
     */
    answered = poll_page(&s_host, LINK_PAGE_IDENTITY, LINK_ID_COUNT, reply)
               && reply->op == LINK_OP_DATA;
    if (answered
        && reply->regs[LINK_ID_PROTOCOL_MAJOR] != LINK_PROTOCOL_MAJOR) {
        /* A protocol major that differs from LINK_PROTOCOL_MAJOR refuses
         * arming; the register is the first one of the identity page.
         *
         * Said once per mismatch, not at every poll: the probe repeats each
         * second while the link is down, and an alert raised again each
         * time would never expire and could not be tapped away.  A probe
         * nobody answers, or one that matches, makes the next one news.
         * An update that ended owes the build check again, not the alert. */
        const bool told = link_uph_mismatch_answers(&s_mismatch);
        if (link_uph_mismatch_probe(&s_mismatch, true)) {
            if (!told) {
                ESP_LOGE(TAG, "IOmcu speaks protocol %u, we speak %u",
                         (unsigned)reply->regs[LINK_ID_PROTOCOL_MAJOR],
                         (unsigned)LINK_PROTOCOL_MAJOR);
                control_alert(TR(ALERT_MISMATCH));
            }
            /* The identity registers and the update pages answer across
             * majors (link_pages.h), so an IOmcu this panel cannot
             * drive can still be moved to the image it carries. */
            cu_link_up(reply);
        }
        answered = false;
    } else {
        (void)link_uph_mismatch_probe(&s_mismatch, false);
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
         * And where they are, if it says.  Only a drawing of the board needs
         * this, so a board that answers neither this nor ARTWORK is used from
         * its catalogue and is not drawn.
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
         * Not a fault, and not warned about: an IOmcu built before the
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

/* The GPIOs bound as throttle (MOTOR PWM, DShot) in the last binding read,
 * for the MOTOR & ESC screen's TARGET row, whether a binding was read, and
 * the protocol they are bound as (outbind_role_proto()).  The same pages
 * the IOmcu's throttle role reads. */
atomic_ullong s_throttle_pins;
atomic_bool   s_throttle_pins_known;
atomic_uint   s_throttle_proto;

/* A reading, into the snapshot the outputs and picker screens take. */
static void post_reading(const bind_reading_t *r)
{
    s_bind_read = r->state;
    /* A binding that did not read names no pin: the screen says so. */
    const bool known = (r->state == BIND_READ_OK);
    atomic_store(&s_throttle_pins,
                 known ? outputs_role_pins(r->slots, r->cfg, OUT_ROLE_THROTTLE)
                       : 0u);
    atomic_store(&s_throttle_pins_known, known);
    atomic_store(&s_throttle_proto,
                 known ? (unsigned)outbind_role_proto(r->slots, r->cfg,
                                                      OUT_ROLE_THROTTLE)
                       : 0u);
    if (xSemaphoreTake(s_snap_lock, portMAX_DELAY) == pdTRUE) {
        s_outputs_read = *r;
        s_outputs_read_fresh = true;
        xSemaphoreGive(s_snap_lock);
    }
}

/*
 * No reading is to be had: the link is down.  The screens keep the last
 * binding read, marked, and take no edit until one is read again.
 */
void post_no_reading(void)
{
    static bind_reading_t r;     /* 140 bytes kept off the task's stack */
    (void)bind_link_classify(&r, s_board, NULL, NULL);
    post_reading(&r);
}

/*
 * What the IOmcu's outputs already are, into the snapshot the outputs
 * and picker screens read.
 *
 * bind_link_read() reads both pages and tells four results apart: a binding
 * (one with nothing bound included), pages that did not read, pages no
 * binding describes, and a board this build has no pin map for.  Only the
 * first is a binding to edit; the screens are told which of the others it
 * is.
 */
void read_outputs_binding(void)
{
    static bind_reading_t r;     /* control task only */
    if (bind_link_read(&s_host, &k_port, s_board, &r) == BIND_READ_OK) {
        /*
         * Which channels the horn may drive, from the pages themselves rather
         * than from the binding they were read into.  A binding names one
         * role per slot -- outbind_from_slots() takes a slot's role from its
         * first channel -- so a multi-channel slot whose channels disagree
         * would put a throttle in the surfaces' mask, and the horn would
         * command it.  The pages answer per channel.
         *
         * Held unlocked because the control task is the only one that touches
         * it, and it is the only task that writes the wire.
         */
        atomic_store(&s_servo_pins,
                     outputs_role_pins(r.slots, r.cfg, OUT_ROLE_SURFACE));
        atomic_store(&s_servo_pins_known, true);
        servo_link_binding(outputs_role_channels(r.slots, r.cfg,
                                                 OUT_ROLE_SURFACE),
                           true);
        post_reading(&r);
        return;
    }
    /*
     * A binding that would not read is unknown, not empty.  The horn
     * drives nothing either way, but the release stays owed: the far end
     * may still be rendering surfaces from before this panel started, and
     * nothing here can name them to settle them.
     */
    atomic_store(&s_servo_pins, 0u);
    atomic_store(&s_servo_pins_known, false);
    servo_link_binding(0u, false);
    post_reading(&r);
    switch (r.state) {
    case BIND_READ_NO_BOARD:
        ESP_LOGW(TAG, "hardware %u has no pin map in this build; the "
                      "screen will offer no pins", (unsigned)s_board);
        break;
    case BIND_READ_ODD:
        ESP_LOGW(TAG, "the output pages describe no binding this build can "
                      "show; the screen takes no edit but UNBIND ALL PINS");
        break;
    default:
        ESP_LOGW(TAG, "could not read the outputs page; the screen keeps the "
                      "last binding read and takes no edit");
        break;
    }
}

/*
 * The edge: an IOmcu started answering.
 *
 * All of it costs a transaction and none of it changes while the link is up,
 * so it runs once per edge rather than once per 50ms poll.  reply is the
 * identity page that detected the edge.
 */
static void link_came_up(const link_msg_t *reply)
{
    /*
     * Who answered, before anything is decoded against it.
     *
     * The identity read at bring-up runs once, with whatever was attached then
     * -- which may have been nothing.  An IOmcu that turns up later, or
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
     * read of it can reach a value from an earlier IOmcu.
     */
    s_board = reply->regs[LINK_ID_HARDWARE];

    /* A binding is prepared and committed whole from 4.10
     * (bind_link_write()), and written page by page before it. */
    s_far_minor = reply->regs[LINK_ID_PROTOCOL_MINOR];
    /* A refusal count is the IOmcu's that sent it: the band shows
     * none until this one's status page is read. */
    s_bring.have_refused = false;
    s_bring.dev_refused  = 0u;
    /* The SERVO screen's link: the rate reset, the sweep's state. */
    servo_link_came_up(reply->regs[LINK_ID_PROTOCOL_MINOR]);

    /*
     * Whether it drives the PD mini (4.3), and its page written again from
     * the start: an IOmcu that started again holds nothing, and one
     * that only went quiet may hold an ON this panel has since let go.
     */
    s_supply_page = reply->regs[LINK_ID_PROTOCOL_MINOR] >= 3u;
    atomic_store(&s_supply_vin_mv, 0u);
    atomic_store(&s_supply_baud, 0u);
    /* 4.4 finds the module's rate itself, and has BAUD_FOUND. */
    s_supply_auto = reply->regs[LINK_ID_PROTOCOL_MINOR] >= 4u;
    supply_link_lost(&s_supply_link);
    if (!s_supply_page && pdmini_wiring().en) {
        control_alert(TR(ALERT_NO_SUPPLY_PAGE));
    }

    /*
     * And the current monitors' pages (4.7): what SENSE holds is read
     * before anything is written, and an IOmcu older than that is sent
     * nothing.  Its capability bits are this identity page's: bits 3 and 4
     * follow a SENSE set-up kept in its flash, whoever wrote it.
     */
    atomic_store(&s_capabilities,
                 (unsigned)reply->regs[LINK_ID_CAPABILITIES]);
    {
        const sense_setup_t w = sense_wanted();
        sense_link_want(&s_sense_link, &w, now_ms());
    }
    sense_link_came_up(&s_sense_link, reply->regs[LINK_ID_PROTOCOL_MINOR],
                       now_ms());
    /* And the INA3221 parts' map (4.25), read before anything is written. */
    sense_map_came_up(reply->regs[LINK_ID_PROTOCOL_MINOR]);
    /* And the channels' limits (4.27). */
    sense_limit_came_up(reply->regs[LINK_ID_PROTOCOL_MINOR]);
    sense_link_alerts();
    /* And the phase tap's page (4.8), the same way. */
    {
        const tone_setup_t tw = tone_wanted();
        tone_link_want(&s_tone_link, &tw, now_ms());
    }
    tone_link_came_up(&s_tone_link, reply->regs[LINK_ID_PROTOCOL_MINOR],
                      now_ms());
    tone_link_alerts();
    /* And the servo programmer's page (4.12). */
    kst_link_came_up(&s_kst_link, reply->regs[LINK_ID_PROTOCOL_MINOR]);
    /* And the Hitec programmer's (4.23): its port, held or not, is read
     * before a request goes. */
    hitec_link_came_up(&s_hitec_link, reply->regs[LINK_ID_PROTOCOL_MINOR]);
    /* And the programming ports' (4.13): not known until it is read. */
    prog_link_came_up(&s_prog_link, reply->regs[LINK_ID_PROTOCOL_MINOR]);
    prog_drive_publish();
    /* And the Hitec receive mode (4.24): owed again, an IOmcu that
     * started again holds PULL-UP. */
    hitec_rx_link_came_up(&s_hitec_rx_link,
                          reply->regs[LINK_ID_PROTOCOL_MINOR]);

    /*
     * A board this build ships no catalogue for describes its own pins, so an
     * IOmcu newer than this panel is usable rather than blank.
     *
     * Only when the build has none.  A board it knows uses its own catalogue:
     * that one has been read by somebody, names the exact signal holding each
     * reserved pin, and cannot change under a running bench.
     *
     * Nothing here makes a pin safe.  The IOmcu reserves its own set at
     * its own end whatever this page says, so a catalogue that is wrong costs
     * a pin rather than the safety line.  An IOmcu built before the page
     * answers NACK, which is not a failure: the screen then offers nothing for
     * that board, as it did before.
     */
    if (outbind_board(s_board) == NULL) {
        learn_board_pins();
    }

    /*
     * And its drawing: the kept one when it is this drawing, else fetched a
     * slice of a poll at a time below.
     */
    art_begin(s_board);

    /*
     * The pole count.  An IOmcu that has just started holds zero, and
     * one whose cable came back holds whatever it was last told, so the edge
     * sends it either way.
     *
     * The warning is here rather than in the payer.  A refusal means the
     * register is not in that IOmcu's build, which is a fact about the
     * board that just answered and worth one line per edge; the retry at
     * every poll would repeat it twenty times a second.
     */
    atomic_store(&s_poles_owed, true);
    if (!poles_service()) {
        ESP_LOGW(TAG, "IOmcu did not take the pole count -- rpm will "
                      "read empty");
    }
    /*
     * The throttle's pulse endpoints, for the same reason: an IOmcu
     * replaced or reflashed since the debt was last paid holds its own
     * CHAN_CFG, and the panel would go on showing a range the motor channels
     * do not have.  The far end is held disarmed until it is paid
     * (endpoints_link_up()), by endpoints_service() at the next poll,
     * which reads the page and writes it only if the throttle channels
     * differ, so an edge to an IOmcu that already agrees costs a read.
     */
    endpoints_link_up(&s_endpoints);
    /*
     * And what its outputs already are.  The screen shows what is configured
     * over there, not what this panel last sent: after a panel restart those
     * are different things, and only one of them is driving pins.
     */
    read_outputs_binding();

    /* And whether it runs the image this panel carries. */
    cu_link_up(reply);
}

/**
 * @brief The IOmcu's STATUS fault bitmap at its last read.
 * @return LINK_FAULT_* bits; 0 before the first read
 */
uint16_t far_end_faults(void)
{
    return s_dev_faults;
}

/*
 * The status page's counters, into the bring-up record.  A 4.21 or later
 * IOmcu is asked for its refusal count as well; an older one refuses
 * a read past its 8 registers, so it is asked for those
 * (link_bringup_status_count()) and its count stays 0.
 */
static void read_status_counters(void)
{
    link_msg_t st;
    if (poll_page(&s_host, LINK_PAGE_STATUS,
                  link_bringup_status_count(s_far_minor), &st)
        /* have_status, which two of link_bringup's diagnoses are gated
         * on, is set here. */
        && link_bringup_take_status(&s_bring, &st, &s_dev_faults)) {
        /* The band shows the bitmap as a code; a store that keeps nothing
         * this boot is said in words, once per link-up. */
        sense_link_faults(&s_sense_link, s_dev_faults);
        sense_link_alerts();
    }
}

/*
 * The far end: one poll, and everything that hangs off whether it answered.
 *
 * Every value it maintains belongs to the control loop and is passed in.
 * link_up is both the gate's period -- 50ms up, 1000ms down -- and this
 * poll's answer; last_poll and last_status are the two gates' clocks.
 * Returns whether this pass produced a bench sample.
 */
bool poll_far_end(bool *link_up, bench_state_t *bench,
                  uint32_t *last_poll, uint32_t *last_status)
{
    bool new_sample = false;
    /*
     * Whether the link is up is read from one thing: s_link_quiet, which
     * exchange() sets when any request ends unanswered with the link up --
     * the bench read or the control write of this poll, a page service, a
     * servo refresh, a command's write, the status read, a slice of the
     * drawing, an exchange of an arm.  A poll's own return value is
     * whether a bench sample or an identity arrived and takes no part.
     *
     * Set since the last poll, it is this poll's answer at once: ahead of
     * the 50ms gate, and without a read that would wait another
     * LINK_HOST_TIMEOUT_MS (1000ms) to learn the same.  An armed bench has
     * been stopped already, where the exchange ended.
     */
    if (s_link_quiet
        || (uint32_t)(now_ms() - *last_poll) >= (*link_up ? 50u : 1000u)) {
        *last_poll = now_ms();
        link_msg_t reply;
        bool answered = false;
        if (s_link_quiet) {
            /* Nothing is asked. */
        } else if (*link_up) {
            answered = poll_bench(bench);
        } else {
            answered = probe_identity(&reply);
        }
        if (s_link_quiet) {
            /* Before this poll or inside it.  Taken down here and the
             * latch released: the next request is the identity probe. */
            answered = false;
            s_link_quiet = false;
            s_link_is_up = false;
        }
        if (answered != *link_up) {
            ESP_LOGI(TAG, "IOmcu %s",
                     answered ? "answered" : "went quiet");
            if (answered) {
                /* From here on an unanswered exchange is a link lost,
                 * link_came_up()'s own included. */
                s_link_is_up = true;
                /*
                 * A bank armed with no link -- the simulator's -- is
                 * stopped before anything is written: the far end was
                 * never asked to arm, and the ARM this bank writes at
                 * every poll would ask it now, at this end's throttle.
                 */
                if (arming_link_found(&s_arm, outputs_armed(&s_out))) {
                    far_end_stop_here();
                }
                link_came_up(&reply);
            }
        }
        if (!answered && s_artbusy) {
            /* The board that was sending it is gone, so the rest of its
             * drawing is not coming.  Nothing was kept or shown: both wait
             * for the whole thing to check out. */
            art_stop("the link went quiet");
        }
        /*
         * The link going, and how long ago.  Taken here rather than in the
         * render loop because this is where the answer arrives; the screen is
         * only shown from there.
         */
        if (answered) {
            atomic_store(&s_link_lost_ms, 0u);
            s_link_lost_shown = false;
            s_recoveries      = 0;   /* the next outage counts its own */
        } else if (*link_up) {
            /* The edge: it was up until this poll. */
            atomic_store(&s_link_lost_ms, now_ms());
            /*
             * An armed bench stops with the link, and so does an arm still
             * waiting for the line.  The far end has failed safe on its own
             * 200ms of silence, or it has started again; either way the
             * bank and the command kept here would be written to it as
             * ARM = 1 at the first poll it answers, with nobody having
             * asked.  The stop also withholds the heartbeat.
             */
            if (arming_link_lost(&s_arm, outputs_armed(&s_out))) {
                far_end_stop_here();
            }
            /* A sweep or a hold ends with the link, here as on the screen
             * and at the far end: kept, it would be said again when the
             * link comes back and start motion nobody asked for. */
            servo_link_lost();
            /* The PD mini goes with it: its readings stop, and the step
             * switches an ON off as a supply not answering. */
            s_supply_page = false;
            supply_link_lost(&s_supply_link);
            atomic_store(&s_supply_vin_mv, 0u);
            atomic_store(&s_supply_baud, 0u);
            /* And the monitors' pages: nothing on them is known until the
             * link comes back, and the ESC's figures go with the readings. */
            sense_link_lost(&s_sense_link);
            sense_map_lost();
            sense_limit_lost();
            (void)servo_source_step(&s_servo_source, now_ms(), false, 0u,
                                    NULL);
            tone_link_lost(&s_tone_link);
            /* An operation under way ends unanswered; the programmer
             * says what that means for the servo. */
            kst_link_lost(&s_kst_link);
            hitec_link_lost(&s_hitec_link);
            /* And no port is the target or names a pin, and the SERVO
             * screen's TARGET row says no link. */
            prog_link_lost(&s_prog_link);
            atomic_store(&s_prog_pin, PROG_LINK_NO_PIN);
            atomic_store(&s_prog_ports, PROG_PORTS_NONE);
            prog_ports_lost();
            prog_drive_publish();
            servo_link_surfaces_publish();
            atomic_store(&s_sense_unheld, 0u);
            atomic_store(&s_tone_unheld, 0u);
            hitec_rx_link_lost(&s_hitec_rx_link);
            atomic_store(&s_hitec_rx_mark,
                         (unsigned)hitec_rx_link_mark(&s_hitec_rx_link));
            iface_pins_publish(false);
            bench_state_set_esc(bench, false, 0.0f, false, 0.0f, false);
            bench->servo_new = false;
            /* And the binding: what the screens show is the last one read,
             * and they take no edit until the link-up read. */
            post_no_reading();
        }
        /*
         * A sample exists only if the bench page was read.  A poll that timed
         * out republishes nothing: counting it would put a stale reading on
         * the plot as a fresh column and stamp a log row for a measurement
         * that never arrived.
         *
         * Taken before link_up moves, and from the branch rather than from
         * the answer.  While the link is down the question asked is the
         * identity page, so an answer there says an IOmcu is there to
         * talk to and says nothing about the bench: bench still holds
         * whatever it held, which at boot is zeros.
         */
        new_sample = *link_up && answered;
        *link_up = answered;
        if (!answered) {
            s_link_is_up = false;
        }

        /*
         * The status page is read a tenth as often as the bench page: a status
         * read costs a whole transaction and its numbers move slowly.
         */
        if (*link_up && (uint32_t)(now_ms() - *last_status) >= 500u) {
            *last_status = now_ms();
            read_status_counters();
            /*
             * And the binding, while it is unknown.  Without a retry an
             * output page that would not read once leaves the servo screen
             * unable to name a channel, and the release it owes unpayable,
             * until the next link-up edge -- which on a link that stays up
             * never comes.  Once it is known this costs nothing.
             */
            if (!servo_link_binding_known()) {
                read_outputs_binding();
            }
        }

        /*
         * And a slice of the drawing, last: the bench's own pages are what
         * the operator is watching, and this is a transfer that happens once
         * and can afford to wait for them.
         */
        if (*link_up && s_artbusy) {
            art_slice();
        }
    }
    return new_sample;
}

/** @brief The newest window of the rail's INA3221 channel, from SERVO_WIN,
 *         for the rows while the INA3221 is the rail's meter.  Control task
 *         only. */
static sense_link_win_t s_rail_win;
/** @brief s_rail_win holds a window of the meter's current stretch. */
static bool             s_rail_win_ok;

/*
 * The INA3221's next window into @p bench's log fields, and a ring window
 * onto the queue to the render side as well.  Control task only.
 */
static void servo_window_take(bench_state_t *bench)
{
    if (!sense_link_win_on(&s_sense_link)) {
        if (sense_link_take_window(&s_sense_link, bench)) {
            sense_win_attach(bench, false);
        }
        return;
    }
    sense_link_win_t w;
    if (!sense_link_take_win(&s_sense_link, &w)) {
        return;
    }
    sense_link_win_bench(&s_sense_link, &w, bench);
    /* The rail's figures while the INA3221 is its meter. */
    s_rail_win    = w;
    s_rail_win_ok = true;
    /* And every watched channel's same window, for the log. */
    sense_win_attach(bench, true);
    /* With the meter as this poll decided it: the snapshot the render side
     * reads can be older or newer than a window on the queue. */
    servo_win_item_t item = {
        .win     = w,
        .source  = servo_source_id(&s_servo_source),
        .changes = servo_source_changes(&s_servo_source),
    };
    item.dropped = servo_source_dropped(&s_servo_source, &item.dropped_at);
    if (xQueueSend(s_win_q, &item, 0) != pdTRUE) {
        servo_win_item_t stale;
        (void)xQueueReceive(s_win_q, &stale, 0);
        (void)xQueueSend(s_win_q, &item, 0);
    }
}

/** @brief The pulse and the angle the render side last handed over
 *         (log_servo_pack()), none until it does. */
atomic_uint s_servo_log = 0x8000u;

/**
 * @brief The servo rail's reading for a SERVO run's row, into s_rail, and
 *        the run's totals of it: the INA3221's newest window while an
 *        INA3221 channel is the rail's meter, the supply's last reading
 *        otherwise.
 *
 * @param bench   The sample, for its source.
 * @param dt_s    The time since the last sample, s.
 * @param driving true while the bank is ARMED.
 */
static void rail_sample(const bench_state_t *bench, float dt_s, bool driving)
{
    const bool modelled = bench_state_simulated(bench);
    if (servo_source_id(&s_servo_source) == SERVO_SOURCE_INA3221) {
        log_rail_take_ina(&s_rail, s_rail_win_ok ? &s_rail_win : NULL,
                          modelled);
    } else {
        /* A window kept from before is not the meter's when it returns. */
        s_rail_win_ok = false;
        log_rail_take(&s_rail, &s_supply, modelled);
    }
    log_rail_count(&s_rail_totals, &s_rail, dt_s, driving);
}

/*
 * The model's step, the run's totals and the bench log's rows, each decided
 * for this pass.
 *
 * The model runs on its own 50ms gate, not on the poll's.  The poll's runs
 * at 1Hz while the link is down, which would step the model once per
 * 1000ms of wall clock instead of twenty times -- the plot's axis twenty
 * times slow.  Its step is the time that passed, capped at a second like
 * the totals: a probe for the IOmcu's identity can hold this loop for
 * its whole 1000ms timeout.
 *
 * The log has no gate: a pass that brought a sample writes one row for
 * each INA3221 window the poll handed over and one row when it handed over
 * none, stamped with the wall time since the arm; see log_cadence.h.
 */
void advance_model_and_log(bool link_up, float emitted,
                           telemetry_sim_t *sim, bench_state_t *bench,
                           bool *new_sample)
{
    float step_s = 0.0f;
    if (log_cadence_model_due(&s_log_cad, now_ms(), link_up, &step_s)) {
        telemetry_sim_step(sim, emitted, step_s, bench);
        *new_sample = true;
    }
    /*
     * The run's totals, from the sample either source just wrote, over the
     * time since the last one -- measured, not the 50ms the model's cadence
     * aims at: a link probe can hold this loop for a second, and an
     * IOmcu sample arrives on the poll's clock.
     */
    float dt_s = 0.0f;
    const bool driving = outputs_armed(&s_out);
    if (*new_sample) {
        const uint32_t t = now_ms();
        dt_s = (float)(uint32_t)(t - s_totals_ms) / 1000.0f;
        bench_totals_count(&s_totals, bench, dt_s, driving);
        s_totals_ms = t;
    }
    /*
     * Written over the source's own charge and energy before the screen or
     * the log sees them, on every pass and not only a counted one: the first
     * read after a link-up is not a new sample, and it has written the
     * IOmcu's own empty registers over the totals.
     */
    bench_totals_show(&s_totals, bench);
    /* A supply run's rows are supply_pump()'s, on the supply's cadence. */
    float t_s = 0.0f;
    const bool logged = log_cadence_row(
        &s_log_cad, now_ms(), *new_sample,
        log_cadence_bench_run(log_run_armed(s_log_kind),
                              atomic_load(&s_servo_testing)),
        &t_s);
    /* Every window is taken in the pass that brought it, logged or not:
     * the render side has each once, and a window taken before a run is
     * in none of its rows. */
    const unsigned windows = sense_link_windows(&s_sense_link);
    const unsigned rows    = log_cadence_rows(windows);
    for (unsigned k = 0u; k < rows; ++k) {
        if (k < windows) {
            servo_window_take(bench);
        }
        const bool last = k + 1u == rows;
        /* The servo rail's meter for a SERVO run's rows, after the pass's
         * last window: its reading at this sample, counted over the same
         * step as the bench's totals. */
        if (last && *new_sample) {
            rail_sample(bench, dt_s, driving);
        }
        if (logged) {
            log_row_t row = { .kind = s_log_kind, .t_s = t_s };
            bench_state_log_row(bench, last, &row.u.bench);
            /* The rail's figures, the pulse and the angle go with the
             * sample, on the last row. */
            if (s_log_kind == LOG_RUN_SERVO && last) {
                row.rail = s_rail;
                log_servo_unpack(atomic_load(&s_servo_log),
                                 bench_state_simulated(bench), &row.servo);
            }
            log_post(&row);
        }
        /* The window is in that row and in no later one. */
        bench->servo_new = false;
    }
}

/*
 * What the renderer draws: the numbers under the mutex, and the sample on the
 * queue.
 */
void publish_snapshot(const bench_state_t *bench, bool link_up,
                      bool new_sample)
{
    snap_lock();
    s_snap.bench       = *bench;
    s_snap.supply      = s_supply;
    s_snap.supply_gen  = s_supply_gen;
    s_snap.link_up     = link_up;
    /* An IOmcu this panel cannot drive still takes the update. */
    s_snap.cu.mismatch = link_uph_mismatch_answers(&s_mismatch);
    s_snap.armed       = outputs_armed(&s_out);
    s_snap.armed_roles = outputs_armed_roles(&s_out);
    s_snap.arm_wanted  = arming_link_needed(&s_arm, s_snap.armed);
    s_snap.arm_gen     = s_arm_gen;
    s_snap.stopped     = arming_stopped(&s_arm);
    s_snap.stops       = arming_stop_count(&s_arm);
    s_snap.pressed     = arming_pressed_count(&s_arm);
    s_snap.faults      = link_up ? s_dev_faults : (uint16_t)0;
    s_snap.link_errors = (uint32_t)s_bring.dev_crc_errors
                         + (uint32_t)s_bring.dev_resyncs;
    s_snap.link_refused = s_bring.dev_refused;
    s_snap.run_seconds = arming_run_seconds(&s_arm);
    s_snap.mcu_temp_c  = s_mcu_c;
    tone_link_readout(&s_tone_link, now_ms(), &s_snap.tone);
    s_snap.servo_source   = servo_source_id(&s_servo_source);
    s_snap.servo_source_changes = servo_source_changes(&s_servo_source);
    s_snap.servo_source_why = servo_source_why(&s_servo_source);
    s_snap.servo_source_dropped =
        servo_source_dropped(&s_servo_source,
                             &s_snap.servo_source_dropped_at);
    s_snap.servo_win_lost = sense_link_win_lost(&s_sense_link);
    kst_publish(&s_snap.kst);
    hitec_publish(&s_snap.hitec);
    snap_unlock();

    /* One queue entry per sample, so none of the plot's time base is lost to
     * a renderer that was busy. */
    if (new_sample && xQueueSend(s_sample_q, bench, 0) != pdTRUE) {
        bench_state_t stale;
        (void)xQueueReceive(s_sample_q, &stale, 0);
        (void)xQueueSend(s_sample_q, bench, 0);
    }
}
