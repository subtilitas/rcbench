/**
 * @file supply_settings.c
 * @brief SUPPLY's SETTINGS list (ui_setlist.h), opened by the footer's
 *        SETTINGS: the output limits, the start values, the trips and the
 *        confirmations (SET_CAT_SUPPLY), then the PD mini's wiring under its
 *        heading (SET_CAT_PDMINI) and, on a PD mini, RESET PD MINI.
 *
 * SETUP's list: 64px rows at a 70px pitch, the keypad on a tap on a
 * number's value, "<" and ">" on a switch and a choice, and the strip
 * [SUPPLY] [RESET SUPPLY] [SAVE] at the footer's place, the source the
 * output comes from under the name.  The heading of the output limits
 * carries the range the set points take now.  A change applies at once,
 * as the settings' observer publishes it; SAVE asks for a write of the
 * whole store, as on SETUP.  The home tag closes the list, through the
 * leave warning while something is unsaved and no save is asked for.
 *
 * A typed cap rounds down onto its step, so a typed 12.01V allows no more
 * than 12.00V; a trip typed above 0 stays a trip.  Any change of the PD
 * mini's wiring switches a live output off on the control task, so its four
 * rows take no press while the output is on or an ON is on its way, while a
 * programming port holds the supply and while the bench is ARMED; the
 * reason stands in their help line.  RESET SUPPLY sets the list's rows to
 * their defaults, the wiring's only while they take a press.  The footer
 * opens the list with the output off only, and the list closes when the
 * output goes on under it.
 *
 * SPDX-License-Identifier: MIT
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "supply_screen.h"
#include "ui_setlist.h"
#include "ui_text.h"

#include "supply_screen_int.h"

/** @brief The list's state. */
static struct {
    ui_setlist_t sl;          /**< the list                            */
    uint32_t     rev;         /**< bumped when the list's look changes */
} p; /**< the list's state */

/* ------------------------------------------------------------- the rows */

static void caps_line(char *out, size_t n);
static void modreset_tap(void);

/** @brief The output limits' heading: its line the range in force. */
static const ui_setlist_custom_t k_caps_line = {
    .label = TX_SUP_CAPS, .help = TX_COUNT, .value_text = caps_line,
};

/** @brief RESET PD MINI: the module restarted, its output off first. */
static const ui_setlist_custom_t k_modreset = {
    .label = TX_SUP_RESET_PDMINI, .help = TX_SUP_RESET_PDMINI_HELP,
    .tap = modreset_tap,
};

/** @brief The rows, top down; RESET PD MINI last, on a PD mini only. */
static const ui_setlist_row_t k_rows[] = {
    UI_SL_ROW_HEADING(TX_SUP_CAPS, &k_caps_line),
    UI_SL_ROW_SETTING(SET_SUPPLY_V_MAX),
    UI_SL_ROW_SETTING(SET_SUPPLY_I_MAX),
    UI_SL_ROW_HEADING(TX_SUP_AFTER_RESTART, NULL),
    UI_SL_ROW_SETTING(SET_SUPPLY_V_START),
    UI_SL_ROW_SETTING(SET_SUPPLY_I_START),
    UI_SL_ROW_HEADING(TX_SUP_TRIPS, NULL),
    UI_SL_ROW_SETTING(SET_SUPPLY_TRIP_I),
    UI_SL_ROW_SETTING(SET_SUPPLY_TRIP_V),
    UI_SL_ROW_SETTING(SET_SUPPLY_TRIP_MS),
    UI_SL_ROW_HEADING(TX_SUP_CONFIRM_ON, NULL),
    UI_SL_ROW_SETTING(SET_SUPPLY_CONFIRM_SLIDE),
    UI_SL_ROW_SETTING(SET_SUPPLY_CONFIRM_KEYS),
    UI_SL_ROW_HEADING(TX_SUP_TAB_PDMINI, NULL),
    UI_SL_ROW_SETTING(SET_PDMINI_EN),
    UI_SL_ROW_SETTING(SET_PDMINI_TX),
    UI_SL_ROW_SETTING(SET_PDMINI_RX),
    UI_SL_ROW_SETTING(SET_PDMINI_BAUD),
    UI_SL_ROW_CUSTOM(&k_modreset),
};
/** @brief The rows' count, RESET PD MINI's with them. */
#define ROW_COUNT ((int)(sizeof(k_rows) / sizeof(k_rows[0])))
/** @brief The PD MINI heading's row. */
#define PD_HEADING 13

_Static_assert(PD_HEADING < ROW_COUNT, "the PD MINI heading is a row");

/**
 * @brief The range the set points take now, as the header on LIVE says it.
 * @param out Takes the text.
 * @param n   Bytes at @p out.
 */
static void caps_line(char *out, size_t n)
{
    snprintf(out, n, "%.1f-%.1fV  %.2f-%.2fA", (double)s.eff.v_min,
             (double)s.eff.v_max, (double)s.eff.i_min, (double)s.eff.i_max);
}

/** @brief A tap on RESET PD MINI: the module restarted, its output off. */
static void modreset_tap(void)
{
    s.pending.module_reset = true;
    s.pending.off          = true;
}

