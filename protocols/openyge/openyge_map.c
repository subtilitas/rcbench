/**
 * @file openyge_map.c
 * @brief The field table of docs/OpenYGE.md section 5.3: one entry per
 *        index, its access and its writable values; the ESC type names.
 *
 * SPDX-License-Identifier: MIT
 */
#include "openyge_map.h"

#include <stddef.h>

/** @brief Bit @p v of a choice set. */
#define BIT(v) ((uint32_t)1u << (v))
/** @brief The set of every value from @p a to @p b, both below 32. */
#define SPAN(a, b) ((BIT((b) + 1u) - 1u) & ~(BIT(a) - 1u))

/** @brief Index 1, docs/OpenYGE.md 5.3. */
static const char *const MODE_NAMES[] = {
    "FREE", "HELI EXT GOV", "HELI GOV", "HELI GOV STORE",
    "GLIDER BRAKE", "AIRPLANE", "AIRPLANE F3A",
};
/** @brief Index 3: 0 and 16 are one setting; 7 to 15 have no meaning. */
static const char *const TIMING_NAMES[] = {
    "AUTO NORMAL", "0°", "6°", "12°", "18°", "24°", "30°",
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    "AUTO NORMAL", "AUTO EFFICIENT", "AUTO POWER", "AUTO EXTREME",
};
/** @brief Index 7. */
static const char *const RESPONSE_NAMES[] = {
    "SLOW", "MEDIUM", "FAST", "CUSTOM",
};
/** @brief Index 8. */
static const char *const CUTOFF_NAMES[] = {
    "OFF", "SLOW DOWN", "CUT OFF",
};
/** @brief Index 9: per LiPo cell. */
static const char *const CUTOFF_V_NAMES[] = {
    "2.9V", "3.0V", "3.1V", "3.2V", "3.3V", "3.4V",
};
/** @brief Index 10: 2 is unused. */
static const char *const FREEWHEEL_NAMES[] = {
    "OFF", "AUTO", NULL, "ALWAYS",
};

/** @brief Number of entries of a name array. */
#define N(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

/** @brief A read-only field. */
#define RO(k, acc, kind_) { .key = (k), .access = (acc), .kind = (kind_), \
                            .unit = "" }
/** @brief A raw field: a number, read-only, written back as read. */
#define RAW(k) RO(k, OY_ACC_RAW, OY_KIND_NUMBER)

/** @brief Index i is entry i. */
static const oy_field_t FIELDS[OY_MAP_FIELDS] = {
    RO("PARAMETER COUNT", OY_ACC_IDENTITY, OY_KIND_NUMBER),
    { .key = "ESC MODE", .access = OY_ACC_CHECK, .kind = OY_KIND_CHOICE,
      .unit = "", .lo = 0, .hi = 6, .set = SPAN(0u, 6u),
      .names = MODE_NAMES, .n_names = N(MODE_NAMES) },
    { .key = "BEC VOLTAGE", .access = OY_ACC_CHECK, .kind = OY_KIND_NUMBER,
      .decimals = 1, .unit = "V", .lo = 55, .hi = 84, .hi_flag = 120 },
    { .key = "MOTOR TIMING", .access = OY_ACC_EDIT, .kind = OY_KIND_CHOICE,
      .unit = "", .lo = 0, .hi = 19,
      .set = SPAN(0u, 6u) | SPAN(17u, 19u),
      .names = TIMING_NAMES, .n_names = N(TIMING_NAMES) },
    RAW("STARTUP RESPONSE"),
    { .key = "GOV P GAIN", .access = OY_ACC_EDIT, .kind = OY_KIND_NUMBER,
      .unit = "", .lo = 1, .hi = 9 },
    { .key = "GOV I GAIN", .access = OY_ACC_EDIT, .kind = OY_KIND_NUMBER,
      .unit = "", .lo = 1, .hi = 9 },
    { .key = "THROTTLE RESPONSE", .access = OY_ACC_EDIT,
      .kind = OY_KIND_CHOICE, .unit = "", .lo = 0, .hi = 3,
      .set = SPAN(0u, 3u),
      .names = RESPONSE_NAMES, .n_names = N(RESPONSE_NAMES) },
    { .key = "CUT-OFF HANDLING", .access = OY_ACC_CHECK,
      .kind = OY_KIND_CHOICE, .unit = "", .lo = 0, .hi = 2,
      .set = SPAN(0u, 2u),
      .names = CUTOFF_NAMES, .n_names = N(CUTOFF_NAMES) },
    { .key = "CUT-OFF VOLTAGE", .access = OY_ACC_EDIT,
      .kind = OY_KIND_CHOICE, .unit = "", .lo = 0, .hi = 5,
      .set = SPAN(0u, 5u),
      .names = CUTOFF_V_NAMES, .n_names = N(CUTOFF_V_NAMES) },
    { .key = "ACTIVE FREEWHEEL", .access = OY_ACC_EDIT,
      .kind = OY_KIND_CHOICE, .unit = "", .lo = 0, .hi = 3,
      .set = BIT(0u) | BIT(1u) | BIT(3u),
      .names = FREEWHEEL_NAMES, .n_names = N(FREEWHEEL_NAMES) },
    RO("ESC TYPE", OY_ACC_IDENTITY, OY_KIND_NUMBER),
    RO("FIRMWARE", OY_ACC_IDENTITY, OY_KIND_U32_LO),
    RO("FIRMWARE", OY_ACC_IDENTITY, OY_KIND_U32_HI),
    RO("SERIAL", OY_ACC_IDENTITY, OY_KIND_U32_LO),
    RO("SERIAL", OY_ACC_IDENTITY, OY_KIND_U32_HI),
    RAW("MAH ALARM"),
    { .key = "STICK ZERO", .access = OY_ACC_EDIT, .kind = OY_KIND_NUMBER,
      .unit = "µs", .lo = 900, .hi = 1900 },
    { .key = "STICK RANGE", .access = OY_ACC_EDIT, .kind = OY_KIND_NUMBER,
      .unit = "µs", .lo = 600, .hi = 1500 },
    RAW("STICK PERIOD"),
    RAW("POLE PAIRS"),
    { .key = "PINION TEETH", .access = OY_ACC_EDIT, .kind = OY_KIND_NUMBER,
      .unit = "", .lo = 1, .hi = 255 },
    { .key = "MAIN GEAR TEETH", .access = OY_ACC_EDIT,
      .kind = OY_KIND_NUMBER, .unit = "", .lo = 0, .hi = 1800 },
    { .key = "START POWER MIN", .access = OY_ACC_EDIT,
      .kind = OY_KIND_NUMBER, .unit = "%", .lo = 0, .hi = 26 },
    { .key = "START POWER MAX", .access = OY_ACC_EDIT,
      .kind = OY_KIND_NUMBER, .unit = "%", .lo = 0, .hi = 31 },
    RO("TELEMETRY TYPE", OY_ACC_TELEMETRY, OY_KIND_NUMBER),
    /* Bit 0 direction (CHECK), bit 1 F3C autorotation (edit); bit 2 keep
     * mAh, bit 3 BEC up to 12V and the rest are raw: every other bit is
     * written back as read. */
    { .key = "FLAGS", .access = OY_ACC_CHECK, .kind = OY_KIND_FLAGS,
      .unit = "", .lo = 0, .hi = 0xFFFFu, .edit_bits = 0x0003u,
      .check_bits = 0x0001u },
    { .key = "CURRENT LIMIT", .access = OY_ACC_CHECK,
      .kind = OY_KIND_NUMBER, .decimals = 2, .unit = "A",
      .lo = 1, .hi = 65500 },
    RAW("SOFT START"),
    RAW("SOFT RUN"),
    RAW("SOFT BLEND"),
    RAW("RPM SETPOINT"),
};

