/**
 * @file
 * @brief Writes a run as CSV (comma-separated values) in the format
 *        shared/logfile reads.
 *
 * Three kinds of row: a bench run's (log_writer_row()), the same row with the
 * supply's figures for a run ARMED from SERVO (log_writer_servo_row()), and a
 * supply run's (log_writer_supply_row()).
 *
 * The sink is injected: the host suite writes into memory and the panel
 * writes into a FILE.  Nothing here depends on the card, so the writer is
 * tested against the reader by writing a run and parsing it back.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bench_state.h"
#include "log_cols.h"
#include "sense_link.h"
#include "supply.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The byte sink a log writer writes to, with its commit.
 */
typedef struct {
    /** @brief Returns bytes written, or a negative value on failure. */
    int (*write)(void *ctx, const void *data, size_t len);
    /**
     * @brief Commit what has been written, so that a power cut keeps it.
     *
     * NULL for a sink that needs no commit, such as memory.  Returns false
     * on failure.  Writing a row is not keeping it: on FAT (file allocation
     * table) the directory entry carries the length, and until that entry is
     * written the file reads as 0 bytes whatever the data sectors hold.
     */
    bool (*flush)(void *ctx);
    void *ctx;
} log_sink_t;

/** @brief The longest bench row, terminator included. */
#define LOG_WRITER_LINE_MAX 256

/**
 * @brief A log writer: its sink, its counts and its latched failure.
 */
typedef struct {
    log_sink_t sink;
    uint32_t   rows;
    uint32_t   pending;      /**< rows written since the last commit */
    float      last_s;       /**< t_s of the last row written */
    float      committed_s;  /**< t_s of the row the last commit ended at */
    bool       header_done;
    bool       failed;     /**< a write failed; the file is not trustworthy */
    /**
     * @brief The INA3221 columns go by cols (log_writer_set_cols()), not CH1 to
     *        CH3.
     */
    bool       by_map;
    log_cols_t cols;
    /**
     * @brief A bench row as it is built: up to 25 cells, here rather than on the
     *        stack of the task that writes the card.
     */
    char       line[LOG_WRITER_LINE_MAX];
} log_writer_t;

/*
 * What a power cut costs.
 *
 * The writer commits every LOG_WRITER_FLUSH_ROWS rows, and again once
 * LOG_WRITER_FLUSH_S have passed on the run's own clock, whichever comes
 * first.  At the panel's 20 Hz sample rate the two are the same point: 20
 * rows is 1000 ms of run.
 *
 * A run interrupted at any moment therefore loses at most 20 rows, spanning
 * less than 1.0 s of its own clock.  Twenty and not nineteen: the count
 * reaches LOG_WRITER_FLUSH_ROWS before the commit is attempted and is
 * cleared only once the commit has succeeded, so those twenty rows are not
 * durable for as long as the sink takes.  The price is one commit per second per
 * run.
 */
#define LOG_WRITER_FLUSH_ROWS 20u
#define LOG_WRITER_FLUSH_S    1.0f

/**
 * @brief Semicolon as the separator, full stop as the decimal point.
 *
 * The reader infers a file's number convention from every value in it and
 * rejects values that do not conform.  With ';' as the separator, '.' is
 * unambiguously a decimal point.  A file using ',' for both, as a German
 * locale writes, is ambiguous and the reader refuses it.
 */
#define LOG_WRITER_SEP ';'

/**
 * @brief The values of the last column, "source", of a bench file and a supply
 *        file: "measured" for a row whose values a meter or a supply produced,
 *        "model" for a row the panel's model produced (LINK_BN_SIMULATED on a
 *        bench row, supply_state_t.modelled on a supply row).
 *
 * A reader takes a file with the column and a file without it; a file written
 * without it ends at the previous column.
 */
#define LOG_WRITER_SRC_MEASURED "measured"
#define LOG_WRITER_SRC_MODEL    "model"

/**
 * @brief Sets up a writer on a sink.
 *
 * @param w    The writer to set up.
 * @param sink The sink the rows go to.
 */
void log_writer_init(log_writer_t *w, const log_sink_t *sink);

/**
 * @brief The INA3221 columns of this file, before its header: with SENSE_MAP in
 *        force a group per watched target (log_cols.h), read from the window
 *        bench_state_t.chw carries; NULL for SERVO_SENSE's CH1 to CH3, as an
 *        IOmcu older than 4.25 has them.
 *
 * Taken until the header is written, ignored after.
 *
 * @param w    The writer.
 * @param cols The INA3221 columns, or NULL.
 */
void log_writer_set_cols(log_writer_t *w, const log_cols_t *cols);

/**
 * @brief Write the header row.
 *
 * log_writer_row calls it if it has not been called,
 * so a file cannot lack a header.
 *
 * @param w The writer.
 * @return true on success; false when a write failed.
 */
