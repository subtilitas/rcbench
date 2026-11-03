/**
 * @file
 * @brief The programming port the servo tester and the programmers use on the
 *        rbmod carrier, chosen on the programmers' PORT row (protocol 4.36).
 *
 * The choice is SET_SERVO_TARGET, as on a board whose pins are chosen: 1 to
 * 4 for PROGP01 to PROGP04, 0 for the servo outputs.  So prog_link's TARGET,
 * the INA3221 channel the SERVO screen reads, the PROGRAM list's port and
 * the KST limit search follow it unchanged.
 *
 * On the carrier a port's FUNC is NONE or SERVO PWM (board_fix.h).  The
 * chosen port needs SERVO PWM for the tester's pulse; every other port goes
 * back to NONE, so no unchosen port holds a PWM slice.  prog_choice_tick()
 * asks for those FUNCs one at a time through port_view_ask().  Until the
 * chosen port reads SERVO PWM the IOmcu's TARGET stays 0 and the tester
 * takes no command (prog_link_no_drive()).
 *
 * The carrier's limits, by name:
 *   - PROGP04 (GPIO 0) shares PWM slice 0 with PWM04 (GPIO 1).  While PWM04
 *     carries a PWM output, PROGP04 is refused (prog_choice_refusal()): the
 *     binding of PWM01 to PWM04 wins.  PROGP02 and PROGP03 share slice 7
 *     and only one of them carries a pulse.
 *   - The ports run from M7's fixed 5V on GPIO 21.  The pulse is asked for
 *     only while the PROG 5V box reads ON (prog_choice_want_pwm()), and
 *     PORT PWM ON, a test and a limit run are refused with a line naming
 *     the box while it is off.  Voltage steps reach the servo rails, not
 *     the ports: a test with steps on a port is refused.
 *
 * On a board whose pins are chosen every function here passes the settings
 * through unchanged and asks nothing.
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

/**
 * @brief The far end's protocol minor, at each link-up; 0 with no link.
 * @param minor LINK_ID_PROTOCOL_MINOR as read
 */
void prog_choice_set_far_minor(uint16_t minor);

/**
 * @brief Whether the board solders its pins (the rbmod carrier).
 * @return true on a board with roles
 */
bool prog_choice_carrier(void);

/**
 * @brief Whether the carrier's PORT list is offered: a carrier whose IOmcu
 *        speaks 4.36 or later (LINK_MINOR_CARRIER_PWM).
 * @return true when a port can be chosen
 */
bool prog_choice_offered(void);

/**
 * @brief The port chosen: SET_SERVO_TARGET.
 * @return 1 to 4, or 0 for the servo outputs
 */
uint8_t prog_choice_port(void);

/**
 * @brief Why port @p port cannot be chosen, by name, or false when it can.
 *
 * On the carrier: PROGP04 while PWM04, on its slice, carries a PWM output.
 * Never on a board whose pins are chosen.
 *
 * @param port 1 to 4
 * @param buf  takes the line; may be NULL
 * @param cap  its size
 * @return true with a reason
 */
bool prog_choice_refusal(uint8_t port, char *buf, size_t cap);

/**
 * @brief Whether the choice is locked: the SERVO TARGET rules (ARMED or
 *        asked to arm, a test, a sweep or a limit run, a session holding a
 *        port), an ARM asked for included (servo_screen_arm_asked()).
 * @return true while locked
 */
bool prog_choice_locked(void);

/**
 * @brief The lock's reason line.
 * @param buf takes it
 * @param cap its size
 * @return true while locked
 */
bool prog_choice_lock_line(char *buf, size_t cap);

/**
 * @brief Choose port @p port and keep it (settings_request_save()).
 * @param port 1 to 4, or 0 for the servo outputs
 * @return true when taken: offered, not locked, not refused
 */
bool prog_choice_pick(uint8_t port);

/**
 * @brief The entry of port @p port in the PORT list: `PROGP01  GPIO 13`.
 * @param port 1 to 4
 * @param buf  takes it
 * @param cap  its size
 */
void prog_choice_entry(uint8_t port, char *buf, size_t cap);

/**
 * @brief What prog_link writes to TARGET.
 *
 * SET_SERVO_TARGET; on a carrier whose IOmcu is older than 4.36, 0.  The
 * chosen port stays the destination while its FUNC is asked for or it is
 * refused: prog_link_wanted() keeps the IOmcu's TARGET at 0 until the port
 * reads SERVO PWM, and prog_link_no_drive() holds every command but a
 * release meanwhile, so nothing reaches the servo outputs in its place.
 *
 * @return 0 to 4
 */
uint8_t prog_choice_want_target(void);

/**
 * @brief What prog_link writes to the target's PWM bit.
 *
 * SET_SERVO_PORT_PWM on a board whose pins are chosen; on the carrier only
 * with a target that is not refused and the PROG 5V box reading ON.
 *
 * @return true for the pulse
 */
bool prog_choice_want_pwm(void);

/**
 * @brief Whether the chosen carrier port's pulse waits for PROG 5V ON: on
 *        the carrier, a port chosen and the PROG 5V box not reading ON.
 * @return true when a pulse there is refused
 */
bool prog_choice_needs_5v(void);

/**
 * @brief One step of the FUNCs on the carrier: at most one ask, for the
 *        first port whose FUNC is not what the choice wants.  Nothing while
 *        an ask waits, on a board whose pins are chosen, or before the
 *        ports are read.  An ask refused is not repeated until the choice
 *        or the ports change.
 */
void prog_choice_tick(void);

/**
 * @brief A programmer's text as the board needs it: on the carrier
 *        TX_KP_NEED_TARGET_2 (`Tap TARGET and set SERVO TESTER ...`) is the
 *        PORT row's, or the IOmcu update an IOmcu older than 4.36 needs;
 *        every other text, and every text elsewhere, as it is.
 * @param id a TX_ ID, as a number
 * @return the ID to show
 */
int prog_choice_text(int id);

/**
 * @brief Start over: no far end, nothing refused.  For the tests.
 */
void prog_choice_reset(void);

#ifdef __cplusplus
}
#endif
