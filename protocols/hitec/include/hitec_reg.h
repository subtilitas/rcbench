/**
 * @file
 * @brief Hitec servo programming protocols: the register models, the
 *        parameter table and the write plans.
 *
 * Implements the register models of Hitec servos.  Hitec is a name or
 * trademark of its owner; it is used here only to say which servos this
 * module works with (README.md, "Legal notice and disclaimer").
 *
 * Two models share one image type (hitec_image_t, 256 bytes):
 *
 *   - Classic, the HS-5xxx and HS-7xxx servos: 256 EEPROM cells of 8 bits.
 *     A two-cell value is stored high byte first.  Cells 0x00 to 0x1C sum
 *     to 0 modulo 256; cell 0x1C is the balance.  0x30 to 0x53 hold the
 *     factory block in the layout of 0x00 to 0x23, 0x60 to 0x6F the model
 *     name in ASCII.
 *   - D series: registers of 16 bits at even addresses 0x00 to 0xFE, the
 *     low byte at the even address.  A write takes effect in the servo's
 *     RAM; a save (0x70 = 0xFFFF) stores it, and a reboot (0x46 = 1)
 *     restarts the servo with it.
 *
 * The image stores raw values.  A parameter (hitec_param_t) is decoded
 * from the image and encoded into it with the encoding the vendor
 * programmers use: the HFP-30 for the D series, measured on a D485HW; the
 * HPP-21 Plus for classic servos, measured on an HS-5645MG and an
 * HS-7955TG.  A raw value that fits no encoding is shown raw and is not
 * edited.  Every value is an integer in the unit of its hitec_unit_t; no
 * floating point is used.
 *
 * An edit (hitec_param_set()) changes a draft image as the vendor
 * programmer changes the servo, with the side effects it writes: the D
 * series direction mirrors the neutral and both end points; the classic
 * centre resets both end points; the classic direction swaps the end
 * points and takes the fail-safe position from the factory block.  A plan
 * (hitec_plan()) lists the writes that take a servo from its read image to
 * the draft, in the vendor programmer's order, with the classic checksum
 * write and the D series save and reboot.
 *
 * Every table row says where its encoding comes from (hitec_src_t).  The
 * module identifies the model from the read data (hitec_identify()); a
 * model other than the measured ones takes its series' layout.
 *
 * The module has no hardware access, no heap and no global or static
 * mutable state, and includes nothing outside the C standard headers and
 * this folder.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HITEC_REG_H
#define HITEC_REG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hitec_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- image ---------------------------------------------------------------- */

/** @brief Bytes of an image: 256 classic cells or 128 D series registers. */
#define HITEC_MEM_SIZE 256u

/* Classic cells. */
#define HITEC_C_SUM_LAST   0x1Cu  /**< last cell of the checksum, the balance */
#define HITEC_C_FACTORY    0x30u  /**< first cell of the factory block */
#define HITEC_C_BLOCK_LEN  0x24u  /**< cells of a block: 0x00 to 0x23 */
#define HITEC_C_NAME       0x60u  /**< first cell of the model name */
#define HITEC_C_NAME_LEN   16u    /**< cells of the model name */
#define HITEC_C_DATE       0x70u  /**< day, month, year high, year low */
#define HITEC_C_SERIAL     0x80u  /**< 7 ASCII digits */
#define HITEC_C_SERIAL_LEN 7u     /**< digits of the serial */

/* D series registers that code outside the table names. */
#define HITEC_D_REG_MODEL   0x00u  /**< model number */
#define HITEC_D_REG_FACTORY 0x6Eu  /**< 0x0F0F restores the factory values */
#define HITEC_D_REG_SAVE    0x70u  /**< 0xFFFF stores the registers */
#define HITEC_D_REG_REBOOT  0x46u  /**< 1 restarts the servo */
#define HITEC_D_FACTORY_KEY 0x0F0Fu  /**< the value of a factory reset */
#define HITEC_D_SAVE_KEY    0xFFFFu  /**< the value of a save */
/** @brief Highest D series position; 8192 is the centre. */
#define HITEC_D_POS_MAX     16383u

/**
 * @brief The registers or cells of one servo, as read.
 *
 * A byte that was not read is not known; a decode or an edit that needs it
 * fails with an UNKNOWN code.
 */
