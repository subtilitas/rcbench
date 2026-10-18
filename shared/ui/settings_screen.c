/*
 * The settings screen: a hub of tiles, and one renderer over the whole
 * schema.
 *
 * SETUP opens on a grid of tiles, the menu's kind: one per category, and
 * one per screen of its own that SETUP reaches (OUTPUTS, PICK A PIN,
 * UPDATES).  A category's tile opens its list across the screen's width.
 * The list walks the rows shared/settings declares and draws each row by
 * its type: a stepper for a number, a toggle for a bool, a choice for an
 * enum.  A new setting is a schema row, and a new tile is a line of
 * k_tiles; nothing else here changes.
 *
 * The home tag that leaves SETUP with settings unsaved and no save asked
 * is held: a warning lists what differs from the stored values and offers
 * SAVE, DISCARD and STAY.  A tile that opens a screen of its own is not a
 * way out, and no route the application takes is held (ui_screen_t.hold).
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_screen.h"

#include <stdio.h>
#include <string.h>

#include "sense_link.h"
#include "settings.h"
#include "tone_link.h"
#include "ui_icons.h"
#include "ui_text.h"
#include "ui_widgets.h"

#define SCREEN_W 800
/* The router owns the top 48 and the home tag; this screen gets the body. */
#define SCREEN_H (480 - UI_BAND_H)
#define MAX_FBS  3

/*
 * Coordinates are in the 432 px body the router hands this screen; the band
 * and the home tag are above it.
 *
 * The hub: three columns by three rows of 256 x 108 px tiles with 8 px
 * margins, and under them the strip that holds SAVE.  The list has the same
 * strip, so SAVE stays where it is between the two.  The strip ends 4 px
 * above the router's alert band, which covers the bottom UI_ALERT_H px of
 * the body: nothing that acts sits under it.
 */
#define HUB_COLS 3
#define HUB_ROWS 3
#define HUB_M    8
#define HUB_TW   ((SCREEN_W - (HUB_COLS + 1) * HUB_M) / HUB_COLS)
#define HUB_TH   108
/* The icon's top in a tile.  Icon, name and line take 76 px from it, which
 * leaves 16 px under the line. */
#define HUB_ICON_DY 16

/* The strip under the grid and under the list: three columns, the hub's. */
#define STRIP_H  34
#define STRIP_Y  (SCREEN_H - UI_ALERT_H - 4 - STRIP_H)

/* The list: the body's width less the margins and the scroll bar, from
 * under the band to the strip. */
#define LIST_X   8
#define LIST_W   776
#define LIST_Y   6
#define LIST_H   (STRIP_Y - 6 - LIST_Y)
#define ROW_H    54
#define ROW_GAP  4
#define ROW_PITCH (ROW_H + ROW_GAP)

#define BTN_W    44
#define BTN_H    36
#define PLUS_X   (LIST_X + LIST_W - 14 - BTN_W)
#define MINUS_X  (PLUS_X - 8 - 110 - 8 - BTN_W)
#define VALUE_R  (PLUS_X - 8)

#define MAX_ROWS 32

/*
 * A "-" or "+" key is a tap: the press arms it and the release steps it
 * once.  The release counts when the same contact makes it inside the key,
 * no further than DRAG_SLOP from the press in x and in y, with no scroll
 * and no repeat in between.  The list scrolls by a drag that starts
 * anywhere on it, the keys included, so a key that stepped on the press
 * would step under every scroll that starts on one.
 *
 * A key held repeats: a 0 to 30,000 mAh range is 300 taps otherwise.  The
 * first step comes REPEAT_DELAY_S after the press, then REPEAT_SLOW_HZ, and
 * REPEAT_FAST_HZ once the key is held REPEAT_FAST_S longer.  The time runs
 * only while the contact stays on the key within the slop, and one frame
 * counts for at most UI_HOLD_MAX_CREDIT_S (0.25 s); the release of a key
 * that has repeated adds no step.
 */
#define REPEAT_DELAY_S 0.45f
#define REPEAT_SLOW_HZ 8.0f
#define REPEAT_FAST_S  1.8f
#define REPEAT_FAST_HZ 30.0f

/* A press that moves further than this is a scroll, not a tap. */
#define DRAG_SLOP 8

enum { HIT_NONE = 0, HIT_TILE, HIT_MINUS, HIT_PLUS, HIT_RESET, HIT_LIST,
       HIT_SAVE, HIT_STAY, HIT_DISCARD, HIT_LEAVE_SAVE };

/*
 * The warning that holds a way out: a card over the body down to the
 * strip, its title, up to two lines on what unsaved means, then one line
 * per change and up to two notes under them.  STAY, DISCARD and SAVE take
 * the strip's three columns, SAVE where SAVE is on the tiles.
 * SETTINGS_WARN_LINES lines of 21 px fit; more changes than that leave the
 * last line to their count.
 */
#define WARN_X      HUB_M
#define WARN_Y      HUB_M
#define WARN_W      (SCREEN_W - 2 * HUB_M)
#define WARN_H      (STRIP_Y - HUB_M - WARN_Y)
#define WARN_WHY_Y  46     /* from WARN_Y, 18 px a line */
#define WARN_LINE_Y 96
#define WARN_PITCH  21
#define WARN_NOTE_Y (WARN_LINE_Y + SETTINGS_WARN_LINES * WARN_PITCH + 4)
#define WARN_NOTE_PITCH 18

