/*
 * SPDX-License-Identifier: MIT
 */

#include "ui_band.h"

#include <stdio.h>
#include <string.h>

#include "link_pages.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define PANEL_W 800

/*
 * STOP is 132 px wide at the right edge of the band, sized for a thumb rather
 * than for its label.  It is the only control in the band, so no other control
 * can be hit on the way to it.
 */
#define STOP_W 132
#define STOP_M 6

gfx_rect_t ui_band_stop_rect(void)
{
    const gfx_rect_t r = {
        (int16_t)(PANEL_W - STOP_W - STOP_M),
        (int16_t)STOP_M,
        (int16_t)STOP_W,
        (int16_t)(UI_BAND_H - 2 * STOP_M),
    };
    return r;
}

const char *ui_band_mode(bool link_up)
{
    return link_up ? "BENCH" : "SIM";
}

#ifdef RCBENCH_TOUCH_INJECTOR
const char *ui_band_mode_inject(void)
{
    return "INJECT";
}
#endif

const char *ui_band_armed_label(uint8_t roles)
{
    switch (roles & (UI_ARMED_MOTOR | UI_ARMED_SERVO)) {
    case UI_ARMED_MOTOR: return "ARMED MOTOR";
    case UI_ARMED_SERVO: return "ARMED SERVO";
    default:             return "ARMED";
    }
}

bool ui_link_refused_label(const ui_bench_status_t *st, char *out, size_t n)
{
    if (out == NULL || n == 0u) {
        return false;
    }
    out[0] = '\0';
    if (st == NULL || !st->link_up || st->link_refused == 0u) {
        return false;
    }
    snprintf(out, n, TR(LINK_REFUSED), (unsigned long)st->link_refused);
    return true;
}

/* ---------------------------------------------------------------- chips */

/* The chip's label for @p st, in @p buf where it is formatted. */
static const char *chip_label(const ui_bench_status_t *st,
                              ui_band_chip_t chip, char *buf, size_t n)
{
    switch (chip) {
    case UI_BAND_CHIP_CLOCK:
        ui_clock(buf, n, st->run_seconds);
        return buf;
    case UI_BAND_CHIP_ARMED:
        return st->armed ? ui_band_armed_label(st->armed_roles)
                         : TR(BAND_SAFE);
    case UI_BAND_CHIP_FAULT:
        snprintf(buf, n, TR(BAND_FAULT), (unsigned)st->faults);
        return buf;
    case UI_BAND_CHIP_MODE:
        return (st->mode != NULL) ? st->mode : "";
    case UI_BAND_CHIP_LINK:
        return st->link_up ? "LINK" : TR(BAND_NO_LINK);
    default:
        return "";
    }
}

static bool chip_shown(const ui_bench_status_t *st, ui_band_chip_t chip)
{
    switch (chip) {
    case UI_BAND_CHIP_CLOCK: return st->run_seconds > 0 || st->armed;
    case UI_BAND_CHIP_ARMED: return true;
    case UI_BAND_CHIP_FAULT: return st->faults != 0;
    case UI_BAND_CHIP_MODE:  return st->mode != NULL;
    case UI_BAND_CHIP_LINK:  return true;
    default:                 return false;
    }
}

/*
 * Chips are laid out right to left from STOP, each placed at the right edge
 * the previous one leaves, so no chip needs another's width in advance and a
 * long mode string cannot push the clock under the ARMED badge.  One layout
 * for drawing and for hit-testing, so a tap finds the chip that is drawn.
 */
static const ui_band_chip_t k_order[] = {
    UI_BAND_CHIP_CLOCK, UI_BAND_CHIP_ARMED, UI_BAND_CHIP_FAULT,
    UI_BAND_CHIP_MODE, UI_BAND_CHIP_LINK,
};

typedef struct {
    bool       on[UI_BAND_CHIP_COUNT];
    gfx_rect_t r[UI_BAND_CHIP_COUNT];
} chips_t;

static void chips_layout(const ui_bench_status_t *st, chips_t *l)
{
    memset(l, 0, sizeof(*l));
    int right = PANEL_W - STOP_W - STOP_M - 12;
    for (size_t i = 0; i < sizeof(k_order) / sizeof(k_order[0]); ++i) {
        const ui_band_chip_t chip = k_order[i];
        if (!chip_shown(st, chip)) {
            continue;
        }
        char buf[24];
        const char *label = chip_label(st, chip, buf, sizeof(buf));
        /* LINK alone carries a dot. */
        const int w = 14 + gfx_text_width(&gfx_font_8x16, label, 1) + 14
                      + (chip == UI_BAND_CHIP_LINK ? 14 : 0);
        l->on[chip] = true;
        l->r[chip]  = (gfx_rect_t){ (int16_t)(right - w), 10, (int16_t)w,
                                    (int16_t)(UI_BAND_H - 20) };
        right = right - w - 8;
    }
}

