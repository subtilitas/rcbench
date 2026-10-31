/**
 * @file ew_map_bj.c
 * @brief The fields of Bluejay.  Bluejay is a name of a firmware; it is used
 *        here only to say which ESCs this module works with.
 *
 * Only layout 208 is written.  Layouts 33 and 200 to 207 are shown from this
 * table: each row names the layouts that have it.  Layout 33 has the offsets
 * of BLHeli_S layout 33, without its stick programming and throttle bytes.
 * Layouts 202 and 205 were not released, and identification refuses them.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_map.h"

/** @brief The labels of a switch. */
static const char *const k_off_on[] = { "OFF", "ON" };
/** @brief Startup power on the BLHeli_S scale, 1 to 13. */
static const char *const k_power[] = {
    "0.031", "0.047", "0.063", "0.094", "0.125", "0.188", "0.25",
    "0.38", "0.50", "0.75", "1.00", "1.25", "1.50",
};
/** @brief The startup sound, 0 and 1. */
static const char *const k_sound[] = { "SHORT BEEP", "MELODY" };
/** @brief Motor direction, 1 to 4. */
static const char *const k_dir[] = {
    "NORMAL", "REVERSED", "BIDIRECTIONAL", "BIDIRECTIONAL REVERSED",
};
/** @brief Commutation timing, 1 to 5. */
static const char *const k_timing[] = {
    "LOW", "MEDIUM LOW", "MEDIUM", "MEDIUM HIGH", "HIGH",
};
/** @brief Beacon delay, 1 to 5. */
static const char *const k_delay[] = { "1min", "2min", "5min", "10min",
                                       "INFINITE" };
/** @brief Demagnetisation compensation, 1 to 3. */
static const char *const k_demag[] = { "OFF", "LOW", "HIGH" };
/** @brief Temperature limit, 0 to 7. */
static const char *const k_temp[] = { "OFF", "80C", "90C", "100C", "110C",
                                      "120C", "130C", "140C" };
/** @brief Power rating, 1 and 2. */
static const char *const k_rating[] = { "1S", "2S AND ABOVE" };

/** @brief An editable field with named values, in layouts @p f to @p t. */
#define NAMED(nm, o, l, h, lab, fl, f, t)                                   \
    { .name = (nm), .labels = (lab), .unit = "", .off = (o),                \
      .access = EW_ACC_EDIT, .flags = (fl), .lo = (l), .hi = (h),           \
      .from = (f), .to = (t) }
/** @brief An editable field, a plain number. */
#define NUMBER(nm, o, l, h, f, t)                                           \
    { .name = (nm), .unit = "", .off = (o), .access = EW_ACC_EDIT,          \
      .lo = (l), .hi = (h), .from = (f), .to = (t) }
/** @brief A byte that is shown and kept as read. */
#define SHOWN(nm, o, f, t)                                                  \
    { .name = (nm), .unit = "", .off = (o), .access = EW_ACC_SHOW,          \
      .lo = 0u, .hi = 255u, .from = (f), .to = (t) }

const ew_field_t ew_map_bj[] = {
    SHOWN("MAJOR", 0x00u, 208u, 208u),
    SHOWN("MINOR", 0x01u, 208u, 208u),
    SHOWN("LAYOUT", 0x02u, 33u, 208u),
    NUMBER("STARTUP BOOST", 0x04u, 0u, 255u, 200u, 200u),
    NUMBER("MIN STARTUP POWER", 0x04u, 0u, 255u, 201u, 208u),
    NAMED("STARTUP SOUND", 0x05u, 0u, 1u, k_sound, 0u, 200u, 208u),
    NAMED("DITHERING", 0x06u, 0u, 1u, k_off_on, 0u, 200u, 207u),
    NUMBER("MAX STARTUP POWER", 0x07u, 0u, 255u, 201u, 208u),
    NAMED("STARTUP POWER", 0x09u, 1u, 13u, k_power, 0u, 33u, 200u),
    NUMBER("POWER SLOPE", 0x09u, 0u, 13u, 201u, 208u),
    SHOWN("PWM INDICATOR", 0x0Au, 203u, 208u),
    NAMED("DIRECTION", 0x0Bu, 1u, 4u, k_dir, 0u, 33u, 208u),
    { .name = "BRAKING", .unit = "%", .mul = 100, .div = 255u, .off = 0x10u,
      .access = EW_ACC_EDIT, .lo = 0u, .hi = 255u, .from = 204u, .to = 208u },
    NAMED("TIMING", 0x15u, 1u, 5u, k_timing, 0u, 33u, 208u),
    NUMBER("BEEP STRENGTH", 0x1Bu, 0u, 255u, 33u, 208u),
    NUMBER("BEACON STRENGTH", 0x1Cu, 0u, 255u, 33u, 208u),
    NAMED("BEACON DELAY", 0x1Du, 1u, 5u, k_delay, 0u, 33u, 208u),
    NAMED("DEMAG", 0x1Fu, 1u, 3u, k_demag, 0u, 33u, 208u),
    NAMED("TEMPERATURE", 0x23u, 0u, 7u, k_temp, EW_FF_WARN, 33u, 208u),
    NAMED("LOW-SPEED PROTECTION", 0x24u, 0u, 1u, k_off_on, EW_FF_WARN,
          33u, 200u),
    NAMED("BRAKE ON STOP", 0x27u, 0u, 1u, k_off_on, 0u, 33u, 208u),
    NUMBER("LED", 0x28u, 0u, 255u, 33u, 208u),
    NAMED("POWER RATING", 0x29u, 1u, 2u, k_rating, EW_FF_WARN, 206u, 208u),
    SHOWN("SAFETY ARM", 0x2Au, 207u, 208u),
};

const size_t ew_map_bj_count = sizeof(ew_map_bj) / sizeof(ew_map_bj[0]);
