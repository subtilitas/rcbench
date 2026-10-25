/**
 * @file
 * @brief The panel's half of the TONE (0x2D) link page, protocol 4.8: what is
 *        written to it, in what order, what is read from it and how often, and
 *        what a read means for the operator.
 *
 * The IOmcu reads one motor phase on a GPIO (the phase tap) and
 * turns its edges into beeps and their pitch; the panel sets the tap up
 * from SETUP INTERFACES and reads the beeps.  An IOmcu whose protocol
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
 *      changes or the link comes up again -- except the first, refused
 *      for a pin something else holds, which is offered again every
 *      TONE_LINK_RETRY_MS and said once.  A page from 4.17
 *      (LINK_MINOR_TONE_BUSY) says that refusal with LINK_NACK_BUSY, and
 *      only that one is offered again.  An older page says BAD_VALUE for
 *      a busy pin and for bad values alike: its refusal is offered again
 *      unless the frame's tones fail on their face -- the high tone not
 *      above the low one, or the gap held too short for the low tone.
 *
 * Switching the tap off writes the first frame with the values the page
 * holds and ENABLE clear, whatever range is asked; the range follows in a
 * frame of its own.  The readout is not OFF while the page holds ENABLE.
 * A saved tap the page refused for a busy pin at boot is offered again
 * every TONE_LINK_RETRY_MS while FLAGS says so, and not said again.
 *
 * A write that enables the tap, or moves it to another pin, starts the
 * capture again with the ring empty and the numbering going on: the panel
 * takes its place at BEEP_HEAD at the next status.  A change of the range
 * or of the other values leaves the ring, the numbering and the panel's
 * place.
 *
 * The tap is an input and drives nothing, so a write does not wait for an
 * idle bank.  It waits TONE_LINK_SETTLE_MS since the set-up's last edit,
 * because the IOmcu keeps each change in flash.
 *
 * While the page holds ENABLE set, registers 8 to 23 are read every
 * TONE_LINK_READ_MS.  The IOmcu keeps the last LINK_TN_RING beeps,
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

/** @brief The protocol minor that has the TONE page. */
#define TONE_LINK_MINOR 8u

/**
 * @brief A set-up rests this long after its last edit before it is written: a
 *        held + or - on SETUP steps a value about 30 times a second, and each
 *        write is a flash save at the IOmcu.
 */
#define TONE_LINK_SETTLE_MS 500u

/**
 * @brief Registers 8 to 23 are due this long after the last read while the tap
 *        is enabled: under the 50ms poll, so every BENCH poll reads them.
 */
#define TONE_LINK_READ_MS 40u

/**
 * @brief A first frame refused on a busy pin is offered again this often:
 *        freeing the pin on OUTPUTS, SENSE, SUPPLY or PROG lets it through.
 */
#define TONE_LINK_RETRY_MS 5000u

/** @brief A read older than this no longer says the tap runs. */
#define TONE_LINK_STALE_MS 500u

/**
 * @brief A WINDOW that has not moved for this long, while FLAGS says the tap
 *        runs, no longer says it runs: the detector closes a window every 8ms,
 *        edges or none, so a count that stands is a capture that stopped while
 *        the page still answers.
 *
 * SUPPLY's and SENSE's counts are judged the
 *  same way, at 1500ms for SUPPLY.
 */
#define TONE_LINK_WINDOW_STALE_MS 1500u

/**
 * @brief How long an event handed out by tone_link_event() has the alert band
 *        before the next one is handed out.
 */
#define TONE_LINK_EVENT_GAP_MS 5000u

/** @brief The beeps the panel keeps for the screen. */
#define TONE_LINK_HISTORY 8u
/** @brief The beeps the readout carries, newest first. */
#define TONE_LINK_SHOWN 4u

/** @brief Registers 8 to 23, as read. */
#define TONE_LINK_STATUS_COUNT \
    ((unsigned)LINK_TN_COUNT - (unsigned)LINK_TN_FLAGS)