bool ui_band_chip_rect(const ui_bench_status_t *st, ui_band_chip_t chip,
                       gfx_rect_t *out)
{
    if (st == NULL || chip <= UI_BAND_CHIP_NONE
        || chip >= UI_BAND_CHIP_COUNT) {
        return false;
    }
    chips_t l;
    chips_layout(st, &l);
    if (l.on[chip] && out != NULL) {
        *out = l.r[chip];
    }
    return l.on[chip];
}

ui_band_chip_t ui_band_chip_at(const ui_bench_status_t *st, int x, int y)
{
    if (st == NULL || y < 0 || y >= UI_BAND_H) {
        return UI_BAND_CHIP_NONE;
    }
    chips_t l;
    chips_layout(st, &l);
    for (int chip = UI_BAND_CHIP_NONE + 1; chip < UI_BAND_CHIP_COUNT; ++chip) {
        if (l.on[chip] && x >= l.r[chip].x - 4
            && x < l.r[chip].x + l.r[chip].w + 4) {
            return (ui_band_chip_t)chip;
        }
    }
    return UI_BAND_CHIP_NONE;
}

/* ----------------------------------------------------------- the bubble */

/*
 * The bubble is the DELETE question's box of LOGS: a panel with its border
 * and a title tab, the text in the label face.  It opens 4 px under the
 * band, 8 px in from the panel's edges at most.  A bubble is 520 px wide,
 * 61 cells of text; FAULT's spans the panel, 94 cells, so the seven bits
 * and their advice fit above the alert strip in either language.
 */
#define HELP_W       520
#define HELP_M       8
#define HELP_GAP     4
#define HELP_PAD     16     /* text from the bubble's sides            */
#define HELP_TOP     30     /* the first line from the top: tab and 10 */
#define HELP_BOTTOM  10
#define HELP_PITCH   18
#define HELP_PARA    4      /* more between two paragraphs             */

/* One paragraph, and one line of it.  Static: the bubble is drawn by the
 * render loop alone, and the render task's stack is budgeted. */
static char s_para[640];
static char s_line[400];

/* The link_fault_t bits by name.  `never` marks a bit no firmware of this
 * release sets: STATUS's bitmap is written in one place on the
 * coprocessor, from safety_gate_status() and the flash store, which set
 * LINK_SILENT, HEARTBEAT and STORE_OFF and nothing else. */
static const struct {
    uint16_t     bit;
    const char  *name;
    ui_text_id_t text;
    bool         never;
} k_bits[] = {
    { LINK_FAULT_LINK_SILENT, "LINK_SILENT", TX_BH_F_LINK_SILENT, false },
    { LINK_FAULT_OVERCURRENT, "OVERCURRENT", TX_BH_F_OVERCURRENT, true  },
    { LINK_FAULT_OVERTEMP,    "OVERTEMP",    TX_BH_F_OVERTEMP,    true  },
    { LINK_FAULT_STALL,       "STALL",       TX_BH_F_STALL,       true  },
    { LINK_FAULT_HEARTBEAT,   "HEARTBEAT",   TX_BH_F_HEARTBEAT,   false },
    { LINK_FAULT_VERSION,     "VERSION",     TX_BH_F_VERSION,     true  },
    { LINK_FAULT_STORE_OFF,   "STORE_OFF",   TX_BH_F_STORE_OFF,   false },
};

/*
 * Paragraph @p i of the FAULT bubble into s_para: one per bit that is set,
 * in bit order, then the bits this panel has no name for, then the advice
 * when a bit no firmware of this release sets is among them.
 */
static bool fault_para(uint16_t faults, int i)
{
    int  n    = 0;
    bool odd  = false;
    uint16_t known = 0u;
    for (size_t b = 0; b < sizeof(k_bits) / sizeof(k_bits[0]); ++b) {
        known = (uint16_t)(known | k_bits[b].bit);
        if ((faults & k_bits[b].bit) == 0u) {
            continue;
        }
        odd = odd || k_bits[b].never;
        if (n++ == i) {
            snprintf(s_para, sizeof(s_para), "%s (%02X): %s%s%s",
                     k_bits[b].name, (unsigned)k_bits[b].bit,
                     ui_tr(k_bits[b].text), k_bits[b].never ? " " : "",
                     k_bits[b].never ? TR(BH_F_NEVER) : "");
            return true;
        }
    }
    const uint16_t other = (uint16_t)(faults & (uint16_t)~known);
    if (other != 0u) {
        odd = true;
        if (n++ == i) {
            snprintf(s_para, sizeof(s_para), TR(BH_F_OTHER),
                     (unsigned)other);
            return true;
        }
    }
    if (odd && n == i) {
        snprintf(s_para, sizeof(s_para), "%s", TR(BH_F_FLASH));
        return true;
    }
    return false;
}

