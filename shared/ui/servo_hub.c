/**
 * @file
 * @brief The SERVO hub: SETUP's grid of tiles, one for each function screen,
 * and the footer with the servo profile's SETTINGS list.
 *
 * Eight tiles of 328 x 130px at x 10, 348 and 686, y 70, 210 and 350 on
 * the panel, in the order of use: LIVE, SWEEP, TEST, then LIMIT RUN,
 * RUDDER, RUDDER AUTO, then KST and HITEC; the ninth slot stays free.  A
 * tile shows its function's name and a line of at most 31 cells of 10 x 20.
 * KST's and HITEC's tile is grey while the IOmcu does not report the
 * capability their port needs, or while the other programmer holds the
 * port (prog_reg_state()); a tap on a grey tile names the reason in the
 * alert band and opens nothing.
 *
 * The footer: SERVO with the port line under it -- what the tester drives,
 * in the warning colour with no target -- the middle slot empty, the hub
 * driving nothing, and SETTINGS, which opens the profile's list
 * (servo_lists.c).  The profile's editors open over the list's left part,
 * as on a function screen.
 *
 * The hub keeps the bench SAFE: a function screen's leave disarms it.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "hitec_panel.h"
#include "kst_panel.h"
#include "prog_registry.h"
#include "ui_footer.h"
#include "ui_help.h"
#include "ui_icons.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* Last: the layout macros of the headers it includes have short names
 * (W, H, PAD), which no header included after it may meet. */
#include "servo_screen_core_int.h"

/** @brief The hub's grid: SETUP's (settings_screen_int.h). */
#define HUB_M  10   /**< the margin around and between tiles, px */
#define HUB_TW 328  /**< a tile's width, px */
#define HUB_TH 130  /**< a tile's height, px */
#define HUB_ICON_DY 14 /**< the icon box's top in a tile, px */
#define HUB_ICON 36    /**< the icon box's side, px */
#define HUB_NAME_DY 60 /**< the name's top, px */
#define HUB_LINE_DY 94 /**< the line's top, px */
/** @brief The tiles shown: eight of the nine slots. */
#define HUB_TILES SERVO_HUB_TILES
_Static_assert(HUB_TILES == SERVO_FN_COUNT - 1, "a tile per function");

_Static_assert(3 * HUB_TW + 4 * HUB_M == W, "three tiles fill the width");
_Static_assert(HUB_M + 3 * (HUB_TH + HUB_M) <= UI_FOOTER_Y,
               "the third row ends above the footer");

/** @brief Each tile's line, LIVE first. */
static const ui_text_id_t k_line[HUB_TILES] = {
    TX_SVH_LIVE_LINE,   TX_SVH_SWEEP_LINE,  TX_SVH_TEST_LINE,
    TX_SVH_LIMRUN_LINE, TX_SVH_RUDDER_LINE, TX_SVH_RAUTO_LINE,
    TX_SVH_KST_LINE,    TX_SVH_HITEC_LINE,
};

/** @brief Each tile's icon, LIVE first. */
static void (*const k_icon[HUB_TILES])(gfx_canvas_t *, int, int, int,
                                       gfx_color_t) = {
    ui_icon_servo, ui_icon_chart, ui_icon_list,  ui_icon_sliders,
    ui_icon_servo, ui_icon_record, ui_icon_chip, ui_icon_chip,
};

gfx_rect_t servo_hub_tile_rect(int i)
{
    if (i < 0 || i >= HUB_TILES) {
        return (gfx_rect_t){ 0, 0, 0, 0 };
    }
    return (gfx_rect_t){ (int16_t)(HUB_M + (i % 3) * (HUB_TW + HUB_M)),
                         (int16_t)(HUB_M + (i / 3) * (HUB_TH + HUB_M)),
                         HUB_TW, HUB_TH };
}

/**
 * @brief The registry's row of @p fn's programmer.
 * @param fn  KST or HITEC.
 * @param out Takes the row's index.
 * @return false for every other function, or with no such row.
 */
static bool prog_row_of(servo_fn_t fn, size_t *out)
{
    const prog_page_set_t *want = (fn == SERVO_FN_KST) ? &kst_panel_pages
                                  : (fn == SERVO_FN_HITEC)
                                      ? &hitec_panel_pages
                                      : NULL;
    size_t n = 0;
    const prog_reg_row_t *rows = prog_registry(&n);
    for (size_t i = 0; want != NULL && i < n; ++i) {
        if (rows[i].page_set == want) {
            *out = i;
            return true;
        }
    }
    return false;
}

bool servo_hub_tile_refused(int i, char *why, size_t n)
{
    if (why != NULL && n > 0u) {
        why[0] = '\0';
    }
    size_t row = 0;
    if (i < 0 || i >= HUB_TILES || !prog_row_of((servo_fn_t)(i + 1), &row)) {
        return false;
    }
    size_t count = 0;
    const prog_reg_row_t *rows = prog_registry(&count);
    const uint16_t caps = ui_router_status()->capabilities;
    if (prog_reg_state(rows, count, row, caps) == PROG_ROW_LIVE) {
        return false;
    }
    char line[64];
    (void)prog_reg_detail(rows, count, row, caps, line, sizeof(line));
    if (why != NULL && n > 0u) {
        snprintf(why, n, "%s", line);
    }
    return true;
}

