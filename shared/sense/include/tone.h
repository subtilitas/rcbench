/*
 * ESC (electronic speed controller) tones from the edges of one motor
 * phase.
 *
 * An ESC beeps by driving the motor windings at the tone frequency, often
 * chopped by its PWM (pulse-width modulation) carrier at 8 to 48 kHz.  One
 * phase reaches a coprocessor input through a series resistor and a zener
 * clamp, so the input is a logic level: high while the phase is driven
 * high, or floats above the input threshold, and low otherwise.  A PIO
 * (programmable input/output) state machine stamps each edge on a counter;
 * this module turns those stamps into tones.
 *
 * The signal's three time scales, and how each is found:
 *
 * - Carrier.  A rise that follows the rise before it by at most the
 *   period of carrier_min_hz is a carrier-rate rise.  The carrier period
 *   is the shortest rise interval inside a burst, not counting the
 *   interval from the burst's first rise, which a burst gated from a
 *   carrier running free can cut short.  Until a burst has had a third
 *   rise, it is the shortest carrier-rate low plus the longest
 *   carrier-rate high.  Both are kept from the run's first rise.  Once a
 *   carrier is seen, a rise starts a burst when the low before it is at
 *   least 1.25 carrier periods: a low inside a burst is shorter than one
 *   period, and the quarter is room for edges that move.  With no carrier
 *   seen, the drive is not chopped and every pulse is a burst.  A duty
 *   that changes inside a beep moves lows and highs and leaves the
 *   period.  A low shorter than glitch_ns is no low: the pulse goes on.
 *   With a capture hold-off, a rise starts a burst only after a low of at
 *   least twice the hold-off: a carrier low within the edges' jitter of
 *   the hold-off is reported at some periods and not at others, and one
 *   reported after a stretch of swallowed ones follows its rise by
 *   several carrier periods, too far apart to read as carrier.  The
 *   tone's own low between bursts has to be that long.
 *
 * - Tone.  The interval from one burst's first rise to the next is one
 *   tone period.  A period between half the period of f_max_hz and the
 *   period of f_min_hz is in range: a burst start moved by a free-running
 *   carrier shortens one period and lengthens the next, and the pair
 *   still counts.  A window holds a tone when at least
 *   window_min_periods in-range periods end in it and their mean lies
 *   between the periods of f_max_hz and f_min_hz; its frequency is that
 *   mean's.
 *
 * - Beep.  A beep is a run of bursts.  It ends where the line has had no
 *   edge for gap_us, or where a block of TONE_BLOCK periods gives a pitch
 *   more than split_pct away from the beep's mean: a pitch change with no
 *   silence between, which splits the beep at the first period nearer the
 *   new pitch than the old.  A block's pitch is the mean of its periods
 *   within TONE_OUTLIER_PCT of their median; it has moved when all but one
 *   of the block's periods lie more than half of split_pct from the beep's
 *   mean on its side.  A beep with fewer than TONE_BLOCK periods in its
 *   mean does not split: when a block moves by TONE_OUTLIER_PCT it starts
 *   its mean again and keeps its start, its first periods having come
 *   before the carrier was known.  A beep reports its first rise and its
 *   last edge, the mean of its in-range periods as its frequency, and its
 *   burst count.  A period more than TONE_OUTLIER_PCT from the beep's
 *   mean is a burst but not part of the mean: a burst missed doubles one
 *   period.  A run with fewer than min_periods in-range periods, or whose
 *   mean lies outside the tone range, is not a beep: a click at power-up,
 *   a stuck line, a drive faster than f_max_hz.
 *
 * Bounds, each in whole ticks:
 *
 * - Inclusive.  A rise interval of exactly the period of carrier_min_hz is
 *   carrier-rate.  A low of exactly 1.25 carrier periods starts a burst.
 *   A low of exactly glitch_ns is a low, and one of exactly twice the
 *   hold-off can start a burst.  A silence of exactly gap_us ends a beep.
 *   A tone of exactly f_min_hz or f_max_hz is in range, and a period of
 *   exactly half the period of f_max_hz.  A beep of exactly min_periods
 *   periods, and a window of exactly window_min_periods, hold a tone.  An
 *   edge at the same tick as the last is taken.
 * - Exclusive.  A pitch change is more than split_pct, so a block exactly
 *   split_pct away does not split; a period is an outlier when more than
 *   TONE_OUTLIER_PCT from the mean.  A window is [n, n + 1) window_us: a
 *   period that ends on the boundary belongs to the next window.
 * - Rounding.  The period of f_max_hz rounds down, the periods of f_min_hz
 *   and carrier_min_hz round up, so a period exactly at a bound, which the
 *   tick clock sees as the tick below or the tick above, is inside it.
 *   glitch_ns and gap_us round up: a low or a silence counts once it has
 *   lasted that long.
 *
 * What the edges cannot tell:
 *
 * - A drive that commutates, as a motor start does, at a rate in the tone
 *   range is a tone here.  The caller listens only while the ESC sounds
 *   its menu.
 * - A tone whose drive is off for less than 1.25 carrier periods between
 *   bursts merges them: the tone is heard as one long burst and reported
 *   as no tone.
 * - The tapped phase sees the bursts of the phase pair the ESC drives.  An
 *   ESC that reverses the pair every half period shows two bursts a period
 *   on the floating phase, and the frequency reads one octave high.
 *   Pitches compared on one bench are compared on one phase, so a
 *   recorded reference carries the same factor.
 * - A carrier that runs free of the tone moves each burst's first rise by
 *   up to one carrier period, so a single period is known to one carrier
 *   period; a mean over n periods to one carrier period in n.
 *
 * Time is a count of ticks of tick_hz, 64 bits wide, never wrapping on a
 * bench; the caller extends the PIO's counter.  Edges arrive in time
 * order.  tone_advance() tells the detector time has passed without an
 * edge, so a silence ends a beep and a window closes without one.
 *
 * Pure C, no SDK (software development kit), no allocation; no division
 * and no floating point per edge.  Host-tested in test_tone.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Periods compared at once for a pitch change. */
