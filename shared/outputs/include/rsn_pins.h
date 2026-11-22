/**
 * @file
 * @brief The remote servo node's Core2350B (board 3): the pins of its own
 *        signals, which no page names.
 *
 * The catalogue and the role pins are in shared/outputs/out_board_rsn.c;
 * these are the on-board signals the catalogue leaves out (the port
 * switches, the strap, the critical alert) and the ones it reserves (the
 * UART to the ES3C28P, the rail switch, the LED).  The IOmcu image built
 * with IOMCU_BOARD=rsn drives them (firmware/iomcu/src/iomcu_rsn.c); the
 * carrier's pull-downs hold RAIL_EN and PORT_EN low while it does not.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_RSN_PINS_H
#define RCBENCH_RSN_PINS_H

/** @brief UART0 TX to the ES3C28P, P1.2. */
#define RSN_PIN_UART_TX   0u
/** @brief UART0 RX from the ES3C28P, P1.4. */
#define RSN_PIN_UART_RX   1u
/** @brief The master rail switch's enable, P1.3; 4.7kΩ pull-down. */
#define RSN_PIN_RAIL_EN   2u
/** @brief The heartbeat from ES3C28P IO21, P1.6; 4.7kΩ pull-down. */
#define RSN_PIN_HEARTBEAT 3u
/** @brief PORT_EN1, the enable of port 1's switch; port k (0 to 8) on
 *         GPIO 6 + k, P1.7 to P1.15, 100kΩ pull-down each. */
#define RSN_PIN_PORT_EN0  6u
/** @brief The SW_FITTED strap, P3.2: tied to GND on a carrier with the
 *         port switches, open (the pad's pull-up) without them. */
#define RSN_PIN_SW_FITTED 15u
/** @brief Servo output S1, P3.4; output k (0 to 8) on GPIO 16 + 2k. */
#define RSN_PIN_OUT0      16u
/** @brief Receiver input IN1, P3.3; input k (0 to 8) on GPIO 17 + 2k. */
#define RSN_PIN_IN0       17u
/** @brief The PD mini's UART TX, to the module's DM, P4.5. */
#define RSN_PIN_PD_TX     34u
/** @brief The PD mini's UART RX, from its DP, P4.8. */
#define RSN_PIN_PD_RX     35u
/** @brief CRIT_N, the three INA3221s' Critical pins wired together, P4.7;
 *         10kΩ pull-up to 3V3; low while one holds its alert. */
#define RSN_PIN_CRIT_N    36u
/** @brief The Core2350B's red user LED, through 470Ω to GND, P4.12. */
#define RSN_PIN_LED       39u
/** @brief The PSRAM's chip select on the Core2350B1 and B2, P2.3. */
#define RSN_PIN_PSRAM_CS  47u

/** @brief The ports: SERVO OUT 1 to 9. */
#define RSN_PORTS         9u

/** @brief Port @p k's (0 to 8) switch enable. */
#define RSN_PIN_PORT_EN(k) (RSN_PIN_PORT_EN0 + (unsigned)(k))
/** @brief Servo output @p k's (0 to 8) pin. */
#define RSN_PIN_OUT(k)     (RSN_PIN_OUT0 + 2u * (unsigned)(k))
/** @brief Receiver input @p k's (0 to 8) pin. */
#define RSN_PIN_IN(k)      (RSN_PIN_IN0 + 2u * (unsigned)(k))

/** @brief The highest SUPPLY set point on board 3, mV: the 9.1V clamp's
 *         lowest 8.65V less 0.25V. */
#define RSN_SUPPLY_MAX_MV 8400u
/** @brief Each channel's shunt, 0.1mΩ: 20.0mΩ. */
#define RSN_SHUNT_DMOHM   200u
/** @brief Each channel's critical alert limit when SENSE_LIMIT sets none
 *         lower, mA: 80mV over 20mΩ. */
#define RSN_CRIT_MA       4000u
/** @brief Each channel's SENSE_LIMIT in a fresh record, mA: the port
 *         switch's 3A rating. */
#define RSN_LIMIT_MA      3000u

#endif /* RCBENCH_RSN_PINS_H */
