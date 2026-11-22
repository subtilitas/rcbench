/**
 * @file
 * @brief A board with soldered pins: each role on its own pin, and nothing
 *        else.
 *
 * On such a board (outbind_roles() not NULL) the pin of every output and
 * every interface is a fact of the board.  The IOmcu starts each role on
 * its pin whatever its flash kept (board_fix_store()), and refuses a write
 * that would move a role off its pin (board_fix_write_ok(),
 * board_fix_slots_ok()).  The pages' own rules still apply on top.
 *
 *   OUTPUTS, BIND_OUT  a slot with a driver sits on a servo output
 *                      (roles.out); a slot with LINK_DRIVER_NONE is not
 *                      judged.
 *   PROG               each port's PIN is its own programming pin
 *                      (roles.prog), or with roles.prog_on_outs any servo
 *                      output or LINK_PG_PIN_NONE; FUNC is NONE or SERVO
 *                      PWM (protocol 4.36), and with roles.prog_dshot a
 *                      DShot rate as well: a programming port carries the
 *                      KST and HITEC wire protocols and the tester's servo
 *                      pulse.  It starts on NONE (board_fix_store()).
 *   SENSE              SDA and SCL are roles.sda and roles.scl; with
 *                      roles.no_encoder ENABLE takes no AS5600 bit.
 *   TONE               PIN is roles.tap; with no tap (OUTBIND_NO_GPIO)
 *                      the page takes no write.
 *   SUPPLY             TX and RX are roles.pd_tx and roles.pd_rx; SET_MV
 *                      is at most roles.supply_max_mv where that is set.
 *   SENSE_MAP          with roles.ina on, the roles.ina.parts parts from
 *                      0x40 + roles.ina.part are fitted and their channels'
 *                      WATCH are roles.ina.watch, and with
 *                      roles.ina.shunt_dmohm set their shunts are that
 *                      (board_fix_map_ok()); FAST and the other parts stay
 *                      the page's.
 *
 * With @p r NULL -- a board whose pins are chosen -- every function here
 * accepts everything and changes nothing.
 *
 * Pure logic, host-tested.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_BOARD_FIX_H
#define RCBENCH_BOARD_FIX_H

#include <stdbool.h>
#include <stdint.h>

#include "out_bind.h"
#include "out_store_rec.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The shunt board_fix_store() gives each channel of the board's
 *         INA3221 when all three are 0: 100.0mOhm in 0.1mOhm, SETUP >
 *         SENSORS' default. */
#define BOARD_FIX_INA_DMOHM 1000u

/** @brief The kept function word with every port on FUNC NONE
 *         (prog_port_kept_func()). */
#define BOARD_FIX_PROG_FUNC_NONE                                     \
    ((uint16_t)(((LINK_PG_FN_NONE + 1u) << 0) | ((LINK_PG_FN_NONE + 1u) << 4) \
                | ((LINK_PG_FN_NONE + 1u) << 8)                      \
                | ((LINK_PG_FN_NONE + 1u) << 12)))

/**
 * @brief Whether @p gpio is one of the board's servo outputs.
 *
 * @param r     The board's roles, or NULL.
 * @param gpio  GPIO number.
 * @return      The output's index, 0 to OUTBIND_FIXED_OUTS - 1, or -1 when
 *              @p gpio is none of them or @p r is NULL.
 */
int board_fix_out_index(const outbind_roles_t *r, unsigned gpio);

/**
 * @brief Whether a whole OUTPUTS page keeps every driven slot on a servo
 *        output.
 *
 * @param r      The board's roles, or NULL for a board whose pins are chosen.
 * @param slots  LINK_OS_ALL registers: OUTPUTS and OUTPUTS_HI as the write
 *               would leave them.
 * @return       true when @p r is NULL or every slot whose driver is not
 *               LINK_DRIVER_NONE names a pin in roles.out; false otherwise,
 *               and for @p slots NULL with @p r set.
 */
bool board_fix_slots_ok(const outbind_roles_t *r, const uint16_t *slots);

/**
 * @brief Whether a whole SENSE_MAP keeps the board's INA3221 as the board
 *        solders it.
 *
 * @param r    The board's roles, or NULL for a board whose pins are chosen.
 * @param map  LINK_SM_KEPT registers: the map as the write would leave it.
 * @return     true when @p r is NULL or carries no INA3221, or when ENABLE
 *             fits its part and each of its channels' WATCH is the role's;
 *             false otherwise, and for @p map NULL with an INA3221 in @p r.
 */
bool board_fix_map_ok(const outbind_roles_t *r, const uint16_t *map);

/**
 * @brief Whether a write of @p n registers from @p off to @p page keeps every
 *        role on its pin.
 *
 * Judges the registers the write carries and no other.  The OUTPUTS,
 * BIND_OUT and SENSE_MAP pages are judged whole by board_fix_slots_ok() and
 * board_fix_map_ok() instead, and are accepted here.
 *
 * @param r     The board's roles, or NULL.
 * @param page  A link_page_t number.
 * @param off   First register written.
 * @param n     Registers written.
 * @param in    The values, @p n of them.
 * @return      true when @p r is NULL, or every pin register in the write
 *              holds its role's pin and every PROG FUNC register holds
 *              LINK_PG_FN_NONE or LINK_PG_FN_SERVO_PWM; false otherwise,
 *              and for @p in NULL with @p r set and @p n not 0.
 */
bool board_fix_write_ok(const outbind_roles_t *r, uint8_t page, uint8_t off,
                        uint8_t n, const uint16_t *in);

/**
 * @brief Each channel's critical alert limit: roles.ina.crit_ma, or the
 *        channel's SENSE_LIMIT where that is lower and not 0.
 *
 * @param r         The board's roles, or NULL.
 * @param limit_ma  LINK_SM_CHANNELS limits in mA, 0 for none; NULL: none.
 * @param crit_ma   Receives LINK_SM_CHANNELS limits in mA: 0 for a channel
 *                  the board's INA3221s do not hold, every one 0 without
 *                  roles or roles.ina.crit_ma.  NULL does nothing.
 */
void board_fix_crit(const outbind_roles_t *r, const uint16_t *limit_ma,
                    uint16_t *crit_ma);

/**
 * @brief Put each role of a kept configuration on its pin.
 *
 * A slot whose driver is not LINK_DRIVER_NONE and whose pin is no servo
 * output is cleared: driver, pin, range and rate 0.  The supply's TX and
 * RX, the sensor bus's SDA and SCL, the tap's pin and each programming
 * port's pin are set to the role's pin (LINK_PG_PIN_NONE with
 * roles.prog_on_outs); every port's function to NONE.  Without a tap the
 * TONE page is disabled; with roles.no_encoder the encoder's enable is
 * cleared.  A SUPPLY set point is not kept, so the write check alone holds
 * roles.supply_max_mv.
 * With roles.ina on, the kept SENSE_MAP fits the parts and holds their
 * channels' WATCH, and is in force (sense_map_set 1); their shunts are
 * roles.ina.shunt_dmohm where that is set, else a part whose shunts are
 * all 0 gets BOARD_FIX_INA_DMOHM on each; with roles.ina.limit_ma set, a
 * record whose SENSE_LIMIT is 0 on every one of their channels gets that
 * limit on each.  Other enables, addresses,
 * ranges, rates, shunts and endpoints stay as kept.
 *
 * @param r    The board's roles, or NULL: then nothing changes.
 * @param cfg  The configuration, from out_store_cfg_read() or
 *             out_store_cfg_defaults(). NULL is ignored.
 */
void board_fix_store(const outbind_roles_t *r, out_store_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_BOARD_FIX_H */
