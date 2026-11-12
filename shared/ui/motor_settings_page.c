/**
 * @file
 * @brief MOTOR & ESC's SETTINGS list (ui_setlist.h): the ESC / BENCH category's
 * 8 rows (SET_CAT_ESC) under PACK, MOTOR and THROTTLE OUTPUT, opened by the
 * footer's SETTINGS.  The rows are the settings SETUP listed before they
 * moved here: one home each, the same label, help and value text.
 *
 * SETUP's list: 64px rows at a 70px pitch, "-" and "+" keys of 56 x 48px,
 * the keypad on a tap on a number's value, and the strip [MOTOR & ESC]
 * [RESET MOTOR & ESC] [SAVE] at the footer's place.  The line under the
 * name says what the throttle drives.  SAVE asks for a write of the whole
 * store, as on SETUP; RESET MOTOR & ESC sets these 8 rows to their
 * defaults and no other.  The home tag closes the list, through the leave
 * warning while something is unsaved and no save is asked for.
 *
 * Idle pulse and Full pulse are the throttle's endpoints, which the IOmcu
 * (the RP2350 board) takes only on a disarmed bench.  They take a press
 * only while the bench is SAFE and ARM is not held (motor_screen.c's
 * mo_endpoints_free()); otherwise their help line says so, and a keypad
 * open for one closes.  The footer opens the list on a SAFE bench only.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "motor_screen.h"
#include "settings.h"
#include "ui_setlist.h"
#include "ui_text.h"

/* Last: its layout macros have short names. */
#include "motor_screen_int.h"

/** @brief The list's rows in their order, each section under its heading. */
static const ui_setlist_row_t k_rows[] = {
    UI_SL_ROW_HEADING(TX_MO_SET_PACK, NULL),
    UI_SL_ROW_SETTING(SET_PACK_CELLS),
    UI_SL_ROW_SETTING(SET_PACK_MAH),
    UI_SL_ROW_SETTING(SET_PACK_MOHM),
    UI_SL_ROW_HEADING(TX_MO_SET_MOTOR, NULL),
    UI_SL_ROW_SETTING(SET_MOTOR_POLES),
    UI_SL_ROW_SETTING(SET_MOTOR_KV),
    UI_SL_ROW_HEADING(TX_MO_SET_THROTTLE, NULL),
    UI_SL_ROW_SETTING(SET_OUT_MIN_US),
    UI_SL_ROW_SETTING(SET_OUT_MAX_US),
    UI_SL_ROW_SETTING(SET_OUT_RAMP),
};
/** @brief The rows' count. */
#define ROW_COUNT ((int)(sizeof(k_rows) / sizeof(k_rows[0])))

/** @brief The list's state. */
static struct {
    ui_setlist_t sl;          /**< the list                               */
    uint32_t     rev;         /**< bumped when the list's look changes    */
    bool         safe;        /**< the endpoints take a press             */
} p; /**< the list's state */

/** @brief The rows' count. @return ROW_COUNT */
static int rows_count(void) { return ROW_COUNT; }

/**
 * @brief Row @p i of k_rows.
 * @param i The row.
 * @return The row.
 */
static ui_setlist_row_t rows_row(int i)
{
    return ui_setlist_table_row(k_rows, ROW_COUNT, i);
}

/** @brief The strip's name: the screen's title, English always.
 *  @return The name. */
static const char *list_name(void) { return "MOTOR & ESC"; }

/**
 * @brief The throttle's endpoints, which change on a SAFE bench only.
 * @param id The setting.
 * @return Why its row takes no press, NULL while it takes one.
 */
static const char *row_locked(setting_id_t id)
{
    if ((id == SET_OUT_MIN_US || id == SET_OUT_MAX_US) && !p.safe) {
        return TR(MO_SET_SAFE_ONLY);
    }
    return NULL;
}

/** @brief The list's look changed. */
static void list_invalidate(void) { ++p.rev; }