bool log_writer_header(log_writer_t *w);

/**
 * @brief Append one sample at @p t_s seconds, and commit when the row completes
 *        an interval.
 *
 * False means one of two things, and log_writer_failed() tells them apart:
 *
 *   A write or the commit after it failed.  log_writer_failed() is true and
 *   stays true, and every later row is refused: the file has a hole in it
 *   and no amount of card answering again makes it whole.
 *
 *   The arguments were refused -- @p w or @p b NULL, or @p t_s not finite.
 *   Nothing was attempted and nothing is latched.  That is a caller with a
 *   bug rather than a card that has gone, and ending a run over it would
 *   throw away a recording for a reason the card had no part in.
 *
 * So a caller that watches only the latch has to check the return as well if
 * it can pass either of those.  The panel cannot: its row carries a struct by
 * value and a finite time: a supply run's sum of steps, or a bench run's
 * wall time since the arm (log_cadence.h).
 *
 * @param w   The writer.
 * @param t_s The time of the row, in s.
 * @param b   The sample to write.
 * @return true on success; false when refused or failed (see
 *         log_writer_failed()).
 */
bool log_writer_row(log_writer_t *w, float t_s, const bench_state_t *b);

/**
 * @brief The servo rail's supply on a SERVO run's row: the supply's last
 *        reading and the run's totals of it, for log_writer_servo_row().
 *
 * Bit SUPPLY_OK_VOLTAGE of ok says v is a reading, SUPPLY_OK_CURRENT that i
 * is one, and both that p is one; counted (BENCH_COUNTED_*) says which of
 * charge_mah and energy_wh the run has counted.  All 0 is a row with no
 * supply reading: its five cells are empty.  withheld marks a supply
 * whose source is not the row's -- the supply's model on a measured row, a
 * measured supply on a modelled row -- whose five cells are empty whatever
 * the run has counted.
 */
typedef struct {
    float   v;           /**< at the supply's output, V */
    float   i;           /**< out of it, A */
    float   p;           /**< v * i, W */
    float   charge_mah;  /**< since the switch to ARMED, mAh */
    float   energy_wh;   /**< since the switch to ARMED, Wh */
    uint8_t ok;          /**< SUPPLY_OK_* of the readings taken */
    uint8_t counted;     /**< BENCH_COUNTED_* of the totals */
    bool    withheld;    /**< the supply's source is not the row's */
} log_rail_t;

/**
 * @brief The supply's reading for a SERVO run's row, into @p out, with
 *        nothing counted.
 *
 * The readings that arrived, by @p s's ok bits, when the supply's source
 * is the row's: a supply that is the panel's model (supply_state_t.modelled)
 * on a modelled row, a measured one on a measured row.  Otherwise none, and
 * out->withheld says so: a row's source names all its figures.  @p s NULL
 * takes none.
 *
 * @param out          Receives the reading.
 * @param s            The supply's state, or NULL.
 * @param row_modelled true when the row is the panel's model
 *                     (bench_state_simulated()).
 */
void log_rail_take(log_rail_t *out, const supply_state_t *s,
                   bool row_modelled);

/**
 * @brief Count @p r's readings into the run's totals @p t over @p dt_s, and
 *        put the totals on @p r.
 *
 * Charge from a current reading, energy from a power reading, as
 * bench_totals_count() counts them: a step over BENCH_TOTALS_MAX_STEP_S
 * (1.0s) counts 1.0s, a step that is not positive counts nothing, and
 * nothing is counted unless @p driving.  bench_totals_reset() starts a run.
 * A withheld reading counts nothing and takes no totals: its row carries
 * none, the model's totals of an earlier stretch included.  Either pointer
 * NULL does nothing.
 *
 * @param t       The run's totals.
 * @param r       The reading; its charge_mah, energy_wh and counted are set.
 * @param dt_s    The time since the last count, in s.
 * @param driving true while the bank is ARMED.
 */
void log_rail_count(bench_totals_t *t, log_rail_t *r, float dt_s,
                    bool driving);

/**
 * @brief The servo rail's INA3221 window for a SERVO run's row, into @p out,
 *        with nothing counted: while an INA3221 channel is the rail's meter
 *        (servo_source.h).
 *
 * The window's mean bus voltage, its mean current and their product, as
 * SERVO's line shows them, each by the window's current and voltage flags.
 * None on a modelled row, and out->withheld says so: the part reads through
 * the IOmcu.  @p win NULL takes none.
 *
 * @param out          Receives the reading.
 * @param win          The newest window of the rail's channel, or NULL.
 * @param row_modelled true when the row is the panel's model.
 */
void log_rail_take_ina(log_rail_t *out, const sense_link_win_t *win,
                       bool row_modelled);

/**
 * @brief A SERVO run's commanded pulse and measured angle on its row.
 */
