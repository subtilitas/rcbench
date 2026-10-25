/**
 * @file
 * @brief The register models of Hitec servos: images, the parameter table,
 *        encodings, model identification and write plans.
 *
 * Sources of the tables, by mark of hitec_src_t:
 *
 *   - RCBENCH: the D485HW through the IOmcu, 2026-10-19 and 2026-10-20: a
 *     read of every even register, the ID, smart sensor on and off.
 *   - CAPTURED: the wire between a vendor programmer and a servo,
 *     2026-10-18: the HFP-30 in D mode with a D485HW, every setting it
 *     offers; the HPP-21 Plus with an HS-5645MG and an HS-7955TG, every
 *     setting it offers.
 *   - READ: the IOmcu's reads of all 256 cells of an HS-5645MG, an
 *     HS-7955TG and an HS-5955TG, 2026-10-20.
 *   - DOC: Hitec's D series protocol manual rev 1.06_EN, a community
 *     description of the HS-5xxx (2006), the HitecDServo library's
 *     observations of a DPC-11.
 *
 * The D series dead band steps 1 and 5 are captured from the HFP-30; steps
 * 2 to 4 (4, 8, 12) are the DPC-11's on a D485HW as HitecDServo reports
 * them, and equal the HPP-21 Plus's classic steps.
 *
 * SPDX-License-Identifier: MIT
 */

#include "hitec_reg.h"

#include <string.h>

/** @brief Encodings of the rows. */
enum {
    ENC_RAW = 0,     /**< the stored value */
    ENC_D_ID,        /**< 0 to 255 */
    ENC_D_POS,       /**< 0 to 16383 */
    ENC_D_DIR,       /**< 0 CW, 1 CCW */
    ENC_STEP5,       /**< dead band steps 1 to 5: 1, 4, 8, 12, 16 */
    ENC_D_SPEED,     /**< 10% to 100%: count x 5; 4095 shows 100% */
    ENC_D_FS,        /**< 0 limp, 1 off, 2us to 5000us */
    ENC_D_SOFT,      /**< 20% 1, 40% 3, 60% 6, 80% 8, 100% 100 */
    ENC_OVERLOAD,    /**< off 100, 10% to 50% as the percent */
    ENC_D_SMART,     /**< on: 0x6C = 0x44 = 0; off: copies of 0x8A, 0x8C */
    ENC_C_VERSION,   /**< the reply to 0x67 */
    ENC_C_DATE,      /**< day, month, year high first */
    ENC_C_SERIAL,    /**< 7 ASCII digits */
    ENC_C_SUM,       /**< cells 0x00 to 0x1C sum to 0 */
    ENC_C_SPEED,     /**< 10% to 100%: 01, 07, 0E ... 38, 40 */
    ENC_C_CENTRE5,   /**< 0.25us: stored + 2052 */
    ENC_C_CENTRE7,   /**< 0.25us: stored + 2048 */
    ENC_C_END,       /**< addr on a CW servo, addr2 on a CCW one */
    ENC_C_DIR,       /**< 0x1B bit 0, 1 CW */
    ENC_C_FS,        /**< 0x1B bit 1 */
    ENC_C_FSPOS5,    /**< 0.25us: stored + 4; sets 0x1B bit 1 */
    ENC_C_OVL5,      /**< overload in addr, 0xFF in addr2 */
    ENC_C_OVL7,      /**< overload in addr, 0x00 in addr2 */
    ENC_C_RES,       /**< 00 Normal, 01 High, 0B High */
};

/** @brief A decoded value and the raw value it is stored as. */
typedef struct {
    int16_t  shown;  /**< decoded value */
    uint16_t raw;    /**< stored value */
} pair_t;

/** @brief Dead band steps, HPP-21 Plus and D series. */
static const pair_t k_step5[] = {
    { 1, 1 }, { 2, 4 }, { 3, 8 }, { 4, 12 }, { 5, 16 },
};
/** @brief HFP-30 speed: 5% a count; the factory 4095 shows as 100%. */
static const pair_t k_d_speed[] = {
    { 10, 2 }, { 20, 4 }, { 30, 6 }, { 40, 8 }, { 50, 10 }, { 60, 12 },
    { 70, 14 }, { 80, 16 }, { 90, 18 }, { 100, 20 }, { 100, 4095 },
};
/** @brief HFP-30 soft start. */
static const pair_t k_d_soft[] = {
    { 20, 1 }, { 40, 3 }, { 60, 6 }, { 80, 8 }, { 100, 100 },
};
/** @brief Overload protection, HFP-30 and HPP-21 Plus alike. */
static const pair_t k_overload[] = {
    { 0, 100 }, { 10, 10 }, { 20, 20 }, { 30, 30 }, { 40, 40 }, { 50, 50 },
};
/** @brief HPP-21 Plus speed. */
static const pair_t k_c_speed[] = {
    { 10, 0x01 }, { 20, 0x07 }, { 30, 0x0E }, { 40, 0x15 }, { 50, 0x1C },
    { 60, 0x23 }, { 70, 0x2A }, { 80, 0x31 }, { 90, 0x38 }, { 100, 0x40 },
};
/** @brief HPP-21 Plus resolution; the factory 0x0B shows as High. */
static const pair_t k_c_res[] = {
    { HITEC_RES_NORMAL, 0x00 }, { HITEC_RES_HIGH, 0x01 },
    { HITEC_RES_HIGH, 0x0B },
};
/** @brief A D series direction. */
static const pair_t k_d_dir[] = {
    { HITEC_CW, 0 }, { HITEC_CCW, 1 },
};

/** @brief The pair table of an encoding and its length. */
typedef struct {
    const pair_t *p;  /**< pairs */
    size_t n;         /**< count */
} pairs_t;

/** @brief Row of the table: one layout. */
#define ROW(id, lay, addr, addr2, size, enc, unit, acc, src, step, min, max) \
    { (uint8_t)(id), (uint8_t)(lay), addr, addr2, size, enc, unit, acc, src, \
      step, min, max }
/** @brief A read-only D series register shown raw. */
#define D_RAW(id, addr, src) \
    ROW(id, LD, addr, NO, 2, ENC_RAW, HITEC_UNIT_RAW, HITEC_RO, src, 0, 0, 0)

