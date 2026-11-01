/**
 * @file ew_map_e32.c
 * @brief The keys of ESCape32, revision 17, in the order of the wire's key
 *        numbers, with `music` left out: a text no write changes.  ESCape32 is
 *        a name of a firmware; it is used here only to say which ESCs this
 *        module works with.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ew_map.h"

/** @brief The labels of a switch. */
static const char *const k_off_on[] = { "OFF", "ON" };
/** @brief `duty_lock`. */
static const char *const k_lock[] = { "OFF", "SOFT", "HARD" };
/** @brief `throt_mode`. */
static const char *const k_mode[] = { "FORWARD", "FORWARD REVERSE",
                                      "FORWARD BRAKE REVERSE",
                                      "FORWARD BRAKE" };
/** @brief `throt_rev`. */
static const char *const k_rev[] = { "100%", "75%", "50%", "25%" };
/** @brief `input_mode`. */
static const char *const k_input[] = { "SERVO", "ANALOG", "SERIAL", "IBUS",
                                       "SBUS", "CRSF", "EXBUS", "HOTT" };
/** @brief `telem_mode`. */
static const char *const k_telem[] = { "KISS", "KISS AUTO", "IBUS", "S.PORT",
                                       "CRSF", "MSB", "HOTT" };
/** @brief `prot_sens`. */
static const char *const k_sens[] = { "ESC", "MOTOR", "BOTH" };
/** @brief `bec`. */
static const char *const k_bec[] = { "5.5V", "6.5V", "7.4V", "8.4V", "12V" };

/** @brief A word for each value from @p l. */
#define NAMED(k, g, l, h, lb, fl)                                           \
    { .key = (k), .labels = (lb), .unit = "", .lo = (l), .hi = (h),         \
      .step = 1u, .flags = (fl), .group = (g) }
/** @brief A switch. */
#define SWITCH(k, g, fl) NAMED(k, g, 0, 1, k_off_on, fl)
/** @brief A number with a unit and a step. */
#define NUMBER(k, g, l, h, st, un, fl)                                      \
    { .key = (k), .unit = (un), .lo = (l), .hi = (h), .step = (st),         \
      .flags = (fl), .group = (g) }
/** @brief A number with a name for one more value. */
#define NUMBER_ALT(k, g, l, h, st, un, a, al, fl)                           \
    { .key = (k), .unit = (un), .alt_label = (al), .lo = (l), .hi = (h),    \
      .alt = (a), .step = (st), .has_alt = true, .flags = (fl), .group = (g) }
/** @brief A number not below another key. */
#define NOT_BELOW(k, g, l, h, un, fk)                                       \
    { .key = (k), .unit = (un), .floor = (fk), .lo = (l), .hi = (h),        \
      .step = 1u, .group = (g) }

/** @brief The warning names a protection switched off. */
#define PROT_WARN (EW_FF_WARN | EW_FF_WARN_ZERO)

