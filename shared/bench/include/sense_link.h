/**
 * @file
 * @brief The panel's half of the SENSE (0x2B), SERVO_SENSE (0x2C) and SERVO_WIN
 *        (0x31) link pages: what is written to SENSE, in what order, what is
 *        read from the three pages and how often, and what a read means for the
 *        operator.
 *
 * The IOmcu reads the two current monitors and the output encoder (an
 * AS5600 angle sensor); the panel sets them up from SETUP INTERFACES and
 * reads what they measured.  An IOmcu whose protocol minor is below
 * SENSE_LINK_MINOR has neither page, and nothing is sent to it: not a write
 * and not a read.  One below SENSE_LINK_ENC_MINOR has no encoder: ENABLE
 * bit 2 is not written, SENSE 26 to 31 are not read, and an encoder enabled
 * on SETUP raises SENSE_LINK_EV_ENC_OLD.  One below SENSE_LINK_WIN_MINOR
 * has no SERVO_WIN and no RESETS: nothing is sent to SERVO_WIN, and the
 * INA3221's windows are SERVO_SENSE's last one per read.
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
 * SENSE_LINK_SETTLE_MS since its last edit, because the IOmcu keeps
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
 * -- are read every SENSE_LINK_READ_MS and, while it enables the INA3221,
 * SERVO_SENSE 0 to 13 every SENSE_LINK_SERVO_MS.  From those reads come
 * the events the caller tells the operator about, each once: a part
 * enabled and not answering, a part that answers with another identity,
 * the bus stuck, and a reading clipped at the top of its range.
 *
 * The window ring.  While the page of a 4.11 IOmcu enables the
 * INA3221 with CH1, SERVO_WIN's registers 0 to 15 -- the header and the two
 * newest windows -- are read every SENSE_LINK_WIN_MS, and SENSE's status
 * read goes to register 31 for RESETS, as it does with the encoder.  Each CH1 window number is handed
 * over once, oldest first (sense_link_take_win()):
 *
 *   - a read that owes one or two windows hands them over;
 *   - SENSE_LINK_WIN_ALL_MS after the last read that left nothing owed,
 *     the read is the whole page, 28 registers, so every window owed
 *     comes from one reply.  Entries of two replies are not joined: a
 *     window boundary between them shifts every entry by one number;
 *   - a read of registers 0 to 15 that owes more than its two windows
 *     hands nothing over and asks for the whole page in the same poll;
 *   - a number more than LINK_SW_RING behind the newest has left the ring
 *     and is counted (sense_link_win_lost());
 *   - an entry whose CLOSED bit is clear is a number the IOmcu
 *     skipped: nothing is handed over for it and nothing is counted.  A
 *     skipped number that left the ring before a read is counted as a
 *     window is: the page no longer says which it was;
 *   - a reply lost on the link moves nothing: the next read owes the same
 *     windows and those closed since;
 *   - the first read after a link-up hands nothing over and starts after
 *     the newest number it shows, so a window read before the link went is
 *     not handed over again;
 *   - a set-up this module wrote, a read without LINK_SW_HAVE, and a newest
 *     number behind the last one handed over mean the ring started again:
 *     every closed entry of the next read is owed;
 *   - at most LINK_SW_RING windows wait to be taken; a read that brings
 *     more leaves the rest owed on the page.
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

/** @brief The protocol minor that has SENSE and SERVO_SENSE. */
#define SENSE_LINK_MINOR 7u
/** @brief The protocol minor that has SENSE's output encoder. */
#define SENSE_LINK_ENC_MINOR 9u
/** @brief The protocol minor that has SERVO_WIN and SENSE's RESETS. */
#define SENSE_LINK_WIN_MINOR LINK_MINOR_SERVO_WIN

/**
 * @brief How long a set-up rests after its last edit before it is written: a
 *        held  + or - on SETUP steps a value about 30 times a second, and each
 *        write is  a flash save at the IOmcu.
 */
#define SENSE_LINK_SETTLE_MS 500u

/**
 * @brief SENSE's read-only registers are due this long after the last read,
 *        while a part is enabled: under the 50ms poll, so every BENCH poll
 *        reads  them -- 20Hz -- however its exchanges move it, and a bench
 *        sample has  the ESC's figures and the INA228's totals of the same
 *        poll.
 */
#define SENSE_LINK_READ_MS 40u

/**
 * @brief The INA228's totals from a SENSE read younger than this are used over
 *        BENCH's: two polls, so one read that went unanswered does not drop the
 *        count back to BENCH's 1mAh and 0.1Wh rounding for a sample.
 */
#define SENSE_LINK_TOTALS_MS 100u