#define LD  HITEC_LAYOUT_BIT(HITEC_LAYOUT_D)    /**< D series */
#define L5  HITEC_LAYOUT_BIT(HITEC_LAYOUT_HS5)  /**< HS-5xxx */
#define L7  HITEC_LAYOUT_BIT(HITEC_LAYOUT_HS7)  /**< HS-7xxx */
#define LC  ((uint8_t)(L5 | L7))                /**< both classic layouts */
#define NO  HITEC_ADDR_NONE                     /**< no second address */
#define RO  HITEC_RO                            /**< read only */
#define RW  HITEC_RW                            /**< writable */
#define SR  HITEC_SRC_RCBENCH                   /**< rcbench write */
#define SC  HITEC_SRC_CAPTURED                  /**< captured write */
#define SD  HITEC_SRC_READ                      /**< read only on a servo */

/** @brief The parameter table. */
static const hitec_param_info_t k_rows[] = {
    D_RAW(HITEC_P_D_MODEL, 0x00, SD),
    ROW(HITEC_P_D_POSITION, LD, 0x0C, NO, 2, ENC_D_POS, HITEC_UNIT_COUNTS,
        RO, SD, 0, 0, 0),
    ROW(HITEC_P_D_ID, LD, 0x32, NO, 2, ENC_D_ID, HITEC_UNIT_RAW, RW, SR,
        1, 0, 255),
    ROW(HITEC_P_D_DIRECTION, LD, 0x5E, NO, 2, ENC_D_DIR, HITEC_UNIT_CHOICE,
        RW, SC, 1, HITEC_CW, HITEC_CCW),
    ROW(HITEC_P_D_NEUTRAL, LD, 0xC2, NO, 2, ENC_D_POS, HITEC_UNIT_COUNTS,
        RW, SC, 1, 1, (int32_t)HITEC_D_POS_MAX),
    ROW(HITEC_P_D_LEFT_END, LD, 0xB2, NO, 2, ENC_D_POS, HITEC_UNIT_COUNTS,
        RW, SC, 1, 1, (int32_t)HITEC_D_POS_MAX),
    ROW(HITEC_P_D_RIGHT_END, LD, 0xB0, NO, 2, ENC_D_POS, HITEC_UNIT_COUNTS,
        RW, SC, 1, 1, (int32_t)HITEC_D_POS_MAX),
    ROW(HITEC_P_D_DEAD_BAND, LD, 0x4E, NO, 2, ENC_STEP5, HITEC_UNIT_STEP,
        RW, SC, 1, 1, 5),
    ROW(HITEC_P_D_SPEED, LD, 0x54, NO, 2, ENC_D_SPEED, HITEC_UNIT_PERCENT,
        RW, SC, 10, 10, 100),
    ROW(HITEC_P_D_FAILSAFE, LD, 0x4C, NO, 2, ENC_D_FS, HITEC_UNIT_FS_US,
        RW, SC, 1, 900, 2100),
    ROW(HITEC_P_D_SOFT_START, LD, 0x60, NO, 2, ENC_D_SOFT,
        HITEC_UNIT_PERCENT, RW, SC, 20, 20, 100),
    ROW(HITEC_P_D_OVERLOAD, LD, 0x9C, NO, 2, ENC_OVERLOAD,
        HITEC_UNIT_OVERLOAD, RW, SC, 10, 0, 50),
    ROW(HITEC_P_D_SMART_SENSOR, LD, 0x6C, 0x44, 2, ENC_D_SMART,
        HITEC_UNIT_CHOICE, RW, SR, 1, HITEC_OFF, HITEC_ON),
    D_RAW(HITEC_P_D_SENSITIVITY, 0x64, SD),
    D_RAW(HITEC_P_D_R04, 0x04, SD),
    D_RAW(HITEC_P_D_R22, 0x22, SD),
    D_RAW(HITEC_P_D_R8A, 0x8A, SD),
    D_RAW(HITEC_P_D_R8C, 0x8C, SD),
    D_RAW(HITEC_P_D_RD4, 0xD4, SD),
    D_RAW(HITEC_P_D_RD6, 0xD6, SD),
    ROW(HITEC_P_C_VERSION, LC, NO, NO, 0, ENC_C_VERSION, HITEC_UNIT_RAW,
        RO, SD, 0, 0, 0),
    ROW(HITEC_P_C_DATE, LC, HITEC_C_DATE, NO, 4, ENC_C_DATE,
        HITEC_UNIT_DATE, RO, SD, 0, 0, 0),
    ROW(HITEC_P_C_SERIAL, LC, HITEC_C_SERIAL, NO, HITEC_C_SERIAL_LEN,
        ENC_C_SERIAL, HITEC_UNIT_NUMBER, RO, SD, 0, 0, 0),
    ROW(HITEC_P_C_CHECKSUM, LC, 0x00, NO, HITEC_C_SUM_LAST + 1u, ENC_C_SUM,
        HITEC_UNIT_CHOICE, RO, SC, 0, 0, 0),
    ROW(HITEC_P_C_DEAD_BAND, LC, 0x02, NO, 1, ENC_STEP5, HITEC_UNIT_STEP,
        RW, SC, 1, 1, 5),
    ROW(HITEC_P_C_SPEED, LC, 0x06, NO, 1, ENC_C_SPEED, HITEC_UNIT_PERCENT,
        RW, SC, 10, 10, 100),
    ROW(HITEC_P_C_CENTRE, L5, 0x07, NO, 2, ENC_C_CENTRE5,
        HITEC_UNIT_QUARTER_US, RW, SC, 1, 3600, 8400),
    ROW(HITEC_P_C_CENTRE, L7, 0x07, NO, 2, ENC_C_CENTRE7,
        HITEC_UNIT_QUARTER_US, RW, SC, 1, 3600, 8400),
    ROW(HITEC_P_C_LEFT_END, LC, 0x14, 0x13, 1, ENC_C_END, HITEC_UNIT_RAW,
        RW, SC, 1, 0, 255),
    ROW(HITEC_P_C_RIGHT_END, LC, 0x13, 0x14, 1, ENC_C_END, HITEC_UNIT_RAW,
        RW, SC, 1, 0, 255),
    ROW(HITEC_P_C_DIRECTION, LC, 0x1B, NO, 1, ENC_C_DIR, HITEC_UNIT_CHOICE,
        RW, SC, 1, HITEC_CW, HITEC_CCW),
    ROW(HITEC_P_C_FAILSAFE, LC, 0x1B, NO, 1, ENC_C_FS, HITEC_UNIT_CHOICE,
        RW, SC, 1, HITEC_OFF, HITEC_ON),
    ROW(HITEC_P_C_FS_POSITION, L5, 0x19, NO, 2, ENC_C_FSPOS5,
        HITEC_UNIT_QUARTER_US, RW, SC, 1, 3600, 8400),
    ROW(HITEC_P_C_FS_POSITION, L7, 0x19, NO, 2, ENC_RAW, HITEC_UNIT_RAW,
        RO, SD, 0, 0, 0),
    ROW(HITEC_P_C_OVERLOAD, L5, 0x20, 0x21, 1, ENC_C_OVL5,
        HITEC_UNIT_OVERLOAD, RW, SC, 10, 0, 50),
    ROW(HITEC_P_C_OVERLOAD, L7, 0x21, 0x20, 1, ENC_C_OVL7,
        HITEC_UNIT_OVERLOAD, RW, SC, 10, 0, 50),
    ROW(HITEC_P_C_RESOLUTION, L7, 0x22, NO, 1, ENC_C_RES, HITEC_UNIT_CHOICE,
        RO, SC, 0, 0, 0),
};
/** @brief Rows in k_rows. */
#define ROW_COUNT (sizeof(k_rows) / sizeof(k_rows[0]))