typedef struct {
    /** @brief Classic cells; D series registers, low byte first. */
    uint8_t mem[HITEC_MEM_SIZE];
    uint8_t known[HITEC_MEM_SIZE / 8u];  /**< bit n: byte n was read */
    uint8_t series;       /**< hitec_family_t */
    uint8_t version;      /**< classic: the reply to the first command, 0x67 */
    uint8_t has_version;  /**< 1: @p version was read */
} hitec_image_t;

/**
 * @brief Empty an image of @p series: no byte known, no version.
 *
 * @param img     Image to empty; NULL does nothing.
 * @param series  hitec_family_t of the servo.
 */
void hitec_image_init(hitec_image_t *img, uint8_t series);

/**
 * @brief Store a classic cell as read.
 *
 * @param img    Classic image.
 * @param cell   Cell address.
 * @param value  Value read.
 * @return false, and no change, for NULL or a D series image.
 */
bool hitec_image_put_cell(hitec_image_t *img, uint8_t cell, uint8_t value);

/**
 * @brief Store a D series register as read.
 *
 * @param img    D series image.
 * @param addr   Register address, even.
 * @param value  Value read.
 * @return false, and no change, for NULL, a classic image or an odd
 *         address.
 */
bool hitec_image_put_reg(hitec_image_t *img, uint8_t addr, uint16_t value);

/**
 * @brief Store the classic reply to the first command (0x67).
 *
 * @param img      Classic image.
 * @param version  First byte of the reply.
 * @return false, and no change, for NULL or a D series image.
 */
bool hitec_image_put_version(hitec_image_t *img, uint8_t version);

/**
 * @brief True when byte @p addr was read.
 *
 * @param img   Image; NULL gives false.
 * @param addr  Cell, or the even address of a D series register.
 * @return true when the byte is known.
 */
bool hitec_image_known(const hitec_image_t *img, uint8_t addr);

/**
 * @brief A classic cell; 0 for NULL.
 *
 * @param img   Image.
 * @param cell  Cell address.
 * @return The cell's value.
 */
uint8_t hitec_image_cell(const hitec_image_t *img, uint8_t cell);

/**
 * @brief A D series register; 0 for NULL or an odd address.
 *
 * @param img   Image.
 * @param addr  Register address.
 * @return The register's value.
 */
uint16_t hitec_image_reg(const hitec_image_t *img, uint8_t addr);

/* --- what to read --------------------------------------------------------- */

/** @brief A run of classic cells. */
typedef struct {
    uint8_t first;  /**< first cell */
    uint8_t count;  /**< cells in the run */
} hitec_range_t;

/**
 * @brief The D series registers a full read takes: the 19 the HFP-30
 *        reads, less 0x6E (write only), plus the model 0x00, the position
 *        0x0C and 0x22.
 *
 * @param n  Receives the count, 21; may be NULL.
 * @return The addresses, ascending.
 */
const uint8_t *hitec_d_read_list(size_t *n);

/**
 * @brief The classic cells a full read takes: 0x00 to 0x23, the factory
 *        block 0x30 to 0x53, the name 0x60 to 0x6F, the date 0x70 to 0x73
 *        and the serial 0x80 to 0x87, 100 cells.
 *
 * @param n  Receives the count of runs, 5; may be NULL.
 * @return The runs, ascending.
 */
const hitec_range_t *hitec_c_read_ranges(size_t *n);

/* --- checksum and mirror -------------------------------------------------- */

/**
 * @brief Sum of classic cells 0x00 to 0x1C modulo 256; 0 is a good image.
 *
 * @param img  Classic image; NULL gives 0.
 * @return The sum.
 */
uint8_t hitec_c_eeprom_sum(const hitec_image_t *img);

/**
 * @brief The value of cell 0x1C that makes cells 0x00 to 0x1C sum to 0.
 *
 * @param img  Classic image; NULL gives 0.
 * @return 0 less the sum of cells 0x00 to 0x1B, modulo 256.
 */
uint8_t hitec_c_balance(const hitec_image_t *img);

