/*
 * The panel's half of the TONE (0x2D) link page, protocol 4.8: what is
 * written to it, in what order, what is read from it and how often, and
 * what a read means for the operator.
 *
 * The coprocessor reads one motor phase on a GPIO (the phase tap) and
 * turns its edges into beeps and their pitch; the panel sets the tap up
 * from SETUP INTERFACES and reads the beeps.  A coprocessor whose protocol
 * minor is below TONE_LINK_MINOR has no such page and nothing is sent to
 * it: not a write and not a read.  With the tap enabled on SETUP, the
 * operator is told so.
 *
 * The set-up is two frames of four registers: ENABLE to F_MAX_HZ (0 to 3)
 * and SPLIT_PCT to register 7 (4 to 7).  Each is written whole.  The page
 * judges every frame against the whole set-up it would leave, and a low
 * F_MIN_HZ needs a long GAP_MS, so when both frames change the one whose
 * intermediate set-up is valid goes first:
 *
 *   1. what the page holds is read first, TONE 0 to 6, once per link-up,
 *      and nothing is written that it already holds;
 *   2. the frames that differ, in the order above;
 *   3. a frame the page refuses is not written again until the set-up
 *      changes -- except the first, refused for a pin something else
 *      holds, which is offered again every TONE_LINK_RETRY_MS and said
 *      once.
 *
 * The tap is an input and drives nothing, so a write does not wait for an
 * idle bank.  It waits TONE_LINK_SETTLE_MS since the set-up's last edit,
 * because the coprocessor keeps each change in flash.
 *
 * While the page holds ENABLE set, registers 8 to 23 are read every
 * TONE_LINK_READ_MS.  The coprocessor keeps the last LINK_TN_RING beeps,
 * numbered 1 to 65535 and then 1 again.  The panel asks for each number in
 * turn: it writes EVT_SEL, then reads EVT_SEQ to EVT_FLAGS.  A read does
 * not consume a beep, so a reply that does not come loses nothing: the same
 * beep is asked for again.  A beep that has left the ring when the panel
 * asks, or that the panel is more than LINK_TN_RING behind BEEP_HEAD, is
 * counted in tone_link_missed().
 *
 * Pure C, no link of its own: the caller makes each exchange and reports
 * how it went.  Control task only.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The protocol minor that has the TONE page. */
#define TONE_LINK_MINOR 8u

/** A set-up rests this long after its last edit before it is written: a
 *  held + or - on SETUP steps a value about 30 times a second, and each
 *  write is a flash save at the coprocessor. */
#define TONE_LINK_SETTLE_MS 500u

/** Registers 8 to 23 are due this long after the last read while the tap
 *  is enabled: under the 50 ms poll, so every BENCH poll reads them. */
#define TONE_LINK_READ_MS 40u

/** A first frame refused on a pin is offered again this often: freeing
 *  the pin on OUTPUTS, SENSE or SUPPLY lets it through. */
#define TONE_LINK_RETRY_MS 5000u

/** A read older than this no longer says the tap runs. */
#define TONE_LINK_STALE_MS 500u

/** How long an event handed out by tone_link_event() has the alert band
 *  before the next one is handed out. */
#define TONE_LINK_EVENT_GAP_MS 5000u

/** The beeps the panel keeps for the screen. */
#define TONE_LINK_HISTORY 8u
/** The beeps the readout carries, newest first. */
#define TONE_LINK_SHOWN 4u

/** Registers 8 to 23, as read. */
#define TONE_LINK_STATUS_COUNT \
    ((unsigned)LINK_TN_COUNT - (unsigned)LINK_TN_FLAGS)
/** EVT_SEQ to EVT_FLAGS, as read. */
#define TONE_LINK_BEEP_COUNT \
    ((unsigned)LINK_TN_EVT_FLAGS - (unsigned)LINK_TN_EVT_SEQ + 1u)

/** What came of an exchange: ACK or DATA, a NACK's reason, or none. */
#define TONE_LINK_NO_ANSWER (-1)
#define TONE_LINK_ACK       0

/** The set-up as SETUP INTERFACES has it. */
typedef struct {
    bool     enable;          /**< the tap runs                         */
    uint8_t  pin;             /**< coprocessor GPIO                     */
    uint16_t f_min_hz;        /**< 50 to 2000                           */
    uint16_t f_max_hz;        /**< above f_min_hz, to 6900              */
    uint8_t  split_pct;       /**< 0 (off) to 50                        */
    uint8_t  gap_ms;          /**< 1 to 100                             */
    uint8_t  min_periods;     /**< 1 to 64                              */
} tone_setup_t;