const ew_key_t ew_map_e32[EW_E32_KEYS] = {
    SWITCH("arm", EW_KG_MOTOR, 0u),
    SWITCH("damp", EW_KG_MOTOR, 0u),
    SWITCH("revdir", EW_KG_MOTOR, 0u),
    SWITCH("brushed", EW_KG_MOTOR, EW_FF_WARN),
    { .key = "timing", .unit = "deg", .mul = 9375, .lo = 1, .hi = 31,
      .step = 1u, .dec = 4u, .group = EW_KG_MOTOR },
    NUMBER_ALT("sine_range", EW_KG_MOTOR, 5, 25, 1u, "%", 0, "OFF", 0u),
    NUMBER("sine_power", EW_KG_MOTOR, 1, 15, 1u, "%", 0u),
    NUMBER("freq_min", EW_KG_MOTOR, 16, 48, 1u, "kHz", 0u),
    NOT_BELOW("freq_max", EW_KG_MOTOR, 16, 96, "kHz", "freq_min"),
    NUMBER("duty_min", EW_KG_MOTOR, 1, 100, 1u, "%", 0u),
    NOT_BELOW("duty_max", EW_KG_MOTOR, 1, 100, "%", "duty_min"),
    NUMBER("duty_spup", EW_KG_MOTOR, 1, 100, 1u, "%", 0u),
    NUMBER("duty_ramp", EW_KG_MOTOR, 0, 100, 1u, "kERPM", 0u),
    { .key = "duty_rate", .unit = "%/ms", .lo = 1, .hi = 100, .step = 1u,
      .dec = 1u, .group = EW_KG_MOTOR },
    NUMBER("duty_drag", EW_KG_MOTOR, 0, 100, 1u, "%", 0u),
    NAMED("duty_lock", EW_KG_MOTOR, 0, 2, k_lock, 0u),
    NAMED("throt_mode", EW_KG_THROTTLE, 0, 3, k_mode, 0u),
    NAMED("throt_rev", EW_KG_THROTTLE, 0, 3, k_rev, 0u),
    NUMBER("throt_brk", EW_KG_THROTTLE, 0, 100, 1u, "%", 0u),
    NUMBER("throt_set", EW_KG_THROTTLE, 0, 100, 1u, "%", 0u),
    SWITCH("throt_ztc", EW_KG_THROTTLE, 0u),
    SWITCH("throt_cal", EW_KG_THROTTLE, 0u),
    NUMBER("throt_min", EW_KG_THROTTLE, 900, 1900, 10u, "us", 0u),
    NUMBER("throt_mid", EW_KG_THROTTLE, 1000, 2000, 10u, "us", 0u),
    NUMBER("throt_max", EW_KG_THROTTLE, 1100, 2100, 10u, "us", 0u),
    NUMBER("analog_min", EW_KG_THROTTLE, 0, 3200, 10u, "mV", 0u),
    NUMBER("analog_max", EW_KG_THROTTLE, 200, 3400, 10u, "mV", 0u),
    NAMED("input_mode", EW_KG_THROTTLE, 0, 7, k_input, EW_FF_WARN),
    NUMBER("input_ch1", EW_KG_THROTTLE, 0, 32, 1u, "", 0u),
    NUMBER("input_ch2", EW_KG_THROTTLE, 0, 32, 1u, "", 0u),
    NAMED("telem_mode", EW_KG_TELEMETRY, 0, 6, k_telem, 0u),
    NUMBER("telem_phid", EW_KG_TELEMETRY, 0, 28, 1u, "", 0u),
    NUMBER("telem_poles", EW_KG_TELEMETRY, 2, 100, 1u, "", 0u),
    NUMBER("telem_volt", EW_KG_TELEMETRY, -80, 160, 1u, "", 0u),
    NUMBER("telem_curr", EW_KG_TELEMETRY, -100, 200, 1u, "", 0u),
    NUMBER_ALT("prot_stall", EW_KG_PROTECT, 10, 3500, 10u, "ERPM", 0, "OFF",
               PROT_WARN),
    NUMBER_ALT("prot_temp", EW_KG_PROTECT, 5, 140, 5u, "C", 0, "OFF",
               PROT_WARN),
    NAMED("prot_sens", EW_KG_PROTECT, 0, 2, k_sens, PROT_WARN),
    { .key = "prot_volt", .unit = "V/cell", .alt_label = "OFF", .lo = 28,
      .hi = 38, .alt = 0, .step = 1u, .has_alt = true, .dec = 1u,
      .flags = PROT_WARN, .group = EW_KG_PROTECT },
    NUMBER_ALT("prot_cells", EW_KG_PROTECT, 1, 24, 1u, "", 0, "AUTO",
               PROT_WARN),
    NUMBER_ALT("prot_curr", EW_KG_PROTECT, 1, 999, 1u, "A", 0, "OFF",
               PROT_WARN),
    NUMBER("prot_park", EW_KG_PROTECT, 0, 4, 1u, "", PROT_WARN),
    NUMBER("volume", EW_KG_SOUND, 0, 100, 1u, "%", 0u),
    NUMBER("beacon", EW_KG_SOUND, 0, 100, 1u, "%", 0u),
    NAMED("bec", EW_KG_MOTOR, 0, 4, k_bec, EW_FF_WARN),
    NUMBER("led", EW_KG_SOUND, 0, 15, 1u, "", 0u),
};