/*
 * The hub's tiles, in their order on the grid, row by row.  A category's
 * tile names the category and counts its settings; a screen's tile names
 * the screen by its title, English always, as the menu does.  The home tag
 * of a screen a tile opens leads back here (settings_screen_opens()).
 *
 * Nine fit the grid.  A tenth needs a second page, through ui_tabs in the
 * strip's free columns, which is not built: the build stops at a tenth
 * entry (the assertion under the table), and so does test_settings_pins_hub.
 */
typedef struct {
    ui_icon_fn     icon;
    int8_t         cat;      /**< the category its list shows, or -1      */
    ui_screen_id_t screen;   /**< else the screen it opens                */
    ui_text_id_t   line;     /**< the line under the name                 */
} hub_tile_t;

#define HUB_CATEGORY(cat_, icon_) \
    { (icon_), (int8_t)(cat_), SCREEN_SETUP, TX_SET_COUNT }
#define HUB_SCREEN(screen_, line_, icon_) \
    { (icon_), -1, (screen_), (line_) }

static const hub_tile_t k_tiles[] = {
    HUB_CATEGORY(SET_CAT_ESC,   ui_icon_motor),
    HUB_CATEGORY(SET_CAT_APP,   ui_icon_sliders),
    HUB_CATEGORY(SET_CAT_IFACE, ui_icon_chip),
    HUB_SCREEN(SCREEN_OUTPUTS, TX_SET_HUB_OUTPUTS, ui_icon_list),
    HUB_SCREEN(SCREEN_PICKER,  TX_SET_HUB_PICKER,  ui_icon_board),
    HUB_SCREEN(SCREEN_WIFI,    TX_SET_HUB_WIFI,    ui_icon_wifi),
    HUB_SCREEN(SCREEN_UPDATES, TX_SET_HUB_UPDATES, ui_icon_record),
};

#define TILE_COUNT ((int)(sizeof(k_tiles) / sizeof(k_tiles[0])))

_Static_assert(TILE_COUNT <= SETTINGS_HUB_SLOTS,
               "a tenth SETUP tile needs the hub's second page");
_Static_assert(SETTINGS_HUB_SLOTS == HUB_COLS * HUB_ROWS,
               "the hub's slots are its grid");
_Static_assert(HUB_M + HUB_ROWS * (HUB_TH + HUB_M) <= STRIP_Y,
               "the grid ends above the strip");
_Static_assert(LIST_Y + LIST_H <= STRIP_Y, "the list ends above the strip");
_Static_assert(STRIP_Y + STRIP_H <= SCREEN_H - UI_ALERT_H,
               "the strip ends above the alert band");
_Static_assert(WARN_NOTE_Y + 2 * WARN_NOTE_PITCH <= WARN_Y + WARN_H - 4,
               "the warning's notes end inside its card");

/* The names the warning gives the strings, which have no row on SETUP. */
static const ui_text_id_t k_text_label[SETTING_TEXT_COUNT] = {
    [SET_TEXT_DUT_NAME] = TX_SET_DUT_NAME,
};

static struct {
    /* The list of cat is open; the hub shows while this is false. */
    bool     list;
    setting_cat_t cat;
    int      scroll;
    int      scroll_max;

    /* One contact holds the screen from its DOWN to its UP; any other
     * presses nothing and ends nothing. */
    bool     have_press;
    uint8_t  press_id;

    int      hit_kind;
    int      hit_index;      /* tile index, or row index */
    bool     dragging;
    int      press_x, press_y;
    int      last_y;

    float    held_for;
    bool     repeating;

    bool     chrome_valid[MAX_FBS];
    bool     drawn_list;
    setting_cat_t drawn_cat;

    /*
     * The save button's three inputs as the last frame drew them.  The
     * write is taken by settings_save_tick() outside this screen, so a
     * pending write completing -- or being refused -- moves them with no
     * touch to invalidate the cached framebuffers.  Without this, a
     * refusal keeps drawing WHEN IDLE until something else repaints.
     */
    bool     drawn_dirty;
    bool     drawn_asked;
    bool     drawn_failed;

    /* The warning is open and holds the way out to warn_to.  drawn_* are
     * what the last frame drew of it. */
    bool     warn;
    ui_screen_id_t warn_to;
    bool     drawn_warn;
    bool     drawn_armed;
} s;

/*
 * The rows the coprocessor does not hold, as the control task last said.
 * Outside s: what the coprocessor holds does not change when the screen
 * is reset.
 */
static uint16_t s_sense_unheld;
static uint8_t  s_tone_unheld;

void settings_screen_invalidate(void)
{
    memset(s.chrome_valid, 0, sizeof(s.chrome_valid));
}

void settings_apply_ui(void)
{
    /* The language with the theme: a change repaints every screen in it
     * on the next frame, through the invalidation below. */
    ui_text_set_language((ui_lang_t)settings_get_int(SET_LANGUAGE));
    ui_theme_set((ui_theme_id_t)settings_get_int(SET_THEME));
    ui_theme_set_brightness(settings_get_int(SET_BRIGHTNESS));
    ui_theme_set_contrast(settings_get_int(SET_CONTRAST));
    ui_router_invalidate();
}

void settings_screen_set_unheld(uint16_t sense_rows, uint8_t tone_rows)
{
    if (sense_rows != s_sense_unheld || tone_rows != s_tone_unheld) {
        s_sense_unheld = sense_rows;
        s_tone_unheld  = tone_rows;
        settings_screen_invalidate();
    }
}

