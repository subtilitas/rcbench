/**
 * @file settings_sensors.h
 * @brief SETUP > SENSORS: up to four INA3221 parts on the sensor bus, at 0x40
 *        to 0x43, and per channel its shunt and what it watches.
 *
 * Rows, in schema order: 0x40 FITTED to 0x43 FITTED, then per channel c --
 * c = 3 a + ch - 1 for part a's channel ch, 0 to 11 -- its WATCHES, its
 * SHUNT and its LIMIT.  A shunt of 0 switches the channel off; the next
 * value up is 5.0mOhm (setting_def_t.off_below), and the top is
 * 1000.0mOhm.  What a channel watches is one of NONE, OUT 1 to OUT 8,
 * PORT 1 to PORT 4 and SUPPLY: the output channels, the programming ports
 * and the supply rail.  LIMIT is the current a 50ms window's highest
 * reading may reach before the band says so, 10 to 32767mA in 10mA steps
 * on the arrows, 0 (OFF) for none: SENSE_LIMIT's LIMIT_MA (protocol 4.27),
 * an alert only.
 *
 * The map the rows make (settings_sensors_map()) is what the panel writes
 * to the IOmcu's SENSE_MAP page (protocol 4.25), and the one INA3221 frame
 * of SENSE (registers 8 to 10) it writes to an older IOmcu instead.
 *
 * Migration.  A build before SENSORS kept one INA3221 in four keys:
 * ina3221_en, ina3221_adr, ina3221_mohm and ina3221_ch.  They stay in the
 * schema, in no category and on no screen, loaded and saved as stored, so
 * a panel taken back to 0.21 finds its set-up.  A SENSORS key the store
 * does not hold is seeded from them at start-up, as a default is, until a
 * save writes it (settings_sensors_migrate()):
 *
 *   - FITTED: the old part's address, when the old INA3221 was on;
 *   - SHUNT: the old part's CH1 the old shunt, CH2 and CH3 the old shunt
 *     with CH1+2+3 and 0 (off) with CH1; every other channel 100.0mOhm;
 *   - WATCHES: CH1 of the old part -- 0x40 while it was off -- PORT n
 *     while the servo tester's target is programming port n, else OUT 1;
 *     every other channel NONE;
 *   - LIMIT: OFF, which 0.21 and the builds up to 0.21 had none of.
 *
 * A fresh panel holds no key, and the old keys' defaults (off, 0x40,
 * 100.0mOhm, CH1) seed the schema's defaults: nothing fitted, every shunt
 * 100.0mOhm, 0x40 CH1 watching OUT 1.
 *
 * Pure C, as settings.c is.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "link_pages.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The row ids: part @p a's FITTED, channel @p c's WATCHES, SHUNT
 *        and LIMIT.  @p a is 0 to 3, @p c 0 to 11.
 */
#define SET_INA_FIT(a) ((setting_id_t)(SET_INA_FIT_0 + (int)(a)))
#define SET_INA_W(c)   ((setting_id_t)(SET_INA_CH_0 + (int)(c) * SET_INA_CH_ROWS))
#define SET_INA_SH(c)  ((setting_id_t)(SET_INA_CH_0 + (int)(c) * SET_INA_CH_ROWS + 1))
/** @brief Channel @p c's LIMIT row, 0 to 11. */
#define SET_INA_LIM(c) ((setting_id_t)(SET_INA_CH_0 + (int)(c) * SET_INA_CH_ROWS + 2))

/** @brief WATCHES' options, in their order.  The last three are a board's
 *         rails: a row shows them while settings_fix() holds it there (the
 *         rbmod carrier's INA3221, ui_board_apply_pins()), and no step
 *         reaches them. */
enum {
    SENS_W_NONE    = 0,
    SENS_W_OUT_1   = 1,    /**< OUT 1 to OUT 8: 1 to 8                   */
    SENS_W_PORT_1  = 9,    /**< PORT 1 to PORT 4: 9 to 12                */
    SENS_W_SUPPLY  = 13,
    SENS_W_OFFERED = 14,   /**< the options a step goes through          */
    SENS_W_RAIL_A  = 14,   /**< servo rail A, LINK_SM_WATCH_RAIL_A        */
    SENS_W_RAIL_B  = 15,   /**< servo rail B, LINK_SM_WATCH_RAIL_B        */
    SENS_W_PROG_5V = 16,   /**< the programming ports' 5V                 */
    SENS_W_COUNT   = 17,
};

/**
 * @brief The options of an enum row a step and a stored value may take.
 * @param d a schema row.
 * @return SENS_W_OFFERED for a WATCHES row, the row's option count for any
 *         other enum (1 for none), 1 for NULL.
 */
int settings_sensors_offered(const setting_def_t *d);

_Static_assert(SET_INA_PARTS == (int)LINK_SM_PARTS
               && SET_INA_CHANNELS == (int)LINK_SM_CHANNELS,
               "SENSORS has the parts and channels SENSE_MAP has");

