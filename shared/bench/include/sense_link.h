/*
 * The panel's half of the SENSE (0x2B) and SERVO_SENSE (0x2C) link pages,
 * protocol 4.7: what is written to SENSE, in what order, what is read from
 * both pages and how often, and what a read means for the operator.
 *
 * The coprocessor reads the two current monitors and the output encoder (an
 * AS5600 angle sensor); the panel sets them up from SETUP INTERFACES and
 * reads what they measured.  A coprocessor whose protocol minor is below
 * SENSE_LINK_MINOR has neither page, and nothing is sent to it: not a write
 * and not a read.  One below SENSE_LINK_ENC_MINOR has no encoder: ENABLE
 * bit 2 is not written, SENSE 26 to 31 are not read, and an encoder enabled
 * on SETUP raises SENSE_LINK_EV_ENC_OLD.
 *
 * The set-up is three frames of four registers: the bus (ENABLE to KHZ),
 * the INA228 (4 to 7) and the INA3221 (8 to 11).  The page judges every
 * frame against the whole set-up it would leave, so the order is fixed:
 *
 *   1. what the page holds is read first, SENSE 0 to 11, once per link-up,
 *      and nothing is written that it already holds;
 *   2. while a part's frame is to change and the page enables both parts,
 *      the bus frame with ENABLE 0 and the page's own pins, so no
 *      intermediate set-up has two enabled parts on one address;
 *   3. the part frames that differ;
 *   4. the bus frame: the pins, and ENABLE without a part whose frame was
 *      refused, so no part runs on a shunt or an address it was not given.
 *
 * Writes go out only while the caller says the bank is idle -- the page
 * refuses any change while it drives -- and only once the set-up has rested
 * SENSE_LINK_SETTLE_MS since its last edit, because the coprocessor keeps
 * each change in flash.  A frame the page refuses is not written again
 * until the set-up changes -- except a bus frame whose SDA and SCL are one
 * I2C block's pair, refused for pins something else holds, which is
 * offered again every SENSE_LINK_BUS_RETRY_MS and said once.  Two set-ups are not written at all, and said
 * once per edit: a part enabled with SDA or SCL unset, and both parts
 * enabled on one address; neither part is enabled on the page then.
 *
 * After a write is taken, the identity page is read again: its capability
 * bits 3 and 4 say what the SENSE set-up enables, and change only with a
 * SENSE write.
 *
 * While the page enables a part, SENSE 12 to 25 -- to 31 with the encoder
 * -- are read every SENSE_LINK_READ_MS and, while it enables the INA3221, SERVO_SENSE 0 to 13
 * every SENSE_LINK_SERVO_MS.  From those reads come the events the caller
 * tells the operator about, each once: a part enabled and not answering, a
 * part that answers with another identity, the bus stuck, and a reading
 * clipped at the top of its range.
 *
 * Pure C, no link of its own: the caller makes each exchange and reports
 * how it went.  Control task only.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bench_state.h"
#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The protocol minor that has SENSE and SERVO_SENSE. */
#define SENSE_LINK_MINOR 7u
/** The protocol minor that has SENSE's output encoder. */
#define SENSE_LINK_ENC_MINOR 9u

/** How long a set-up rests after its last edit before it is written: a held
 *  + or - on SETUP steps a value about 30 times a second, and each write is
 *  a flash save at the coprocessor. */
#define SENSE_LINK_SETTLE_MS 500u

/** SENSE's read-only registers are due this long after the last read,
 *  while a part is enabled: under the 50 ms poll, so every BENCH poll reads
 *  them -- 20 Hz -- however its exchanges move it, and a bench sample has
 *  the ESC's figures and the INA228's totals of the same poll. */
#define SENSE_LINK_READ_MS 40u

/** The INA228's totals from a SENSE read younger than this are used over
 *  BENCH's: two polls, so one read that went unanswered does not drop the
 *  count back to BENCH's 1 mAh and 0.1 Wh rounding for a sample. */
#define SENSE_LINK_TOTALS_MS 100u

/** SERVO_SENSE's windows and channel flags are due this long after the
 *  last read while the INA3221 is enabled: under the 50 ms poll, so every
 *  poll reads them and every 50 ms window is seen -- its mean the filter,
 *  its highest current and lowest voltage the spikes. */