/**
 * @brief Tile @p i: SETUP's look, grey with the reason as its line while
 *        its function is refused.
 * @param c the canvas
 * @param i the tile
 */
static void draw_tile(gfx_canvas_t *c, int i)
{
    const gfx_rect_t r = servo_hub_tile_rect(i);
    char why[64];
    const bool refused = servo_hub_tile_refused(i, why, sizeof(why));
    const bool down = s.fp_pressed == FP_TILE && s.fp_tile == i;
    gfx_fill_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CARD,
                        refused ? ui_button_off_fill()
                        : down  ? ui_theme_color(UI_C_PANEL_HI)
                                : ui_theme_color(UI_C_PANEL));
    gfx_draw_round_rect(c, r.x, r.y, r.w, r.h, UI_R_CARD,
                        ui_theme_color(UI_C_EDGE));
    k_icon[i](c, r.x + r.w / 2, r.y + HUB_ICON_DY + HUB_ICON / 2, HUB_ICON,
              refused ? ui_theme_color(UI_C_TEXT_FAINT)
                      : ui_theme_color(UI_C_ACCENT));
    const gfx_rect_t inner = { (int16_t)(r.x + 8), r.y, (int16_t)(r.w - 16),
                               r.h };
    const gfx_rect_t saved = gfx_clip_get(c);
    gfx_clip_intersect(c, inner);
    gfx_text_in(c, (gfx_rect_t){ inner.x, (int16_t)(r.y + HUB_NAME_DY),
                                 inner.w, 24 },
                svs_fn_name((servo_fn_t)(i + 1)), &gfx_font_12x24,
                refused ? ui_theme_color(UI_C_TEXT_FAINT)
                        : ui_theme_color(UI_C_TEXT), 1, GFX_ALIGN_CENTER);
    gfx_text_in(c, (gfx_rect_t){ inner.x, (int16_t)(r.y + HUB_LINE_DY),
                                 inner.w, 20 },
                refused ? why : ui_tr(k_line[i]), SV_FONT_LABEL,
                refused ? ui_theme_color(UI_C_WARN)
                        : ui_theme_color(UI_C_TEXT_DIM), 1,
                GFX_ALIGN_CENTER);
    c->clip = saved;
}

/**
 * @brief What the hub shows, as one number: a change repaints it.
 * @return the signature
 */
static uint32_t hub_sig(void)
{
    uint32_t sig = s.fp_rev * 2654435761u;
    sig = sig * 31u + s.ctrl_rev;
    sig = sig * 31u + (uint32_t)s.drive_target * 257u + s.drive_pin
          + (s.drive_pwm ? 65536u : 0u) + (uint32_t)s.pins;
    sig = sig * 31u + ui_router_status()->capabilities;
    sig = sig * 31u + (prog_reg_servo_holds() ? 1u : 0u);
    sig = sig * 31u + (s.pins_known ? 1u : 0u);
    return sig;
}

void hub_render(gfx_canvas_t *c, int buf)
{
    if (modal_open()) {
        /* A profile editor over the list's left part: the keypad, the
         * choice list, the warning. */
        const uint32_t rev = s.ctrl_rev + s.kp.revision + s.tk.revision
                             + s.warn.rev + s.ask.rev + 0x5a5a5a5au;
        if (s.drawn_hub[buf] != rev) {
            s.drawn_hub[buf] = rev;
            gfx_clear(c, ui_theme_color(UI_C_BG));
            draw_overlay(c);
        }
        return;
    }
    if (svs_list_shown()) {
        const uint32_t rev = s.sl_rev + s.sl.kp.revision + s.sl.tk.revision
                             + s.ctrl_rev + 0x3c3c3c3cu;
        if (s.drawn_hub[buf] != rev) {
            s.drawn_hub[buf] = rev;
            ui_setlist_render(&s.sl, c);
        }
    } else {
        const uint32_t rev = hub_sig();
        if (s.drawn_hub[buf] != rev || (s.drawn_mask & (1u << buf)) == 0) {
            s.drawn_hub[buf] = rev;
            s.drawn_mask |= 1u << buf;
            gfx_clear(c, ui_theme_color(UI_C_BG));
            for (int i = 0; i < HUB_TILES; ++i) {
                draw_tile(c, i);
            }
            draw_footer(c);
        }
    }
    /* The profile warning closes as its hold fires: HOLD TO APPLY flashes
     * where it was until the flash ends and the hub repaints whole. */
    if (!s.warn.open && ui_hold_flashing(&s.warn.hold)) {
        const gfx_rect_t saved = gfx_clip_get(c);
        gfx_clip_intersect(c, warn_apply_rect());
        draw_warn_apply(c);
        c->clip = saved;
        s.drawn_hub[buf] = 0u;
    }
}