bool settings_screen_unheld(setting_id_t id)
{
    uint16_t sense = 0u;
    uint8_t  tone  = 0u;
    switch (id) {
    case SET_INA228_EN:    sense = SENSE_LINK_ROW_I228;        break;
    case SET_INA228_ADDR:  sense = SENSE_LINK_ROW_I228_ADDR;   break;
    case SET_INA228_UOHM:  sense = SENSE_LINK_ROW_I228_SHUNT;  break;
    case SET_INA228_MAX_A: sense = SENSE_LINK_ROW_I228_MAX;    break;
    case SET_INA3221_EN:   sense = SENSE_LINK_ROW_I3221;       break;
    case SET_INA3221_ADDR: sense = SENSE_LINK_ROW_I3221_ADDR;  break;
    case SET_INA3221_MOHM: sense = SENSE_LINK_ROW_I3221_SHUNT; break;
    case SET_INA3221_CH:   sense = SENSE_LINK_ROW_I3221_CH;    break;
    case SET_SENSE_SDA:
    case SET_SENSE_SCL:    sense = SENSE_LINK_ROW_PINS;        break;
    case SET_ENC_EN:       sense = SENSE_LINK_ROW_ENC;         break;
    case SET_TONE_EN:
    case SET_TONE_PIN:
    case SET_TONE_F_MIN:
    case SET_TONE_F_MAX:   tone = TONE_LINK_ROWS_TAP;          break;
    case SET_TONE_SPLIT:
    case SET_TONE_GAP:
    case SET_TONE_PERIODS: tone = TONE_LINK_ROWS_BEEP;         break;
    default:
        break;
    }
    return (s_sense_unheld & sense) != 0u || (s_tone_unheld & tone) != 0u;
}

static void drop_press(void);

/* ---------------------------------------------------------------- geometry */

bool settings_hub_slot(int index, gfx_rect_t *out)
{
    if (index < 0 || index >= SETTINGS_HUB_SLOTS) {
        return false;
    }
    if (out != NULL) {
        const int col = index % HUB_COLS;
        const int row = index / HUB_COLS;
        *out = gfx_rect_make(HUB_M + col * (HUB_TW + HUB_M),
                             HUB_M + row * (HUB_TH + HUB_M), HUB_TW, HUB_TH);
    }
    return true;
}

int settings_hub_tiles(void)
{
    return TILE_COUNT;
}

bool settings_screen_opens(ui_screen_id_t id)
{
    for (int i = 0; i < TILE_COUNT; ++i) {
        if (k_tiles[i].cat < 0 && k_tiles[i].screen == id) {
            return true;
        }
    }
    return false;
}

int settings_screen_category(void)
{
    return s.list ? (int)s.cat : -1;
}

static gfx_rect_t tile_rect(int i)
{
    gfx_rect_t r = gfx_rect_make(0, 0, 0, 0);
    (void)settings_hub_slot(i, &r);
    return r;
}

static int tile_at(int x, int y)
{
    for (int i = 0; i < TILE_COUNT; ++i) {
        if (gfx_rect_contains(tile_rect(i), x, y)) {
            return i;
        }
    }
    return -1;
}

/* A column of the strip, under the hub's column @p col. */
static gfx_rect_t strip_rect(int col)
{
    return gfx_rect_make(HUB_M + col * (HUB_TW + HUB_M), STRIP_Y, HUB_TW,
                         STRIP_H);
}

static gfx_rect_t reset_rect(void)
{
    return strip_rect(1);
}

static gfx_rect_t save_rect(void)
{
    return strip_rect(2);
}

/* The one condition the press, the highlight and the render all read. */
static bool save_offered(void)
{
    return settings_dirty() && !settings_save_asked();
}

/*
 * The same condition holds the home tag.  A save asked for is the operator
 * saying keep, and holds nothing; a save refused (NOT SAVED) leaves the
 * values dirty and holds it.
 */
static bool leave_held(void)
{
    return save_offered();
}

/* DISCARD changes nothing while the bench is ARMED: see release(). */
static bool bench_armed(void)
{
    return ui_router_status()->armed;
}

static gfx_rect_t stay_rect(void)
{
    return strip_rect(0);
}

static gfx_rect_t discard_rect(void)
{
    return strip_rect(1);
}

/* ------------------------------------------------------------ the warning */

/* Change @p k, in schema order and the strings after the values, into
 * @p line as "name: saved -> now", each value with its unit. */
static void change_line(int k, char *line, size_t n)
{
    for (int i = 0; i < SETTING_COUNT; ++i) {
        const setting_id_t id = (setting_id_t)i;
        if (!settings_differs(id) || k-- > 0) {
            continue;
        }
        const setting_def_t *d = settings_def(id);
        const char *unit = (d != NULL && d->unit != NULL) ? d->unit : "";
        const char *gap = (unit[0] != '\0') ? " " : "";
        char was[32], now[32];
        (void)ui_setting_value_of(id, settings_stored(id), was, sizeof(was));
        (void)ui_setting_value(id, now, sizeof(now));
        snprintf(line, n, "%s: %s%s%s -> %s%s%s", ui_setting_label(id), was,
                 gap, unit, now, gap, unit);
        return;
    }
    for (int i = 0; i < SETTING_TEXT_COUNT; ++i) {
        const setting_text_id_t id = (setting_text_id_t)i;
        if (!settings_text_differs(id) || k-- > 0) {
            continue;
        }
        snprintf(line, n, "%s: %s -> %s", ui_tr(k_text_label[i]),
                 settings_stored_text(id), settings_text(id));
        return;
    }
    line[0] = '\0';
}