#define TONE_BLOCK 4u
/** A period this far from its beep's mean, in percent, is left out of the
 *  mean. */
#define TONE_OUTLIER_PCT 45u
/** Beeps held until the caller takes them. */
#define TONE_BEEP_QUEUE 16u

typedef struct {
    uint32_t tick_hz;        /**< the edge stamps' clock                  */
    uint32_t window_us;      /**< a window's length, 1000 to 100000 us    */
    uint32_t f_min_hz;       /**< lowest tone, at least 50 Hz             */
    uint32_t f_max_hz;       /**< highest tone                            */
    /** Rise intervals up to this carrier's period are chopping inside a
     *  burst.  Above f_max_hz. */
    uint32_t carrier_min_hz;
    /** A low shorter than this, ns, is no low; 1 to 10000. */
    uint32_t glitch_ns;
    /** The capture's hold-off (tone_holdoff_t), ns, 0 to 100000; 0 none.
     *  A rise starts a burst only after a low of at least twice it. */
    uint32_t hold_ns;
    /** No edge for this long ends a beep, us; at least the period of
     *  f_min_hz, so a tone's own off time never does. */
    uint32_t gap_us;
    /** TONE_BLOCK periods this far from the beep's mean, in percent, start
     *  a new beep; 0 splits only on silence. */
    uint32_t split_pct;
    uint32_t min_periods;    /**< in-range periods a beep needs, >= 1     */
    uint32_t window_min_periods; /**< in-range periods a window needs to
                                      hold a tone, >= 1                   */
} tone_cfg_t;

/** One edge: the tick it happened at and the level after it. */
typedef struct {
    uint64_t t;
    bool     level;
} tone_edge_t;

/** One window of window_us. */
typedef struct {
    uint64_t index;          /**< start tick / window ticks               */
    bool     present;        /**< at least window_min_periods in range    */
    float    freq_hz;        /**< their mean; 0 with no tone              */
    uint32_t periods;        /**< in-range periods ending in the window   */
    uint32_t bursts;         /**< bursts starting in the window           */
} tone_window_t;

