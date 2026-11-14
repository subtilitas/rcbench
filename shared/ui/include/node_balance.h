/**
 * @file
 * @brief The balance node's readings on the panel: a report turned into the
 *        words and numbers the BALANCE screen shows, the captures of the
 *        MOTOR & ESC BALANCE tab, the correction shared/vibe solves from
 *        them and the CSV (comma-separated values) row of each solution.
 *
 * A report is fresh for BAL_STALE_MS (1000ms, 4 report periods) after it
 * came; an older one, or none, shows "--" and never its old value.  The
 * quality word follows the report's RCNODE_BF_* flags in a fixed order:
 * the sensor's DC level, a clipped sample, an rpm out of range, a lost
 * index, no revolution counted, too few revolutions for the quality, no
 * index (magnitude only), then the quality percentage: GOOD from 90%,
 * FAIR from 70%, else POOR.
 *
 * A capture averages the next NB_CAP_REPORTS fresh reports after TAKE,
 * 2s at 250ms, as a vector (the two-run method needs the phase) and as a
 * magnitude (the four-run method needs only it).  It fails on a gap of
 * BAL_STALE_MS, a report without a counted revolution, a report without
 * the index in the two-run method, or an rpm more than NB_RPM_DRIFT_PCT
 * from the capture's first report.  The two-run method takes the baseline
 * and the trial mass in line with blade 1 (0°); the four-run method the
 * baseline and the trial mass at 0°, 120° and 240°.  Angles count against
 * the direction of rotation from the index mark, as shared/vibe's do.
 *
 * The functions on nb_session_t and nb_reading_t are pure; the node_balance_
 * functions keep the panel's one instance for the UI task.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bal_ctl.h"
#include "rcnode.h"
#include "vibe_solve.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Captures. @{ */
#define NB_SLOTS         4u  /**< the four-run method's runs */
#define NB_CAP_REPORTS   8u  /**< reports a capture averages: 2s at 250ms */
#define NB_RPM_DRIFT_PCT 5u  /**< the most a capture's rpm may move, % */
#define NB_CSV_MAX     160u  /**< a CSV row with its newline and NUL */
/** @} */

/** @brief What the readings show. */
typedef enum {
    NB_LIVE_SIMULATED = 0, /**< no node linked: the screen's model */
    NB_LIVE_STALE,         /**< linked, no report in BAL_STALE_MS: "--" */
    NB_LIVE_FRESH,         /**< a report within BAL_STALE_MS */
} nb_live_t;

/** @brief The quality word, in the order it is chosen. */
typedef enum {
    NB_Q_NONE = 0,    /**< no fresh report: "--" */
    NB_Q_SENSOR,      /**< RCNODE_BF_DC_OK clear: the sensor's DC level is out */
    NB_Q_CLIPPED,     /**< RCNODE_BF_CLIPPED */
    NB_Q_RPM_RANGE,   /**< RCNODE_BF_RPM_RANGE */
    NB_Q_INDEX_LOST,  /**< RCNODE_BF_INDEX_LOST */
    NB_Q_NO_INDEX,    /**< no revolution counted in no-index mode */
    NB_Q_NO_REV,      /**< RCNODE_BF_VALID clear */
    NB_Q_SETTLING,    /**< RCNODE_BF_QUALITY clear */
    NB_Q_NO_PHASE,    /**< RCNODE_BF_NO_INDEX: magnitudes, no angle */
    NB_Q_GOOD,        /**< quality 90% or more */
    NB_Q_FAIR,        /**< 70% to 89% */
    NB_Q_POOR,        /**< under 70% */
    NB_Q_COUNT        /**< the number of words */
} nb_quality_t;

/** @brief One report as the screen shows it. */
typedef struct {
    uint8_t  live;     /**< nb_live_t */
    uint8_t  word;     /**< nb_quality_t */
    bool     counts;   /**< magnitudes in ADC counts, not g (RCNODE_BF_COUNTS) */
    bool     phase;    /**< deg holds: a counted revolution against the index */
    float    mag;      /**< the 1x magnitude, g, or counts with counts */
    float    deg;      /**< the 1x phase, 0 to below 360° */
    uint16_t rpm;      /**< 0 when no revolution was counted */
    uint8_t  quality;  /**< %, 0 to 100 */
} nb_reading_t;