/* Paragraph @p i of @p chip's bubble into s_para, or false past the last. */
static bool help_para(const ui_bench_status_t *st, ui_band_chip_t chip,
                      int i)
{
    ui_text_id_t ids[3];
    int n = 0;
    switch (chip) {
    case UI_BAND_CHIP_FAULT:
        return fault_para(st->faults, i);
    case UI_BAND_CHIP_LINK:
        ids[n++] = st->link_up ? TX_BH_LINK_UP : TX_BH_NO_LINK;
        /* The frames the coprocessor refused, as MOTOR & ESC's header strip
         * has them, while the link is up and there are any. */
        if (i == n && ui_link_refused_label(st, s_para, sizeof(s_para))) {
            return true;
        }
        break;
    case UI_BAND_CHIP_MODE:
        /* The two modes ui_band_mode() names and INJECT; another string
         * has no text, and its bubble is its title. */
        if (st->mode != NULL && strcmp(st->mode, ui_band_mode(true)) == 0) {
            ids[n++] = TX_BH_MODE_BENCH;
        } else if (st->mode != NULL
                   && strcmp(st->mode, ui_band_mode(false)) == 0) {
            ids[n++] = TX_BH_MODE_SIM;
        }
#ifdef RCBENCH_TOUCH_INJECTOR
        else if (st->mode != NULL
                 && strcmp(st->mode, ui_band_mode_inject()) == 0) {
            ids[n++] = TX_BH_MODE_INJECT;
            ids[n++] = TX_BH_MODE_INJECT_LOST;
        }
#endif
        break;
    case UI_BAND_CHIP_ARMED:
        if (!st->armed) {
            ids[n++] = TX_BH_SAFE;
            break;
        }
        switch (st->armed_roles & (UI_ARMED_MOTOR | UI_ARMED_SERVO)) {
        case UI_ARMED_MOTOR: ids[n++] = TX_BH_ARMED_MOTOR; break;
        case UI_ARMED_SERVO: ids[n++] = TX_BH_ARMED_SERVO; break;
        default:             ids[n++] = TX_BH_ARMED_ALL;   break;
        }
        ids[n++] = TX_BH_ARMED_REST;
        if (!st->link_up) {
            ids[n++] = TX_BH_ARMED_SIM;
        }
        break;
    case UI_BAND_CHIP_CLOCK:
        ids[n++] = TX_BH_CLOCK;
        break;
    default:
        break;
    }
    if (i < 0 || i >= n) {
        return false;
    }
    snprintf(s_para, sizeof(s_para), "%s", ui_tr(ids[i]));
    return true;
}

/* The bytes of the line that starts @p s, at most @p cells: broken at the
 * last space that falls in it, or inside a word where none does. */
static size_t line_len(const char *s, int cells)
{
    if (gfx_text_cells(s) <= cells) {
        return strlen(s);
    }
    const size_t max = gfx_text_prefix(s, cells);
    for (size_t i = max; i > 0u; --i) {
        if (s[i] == ' ') {
            return i;
        }
    }
    return max;
}

/*
 * The bubble's text from (@p x, @p y), each paragraph broken into lines of
 * @p cells.  Draws when @p c is not NULL; returns the height either way, so
 * the bubble is measured by the code that draws it.
 */
static int help_body(gfx_canvas_t *c, const ui_bench_status_t *st,
                     ui_band_chip_t chip, int x, int y, int cells)
{
    int h = 0;
    for (int i = 0; help_para(st, chip, i); ++i) {
        if (i > 0) {
            h += HELP_PARA;
        }
        const char *at = s_para;
        while (*at == ' ') {
            ++at;
        }
        while (*at != '\0') {
            const size_t len = line_len(at, cells);
            if (c != NULL) {
                const size_t k = (len < sizeof(s_line)) ? len
                                                        : sizeof(s_line) - 1u;
                memcpy(s_line, at, k);
                s_line[k] = '\0';
                gfx_text(c, x, y + h, s_line, UI_FONT_LABEL, UI_TEXT, 1);
            }
            h += HELP_PITCH;
            at += len;
            while (*at == ' ') {
                ++at;
            }
        }
    }
    return h;
}

