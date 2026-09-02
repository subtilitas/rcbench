/*
 * The visual language of the bench, resolved at runtime.
 *
 * Two palettes, dark and light.  In the dark palette the background is
 * near-black so the four telemetry colours run at full chroma; in both, every
 * text colour sits well clear of its background.
 *
 * Colours are an array lookup rather than compile-time constants, because
 * theme, brightness and contrast are settings.  Brightness and contrast are
 * applied to the 8-bit palette before packing to RGB565 (16-bit red, green,
 * blue), so they do not quantise the way scaling packed colours would.
 *
 * No ESP-IDF (Espressif Internet-of-Things Development Framework): the whole
 * UI (user interface) renders on the host.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_C_BG = 0,
    UI_C_PANEL,
    UI_C_PANEL_HI,
    UI_C_PANEL_SUNK,
    UI_C_EDGE,
    UI_C_EDGE_HI,
    UI_C_GRID,
    UI_C_GRID_STRONG,
    UI_C_TEXT,
    UI_C_TEXT_DIM,
    UI_C_TEXT_FAINT,
    UI_C_TEXT_ON_LIGHT,
    UI_C_ACCENT,
    UI_C_OK,
    UI_C_WARN,
    UI_C_DANGER,
    UI_C_VOLT,
    UI_C_CURR,
    UI_C_POWER,
    UI_C_RPM,
    UI_C_LILAC,
    UI_C_TEAL,
    UI_C_COUNT
} ui_color_id_t;

typedef enum {
    UI_THEME_DARK = 0,
    UI_THEME_LIGHT,
    UI_THEME_COUNT
} ui_theme_id_t;

/** The live palette.  Indexed by ui_color_id_t; recomputed by the setters. */
extern gfx_color_t g_ui_palette[UI_C_COUNT];

static inline gfx_color_t ui_theme_color(ui_color_id_t id)
{
    return (id >= 0 && id < UI_C_COUNT) ? g_ui_palette[id] : 0;
}

/** Recompute the palette.  Call ui_router_invalidate() after. */
void ui_theme_set(ui_theme_id_t theme);
/** 20..100 %.  Scales the whole palette. */
void ui_theme_set_brightness(int percent);
/** 60..140 %.  Pushes colours away from mid-grey, or pulls them toward it. */
void ui_theme_set_contrast(int percent);

ui_theme_id_t ui_theme_get(void);
int ui_theme_brightness(void);
int ui_theme_contrast(void);

/* ---------------------------------------------------------------- surfaces */

#define UI_BG            ui_theme_color(UI_C_BG)
#define UI_PANEL         ui_theme_color(UI_C_PANEL)
#define UI_PANEL_HI      ui_theme_color(UI_C_PANEL_HI)
#define UI_PANEL_SUNK    ui_theme_color(UI_C_PANEL_SUNK)
#define UI_EDGE          ui_theme_color(UI_C_EDGE)
#define UI_EDGE_HI       ui_theme_color(UI_C_EDGE_HI)
#define UI_GRID          ui_theme_color(UI_C_GRID)
#define UI_GRID_STRONG   ui_theme_color(UI_C_GRID_STRONG)

/* -------------------------------------------------------------------- text */

#define UI_TEXT          ui_theme_color(UI_C_TEXT)
#define UI_TEXT_DIM      ui_theme_color(UI_C_TEXT_DIM)
#define UI_TEXT_FAINT    ui_theme_color(UI_C_TEXT_FAINT)
#define UI_TEXT_ON_LIGHT ui_theme_color(UI_C_TEXT_ON_LIGHT)

/* ------------------------------------------------------------------ accent */

#define UI_ACCENT        ui_theme_color(UI_C_ACCENT)
#define UI_OK            ui_theme_color(UI_C_OK)
#define UI_WARN          ui_theme_color(UI_C_WARN)
#define UI_DANGER        ui_theme_color(UI_C_DANGER)

/* ------------------------------------------------------- telemetry series */

#define UI_VOLT          ui_theme_color(UI_C_VOLT)
#define UI_CURR          ui_theme_color(UI_C_CURR)
#define UI_POWER         ui_theme_color(UI_C_POWER)
#define UI_RPM           ui_theme_color(UI_C_RPM)
#define UI_LILAC         ui_theme_color(UI_C_LILAC)
#define UI_TEAL          ui_theme_color(UI_C_TEAL)

/* ----------------------------------------------------------------- metrics */

#define UI_CHAMFER       8
#define UI_CHAMFER_SM    4

/*
 * Corner radii, one scale for the whole interface, so a card, a button and a
 * track read as the same material.  UI_CHAMFER is the 45-degree cut ui_panel
 * and the home tag use.
 */
#define UI_R_CARD  8
#define UI_R_CTL   6
#define UI_R_CHIP  4

/*
 * The bench's numeral style, in one place: the four readings and the throttle
 * share it.  Thickness is an eighth of the height; a fatter segment makes the
 * horizontals, which only span the gap between the two verticals, shorter
 * than they are thick.  The slant of 3 px is the lean of a panel meter.
 */
static inline gfx_seg_style_t ui_seg_hero(void)
{
    gfx_seg_style_t s = { .digit_w = 22, .digit_h = 32, .thickness = 4,
                          .gap = 5, .slant = 3, .ghost = true };
    return s;
}

/* Fonts by role rather than by size, so a face can be swapped in one place. */
#define UI_FONT_LABEL    (&gfx_font_8x16)
#define UI_FONT_HEAD     (&gfx_font_16x28)
#define UI_FONT_NUM      (&gfx_font_num_24x30)

#ifdef __cplusplus
}
#endif
