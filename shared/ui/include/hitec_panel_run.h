/**
 * @file
 * @brief The run seam of the programmer for servos by Hitec, the overlay's
 *        side: the CONNECT, the writes and the LEAVE a run asks of the
 *        overlay's model, and the poll that reports each step's end.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this overlay works with.
 *
 * The shape of kst_panel.h's kst_panel_run_*.  A run's start, a button held
 * 2s, is the operator's consent: hitec_panel_run_begin() hands it to the
 * model (hitec_prog_run.h), no CHECK THIS CHANGE page is shown, and every
 * step closes what the overlay has open.  The rest is the model's: the hard
 * rules, the second read, the record, the backup file of the run's first
 * write, the D series save (0x70) and reboot (0x46) with 100ms and 1000ms
 * after them, the classic balance 0x1C, the read-back.  Values are in
 * travel units (hitec_run_servo_t).
 *
 * One task only: the render loop's, as the overlay.
 *
 * SPDX-License-Identifier: MIT
 */
/* hitec_panel_run.h: the overlay's side, the shape of kst_panel.h's
 * kst_panel_run_*. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "hitec_prog_run.h"
#ifdef __cplusplus
extern "C" {
#endif
/** @brief How a run's step of the programmer stands. */
typedef enum { HITEC_PANEL_RUN_NONE = 0, HITEC_PANEL_RUN_BUSY, HITEC_PANEL_RUN_OK, HITEC_PANEL_RUN_FAILED } hitec_panel_run_t;
/** @brief The programmer's port is in PWM with no operation under way: a
 *         run's CONNECT can start.
 *  @return true when it can. */
bool hitec_panel_run_ready(void);
/** @brief A run begins: no step and no write of it yet, the model's run
 *         flag set (hitec_prog_run_begin()). */
void hitec_panel_run_begin(void);
/** @brief CONNECT on the SERVO screen's port for SET_HITEC_SERIES; with
 *         @p supply the PD mini at @p mv and @p ma.
 *  @param supply true: the session asks the PD mini for @p mv and @p ma.
 *  @param mv     The programming voltage, mV.
 *  @param ma     The current limit, mA.
 *  @return HITEC_PROG_MSG_NONE when taken; HITEC_PROG_MSG_BUSY while the
 *          port is not in PWM; else the model's refusal. */
hitec_prog_msg_id_t hitec_panel_run_connect(bool supply, uint16_t mv, uint16_t ma);
/** @brief Write the two end points and nothing else
 *         (hitec_prog_run_write()).
 *  @param left  The left travel.
 *  @param right The right travel.
 *  @return HITEC_PROG_MSG_NONE when the write starts. */
hitec_prog_msg_id_t hitec_panel_run_write(uint16_t left, uint16_t right);
/** @brief LEAVE: the write switch off, the draft dropped, the port back to
 *         PWM.
 *  @return HITEC_PROG_MSG_NONE when it starts. */
hitec_prog_msg_id_t hitec_panel_run_leave(void);
/** @brief How the step asked last ended; the end is taken.
 *  @return HITEC_PANEL_RUN_NONE with no step asked, BUSY while it runs,
 *          then OK or FAILED once.  A write is OK when its read-back holds
 *          the two values asked; its end switches the write off. */
hitec_panel_run_t   hitec_panel_run_poll(void);
/** @brief Whether the CONNECT of hitec_panel_run_connect() ended with no
 *         servo answering: the model's last message is
 *         HITEC_PROG_MSG_NO_D or HITEC_PROG_MSG_NO_C, said after that call.
 *  @return true for no servo. */
bool hitec_panel_run_no_servo(void);
/** @brief The servo as read, in travel units (hitec_prog_run_servo()).
 *  @param out Receives it.
 *  @return out->ok. */
bool hitec_panel_run_servo(hitec_run_servo_t *out);
/** @brief The values the servo holds after the last write's read-back
 *         (hitec_prog_run_values()).
 *  @param value Receives the left and the right travel.
 *  @return false while no read-back stands. */
bool hitec_panel_run_values(uint16_t value[2]);
/** @brief The file that holds the servo as the run found it.
 *  @return Its base name, kept over the run's LEAVEs; "" for none. */
const char *hitec_panel_run_backup(void);
/** @brief The run is over: the model's run flag and the write switch off
 *         (hitec_prog_run_end()), the programming voltage and current back
 *         to the operator's. */
void hitec_panel_run_end(void);
#ifdef __cplusplus
}
#endif