/** @brief The leave warning's SAVE and DISCARD: the list closes. */
static void list_way_out(void) { msp_close(); }

/** @brief MOTOR & ESC's side of its list. */
static const ui_setlist_host_t k_host = {
    .count      = rows_count,
    .row        = rows_row,
    .headings   = true,
    .name       = list_name,
    .name_line  = mo_target_line,
    .locked     = row_locked,
    .invalidate = list_invalidate,
    .way_out    = list_way_out,
};

void msp_reset(void)
{
    memset(&p, 0, sizeof(p));
    ui_setlist_init(&p.sl, &k_host);
    p.safe = true;
}

void msp_open(void)
{
    ui_setlist_open(&p.sl);
}

void msp_close(void)
{
    if (p.sl.open || ui_setlist_covered(&p.sl)) {
        ui_setlist_close(&p.sl);
    }
}

bool msp_shown(void) { return p.sl.open; }

void msp_cancel(void)
{
    ui_setlist_cancel(&p.sl);
}

bool msp_back(void)
{
    if (!p.sl.open) {
        return false;
    }
    if (ui_setlist_back(&p.sl) || ui_setlist_warn(&p.sl)) {
        return true;
    }
    msp_close();
    return true;
}

uint32_t msp_revision(void) { return p.rev; }

const ui_setlist_t *msp_list(void) { return &p.sl; }

void msp_tick(float dt_s, bool safe)
{
    if (p.safe != safe) {
        /* The endpoints' rows change their look, and a keypad open for one
         * closes in the tick: what was typed would not reach the IOmcu
         * until it disarms. */
        p.safe = safe;
        ++p.rev;
    }
    ui_setlist_tick(&p.sl, dt_s);
}

void msp_event(const touch_event_t *evt, bool safe)
{
    p.safe = safe;
    ui_setlist_event(&p.sl, evt);
}

void msp_render(gfx_canvas_t *c)
{
    ui_setlist_render(&p.sl, c);
}

/* ------------------------------------------------------------ for tests */

int motor_screen_keypad_setting(void)
{
    return ui_setlist_keypad_setting(&p.sl);
}

const ui_keypad_t *motor_screen_keypad(void)
{
    return &p.sl.kp;
}

int motor_settings_rows(setting_id_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (k_rows[i].kind != UI_SL_SETTING) {
            continue;
        }
        if (out != NULL && n < max) {
            out[n] = k_rows[i].id;
        }
        ++n;
    }
    return n;
}

int motor_settings_list_row(int row)
{
    int n = 0;
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (k_rows[i].kind == UI_SL_SETTING && n++ == row) {
            return i;
        }
    }
    return -1;
}

int motor_settings_heading(int row)
{
    for (int i = motor_settings_list_row(row); i >= 0; --i) {
        if (k_rows[i].kind == UI_SL_HEADING) {
            return (int)k_rows[i].heading;
        }
    }
    return -1;
}

gfx_rect_t motor_settings_box(int row)
{
    const int at = motor_settings_list_row(row);
    if (at < 0) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    return ui_setlist_row_part(&p.sl, at, UI_SL_PART_VALUE);
}

const ui_setlist_t *motor_settings_list(void)
{
    return &p.sl;
}

const char *motor_endpoints_alert(endpoints_block_t why)
{
    switch (why) {
    case ENDPOINTS_BLOCK_OWED:    return TR(ALERT_ARM_ENDPOINTS);
    case ENDPOINTS_BLOCK_UNKNOWN: return TR(ALERT_ARM_RANGE_UNKNOWN);
    case ENDPOINTS_BLOCK_ORDER:   return TR(ALERT_ARM_RANGE_ORDER);
    case ENDPOINTS_BLOCK_REFUSED: return TR(ALERT_ARM_RANGE_REFUSED);
    case ENDPOINTS_BLOCK_DIFFERS: return TR(ALERT_ARM_RANGE_DIFFERS);
    case ENDPOINTS_BLOCK_NONE:
    default:                      return NULL;
    }
}