/** @brief The balance methods. */
typedef enum {
    NB_TWO_RUNS = 0, /**< baseline, trial at 0°: needs the index */
    NB_FOUR_RUNS,    /**< baseline, trial at 0°, 120°, 240°: magnitudes */
    NB_METHOD_COUNT  /**< the number of methods */
} nb_method_t;

/** @brief A capture's state. */
typedef enum {
    NB_CAP_EMPTY = 0, /**< not taken */
    NB_CAP_TAKING,    /**< averaging */
    NB_CAP_DONE,      /**< averaged */
    NB_CAP_FAILED,    /**< ended early: fail says why */
} nb_cap_state_t;

/** @brief Why a capture failed. */
typedef enum {
    NB_FAIL_NONE = 0, /**< it did not */
    NB_FAIL_STALE,    /**< no report for BAL_STALE_MS, or the node went */
    NB_FAIL_NO_REV,   /**< a report without a counted revolution */
    NB_FAIL_NO_PHASE, /**< two runs: a report without the index */
    NB_FAIL_RPM,      /**< the rpm moved more than NB_RPM_DRIFT_PCT */
    NB_FAIL_COUNT     /**< the number of reasons */
} nb_fail_t;

/** @brief What TAKE got. */
typedef enum {
    NB_TAKE_STARTED = 0, /**< the capture runs */
    NB_TAKE_NO_DATA,     /**< refused: no fresh report */
    NB_TAKE_BUSY,        /**< refused: another capture runs */
    NB_TAKE_SLOT,        /**< refused: no such run in this method */
} nb_take_t;

/** @brief One capture. */
typedef struct {
    uint8_t      state;   /**< nb_cap_state_t */
    uint8_t      fail;    /**< nb_fail_t */
    uint8_t      n;       /**< reports averaged so far */
    bool         counts;  /**< its magnitudes are ADC counts */
    uint16_t     rpm0;    /**< its first report's rpm */
    float        sx;      /**< the sum of mag cos(deg) */
    float        sy;      /**< the sum of mag sin(deg) */
    float        smag;    /**< the sum of the magnitudes */
    float        srpm;    /**< the sum of the rpm */
    vibe_polar_t vec;     /**< done: the mean vector */
    float        mag;     /**< done: the mean magnitude */
    float        rpm;     /**< done: the mean rpm */
} nb_cap_t;

/** @brief The captures of one balance. */
typedef struct {
    uint8_t  method;          /**< nb_method_t */
    nb_cap_t cap[NB_SLOTS];   /**< 0 the baseline, 1 to 3 the trials */
    int8_t   taking;          /**< the capture that runs, -1 none */
    bool     have_last;       /**< last_seq and last_t hold a report */
    uint8_t  last_seq;        /**< the last report seen */
    uint32_t last_t;          /**< its t_node */
    uint32_t rev;             /**< counts every change */
} nb_session_t;

/**
 * @brief A report as the screen shows it.
 * @param linked Whether a node is linked (BAL_LINK_WAITING or later).
 * @param r      The last report, NULL for none.
 * @param age_ms Its age.
 * @param out    Receives the reading.
 */
void nb_read(bool linked, const rcnode_bal_report_t *r, uint32_t age_ms,
             nb_reading_t *out);

/**
 * @brief The quality word of a report's flags and quality.
 * @param flags   RCNODE_BF_*.
 * @param quality %, 0 to 100.
 * @return The word, never NB_Q_NONE.
 */
nb_quality_t nb_quality(uint16_t flags, uint8_t quality);

/**
 * @brief The word's text in the operator's language.
 * @param q The word.
 * @return The text; "--" for NB_Q_NONE and out of range.
 */
const char *nb_quality_text(nb_quality_t q);

/**
 * @brief The magnitude as shown: "0.42g", "412 counts", "--" when stale.
 * @param r The reading.  @param out The text.  @param n Its size.
 * @return out.
 */
const char *nb_fmt_mag(const nb_reading_t *r, char *out, size_t n);

/**
 * @brief The angle as shown: "265°", "--" when stale or without phase.
 * @param r The reading.  @param out The text.  @param n Its size.
 * @return out.
 */
const char *nb_fmt_deg(const nb_reading_t *r, char *out, size_t n);

