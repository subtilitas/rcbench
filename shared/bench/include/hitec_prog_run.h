/**
 * @file
 * @brief The run seam of the programmer for servos by Hitec, the model's
 *        side: a run's servo in travel units and the run's writes of its two
 *        end points.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this module works with.
 *
 * A run (the limit run, AUTO end points) programs LEFT END and RIGHT END and
 * nothing else.  Its start, a button held 2s, is the operator's consent:
 * inside a run (hitec_prog_run_begin() to hitec_prog_run_end()) the run's
 * own edits of an end point or of NEUTRAL take HITEC_RULE_END and
 * HITEC_RULE_CENTRE as confirmed, and the run's writes switch ENABLE WRITE
 * TO SERVO on.  An edit of the operator inside a run still waits for CHECK
 * THIS CHANGE, and outside a run hitec_prog_run_write() is refused.  Every
 * hard rule stays: the edit's encoding, the D series order left end <
 * neutral < right end, the write gate, the second read, the record, the
 * read-back.
 *
 * Travel units: D series counts from NEUTRAL 0xC2, left = 0xC2 - 0xB2 and
 * right = 0xB0 - 0xC2, on a CW and a CCW servo alike; classic servos the
 * raw 8-bit cell, left 0x14 and right 0x13 on a CW servo, swapped on a CCW
 * one (0x1B bit 0), as hitec_reg.h decodes them.
 *
 * The backup file: the run's first write that sends a step saves the
 * session's backup to HITEC001 to HITEC999 first (hitec_prog.h), the servo
 * as the run found it.  A later session of the same run saves none.
 *
 * SPDX-License-Identifier: MIT
 */
/* hitec_prog_run.h: the model's side.  Added to hitec_prog_t in
 * hitec_prog.h: `bool run; bool run_wrote;` (two fields) -- the only edit
 * of hitec_prog.h. */
#pragma once
#include "hitec_prog.h"
#ifdef __cplusplus
extern "C" {
#endif
/** @brief The run's servo in travel units: D series counts from 0xC2
 *         (left = 0xC2 - 0xB2, right = 0xB0 - 0xC2), classic the raw cells
 *         (left 0x14 / right 0x13 on a CW servo, swapped on CCW). */
typedef struct {
    bool     ok;           /**< a servo read whose two end points edit */
    uint8_t  series;       /**< hitec_family_t */
    uint16_t value[2];     /**< left, right travel */
    uint16_t mid;          /**< D: 0xC2; classic 0 */
    uint16_t lo, hi;       /**< what the hard rules take, per travel unit */
    bool     cw;           /**< DIRECTION reads CW */
    char     backup[HITEC_CARD_NAME_MAX]; /**< the session's backup file,
                                               hitec_prog_backup_name() */
} hitec_run_servo_t;
/** @brief A run begins: HITEC_RULE_END and HITEC_RULE_CENTRE count as
 *         confirmed, ENABLE WRITE goes on for the run's writes.
 *  @param p The programmer. */
void hitec_prog_run_begin(hitec_prog_t *p);
/** @brief The run ends: the switch off, the rules asked again.
 *  @param p The programmer. */
void hitec_prog_run_end(hitec_prog_t *p);
/** @brief The servo read, in travel units.
 *  @param p   The programmer.
 *  @param out Receives the servo; zero but series and backup with none.
 *  @return out->ok. */
bool hitec_prog_run_servo(const hitec_prog_t *p, hitec_run_servo_t *out);
/** @brief Edit the draft's two end points from travel units and start the
 *         write (hitec_prog_write()).
 *  @param p     The programmer.
 *  @param left  The left travel, lo to hi of hitec_prog_run_servo().
 *  @param right The right travel, lo to hi.
 *  @return HITEC_PROG_MSG_NONE when it starts. */
hitec_prog_msg_id_t hitec_prog_run_write(hitec_prog_t *p, uint16_t left, uint16_t right);
/** @brief The values the servo holds after a write's read-back, travel
 *         units.
 *  @param p     The programmer.
 *  @param value Receives the left and the right travel.
 *  @return false while no read-back stands. */
bool hitec_prog_run_values(const hitec_prog_t *p, uint16_t value[2]);
/** @brief Travel units to registers and back, for the tests.
 *  @param sv     The servo of hitec_prog_run_servo().
 *  @param side   0 left, 1 right.
 *  @param travel The travel.
 *  @return The register or cell; D series 0 to 65535, saturated. */
uint16_t hitec_run_to_reg(const hitec_run_servo_t *sv, uint8_t side, uint16_t travel);
/** @brief A register or cell to travel units.
 *  @param sv   The servo of hitec_prog_run_servo().
 *  @param side 0 left, 1 right.
 *  @param reg  The register or cell.
 *  @return The travel; D series 0 for a register on the other side of
 *          NEUTRAL. */
uint16_t hitec_run_to_travel(const hitec_run_servo_t *sv, uint8_t side, uint16_t reg);
#ifdef __cplusplus
}
#endif
