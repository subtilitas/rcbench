/*
 * SPDX-License-Identifier: MIT
 */

#include "ui_theme.h"

typedef struct {
    uint8_t r, g, b;
} ui_rgb_t;

/*
 * Base palettes in 8-bit RGB, so brightness and contrast are applied before
 * the 5/6/5 quantisation rather than after it.
 *
 * The light theme is not the dark one inverted.  Series colours have to be
 * darkened to stay readable on white, while the accent blocks stay bright
 * because near-black ink is drawn on them in both themes.
 */
static const ui_rgb_t k_base[UI_THEME_COUNT][UI_C_COUNT] = {
    [UI_THEME_DARK] = {
        [UI_C_BG]            = {   5,   8,  13 },
        [UI_C_PANEL]         = {  20,  28,  39 },
        [UI_C_PANEL_HI]      = {  30,  41,  56 },
        [UI_C_PANEL_SUNK]    = {   8,  12,  18 },
        [UI_C_EDGE]          = {  40,  55,  74 },
        [UI_C_EDGE_HI]       = {  72, 104, 136 },
        [UI_C_GRID]          = {  32,  46,  62 },
        [UI_C_GRID_STRONG]   = {  54,  78, 102 },
        [UI_C_TEXT]          = { 228, 241, 255 },
        [UI_C_TEXT_DIM]      = { 128, 152, 176 },
        [UI_C_TEXT_FAINT]    = {  78,  98, 118 },
        [UI_C_TEXT_ON_LIGHT] = {   4,   8,  14 },
        [UI_C_ACCENT]        = {   0, 229, 255 },
        [UI_C_OK]            = {   0, 255, 163 },
        [UI_C_WARN]          = { 255, 176,   0 },
        [UI_C_DANGER]        = { 255,  61,  61 },
        [UI_C_VOLT]          = { 255, 184,  40 },
        [UI_C_CURR]          = { 255,  60, 110 },
        [UI_C_POWER]         = {   0, 214, 255 },
        [UI_C_RPM]           = { 124, 255,  79 },
        [UI_C_LILAC]         = { 186, 148, 255 },
        [UI_C_TEAL]          = {  64, 232, 196 },
    },
    [UI_THEME_LIGHT] = {
        [UI_C_BG]            = { 234, 239, 245 },
        [UI_C_PANEL]         = { 252, 253, 255 },
        [UI_C_PANEL_HI]      = { 228, 236, 246 },
        [UI_C_PANEL_SUNK]    = { 216, 225, 236 },
        [UI_C_EDGE]          = { 170, 186, 204 },
        [UI_C_EDGE_HI]       = { 104, 126, 150 },
        [UI_C_GRID]          = { 204, 215, 228 },
        [UI_C_GRID_STRONG]   = { 162, 180, 200 },
        [UI_C_TEXT]          = {  10,  18,  28 },
        [UI_C_TEXT_DIM]      = {  70,  88, 108 },
        [UI_C_TEXT_FAINT]    = { 120, 138, 158 },
        [UI_C_TEXT_ON_LIGHT] = {   4,   8,  14 },
        [UI_C_ACCENT]        = {   0, 196, 232 },
        [UI_C_OK]            = {   0, 198, 128 },
        [UI_C_WARN]          = { 246, 168,   0 },
        [UI_C_DANGER]        = { 232,  48,  48 },
        [UI_C_VOLT]          = { 196, 122,   0 },
        [UI_C_CURR]          = { 208,  24,  78 },
        [UI_C_POWER]         = {   0, 132, 186 },
        [UI_C_RPM]           = {  46, 148,  22 },
        [UI_C_LILAC]         = { 118,  80, 200 },
        [UI_C_TEAL]          = {   0, 150, 130 },
    },
};

gfx_color_t g_ui_palette[UI_C_COUNT];

static struct {
    ui_theme_id_t theme;
    int brightness;
    int contrast;
    bool built;
} s = { UI_THEME_DARK, 100, 100, false };

static int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static uint8_t apply_one(uint8_t value, int contrast, int brightness)
{
    /* Contrast pivots on mid-grey: above 100 pushes away from it, below 100
     * pulls toward it.  Then brightness scales what is left. */
    int v = (int)value;
    v = 128 + ((v - 128) * contrast) / 100;
    v = (v * brightness) / 100;
    return (uint8_t)clampi(v, 0, 255);
}

static void rebuild(void)
{
    const ui_rgb_t *base = k_base[s.theme];
    for (int i = 0; i < UI_C_COUNT; ++i) {
        g_ui_palette[i] = GFX_RGB(apply_one(base[i].r, s.contrast, s.brightness),
                                  apply_one(base[i].g, s.contrast, s.brightness),
                                  apply_one(base[i].b, s.contrast, s.brightness));
    }
    s.built = true;
}

/* The palette has to exist before the first draw even if nobody called a
 * setter, so every accessor makes sure it has been built once. */
static void ensure(void)
{
    if (!s.built) {
        rebuild();
    }
}

void ui_theme_set(ui_theme_id_t theme)
{
    if (theme < 0 || theme >= UI_THEME_COUNT) {
        return;
    }
    s.theme = theme;
    rebuild();
}

void ui_theme_set_brightness(int percent)
{
    s.brightness = clampi(percent, 20, 100);
    rebuild();
}

void ui_theme_set_contrast(int percent)
{
    s.contrast = clampi(percent, 60, 140);
    rebuild();
}

ui_theme_id_t ui_theme_get(void)
{
    ensure();
    return s.theme;
}

int ui_theme_brightness(void)
{
    ensure();
    return s.brightness;
}

int ui_theme_contrast(void)
{
    ensure();
    return s.contrast;
}

/* Built at load time on the host and at startup on the device, so no draw can
 * ever read a zeroed palette. */
__attribute__((constructor))
static void ui_theme_ctor(void)
{
    rebuild();
}