/** @brief One known model. */
typedef struct {
    const char *name;   /**< Hitec's name; "" for the OTHER rows */
    const char *cells;  /**< classic: the name cells; NULL for D series */
    uint16_t number;    /**< D series: register 0x00 */
    uint8_t layout;     /**< hitec_layout_t */
    uint8_t src;        /**< hitec_src_t */
} model_row_t;

/** @brief In the order of hitec_model_id_t. */
static const model_row_t k_models[HITEC_MODEL_COUNT] = {
    { "", NULL, 0, HITEC_LAYOUT_D, HITEC_SRC_DOC },
    { "D485HW", NULL, 34485u, HITEC_LAYOUT_D, HITEC_SRC_RCBENCH },
    { "D645MW", NULL, 34645u, HITEC_LAYOUT_D, HITEC_SRC_DOC },
    { "", NULL, 0, HITEC_LAYOUT_D, HITEC_SRC_DOC },
    { "HS-5645MG", "HS5645MG", 0, HITEC_LAYOUT_HS5, HITEC_SRC_CAPTURED },
    { "HS-5955TG", "HS5955TG", 0, HITEC_LAYOUT_HS5, HITEC_SRC_READ },
    { "HS-7955TG", "HS7955", 0, HITEC_LAYOUT_HS7, HITEC_SRC_CAPTURED },
    { "", NULL, 0, HITEC_LAYOUT_HS5, HITEC_SRC_DOC },
    { "", NULL, 0, HITEC_LAYOUT_HS7, HITEC_SRC_DOC },
    { "", NULL, 0, HITEC_LAYOUT_HS5, HITEC_SRC_DOC },
};

/** @brief The D series registers of a full read. */
static const uint8_t k_d_read[] = {
    0x00, 0x04, 0x0C, 0x22, 0x32, 0x44, 0x4C, 0x4E, 0x54, 0x5E, 0x60,
    0x64, 0x6C, 0x8A, 0x8C, 0x9C, 0xB0, 0xB2, 0xC2, 0xD4, 0xD6,
};

/** @brief The classic cells of a full read. */
static const hitec_range_t k_c_read[] = {
    { 0x00, HITEC_C_BLOCK_LEN }, { HITEC_C_FACTORY, HITEC_C_BLOCK_LEN },
    { HITEC_C_NAME, HITEC_C_NAME_LEN }, { HITEC_C_DATE, 4 },
    { HITEC_C_SERIAL, 8 },
};

/* --- image ---------------------------------------------------------------- */

void hitec_image_init(hitec_image_t *img, uint8_t series)
{
    if (img != NULL) {
        memset(img, 0, sizeof(*img));
        img->series = series;
    }
}

/**
 * @brief Mark byte @p addr known.
 *
 */
static void mark(hitec_image_t *img, unsigned addr)
{
    img->known[addr / 8u] = (uint8_t)(img->known[addr / 8u]
                                      | (1u << (addr % 8u)));
}

bool hitec_image_put_cell(hitec_image_t *img, uint8_t cell, uint8_t value)
{
    if (img == NULL || img->series != HITEC_CLASSIC) {
        return false;
    }
    img->mem[cell] = value;
    mark(img, cell);
    return true;
}

bool hitec_image_put_reg(hitec_image_t *img, uint8_t addr, uint16_t value)
{
    if (img == NULL || img->series != HITEC_D || (addr & 1u) != 0u) {
        return false;
    }
    img->mem[addr] = (uint8_t)(value & 0xFFu);
    img->mem[addr + 1u] = (uint8_t)(value >> 8);
    mark(img, addr);
    mark(img, addr + 1u);
    return true;
}

bool hitec_image_put_version(hitec_image_t *img, uint8_t version)
{
    if (img == NULL || img->series != HITEC_CLASSIC) {
        return false;
    }
    img->version = version;
    img->has_version = 1u;
    return true;
}

bool hitec_image_known(const hitec_image_t *img, uint8_t addr)
{
    return img != NULL && ((img->known[addr / 8u] >> (addr % 8u)) & 1u) != 0u;
}

uint8_t hitec_image_cell(const hitec_image_t *img, uint8_t cell)
{
    return img == NULL ? 0u : img->mem[cell];
}

uint16_t hitec_image_reg(const hitec_image_t *img, uint8_t addr)
{
    if (img == NULL || (addr & 1u) != 0u) {
        return 0u;
    }
    return (uint16_t)(img->mem[addr] | ((unsigned)img->mem[addr + 1u] << 8));
}

/**
 * @brief True when @p count bytes from @p first are known.
 *
 */