typedef struct {
    float    angle_deg;  /**< the horn's angle from the centre count, deg */
    uint16_t pulse_us;   /**< the commanded pulse, us */
    bool     pulse_ok;   /**< a pulse is commanded */
    bool     angle_ok;   /**< an angle was measured */
} log_servo_t;

/**
 * @brief The pulse and the angle in one 32-bit word, for the render side to
 *        hand the control task in one atomic store.
 *
 * The pulse in bits 16 to 31, 0 for none; the angle in centidegrees, signed,
 * in bits 0 to 15, 0x8000 for none.  An angle that is not finite or lies
 * past +-327.67deg packs as none, and so does a pulse of 0.
 *
 * @param pulse_ok  true when a pulse is commanded.
 * @param pulse_us  The commanded pulse, us.
 * @param angle_ok  true when an angle was measured.
 * @param angle_deg The angle, deg.
 * @return The packed word.
 */
uint32_t log_servo_pack(bool pulse_ok, uint16_t pulse_us, bool angle_ok,
                        float angle_deg);

/**
 * @brief log_servo_pack()'s word back into @p out, for a row: no angle on a
 *        modelled row, whose figures the encoder did not produce.
 *
 * @param packed       The word.
 * @param row_modelled true when the row is the panel's model.
 * @param out          Receives the pulse and angle; NULL does nothing.
 */
void log_servo_unpack(uint32_t packed, bool row_modelled, log_servo_t *out);

/**
 * @brief Append one sample of a run ARMED from SERVO: log_writer_row()'s row
 *        with voltage, current, power, charge and energy from @p rail.
 *
 * The header is the motor run's with `pulse (us);angle (deg)` before
 * `source`, so both files read by one rule.  The
 * supply's cells are written as a supply file writes them: voltage and power
 * to 0.01, current to 0.001, charge to 1mAh, energy to 0.01Wh; a cell rail
 * has no reading or total of is empty, and so are all five with @p rail
 * NULL.  rpm, the temperatures, the INA228's and the ESC's columns, the
 * INA3221 window and source are @p b's, as log_writer_row() writes them.
 * The pulse is written in whole us and the angle to 0.1deg, each empty when
 * @p sv has none, and both with @p sv NULL.  Same returns, commits and latch
 * as log_writer_row().
 *
 * @param w    The writer.
 * @param t_s  The time of the row, in s.
 * @param b    The bench sample.
 * @param rail The supply's reading and totals, or NULL.
 * @param sv   The pulse and angle, or NULL.
 * @return true on success; false when refused or failed (see
 *         log_writer_failed()).
 */
bool log_writer_servo_row(log_writer_t *w, float t_s, const bench_state_t *b,
                          const log_rail_t *rail, const log_servo_t *sv);

/**
 * @brief Append one supply sample, for a run that is the supply's output being
 *        on.
 *
 * The file's first row writes the supply header -- time; set (V);
 * voltage (V); limit (A); current (A); power (W); mode; charge (mAh);
 * energy (Wh); source -- so a file is one kind or the other and the caller keeps it
 * so.  Readings that did not arrive, and totals nothing counted, are empty
 * cells; the mode is CV, CC or OFF, and empty while the supply does not
 * answer.  Same returns, commits and latch as log_writer_row().
 *
 * @param w   The writer.
 * @param t_s The time of the row, in s.
 * @param s   The supply sample.
 * @return true on success; false when refused or failed (see
 *         log_writer_failed()).
 */
bool log_writer_supply_row(log_writer_t *w, float t_s,
                           const supply_state_t *s);

/**
 * @brief Commit the rows written since the last commit.
 *
 * log_writer_row() calls it on the schedule above.  A caller calls it by
 * hand when a run has gone quiet with rows still uncommitted, and at the end
 * of a run when closing the sink does not commit by itself.
 *
 * A commit with nothing pending does nothing and reports success: on a card
 * it would be a transaction that buys nothing.  A commit that fails latches
 * the writer the way a failed write does, because a row the sink will not
 * keep is a row the file has lost.
 *
 * @param w The writer.
 * @return true on success; false when the commit failed.
 */
bool log_writer_commit(log_writer_t *w);

/**
 * @brief Rows written and not yet committed: what a power cut would cost now.
 *
 * @param w The writer.
 * @return The rows written and not yet committed.
 */
static inline uint32_t log_writer_pending(const log_writer_t *w)
{
    return (w == NULL) ? 0u : w->pending;
}

/**
 * @brief True once any write has failed: the file is incomplete, not merely
 *        short.
 *
 * @param w The writer.
 * @return true once any write has failed.
 */
static inline bool log_writer_failed(const log_writer_t *w)
{
    return w == NULL || w->failed;
}

#ifdef __cplusplus
}
#endif