/**
 * @brief SERVO_SENSE's windows and channel flags are due this long after the
 *        last read while the INA3221 is enabled: under the 50ms poll, so every
 *        poll reads them and every 50ms window is seen -- its mean the filter,
 *        its highest current and lowest voltage the spikes.
 */
#define SENSE_LINK_SERVO_MS 40u

/**
 * @brief SERVO_WIN's header and two newest windows are due this long after the
 *        last read while the INA3221 is enabled with CH1: under the 50ms poll,
 *        so every poll reads them.
 */
#define SENSE_LINK_WIN_MS 40u

/** @brief The registers of that read, and the windows they carry. */
#define SENSE_LINK_WIN_HEAD ((unsigned)LINK_SW_ENTRY(2, 0))
#define SENSE_LINK_WIN_HEAD_WINDOWS 2u

/**
 * @brief The whole page is read instead once this long has passed since a read
 *        last left no window owed: two windows, after which a third can have
 *        closed.
 */
#define SENSE_LINK_WIN_ALL_MS 100u

/**
 * @brief A part enabled and not online this long after its set-up was taken, or
 *        first read, is said not to answer: the IOmcu scans the bus again
 *        every 1000ms while a part is missing.
 */
#define SENSE_LINK_GRACE_MS 2500u

/**
 * @brief A bus frame refused on pins something else holds is offered again
 *        this often: freeing the pin on OUTPUTS or SUPPLY lets it through.
 */
#define SENSE_LINK_BUS_RETRY_MS 5000u

/** @brief A SENSE read older than this gives no ESC figures. */
#define SENSE_LINK_STALE_MS 500u

/**
 * @brief The registers read from SENSE: FLAGS to the last, AS5600_STILL_MS's
 *        neighbour; and without the encoder FLAGS to ESC_FLAGS.
 */
#define SENSE_LINK_STATUS_COUNT ((unsigned)LINK_SN_COUNT - (unsigned)LINK_SN_FLAGS)
#define SENSE_LINK_STATUS_COUNT_V48 \
    ((unsigned)LINK_SN_COUNT_V48 - (unsigned)LINK_SN_FLAGS)
/**
 * @brief And from SERVO_SENSE: the three channels' windows, WINDOW and
 *        CH_FLAGS.
 */
#define SENSE_LINK_SERVO_COUNT ((unsigned)LINK_SS_CH_FLAGS + 1u)

/** @brief What came of an exchange: ACK or DATA, a NACK's reason, or none. */
#define SENSE_LINK_NO_ANSWER (-1)
#define SENSE_LINK_ACK       0

/** @brief The two parts, for the accessors. */
typedef enum {
    SENSE_LINK_INA228 = 0,
    SENSE_LINK_INA3221 = 1,
} sense_link_part_t;

/** @brief The set-up as SETUP INTERFACES has it. */
typedef struct {
    bool     i228, i3221;     /**< enabled                              */
    bool     as5600;          /**< the output encoder, enabled          */
    int8_t   sda;             /**< IOmcu GPIO of the data line; -1 not set */
    int8_t   scl;             /**< IOmcu GPIO of the clock line; -1 not set */
    uint8_t  i228_addr;       /**< 0x40 to 0x4F                         */
    uint16_t i228_uohm;       /**< shunt, uOhm                          */
    uint16_t i228_max_da;     /**< the current its range is set for, 0.1A */
    uint8_t  i3221_addr;      /**< 0x40 to 0x43                         */
    uint16_t i3221_dmohm;     /**< shunt, 0.1mOhm                       */
    uint8_t  i3221_ch;        /**< bits 0..2 for CH1 to CH3             */
    /**
     * @brief Bit a: the INA3221 at 0x40 + a is fitted (SETUP > SENSORS); the
     *        INA228 on any of them is one address.
     *
     * 0: i3221_addr alone.
     */
    uint8_t  i3221_parts;
    /**
     * @brief No fitted channel watches the servo tester's target (SENSORS'
     *        FAST is none): the INA3221 is not the servo rail's meter.
     */
    bool     i3221_unwatched;
    /**
     * @brief FAST is CH2 or CH3 of its part: without the map the INA3221 is
     *        not the servo rail's meter then, as an IOmcu older than 4.25 reads
     *        CH1 at 1kHz whatever watches.
     */
    bool     i3221_fast_off_ch1;
} sense_setup_t;