/** The schema row of @p id: a SENSORS row, or one of the four kept keys of
 *  0.21; NULL for any other id. */
const setting_def_t *settings_sensors_def(setting_id_t id);

/** Whether @p id is a SENSORS row. */
bool settings_sensors_row(setting_id_t id);

/** The part, 0 to 3, a SENSORS row belongs to; -1 for any other id. */
int settings_sensors_part(setting_id_t id);

/** The SENSE_MAP WATCH value of WATCHES option @p option; NONE for one out
 *  of range. */
uint16_t settings_sensors_watch_code(int option);

/**
 * @brief The WATCHES option of a SENSE_MAP WATCH value.
 * @param code a SENSE_MAP WATCH value.
 * @return the option, SENS_W_*; SENS_W_NONE for a value no option has.
 */
int settings_sensors_watch_option(uint16_t code);

/**
 * @brief Channel @p c's LIMIT as SENSE_LIMIT's LIMIT_MA takes it.
 * @param c the channel, 0 to 11.
 * @return the limit, mA, at most 32767; 0 for none and for a @p c out of
 *         range.
 */
uint16_t settings_sensors_limit_ma(int c);

/**
 * Before the store is read: every SENSORS value set to NAN, a value no
 * store holds, so settings_sensors_migrate() can tell a key the store did
 * not have.  @p values is the model's array of SETTING_COUNT.
 */
void settings_sensors_unset(float *values);

/**
 * After the store is read and its values coerced: every SENSORS value
 * still NAN seeded from the old INA3221 keys and the servo tester's
 * target, as the file comment says.  Returns how many were seeded.
 */
int settings_sensors_migrate(float *values);

/** The map the SENSORS rows make, with the frame an older IOmcu takes. */
typedef struct {
    /** SENSE_MAP registers 0 to 25 as asked. */
    uint16_t regs[LINK_SM_KEPT];
    uint8_t  parts;      /**< bit a: 0x40 + a fitted                      */
    /** Fitted parts with every shunt 0: the page refuses such a map, so
     *  none is written while this is not 0. */
    uint8_t  dark;
    uint8_t  fast;       /**< the channel read at 1kHz, LINK_SM_FAST_NONE */
    /** The part SENSE's one-part frame names, 0 to 3: FAST's, else the
     *  lowest fitted part; 0xFF while none is fitted.  On an IOmcu older
     *  than 4.25 it is the one part read. */
    uint8_t  mirror;
    /** @brief SENSE registers 8 to 10 for an IOmcu older than 4.25: the mirror
     *        part, its CH1 shunt or its first one not 0, and its channels on
     *        that shunt -- the frame has one shunt for every channel it reads,
     *        so a channel on another shunt is not read.  one_on false: no part
     *        to read; the frame then holds 0x40,
     *  100.0mOhm and CH1, values the page takes. */
    bool     one_on;
    uint8_t  one_addr;
    uint16_t one_dmohm;
    uint8_t  one_ch;
} settings_sensors_map_t;

/**
 * @brief The map the values in force make.
 *
 * FAST is the lowest channel with a shunt on a fitted part that watches
 * the servo tester's target: an output, OUT 1 to OUT 8, or the rail
 * settings_sensors_set_tester_rail() names, while the target is OUTPUTS;
 * PORT n or PROG 5V while it is programming port n (SET_SERVO_TARGET n).
 *
 * @param m Receives the map; NULL does nothing.
 */
void settings_sensors_map(settings_sensors_map_t *m);

/**
 * @brief Name the rail the servo tester's outputs are on, for FAST.
 *
 * On a board that solders an INA3221 on its servo rails (the rbmod
 * carrier) a channel watches a rail, not an output; the panel says which
 * rail holds the outputs the tester drives (ui_board_tester_rail()).
 *
 * @param code LINK_SM_WATCH_RAIL_A or LINK_SM_WATCH_RAIL_B, or
 *             LINK_SM_WATCH_NONE for none: no rail channel is FAST then.
 * @return true when the rail changed, so the map is to be published again.
 */
bool settings_sensors_set_tester_rail(uint16_t code);

/** settings_sensors_map()'s FAST: the channel watching the servo tester's
 *  target, LINK_SM_FAST_NONE for none. */
uint8_t settings_sensors_fast(void);

/** The fitted parts: bit a for 0x40 + a. */
uint8_t settings_sensors_fitted(void);

/**
 * @brief The shunt of the channel the servo rail's meter reads.
 * @return FAST's shunt, else the one-part frame's
 *         (settings_sensors_map_t), in 0.1mOhm; 0 for no part fitted
 */
uint16_t settings_sensors_meter_dmohm(void);

/**
 * @brief A channel's shunt as the map carries it.
 * @param c the channel
 * @return the shunt in 0.1mOhm; 0 off
 */
uint16_t settings_sensors_dmohm(int c);

#ifdef __cplusplus
}
#endif