#define SENSE_LINK_SERVO_MS 40u

/** A part enabled and not online this long after its set-up was taken, or
 *  first read, is said not to answer: the coprocessor scans the bus again
 *  every 1000 ms while a part is missing. */
#define SENSE_LINK_GRACE_MS 2500u

/** A bus frame refused on pins something else holds is offered again
 *  this often: freeing the pin on OUTPUTS or SUPPLY lets it through. */
#define SENSE_LINK_BUS_RETRY_MS 5000u

/** A SENSE read older than this gives no ESC figures. */
#define SENSE_LINK_STALE_MS 500u

/** The registers read from SENSE: FLAGS to the last, AS5600_STILL_MS's
 *  neighbour; and without the encoder FLAGS to ESC_FLAGS. */
#define SENSE_LINK_STATUS_COUNT ((unsigned)LINK_SN_COUNT - (unsigned)LINK_SN_FLAGS)
#define SENSE_LINK_STATUS_COUNT_V48 \
    ((unsigned)LINK_SN_COUNT_V48 - (unsigned)LINK_SN_FLAGS)
/** And from SERVO_SENSE: the three channels' windows, WINDOW and CH_FLAGS. */
#define SENSE_LINK_SERVO_COUNT ((unsigned)LINK_SS_CH_FLAGS + 1u)

/** What came of an exchange: ACK or DATA, a NACK's reason, or none. */
#define SENSE_LINK_NO_ANSWER (-1)
#define SENSE_LINK_ACK       0

/** The two parts, for the accessors. */
typedef enum {
    SENSE_LINK_INA228 = 0,
    SENSE_LINK_INA3221 = 1,
} sense_link_part_t;

/** The set-up as SETUP INTERFACES has it. */
typedef struct {
    bool     i228, i3221;     /**< enabled                              */
    bool     as5600;          /**< the output encoder, enabled          */
    int8_t   sda, scl;        /**< coprocessor GPIO; -1 not set         */
    uint8_t  i228_addr;       /**< 0x40 to 0x4F                         */
    uint16_t i228_uohm;       /**< shunt, uOhm                          */
    uint16_t i228_max_da;     /**< the current its range is set for, 0.1 A */
    uint8_t  i3221_addr;      /**< 0x40 to 0x43                         */
    uint16_t i3221_dmohm;     /**< shunt, 0.1 mOhm                      */
    uint8_t  i3221_ch;        /**< bits 0..2 for CH1 to CH3             */
} sense_setup_t;

/** Things the caller tells the operator about, each once. */
enum {
    /** A part is enabled and the coprocessor has no SENSE page. */
    SENSE_LINK_EV_NO_PAGE       = 0x0001,
    /** A part is enabled with SDA or SCL unset: none is enabled. */
    SENSE_LINK_EV_PINS_UNSET    = 0x0002,
    /** Both parts enabled on one address: neither is. */
    SENSE_LINK_EV_SAME_ADDR     = 0x0004,
    /** The page refused the bus frame: the pins, as a rule. */
    SENSE_LINK_EV_BUS_REFUSED   = 0x0008,
    /** The page refused the INA228's frame: its shunt and range. */
    SENSE_LINK_EV_I228_REFUSED  = 0x0010,
    /** The page refused the INA3221's frame. */
    SENSE_LINK_EV_I3221_REFUSED = 0x0020,
    /** The INA228 is enabled and does not answer at its address. */
    SENSE_LINK_EV_I228_SILENT   = 0x0040,
    SENSE_LINK_EV_I3221_SILENT  = 0x0080,
    /** Something answers at the INA228's address with another identity. */
    SENSE_LINK_EV_I228_WRONG    = 0x0100,
    SENSE_LINK_EV_I3221_WRONG   = 0x0200,
    /** SDA held low; the coprocessor clocks it free. */
    SENSE_LINK_EV_STUCK         = 0x0400,
    /** The INA228's current read at the end of its range. */
    SENSE_LINK_EV_I228_CLIPPED  = 0x0800,
    /** An INA3221 channel read 163.8 mV across its shunt; see
     *  sense_link_clipped_channel(). */
    SENSE_LINK_EV_I3221_CLIPPED = 0x1000,
    /** STATUS fault bit 6: the coprocessor saves nothing this boot. */
    SENSE_LINK_EV_STORE_OFF     = 0x2000,
    /** The encoder is enabled and the coprocessor is older than 4.9. */
    SENSE_LINK_EV_ENC_OLD       = 0x4000,
    /** The encoder is enabled and does not answer at 0x36, or something
     *  answers there that is not an AS5600. */
    SENSE_LINK_EV_ENC_SILENT    = 0x8000,
    /** The encoder answers and its magnet is missing, too weak or too
     *  strong; see sense_link_enc_magnet().  Said once until the field
     *  reads right, and again when a magnet that was weak or strong goes
     *  missing or a missing one comes back weak or strong: the first takes
     *  the angle away and the second gives it back. */
    SENSE_LINK_EV_ENC_MAGNET    = 0x10000,
};