/**
 * @brief The rpm as shown: "8000rpm", "--" when stale or 0.
 * @param r The reading.  @param out The text.  @param n Its size.
 * @return out.
 */
const char *nb_fmt_rpm(const nb_reading_t *r, char *out, size_t n);

/** @brief Clear: two runs, nothing taken. @param s NULL does nothing. */
void nb_init(nb_session_t *s);

/**
 * @brief Another method: every capture cleared.
 * @param s The session.  @param m nb_method_t; another value is ignored.
 */
void nb_set_method(nb_session_t *s, uint8_t m);

/**
 * @brief The runs a method takes.
 * @param m nb_method_t.
 * @return 2 or 4; 0 for another value.
 */
uint8_t nb_slots(uint8_t m);

/**
 * @brief TAKE for run @p slot: it averages the next fresh reports.  A run
 *        taken again starts over.
 * @param s     The session.
 * @param slot  The run, below nb_slots().
 * @param fresh Whether the reading is NB_LIVE_FRESH now.
 * @return What it got.
 */
nb_take_t nb_take(nb_session_t *s, uint8_t slot, bool fresh);

/**
 * @brief Every capture cleared, the method kept.
 * @param s The session.
 */
void nb_clear(nb_session_t *s);

/**
 * @brief The node's state, each frame: a running capture takes each new
 *        report (a seq or a t_node not seen) and fails on a stale one.
 * @param s      The session.
 * @param linked Whether a node is linked.
 * @param r      The last report, NULL for none.
 * @param age_ms Its age.
 */
void nb_feed(nb_session_t *s, bool linked, const rcnode_bal_report_t *r,
             uint32_t age_ms);

/**
 * @brief Whether every run of the method is DONE.
 * @param s The session.
 * @return true when nb_solve() has its input.
 */
bool nb_ready(const nb_session_t *s);

/**
 * @brief The correction of the captures through shared/vibe.
 * @param s       The session.
 * @param trial_g The trial mass, g.
 * @param out     Receives the solution.
 * @return false, out untouched, until nb_ready().
 */
bool nb_solve(const nb_session_t *s, float trial_g, vibe_solution_t *out);

/**
 * @brief The CSV header line, with its newline.
 * @return The line.
 */
const char *nb_csv_header(void);

/**
 * @brief One CSV row of a solved balance, with its newline: method, trial
 *        mass, each run's magnitude, angle and rpm, the unit, the status,
 *        the correction's mass and angle, the blade count and the two
 *        blades it falls between.
 * @param s       The session, nb_ready().
 * @param trial_g The trial mass, g.
 * @param sol     Its solution.
 * @param blades  The rotor's blades.
 * @param out     The row.
 * @param n       Its size, NB_CSV_MAX.
 * @return The length; 0 when it does not fit or the session is not ready.
 */
size_t nb_csv_row(const nb_session_t *s, float trial_g,
                  const vibe_solution_t *sol, int blades, char *out,
                  size_t n);

/* ------------------------------------------- the panel's one instance */

/**
 * @brief The node's state for the screens, each frame from the panel.
 * @param linked Whether a node is linked.
 * @param r      The last report, NULL for none.
 * @param age_ms Its age.
 */
void node_balance_feed(bool linked, const rcnode_bal_report_t *r,
                       uint32_t age_ms);

/** @brief The reading the screens show. @return Never NULL. */
const nb_reading_t *node_balance_live(void);

/** @brief The captures. @return Never NULL. */
nb_session_t *node_balance_session(void);

/**
 * @brief Moves whenever the reading or the captures change: a screen
 *        repaints on it.
 * @return The revision.
 */
uint32_t node_balance_rev(void);

/**
 * @brief The solution of the captures, kept from the frame they became
 *        ready, and its trial mass.
 * @param out     Receives the solution, may be NULL.
 * @param trial_g Receives its trial mass, may be NULL.
 * @return false while the captures are not ready.
 */
bool node_balance_result(vibe_solution_t *out, float *trial_g);

/**
 * @brief The CSV row of a solution not yet taken, once.
 * @param blades The rotor's blades.
 * @param out    The row.
 * @param n      Its size.
 * @return The length, 0 for none.
 */
size_t node_balance_take_csv(int blades, char *out, size_t n);

/** @brief Back to power-up: no node, two runs, nothing taken. */
void node_balance_reset(void);

#ifdef __cplusplus
}
#endif