/** Things the caller tells the operator about, each once. */
enum {
    /** The tap is enabled and the coprocessor has no TONE page. */
    TONE_LINK_EV_NO_PAGE      = 0x01,
    /** The page refused the first frame: the pin, or the tone range. */
    TONE_LINK_EV_PIN_REFUSED  = 0x02,
    /** The page refused the second frame: split, gap or periods. */
    TONE_LINK_EV_SETUP_REFUSED = 0x04,
    /** FLAGS says the pin is not free: a set-up kept in the
     *  coprocessor's flash met a binding at boot. */
    TONE_LINK_EV_PIN_BUSY     = 0x08,
    /** FLAGS says the capture overran: a beep was cut. */
    TONE_LINK_EV_OVERRUN      = 0x10,
};

/** The exchange tone_link_next() asks for. */
typedef enum {
    TONE_LINK_OP_NONE = 0,
    TONE_LINK_OP_READ_SETUP,    /**< TONE 0 to 6: what the page holds     */
    TONE_LINK_OP_FRAME0,        /**< TONE 0 to 3                          */
    TONE_LINK_OP_FRAME1,        /**< TONE 4 to 7                          */
    TONE_LINK_OP_STATUS,        /**< TONE 8 to 23                         */
    TONE_LINK_OP_SELECT,        /**< TONE 13, the beep wanted             */
    TONE_LINK_OP_BEEP,          /**< TONE 14 to 21, the beep selected     */
} tone_link_op_kind_t;

typedef struct {
    tone_link_op_kind_t kind;
    bool     write;           /**< a write of regs; otherwise a read   */
    uint8_t  page;            /**< link_page_id_t                      */
    uint8_t  off, n;
    uint16_t regs[4];         /**< a write's registers                 */
} tone_link_op_t;

/** One beep as the page reported it. */
typedef struct {
    uint16_t seq;             /**< its number, 1 to 65535               */
    uint32_t start_ms;        /**< first rise, ms since ENABLE          */
    uint16_t len_dms;         /**< first rise to last edge, 0.1 ms      */
    uint16_t freq_dhz;        /**< mean pitch, 0.1 Hz                   */
    uint16_t bursts;
    uint16_t carrier_hhz;     /**< 100 Hz steps; 0 unchopped            */
    uint8_t  flags;           /**< LINK_TN_EVT_*                        */
} tone_beep_t;

/** What the tap is doing, for the screen. */
typedef enum {
    TONE_STATE_OFF = 0,       /**< not enabled on SETUP                 */
    TONE_STATE_NO_PAGE,       /**< enabled; the coprocessor has no page */
    TONE_STATE_WAITING,       /**< enabled; no link, or nothing read yet */
    TONE_STATE_RUNNING,       /**< the capture runs                     */
    TONE_STATE_REFUSED,       /**< the pin is not free                  */
    TONE_STATE_STOPPED,       /**< enabled and not running              */
} tone_state_t;

/** The screen's copy: a plain value the control task fills and the
 *  renderer reads. */
typedef struct {
    tone_state_t state;
    bool     overrun;         /**< the capture overran since ENABLE     */
    bool     tone;            /**< the last window held a tone          */
    bool     beep;            /**< a beep is under way                  */
    uint16_t win_freq_dhz;    /**< the last window's pitch, 0.1 Hz; 0 none */
    uint16_t win_periods;
    uint16_t lost;            /**< beeps the coprocessor dropped        */
    uint16_t glitches;        /**< lows it ignored as too short         */
    uint32_t missed;          /**< beeps the panel did not read         */
    uint8_t  n;               /**< beeps held below                     */
    tone_beep_t beeps[TONE_LINK_SHOWN];   /**< newest first             */
} tone_readout_t;