const oy_field_t *oy_map_field(uint16_t index)
{
    return index < OY_MAP_FIELDS ? &FIELDS[index] : NULL;
}

bool oy_map_writable(uint16_t index)
{
    const oy_field_t *f = oy_map_field(index);

    return f != NULL
           && (f->access == OY_ACC_CHECK || f->access == OY_ACC_EDIT);
}

bool oy_map_value_ok(uint16_t index, uint16_t value, uint16_t was,
                     uint16_t flags)
{
    if (!oy_map_writable(index)) {
        return false;
    }
    const oy_field_t *f = &FIELDS[index];

    if (f->kind == OY_KIND_FLAGS) {
        return ((uint16_t)(value ^ was) & (uint16_t)~f->edit_bits) == 0u;
    }
    uint16_t hi = f->hi;
    if (f->hi_flag != 0u && (flags & OY_FLAG_BEC12V) != 0u) {
        hi = f->hi_flag;
    }
    if (value < f->lo || value > hi) {
        return false;
    }
    return f->set == 0u || (value < 32u && (f->set & BIT(value)) != 0u);
}

const char *oy_map_choice_name(uint16_t index, uint16_t value)
{
    const oy_field_t *f = oy_map_field(index);

    if (f == NULL || f->names == NULL || value >= f->n_names) {
        return NULL;
    }
    return f->names[value];
}

/** @brief One ESC type code and its model (docs/OpenYGE.md 5.3). */
typedef struct {
    uint16_t    code; /**< index 11 */
    const char *name; /**< the model */
} oy_type_t;

/** @brief The 20 codes of docs/OpenYGE.md, "ESC type codes, index 11". */
static const oy_type_t TYPES[] = {
    { 848, "35 LVT BEC" },    { 1616, "65 LVT BEC" },
    { 2128, "85 LVT BEC" },   { 2384, "95 LVT BEC" },
    { 4944, "135 LVT BEC" },  { 2304, "90 HVT Opto" },
    { 4608, "120 HVT Opto" }, { 5712, "165 HVT" },
    { 8272, "205 HVT" },      { 8273, "205 HVT BEC" },
    { 4177, "Aureus 105" },   { 4179, "Aureus 105 v2" },
    { 5025, "Aureus 135" },   { 5027, "Aureus 135 v2" },
    { 4689, "Saphir 125" },   { 5457, "Saphir 155" },
    { 5459, "Saphir 155 v2" }, { 4928, "Opto 135" },
    { 9552, "Opto 255" },     { 16464, "Opto 405" },
};

const char *oy_map_type_name(uint16_t code)
{
    for (size_t i = 0; i < sizeof(TYPES) / sizeof(TYPES[0]); ++i) {
        if (TYPES[i].code == code) {
            return TYPES[i].name;
        }
    }
    return NULL;
}

uint32_t oy_map_u32(uint16_t lo, uint16_t hi)
{
    return (uint32_t)lo | ((uint32_t)hi << 16);
}
