/*
 * Visual language for the bench, resolved at runtime.
 *
 * Dark, high contrast, hard edges.  The background is near-black so the four
 * telemetry colours can run at full chroma without fighting each other, and
 * every text colour sits well clear of its background rather than merely
 * "looking dark mode".  Corners are chamfered rather than rounded: a
 * 45-degree cut reads as instrument panel, a radius reads as consumer app.
 *
 * Colours are an array lookup rather than a compile-time constant, because
 * theme, brightness and contrast are settings.  The names below are unchanged
 * from when they were #defines, so drawing code did not have to move; what
 * changed is that a palette can be swapped and every screen repainted.
 *
 * Brightness scales the whole palette in 8-bit space before packing to
 * RGB565, so it does not quantise the way scaling packed colours would.
 *
 * No ESP-IDF here -- the whole UI renders on the host too, which is how it
 * gets designed without a board in front of you.
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
/** 20..100 %.  Scales the palette; see the note about this board's backlight. */
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

/* Fonts, by role rather than by size, so a face can be swapped in one place. */
#define UI_FONT_LABEL    (&gfx_font_8x16)
#define UI_FONT_HEAD     (&gfx_font_16x28)
#define UI_FONT_NUM      (&gfx_font_num_24x30)

#ifdef __cplusplus
}
#endif