/** @brief The rows: RESET PD MINI only on a PD mini, not on the model.
 *  @return The count. */
static int rows_count(void) { return s.model ? ROW_COUNT - 1 : ROW_COUNT; }

/**
 * @brief Row @p i.
 * @param i The row.
 * @return The row; a UI_SL_NONE row past the last.
 */
static ui_setlist_row_t rows_row(int i)
{
    return ui_setlist_table_row(k_rows, rows_count(), i);
}

/** @brief The strip's name: the screen's title, English always.
 *  @return The name. */
static const char *list_name(void) { return "SUPPLY"; }

/**
 * @brief Whether @p id is one of the PD mini's wiring.
 * @param id The setting.
 * @return True for SET_CAT_PDMINI's.
 */
static bool pd_row(setting_id_t id)
{
    const setting_def_t *d = settings_def(id);
    return d != NULL && d->cat == SET_CAT_PDMINI;
}

/**
 * @brief Why a row takes no press: the PD mini's rows under a lock.
 * @param id The setting.
 * @return The reason, NULL while it takes one.
 */
static const char *row_locked(setting_id_t id)
{
    if (!pd_row(id)) {
        return NULL;
    }
    switch (sup_pd_lock()) {
    case SUP_PD_LOCK_PORT:  return TR(SUP_PD_LOCK_PORT);
    case SUP_PD_LOCK_LIVE:  return TR(SUP_PD_LOCK_ON);
    case SUP_PD_LOCK_ARMED: return TR(MO_SET_SAFE_ONLY);
    case SUP_PD_FREE:
    default:                return NULL;
    }
}

/**
 * @brief A row's help line: one pin for both and a soldered pin say so,
 *        else the setting's own.
 * @param id The setting.
 * @return The line.
 */
static const char *row_help(setting_id_t id)
{
    if ((id == SET_PDMINI_TX || id == SET_PDMINI_RX)
        && settings_pdmini_same_pin()) {
        return TR(SET_PD_SAME_PIN);
    }
    if (settings_fixed(id)) {
        return TR(SET_PIN_SOLDERED);
    }
    return ui_setting_help(id);
}

/**
 * @brief A typed value onto its step the safe way: a cap rounds down, a
 *        trip typed above 0 stays a trip, never rounded to off.
 * @param id The setting.
 * @param v  The value typed.
 * @return The value to set.
 */
static float row_typed(setting_id_t id, float v)
{
    const setting_def_t *d = settings_def(id);
    if (d == NULL || !(d->step > 0.0f)) {
        return v;
    }
    if (id == SET_SUPPLY_V_MAX || id == SET_SUPPLY_I_MAX) {
        return d->min + floorf((v - d->min) / d->step + 1e-3f) * d->step;
    }
    if ((id == SET_SUPPLY_TRIP_I || id == SET_SUPPLY_TRIP_V) && v > 0.0f
        && v < d->step) {
        return d->step;
    }
    return v;
}

/**
 * @brief The keypad's range of a cap or a start value: the supply's own
 *        caps, and a start value under the operator's cap too, so a value
 *        the output cannot take is refused on the keypad.  Other rows keep
 *        the schema's.
 * @param id  The setting.
 * @param min The schema's bottom; takes the row's.
 * @param max The schema's top; takes the row's.
 */
static void row_range(setting_id_t id, float *min, float *max)
{
    float lo = *min, hi = *max;
    switch (id) {
    case SET_SUPPLY_V_MAX:   lo = s.caps.v_min; hi = s.caps.v_max; break;
    case SET_SUPPLY_I_MAX:   lo = s.caps.i_min; hi = s.caps.i_max; break;
    case SET_SUPPLY_V_START: lo = s.eff.v_min;  hi = s.eff.v_max;  break;
    case SET_SUPPLY_I_START: lo = s.eff.i_min;  hi = s.eff.i_max;  break;
    default:                 return;
    }
    /* Never wider than the schema, never empty. */
    *min = (lo > *min) ? lo : *min;
    *max = (hi < *max) ? hi : *max;
    if (*max < *min) {
        *max = *min;
    }
}

/**
 * @brief A change: the limits and the sliders follow the settings.
 * @param id The setting; SETTING_COUNT for several.
 */
static void row_changed(setting_id_t id)
{
    (void)id;
    sup_refresh_limits();
}

/** @brief RESET SUPPLY: the limits' rows, and the wiring's while they take
 *         a press. */
static void list_reset(void)
{
    setting_id_t ids[ROW_COUNT];
    int n = 0;
    const bool pd_free = sup_pd_lock() == SUP_PD_FREE;
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (k_rows[i].kind == UI_SL_SETTING
            && (pd_free || !pd_row(k_rows[i].id))) {
            ids[n++] = k_rows[i].id;
        }
    }
    settings_reset_rows(ids, n);
    sup_refresh_limits();
}

/** @brief The list's look changed. */
static void list_invalidate(void) { ++p.rev; }