static int help_cells(int w)
{
    return (w - 2 * HELP_PAD) / gfx_text_width(UI_FONT_LABEL, "M", 1);
}

bool ui_band_help_rect(const ui_bench_status_t *st, ui_band_chip_t chip,
                       gfx_rect_t *out)
{
    gfx_rect_t at;
    if (!ui_band_chip_rect(st, chip, &at)) {
        return false;
    }
    const int w = (chip == UI_BAND_CHIP_FAULT) ? PANEL_W - 2 * HELP_M
                                               : HELP_W;
    int x = at.x;
    if (x + w > PANEL_W - HELP_M) {
        x = PANEL_W - HELP_M - w;
    }
    if (x < HELP_M) {
        x = HELP_M;
    }
    const int h = HELP_TOP + help_body(NULL, st, chip, 0, 0, help_cells(w))
                  + HELP_BOTTOM;
    if (out != NULL) {
        *out = (gfx_rect_t){ (int16_t)x, (int16_t)(UI_BAND_H + HELP_GAP),
                             (int16_t)w, (int16_t)h };
    }
    return true;
}

void ui_band_help_render(gfx_canvas_t *c, const ui_bench_status_t *st,
                         ui_band_chip_t chip)
{
    gfx_rect_t r;
    if (c == NULL || !ui_band_help_rect(st, chip, &r)) {
        return;
    }
    gfx_color_t accent = ui_theme_color(UI_C_ACCENT);
    if (chip == UI_BAND_CHIP_FAULT) {
        accent = ui_theme_color(UI_C_WARN);
    } else if (chip == UI_BAND_CHIP_LINK) {
        accent = st->link_up ? ui_theme_color(UI_C_OK)
                             : ui_theme_color(UI_C_DANGER);
    } else if (chip == UI_BAND_CHIP_ARMED && st->armed) {
        accent = ui_theme_color(UI_C_DANGER);
    }
    char buf[24];
    ui_panel(c, r, chip_label(st, chip, buf, sizeof(buf)), accent);
    (void)help_body(c, st, chip, r.x + HELP_PAD, r.y + HELP_TOP,
                    help_cells(r.w));
}

/* ----------------------------------------------------------- the band */

void ui_band_render(gfx_canvas_t *c, const char *title, bool show_home,
                    const ui_bench_status_t *st, bool stop_pressed,
                    bool home_pressed)
{
    gfx_fill_rect(c, 0, 0, PANEL_W, UI_BAND_H, ui_theme_color(UI_C_PANEL));
    ui_rule(c, 0, UI_BAND_H - 1, PANEL_W, ui_theme_color(UI_C_EDGE));

    if (show_home && title != NULL) {
        ui_home_tag(c, title, home_pressed);
    } else if (title != NULL) {
        ui_wordmark(c, title, ui_theme_color(UI_C_TEXT));
    }

    /* STOP is drawn on every screen that carries the band and is always
     * live. */
    ui_button(c, ui_band_stop_rect(), "STOP", ui_theme_color(UI_C_DANGER),
              stop_pressed, true);

    if (st == NULL) {
        return;
    }

    chips_t l;
    chips_layout(st, &l);
    for (size_t i = 0; i < sizeof(k_order) / sizeof(k_order[0]); ++i) {
        const ui_band_chip_t chip = k_order[i];
        if (!l.on[chip]) {
            continue;
        }
        char buf[24];
        const char *label = chip_label(st, chip, buf, sizeof(buf));
        gfx_color_t dot  = 0;
        gfx_color_t fill = ui_theme_color(UI_C_PANEL_SUNK);
        switch (chip) {
        case UI_BAND_CHIP_ARMED:
            /* ARMED is a filled badge rather than a coloured dot, so the
             * armed state is readable by shape at arm's length, not by
             * colour alone.  It names the role an arm covers when it
             * covers one: an arm on MOTOR leaves the servos without a
             * pulse, and one on SERVO the motor. */
            if (st->armed) {
                fill = ui_theme_color(UI_C_DANGER);
            }
            break;
        case UI_BAND_CHIP_FAULT:
            fill = ui_theme_color(UI_C_WARN);
            break;
        case UI_BAND_CHIP_LINK:
            dot = st->link_up ? ui_theme_color(UI_C_OK)
                              : ui_theme_color(UI_C_DANGER);
            break;
        default:
            break;
        }
        ui_pill(c, l.r[chip], label, dot, fill);
    }
}
