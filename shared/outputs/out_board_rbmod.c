/**
 * @file
 * @brief The rbmod carrier: a Waveshare RP2350-CAN module soldered or
 *        socketed onto a board that wires every lead to a fixed GPIO.
 *
 * The catalogue is the module's 26 header pins with the module's pad
 * numbers, because the module is what sits on the carrier.  A pin that no
 * role uses is reserved and names why, so no output and no interface can
 * take it.  A pin a role uses is free in the catalogue; which role may use
 * it is the role table's to say (board_fix.h).
 *
 * The map, from the carrier's schematic labels:
 *
 *   PROGP01 to PROGP04   GP13, GP14, GP15, GP0
 *   PWM01 to PWM04       GP2, GP4, GP5, GP1
 *   SDA01 / SCL01        GP16 / GP17 (I2C0, the sensor bus)
 *   SDA02 / SCL02        GP18 / GP19 (I2C1, not driven yet)
 *   PDMINI TX / RX       GP7 / GP6
 *   TAP                  GP22
 *   PROGP_5V_EN          GP21, the enable of the programming ports' 5V
 *   HEARTBEAT            GP3
 *   CAN                  GP8 to GP12
 *   unused               GP20, GP26, GP27, GP28
 *
 * PROGP03 is GP15 on every carrier.  The first boards route it to GP26, an
 * ADC (analog-to-digital converter) pin that is not 5V tolerant, and carry a
 * wire from the lead to GP15; GP26 is reserved as unused.
 *
 * SPDX-License-Identifier: MIT
 */

#include "out_board_rbmod.h"

#include <stddef.h>

const outbind_pin_t outbind_rbmod_pins[OUTBIND_RBMOD_PIN_COUNT] = {
    {  0,  1, false, NULL },             /* PROGP04 */
    {  1,  2, false, NULL },             /* PWM04 */
    {  2,  4, false, NULL },             /* PWM01 */
    {  3,  5, true,  "heartbeat" },
    {  4,  6, false, NULL },             /* PWM02 */
    {  5,  7, false, NULL },             /* PWM03 */
    {  6,  9, false, NULL },             /* PDMINI RX */
    {  7, 10, false, NULL },             /* PDMINI TX */
    {  8, 11, true,  "CAN INT" },
    {  9, 12, true,  "CAN CS" },
    { 10, 14, true,  "CAN SCK" },
    { 11, 15, true,  "CAN MOSI" },
    { 12, 16, true,  "CAN MISO" },
    { 13, 17, false, NULL },             /* PROGP01 */
    { 14, 19, false, NULL },             /* PROGP02 */
    { 15, 20, false, NULL },             /* PROGP03 */
    { 16, 21, false, NULL },             /* SDA01 */
    { 17, 22, false, NULL },             /* SCL01 */
    { 18, 24, true,  "I2C 2 SDA" },
    { 19, 25, true,  "I2C 2 SCL" },
    { 20, 26, true,  "unused" },
    { 21, 27, true,  "PROG 5V EN" },
    { 22, 29, false, NULL },             /* TAP */
    { 26, 31, true,  "unused" },
    { 27, 32, true,  "unused" },
    { 28, 34, true,  "unused" },
};

/**
 * @brief J1 and J2 in their position order (the carrier's netlist): J1 the
 *        programming ports on the 5V of M7, J2 the servo outputs on the PD
 *        mini's two rails.
 */
static const outbind_conn_t k_conns[] = {
    { "J1", (uint8_t)OUTBIND_CONN_PROG, 4u, { 0u, 13u, 14u, 15u },
      { "VCC_PROGPORT", "VCC_PROGPORT", "VCC_PROGPORT", "VCC_PROGPORT" } },
    { "J2", (uint8_t)OUTBIND_CONN_OUT, 4u, { 5u, 4u, 2u, 1u },
      { "VCC_SERVOPORT_B", "VCC_SERVOPORT_B", "VCC_SERVOPORT_A",
        "VCC_SERVOPORT_A" } },
};

/** @brief The other connectors, and GPIO 21, which is on none. */
static const outbind_other_t k_others[] = {
    { "J3", "HEARTBEAT",  { 3u,  OUTBIND_NO_GPIO } },
    { "J5", "SENSOR I2C", { 16u, 17u } },
    { "J4", "I2C 2",      { 18u, 19u } },
    { "J6", "PD MINI",    { 7u,  6u } },
    { "J9", "TAP",        { 22u, OUTBIND_NO_GPIO } },
    { "",   "PROG 5V EN", { 21u, OUTBIND_NO_GPIO } },
};

const outbind_roles_t outbind_rbmod_roles = {
    .out        = { 2u, 4u, 5u, 1u },
    .prog       = { 13u, 14u, 15u, 0u },
    .sda        = 16u,
    .scl        = 17u,
    .sda2       = 18u,
    .scl2       = 19u,
    .pd_tx      = 7u,
    .pd_rx      = 6u,
    .tap        = 22u,
    .prog_5v_en = 21u,
    .heartbeat  = 3u,
    .conns       = k_conns,
    .conn_count  = (uint8_t)(sizeof(k_conns) / sizeof(k_conns[0])),
    .others      = k_others,
    .other_count = (uint8_t)(sizeof(k_others) / sizeof(k_others[0])),
};