typedef enum {
    /** Began at a pitch change, with no silence before it. */
    TONE_BEEP_AFTER_CHANGE  = 1u << 0,
    /** Ended at a pitch change, with no silence after it. */
    TONE_BEEP_BEFORE_CHANGE = 1u << 1,
} tone_beep_flag_t;

typedef struct {
    uint64_t start;          /**< its first rise, ticks                   */
    uint64_t end;            /**< its last edge, ticks                    */
    float    freq_hz;        /**< mean of the in-range periods            */
    /** The carrier its run was chopped at; 0 when no rise came at the
     *  carrier rate. */
    float    carrier_hz;
    uint32_t bursts;
    uint32_t periods;        /**< in the mean                             */
    uint8_t  flags;          /**< tone_beep_flag_t                        */
} tone_beep_t;

typedef struct {
    uint32_t edges;          /**< taken                                   */
    uint32_t glitches;       /**< lows shorter than glitch_ns, ignored    */
    uint32_t out_of_order;   /**< earlier than the last time seen; ignored */
    uint32_t beeps;          /**< reported                                */
    /** Runs that made no beep: too few periods, or a mean outside the
     *  tone range. */
    uint32_t rejected;
    uint32_t lost;           /**< beeps dropped with the queue full       */
} tone_stats_t;

/** A period waiting for the pitch check. */
typedef struct {
    uint64_t p;              /**< ticks                                   */
    uint64_t s_open;         /**< the burst start it begins at            */
    uint64_t e_before;       /**< the last edge before s_open             */
    bool     good;           /**< in range                                */
} tone_pend_t;

typedef struct {
    tone_cfg_t c;
    uint64_t win_ticks, per_min, per_max, car_max, glitch, gap;
    uint64_t start_low;      /**< twice the hold-off, ticks               */
    /** The shortest time between two burst starts that makes a counted
     *  period, ticks: the most of half the shortest period in range, the
     *  glitch and start_low. */
    uint64_t spacing;

    bool     seen;           /**< any time given yet                      */
    uint64_t now;            /**< the latest time given                   */
    bool     have_edge;
    uint64_t last_edge;
    bool     high;           /**< the line's level after the last edge    */
    uint64_t last_rise, last_fall;

    /* The run: bursts with no silence of gap between them. */
    bool     in_run;
    uint64_t car_low;        /**< shortest carrier-rate low; 0 none       */
    uint64_t car_high;       /**< longest carrier-rate high               */
    uint64_t car_rise;       /**< shortest rise interval inside a burst,
                                  not its first; 0 none                   */
    bool     rise_started;   /**< the last rise started a burst           */
    uint64_t burst_start, burst_e_before;

    /* The beep under way. */
    uint64_t b_start;
    uint64_t b_sum;
    uint32_t b_n, b_bursts;
    uint8_t  b_flags;
    tone_pend_t pend[TONE_BLOCK];
    uint32_t n_pend;

    /* The window under way, and the last one finished. */
    uint64_t w_index, w_end, w_sum;
    uint32_t w_n, w_bursts;
    bool     have_window;
    tone_window_t window;

    tone_beep_t q[TONE_BEEP_QUEUE];
    uint32_t q_head, q_len;
    tone_stats_t st;
} tone_t;

/** Defaults for a clock of @p tick_hz: 8000 us windows, 400 to 6500 Hz,
 *  carrier from 7 kHz, 500 ns glitch, 8000 ns hold-off, 3000 us gap, 8 %
 *  split, 3 periods a beep, 2 a window. */
void tone_cfg_defaults(tone_cfg_t *c, uint32_t tick_hz);