/**
 * @brief Write the balance into cell 0x1C of a classic draft.
 *
 * @param img  Classic image.
 * @return false, and no change, for NULL, a D series image or a cell of
 *         0x00 to 0x1B not known.
 */
bool hitec_c_rebalance(hitec_image_t *img);

/**
 * @brief A D series position mirrored by a direction change: 16384 less
 *        the position, as the HFP-30 writes it.
 *
 * @param pos  Position, 1 to HITEC_D_POS_MAX.
 * @param out  Receives the mirrored position.
 * @return false for 0, a position above HITEC_D_POS_MAX and a NULL @p out:
 *         the mirror would leave the range.
 */
bool hitec_d_mirror(uint16_t pos, uint16_t *out);

/* --- models --------------------------------------------------------------- */

/**
 * @brief Register layouts.
 *
 * A layout fixes which parameters a servo has and how they are encoded.
 */
typedef enum {
    HITEC_LAYOUT_D = 0,   /**< D series, measured on a D485HW */
    HITEC_LAYOUT_HS5,     /**< HS-5xxx, measured on an HS-5645MG */
    HITEC_LAYOUT_HS7,     /**< HS-7xxx, measured on an HS-7955TG */
    HITEC_LAYOUT_COUNT
} hitec_layout_t;

/** @brief The bit of layout @p l in a parameter row's layout set. */
#define HITEC_LAYOUT_BIT(l) ((uint8_t)(1u << (l)))

/**
 * @brief The series of a layout.
 *
 * @param layout  hitec_layout_t.
 * @return HITEC_D or HITEC_CLASSIC; HITEC_CLASSIC for an unknown layout.
 */
uint8_t hitec_layout_series(uint8_t layout);

/**
 * @brief Where an entry comes from.
 *
 * A parameter row's mark refers to the measured servo of its layout: the
 * D485HW, the HS-5645MG or the HS-7955TG.  A model's mark says what is
 * measured on that model.
 */
typedef enum {
    /** @brief Written through the rcbench IOmcu and read back. */
    HITEC_SRC_RCBENCH = 0,
    /** @brief A vendor programmer's writes captured on the wire. */
    HITEC_SRC_CAPTURED,
    /** @brief Read from a servo; no write recorded. */
    HITEC_SRC_READ,
    /** @brief Documentation only: Hitec's D series manual, community
     *         documents, third-party observations. */
    HITEC_SRC_DOC,
} hitec_src_t;

/** @brief Models the module knows by name. */
typedef enum {
    HITEC_MODEL_NONE = 0,  /**< what identification reads is not known */
    HITEC_MODEL_D485HW,    /**< register 0x00 reads 34485 */
    HITEC_MODEL_D645MW,    /**< register 0x00 reads 34645 (third-party) */
    HITEC_MODEL_D_OTHER,   /**< any other D series number */
    HITEC_MODEL_HS5645MG,  /**< name `HS5645MG` */
    HITEC_MODEL_HS5955TG,  /**< name `HS5955TG` */
    HITEC_MODEL_HS7955TG,  /**< name `HS7955` */
    HITEC_MODEL_HS5_OTHER, /**< another name starting `HS5` */
    HITEC_MODEL_HS7_OTHER, /**< another name starting `HS7` */
    HITEC_MODEL_C_OTHER,   /**< any other name; the HS-5xxx layout */
    HITEC_MODEL_COUNT
} hitec_model_id_t;

/** @brief What identification found. */
typedef struct {
    uint8_t  model;    /**< hitec_model_id_t */
    uint8_t  layout;   /**< hitec_layout_t */
    uint8_t  src;      /**< hitec_src_t of the model */
    uint16_t number;   /**< D series: register 0x00; classic: 0 */
    /** @brief Classic: the name cells up to the first 0x00, a byte outside
     *         printable ASCII as `?`; D series: empty. */
    char     name[HITEC_C_NAME_LEN + 1u];
} hitec_model_t;

/**
 * @brief Identify the model from a read image.
 *
 * D series: register 0x00.  Classic: the name cells 0x60 to 0x6F.  The
 * classic version byte is not used.
 *
 * @param img  Read image.
 * @param out  Receives the model; on false its model is HITEC_MODEL_NONE.
 * @return false for NULL and for an image without the data read.
 */