bool settings_screen_warning(void)
{
    return s.warn;
}

bool settings_screen_warn_line(int i, char *line, size_t n)
{
    if (line == NULL || n == 0u) {
        return false;
    }
    line[0] = '\0';
    const int changes = settings_changes();
    const bool more = changes > SETTINGS_WARN_LINES;
    const int shown = more ? SETTINGS_WARN_LINES - 1 : changes;
    if (!s.warn || i < 0 || i > shown || (i == shown && !more)) {
        return false;
    }
    if (i == shown) {
        snprintf(line, n, TR(SET_LEAVE_MORE), changes - shown);
    } else {
        change_line(i, line, n);
    }
    return true;
}

/* The home tag leaves for @p to: held while something is unsaved. */
static bool hold(ui_screen_id_t to)
{
    drop_press();
    if (!leave_held()) {
        return false;
    }
    s.warn = true;
    s.warn_to = to;
    settings_screen_invalidate();
    return true;
}

static int row_count(void)
{
    return settings_in_category(s.cat, NULL, 0);
}

static void clamp_scroll(void)
{
    /* n rows are n*ROW_H plus the n-1 gaps between them.  A trailing gap
     * would give a 7-row category a 4 px scroll range, and a scroll indicator
     * beside a list that fits. */
    int n = row_count();
    int content = (n > 0) ? (n * ROW_H + (n - 1) * ROW_GAP) : 0;
    s.scroll_max = (content > LIST_H) ? (content - LIST_H) : 0;
    if (s.scroll < 0) { s.scroll = 0; }
    if (s.scroll > s.scroll_max) { s.scroll = s.scroll_max; }
}

/** Screen-space rect of a row, or an empty rect when it is scrolled away. */
static gfx_rect_t row_rect(int index)
{
    int y = LIST_Y + index * ROW_PITCH - s.scroll;
    return gfx_rect_make(LIST_X, y, LIST_W, ROW_H);
}

static int row_at(int x, int y)
{
    if (x < LIST_X || x >= LIST_X + LIST_W || y < LIST_Y || y >= LIST_Y + LIST_H) {
        return -1;
    }
    int idx = (y - LIST_Y + s.scroll) / ROW_PITCH;
    return (idx >= 0 && idx < row_count()) ? idx : -1;
}

static setting_id_t row_setting(int index)
{
    setting_id_t ids[MAX_ROWS];
    int n = settings_in_category(s.cat, ids, MAX_ROWS);
    return (index >= 0 && index < n) ? ids[index] : (setting_id_t)0;
}

/*
 * Whether (x, y) is on the "-" or the "+" key of row @p index: the key's
 * column over the row's own height.  The gap between two rows belongs to
 * neither key, so the keys of a column are apart and a press in a gap
 * scrolls and steps nothing.
 */
static bool on_key(int kind, int index, int x, int y)
{
    const int kx = (kind == HIT_MINUS) ? MINUS_X : PLUS_X;
    if (x < kx || x >= kx + BTN_W || row_at(x, y) != index) {
        return false;
    }
    /* A row whose value follows another setting has no keys: a press
     * there is a press on the list. */
    if (settings_derived(row_setting(index))) {
        return false;
    }
    return gfx_rect_contains(row_rect(index), x, y);
}

/* ------------------------------------------------------------------- input */

static void step_row(int index, int steps)
{
    setting_id_t id = row_setting(index);
    const setting_def_t *d = settings_def(id);
    if (!d) {
        return;
    }
    settings_adjust(id, steps);
    if (d->cat == SET_CAT_APP) {
        settings_apply_ui();      /* language, theme, brightness, contrast */
    }
    settings_screen_invalidate();
}

/* No press in progress, and no repeat running for one. */
static void drop_press(void)
{
    s.have_press = false;
    s.hit_kind  = HIT_NONE;
    s.hit_index = -1;
    s.dragging  = false;
    s.held_for  = 0.0f;
    s.repeating = false;
}

static void reset(void)
{
    memset(&s, 0, sizeof(s));
    s.cat = SET_CAT_ESC;
    s.hit_index = -1;
    clamp_scroll();
}

static void enter(void)
{
    /* SETUP opens on its tiles, from the menu and from a screen a tile
     * opened alike.  The list keeps its category and its scroll. */
    s.list = false;
    s.warn = false;
    drop_press();
    clamp_scroll();
    settings_screen_invalidate();
}

static void leave(void)
{
    /*
     * Nothing is written here.  The SAVE button asks, and the application
     * takes it when the bench is idle.  A request outlives this screen on
     * purpose: the operator has said what they want kept.
     *
     * The warning closes with any way out.  A route the application takes
     * while it shows drops the way out it held, and the values stay as
     * they are, unsaved.
     */
    s.warn = false;
    drop_press();
}

/* The home tag on a list goes back to the tiles; on the tiles it leaves,
 * through hold().  Over the warning it is STAY. */
static bool back(void)
{
    drop_press();
    if (s.warn) {
        s.warn = false;
        settings_screen_invalidate();
        return true;
    }
    if (!s.list) {
        return false;
    }
    s.list = false;
    settings_screen_invalidate();
    return true;
}

