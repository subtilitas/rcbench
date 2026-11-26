/**
 * @file
 * @brief The SERVO LABEL rows of the SERVO lists: ID PREFIX, SERVO ID and
 *        NEW ID under a heading, one block on every list that carries DUT
 *        NAME (the hub's and LIVE's, TEST's, LIMIT RUN's and RUDDER
 *        AUTO's).
 *
 * | Row | Value | A press |
 * | --- | --- | --- |
 * | ID PREFIX | the prefix NEW ID uses | the name keyboard, 17 characters at most; OK asks a save at once |
 * | SERVO ID | the active servo's id, or NONE | a choice list: NONE, the card's 6 highest ids, TYPE ID |
 * | NEW ID | NEXT and the id it makes | the next id from the card, its folder, and the servo active |
 *
 * SERVO ID's NONE calls servo_active_clear(); an id of the list makes it
 * active (SERVO_ACTIVE_PICK); TYPE ID opens the name keyboard on the
 * prefix and its "-" (ui_textkey_open_servo_id()), and an id it takes
 * runs as a scanned line does (scan_flow_typed(), SERVO_ACTIVE_TYPED):
 * the same notices, the same refusal during a run, DUT NAME from a known
 * servo's newest run.  SERVO ID and NEW ID are locked while a run holds
 * the active servo (servo_screen_run_active()); NEW ID also without a
 * card, at ZZZZZ and with a prefix that is none, the reason on the row.
 *
 * What the card holds comes through servo_label_io_t, the panel's
 * (panel_servo.c's walk and NEW ID request); without it the card reads
 * as not there.  servo_label_tick() repaints the rows when the active
 * servo, the card's ids or NEW ID's request change outside the list: NEW
 * ID's late answer, a scan.  SERVO ID's open choice list keeps the ids it
 * shows; a pick takes the id drawn, refused when the card holds it no
 * more among its 6 newest.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "servo_id.h"
#include "ui_setlist.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The card's side of the rows. */
typedef struct {
    /**
     * @brief The card's ids as the last walk read them.
     * @param out Takes them.
     * @return false with no card, or a walk not read to its end.
     */
    bool (*ids)(servo_ids_t *out);
    /**
     * @brief Ask for NEW ID with @p prefix; the answer makes the servo
     *        active.
     * @param prefix ID PREFIX.
     * @return false while one is under way.
     */
    bool (*new_id)(const char *prefix);
    /**
     * @brief Whether a NEW ID asked is not answered yet.
     * @return true from the ask to its answer.
     */
    bool (*new_busy)(void);
} servo_label_io_t;

/**
 * @brief Register the card's side; NULL for none.
 * @param io The hooks; they outlive the rows.
 */
void servo_label_set_io(const servo_label_io_t *io);

/** @brief The block's rows on a list, in order. */
enum {
    SVL_ROW_HEAD = 0,  /**< the SERVO LABEL heading */
    SVL_ROW_PREFIX,    /**< ID PREFIX               */
    SVL_ROW_ID,        /**< SERVO ID                */
    SVL_ROW_NEW,       /**< NEW ID                  */
    SVL_ROWS           /**< the rows of the block   */
};

/** @brief SERVO ID's row. */
extern const ui_setlist_custom_t svl_id_row;
/** @brief NEW ID's row. */
extern const ui_setlist_custom_t svl_new_row;

/** @brief The block's rows, for a list's table: the heading, ID PREFIX,
 *         SERVO ID and NEW ID. */
#define SERVO_LABEL_ROWS                                                     \
    UI_SL_ROW_HEADING(TX_SVL_LABEL, NULL),                                   \
    { (uint8_t)UI_SL_TEXT, (int8_t)SET_TEXT_SERVO_PREFIX, (setting_id_t)0,   \
      TX_COUNT, NULL },                                                      \
    UI_SL_ROW_CUSTOM(&svl_id_row),                                           \
    UI_SL_ROW_CUSTOM(&svl_new_row)

/**
 * @brief SERVO ID's choice list as it stands: NONE, the ids, TYPE ID.
 * @return Its rows, 2 to 8.
 */
int servo_label_choices(void);

/**
 * @brief Frame task, with a SERVO list open: the rows repaint when the
 *        active servo, the card's ids or NEW ID's request changed since the
 *        last call.
 */
void servo_label_tick(void);

/**
 * @brief How many repaints servo_label_tick() asked, for the tests.
 * @return The count.
 */
uint32_t servo_label_repaints(void);

/**
 * @brief Why NEW ID takes no press now.
 * @return The reason; NULL while it takes one.
 */
const char *servo_label_new_why(void);

#ifdef __cplusplus
}
#endif