bool hitec_identify(const hitec_image_t *img, hitec_model_t *out);

/**
 * @brief The model's name as Hitec writes it, "D485HW", "HS-5645MG"; ""
 *        for the OTHER and NONE models.
 *
 * @param model  hitec_model_id_t.
 * @return The name.
 */
const char *hitec_model_name(uint8_t model);

/* --- parameters ----------------------------------------------------------- */

/** @brief The parameters of both series. */
typedef enum {
    HITEC_P_D_MODEL = 0,     /**< 0x00, model number, read only */
    HITEC_P_D_POSITION,      /**< 0x0C, read only */
    HITEC_P_D_ID,            /**< 0x32 */
    HITEC_P_D_DIRECTION,     /**< 0x5E; mirrors 0xC2, 0xB2, 0xB0 */
    HITEC_P_D_NEUTRAL,       /**< 0xC2 */
    HITEC_P_D_LEFT_END,      /**< 0xB2 */
    HITEC_P_D_RIGHT_END,     /**< 0xB0 */
    HITEC_P_D_DEAD_BAND,     /**< 0x4E */
    HITEC_P_D_SPEED,         /**< 0x54 */
    HITEC_P_D_FAILSAFE,      /**< 0x4C */
    HITEC_P_D_SOFT_START,    /**< 0x60 */
    HITEC_P_D_OVERLOAD,      /**< 0x9C */
    HITEC_P_D_SMART_SENSOR,  /**< 0x6C and 0x44 */
    HITEC_P_D_SENSITIVITY,   /**< 0x64, read only */
    HITEC_P_D_R04,           /**< 0x04, meaning not known, read only */
    HITEC_P_D_R22,           /**< 0x22, meaning not known, read only */
    HITEC_P_D_R8A,           /**< 0x8A, the smart sensor's off value */
    HITEC_P_D_R8C,           /**< 0x8C, the smart sensor's off value */
    HITEC_P_D_RD4,           /**< 0xD4, meaning not known, read only */
    HITEC_P_D_RD6,           /**< 0xD6, meaning not known, read only */
    HITEC_P_C_VERSION,       /**< the reply to 0x67, read only */
    HITEC_P_C_DATE,          /**< 0x70 to 0x73, read only */
    HITEC_P_C_SERIAL,        /**< 0x80 to 0x86, read only */
    HITEC_P_C_CHECKSUM,      /**< 0x00 to 0x1C sum to 0, read only */
    HITEC_P_C_DEAD_BAND,     /**< 0x02 */
    HITEC_P_C_SPEED,         /**< 0x06 */
    HITEC_P_C_CENTRE,        /**< 0x07 and 0x08; resets 0x13, 0x14 */
    HITEC_P_C_LEFT_END,      /**< 0x14 on a CW servo, 0x13 on a CCW one */
    HITEC_P_C_RIGHT_END,     /**< 0x13 on a CW servo, 0x14 on a CCW one */
    HITEC_P_C_DIRECTION,     /**< 0x1B bit 0; swaps 0x13, 0x14 */
    HITEC_P_C_FAILSAFE,      /**< 0x1B bit 1 */
    HITEC_P_C_FS_POSITION,   /**< 0x19 and 0x1A */
    HITEC_P_C_OVERLOAD,      /**< HS-5xxx 0x20, HS-7xxx 0x21 */
    HITEC_P_C_RESOLUTION,    /**< 0x22, HS-7xxx, read only */
    HITEC_P_COUNT
} hitec_param_t;

/** @brief Unit of a decoded value. */
typedef enum {
    HITEC_UNIT_RAW = 0,     /**< the stored value, no unit known */
    HITEC_UNIT_COUNTS,      /**< D series position, 8192 the centre */
    HITEC_UNIT_PERCENT,     /**< 1% */
    HITEC_UNIT_STEP,        /**< a dead band step, 1 to 5 */
    HITEC_UNIT_CHOICE,      /**< an index; hitec_choice_t names them */
    HITEC_UNIT_FS_US,       /**< 0 limp, 1 off, else a position in us */
    HITEC_UNIT_OVERLOAD,    /**< 0 off, else the percent */
    HITEC_UNIT_QUARTER_US,  /**< 0.25us: 6000 is 1500us */
    HITEC_UNIT_DATE,        /**< year x 10000 + month x 100 + day */
    HITEC_UNIT_NUMBER,      /**< a decimal number read from digits */
} hitec_unit_t;

