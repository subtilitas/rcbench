/**
 * @file
 * @brief The remote servo node (board 3): a Waveshare Core2350B (RP2350B,
 *        16MB flash) in a socket on a carrier that wires every lead to a
 *        fixed GPIO.
 *
 * The catalogue holds the role pins only, 27 of the 38 the carrier uses:
 * the 9 port enables, CRIT_N and the SW_FITTED strap are on-board signals
 * and stay out (rsn_pins.h), so the page's 32 entries hold the rest.  A pin
 * a page role uses is free in the catalogue (the servo outputs, the sensor
 * bus, the PD mini's UART); every other listed pin is reserved and names
 * why.  A pin the catalogue leaves out is reserved by outbind_reserved_mask().
 *
 * The map:
 *
 *   S1 to S9 out          GPIO 16, 18, ... 32: each on its own PWM slice
 *                         (0 to 8), channel A
 *   IN1 to IN9            GPIO 17, 19, ... 33: one above its output
 *   PD mini TX / RX       GPIO 34 / 35
 *   UART to the ES3C28P   GPIO 0 (TX) / 1 (RX)
 *   RAIL_EN               GPIO 2
 *   HEARTBEAT             GPIO 3
 *   SENSE SDA / SCL       GPIO 4 / 5 (I2C0): INA3221 at 0x40, 0x41, 0x42
 *   LED                   GPIO 39
 *
 * Pad numbers: header P1 pin n is pad n, P2 pin n is 16 + n, P3 pin n is
 * 32 + n, P4 pin n is 48 + n; every listed pad is 63 or less, which the
 * catalogue's 6 bits hold.  The board carries no shape and no pad list: its
 * pads sit on 4 edges, and the SHAPE page describes two rows.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_board_rsn.h"

#include <stddef.h>

#include "link_pages.h"
#include "rsn_pins.h"

const outbind_pin_t outbind_rsn_pins[OUTBIND_RSN_PIN_COUNT] = {
    {  0,  2, true,  "link UART TX" },   /* P1.2 */
    {  1,  4, true,  "link UART RX" },   /* P1.4 */
    {  2,  3, true,  "RAIL EN" },        /* P1.3 */
    {  3,  6, true,  "heartbeat" },      /* P1.6 */
    {  4,  5, false, NULL },             /* SENSE SDA, P1.5 */
    {  5,  8, false, NULL },             /* SENSE SCL, P1.8 */
    { 16, 36, false, NULL },             /* S1, P3.4 */
    { 17, 35, true,  "RX IN1" },         /* P3.3 */
    { 18, 38, false, NULL },             /* S2, P3.6 */
    { 19, 37, true,  "RX IN2" },         /* P3.5 */
    { 20, 40, false, NULL },             /* S3, P3.8 */
    { 21, 39, true,  "RX IN3" },         /* P3.7 */
    { 22, 42, false, NULL },             /* S4, P3.10 */
    { 23, 41, true,  "RX IN4" },         /* P3.9 */
    { 24, 44, false, NULL },             /* S5, P3.12 */
    { 25, 43, true,  "RX IN5" },         /* P3.11 */
    { 26, 46, false, NULL },             /* S6, P3.14 */
    { 27, 45, true,  "RX IN6" },         /* P3.13 */
    { 28, 48, false, NULL },             /* S7, P3.16 */
    { 29, 47, true,  "RX IN7" },         /* P3.15 */
    { 30, 50, false, NULL },             /* S8, P4.2 */
    { 31, 52, true,  "RX IN8" },         /* P4.4 */
    { 32, 51, false, NULL },             /* S9, P4.3 */
    { 33, 54, true,  "RX IN9" },         /* P4.6 */
    { 34, 53, false, NULL },             /* PD MINI TX, P4.5 */
    { 35, 56, false, NULL },             /* PD MINI RX, P4.8 */
    { 39, 60, true,  "LED" },            /* P4.12 */
};

/** @brief J1 to J9, SERVO OUT 1 to 9: one position each, on the common
 *         servo rail through the port's switch, shunt and + link. */