/** @brief Things the caller tells the operator about, each once. */
enum {
    /** @brief A part is enabled and the IOmcu has no SENSE page. */
    SENSE_LINK_EV_NO_PAGE       = 0x0001,
    /** @brief A part is enabled with SDA or SCL unset: none is enabled. */
    SENSE_LINK_EV_PINS_UNSET    = 0x0002,
    /** @brief Both parts enabled on one address: neither is. */
    SENSE_LINK_EV_SAME_ADDR     = 0x0004,
    /** @brief The page refused the bus frame: the pins, as a rule. */
    SENSE_LINK_EV_BUS_REFUSED   = 0x0008,
    /** @brief The page refused the INA228's frame: its shunt and range. */
    SENSE_LINK_EV_I228_REFUSED  = 0x0010,
    /** @brief The page refused the INA3221's frame. */
    SENSE_LINK_EV_I3221_REFUSED = 0x0020,
    /** @brief The INA228 is enabled and does not answer at its address. */
    SENSE_LINK_EV_I228_SILENT   = 0x0040,
    SENSE_LINK_EV_I3221_SILENT  = 0x0080,
    /**
     * @brief Something answers at the INA228's address with another identity.
     */
    SENSE_LINK_EV_I228_WRONG    = 0x0100,
    SENSE_LINK_EV_I3221_WRONG   = 0x0200,
    /** @brief SDA held low; the IOmcu clocks it free. */
    SENSE_LINK_EV_STUCK         = 0x0400,
    /** @brief The INA228's current read at the end of its range. */
    SENSE_LINK_EV_I228_CLIPPED  = 0x0800,
    /**
     * @brief An INA3221 channel read 163.8mV across its shunt; see
     *        sense_link_clipped_channel().
     */
    SENSE_LINK_EV_I3221_CLIPPED = 0x1000,
    /** @brief STATUS fault bit 6: the IOmcu saves nothing this boot. */
    SENSE_LINK_EV_STORE_OFF     = 0x2000,
    /** @brief The encoder is enabled and the IOmcu is older than 4.9. */
    SENSE_LINK_EV_ENC_OLD       = 0x4000,
    /**
     * @brief The encoder is enabled and does not answer at 0x36, or something
     *        answers there that is not an AS5600.
     */
    SENSE_LINK_EV_ENC_SILENT    = 0x8000,
    /**
     * @brief The encoder answers and its magnet is missing, too weak or too
     *        strong; see sense_link_enc_magnet().
     *
     * Said once while the magnet
     *  bits (MD, ML, MH) stay as they were said, across the set-ups an
     *  edit on SETUP writes; said again when a bit changes -- a weak
     *  magnet that goes missing, a missing one that comes back weak, a
     *  weak one that reads strong -- when the field read right in between,
     *  after the encoder was said not to answer, after a set-up that
     *  switches it off, and after a link-up.
     */
    SENSE_LINK_EV_ENC_MAGNET    = 0x10000,
};

/**
 * @brief The events about the set-up asked, which sense_link_event_stands()
 *        judges: what ends them is an edit on SETUP and a write the page takes.
 */
#define SENSE_LINK_EV_SETUP ((uint32_t)(SENSE_LINK_EV_PINS_UNSET        \
                                        | SENSE_LINK_EV_SAME_ADDR       \
                                        | SENSE_LINK_EV_BUS_REFUSED     \
                                        | SENSE_LINK_EV_I228_REFUSED    \
                                        | SENSE_LINK_EV_I3221_REFUSED))

/** @brief The exchange sense_link_next() asks for. */
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
    SENSE_LINK_OP_WIN,          /**< SERVO_WIN 0 to 15                   */
    SENSE_LINK_OP_WIN_ALL,      /**< SERVO_WIN 0 to 27                   */
} sense_link_op_kind_t;

/**
 * @brief One exchange the SENSE link asks the caller to make.
 */
typedef struct {
    sense_link_op_kind_t kind;
    bool     write;           /**< a write of regs; otherwise a read   */
    uint8_t  page;            /**< link_page_id_t                      */
    uint8_t  off;             /**< first register of the page          */
    uint8_t  n;               /**< registers                           */
    uint16_t regs[4];         /**< a write's registers                 */
} sense_link_op_t;

/** @brief One window of the IOmcu's, in ms: SENSE_WINDOW_MS. */
#define SENSE_LINK_WINDOW_MS 50u

/**
 * @brief One 50ms window of INA3221 CH1, as SERVO_WIN carries it.
 *
 * A clipped
 *  sample counts in the three currents at the end of the range it read.
 */
typedef struct {
    uint16_t number;      /**< the window's, modulo 65536                 */
    bool     current;     /**< it holds current samples                   */
    bool     voltage;     /**< it holds bus voltage samples               */
    bool     clip_hi;     /**< a sample read the top of the range         */
    bool     clip_lo;     /**< a sample read the bottom                   */
    uint8_t  clipped;     /**< samples at an end, held at 255             */
    int16_t  mean_ma;     /**< mA, signed                                 */
    int16_t  max_ma;      /**< the highest sample, mA, signed             */
    int16_t  min_ma;      /**< the lowest sample, mA, signed              */
    uint16_t mean_mv;     /**< bus voltage at the load side of the shunt  */
    uint16_t min_mv;      /**< the lowest bus voltage sample, mV          */
    /**
     * @brief The latest the window can have closed, on the panel's tick: when
     *        the panel had the read that brought it, less SENSE_LINK_WINDOW_MS
     *        for each window that closed after it and came in the same read.
     *
     * The windows of one read are 50ms apart, oldest first.
     */
    uint32_t taken_ms;
    bool     stamped;     /**< taken_ms is set; 0 is a tick like any other */
} sense_link_win_t;

