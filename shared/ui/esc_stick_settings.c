/**
 * @file esc_stick_settings.c
 * @brief ESC STICK's SETTINGS list (ui_setlist.h), opened by the footer's
 *        SETTINGS on a profile's page: the 14 settings of SET_CAT_STICK,
 *        the supply's two under SUPPLY and the twelve of the beeps'
 *        timing and detection under TIMING AND DETECTION.
 *
 * SETUP's list over the whole body: 64px rows at a 70px pitch, "-" and
 * "+" keys of 56 x 48px, the keypad on a tap on a number's value, and the
 * strip [ESC STICK] [RESET ESC STICK] [SAVE] at the footer's place.  The
 * timing heading's line says that the values are defaults, not
 * measurements.  A change applies at once, as the settings' observer
 * publishes it: the next run times its beeps by it.  SAVE asks for a write
 * of the whole store, as on SETUP; RESET ESC STICK sets these 14 to their
 * defaults and no other.  The home tag closes the list, through the leave
 * warning while something is unsaved and no save is asked for.
 *
 * The footer opens the list on a SAFE bench with no run under way, and
 * the list closes when the bench arms under it (esc_stick_panel.c).  The
 * list covers the output strip MOTOR & ESC draws; MOTOR & ESC repaints
 * whole as the list opens and closes.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel.h"

#include <string.h>

#include "motor_screen.h"
#include "settings.h"
#include "ui_setlist.h"
#include "ui_text.h"

/* Last: the layout macros of the headers it includes have short names
 * (W, H, PAD) and s names the state, which no header included after it
 * may meet. */
#include "esc_stick_panel_int.h"

static void timing_line(char *out, size_t n);

/** @brief The timing heading's line: the values are not measured. */
static const ui_setlist_custom_t k_timing_line = {
    .label = TX_SP_TIMING_TITLE, .help = TX_COUNT, .value_text = timing_line,
};

/** @brief The list's rows in their order, each section under its heading. */
static const ui_setlist_row_t k_rows[] = {
    UI_SL_ROW_HEADING(TX_SP_SET_SUPPLY, NULL),
    UI_SL_ROW_SETTING(SET_STICK_V),
    UI_SL_ROW_SETTING(SET_STICK_I),
    UI_SL_ROW_HEADING(TX_SP_TIMING_TITLE, &k_timing_line),
    UI_SL_ROW_SETTING(SET_STICK_BEEP_MIN),
    UI_SL_ROW_SETTING(SET_STICK_GAP_MIN),
    UI_SL_ROW_SETTING(SET_STICK_LONG),
    UI_SL_ROW_SETTING(SET_STICK_LONG_MAX),
    UI_SL_ROW_SETTING(SET_STICK_GROUP_GAP),
    UI_SL_ROW_SETTING(SET_STICK_ENTRY),
    UI_SL_ROW_SETTING(SET_STICK_STORE),
    UI_SL_ROW_SETTING(SET_STICK_OFF),
    UI_SL_ROW_SETTING(SET_STICK_SILENCE),
    UI_SL_ROW_SETTING(SET_STICK_TIMEOUT),
    UI_SL_ROW_SETTING(SET_STICK_THRESHOLD),
    UI_SL_ROW_SETTING(SET_STICK_HYST),
};
/** @brief The rows' count. */
#define ROW_COUNT ((int)(sizeof(k_rows) / sizeof(k_rows[0])))

/** @brief The list's state. */
static struct {
    ui_setlist_t sl;          /**< the list */
} p; /**< the list's state */

/**
 * @brief The timing heading's line.
 * @param out Takes it.
 * @param n   Its size, bytes.
 */
static void timing_line(char *out, size_t n)
{
    sp_copy(out, n, TR(SP_TIMING_NOTE));
}

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

/** @brief The strip's name: the session's, English always.
 *  @return The name. */
static const char *list_name(void) { return "ESC STICK"; }

/** @brief The list's look changed: the session repaints. */
static void list_invalidate(void) { ++pg.rev; }

/** @brief The leave warning's SAVE and DISCARD: the list closes. */
static void list_way_out(void) { sp_set_close(); }

/** @brief ESC STICK's side of its list. */
static const ui_setlist_host_t k_host = {
    .count      = rows_count,
    .row        = rows_row,
    .headings   = true,
    .name       = list_name,
    .invalidate = list_invalidate,
    .way_out    = list_way_out,
};

void sp_set_reset(void)
{
    memset(&p, 0, sizeof(p));
    ui_setlist_init(&p.sl, &k_host);
}

void sp_set_open(void)
{
    ui_setlist_open(&p.sl);
    ++pg.rev;
    motor_invalidate();
}

void sp_set_close(void)
{
    if (p.sl.open || ui_setlist_covered(&p.sl)) {
        ui_setlist_close(&p.sl);
        ++pg.rev;
        /* The output strip under the list comes back. */
        motor_invalidate();
    }
}

bool sp_set_shown(void) { return p.sl.open; }

bool esc_stick_settings_shown(void) { return p.sl.open; }

bool sp_set_back(void)
{
    if (!p.sl.open) {
        return false;
    }
    if (ui_setlist_back(&p.sl) || ui_setlist_warn(&p.sl)) {
        ++pg.rev;
        return true;
    }
    sp_set_close();
    return true;
}

void sp_set_event(const touch_event_t *evt)
{
    ui_setlist_event(&p.sl, evt);
}

void sp_set_cancel(void)
{
    ui_setlist_cancel(&p.sl);
}

void sp_set_tick(float dt_s)
{
    ui_setlist_tick(&p.sl, dt_s);
}

void sp_set_render(gfx_canvas_t *c)
{
    ui_setlist_render(&p.sl, c);
}

bool sp_set_holds(uint8_t id)
{
    return p.sl.open
           && ((p.sl.have_press && p.sl.press_id == id)
               || (p.sl.kp.open && p.sl.kp.pressed >= 0
                   && p.sl.kp.press_id == id)
               || (p.sl.tk.open && p.sl.tk.pressed >= 0
                   && p.sl.tk.press_id == id));
}

/* ------------------------------------------------------------ for tests */

const ui_keypad_t *esc_stick_panel_keypad(void)
{
    return &p.sl.kp;
}

int esc_stick_panel_keypad_setting(void)
{
    return ui_setlist_keypad_setting(&p.sl);
}

void esc_stick_settings_show(setting_id_t id)
{
    const int row = esc_stick_settings_row(id);
    if (row >= 0) {
        ui_setlist_scroll_to(&p.sl, row);
        ++pg.rev;
    }
}

const ui_setlist_t *esc_stick_settings_list(void)
{
    return &p.sl;
}

int esc_stick_settings_row(setting_id_t id)
{
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (k_rows[i].kind == UI_SL_SETTING && k_rows[i].id == id) {
            return i;
        }
    }
    return -1;
}