static void tick(float dt_s)
{
    if (s.drawn_dirty != settings_dirty()
        || s.drawn_asked != settings_save_asked()
        || s.drawn_failed != settings_save_failed()
        || (s.warn && s.drawn_armed != bench_armed())) {
        settings_screen_invalidate();
    }

    if (s.hit_kind != HIT_MINUS && s.hit_kind != HIT_PLUS) {
        s.held_for = 0.0f;
        s.repeating = false;
        return;
    }
    float before = s.held_for;
    /* A stalled frame is worth UI_HOLD_MAX_CREDIT_S (0.25 s) and no more,
     * so a 1 s stall steps at most 3 times at 8 Hz and 8 times at 30 Hz. */
    s.held_for += ui_hold_credit(dt_s);
    if (s.held_for < REPEAT_DELAY_S) {
        return;
    }
    float hz = (s.held_for > REPEAT_DELAY_S + REPEAT_FAST_S) ? REPEAT_FAST_HZ
                                                             : REPEAT_SLOW_HZ;
    /* One step as the delay ends, then one on each crossing of the repeat
     * period, so the rate is honest even if a frame runs long. */
    int fired_before = (before < REPEAT_DELAY_S)
                           ? 0 : 1 + (int)((before - REPEAT_DELAY_S) * hz);
    int fired_now = 1 + (int)((s.held_for - REPEAT_DELAY_S) * hz);
    for (int i = fired_before; i < fired_now; ++i) {
        step_row(s.hit_index, (s.hit_kind == HIT_PLUS) ? 1 : -1);
        s.repeating = true;
    }
}

/* A tap on tile @p index: a category's list, or a screen of its own. */
static void open_tile(int index)
{
    const hub_tile_t *t = &k_tiles[index];
    if (t->cat < 0) {
        ui_router_goto(t->screen);
        return;
    }
    if (s.cat != (setting_cat_t)t->cat) {
        s.cat = (setting_cat_t)t->cat;
        s.scroll = 0;
    }
    s.list = true;
    clamp_scroll();
    settings_screen_invalidate();
}

/* The DOWN of the contact that holds the screen: what it presses. */
static void press(const touch_event_t *evt, int x, int y)
{
    if (s.warn) {
        /* The warning covers the screen: its three buttons and nothing
         * under them.  DISCARD presses only while it is offered. */
        s.hit_kind = gfx_rect_contains(stay_rect(), x, y)   ? HIT_STAY
                     : gfx_rect_contains(save_rect(), x, y) ? HIT_LEAVE_SAVE
                     : (gfx_rect_contains(discard_rect(), x, y)
                        && !bench_armed())                  ? HIT_DISCARD
                                                            : HIT_NONE;
        if (s.hit_kind != HIT_NONE) {
            settings_screen_invalidate();
        }
        return;
    }
    if (!s.list) {
        const int tile = tile_at(x, y);
        if (tile >= 0) {
            s.hit_kind = HIT_TILE;
            s.hit_index = tile;
            settings_screen_invalidate();
            return;
        }
    } else if (gfx_rect_contains(reset_rect(), x, y)) {
        s.hit_kind = HIT_RESET;
        settings_screen_invalidate();
        return;
    }
    /*
     * Only while it is offered.  With nothing to write, a button that
     * presses says a save happened; with a write already asked for, it
     * lights a button the render draws as inert.
     */
    if (save_offered() && gfx_rect_contains(save_rect(), x, y)) {
        s.hit_kind = HIT_SAVE;
        settings_screen_invalidate();
        return;
    }

    const int row = s.list ? row_at(x, y) : -1;
    if (row >= 0) {
        /* A key is armed here and steps nothing: its release steps it, or
         * the repeat while it is held.  A DOWN the tracker made for a jump
         * arms none: a flick up the list arrives as one, and its lift on a
         * key in the next report would be a tap. */
        const bool jump = (evt->flags & TOUCH_FLAG_JUMP) != 0u;
        s.hit_index = row;
        s.hit_kind = jump                          ? HIT_LIST
                     : on_key(HIT_MINUS, row, x, y) ? HIT_MINUS
                     : on_key(HIT_PLUS, row, x, y)  ? HIT_PLUS
                                                    : HIT_LIST;
        if (s.hit_kind != HIT_LIST) {
            settings_screen_invalidate();   /* the key draws pressed */
        }
        return;
    }
    s.hit_kind = HIT_NONE;
}

/* The UP of a tap the finger made: what it activates. */
static void release(int kind, int index, bool repeated, int x, int y)
{
    if (kind == HIT_MINUS || kind == HIT_PLUS) {
        /* One step for a tap.  A key that has repeated has stepped on the
         * timer and its release adds none.  A MOVE has given the key up
         * past the slop or outside it; the release is asked again because
         * it can be the first event to carry the point. */
        int dx = x - s.press_x, dy = y - s.press_y;
        if (dx < 0) { dx = -dx; }
        if (dy < 0) { dy = -dy; }
        if (!repeated && dx <= DRAG_SLOP && dy <= DRAG_SLOP
            && on_key(kind, index, x, y)) {
            step_row(index, (kind == HIT_PLUS) ? 1 : -1);
        }
    } else if (kind == HIT_TILE && !s.list
               && gfx_rect_contains(tile_rect(index), x, y)) {
        open_tile(index);
    } else if (kind == HIT_RESET && s.list
               && gfx_rect_contains(reset_rect(), x, y)) {
        settings_reset(s.cat);
        settings_apply_ui();
    } else if (kind == HIT_SAVE && save_offered()
               && gfx_rect_contains(save_rect(), x, y)) {
        /* Asked for, not written.  The application decides when the bench
         * can afford to stop for it. */
        settings_request_save();
        settings_screen_invalidate();
    } else if (kind == HIT_STAY && s.warn
               && gfx_rect_contains(stay_rect(), x, y)) {
        /* The tiles again; the list keeps its category and its scroll. */
        s.warn = false;
        settings_screen_invalidate();
    } else if (kind == HIT_LEAVE_SAVE && s.warn
               && gfx_rect_contains(save_rect(), x, y)) {
        /* The SAVE button's request, then the way out.  leave() closes
         * the warning. */
        settings_request_save();
        ui_router_goto(s.warn_to);
    } else if (kind == HIT_DISCARD && s.warn && !bench_armed()
               && gfx_rect_contains(discard_rect(), x, y)) {
        /*
         * Every changed value back to its stored one, each by the path an
         * edit by hand takes: the observer, which sends a pin or a port to
         * the coprocessor, and settings_apply_ui() for the APPLICATION
         * rows.  Never while ARMED, asked again at the release: a bench
         * that armed while the finger was down keeps its values.  After a
         * refused save settings_discard() also asks for a write.
         */
        (void)settings_discard();
        settings_apply_ui();
        ui_router_goto(s.warn_to);
    }
}