/** @brief The leave warning's SAVE and DISCARD: the list closes. */
static void list_way_out(void) { sup_list_close(); }

/** @brief SUPPLY's side of its list. */
static const ui_setlist_host_t k_host = {
    .count      = rows_count,
    .row        = rows_row,
    .headings   = true,
    .name       = list_name,
    .name_line  = sup_source_line,
    .reset      = list_reset,
    .help       = row_help,
    .locked     = row_locked,
    .changed    = row_changed,
    .typed      = row_typed,
    .range      = row_range,
    .invalidate = list_invalidate,
    .way_out    = list_way_out,
};

/* ---------------------------------------------------------- the interface */

void sup_list_reset(void)
{
    memset(&p, 0, sizeof(p));
    ui_setlist_init(&p.sl, &k_host);
}

void sup_list_open(bool pdmini)
{
    ui_setlist_open(&p.sl);
    if (pdmini) {
        ui_setlist_scroll_to(&p.sl, PD_HEADING);
    }
    supply_invalidate();
}

void sup_list_close(void)
{
    if (p.sl.open || ui_setlist_covered(&p.sl)) {
        ui_setlist_close(&p.sl);
        supply_invalidate();
    }
}

bool sup_list_shown(void) { return p.sl.open; }

bool sup_list_back(void)
{
    if (!p.sl.open) {
        return false;
    }
    if (ui_setlist_back(&p.sl) || ui_setlist_warn(&p.sl)) {
        return true;
    }
    sup_list_close();
    return true;
}

void sup_list_event(const touch_event_t *evt)
{
    ui_setlist_event(&p.sl, evt);
}

void sup_list_cancel(void)
{
    ui_setlist_cancel(&p.sl);
}

void sup_list_tick(float dt_s)
{
    ui_setlist_tick(&p.sl, dt_s);
}

uint32_t sup_list_revision(void) { return p.rev; }

void sup_list_render(gfx_canvas_t *c)
{
    ui_setlist_render(&p.sl, c);
}

void sup_list_repaint(void) { ++p.rev; }

/* -------------------------------------------------------------- openers */

void supply_screen_open_pdmini(void)
{
    if (supply_screen_output_live()) {
        ui_router_set_alert(TR(FOOT_SETTINGS_ON));
        return;
    }
    sup_list_open(true);
}

void supply_screen_open_limits(void)
{
    if (supply_screen_output_live()) {
        ui_router_set_alert(TR(FOOT_SETTINGS_ON));
        return;
    }
    sup_list_open(false);
}

int supply_screen_settings_tab(void)
{
    if (!p.sl.open) {
        return -1;
    }
    const gfx_rect_t head = ui_setlist_row_rect(&p.sl, PD_HEADING);
    const gfx_rect_t pane = ui_setlist_pane(&p.sl);
    return (head.y < pane.y + pane.h / 2) ? 1 : 0;
}

bool supply_screen_settings_shown(void) { return p.sl.open; }

const ui_setlist_t *supply_settings_list(void) { return &p.sl; }

int supply_settings_row(setting_id_t id)
{
    for (int i = 0; i < rows_count(); ++i) {
        if (k_rows[i].kind == UI_SL_SETTING && k_rows[i].id == id) {
            return i;
        }
    }
    return -1;
}

int supply_settings_modreset_row(void)
{
    return s.model ? -1 : ROW_COUNT - 1;
}

int supply_screen_keypad_setting(void)
{
    return ui_setlist_keypad_setting(&p.sl);
}

const ui_keypad_t *supply_settings_keypad(void) { return &p.sl.kp; }

/* ------------------------------------------------------- the wiring's lock */

sup_pd_lock_t sup_pd_lock(void)
{
    if (s.port_held) {
        return SUP_PD_LOCK_PORT;
    }
    if (s.on || s.on_asked || s.pending.on) {
        return SUP_PD_LOCK_LIVE;
    }
    return settings_held(SET_PDMINI_EN) ? SUP_PD_LOCK_ARMED : SUP_PD_FREE;
}

/**
 * @brief The wiring's values and solder as one word: a change made outside
 *        the list (a board found, a discard) repaints it.
 * @return The word.
 */
static uint32_t pd_signature(void)
{
    static const setting_id_t k_pd[] = {
        SET_PDMINI_EN, SET_PDMINI_TX, SET_PDMINI_RX, SET_PDMINI_BAUD,
    };
    uint32_t sig = 0u;
    for (size_t i = 0; i < sizeof(k_pd) / sizeof(k_pd[0]); ++i) {
        const uint32_t v = (uint32_t)(settings_get_int(k_pd[i]) + 1)
                           | (settings_fixed(k_pd[i]) ? 0x40u : 0u);
        sig = (sig << 8) | (v & 0xFFu);
    }
    return sig;
}

void sup_pd_tick(void)
{
    const sup_pd_lock_t l = sup_pd_lock();
    const uint32_t sig = pd_signature();
    if ((uint8_t)l != s.pd_lock || sig != s.pd_sig) {
        s.pd_lock = (uint8_t)l;
        s.pd_sig  = sig;
        ++p.rev;
    }
}