/**
 * @brief The tile under a point.
 * @param x the point's x @param y its y
 * @return the tile, -1 off every tile
 */
static int tile_at(int x, int y)
{
    for (int i = 0; i < HUB_TILES; ++i) {
        if (gfx_rect_contains(servo_hub_tile_rect(i), x, y)) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief A tap released on tile @p i: its function screen, or the reason
 *        it is grey in the alert band.
 * @param i the tile
 */
static void tile_open(int i)
{
    char why[64];
    if (servo_hub_tile_refused(i, why, sizeof(why))) {
        ui_action_refused(why);
        return;
    }
    ui_router_goto(servo_fn_screen_id((servo_fn_t)(i + 1)));
}

void hub_event(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;
    /* A profile editor over the left part: as on a function screen. */
    if (s.ov_have && evt->point.id == s.ov_id
        && evt->type != TOUCH_EVENT_DOWN) {
        ov_rest(evt);
        return;
    }
    if (modal_open()) {
        if (evt->type == TOUCH_EVENT_DOWN && !s.ov_have
            && gfx_rect_contains(overlay_area(), x, y)) {
            ov_down(evt);
        }
        return;
    }
    if (svs_list_shown()) {
        ui_setlist_event(&s.sl, evt);
        return;
    }
    if (evt->type == TOUCH_EVENT_DOWN) {
        if (s.fp_pressed != FP_NONE) {
            return;             /* one contact at a time */
        }
        const int t = tile_at(x, y);
        if (t >= 0) {
            s.fp_pressed = FP_TILE;
            s.fp_tile = (int8_t)t;
        } else if (gfx_rect_contains(ui_footer_rect(UI_FOOTER_SETTINGS), x,
                                     y)) {
            s.fp_pressed = FP_SETTINGS;
        } else {
            return;
        }
        s.fp_id = evt->point.id;
        ++s.fp_rev;
        return;
    }
    if (s.fp_pressed == FP_NONE || evt->point.id != s.fp_id
        || evt->type != TOUCH_EVENT_UP) {
        return;
    }
    const uint8_t was = s.fp_pressed;
    const int tile = s.fp_tile;
    s.fp_pressed = FP_NONE;
    s.fp_tile = -1;
    ++s.fp_rev;
    if (!touch_event_is_tap_up(evt)) {
        return;
    }
    if (was == FP_TILE && tile_at(x, y) == tile) {
        tile_open(tile);
    } else if (was == FP_SETTINGS
               && gfx_rect_contains(ui_footer_rect(UI_FOOTER_SETTINGS), x,
                                    y)) {
        svs_list_open();
    }
}

/* ------------------------------------------------------------ the guide */

/** @brief The tiles' page: the dock on the right over the third column,
 *         the first column's tiles and the port line in view.
 *  @param p The page. */
static void page_tiles(ui_help_page_t *p)
{
    p->title = TX_HG_SVH_TITLE_TILES;
    p->side = UI_HELP_DOCK_RIGHT;
    p->armed_ok = true;
    (void)ui_help_add(p, svs_fn_name(SERVO_FN_LIVE), TX_HG_SVH_DRIVE,
                      servo_hub_tile_rect(0));
    (void)ui_help_add(p, svs_fn_name(SERVO_FN_LIMRUN), TX_HG_SVH_MEASURE,
                      servo_hub_tile_rect(3));
    (void)ui_help_add(p, svs_fn_name(SERVO_FN_RUDDER), TX_HG_SVH_SURFACE,
                      servo_hub_tile_rect(4));
    (void)ui_help_add(p, svs_fn_name(SERVO_FN_KST), TX_HG_SVH_PROGRAM,
                      servo_hub_tile_rect(6));
    (void)ui_help_add(p, "SERVO", TX_HG_SVH_PORT,
                      ui_footer_rect(UI_FOOTER_NAME));
}

/** @brief The second page: a tile's states and SETTINGS, the dock on the
 *         left.  @param p The page. */
static void page_footer(ui_help_page_t *p)
{
    p->title = TX_HG_SVH_TITLE_FOOTER;
    p->side = UI_HELP_DOCK_LEFT;
    p->armed_ok = true;
    (void)ui_help_add(p, TR(HG_SVH_TILE_NAME), TX_HG_SVH_TILE,
                      servo_hub_tile_rect(5));
    (void)ui_help_add(p, TR(FOOT_SETTINGS), TX_HG_SVH_SETTINGS,
                      ui_footer_rect(UI_FOOTER_SETTINGS));
}

int hub_help_count(void)
{
    if (modal_open()) {
        return 0;
    }
    if (svs_list_shown()) {
        return ui_setlist_help_count(&s.sl);
    }
    return 2;
}

bool hub_help_page(int i, ui_help_page_t *out)
{
    if (svs_list_shown()) {
        return ui_setlist_help_page(&s.sl, i, out);
    }
    if (i < 0 || i >= hub_help_count()) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (i == 0) {
        page_tiles(out);
    } else {
        page_footer(out);
    }
    return true;
}