static void event(const touch_event_t *evt)
{
    int x = evt->point.x;
    int y = evt->point.y;

    if (evt->type == TOUCH_EVENT_DOWN) {
        /* A DOWN with the id of the press held says that contact has gone
         * and its release with it: the press is this one's. */
        if (s.have_press && evt->point.id != s.press_id) {
            return;
        }
    } else if (!s.have_press || evt->point.id != s.press_id) {
        return;
    }

    switch (evt->type) {
    case TOUCH_EVENT_DOWN:
        s.have_press = true;
        s.press_id = evt->point.id;
        s.press_x = x;
        s.press_y = y;
        s.last_y = y;
        s.dragging = false;
        s.repeating = false;
        s.held_for = 0.0f;
        press(evt, x, y);
        break;

    case TOUCH_EVENT_MOVE: {
        int dy = y - s.last_y;
        s.last_y = y;
        if (s.hit_kind == HIT_NONE) {
            break;
        }
        int moved = y - s.press_y;
        if (moved < 0) { moved = -moved; }
        if (!s.dragging && moved > DRAG_SLOP &&
            (s.hit_kind == HIT_LIST || s.hit_kind == HIT_MINUS ||
             s.hit_kind == HIT_PLUS)) {
            /* Turned into a scroll: give up the button. */
            s.dragging = true;
            s.hit_kind = HIT_LIST;
            settings_screen_invalidate();
        }
        int side = x - s.press_x;
        if (side < 0) { side = -side; }
        if ((s.hit_kind == HIT_MINUS || s.hit_kind == HIT_PLUS)
            && (side > DRAG_SLOP
                || !on_key(s.hit_kind, s.hit_index, x, y))) {
            /* Off the key sideways, or out of it within the slop: the
             * press on the key ends, and the repeat with it.  The contact
             * can still scroll. */
            s.hit_kind = HIT_LIST;
            settings_screen_invalidate();
        }
        if (s.dragging) {
            s.scroll -= dy;
            clamp_scroll();
            settings_screen_invalidate();
        }
        break;
    }

    case TOUCH_EVENT_UP:
        /* A contact that scrolled activates nothing, and neither does a
         * release the finger did not make: it ends the press.  The press
         * is dropped before it acts, so what it opens starts clean. */
        if (!s.dragging && touch_event_is_tap_up(evt)) {
            const int kind = s.hit_kind;
            const int index = s.hit_index;
            const bool repeated = s.repeating;
            drop_press();
            settings_screen_invalidate();
            release(kind, index, repeated, x, y);
        } else {
            drop_press();
            settings_screen_invalidate();
        }
        break;
    }
}

/* ------------------------------------------------------------------ render */

/*
 * Four states, and the label is the whole of the feedback: nothing to write
 * (SAVED), something to write (SAVE), something asked for and waiting for a
 * moment to write it in (WHEN IDLE), and an attempt the store refused (NOT
 * SAVED).  WHEN IDLE is seen only while the bench is armed or a transfer is
 * running; disarmed, the write is taken on the next frame.  SAVE takes the
 * accent rather than the warning colour: RESET CATEGORY sits beside it and
 * two amber buttons in a row read as one control.  NOT SAVED takes the
 * danger colour, because the values on this screen are not the values the
 * next boot will load and nothing else on the panel says so.  The same
 * button on the tiles and on a list: the values are one set, not one per
 * category.
 */
static void draw_save(gfx_canvas_t *c)
{
    const bool dirty  = settings_dirty();
    const bool asked  = settings_save_asked();
    const bool failed = settings_save_failed();
    s.drawn_dirty  = dirty;
    s.drawn_asked  = asked;
    s.drawn_failed = failed;
    const char *label = !dirty ? TR(SET_SAVED)
                        : asked ? TR(SET_WHEN_IDLE)
                        : failed ? TR(SET_NOT_SAVED) : TR(SET_SAVE);
    ui_button(c, save_rect(), label,
              dirty ? (asked ? UI_WARN : failed ? UI_DANGER : UI_ACCENT)
                    : UI_PANEL_HI,
              s.hit_kind == HIT_SAVE, save_offered());
}