/** The exchange sense_link_next() asks for. */
typedef enum {
    SENSE_LINK_OP_NONE = 0,
    SENSE_LINK_OP_READ_SETUP,   /**< SENSE 0 to 11: what the page holds  */
    SENSE_LINK_OP_OFF,          /**< SENSE 0 to 3, both parts off        */
    SENSE_LINK_OP_I228,         /**< SENSE 4 to 7                        */
    SENSE_LINK_OP_I3221,        /**< SENSE 8 to 11                       */
    SENSE_LINK_OP_BUS,          /**< SENSE 0 to 3                        */
    SENSE_LINK_OP_IDENTITY,     /**< IDENTITY, for the capability bits   */
    SENSE_LINK_OP_STATUS,       /**< SENSE 12 to 25, or to 31            */
    SENSE_LINK_OP_SERVO,        /**< SERVO_SENSE 0 to 13                 */
} sense_link_op_kind_t;

typedef struct {
    sense_link_op_kind_t kind;
    bool     write;           /**< a write of regs; otherwise a read   */
    uint8_t  page;            /**< link_page_id_t                      */
    uint8_t  off, n;
    uint16_t regs[4];         /**< a write's registers                 */
} sense_link_op_t;

typedef struct {
    bool     up;              /**< a coprocessor answers               */
    bool     page;            /**< the coprocessor has SENSE (4.7)     */
    bool     enc_page;        /**< and the encoder (4.9)               */

    /* What is asked, as the page is to hold it. */
    bool     want_set;
    bool     want_i228, want_i3221;   /**< enabled on SETUP, as asked */
    bool     want_enc;                /**< the encoder, likewise       */
    int8_t   want_sda, want_scl;      /**< as asked, -1 unset         */
    uint16_t want[LINK_SN_CONFIG_COUNT];
    uint32_t want_ms;         /**< when it last changed                */

    /* What the page holds, as read or written since the link came up. */
    bool     known;
    uint16_t held[LINK_SN_CONFIG_COUNT];
    uint32_t setup_ms;        /**< when the set-up in force was taken  */
    bool     refused_bus, refused_i228, refused_i3221;
    bool     bus_told;        /**< the bus refusal said for this edit  */
    bool     bus_retry;       /**< the refused pair may be let go of    */
    uint32_t bus_refused_ms;
    bool     caps_owed;       /**< a write taken; identity not re-read */

    /* The exchange in flight. */
    sense_link_op_kind_t pending;
    uint16_t out[4];

    /* The reads. */
    bool     asked_status, asked_servo;
    uint32_t status_asked_ms, servo_asked_ms;
    bool     have_status, have_servo;
    uint16_t status[SENSE_LINK_STATUS_COUNT];
    uint16_t servo[SENSE_LINK_SERVO_COUNT];
    uint32_t status_ms;
    uint32_t status_reads;    /**< SENSE reads answered, since init    */
    bool     window_taken;    /**< a window has gone to the log        */
    uint16_t window_last;     /**< the number of the last one          */
    bool     caps_new;
    uint16_t caps;

    /* What has been said. */
    uint16_t was_flags;       /**< FLAGS as last read under this set-up */
    uint16_t was_clipped;     /**< CH_FLAGS' clipped bits, likewise    */
    bool     silent_told[2];
    bool     online_seen[2];
    bool     enc_silent_told, enc_online_seen;
    bool     enc_magnet_told;       /**< the magnet event is said          */
    uint16_t enc_magnet;            /**< the magnet bits of the last read   */
    uint16_t was_faults;
    uint8_t  clipped_ch;
    uint8_t  clip_pending;    /**< INA3221 channels clipped, not yet
                                   handed out: bit n-1 for CHn          */
    uint16_t ev_id[2];        /**< the ID a WRONG event was raised with */
    uint8_t  ev_found[2];     /**< the address a SILENT event found     */
    uint32_t events;
    bool     event_given;     /**< an event has been handed out        */
    uint32_t event_ms;        /**< when                                */
} sense_link_t;