typedef struct {
    bool     up;              /**< a coprocessor answers               */
    bool     page;            /**< the coprocessor has TONE (4.8)      */

    /* What is asked, as the page is to hold it. */
    bool     want_set;
    uint16_t want[LINK_TN_CONFIG_COUNT];
    uint32_t want_ms;         /**< when it last changed                */

    /* What the page holds, as read or written since the link came up. */
    bool     known;
    uint16_t held[LINK_TN_CONFIG_COUNT];
    bool     refused0, refused1;
    uint8_t  told;            /**< bit 0, 1: a frame's refusal said for this edit */
    uint32_t refused0_ms;

    /* The exchange in flight. */
    tone_link_op_kind_t pending;
    uint16_t out[4];

    /* The status read. */
    bool     asked_status;
    uint32_t status_asked_ms;
    bool     have_status;
    uint16_t status[TONE_LINK_STATUS_COUNT];
    uint32_t status_ms;
    uint16_t was_flags;       /**< FLAGS as last read under this set-up */

    /* The beeps. */
    bool     synced;          /**< last is meaningful                  */
    uint16_t last;            /**< the number of the last beep taken, 0 none */
    uint32_t missed;
    bool     sel_ok;          /**< EVT_SEL holds sel_num on the page   */
    uint16_t sel_num;
    tone_beep_t hist[TONE_LINK_HISTORY];
    uint8_t  hist_n;          /**< held, to TONE_LINK_HISTORY          */
    uint8_t  hist_at;         /**< the slot the next beep goes to      */
    uint32_t beeps_read;

    /* What has been said. */
    uint16_t events;
    bool     event_given;
    uint32_t event_ms;
} tone_link_t;

void tone_link_init(tone_link_t *t);

/** The link went: nothing on the page is known, and nothing is read or
 *  written until it comes up again.  The beep history stays on screen. */
void tone_link_lost(tone_link_t *t);

/**
 * A coprocessor answered with protocol minor @p minor.  Below
 * TONE_LINK_MINOR it has no page and nothing is sent to it, and the tap
 * enabled on SETUP raises TONE_LINK_EV_NO_PAGE.
 */
void tone_link_came_up(tone_link_t *t, uint16_t minor, uint32_t now_ms);

/** The set-up SETUP names now.  The tap enabled while a coprocessor
 *  without the page answers raises TONE_LINK_EV_NO_PAGE.  A set-up with a
 *  value outside the page's range -- a zeroed snapshot -- is ignored:
 *  nothing is asked, and nothing is written for it. */
void tone_link_want(tone_link_t *t, const tone_setup_t *w, uint32_t now_ms);

/**
 * The next exchange owed at @p now_ms, into @p op; false for none.  The
 * caller makes the exchange and reports with tone_link_done().
 */
bool tone_link_next(tone_link_t *t, uint32_t now_ms, tone_link_op_t *op);

/**
 * How the exchange tone_link_next() gave went: TONE_LINK_ACK with the
 * registers read (@p regs, op.n of them; NULL for a write), a NACK reason,
 * or TONE_LINK_NO_ANSWER.
 */
void tone_link_done(tone_link_t *t, int result, const uint16_t *regs,
                    uint32_t now_ms);

/**
 * One event for the alert band, which shows one line and replaces it with
 * the next: the most pressing waiting, cleared, and the rest kept.  0
 * while none waits or while the last one handed out is younger than
 * TONE_LINK_EVENT_GAP_MS at @p now_ms.
 */
uint16_t tone_link_event(tone_link_t *t, uint32_t now_ms);

/** Put back an event tone_link_event() handed out that never reached the
 *  band, so it is handed out again after the gap.  An event about a
 *  coprocessor is not put back once the link has gone. */
void tone_link_event_back(tone_link_t *t, uint16_t ev);

/** Whether nothing is owed: the page holds the set-up asked, less the
 *  frames it refused. */
bool tone_link_settled(const tone_link_t *t);

/** The beeps the panel has read, newest first, at most @p max; how many. */
unsigned tone_link_beeps(const tone_link_t *t, tone_beep_t *out,
                         unsigned max);

/** Beeps read since init, and beeps the panel did not read: the ring
 *  moved past them, or the page no longer held them. */
uint32_t tone_link_read_count(const tone_link_t *t);
uint32_t tone_link_missed(const tone_link_t *t);

/** FLAGS as last read, 0 before any read. */
uint16_t tone_link_flags(const tone_link_t *t);

/** The screen's copy of everything above, at @p now_ms. */
void tone_link_readout(const tone_link_t *t, uint32_t now_ms,
                       tone_readout_t *r);

/** The pin and the tone range asked, for the alerts' text. */
unsigned tone_link_pin(const tone_link_t *t);
unsigned tone_link_f_min(const tone_link_t *t);
unsigned tone_link_f_max(const tone_link_t *t);

#ifdef __cplusplus
}
#endif
