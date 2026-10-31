/**
 * @file ew_map_am.c
 * @brief The fields of AM32, settings revisions 3 and 4.  AM32 is a name of a
 *        firmware; it is used here only to say which ESCs this module works
 *        with.
 *
 * Both revisions have the same map.  Revision 3 holds an old name character
 * at 0x0D, so that byte is a field of revision 4 only.  A revision below 3
 * has no row here.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_map.h"

/** @brief The labels of a switch. */
static const char *const k_off_on[] = { "OFF", "ON" };
/** @brief Variable PWM (pulse-width modulation), 0 to 2. */
static const char *const k_vpwm[] = { "FIXED", "BY INTERVAL", "AUTOMATIC" };
/** @brief The input type, 0 to 4. */
static const char *const k_input[] = { "AUTO", "DSHOT", "SERVO", "SERIAL",
                                       "DSHOT ARMED" };

/** @brief A switch. */
#define SWITCH(nm, o, fl)                                                   \
    { .name = (nm), .labels = k_off_on, .unit = "", .off = (o),             \
      .access = EW_ACC_EDIT, .flags = (fl), .lo = 0u, .hi = 1u,             \
      .from = 3u, .to = 4u }
/** @brief A number with a unit. */
#define NUMBER(nm, o, l, h, un, fl)                                         \
    { .name = (nm), .unit = (un), .off = (o), .access = EW_ACC_EDIT,        \
      .flags = (fl), .lo = (l), .hi = (h), .from = 3u, .to = 4u }
/** @brief A number that reads (raw x m + a) with @p d decimal places. */
#define SCALED(nm, o, l, h, m, a, d, un, fl)                                \
    { .name = (nm), .unit = (un), .mul = (m), .add = (a), .off = (o),       \
      .access = EW_ACC_EDIT, .flags = (fl), .lo = (l), .hi = (h),           \
      .dec = (d), .from = 3u, .to = 4u }
/** @brief A byte that is shown and not edited. */
#define SHOWN(nm, o)                                                        \
    { .name = (nm), .unit = "", .off = (o), .access = EW_ACC_SHOW,          \
      .lo = 0u, .hi = 255u, .from = 3u, .to = 4u }

const ew_field_t ew_map_am[] = {
    SHOWN("START MARKER", 0x00u),
    SHOWN("REVISION", 0x01u),
    SHOWN("BOOTLOADER", 0x02u),
    SHOWN("FIRMWARE MAJOR", 0x03u),
    SHOWN("FIRMWARE MINOR", 0x04u),
    NUMBER("POWER RAMP", 0x05u, 0u, 255u, "", 0u),
    NUMBER("MIN DUTY", 0x06u, 0u, 50u, "", 0u),
    SWITCH("STICK CAL OFF", 0x07u, 0u),
    SCALED("VOLTAGE CUTOFF", 0x08u, 1u, 100u, 5, 0, 1u, "V", EW_FF_WARN),
    NUMBER("CURRENT P", 0x09u, 0u, 255u, "", EW_FF_WARN),
    NUMBER("CURRENT I", 0x0Au, 0u, 255u, "", EW_FF_WARN),
    NUMBER("CURRENT D", 0x0Bu, 0u, 255u, "", EW_FF_WARN),
    NUMBER("ACTIVE BRAKE", 0x0Cu, 1u, 5u, "%", 0u),
    { .name = "ZERO THROTTLE BRAKE", .unit = "", .off = 0x0Du,
      .access = EW_ACC_EDIT, .lo = 0u, .hi = 9u, .from = 4u, .to = 4u },
    SWITCH("REVERSED", 0x11u, 0u),
    SWITCH("BIDIRECTIONAL", 0x12u, 0u),
    SWITCH("SINE START", 0x13u, 0u),
    SWITCH("COMPLEMENTARY PWM", 0x14u, 0u),
    { .name = "VARIABLE PWM", .labels = k_vpwm, .unit = "", .off = 0x15u,
      .access = EW_ACC_EDIT, .lo = 0u, .hi = 2u, .from = 3u, .to = 4u },
    SWITCH("STUCK ROTOR", 0x16u, EW_FF_WARN),
    SCALED("TIMING", 0x17u, 10u, 42u, 9375, -93750, 4u, "deg", 0u),
    NUMBER("PWM FREQUENCY", 0x18u, 24u, 48u, "kHz", 0u),
    NUMBER("STARTUP POWER", 0x19u, 50u, 150u, "", 0u),
    SCALED("MOTOR KV", 0x1Au, 0u, 255u, 40, 20, 0u, "", 0u),
    NUMBER("MOTOR POLES", 0x1Bu, 0u, 255u, "", 0u),
    SWITCH("BRAKE ON STOP", 0x1Cu, 0u),
    SWITCH("STALL PROTECTION", 0x1Du, EW_FF_WARN),
    NUMBER("BEEP VOLUME", 0x1Eu, 0u, 11u, "", 0u),
    SWITCH("TELEMETRY INTERVAL", 0x1Fu, 0u),
    SCALED("SERVO LOW", 0x20u, 0u, 255u, 2, 750, 0u, "us", 0u),
    SCALED("SERVO HIGH", 0x21u, 0u, 255u, 2, 1750, 0u, "us", 0u),
    SCALED("SERVO NEUTRAL", 0x22u, 0u, 255u, 1, 1374, 0u, "us", 0u),
    NUMBER("SERVO DEAD BAND", 0x23u, 0u, 100u, "", 0u),
    SWITCH("LOW VOLTAGE CUTOFF", 0x24u, EW_FF_WARN),
    SCALED("CUTOFF PER CELL", 0x25u, 0u, 100u, 1, 250, 2u, "V", EW_FF_WARN),
    SWITCH("CAR REVERSE", 0x26u, 0u),
    SWITCH("HALL SENSORS", 0x27u, 0u),
    NUMBER("SINE CHANGEOVER", 0x28u, 5u, 25u, "%", 0u),
    NUMBER("DRAG BRAKE", 0x29u, 1u, 10u, "", 0u),
    NUMBER("DRIVING BRAKE", 0x2Au, 1u, 9u, "", 0u),
    { .name = "TEMPERATURE LIMIT", .unit = "C", .alt_label = "OFF",
      .off = 0x2Bu, .access = EW_ACC_EDIT, .flags = EW_FF_WARN, .lo = 70u,
      .hi = 140u, .alt = 255u, .has_alt = true, .from = 3u, .to = 4u },
    { .name = "CURRENT LIMIT", .unit = "", .alt_label = "OFF", .off = 0x2Cu,
      .access = EW_ACC_EDIT, .flags = EW_FF_WARN, .lo = 1u, .hi = 100u,
      .alt = 0u, .has_alt = true, .from = 3u, .to = 4u },
    NUMBER("SINE POWER", 0x2Du, 1u, 10u, "", 0u),
    { .name = "INPUT TYPE", .labels = k_input, .unit = "", .off = 0x2Eu,
      .access = EW_ACC_EDIT, .flags = EW_FF_WARN, .lo = 0u, .hi = 4u,
      .from = 3u, .to = 4u },
    SWITCH("AUTO ADVANCE", 0x2Fu, 0u),
};

const size_t ew_map_am_count = sizeof(ew_map_am) / sizeof(ew_map_am[0]);