/** @brief The values of the CHOICE parameters. */
typedef enum {
    HITEC_CW = 0,           /**< direction: clockwise */
    HITEC_CCW = 1,          /**< direction: counterclockwise */
    HITEC_OFF = 0,          /**< smart sensor, fail-safe: off */
    HITEC_ON = 1,           /**< smart sensor, fail-safe: on */
    HITEC_RES_NORMAL = 0,   /**< resolution Normal */
    HITEC_RES_HIGH = 1,     /**< resolution High */
    HITEC_SUM_BAD = 0,      /**< checksum: cells 0x00 to 0x1C do not sum to 0 */
    HITEC_SUM_OK = 1,       /**< checksum: they do */
} hitec_choice_t;

#define HITEC_FS_LIMP 0   /**< D series fail-safe: no torque on signal loss */
#define HITEC_FS_OFF  1   /**< D series fail-safe: off */

/** @brief Overload protection off. */
#define HITEC_OVERLOAD_OFF 0

/** @brief Access of a parameter. */
typedef enum {
    HITEC_RO = 0,   /**< read only */
    HITEC_RW,       /**< an edit may change it */
} hitec_access_t;

/** @brief No second register or cell. */
#define HITEC_ADDR_NONE 0xFFu

/**
 * @brief One row of the parameter table.
 *
 * A parameter whose cells or encoding differ between layouts has one row
 * per layout.  @p min, @p max and @p step are the limits of an edit in the
 * row's unit; FS_US also takes HITEC_FS_LIMP and HITEC_FS_OFF.
 */
typedef struct {
    uint8_t id;       /**< hitec_param_t */
    uint8_t layouts;  /**< HITEC_LAYOUT_BIT() of every layout the row is for */
    uint8_t addr;     /**< register or first cell */
    uint8_t addr2;    /**< second register or cell, HITEC_ADDR_NONE */
    uint8_t size;     /**< bytes from @p addr: classic 1 to 16, D 2 */
    uint8_t enc;      /**< encoding, private to hitec_reg.c */
    uint8_t unit;     /**< hitec_unit_t */
    uint8_t access;   /**< hitec_access_t */
    uint8_t src;      /**< hitec_src_t of the encoding */
    uint8_t step;     /**< step of an edit; 0 for read only */
    int32_t min;      /**< smallest value of an edit */
    int32_t max;      /**< largest value of an edit */
} hitec_param_info_t;

/**
 * @brief The row of @p id for @p layout; NULL when the layout has no such
 *        parameter.
 *
 * @param layout  hitec_layout_t.
 * @param id      hitec_param_t.
 * @return The table row, or NULL.
 */
const hitec_param_info_t *hitec_param(uint8_t layout, uint8_t id);

/** @brief Result of a decode. */
typedef enum {
    HITEC_GET_OK = 0,     /**< value is the decoded value */
    HITEC_GET_RAW,        /**< the stored value fits no encoding: value is
                               the raw value, and the parameter is not edited */
    HITEC_GET_UNKNOWN,    /**< a byte the decode needs was not read */
    HITEC_GET_ERR_ARG,    /**< NULL, a parameter the layout lacks, an image
                               of the other series */
} hitec_get_t;

/** @brief A decoded value and the raw value beside it. */
typedef struct {
    int32_t  value;  /**< in the row's unit; for HITEC_GET_RAW the raw value */
    /**
     * @brief The stored value: the register, the cell, or 2 to 4 cells high
     *        byte first; the sum for CHECKSUM; 0 for SERIAL.
     */
    uint32_t raw;
} hitec_value_t;

/**
 * @brief Decode a parameter.
 *
 * @param img     Image.
 * @param layout  hitec_layout_t of the servo.
 * @param id      hitec_param_t.
 * @param out     Receives the value; zero on HITEC_GET_UNKNOWN and on an
 *                error.
 * @return A hitec_get_t.
 */
hitec_get_t hitec_param_get(const hitec_image_t *img, uint8_t layout,
                            uint8_t id, hitec_value_t *out);