static bool known_run(const hitec_image_t *img, unsigned first,
                      unsigned count)
{
    for (unsigned a = first; a < first + count; ++a) {
        if (!hitec_image_known(img, (uint8_t)a)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Store a classic value of 2 cells, high byte first.
 *
 */
static void put_be16(hitec_image_t *img, uint8_t cell, unsigned value)
{
    img->mem[cell] = (uint8_t)(value >> 8);
    img->mem[cell + 1u] = (uint8_t)(value & 0xFFu);
}

/**
 * @brief A classic value of 2 cells, high byte first.
 *
 */
static unsigned be16(const hitec_image_t *img, uint8_t cell)
{
    return ((unsigned)img->mem[cell] << 8) | img->mem[cell + 1u];
}

/**
 * @brief Store a D series register in a draft.
 *
 */
static void put_reg(hitec_image_t *img, uint8_t addr, unsigned value)
{
    img->mem[addr] = (uint8_t)(value & 0xFFu);
    img->mem[addr + 1u] = (uint8_t)(value >> 8);
}

/* --- what to read --------------------------------------------------------- */

const uint8_t *hitec_d_read_list(size_t *n)
{
    if (n != NULL) {
        *n = sizeof(k_d_read);
    }
    return k_d_read;
}

const hitec_range_t *hitec_c_read_ranges(size_t *n)
{
    if (n != NULL) {
        *n = sizeof(k_c_read) / sizeof(k_c_read[0]);
    }
    return k_c_read;
}

/* --- checksum and mirror -------------------------------------------------- */

/**
 * @brief Sum of @p count cells from 0x00 of @p mem, modulo 256.
 *
 */
static uint8_t sum_cells(const uint8_t *mem, unsigned count)
{
    unsigned s = 0;

    for (unsigned i = 0; i < count; ++i) {
        s += mem[i];
    }
    return (uint8_t)s;
}

uint8_t hitec_c_eeprom_sum(const hitec_image_t *img)
{
    return img == NULL ? 0u : sum_cells(img->mem, HITEC_C_SUM_LAST + 1u);
}

uint8_t hitec_c_balance(const hitec_image_t *img)
{
    return img == NULL ? 0u
                       : (uint8_t)(0u - sum_cells(img->mem, HITEC_C_SUM_LAST));
}

bool hitec_c_rebalance(hitec_image_t *img)
{
    if (img == NULL || img->series != HITEC_CLASSIC
        || !known_run(img, 0, HITEC_C_SUM_LAST)) {
        return false;
    }
    img->mem[HITEC_C_SUM_LAST] = hitec_c_balance(img);
    mark(img, HITEC_C_SUM_LAST);
    return true;
}

bool hitec_d_mirror(uint16_t pos, uint16_t *out)
{
    if (out == NULL || pos == 0u || pos > HITEC_D_POS_MAX) {
        return false;
    }
    *out = (uint16_t)(HITEC_D_POS_MAX + 1u - pos);
    return true;
}

/* --- models --------------------------------------------------------------- */

uint8_t hitec_layout_series(uint8_t layout)
{
    return layout == HITEC_LAYOUT_D ? (uint8_t)HITEC_D
                                    : (uint8_t)HITEC_CLASSIC;
}

/**
 * @brief Fill @p out from row @p model of k_models.
 *
 */
static void take_model(hitec_model_t *out, uint8_t model)
{
    out->model = model;
    out->layout = k_models[model].layout;
    out->src = k_models[model].src;
}

bool hitec_identify(const hitec_image_t *img, hitec_model_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (img == NULL) {
        return false;
    }
    if (img->series == HITEC_D) {
        if (!hitec_image_known(img, HITEC_D_REG_MODEL)) {
            return false;
        }
        out->number = hitec_image_reg(img, HITEC_D_REG_MODEL);
        take_model(out, HITEC_MODEL_D_OTHER);
        for (unsigned m = HITEC_MODEL_D485HW; m < HITEC_MODEL_D_OTHER; ++m) {
            if (k_models[m].number == out->number) {
                take_model(out, (uint8_t)m);
            }
        }
        return true;
    }
    if (!known_run(img, HITEC_C_NAME, HITEC_C_NAME_LEN)) {
        return false;
    }
    for (unsigned i = 0; i < HITEC_C_NAME_LEN; ++i) {
        const uint8_t c = img->mem[HITEC_C_NAME + i];
        if (c == 0u) {
            break;
        }
        out->name[i] = (c >= 0x20u && c < 0x7Fu) ? (char)c : '?';
    }
    take_model(out, HITEC_MODEL_C_OTHER);
    if (strncmp(out->name, "HS5", 3) == 0) {
        take_model(out, HITEC_MODEL_HS5_OTHER);
    } else if (strncmp(out->name, "HS7", 3) == 0) {
        take_model(out, HITEC_MODEL_HS7_OTHER);
    }
    for (unsigned m = HITEC_MODEL_HS5645MG; m < HITEC_MODEL_HS5_OTHER; ++m) {
        if (strcmp(out->name, k_models[m].cells) == 0) {
            take_model(out, (uint8_t)m);
        }
    }
    return true;
}

const char *hitec_model_name(uint8_t model)
{
    return model < HITEC_MODEL_COUNT ? k_models[model].name : "";
}

/* --- parameters ----------------------------------------------------------- */

const hitec_param_info_t *hitec_param(uint8_t layout, uint8_t id)
{
    if (layout >= HITEC_LAYOUT_COUNT) {
        return NULL;
    }
    for (size_t i = 0; i < ROW_COUNT; ++i) {
        if (k_rows[i].id == id
            && (k_rows[i].layouts & HITEC_LAYOUT_BIT(layout)) != 0u) {
            return &k_rows[i];
        }
    }
    return NULL;
}

/**
 * @brief The row of @p id for an image of @p layout; NULL for a bad
 *        argument or an image of the other series.
 *
 */
static const hitec_param_info_t *row_for(const hitec_image_t *img,
                                         uint8_t layout, uint8_t id)
{
    if (img == NULL || img->series != hitec_layout_series(layout)) {
        return NULL;
    }
    return hitec_param(layout, id);
}

/**
 * @brief The pair table of encoding @p enc; an empty one for an encoding
 *        without.
 *
 */
static pairs_t pairs_of(uint8_t enc)
{
    pairs_t t = { NULL, 0 };

    switch (enc) {
    case ENC_D_DIR:    t.p = k_d_dir;    t.n = 2;  break;
    case ENC_STEP5:    t.p = k_step5;    t.n = 5;  break;
    case ENC_D_SPEED:  t.p = k_d_speed;  t.n = 11; break;
    case ENC_D_SOFT:   t.p = k_d_soft;   t.n = 5;  break;
    case ENC_OVERLOAD:
    case ENC_C_OVL5:
    case ENC_C_OVL7:   t.p = k_overload; t.n = 6;  break;
    case ENC_C_SPEED:  t.p = k_c_speed;  t.n = 10; break;
    case ENC_C_RES:    t.p = k_c_res;    t.n = 3;  break;
    default: break;
    }
    return t;
}

/**
 * @brief The cell a classic end point is in: @p addr on a CW servo, @p addr2
 *        on a CCW one.
 *
 */
static uint8_t end_cell(const hitec_image_t *img, const hitec_param_info_t *p)
{
    return (img->mem[0x1B] & 1u) != 0u ? p->addr : p->addr2;
}

/**
 * @brief The 0.25us a classic position adds to its stored value.
 *
 */
static unsigned quarter_offset(uint8_t enc)
{
    return enc == ENC_C_CENTRE5 ? 2052u : enc == ENC_C_CENTRE7 ? 2048u : 4u;
}

/**
 * @brief The stored value of row @p p: the register, the cells high byte
 *        first, or the value cell.
 *
 */
static uint32_t raw_of(const hitec_image_t *img, const hitec_param_info_t *p)
{
    uint32_t raw = 0;

    if (img->series == HITEC_D) {
        return hitec_image_reg(img, p->addr);
    }
    switch (p->enc) {
    case ENC_C_VERSION: return img->version;
    case ENC_C_SUM:     return hitec_c_eeprom_sum(img);
    case ENC_C_SERIAL:  return 0u;
    case ENC_C_END:     return img->mem[end_cell(img, p)];
    default: break;
    }
    for (unsigned i = 0; i < p->size; ++i) {
        raw = (raw << 8) | img->mem[p->addr + i];
    }
    return raw;
}

/**
 * @brief True when the bytes a decode of @p p reads are known; the smart
 *        sensor's 0x8A and 0x8C are checked where they are compared.
 *
 */
static bool known_for_get(const hitec_image_t *img,
                          const hitec_param_info_t *p)
{
    switch (p->enc) {
    case ENC_C_VERSION:
        return img->has_version != 0u;
    case ENC_C_END:
        return known_run(img, 0x13, 2) && hitec_image_known(img, 0x1B);
    case ENC_D_SMART:
        return hitec_image_known(img, p->addr)
               && hitec_image_known(img, p->addr2);
    default:
        return known_run(img, p->addr, p->size);
    }
}

/**
 * @brief Look a raw value up in a pair table.
 *
 */
static hitec_get_t from_pairs(pairs_t t, uint32_t raw, hitec_value_t *out)
{
    for (size_t i = 0; i < t.n; ++i) {
        if (t.p[i].raw == raw) {
            out->value = t.p[i].shown;
            return HITEC_GET_OK;
        }
    }
    out->value = (int32_t)raw;
    return HITEC_GET_RAW;
}

/**
 * @brief OK with @p value when @p fits, else RAW with the raw value.
 *
 */
static hitec_get_t fit(bool fits, int32_t value, hitec_value_t *out)
{
    out->value = fits ? value : (int32_t)out->raw;
    return fits ? HITEC_GET_OK : HITEC_GET_RAW;
}

/**
 * @brief Decode the date cells: day, month, year high byte first.
 *
 */
static hitec_get_t get_date(const hitec_image_t *img, hitec_value_t *out)
{
    const unsigned day = img->mem[HITEC_C_DATE];
    const unsigned month = img->mem[HITEC_C_DATE + 1u];
    const unsigned year = be16(img, HITEC_C_DATE + 2u);

    return fit(day >= 1u && day <= 31u && month >= 1u && month <= 12u,
               (int32_t)(year * 10000u + month * 100u + day), out);
}

/**
 * @brief Decode the serial digits.
 *
 */
static hitec_get_t get_serial(const hitec_image_t *img, hitec_value_t *out)
{
    int32_t n = 0;

    for (unsigned i = 0; i < HITEC_C_SERIAL_LEN; ++i) {
        const uint8_t c = img->mem[HITEC_C_SERIAL + i];
        if (c < (uint8_t)'0' || c > (uint8_t)'9') {
            return fit(false, 0, out);
        }
        n = n * 10 + (c - '0');
    }
    return fit(true, n, out);
}

/**
 * @brief Decode the smart sensor: on is 0x6C = 0x44 = 0, off is 0x6C =
 *        0x8A and 0x44 = 0x8C.
 *
 */
static hitec_get_t get_smart(const hitec_image_t *img, hitec_value_t *out)
{
    const uint16_t a = hitec_image_reg(img, 0x6C);
    const uint16_t b = hitec_image_reg(img, 0x44);

    if (a == 0u && b == 0u) {
        return fit(true, HITEC_ON, out);
    }
    if (!hitec_image_known(img, 0x8A) || !hitec_image_known(img, 0x8C)) {
        return HITEC_GET_UNKNOWN;
    }
    return fit(a == hitec_image_reg(img, 0x8A)
               && b == hitec_image_reg(img, 0x8C), HITEC_OFF, out);
}

/**
 * @brief Decode row @p p of a checked image.
 *
 */
static hitec_get_t decode(const hitec_image_t *img,
                          const hitec_param_info_t *p, hitec_value_t *out)
{
    const uint32_t raw = raw_of(img, p);
    const pairs_t t = pairs_of(p->enc);

    out->raw = raw;
    if (t.n != 0u) {
        return from_pairs(t, raw, out);
    }
    switch (p->enc) {
    case ENC_D_ID:     return fit(raw <= 255u, (int32_t)raw, out);
    case ENC_D_POS:    return fit(raw <= HITEC_D_POS_MAX, (int32_t)raw, out);
    case ENC_D_FS:     return fit(raw <= 5000u, (int32_t)raw, out);
    case ENC_D_SMART:  return get_smart(img, out);
    case ENC_C_DATE:   return get_date(img, out);
    case ENC_C_SERIAL: return get_serial(img, out);
    case ENC_C_SUM:    return fit(true, raw == 0u, out);
    case ENC_C_DIR:
        return fit(true, (raw & 1u) != 0u ? HITEC_CW : HITEC_CCW, out);
    case ENC_C_FS:     return fit(true, (int32_t)((raw >> 1) & 1u), out);
    case ENC_C_CENTRE5:
    case ENC_C_CENTRE7:
    case ENC_C_FSPOS5:
        return fit(true, (int32_t)(raw + quarter_offset(p->enc)), out);
    default:           return fit(true, (int32_t)raw, out);
    }
}

hitec_get_t hitec_param_get(const hitec_image_t *img, uint8_t layout,
                            uint8_t id, hitec_value_t *out)
{
    const hitec_param_info_t *p = row_for(img, layout, id);
    hitec_get_t r;

    if (out == NULL || p == NULL) {
        if (out != NULL) {
            memset(out, 0, sizeof(*out));
        }
        return HITEC_GET_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!known_for_get(img, p)) {
        return HITEC_GET_UNKNOWN;
    }
    r = decode(img, p, out);
    if (r == HITEC_GET_UNKNOWN) {
        memset(out, 0, sizeof(*out));
    }
    return r;
}

/**
 * @brief True when @p v is a value an edit of row @p p takes.
 *
 */
static bool value_ok(const hitec_param_info_t *p, int32_t v)
{
    if (p->access != HITEC_RW) {
        return false;
    }
    if (p->unit == HITEC_UNIT_FS_US && (v == HITEC_FS_LIMP
                                        || v == HITEC_FS_OFF)) {
        return true;
    }
    return v >= p->min && v <= p->max && (v - p->min) % p->step == 0;
}

bool hitec_param_ok(uint8_t layout, uint8_t id, int32_t value)
{
    const hitec_param_info_t *p = hitec_param(layout, id);

    return p != NULL && value_ok(p, value);
}

int32_t hitec_param_clamp(uint8_t layout, uint8_t id, int32_t value)
{
    const hitec_param_info_t *p = hitec_param(layout, id);
    int32_t v = value;

    if (p == NULL || p->access != HITEC_RW) {
        return value;
    }
    if (p->unit == HITEC_UNIT_FS_US && v <= HITEC_FS_OFF) {
        return v < HITEC_FS_LIMP ? HITEC_FS_LIMP : v;
    }
    v = v < p->min ? p->min : v > p->max ? p->max : v;
    return p->min + (v - p->min + p->step / 2) / p->step * p->step;
}

/**
 * @brief The raw value of @p v in pair table @p t: the first pair that
 *        shows it.  value_ok() has checked that one does.
 *
 */
static uint16_t to_raw(pairs_t t, int32_t v)
{
    size_t i = 0;

    while (i + 1u < t.n && t.p[i].shown != v) {
        ++i;
    }
    return t.p[i].raw;
}

/**
 * @brief True when the bytes an edit of @p p reads or writes are known,
 *        beyond those of its decode.
 *
 */
static bool known_for_set(const hitec_image_t *img,
                          const hitec_param_info_t *p)
{
    switch (p->enc) {
    case ENC_D_DIR:
        if (hitec_image_reg(img, 0x5E) > 1u) {
            return true;   /* 0x5E alone: no mirror */
        }
        /* fall through */
    case ENC_D_POS:
        return hitec_image_known(img, 0xC2) && hitec_image_known(img, 0xB2)
               && hitec_image_known(img, 0xB0);
    case ENC_D_SMART:
        return hitec_image_known(img, 0x8A) && hitec_image_known(img, 0x8C);
    case ENC_C_DIR:
        if (!hitec_image_known(img, 0x49) || !hitec_image_known(img, 0x4A)) {
            return false;
        }
        break;
    case ENC_C_OVL5:
    case ENC_C_OVL7:
        if (!hitec_image_known(img, p->addr2)) {
            return false;
        }
        break;
    default:
        break;
    }
    return img->series == HITEC_D || known_run(img, 0, HITEC_C_SUM_LAST + 1u);
}

/**
 * @brief Edit a D series position and keep left end < neutral < right end;
 *        the other two positions must fit the encoding.
 *
 */
static hitec_set_t set_d_pos(hitec_image_t *d, const hitec_param_info_t *p,
                             int32_t v)
{
    unsigned left = hitec_image_reg(d, 0xB2);
    unsigned mid = hitec_image_reg(d, 0xC2);
    unsigned right = hitec_image_reg(d, 0xB0);

    if (p->id == HITEC_P_D_LEFT_END) {
        left = (unsigned)v;
    } else if (p->id == HITEC_P_D_NEUTRAL) {
        mid = (unsigned)v;
    } else {
        right = (unsigned)v;
    }
    if (left > HITEC_D_POS_MAX || mid > HITEC_D_POS_MAX
        || right > HITEC_D_POS_MAX) {
        return HITEC_SET_ERR_RAW;
    }
    if (!(left < mid && mid < right)) {
        return HITEC_SET_ERR_ORDER;
    }
    put_reg(d, p->addr, (unsigned)v);
    return HITEC_SET_OK;
}

/**
 * @brief Edit the D series direction: 0x5E and the three positions
 *        mirrored, the ends swapped.  From a 0x5E that is neither 0 nor 1
 *        the servo's direction is not known: 0x5E alone.
 *
 */
static hitec_set_t set_d_dir(hitec_image_t *d, int32_t v)
{
    uint16_t mid;
    uint16_t left;
    uint16_t right;

    if (hitec_image_reg(d, 0x5E) > 1u) {
        put_reg(d, 0x5E, (unsigned)v);
        return HITEC_SET_OK;
    }
    if (!hitec_d_mirror(hitec_image_reg(d, 0xC2), &mid)
        || !hitec_d_mirror(hitec_image_reg(d, 0xB0), &left)
        || !hitec_d_mirror(hitec_image_reg(d, 0xB2), &right)) {
        return HITEC_SET_ERR_MIRROR;
    }
    put_reg(d, 0xC2, mid);
    put_reg(d, 0xB2, left);
    put_reg(d, 0xB0, right);
    put_reg(d, 0x5E, (unsigned)v);
    return HITEC_SET_OK;
}

/**
 * @brief Edit the classic direction: 0x1B bit 0, the end points swapped,
 *        the fail-safe position from the factory block.
 *
 */
static void set_c_dir(hitec_image_t *d, int32_t v)
{
    const uint8_t right = d->mem[0x13];

    d->mem[0x13] = d->mem[0x14];
    d->mem[0x14] = right;
    d->mem[0x19] = d->mem[0x49];
    d->mem[0x1A] = d->mem[0x4A];
    d->mem[0x1B] = (uint8_t)((d->mem[0x1B] & ~1u) | (v == HITEC_CW ? 1u : 0u));
}

/**
 * @brief Encode @p v into a draft with its side effects; the draft is
 *        checked and @p v is a value of the row.
 *
 */
static hitec_set_t encode(hitec_image_t *d, const hitec_param_info_t *p,
                          int32_t v)
{
    const pairs_t t = pairs_of(p->enc);
    const unsigned u = (unsigned)v;

    switch (p->enc) {
    case ENC_D_POS:   return set_d_pos(d, p, v);
    case ENC_D_DIR:   return set_d_dir(d, v);
    case ENC_D_ID:
    case ENC_D_FS:    put_reg(d, p->addr, u); break;
    case ENC_STEP5:
    case ENC_D_SPEED:
    case ENC_D_SOFT:
    case ENC_OVERLOAD:
        if (d->series == HITEC_D) {
            put_reg(d, p->addr, to_raw(t, v));
        } else {
            d->mem[p->addr] = (uint8_t)to_raw(t, v);
        }
        break;
    case ENC_D_SMART:
        if (v == HITEC_OFF && hitec_image_reg(d, 0x8A) == 0u
            && hitec_image_reg(d, 0x8C) == 0u) {
            return HITEC_SET_ERR_RAW;   /* off would read as on */
        }
        put_reg(d, 0x6C, v == HITEC_ON ? 0u : hitec_image_reg(d, 0x8A));
        put_reg(d, 0x44, v == HITEC_ON ? 0u : hitec_image_reg(d, 0x8C));
        break;
    case ENC_C_SPEED: d->mem[p->addr] = (uint8_t)to_raw(t, v); break;
    case ENC_C_OVL5:
    case ENC_C_OVL7:
        d->mem[p->addr] = (uint8_t)to_raw(t, v);
        d->mem[p->addr2] = p->enc == ENC_C_OVL5 ? 0xFFu : 0x00u;
        break;
    case ENC_C_CENTRE5:
    case ENC_C_CENTRE7:
        put_be16(d, p->addr, u - quarter_offset(p->enc));
        d->mem[0x13] = 0x10u;
        d->mem[0x14] = 0x10u;
        break;
    case ENC_C_FSPOS5:
        put_be16(d, p->addr, u - quarter_offset(p->enc));
        d->mem[0x1B] |= 2u;
        break;
    case ENC_C_END:   d->mem[end_cell(d, p)] = (uint8_t)u; break;
    case ENC_C_DIR:   set_c_dir(d, v); break;
    default:  /* ENC_C_FS */
        d->mem[0x1B] = (uint8_t)((d->mem[0x1B] & ~2u) | (u << 1));
        break;
    }
    if (d->series == HITEC_CLASSIC) {
        d->mem[HITEC_C_SUM_LAST] = hitec_c_balance(d);
    }
    return HITEC_SET_OK;
}

hitec_set_t hitec_param_set(hitec_image_t *img, uint8_t layout, uint8_t id,
                            int32_t value)
{
    const hitec_param_info_t *p = row_for(img, layout, id);
    hitec_image_t draft;
    hitec_value_t now;
    hitec_get_t got;
    hitec_set_t r;

    if (p == NULL) {
        return HITEC_SET_ERR_ARG;
    }
    if (p->access != HITEC_RW) {
        return HITEC_SET_ERR_RO;
    }
    if (!value_ok(p, value)) {
        return HITEC_SET_ERR_RANGE;
    }
    if (!known_for_get(img, p) || !known_for_set(img, p)) {
        return HITEC_SET_ERR_UNKNOWN;
    }
    got = hitec_param_get(img, layout, id, &now);
    if (got == HITEC_GET_OK && now.value == value) {
        return HITEC_SET_OK;
    }
    draft = *img;
    r = encode(&draft, p, value);
    if (r == HITEC_SET_OK) {
        *img = draft;
    }
    return r;
}

bool hitec_c_factory(hitec_image_t *img)
{
    if (img == NULL || img->series != HITEC_CLASSIC
        || !known_run(img, 0, HITEC_C_BLOCK_LEN)
        || !known_run(img, HITEC_C_FACTORY, HITEC_C_BLOCK_LEN)) {
        return false;
    }
    memcpy(img->mem, &img->mem[HITEC_C_FACTORY], HITEC_C_BLOCK_LEN);
    img->mem[HITEC_C_SUM_LAST] = hitec_c_balance(img);
    return true;
}

/* --- write plans ---------------------------------------------------------- */

/** @brief The HPP-21 Plus's settings: first cell and count, in its order. */
static const uint8_t k_c_ops[][2] = {
    { 0x00, 1 }, { 0x01, 1 }, { 0x02, 1 }, { 0x03, 1 }, { 0x04, 1 },
    { 0x05, 1 }, { 0x06, 1 }, { 0x07, 2 }, { 0x09, 2 }, { 0x0B, 2 },
    { 0x0D, 2 }, { 0x0F, 2 }, { 0x11, 2 }, { 0x13, 1 }, { 0x14, 1 },
    { 0x15, 2 }, { 0x17, 2 }, { 0x19, 2 }, { 0x1B, 1 }, { 0x1D, 1 },
    { 0x1E, 2 }, { 0x20, 2 }, { 0x22, 1 }, { 0x23, 1 },
};

/** @brief A register a D series setting writes. */
typedef struct {
    uint8_t  addr;   /**< register */
    uint8_t  how;    /**< D_IMG, D_TRIG or D_CONST */
    uint16_t value;  /**< D_CONST: the value */
} d_item_t;

/** @brief How a D series setting writes a register. */
enum {
    D_IMG = 0,  /**< the draft's value */
    D_TRIG,     /**< the draft's value; a change starts the setting */
    D_CONST,    /**< a fixed value */
};

/** @brief The HFP-30's settings, in order, each ending at a 0 address. */
static const d_item_t k_d_ops[][6] = {
    { { 0x32, D_TRIG, 0 } },
    { { 0xC2, D_IMG, 0 }, { 0xB2, D_IMG, 0 }, { 0xB0, D_IMG, 0 },
      { 0x4C, D_IMG, 0 }, { 0x5E, D_TRIG, 0 } },
    { { 0xC2, D_TRIG, 0 } },
    { { 0xB2, D_TRIG, 0 } },
    { { 0xB0, D_TRIG, 0 } },
    { { 0x4E, D_TRIG, 0 } },
    { { 0x54, D_TRIG, 0 } },
    { { 0x4C, D_TRIG, 0 } },
    { { 0x60, D_TRIG, 0 } },
    { { 0x9C, D_TRIG, 0 }, { 0x9A, D_CONST, 3 }, { 0x98, D_CONST, 200 } },
    { { 0x72, D_CONST, 0x4E54 }, { 0x6C, D_TRIG, 0 }, { 0x44, D_TRIG, 0 } },
};
/** @brief The direction's setting in k_d_ops. */
#define D_OP_DIRECTION 1u
/** @brief Settings in k_d_ops. */
#define D_OP_COUNT (sizeof(k_d_ops) / sizeof(k_d_ops[0]))
/** @brief Items of one setting. */
#define D_ITEM_COUNT (sizeof(k_d_ops[0]) / sizeof(k_d_ops[0][0]))

bool hitec_writable(uint8_t series, uint8_t addr)
{
    if (series == HITEC_CLASSIC) {
        return addr < HITEC_C_BLOCK_LEN && addr != HITEC_C_SUM_LAST;
    }
    for (size_t i = 0; i < D_OP_COUNT; ++i) {
        for (size_t j = 0; j < D_ITEM_COUNT; ++j) {
            if (k_d_ops[i][j].addr == addr && k_d_ops[i][j].how != D_CONST
                && addr != 0u) {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Append a write to @p out.
 *
 */
static void push(hitec_plan_t *out, uint8_t kind, uint8_t addr,
                 uint16_t value, uint16_t wait_ms)
{
    if (out->n < HITEC_PLAN_MAX) {
        const hitec_step_t s = { kind, addr, value, wait_ms };
        out->step[out->n++] = s;
    }
}

/**
 * @brief The classic plan; both images are classic.
 *
 */
static hitec_plan_result_t plan_c(const hitec_image_t *from,
                                  const hitec_image_t *to, hitec_plan_t *out)
{
    uint8_t cur[HITEC_C_BLOCK_LEN];

    if (!known_run(from, 0, HITEC_C_BLOCK_LEN)
        || !known_run(to, 0, HITEC_C_BLOCK_LEN)) {
        return HITEC_PLAN_ERR_UNKNOWN;
    }
    memcpy(cur, from->mem, sizeof(cur));
    for (size_t i = 0; i < sizeof(k_c_ops) / sizeof(k_c_ops[0]); ++i) {
        const uint8_t first = k_c_ops[i][0];
        const uint8_t count = k_c_ops[i][1];

        if (memcmp(&cur[first], &to->mem[first], count) == 0) {
            continue;
        }
        for (uint8_t c = first; c < first + count; ++c) {
            cur[c] = to->mem[c];
            push(out, HITEC_STEP_C_CELL, c, cur[c], 0);
        }
        if (first < HITEC_C_SUM_LAST) {
            cur[HITEC_C_SUM_LAST] =
                (uint8_t)(0u - sum_cells(cur, HITEC_C_SUM_LAST));
            push(out, HITEC_STEP_C_SUM, HITEC_C_SUM_LAST,
                 cur[HITEC_C_SUM_LAST], 0);
        }
    }
    if (sum_cells(cur, HITEC_C_SUM_LAST + 1u) != 0u) {
        push(out, HITEC_STEP_C_SUM, HITEC_C_SUM_LAST,
             (uint8_t)(0u - sum_cells(cur, HITEC_C_SUM_LAST)), 0);
    }
    return HITEC_PLAN_OK;
}

/**
 * @brief True when a trigger of D series setting @p op differs; false with
 *        @p *unknown set when one is known in the draft only.
 *
 */
static bool d_op_changed(const hitec_image_t *from, const hitec_image_t *to,
                         size_t op, bool *unknown)
{
    bool changed = false;

    for (size_t j = 0; j < D_ITEM_COUNT; ++j) {
        const d_item_t *it = &k_d_ops[op][j];

        if (it->how != D_TRIG || !hitec_image_known(to, it->addr)) {
            continue;
        }
        if (!hitec_image_known(from, it->addr)) {
            *unknown = true;
        } else if (hitec_image_reg(from, it->addr)
                   != hitec_image_reg(to, it->addr)) {
            changed = true;
        }
    }
    return changed && !*unknown;
}

/**
 * @brief True when a plan from @p from writes item @p it of setting @p op:
 *        from a direction that is neither 0 nor 1 the direction's setting
 *        writes 0x5E alone.
 *
 */
static bool d_item_written(const hitec_image_t *from, size_t op,
                           const d_item_t *it)
{
    return !(op == D_OP_DIRECTION && it->how != D_TRIG
             && hitec_image_reg(from, 0x5E) > 1u);
}

/**
 * @brief The D series plan; both images are D series.
 *
 */
static hitec_plan_result_t plan_d(const hitec_image_t *from,
                                  const hitec_image_t *to, hitec_plan_t *out)
{
    uint8_t written[HITEC_MEM_SIZE / 8u] = { 0 };
    bool unknown = false;

    for (size_t i = 0; i < D_OP_COUNT; ++i) {
        if (!d_op_changed(from, to, i, &unknown)) {
            if (unknown) {
                return HITEC_PLAN_ERR_UNKNOWN;
            }
            continue;
        }
        for (size_t j = 0; j < D_ITEM_COUNT && k_d_ops[i][j].addr != 0u;
             ++j) {
            const d_item_t *it = &k_d_ops[i][j];
            if (it->how != D_CONST && d_item_written(from, i, it)
                && !hitec_image_known(to, it->addr)) {
                return HITEC_PLAN_ERR_UNKNOWN;
            }
        }
        for (size_t j = 0; j < D_ITEM_COUNT && k_d_ops[i][j].addr != 0u;
             ++j) {
            const d_item_t *it = &k_d_ops[i][j];
            const unsigned bit = 1u << (it->addr / 2u % 8u);
            uint8_t *w = &written[it->addr / 16u];

            if (d_item_written(from, i, it) && (*w & bit) == 0u) {
                *w = (uint8_t)(*w | bit);
                push(out, HITEC_STEP_D_WRITE, it->addr,
                     it->how == D_CONST ? it->value
                                        : hitec_image_reg(to, it->addr),
                     HITEC_D_WRITE_GAP_MS);
            }
        }
    }
    if (out->n != 0u) {
        push(out, HITEC_STEP_D_SAVE, HITEC_D_REG_SAVE, HITEC_D_SAVE_KEY,
             HITEC_D_SAVE_GAP_MS);
        push(out, HITEC_STEP_D_REBOOT, HITEC_D_REG_REBOOT, 1u,
             HITEC_D_REBOOT_WAIT_MS);
    }
    return HITEC_PLAN_OK;
}

hitec_plan_result_t hitec_plan(const hitec_image_t *from,
                               const hitec_image_t *to, hitec_plan_t *out)
{
    hitec_plan_result_t r;

    if (out == NULL) {
        return HITEC_PLAN_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (from == NULL || to == NULL || from->series != to->series) {
        return HITEC_PLAN_ERR_ARG;
    }
    r = from->series == HITEC_CLASSIC ? plan_c(from, to, out)
                                      : plan_d(from, to, out);
    if (r != HITEC_PLAN_OK) {
        memset(out, 0, sizeof(*out));
    }
    return r;
}

void hitec_d_factory_plan(hitec_plan_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    push(out, HITEC_STEP_D_WRITE, HITEC_D_REG_FACTORY, HITEC_D_FACTORY_KEY,
         HITEC_D_WRITE_GAP_MS);
    push(out, HITEC_STEP_D_SAVE, HITEC_D_REG_SAVE, HITEC_D_SAVE_KEY,
         HITEC_D_SAVE_GAP_MS);
    push(out, HITEC_STEP_D_REBOOT, HITEC_D_REG_REBOOT, 1u,
         HITEC_D_REBOOT_WAIT_MS);
}