/** @brief EVT_SEQ to EVT_FLAGS, as read. */
#define TONE_LINK_BEEP_COUNT \
    ((unsigned)LINK_TN_EVT_FLAGS - (unsigned)LINK_TN_EVT_SEQ + 1u)

/** @brief What came of an exchange: ACK or DATA, a NACK's reason, or none. */
#define TONE_LINK_NO_ANSWER (-1)
#define TONE_LINK_ACK       0

/** @brief The set-up as SETUP INTERFACES has it. */
typedef struct {
    bool     enable;          /**< the tap runs                         */
    uint8_t  pin;             /**< IOmcu GPIO                     */
    uint16_t f_min_hz;        /**< 50 to 2000                           */
    uint16_t f_max_hz;        /**< above f_min_hz, to 6900              */
    uint8_t  split_pct;       /**< 0 (off) to 50                        */
    uint8_t  gap_ms;          /**< 1 to 100                             */
    uint8_t  min_periods;     /**< 1 to 64                              */
} tone_setup_t;

/** @brief Things the caller tells the operator about, each once. */
enum {
    /** @brief The tap is enabled and the IOmcu has no TONE page. */
    TONE_LINK_EV_NO_PAGE      = 0x01,
    /** @brief The page refused the first frame: the pin, or the tone range. */
    TONE_LINK_EV_PIN_REFUSED  = 0x02,
    /** @brief The page refused the second frame: split, gap or periods. */
    TONE_LINK_EV_SETUP_REFUSED = 0x04,
    /**
     * @brief FLAGS says the pin is not free: a set-up kept in the  IOmcu's
     *        flash met a binding at boot.
     */
    TONE_LINK_EV_PIN_BUSY     = 0x08,
    /** @brief FLAGS says the capture overran: a beep was cut. */
    TONE_LINK_EV_OVERRUN      = 0x10,
};

/**
 * @brief The events about the set-up asked, which tone_link_event_stands()
 *        judges.
 */
#define TONE_LINK_EV_SETUP ((uint16_t)(TONE_LINK_EV_PIN_REFUSED           \
                                       | TONE_LINK_EV_SETUP_REFUSED       \
                                       | TONE_LINK_EV_PIN_BUSY))

/** @brief The exchange tone_link_next() asks for. */
typedef enum {
    TONE_LINK_OP_NONE = 0,
    TONE_LINK_OP_READ_SETUP,    /**< TONE 0 to 6: what the page holds     */
    TONE_LINK_OP_FRAME0,        /**< TONE 0 to 3                          */
    TONE_LINK_OP_FRAME1,        /**< TONE 4 to 7                          */
    TONE_LINK_OP_STATUS,        /**< TONE 8 to 23                         */
    TONE_LINK_OP_SELECT,        /**< TONE 13, the beep wanted             */
    TONE_LINK_OP_BEEP,          /**< TONE 14 to 21, the beep selected     */
} tone_link_op_kind_t;

/**
 * @brief One exchange the TONE link asks the caller to make.
 */
typedef struct {
    tone_link_op_kind_t kind;
    bool     write;           /**< a write of regs; otherwise a read   */
    uint8_t  page;            /**< link_page_id_t                      */
    uint8_t  off;             /**< first register of the page          */
    uint8_t  n;               /**< registers                           */
    uint16_t regs[4];         /**< a write's registers                 */
} tone_link_op_t;

/** @brief One beep as the page reported it. */
typedef struct {
    uint16_t seq;             /**< its number, 1 to 65535               */
    uint32_t start_ms;        /**< first rise, ms since ENABLE          */
    uint16_t len_dms;         /**< first rise to last edge, 0.1ms       */
    uint16_t freq_dhz;        /**< mean pitch, 0.1Hz                    */
    uint16_t bursts;          /**< its bursts, to 65535                 */
    uint16_t carrier_hhz;     /**< 100Hz steps; 0 unchopped             */
    uint8_t  flags;           /**< LINK_TN_EVT_*                        */
} tone_beep_t;