/** Start from nothing.  False, leaving @p d unusable, when @p c contradicts
 *  itself or names a range the tick clock cannot resolve: f_min_hz >=
 *  f_max_hz, carrier_min_hz <= f_max_hz, f_max_hz or carrier_min_hz above
 *  tick_hz (a period under one tick), hold_ns above 100000, glitch_ns
 *  outside 1 to 10000 or under one tick, gap_us under the period of
 *  f_min_hz, a window outside 1000 to 100000 us, f_min_hz under 50 Hz, a
 *  minimum of 0.  Also refused, as settings under which no signal makes a
 *  tone, in whole ticks: the period of f_min_hz, or the gap, at most the
 *  glitch or at most twice the hold-off (no low could part two bursts
 *  without being a glitch, being too short to start one, or ending the
 *  run); window_min_periods - 1 of the shortest burst-start spacings
 *  (tone_t's spacing) filling a window. */
bool tone_init(tone_t *d, const tone_cfg_t *c);

/** One edge at tick @p t, @p level after it. */
void tone_edge(tone_t *d, uint64_t t, bool level);

/** @p n edges in time order. */
void tone_feed(tone_t *d, const tone_edge_t *e, size_t n);

/** No edge up to tick @p now: a silence ends a beep, a window closes. */
void tone_advance(tone_t *d, uint64_t now);

/** End the beep under way at its last edge, as a silence would, and
 *  forget the line's level, so the first rise after it starts a run: the
 *  caller's edges have a hole (a buffer overrun) or the recording ends. */
void tone_flush(tone_t *d);

/** The last window finished; false before the first. */
bool tone_window(const tone_t *d, tone_window_t *w);

/** Take the oldest beep reported; false with none waiting. */
bool tone_next_beep(tone_t *d, tone_beep_t *b);

/** A run of bursts is under way. */
bool tone_busy(const tone_t *d);

const tone_stats_t *tone_stats(const tone_t *d);

/** @p ticks of the detector's clock in microseconds, rounded down. */
uint64_t tone_ticks_us(const tone_t *d, uint64_t ticks);

/* ------------------------------------------------------------ hold-off */

/*
 * The capture's hold-off: the rule the PIO program applies before an edge
 * reaches the detector, written here so it is tested on the host and the
 * PIO is held to it.
 *
 * A fall is reported only once the line has stayed low for at least the
 * hold-off; a low that ends sooner is not reported at all, and the pulse
 * around it goes on.  A reported fall carries the time the line fell, not
 * the time the low qualified.  A rise is reported at once.
 *
 * Why: a carrier's lows inside a burst are (1 - duty) / carrier long.  At
 * the default 8 us, every low of a carrier above 125 kHz, and every low
 * at 50 % duty above 62.5 kHz, is shorter, so such a burst reaches the
 * detector as one pulse: two edges per burst instead of two per carrier
 * period, 288,000 edges/s at 144 kHz.  Lows between bursts, at least a
 * quarter of the longest tone period in range, are far longer.
 *
 * A fall is reported up to the hold-off late, so the detector is advanced
 * only to tone_holdoff_horizon(), before which every edge is out: the
 * PIO's own drain knows nothing of a fall still being held, and so uses
 * the same now less the hold-off.
 */
typedef struct {
    uint64_t hold;           /**< ticks; 0 passes every edge              */
    bool     line;           /**< the line's level after the last edge    */
    bool     pending;        /**< a fall is being held                    */
    uint64_t fall;           /**< when it fell                            */
} tone_holdoff_t;

/** @p hold_ns of a clock of @p tick_hz, rounded up to whole ticks; the
 *  line starts low. */
void tone_holdoff_init(tone_holdoff_t *h, uint32_t tick_hz, uint32_t hold_ns);

/** One edge at @p t; the edges reported, 0 to 2, into @p out in time
 *  order: a held fall whose low lasted the hold-off, then the rise. */
size_t tone_holdoff_edge(tone_holdoff_t *h, uint64_t t, bool level,
                         tone_edge_t out[2]);

/** No edge up to @p now: a held fall whose low has lasted the hold-off by
 *  then, into @p out; 0 or 1. */
size_t tone_holdoff_advance(tone_holdoff_t *h, uint64_t now,
                            tone_edge_t out[1]);

/** @p now less the hold-off, at least 0: every edge before it is out. */
uint64_t tone_holdoff_horizon(const tone_holdoff_t *h, uint64_t now);

#ifdef __cplusplus
}
#endif