void sense_link_init(sense_link_t *s);

/** The link went: nothing on the page is known, and nothing is read or
 *  written until it comes up again. */
void sense_link_lost(sense_link_t *s);

/**
 * A coprocessor answered with protocol minor @p minor.  Below
 * SENSE_LINK_MINOR it has neither page and nothing is sent to it, and a
 * part enabled on SETUP raises SENSE_LINK_EV_NO_PAGE.
 */
void sense_link_came_up(sense_link_t *s, uint16_t minor, uint32_t now_ms);

/** The set-up SETUP names now.  A part enabled while a coprocessor without
 *  the page answers raises SENSE_LINK_EV_NO_PAGE.  A set-up with a value
 *  outside the page's range -- a zeroed snapshot -- is ignored: nothing is
 *  asked, and nothing is written for it. */
void sense_link_want(sense_link_t *s, const sense_setup_t *w,
                     uint32_t now_ms);

/**
 * The next exchange owed at @p now_ms, into @p op; false for none.  Writes
 * only while @p idle: the bank is disarmed here and at the far end.  The
 * caller makes the exchange and reports with sense_link_done().
 */
bool sense_link_next(sense_link_t *s, uint32_t now_ms, bool idle,
                     sense_link_op_t *op);

/**
 * How the exchange sense_link_next() gave went: SENSE_LINK_ACK with the
 * registers read (@p regs, op.n of them; NULL for a write), a NACK
 * reason, or SENSE_LINK_NO_ANSWER.
 */
void sense_link_done(sense_link_t *s, int result, const uint16_t *regs,
                     uint32_t now_ms);

/** STATUS's faults word, as the caller last read it. */
void sense_link_faults(sense_link_t *s, uint16_t faults);

/** The events since the last call, SENSE_LINK_EV_*, and cleared. */
uint32_t sense_link_events(sense_link_t *s);

/** How long an event handed out by sense_link_event() has the alert band
 *  before the next one is handed out. */
#define SENSE_LINK_EVENT_GAP_MS 5000u

/**
 * One event for the alert band, which shows one line and replaces it with
 * the next: the most pressing waiting -- no page, a set-up not sent or
 * refused, a stuck bus, a part not answering, another identity, a clipped
 * reading, the store off -- cleared, and the rest kept.  0 while none waits
 * or while the last one handed out is younger than SENSE_LINK_EVENT_GAP_MS
 * at @p now_ms.
 */
uint32_t sense_link_event(sense_link_t *s, uint32_t now_ms);

/**
 * Put back an event sense_link_event() handed out that never reached the
 * band -- another alert replaced it first -- with what it was raised
 * with, so it is handed out again after the gap.  A clipped event takes
 * its channel back.  An event about a coprocessor is not put back once the
 * link has gone.
 */
void sense_link_event_back(sense_link_t *s, uint32_t ev);

/** Whether nothing is owed: the page holds the set-up asked, less the
 *  frames it refused. */
bool sense_link_settled(const sense_link_t *s);

/** The capability bits an identity read since the last call brought, into
 *  @p caps; false if none did. */
bool sense_link_take_caps(sense_link_t *s, uint16_t *caps);

/** FLAGS as last read, 0 before any read. */
uint16_t sense_link_flags(const sense_link_t *s);

/**
 * The INA3221's last window into @p b for the log, once per window: true
 * and servo_new set when the last SERVO_SENSE read holds a window with
 * readings whose number has not been taken before; false otherwise, @p b
 * untouched.
 */
bool sense_link_take_window(sense_link_t *s, bench_state_t *b);

/** SENSE reads answered since init: a caller that compares two counts can
 *  tell a read made in between. */
uint32_t sense_link_reads(const sense_link_t *s);