/** @brief What the tap is doing, for the screen. */
typedef enum {
    TONE_STATE_OFF = 0,       /**< not enabled on SETUP                 */
    TONE_STATE_NO_PAGE,       /**< enabled; the IOmcu has no page */
    TONE_STATE_WAITING,       /**< enabled; no link, or nothing read yet */
    TONE_STATE_RUNNING,       /**< the capture runs                     */
    TONE_STATE_REFUSED,       /**< the pin is not free                  */
    TONE_STATE_STOPPED,       /**< enabled and not running              */
} tone_state_t;

/**
 * @brief The screen's copy: a plain value the control task fills and the
 *        renderer reads.
 */
typedef struct {
    tone_state_t state;
    /**
     * @brief The state and the seven values below come from a status read
     *        younger than TONE_LINK_STALE_MS; false, and the seven 0, while
     *        none  speaks for the page: no link, no page, no read yet, a read
     *        that  old, a read whose FLAGS say the tap runs while its WINDOW
     *        has not  moved for TONE_LINK_WINDOW_STALE_MS, or a page that holds
     *        the tap  off.
     *
     * The state is then WAITING.
     */
    bool     read;
    bool     overrun;         /**< the capture overran since ENABLE     */
    bool     tone;            /**< the last window held a tone          */
    bool     beep;            /**< a beep is under way                  */
    uint16_t win_freq_dhz;    /**< the last window's pitch, 0.1Hz; 0 none */
    uint16_t win_periods;     /**< the tone periods in the last window  */
    uint16_t lost;            /**< beeps the IOmcu dropped        */
    uint16_t glitches;        /**< lows it ignored as too short         */
    uint32_t missed;          /**< beeps the panel did not read         */
    uint8_t  n;               /**< beeps held below                     */
    tone_beep_t beeps[TONE_LINK_SHOWN];   /**< newest first             */
} tone_readout_t;

/**
 * @brief The panel's state of the TONE link page.
 */