/**
 * @brief What the servo rail's meter is chosen from (servo_source.h), as of the
 *        last exchange.
 */
typedef struct {
    bool     page;        /**< the IOmcu has SENSE                  */
    /**
     * @brief SETUP has the INA3221 on and a channel watching the tester's
     *        target: with the map in force, FAST; without it, CH1.
     */
    bool     wanted;
    /**
     * @brief SENSE's registers 0 to 11 are the set-up asked, with the INA3221
     *        enabled on CH1: no write owed and no frame refused.
     */
    bool     held;
    uint16_t setups;      /**< set-ups taken and links lost, modulo 65536:
                               moves when the reads below start again     */
    bool     status;      /**< a SENSE read answered under this set-up    */
    uint32_t status_ms;   /**< when                                       */
    uint16_t flags;       /**< its FLAGS                                  */
    bool     resets_read; /**< it carried RESETS (4.11)                   */
    uint8_t  resets;      /**< the INA3221's count, modulo 256            */
    /**
     * @brief SERVO_WIN showed a window under this set-up, and no read has found
     *        its number standing for SENSE_LINK_STALE_MS.
     */
    bool     win;
    bool     win_valid;   /**< the newest closed with current samples     */
    uint32_t win_ms;      /**< when the newest number last moved          */
} sense_link_meter_t;

/**
 * @brief The panel's state of the SENSE link page.
 */