static void draw_hub(gfx_canvas_t *c)
{
    for (int i = 0; i < TILE_COUNT; ++i) {
        const hub_tile_t *t = &k_tiles[i];
        const bool down = (s.hit_kind == HIT_TILE && s.hit_index == i);
        if (t->cat < 0) {
            ui_tile(c, tile_rect(i), HUB_ICON_DY, t->icon,
                    ui_router_title(t->screen), ui_tr(t->line), down);
            continue;
        }
        char count[40];
        snprintf(count, sizeof(count), TR(SET_COUNT),
                 settings_in_category((setting_cat_t)t->cat, NULL, 0));
        ui_tile(c, tile_rect(i), HUB_ICON_DY, t->icon,
                ui_setting_category((setting_cat_t)t->cat), count, down);
    }
}

static void draw_row(gfx_canvas_t *c, int index)
{
    gfx_rect_t r = row_rect(index);
    if (r.y + r.h <= LIST_Y || r.y >= LIST_Y + LIST_H) {
        return;
    }
    setting_id_t id = row_setting(index);
    const setting_def_t *d = settings_def(id);
    if (!d) {
        return;
    }

    /* A value the coprocessor does not hold: the row's edge, its value
     * and one line in place of its help, in the colour of a write that
     * waits. */
    const bool unheld = settings_screen_unheld(id);
    gfx_fill_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0, UI_PANEL);
    gfx_draw_chamfer_rect_ex(c, r.x, r.y, r.w, r.h, 8, 0, 8, 0,
                             unheld ? UI_WARN : UI_EDGE);

    /* Label and help are clipped short of the keys rather than allowed to
     * run under them. */
    gfx_rect_t saved = gfx_clip_get(c);
    gfx_clip_intersect(c, gfx_rect_make(r.x, r.y, MINUS_X - 10 - r.x, r.h));
    gfx_text(c, r.x + 16, r.y + 3, ui_setting_label(id), UI_FONT_HEAD,
             UI_TEXT, 1);
    const char *help = unheld ? TR(SET_UNHELD) : ui_setting_help(id);
    if (help[0] != '\0') {
        gfx_text(c, r.x + 16, r.y + 33, help, UI_FONT_LABEL,
                 unheld ? UI_WARN : UI_TEXT_FAINT, 1);
    }
    c->clip = saved;

    /* Value, then the unit after it, both right-aligned against the + key.
     * Numbers get the big face; enum words get the small one, because
     * "BLHELI SERIAL" at 16 px a character does not fit in the gap and a
     * clipped setting name is worse than a small one. */
    char buf[32];
    ui_setting_value(id, buf, sizeof(buf));
    int ux = VALUE_R;
    if (d->unit && d->unit[0]) {
        int uw = gfx_text_width(UI_FONT_LABEL, d->unit, 1);
        gfx_text(c, VALUE_R - uw, r.y + 26, d->unit, UI_FONT_LABEL, UI_TEXT_DIM, 1);
        ux = VALUE_R - uw - 6;
    }
    bool wordy = (d->type == SET_TYPE_ENUM || d->type == SET_TYPE_BOOL);
    const gfx_font_t *vf = wordy ? UI_FONT_LABEL : UI_FONT_HEAD;
    int vw = gfx_text_width(vf, buf, 1);
    int avail = ux - (MINUS_X + BTN_W + 8);
    if (vw > avail && vf == UI_FONT_HEAD) {
        vf = UI_FONT_LABEL;
        vw = gfx_text_width(vf, buf, 1);
    }
    int vy = r.y + (ROW_H - gfx_text_height(vf, 1)) / 2;
    gfx_text(c, ux - vw, vy, buf, vf, unheld ? UI_WARN : UI_ACCENT, 1);

    if (settings_derived(id) || settings_held(id)) {
        return;                 /* shown, not set here: no keys */
    }
    bool minus_down = (s.hit_kind == HIT_MINUS && s.hit_index == index);
    bool plus_down  = (s.hit_kind == HIT_PLUS  && s.hit_index == index);
    int by = r.y + (ROW_H - BTN_H) / 2;
    ui_button(c, gfx_rect_make(MINUS_X, by, BTN_W, BTN_H),
              (d->type == SET_TYPE_BOOL || d->type == SET_TYPE_ENUM) ? "<" : "-",
              UI_EDGE_HI, minus_down, true);
    ui_button(c, gfx_rect_make(PLUS_X, by, BTN_W, BTN_H),
              (d->type == SET_TYPE_BOOL || d->type == SET_TYPE_ENUM) ? ">" : "+",
              UI_EDGE_HI, plus_down, true);
}

static void draw_list(gfx_canvas_t *c)
{
    /* The list is clipped rather than masked, so a part-scrolled row is cut
     * cleanly at the pane edge instead of drawing over the strip. */
    gfx_rect_t saved = gfx_clip_get(c);
    gfx_clip_set(c, gfx_rect_make(LIST_X, LIST_Y, LIST_W, LIST_H));
    int n = row_count();
    for (int i = 0; i < n; ++i) {
        draw_row(c, i);
    }
    c->clip = saved;

    /* Scroll position, drawn only when there is somewhere to scroll to. */
    if (s.scroll_max > 0) {
        int track_h = LIST_H;
        int knob_h = track_h * LIST_H / (n * ROW_PITCH);
        if (knob_h < 24) { knob_h = 24; }
        int knob_y = LIST_Y + (track_h - knob_h) * s.scroll / s.scroll_max;
        gfx_fill_rect(c, LIST_X + LIST_W + 4, LIST_Y, 3, track_h, UI_PANEL_SUNK);
        gfx_fill_rect(c, LIST_X + LIST_W + 4, knob_y, 3, knob_h, UI_ACCENT);
    }

    /* Which category is open, under the first column: in the heading face
     * where it fits, as the rows' labels, a longer name in the label face. */
    const gfx_rect_t nr = strip_rect(0);
    const char *name = ui_setting_category(s.cat);
    const bool big = gfx_text_width(UI_FONT_HEAD, name, 1) <= nr.w - 16;
    const gfx_font_t *nf = big ? UI_FONT_HEAD : UI_FONT_LABEL;
    gfx_text(c, LIST_X + 16, nr.y + (nr.h - gfx_text_height(nf, 1)) / 2,
             name, nf, UI_TEXT, 1);

    ui_button(c, reset_rect(), TR(SET_RESET), UI_WARN,
              s.hit_kind == HIT_RESET, true);
}