/** @brief Result of an edit. */
typedef enum {
    HITEC_SET_OK = 0,      /**< stored, side effects included */
    HITEC_SET_ERR_ARG,     /**< NULL, a parameter the layout lacks, an image
                                of the other series */
    HITEC_SET_ERR_RO,      /**< a read-only parameter */
    HITEC_SET_ERR_RANGE,   /**< a value no step of the encoding gives */
    /** @brief The stored value fits no encoding; also a D series position
     *         edit beside a position above 16383, and smart sensor off with
     *         0x8A = 0x8C = 0, which reads as on. */
    HITEC_SET_ERR_RAW,
    HITEC_SET_ERR_UNKNOWN, /**< a byte the edit reads or writes was not read */
    HITEC_SET_ERR_MIRROR,  /**< D direction: a position the mirror leaves */
    HITEC_SET_ERR_ORDER,   /**< D: left end, neutral, right end not ascending */
} hitec_set_t;

/**
 * @brief Edit a draft: encode @p value and write its side effects.
 *
 * A value equal to the decoded one changes nothing, side effects included.
 * Side effects, as the vendor programmers write them:
 *
 *   D direction     0xC2, 0xB2, 0xB0 mirrored (hitec_d_mirror()), 0xB2 from
 *                   0xB0 and 0xB0 from 0xB2
 *   D smart sensor  on: 0x6C = 0x44 = 0; off: 0x6C = 0x8A, 0x44 = 0x8C
 *   C centre        0x13 = 0x14 = 0x10
 *   C direction     0x13 and 0x14 swapped, 0x19 and 0x1A from 0x49 and 0x4A
 *   C fail-safe position  0x1B bit 1 set
 *   C overload      HS-5xxx 0x21 = 0xFF, HS-7xxx 0x20 = 0x00
 *
 * A classic edit writes the balance into 0x1C.  A D series neutral or end
 * point keeps left end < neutral < right end.
 *
 * @param img     Draft image.
 * @param layout  hitec_layout_t of the servo.
 * @param id      hitec_param_t.
 * @param value   Value in the row's unit.
 * @return A hitec_set_t; on an error the draft is unchanged.
 */
hitec_set_t hitec_param_set(hitec_image_t *img, uint8_t layout, uint8_t id,
                            int32_t value);

/**
 * @brief True when @p value is a value an edit of @p id takes.
 *
 * @param layout  hitec_layout_t.
 * @param id      hitec_param_t.
 * @param value   Value in the row's unit.
 * @return true for a writable parameter and a value inside its limits on
 *         its step.
 */
bool hitec_param_ok(uint8_t layout, uint8_t id, int32_t value);

/**
 * @brief The nearest value an edit of @p id takes.
 *
 * Clamped to the row's limits, then to the nearest step from @p min, a tie
 * going up.  FS_US keeps HITEC_FS_LIMP and HITEC_FS_OFF, takes a value
 * under 0 as HITEC_FS_LIMP and a position under @p min as @p min.  A
 * read-only or unknown parameter gives @p value unchanged.
 *
 * @param layout  hitec_layout_t.
 * @param id      hitec_param_t.
 * @param value   Value in the row's unit.
 * @return The clamped value.
 */
int32_t hitec_param_clamp(uint8_t layout, uint8_t id, int32_t value);

/**
 * @brief Classic factory reset on a draft: cells 0x00 to 0x23 from the
 *        factory block 0x30 to 0x53, then the balance.
 *
 * @param img  Classic draft.
 * @return false, and no change, for NULL, a D series image or a cell of
 *         either block not known.
 */
bool hitec_c_factory(hitec_image_t *img);

/* --- write plans ---------------------------------------------------------- */

/** @brief Kind of a write. */
typedef enum {
    HITEC_STEP_C_CELL = 0,  /**< classic write EEPROM: @p addr, @p value */
    HITEC_STEP_C_SUM,       /**< classic write of 0x1C, the balance */
    HITEC_STEP_D_WRITE,     /**< D series register write */
    HITEC_STEP_D_SAVE,      /**< D series 0x70 = 0xFFFF */
    HITEC_STEP_D_REBOOT,    /**< D series 0x46 = 1 */
} hitec_step_kind_t;