typedef struct {
    bool     up;              /**< an IOmcu answers               */
    bool     page;            /**< the IOmcu has TONE (4.8)      */

    /* What is asked, as the page is to hold it. */
    bool     want_set;
    uint16_t want[LINK_TN_CONFIG_COUNT];
    uint32_t want_ms;         /**< when it last changed                */

    /* What the page holds, as read or written since the link came up. */
    bool     known;
    uint16_t held[LINK_TN_CONFIG_COUNT];
    bool     refused0, refused1;
    bool     retry0;          /**< refused0 lifts after TONE_LINK_RETRY_MS */
    bool     busy_nack;       /**< the page says BUSY for a busy pin (4.17) */
    uint8_t  told;            /**< bit 0, 1: a frame's refusal said for this edit */
    uint32_t refused0_ms;
    uint32_t retry_ms;        /**< the first frame last offered for a busy pin */

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
    bool     win_seen;        /**< win_last holds a read's WINDOW       */
    uint16_t win_last;        /**< WINDOW as last read                  */
    uint32_t win_moved_ms;    /**< the read that first showed it, or one
                                   of a page not running               */

    /* The beeps. */
    bool     synced;          /**< last is meaningful                  */
    bool     ring_empty;      /**< the capture began again, place unknown */
    uint8_t  quiet;           /**< beeps still to ask that may predate it */
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

/**
 * @brief Sets up the tone link state.
 *
 * @param t The link state to set up.
 */
void tone_link_init(tone_link_t *t);

/**
 * @brief The link went: nothing on the page is known, and nothing is read or
 *        written until it comes up again.
 *
 * The beep history stays on screen.
 *
 * @param t The link state.
 */
void tone_link_lost(tone_link_t *t);

/**
 * @brief An IOmcu answered with protocol minor @p minor.
 *
 * Below TONE_LINK_MINOR it has no page and nothing is sent to it, and the tap
 * enabled on SETUP raises TONE_LINK_EV_NO_PAGE.
 *
 * @param t      The link state.
 * @param minor  The IOmcu's protocol minor version.
 * @param now_ms The time, in ms.
 */
void tone_link_came_up(tone_link_t *t, uint16_t minor, uint32_t now_ms);

/**
 * @brief The set-up SETUP names now.
 *
 * The tap enabled while an IOmcu
 *  without the page answers raises TONE_LINK_EV_NO_PAGE.  A set-up with a
 *  value outside the page's range -- a zeroed snapshot -- is ignored:
 *  nothing is asked, and nothing is written for it.
 *
 * @param t      The link state.
 * @param w      The set-up SETUP names.
 * @param now_ms The time, in ms.
 */
void tone_link_want(tone_link_t *t, const tone_setup_t *w, uint32_t now_ms);

/**
 * @brief The next exchange owed at @p now_ms, into @p op; false for none.
 *
 * The caller makes the exchange and reports with tone_link_done().
 *
 * @param t      The link state.
 * @param now_ms The time, in ms.
 * @param op     Receives the exchange owed.
 * @return true when an exchange is owed.
 */
bool tone_link_next(tone_link_t *t, uint32_t now_ms, tone_link_op_t *op);

/**
 * @brief How the exchange tone_link_next() gave went: TONE_LINK_ACK with the
 *        registers read (@p regs, op.n of them; NULL for a write), a NACK
 *        reason, or TONE_LINK_NO_ANSWER.
 *
 * @param t      The link state.
 * @param result The result: an acknowledgement, a NACK's reason or
 *               TONE_LINK_NO_ANSWER.
 * @param regs   The registers read, or NULL for a write.
 * @param now_ms The time, in ms.
 */
void tone_link_done(tone_link_t *t, int result, const uint16_t *regs,
                    uint32_t now_ms);

/**
 * @brief One event for the alert band, which shows one line and replaces it
 *        with the next: the most pressing waiting, cleared, and the rest kept.
 *
 * 0 while none waits or while the last one handed out is younger than
 * TONE_LINK_EVENT_GAP_MS at @p now_ms.
 *
 * @param t      The link state.
 * @param now_ms The time, in ms.
 * @return The event bit for the alert band, or 0 for none.
 */
uint16_t tone_link_event(tone_link_t *t, uint32_t now_ms);

/**
 * @brief Whether what @p ev, one TONE_LINK_EV_* bit, says still holds now.
 *
 * A refused frame -- TONE_LINK_EV_PIN_REFUSED for the first,
 * TONE_LINK_EV_SETUP_REFUSED for the second -- until the page is known to hold
 * that frame as asked: not refused, and written and taken.  A value stepped
 * back on SETUP ends it once the IOmcu takes the frame, after
 * TONE_LINK_SETTLE_MS and a poll.  TONE_LINK_EV_PIN_BUSY until the tap is off
 * as asked and held, or a status read under the first frame as asked shows its
 * pin taken (FLAGS bit 1 clear).  With no IOmcu answering they stand.  True for
 * every other event.
 *
 * The caller takes back an alert it showed for such an event once this
 * is false; tone_link_event() drops one waiting that is over.
 *
 * @param t  The link state.
 * @param ev One TONE_LINK_EV_* bit.
 * @return true while the cause of the event holds.
 */
bool tone_link_event_stands(const tone_link_t *t, uint16_t ev);

/**
 * @brief Put back an event tone_link_event() handed out that never reached the
 *        band, so it is handed out again after the gap.
 *
 * An event about an
 *  IOmcu is not put back once the link has gone.
 *
 * @param t  The link state.
 * @param ev The event to put back.
 */
void tone_link_event_back(tone_link_t *t, uint16_t ev);

/**
 * @brief Whether nothing is owed: the page holds the set-up asked, less the
 *        frames it refused.
 *
 * @param t The link state.
 * @return true when nothing is owed.
 */
bool tone_link_settled(const tone_link_t *t);

/**
 * @brief The rows of SETUP INTERFACES, for tone_link_unheld(): the first
 *        frame's  -- Phase tap, Tap pin and the two tones -- and the second's
 *        -- pitch  split, gap and periods.
 */
enum {
    TONE_LINK_ROWS_TAP  = 0x01,
    TONE_LINK_ROWS_BEEP = 0x02,
};

/**
 * @brief The frames whose values as asked are not the ones the page holds,
 *        TONE_LINK_ROWS_*: not written yet, or refused.
 *
 * The comparison is want against held, the two this module writes from.  0
 * while no IOmcu answers and before the page has been read.  An IOmcu without
 * the page holds no tap, and a tap that is on is named.
 *
 * @param t The link state.
 * @return The frames not held as asked, as a TONE_LINK_ROWS_* value.
 */
uint8_t tone_link_unheld(const tone_link_t *t);

/**
 * @brief The beeps the panel has read, newest first, at most @p max; how many.
 *
 * @param t   The link state.
 * @param out Receives the beeps, newest first.
 * @param max The number of entries out holds.
 * @return The number of beeps written to out.
 */
unsigned tone_link_beeps(const tone_link_t *t, tone_beep_t *out,
                         unsigned max);

/**
 * @brief Beeps read since init, and beeps the panel did not read: the ring
 *        moved past them, or the page no longer held them.
 *
 * @param t The link state.
 * @return The beeps read since init.
 */
uint32_t tone_link_read_count(const tone_link_t *t);
/**
 * @brief Counts the beeps the panel did not read.
 *
 * @param t The link state.
 * @return The beeps missed since init.
 */
uint32_t tone_link_missed(const tone_link_t *t);

/**
 * @brief FLAGS as last read, 0 before any read.
 *
 * @param t The link state.
 * @return FLAGS as last read.
 */
uint16_t tone_link_flags(const tone_link_t *t);

/**
 * @brief The screen's copy of everything above, at @p now_ms.
 *
 * The status's
 *  values are filled only while tone_readout_t.read holds; the beeps and
 *  the missed count are the panel's own and stay.
 *
 * @param t      The link state.
 * @param now_ms The time, in ms.
 * @param r      Receives the copy.
 */
void tone_link_readout(const tone_link_t *t, uint32_t now_ms,
                       tone_readout_t *r);

/**
 * @brief The pin and the tone range asked, for the alerts' text.
 *
 * @param t The link state.
 * @return The tap's pin asked.
 */
unsigned tone_link_pin(const tone_link_t *t);
/**
 * @brief The lower end of the tone range asked, for the alerts' text.
 *
 * @param t The link state.
 * @return The lower end of the tone range asked.
 */
unsigned tone_link_f_min(const tone_link_t *t);
/**
 * @brief The upper end of the tone range asked, for the alerts' text.
 *
 * @param t The link state.
 * @return The upper end of the tone range asked.
 */
unsigned tone_link_f_max(const tone_link_t *t);

/**
 * @brief The tap's pin the page holds by its last read or write, as the IOmcu
 *        reserves it (tone_page_pins()): the pin while ENABLE has the tap on
 *        and the last status read does not say the pin refused, -1 otherwise
 *        and while the IOmcu has no TONE page.
 *
 * False, -1, while that is not known: no link, or no read yet.  @p pin may be
 * NULL.
 *
 * @param t   The link state.
 * @param pin Receives the pin, or -1 while not known; may be NULL.
 * @return true when the pin is known.
 */
bool tone_link_held_pin(const tone_link_t *t, int *pin);

#ifdef __cplusplus
}
#endif