/**
 * The ESC's own telemetry voltage and current, from the last SENSE read
 * if it is younger than SENSE_LINK_STALE_MS, each with its valid bit.
 * False, both bits clear, otherwise.
 */
bool sense_link_esc(const sense_link_t *s, uint32_t now_ms, bool *v_ok,
                    float *volts, bool *i_ok, float *amps);

/** The INA228's charge in 0.01 mAh and energy in 0.01 Wh as last read,
 *  0 while they are not the run's totals.  False before any read and for a
 *  read older than SENSE_LINK_TOTALS_MS at @p now_ms. */
bool sense_link_totals(const sense_link_t *s, uint32_t now_ms,
                       int32_t *charge_cmah, uint32_t *energy_cwh);

/** The output encoder's angle as last read. */
typedef struct {
    uint16_t raw;         /**< RAW ANGLE, 0 to 4095                       */
    uint16_t samples;     /**< angle reads, modulo 65536                  */
    uint16_t still_ms;    /**< held within the tolerance, ms, at the read */
    uint32_t taken_ms;    /**< when the panel had the read                */
    bool     weak, strong;/**< STATUS ML, MH: a position, its noise not
                               specified (as5600.h)                       */
} sense_link_enc_t;

/**
 * The encoder's last angle into @p out: true when the page enables it, the
 * last SENSE read is younger than SENSE_LINK_STALE_MS at @p now_ms, the
 * part was online in it, the angle holds a reading of this set-up and that
 * reading's STATUS has MD set: a magnet is detected, and RAW ANGLE is a
 * position.  False, @p out untouched, otherwise.  This is the one place an
 * angle leaves the link, so no caller has an angle read without a magnet.
 */
bool sense_link_enc(const sense_link_t *s, uint32_t now_ms,
                    sense_link_enc_t *out);

/** Whether sense_link_enc() gives no angle at @p now_ms because the part
 *  answers and sees no magnet: every condition of sense_link_enc() but MD
 *  holds. */
bool sense_link_enc_no_magnet(const sense_link_t *s, uint32_t now_ms);

/** The magnet bits SENSE_LINK_EV_ENC_MAGNET was raised with:
 *  LINK_SN_ENC_MD, _ML and _MH, as read. */
uint16_t sense_link_enc_magnet(const sense_link_t *s);

/** Whether the encoder is set up on the page: enabled there, and read. */
bool sense_link_enc_on(const sense_link_t *s);

/** The address @p part is set up at on the page, 0 while not known. */
uint8_t sense_link_addr(const sense_link_t *s, sense_link_part_t part);

/** The ID @p part gave, as last read (DEVICE_ID, the die ID). */
uint16_t sense_link_id(const sense_link_t *s, sense_link_part_t part);

/**
 * Another address that answered the last scan where @p part could be: an
 * INA228 at 0x40 to 0x4F, an INA3221 at 0x40 to 0x43, neither at the
 * other part's address while that one is enabled.  The lowest; 0 for none.
 */
uint8_t sense_link_found(const sense_link_t *s, sense_link_part_t part);

/**
 * The INA3221 channel SENSE_LINK_EV_I3221_CLIPPED names, 1 to 3: the one
 * sense_link_event() last handed out.  Channels that clip together are
 * handed out one each, the event standing until the last.
 */
uint8_t sense_link_clipped_channel(const sense_link_t *s);

/**
 * What an event was raised with, kept for when the band shows it: the ID
 * @p part gave for SENSE_LINK_EV_*_WRONG, and the address the scan found
 * for SENSE_LINK_EV_*_SILENT (0 none).  A later read can show another; a
 * part that answers as itself before the band shows either drops it.
 */
uint16_t sense_link_event_id(const sense_link_t *s, sense_link_part_t part);
uint8_t  sense_link_event_found(const sense_link_t *s, sense_link_part_t part);

/** The INA3221's shunt on the page, 0.1 mOhm. */
uint16_t sense_link_i3221_dmohm(const sense_link_t *s);

/** The pins asked, -1 for unset: what SENSE_LINK_EV_BUS_REFUSED names. */
int sense_link_sda(const sense_link_t *s);
int sense_link_scl(const sense_link_t *s);

#ifdef __cplusplus
}
#endif