static const outbind_conn_t k_conns[] = {
    { "J1", (uint8_t)OUTBIND_CONN_OUT, 1u, { 16u }, { "SERVO RAIL" } },
    { "J2", (uint8_t)OUTBIND_CONN_OUT, 1u, { 18u }, { "SERVO RAIL" } },
    { "J3", (uint8_t)OUTBIND_CONN_OUT, 1u, { 20u }, { "SERVO RAIL" } },
    { "J4", (uint8_t)OUTBIND_CONN_OUT, 1u, { 22u }, { "SERVO RAIL" } },
    { "J5", (uint8_t)OUTBIND_CONN_OUT, 1u, { 24u }, { "SERVO RAIL" } },
    { "J6", (uint8_t)OUTBIND_CONN_OUT, 1u, { 26u }, { "SERVO RAIL" } },
    { "J7", (uint8_t)OUTBIND_CONN_OUT, 1u, { 28u }, { "SERVO RAIL" } },
    { "J8", (uint8_t)OUTBIND_CONN_OUT, 1u, { 30u }, { "SERVO RAIL" } },
    { "J9", (uint8_t)OUTBIND_CONN_OUT, 1u, { 32u }, { "SERVO RAIL" } },
};

/** @brief The receiver inputs and the other connectors. */
static const outbind_other_t k_others[] = {
    { "J11", "RX IN1",     { 17u, OUTBIND_NO_GPIO } },
    { "J12", "RX IN2",     { 19u, OUTBIND_NO_GPIO } },
    { "J13", "RX IN3",     { 21u, OUTBIND_NO_GPIO } },
    { "J14", "RX IN4",     { 23u, OUTBIND_NO_GPIO } },
    { "J15", "RX IN5",     { 25u, OUTBIND_NO_GPIO } },
    { "J16", "RX IN6",     { 27u, OUTBIND_NO_GPIO } },
    { "J17", "RX IN7",     { 29u, OUTBIND_NO_GPIO } },
    { "J18", "RX IN8",     { 31u, OUTBIND_NO_GPIO } },
    { "J19", "RX IN9",     { 33u, OUTBIND_NO_GPIO } },
    { "J20", "LINK UART",  { 0u,  1u } },
    { "J21", "HEARTBEAT",  { 3u,  OUTBIND_NO_GPIO } },
    { "J23", "PD MINI",    { 34u, 35u } },
    { "J26", "SENSOR I2C", { 4u,  5u } },
    { "",    "RAIL EN",    { 2u,  OUTBIND_NO_GPIO } },
};

const outbind_roles_t outbind_rsn_roles = {
    .out        = { 16u, 18u, 20u, 22u, 24u, 26u, 28u, 30u, 32u },
    .prog       = { OUTBIND_NO_GPIO, OUTBIND_NO_GPIO, OUTBIND_NO_GPIO,
                    OUTBIND_NO_GPIO },
    .sda        = 4u,
    .scl        = 5u,
    .sda2       = OUTBIND_NO_GPIO,
    .scl2       = OUTBIND_NO_GPIO,
    .pd_tx      = (uint8_t)RSN_PIN_PD_TX,
    .pd_rx      = (uint8_t)RSN_PIN_PD_RX,
    .tap        = OUTBIND_NO_GPIO,
    .prog_5v_en = OUTBIND_NO_GPIO,
    .heartbeat  = (uint8_t)RSN_PIN_HEARTBEAT,
    .prog_on_outs  = true,
    .prog_dshot    = true,
    .supply_max_mv = (uint16_t)RSN_SUPPLY_MAX_MV,
    .no_encoder    = true,
    .ina        = {
        .on    = true,
        .part  = 0u,
        .parts = 3u,
        .watch = { (uint8_t)LINK_SM_WATCH_OUT(0u), (uint8_t)LINK_SM_WATCH_OUT(1u),
                   (uint8_t)LINK_SM_WATCH_OUT(2u), (uint8_t)LINK_SM_WATCH_OUT(3u),
                   (uint8_t)LINK_SM_WATCH_OUT(4u), (uint8_t)LINK_SM_WATCH_OUT(5u),
                   (uint8_t)LINK_SM_WATCH_OUT(6u), (uint8_t)LINK_SM_WATCH_OUT(7u),
                   (uint8_t)LINK_SM_WATCH_OUT(8u) },
        .feeds = { 1ull << 16, 1ull << 18, 1ull << 20, 1ull << 22, 1ull << 24,
                   1ull << 26, 1ull << 28, 1ull << 30, 1ull << 32 },
        .shunt_dmohm = (uint16_t)RSN_SHUNT_DMOHM,
        .crit_ma     = (uint16_t)RSN_CRIT_MA,
        .limit_ma    = (uint16_t)RSN_LIMIT_MA,
    },
    .conns       = k_conns,
    .conn_count  = (uint8_t)(sizeof(k_conns) / sizeof(k_conns[0])),
    .others      = k_others,
    .other_count = (uint8_t)(sizeof(k_others) / sizeof(k_others[0])),
};
