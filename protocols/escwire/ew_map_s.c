/**
 * @file ew_map_s.c
 * @brief The fields of BLHeli_S, layouts 32 and 33.  BLHeli_S is a name of a
 *        firmware; it is used here only to say which ESCs this module works
 *        with.
 *
 * Layout 32 has every offset of 33; offset 0x23 is a switch there.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_map.h"

/** @brief The labels of a switch. */
static const char *const k_off_on[] = { "OFF", "ON" };
/** @brief Startup power, 1 to 13. */
static const char *const k_power[] = {
    "0.031", "0.047", "0.063", "0.094", "0.125", "0.188", "0.25",
    "0.38", "0.50", "0.75", "1.00", "1.25", "1.50",
};
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

/** @brief An editable field with named values in layouts 32 and 33. */
#define NAMED(nm, o, l, h, lab, fl)                                         \
    { .name = (nm), .labels = (lab), .unit = "", .off = (o),                \
      .access = EW_ACC_EDIT, .flags = (fl), .lo = (l), .hi = (h),           \
      .from = 32u, .to = 33u }
/** @brief A throttle byte: 4 x value + 1000us, its range not known, so shown
 *         and not edited. */
#define THROTTLE(nm, o)                                                     \
    { .name = (nm), .unit = "us", .mul = 4, .add = 1000, .off = (o),        \
      .access = EW_ACC_SHOW, .lo = 0u, .hi = 255u, .from = 32u, .to = 33u }
/** @brief An editable field, a plain number. */
#define NUMBER(nm, o, l, h)                                                 \
    { .name = (nm), .unit = "", .off = (o), .access = EW_ACC_EDIT,          \
      .lo = (l), .hi = (h), .from = 32u, .to = 33u }
/** @brief A tag, shown. */
#define TAG(nm, o)                                                          \
    { .name = (nm), .unit = "", .off = (o), .access = EW_ACC_SHOW,          \
      .lo = 0u, .hi = 255u, .from = 32u, .to = 33u }

const ew_field_t ew_map_s[] = {
    TAG("MAIN REVISION", 0x00u),
    TAG("SUB REVISION", 0x01u),
    TAG("LAYOUT", 0x02u),
    NAMED("STARTUP POWER", 0x09u, 1u, 13u, k_power, 0u),
    NAMED("DIRECTION", 0x0Bu, 1u, 4u, k_dir, 0u),
    NAMED("STICK PROGRAMMING", 0x0Fu, 0u, 1u, k_off_on, 0u),
    NAMED("TIMING", 0x15u, 1u, 5u, k_timing, 0u),
    THROTTLE("MIN THROTTLE", 0x19u),
    THROTTLE("MAX THROTTLE", 0x1Au),
    NUMBER("BEEP STRENGTH", 0x1Bu, 1u, 255u),
    NUMBER("BEACON STRENGTH", 0x1Cu, 1u, 255u),
    NAMED("BEACON DELAY", 0x1Du, 1u, 5u, k_delay, 0u),
    NAMED("DEMAG", 0x1Fu, 1u, 3u, k_demag, 0u),
    THROTTLE("CENTRE THROTTLE", 0x21u),
    { .name = "TEMPERATURE", .labels = k_off_on, .unit = "", .off = 0x23u,
      .access = EW_ACC_EDIT, .flags = EW_FF_WARN, .lo = 0u, .hi = 1u,
      .from = 32u, .to = 32u },
    { .name = "TEMPERATURE", .labels = k_temp, .unit = "", .off = 0x23u,
      .access = EW_ACC_EDIT, .flags = EW_FF_WARN, .lo = 0u, .hi = 7u,
      .from = 33u, .to = 33u },
    NAMED("LOW-SPEED PROTECTION", 0x24u, 0u, 1u, k_off_on, EW_FF_WARN),
    NAMED("BRAKE ON STOP", 0x27u, 0u, 1u, k_off_on, 0u),
    NUMBER("LED", 0x28u, 0u, 255u),
};

const size_t ew_map_s_count = sizeof(ew_map_s) / sizeof(ew_map_s[0]);