/*
 * The warning: what differs from the stored values, one line each as
 * "name: saved -> now" with the values as the list shows them, and the
 * three answers.  DISCARD is drawn inert while the bench is ARMED, and a
 * note says why; another says that the last save failed.
 */
static void draw_warning(gfx_canvas_t *c)
{
    const bool armed = bench_armed();
    s.drawn_armed  = armed;
    s.drawn_dirty  = settings_dirty();
    s.drawn_asked  = settings_save_asked();
    s.drawn_failed = settings_save_failed();

    gfx_fill_rect(c, WARN_X, WARN_Y, WARN_W, WARN_H, UI_PANEL);
    for (int i = 0; i < 3; ++i) {
        gfx_draw_rect(c, WARN_X + i, WARN_Y + i, WARN_W - 2 * i,
                      WARN_H - 2 * i, UI_WARN);
    }
    const gfx_rect_t saved = gfx_clip_get(c);
    gfx_clip_intersect(c, gfx_rect_make(WARN_X + 4, WARN_Y + 4, WARN_W - 8,
                                        WARN_H - 8));
    gfx_text(c, WARN_X + 16, WARN_Y + 12, TR(SET_LEAVE_TITLE), UI_FONT_HEAD,
             UI_WARN, 1);
    (void)ui_text_wrap(c, gfx_rect_make(WARN_X + 16, WARN_Y + WARN_WHY_Y,
                                        WARN_W - 32, 18),
                       18, TR(SET_LEAVE_WHY), UI_FONT_LABEL, UI_TEXT_DIM);
    char line[128];
    for (int i = 0; settings_screen_warn_line(i, line, sizeof(line)); ++i) {
        gfx_text(c, WARN_X + 16, WARN_LINE_Y + i * WARN_PITCH, line,
                 UI_FONT_LABEL, UI_TEXT, 1);
    }
    int note_y = WARN_NOTE_Y;
    if (s.drawn_failed) {
        gfx_text(c, WARN_X + 16, note_y, TR(SET_LEAVE_FAILED), UI_FONT_LABEL,
                 UI_DANGER, 1);
        note_y += WARN_NOTE_PITCH;
    }
    if (armed) {
        gfx_text(c, WARN_X + 16, note_y, TR(SET_LEAVE_ARMED), UI_FONT_LABEL,
                 UI_WARN, 1);
    }
    c->clip = saved;

    ui_button(c, stay_rect(), TR(SET_STAY), UI_PANEL_HI,
              s.hit_kind == HIT_STAY, true);
    ui_button(c, discard_rect(), TR(SET_DISCARD), UI_WARN,
              s.hit_kind == HIT_DISCARD, !armed);
    ui_button(c, save_rect(), TR(SET_SAVE), UI_ACCENT,
              s.hit_kind == HIT_LEAVE_SAVE, true);
}

static void render(gfx_canvas_t *c, int buffer_index)
{
    bool stale = true;
    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        stale = !s.chrome_valid[buffer_index] || s.drawn_cat != s.cat
                || s.drawn_list != s.list || s.drawn_warn != s.warn;
    }
    if (!stale) {
        return;
    }

    gfx_clear(c, UI_BG);
    if (s.warn) {
        draw_warning(c);
    } else {
        if (s.list) {
            draw_list(c);
        } else {
            draw_hub(c);
        }
        draw_save(c);
    }

    if (buffer_index >= 0 && buffer_index < MAX_FBS) {
        s.chrome_valid[buffer_index] = true;
        s.drawn_cat = s.cat;
        s.drawn_list = s.list;
        s.drawn_warn = s.warn;
    }
}

/*
 * Touch events were lost between two frames, so this screen's record of what
 * is on the glass cannot be trusted.  A hit held open acts on its release
 * and a drag owns its track id, and the GT911 reuses ids: a later contact
 * that began somewhere else would be taken for this one and press a control
 * nobody touched.  The repeat on the plus and minus keys runs on the frame
 * timer while a hit stands, so it stops here too.
 */
static void cancel(void)
{
    drop_press();
    settings_screen_invalidate();
}

static const ui_screen_t s_screen = {
    .title = "SETUP",
    .reset = reset,
    .enter = enter,
    .leave = leave,
    .tick = tick,
    .event = event,
    .cancel = cancel,
    .render = render,
    .back = back,
    .hold = hold,
};

const ui_screen_t *settings_screen(void)
{
    return &s_screen;
}

int settings_screen_scroll(int *max)
{
    if (max != NULL) {
        *max = s.scroll_max;
    }
    return s.scroll;
}