/** @brief One write of a plan. */
typedef struct {
    uint8_t  kind;     /**< hitec_step_kind_t */
    uint8_t  addr;     /**< cell or register */
    uint16_t value;    /**< classic: 0 to 255 */
    uint16_t wait_ms;  /**< the least time from this write to the next */
} hitec_step_t;

/** @brief Gap from a D series write to the next, the HFP-30's. */
#define HITEC_D_WRITE_GAP_MS  20u
/** @brief Gap from the save to the reboot, the HFP-30's. */
#define HITEC_D_SAVE_GAP_MS   100u
/** @brief Wait after the reboot: the servo ignores frames for 1000ms
 *         (third-party observation; the HFP-30 reads again after 834.6ms). */
#define HITEC_D_REBOOT_WAIT_MS 1000u

/**
 * @brief The longest plan: a classic restore of all 35 writable cells with
 *        a balance after each of the 19 settings of cells up to 0x1B, and
 *        room for 2 more.
 */
#define HITEC_PLAN_MAX 56u

/** @brief An ordered list of writes. */
typedef struct {
    uint8_t      n;                     /**< writes in @p step */
    hitec_step_t step[HITEC_PLAN_MAX];  /**< in order */
} hitec_plan_t;

/** @brief Result of planning. */
typedef enum {
    HITEC_PLAN_OK = 0,       /**< out holds the writes; none when equal */
    HITEC_PLAN_ERR_ARG,      /**< NULL, or the two images of other series */
    HITEC_PLAN_ERR_UNKNOWN,  /**< a byte a write needs was not read */
} hitec_plan_result_t;

/**
 * @brief True when a plan writes @p addr of @p series.
 *
 * Classic: cells 0x00 to 0x23 but 0x1C, which the balance writes.  D
 * series: 0x32, 0x44, 0x4C, 0x4E, 0x54, 0x5E, 0x60, 0x6C, 0x9C, 0xB0, 0xB2,
 * 0xC2.
 *
 * @param series  hitec_family_t.
 * @param addr    Cell or register.
 * @return true when a plan writes it.
 */
bool hitec_writable(uint8_t series, uint8_t addr);

/**
 * @brief The writes that take a servo from image @p from to @p to.
 *
 * Only writable addresses (hitec_writable()) are compared; a difference
 * anywhere else is not written.
 *
 * Classic, in the order of the HPP-21 Plus's operations: cells 0x00 to
 * 0x06, the pairs 0x07/08 to 0x11/12, 0x13, 0x14, the pairs 0x15/16 to
 * 0x19/1A, 0x1B, 0x1D, the pairs 0x1E/1F and 0x20/21, 0x22, 0x23.  A
 * setting with a changed cell writes all its cells; a setting of cells up
 * to 0x1B is followed by a write of the balance to 0x1C.  Cells above 0x1C
 * get no balance.  When @p from has a bad checksum and no setting up to
 * 0x1B changes, one balance write ends the plan.
 *
 * D series, by setting, each with the registers the HFP-30 writes for it:
 * ID 0x32; direction 0xC2, 0xB2, 0xB0, 0x4C, 0x5E; neutral 0xC2; left end
 * 0xB2; right end 0xB0; dead band 0x4E; speed 0x54; fail-safe 0x4C; soft
 * start 0x60; overload protection 0x9C, 0x9A = 3, 0x98 = 200; smart sensor
 * 0x72 = 0x4E54, 0x6C, 0x44.  A register already written with the same
 * value is not written again.  The save and the reboot end a plan with a
 * write.
 *
 * @param from  Image the servo holds.
 * @param to    Draft.
 * @param out   Receives the plan; empty on an error.
 * @return A hitec_plan_result_t.
 */
hitec_plan_result_t hitec_plan(const hitec_image_t *from,
                               const hitec_image_t *to, hitec_plan_t *out);

/**
 * @brief The D series factory reset: 0x6E = 0x0F0F, save, reboot, as the
 *        HFP-30 writes it.
 *
 * @param out  Receives the plan; NULL does nothing.
 */
void hitec_d_factory_plan(hitec_plan_t *out);

#ifdef __cplusplus
}
#endif

#endif /* HITEC_REG_H */