typedef struct {
    bool     up;              /**< an IOmcu answers               */
    bool     page;            /**< the IOmcu has SENSE (4.7)     */
    bool     enc_page;        /**< and the encoder (4.9)               */
    bool     win_page;        /**< and SERVO_WIN and RESETS (4.11)     */
    /**
     * @brief The INA3221 parts go by SENSE_MAP (4.25, sense_map_link.h):
     *        SENSE's  ENABLE bit 1 and registers 8 to 11 mirror the map and are
     *        never  written.
     */
    bool     map;
    /**
     * @brief With the map in force: SENSE_MAP holds the map asked, so FAST and
     *        its shunt are the ones SETUP names (sense_link_set_map_held()).
     */
    bool     map_held;

    /* What is asked, as the page is to hold it. */
    bool     want_set;
    bool     want_i228, want_i3221;   /**< enabled on SETUP, as asked */
    bool     want_enc;                /**< the encoder, likewise       */
    int8_t   want_sda, want_scl;      /**< as asked, -1 unset         */
    uint8_t  want_parts;              /**< sense_setup_t.i3221_parts  */
    bool     want_unwatched;          /**< and .i3221_unwatched       */
    bool     want_off_ch1;            /**< and .i3221_fast_off_ch1    */
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

    /* The window ring. */
    bool     asked_win;
    uint32_t win_asked_ms;
    bool     win_all;         /**< the whole page is owed               */
    bool     win_synced;      /**< win_next is the next number owed     */
    bool     win_restart;     /**< every closed entry of the next read
                                   is owed                              */
    uint16_t win_next;
    uint32_t win_sync_ms;     /**< when a read last left nothing owed   */
    uint32_t win_lost;        /**< numbers that left the ring, since init */
    bool     win_have;        /**< a read showed LINK_SW_HAVE           */
    bool     win_valid;       /**< its newest entry holds current       */
    uint16_t win_newest;
    uint32_t win_moved_ms;    /**< when win_newest last moved           */
    bool     win_stands;      /**< a read found it SENSE_LINK_STALE_MS
                                   old or more; until it moves          */
    uint8_t  winq_n;          /**< windows waiting in winq, oldest first */
    sense_link_win_t winq[LINK_SW_RING];
    uint16_t setups;          /**< set-ups taken and links lost         */
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

/**
 * @brief Sets up the SENSE link state.
 *
 * @param s The link state to set up.
 */
void sense_link_init(sense_link_t *s);

/**
 * @brief The link went: nothing on the page is known, and nothing is read or
 *        written until it comes up again.
 *
 * @param s The link state.
 */
void sense_link_lost(sense_link_t *s);

/**
 * @brief An IOmcu answered with protocol minor @p minor.
 *
 * Below SENSE_LINK_MINOR it has neither page and nothing is sent to it, and a
 * part enabled on SETUP raises SENSE_LINK_EV_NO_PAGE.
 *
 * @param s      The link state.
 * @param minor  The IOmcu's protocol minor version.
 * @param now_ms The time, in ms.
 */
void sense_link_came_up(sense_link_t *s, uint16_t minor, uint32_t now_ms);

/**
 * @brief The set-up SETUP names now.
 *
 * A part enabled while an IOmcu without
 *  the page answers raises SENSE_LINK_EV_NO_PAGE.  A set-up with a value
 *  outside the page's range -- a zeroed snapshot -- is ignored: nothing is
 *  asked, and nothing is written for it.
 *
 * @param s      The link state.
 * @param w      The set-up SETUP names.
 * @param now_ms The time, in ms.
 */
void sense_link_want(sense_link_t *s, const sense_setup_t *w,
                     uint32_t now_ms);

/**
 * @brief The next exchange owed at @p now_ms, into @p op; false for none.
 *
 * Writes only while @p idle: the bank is disarmed here and at the far end.  The
 * caller makes the exchange and reports with sense_link_done().
 *
 * @param s      The link state.
 * @param now_ms The time, in ms.
 * @param idle   true while the bank is disarmed here and at the far end.
 * @param op     Receives the exchange owed.
 * @return true when an exchange is owed.
 */
bool sense_link_next(sense_link_t *s, uint32_t now_ms, bool idle,
                     sense_link_op_t *op);

/**
 * @brief How the exchange sense_link_next() gave went: SENSE_LINK_ACK with the
 *        registers read (@p regs, op.n of them; NULL for a write), a NACK
 *        reason, or SENSE_LINK_NO_ANSWER.
 *
 * @param s      The link state.
 * @param result The result: an acknowledgement, a NACK's reason or
 *               SENSE_LINK_NO_ANSWER.
 * @param regs   The registers read, or NULL for a write.
 * @param now_ms The time, in ms.
 */
void sense_link_done(sense_link_t *s, int result, const uint16_t *regs,
                     uint32_t now_ms);

/**
 * @brief STATUS's faults word, as the caller last read it.
 *
 * @param s      The link state.
 * @param faults The STATUS faults word.
 */
void sense_link_faults(sense_link_t *s, uint16_t faults);

/**
 * @brief The events since the last call, SENSE_LINK_EV_*, and cleared.
 *
 * @param s The link state.
 * @return The events as SENSE_LINK_EV_* bits.
 */
uint32_t sense_link_events(sense_link_t *s);

/**
 * @brief How long an event handed out by sense_link_event() has the alert band
 *        before the next one is handed out.
 */
#define SENSE_LINK_EVENT_GAP_MS 5000u

/**
 * @brief One event for the alert band, which shows one line and replaces it
 *        with the next: the most pressing waiting -- no page, a set-up not sent
 *        or refused, a stuck bus, a part not answering, another identity, a
 *        clipped reading, the store off -- cleared, and the rest kept.
 *
 * 0 while none waits or while the last one handed out is younger than
 * SENSE_LINK_EVENT_GAP_MS at @p now_ms.
 *
 * @param s      The link state.
 * @param now_ms The time, in ms.
 * @return The event bit for the alert band, or 0 for none.
 */
uint32_t sense_link_event(sense_link_t *s, uint32_t now_ms);

/**
 * @brief Whether what @p ev, one SENSE_LINK_EV_* bit, says still holds now.
 *
 * For the events about the set-up asked: SENSE_LINK_EV_PINS_UNSET and
 * SENSE_LINK_EV_SAME_ADDR while the settings ask it; a refusal --
 * SENSE_LINK_EV_BUS_REFUSED, _I228_REFUSED, _I3221_REFUSED -- until the page is
 * known to hold the frame as asked: not refused, and written and taken.  A
 * refused value stepped back on SETUP ends it once the IOmcu takes the frame;
 * the rest and the write take SENSE_LINK_SETTLE_MS and a poll.  With no IOmcu
 * answering a refusal stands.  True for every other event: what a read raised
 * ends with the band's own time.
 *
 * The caller takes back an alert it showed for such an event once this
 * is false; sense_link_event() drops one waiting that is over.
 *
 * @param s  The link state.
 * @param ev One SENSE_LINK_EV_* bit.
 * @return true while the cause of the event holds.
 */
bool sense_link_event_stands(const sense_link_t *s, uint32_t ev);

/**
 * @brief Put back an event sense_link_event() handed out that never reached the
 *        band -- another alert replaced it first -- with what it was raised
 *        with, so it is handed out again after the gap.
 *
 * A clipped event takes its channel back.  An event about an IOmcu is not put
 * back once the link has gone.
 *
 * @param s  The link state.
 * @param ev The event to put back.
 */
void sense_link_event_back(sense_link_t *s, uint32_t ev);

/**
 * @brief Whether nothing is owed: the page holds the set-up asked, less the
 *        frames it refused.
 *
 * @param s The link state.
 * @return true when nothing is owed.
 */
bool sense_link_settled(const sense_link_t *s);

/** @brief The rows of SETUP INTERFACES, for sense_link_unheld(). */
enum {
    SENSE_LINK_ROW_I228        = 0x0001,  /**< INA228 on or off         */
    SENSE_LINK_ROW_I3221       = 0x0002,  /**< INA3221 on or off        */
    SENSE_LINK_ROW_ENC         = 0x0004,  /**< AS5600 on or off         */
    SENSE_LINK_ROW_PINS        = 0x0008,  /**< Sensor SDA and SCL       */
    SENSE_LINK_ROW_I228_ADDR   = 0x0010,
    SENSE_LINK_ROW_I228_SHUNT  = 0x0020,
    SENSE_LINK_ROW_I228_MAX    = 0x0040,
    SENSE_LINK_ROW_I3221_ADDR  = 0x0080,
    SENSE_LINK_ROW_I3221_SHUNT = 0x0100,
    SENSE_LINK_ROW_I3221_CH    = 0x0200,
};

/**
 * @brief The rows whose value as asked is not the one the page holds,
 *        SENSE_LINK_ROW_*: a set-up not written yet -- it rests
 *        SENSE_LINK_SETTLE_MS and waits for an idle bank -- one the page
 *        refused, and a part that is on in the settings and off on the page
 *        because its frame was refused, its pins are unset, both monitors share
 *        an address or the IOmcu is older than the part.
 *
 * The comparison is want against held, the two this module writes from.  0
 * while no IOmcu answers and before the page has been read: nothing is known to
 * compare with.  An IOmcu without the page holds no part, and the parts that
 * are on are named.
 *
 * @param s The link state.
 * @return The SENSE_LINK_ROW_* bits of the rows not held as asked.
 */
uint16_t sense_link_unheld(const sense_link_t *s);

/**
 * @brief Whether the INA3221 parts go by SENSE_MAP (sense_map_link_page()):
 *        then ENABLE bit 1 and the INA3221's frame are the IOmcu's mirror of
 *        the map, kept as read and never written, and refuse nothing.
 *
 * @param s   The link state.
 * @param map true when the INA3221 parts go by SENSE_MAP.
 */
void sense_link_set_map(sense_link_t *s, bool map);

/**
 * @brief Whether SENSE_MAP holds the map SETUP asks (sense_map_link_held()):
 *        with the map in force the meter is held only then.
 *
 * @param s    The link state.
 * @param held true while SENSE_MAP holds the map SETUP asks.
 */
void sense_link_set_map_held(sense_link_t *s, bool held);

/**
 * @brief Read SENSE's set-up again, and the identity after it: a SENSE_MAP
 *        write  was taken, and the mirror registers moved with it.
 *
 * @param s The link state.
 */
void sense_link_reread(sense_link_t *s);

/**
 * @brief The capability bits an identity read since the last call brought, into
 *        @p caps; false if none did.
 *
 * @param s    The link state.
 * @param caps Receives the capability bits.
 * @return true when an identity read since the last call brought bits.
 */
bool sense_link_take_caps(sense_link_t *s, uint16_t *caps);

/**
 * @brief FLAGS as last read, 0 before any read.
 *
 * @param s The link state.
 * @return FLAGS as last read.
 */
uint16_t sense_link_flags(const sense_link_t *s);

/**
 * @brief Whether the INA3221's windows come from SERVO_WIN: the IOmcu has  the
 *        page and its SENSE page enables the INA3221 with CH1.
 *
 * @param s The link state.
 * @return true when the INA3221's windows come from SERVO_WIN.
 */
bool sense_link_win_on(const sense_link_t *s);

/**
 * @brief The next CH1 window of the ring into @p out, oldest first, each number
 *        once: false when none waits, @p out untouched.
 *
 * @param s   The link state.
 * @param out Receives the window.
 * @return true when a window was taken.
 */
bool sense_link_take_win(sense_link_t *s, sense_link_win_t *out);

/**
 * @brief Window numbers that left the ring before a read took them, since init,
 *        modulo 2^32: the windows lost, and with them any number the IOmcu
 *        skipped that was out of the ring by the read.
 *
 * A caller that compares
 *  two counts has those of the time in between.
 *
 * @param s The link state.
 * @return The windows lost since init, modulo 2^32.
 */
uint32_t sense_link_win_lost(const sense_link_t *s);

/**
 * @brief How many windows sense_link_take_window() hands over now: the ring's
 *        waiting ones, or without the ring 1 while SERVO_SENSE's last read
 *        holds a window not taken yet.
 *
 * @param s The link state.
 * @return The number of windows sense_link_take_window() hands over now.
 */
unsigned sense_link_windows(const sense_link_t *s);

/**
 * @brief A ring window into @p b's log fields: servo_new set, its number, CH1's
 *        mean and highest current and lowest bus voltage with servo_ok bit 0
 *        set while the window holds samples, servo_no_current and
 *        servo_no_voltage bit 0 for the quantity it holds no sample of, and
 *        CH2's and CH3's figures from SERVO_SENSE's last read when that read
 *        shows the same window number.
 *
 * @param s The link state.
 * @param w The ring window.
 * @param b Receives the log fields.
 */
void sense_link_win_bench(const sense_link_t *s, const sense_link_win_t *w,
                          bench_state_t *b);

/**
 * @brief The INA3221's next window into @p b for the log, once per window; true
 *        and servo_new set, false and @p b untouched otherwise.
 *
 * With the ring (sense_link_win_on()): the next CH1 window, as
 * sense_link_take_win() and sense_link_win_bench() give it.  Call until
 * false: a poll brings up to LINK_SW_RING windows.
 *
 * Without it: SERVO_SENSE's last read, when it holds a window with
 * readings whose number has not been taken before.
 *
 * @param s The link state.
 * @param b Receives the window for the log.
 * @return true when a window was taken.
 */
bool sense_link_take_window(sense_link_t *s, bench_state_t *b);

/**
 * @brief What the servo rail's meter is chosen from, into @p out.
 *
 * @param s   The link state.
 * @param out Receives the meter's choice.
 */
void sense_link_meter(const sense_link_t *s, sense_link_meter_t *out);

/**
 * @brief SENSE reads answered since init: a caller that compares two counts can
 *        tell a read made in between.
 *
 * @param s The link state.
 * @return The SENSE reads answered since init.
 */
uint32_t sense_link_reads(const sense_link_t *s);

/**
 * @brief The ESC's own telemetry voltage and current, from the last SENSE read
 *        if it is younger than SENSE_LINK_STALE_MS, each with its valid bit.
 *
 * False, both bits clear, otherwise.
 *
 * @param s      The link state.
 * @param now_ms The time, in ms.
 * @param v_ok   Receives the valid bit of the voltage.
 * @param volts  Receives the voltage, in V.
 * @param i_ok   Receives the valid bit of the current.
 * @param amps   Receives the current, in A.
 * @return true when the telemetry is available.
 */
bool sense_link_esc(const sense_link_t *s, uint32_t now_ms, bool *v_ok,
                    float *volts, bool *i_ok, float *amps);

/**
 * @brief The INA228's charge in 0.01mAh and energy in 0.01Wh as last read,  0
 *        while they are not the run's totals.
 *
 * False before any read and for a
 *  read older than SENSE_LINK_TOTALS_MS at @p now_ms.
 *
 * @param s           The link state.
 * @param now_ms      The time, in ms.
 * @param charge_cmah Receives the charge, in 0.01mAh.
 * @param energy_cwh  Receives the energy, in 0.01Wh.
 * @return true when the totals are valid.
 */
bool sense_link_totals(const sense_link_t *s, uint32_t now_ms,
                       int32_t *charge_cmah, uint32_t *energy_cwh);

/** @brief The output encoder's angle as last read. */
typedef struct {
    uint16_t raw;         /**< RAW ANGLE, 0 to 4095                       */
    uint16_t samples;     /**< angle reads, modulo 65536                  */
    uint16_t still_ms;    /**< held within the tolerance, ms, at the read */
    uint32_t taken_ms;    /**< when the panel had the read                */
    bool     weak;        /**< STATUS ML: a position, its noise not specified (as5600.h) */
    bool     strong;      /**< STATUS MH: a position, its noise not specified (as5600.h) */
} sense_link_enc_t;

/**
 * @brief The encoder's last angle into @p out: true when the page enables it,
 *        the last SENSE read is younger than SENSE_LINK_STALE_MS at @p now_ms,
 *        the part was online in it, the angle holds a reading of this set-up
 *        and that reading's STATUS has MD set: a magnet is detected, and RAW
 *        ANGLE is a position.
 *
 * False, @p out untouched, otherwise.  This is the one place an
 * angle leaves the link, so no caller has an angle read without a magnet.
 *
 * @param s      The link state.
 * @param now_ms The time, in ms.
 * @param out    Receives the angle.
 * @return true when an angle is given.
 */
bool sense_link_enc(const sense_link_t *s, uint32_t now_ms,
                    sense_link_enc_t *out);

/**
 * @brief Whether sense_link_enc() gives no angle at @p now_ms because the part
 *        answers and sees no magnet: every condition of sense_link_enc() but MD
 *        holds.
 *
 * @param s      The link state.
 * @param now_ms The time, in ms.
 * @return true when the encoder answers and sees no magnet.
 */
bool sense_link_enc_no_magnet(const sense_link_t *s, uint32_t now_ms);

/**
 * @brief The magnet bits SENSE_LINK_EV_ENC_MAGNET was raised with:
 *        LINK_SN_ENC_MD, _ML and _MH, as read.
 *
 * @param s The link state.
 * @return The magnet bits, as read.
 */
uint16_t sense_link_enc_magnet(const sense_link_t *s);

/**
 * @brief Whether the encoder is set up on the page: enabled there, and read.
 *
 * @param s The link state.
 * @return true when the encoder is set up on the page.
 */
bool sense_link_enc_on(const sense_link_t *s);

/**
 * @brief The address @p part is set up at on the page, 0 while not known.
 *
 * @param s    The link state.
 * @param part The part.
 * @return The address, or 0 while not known.
 */
uint8_t sense_link_addr(const sense_link_t *s, sense_link_part_t part);

/**
 * @brief The ID @p part gave, as last read (DEVICE_ID, the die ID).
 *
 * @param s    The link state.
 * @param part The part.
 * @return The ID the part gave.
 */
uint16_t sense_link_id(const sense_link_t *s, sense_link_part_t part);

/**
 * @brief Another address that answered the last scan where @p part could be: an
 *        INA228 at 0x40 to 0x4F, an INA3221 at 0x40 to 0x43, neither at the
 *        other part's address while that one is enabled.
 *
 * The lowest; 0 for none.
 *
 * @param s    The link state.
 * @param part The part.
 * @return The lowest other address that answered, or 0 for none.
 */
uint8_t sense_link_found(const sense_link_t *s, sense_link_part_t part);

/**
 * @brief The INA3221 channel SENSE_LINK_EV_I3221_CLIPPED names, 1 to 3: the one
 *        sense_link_event() last handed out.
 *
 * Channels that clip together are handed out one each, the event standing until
 * the last.
 *
 * @param s The link state.
 * @return The INA3221 channel, 1 to 3.
 */
uint8_t sense_link_clipped_channel(const sense_link_t *s);

/**
 * @brief What an event was raised with, kept for when the band shows it: the ID
 *        @p part gave for SENSE_LINK_EV_*_WRONG, and the address the scan found
 *        for SENSE_LINK_EV_*_SILENT (0 none).
 *
 * A later read can show another; a part that answers as itself before the band
 * shows either drops it.
 *
 * @param s    The link state.
 * @param part The part.
 * @return The ID the part gave for a WRONG event.
 */
uint16_t sense_link_event_id(const sense_link_t *s, sense_link_part_t part);
/**
 * @brief The address a scan found for the event's part, or 0 for none.
 *
 * @param s    The link state.
 * @param part The part.
 * @return The address a scan found for the event.
 */
uint8_t  sense_link_event_found(const sense_link_t *s, sense_link_part_t part);

/**
 * @brief The INA3221's shunt on the page, 0.1mOhm.
 *
 * @param s The link state.
 * @return The INA3221's shunt on the page, in 0.1mOhm.
 */
uint16_t sense_link_i3221_dmohm(const sense_link_t *s);

/**
 * @brief The INA3221's address on the page, the part SERVO_SENSE reads: 0x40 to
 *        0x43; 0 before the page is read.
 *
 * @param s The link state.
 * @return The INA3221's address, 0x40 to 0x43, or 0 before the page is read.
 */
uint8_t sense_link_i3221_addr(const sense_link_t *s);

/**
 * @brief The pins asked, -1 for unset: what SENSE_LINK_EV_BUS_REFUSED names.
 *
 * @param s The link state.
 * @return The SDA pin asked, or -1 for unset.
 */
int sense_link_sda(const sense_link_t *s);
/**
 * @brief The SCL pin asked, -1 for unset.
 *
 * @param s The link state.
 * @return The SCL pin asked, or -1 for unset.
 */
int sense_link_scl(const sense_link_t *s);

/**
 * @brief The sensor bus's pins the page holds by its last read or write, as the
 *        IOmcu reserves them (sense_page_pins()): SDA and SCL while ENABLE has
 *        a part on, -1 and -1 while it has none or the IOmcu has no SENSE page.
 *
 * False, both -1, while that is not known: no link, or no read yet.
 * @p sda and @p scl may be NULL.
 *
 * @param s   The link state.
 * @param sda Receives the SDA pin, or -1; may be NULL.
 * @param scl Receives the SCL pin, or -1; may be NULL.
 * @return true when the held pins are known.
 */
bool sense_link_held_pins(const sense_link_t *s, int *sda, int *scl);

#ifdef __cplusplus
}
#endif
