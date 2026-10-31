/**
 * @file
 * @brief The screen router: the band, the alert band, and the touches between
 * them and the screens.
 *
 * SPDX-License-Identifier: MIT
 */

#include "busfault_screen.h"
#include "coproc_update_screen.h"
#include "files_screen.h"
#include "layout_screen.h"
#include "outputs_screen.h"
#include "picker_screen.h"
#include "supply_screen.h"
#include "upd_screen.h"
#include "wifi_screen.h"
#include "ui_screen.h"
#include "ui_shot.h"
#include "shot_hold.h"

#include <stdio.h>
#include <string.h>

#include "log_viewer_screen.h"
#include "motor_screen.h"
#include "analyser_screen.h"
#include "balance_screen.h"
#include "battery_screen.h"
#include "servo_screen.h"
#include "settings.h"
#include "settings_screen.h"
#include "overview_screen.h"
#include "splash_screen.h"
#include "ui_band.h"
#include "ui_help.h"
#include "ui_theme.h"
#include "ui_watermark.h"
#include "ui_widgets.h"

/** @brief The panel's width, UI_PANEL_W px. */
#define PANEL_W UI_PANEL_W
/** @brief The panel's height, UI_PANEL_H px. */
#define PANEL_H UI_PANEL_H
/** @brief The alert band's height, across the bottom. */
#define ALERT_H UI_ALERT_H
/** @brief What the band's empty part keeps from the home tag and from the
 *         chips: 8px, four times what a chip takes beside it for a tap. */
#define SHOT_GAP_M 8

static struct {
    ui_screen_id_t    current;
    /* The screen that opened the one on top through k_openers, for its home
     * tag; SCREEN_COUNT when it was entered any other way. */
    ui_screen_id_t    opener;
    uint32_t          nav_count;      /**< navigations since init           */
    uint32_t          dispatched;     /**< events handed to a screen        */
    ui_bench_status_t status;
    char              alert[UI_ALERT_MAX];
    bool              has_alert;
    bool              alert_notice;   /**< the passing alert is a notice    */
    float             alert_age_s;
    bool              alert_fresh;    /**< set since the last tick          */
    uint32_t          alert_gen;      /**< counts alerts set                */
    /* The held alert, apart from the passing one: a passing alert shows over
     * it and, once cleared, shows it again. */
    char              held[UI_ALERT_MAX];
    bool              has_held;
    bool              stop_latched;
    bool              demo_seen;      /**< Demo mode as the last frame saw it;
                                           a flip repaints every screen    */

    /* A press that began on the alert band, by track id, held until its
     * release even if the alert clears meanwhile: the screen beneath never
     * saw the press, so it must not see the release either. */
    bool     alert_press;
    uint8_t  alert_id;
    uint32_t alert_press_gen;   /**< the alert the press began on         */
    /* Every contact that came down on the alert band, owner or not, so a
     * second finger's events reach no screen either. */
    uint8_t  alert_ids[256u / 8u];

    /* Which contact owns a press on the band.  The GT911 reports up to five,
     * and every event carries its track id, so a second finger or a resting
     * palm cannot steal the release the first one is waiting for. */
    bool    band_press;
    uint8_t band_id;
    bool    on_stop;      /**< the press started on STOP rather than home   */
    ui_band_chip_t on_chip; /**< or on this chip of the band              */
    bool    on_home;      /**< or on the home tag                         */
    bool    on_guide;     /**< or on "?" with the guide closed, to open it  */

    /*
     * The screen guide (ui_help.h): open or not, the page it shows, the
     * page set it opened on (ui_help_set_key()), and what each framebuffer
     * holds of its dock, as for the band.  A press on PREV, NEXT or CLOSE
     * is held by track id until its release, which acts on the button it
     * came down on.
     */
    bool          guide;        /**< the guide is open                    */
    int           guide_page;   /**< its page, from 0                     */
    uint32_t      guide_set;    /**< ui_help_set_key() at the opening     */
    uint32_t      guide_key[2]; /**< ui_help_dock_key() of each buffer's dock */
    uint8_t       guide_drawn;  /**< the buffers whose guide_key holds    */
    uint32_t      guide_marks;  /**< ui_help_marks_key() of the last frame */
    bool          guide_marked; /**< guide_marks holds                    */
    bool          guide_press;  /**< a press on a button of the dock      */
    uint8_t       guide_id;     /**< its track id                         */
    ui_help_hit_t guide_btn;    /**< its button                           */

    /*
     * The chip whose help bubble is open, and what the last frame drew of
     * it.  Screens cache their chrome, so a bubble that closes, moves or
     * changes size leaves pixels behind unless every screen repaints.
     */
    ui_band_chip_t help;
    bool           help_drawn;
    gfx_rect_t     help_rect;

    /*
     * What each framebuffer holds of the band: ui_band_key() of the band
     * it last drew, valid where band_drawn has the buffer's bit.  The band
     * is drawn whole in 102,288 pixel visits SAFE and 114,281 ARMED
     * (test_frame_work) and changes once a second while the bench runs,
     * so a frame repaints it only when it changed in that buffer.
     */
    uint32_t band_key[2];   /**< ui_band_key() of each buffer's band */
    uint8_t  band_drawn;    /**< the buffers whose band_key holds, a bit each */

    /*
     * The contacts the screen on top owns: each one whose DOWN it was
     * handed, with the last point it was handed, in panel coordinates.  A
     * screen is handed a MOVE or an UP only for a contact in this table, so
     * a contact has one owner from its DOWN to its UP.  Every other contact
     * is the router's and reaches no screen.
     */
    struct {
        bool    live;
        uint8_t id;
        int16_t x, y;
    } owned[TOUCH_MAX_POINTS];

    /** @brief The bench is idle (shot_gate.h), as the application last said;
     *         false until it says so. */
    bool     shot_idle;
} s;

static bool has_band(ui_screen_id_t id);
static void release_owned(void);
static void drop_notice(void);
static void guide_check(void);
static void shot_service(void);
static void guide_close(void);

/*
 * The openers, each on a row of its own: a control on one screen that opens
 * another, whose home tag then returns to the opener.  OUTPUTS opens from the
 * TARGET rows of MOTOR & ESC and SERVO and from SETUP's OUTPUTS tile; SUPPLY
 * and SETUP from SERVO's caps and AS5600 buttons; WI-FI from UPDATES' button
 * and LOGS' WI-FI button.  SETUP's WI-FI tile needs no row: WI-FI's parent is
 * SETUP.
 * A screen with no row here, or entered from a screen not on its row, leaves
 * to its tile's parent (ui_router_parent()).
 */
static const struct {
    ui_screen_id_t screen;
    ui_screen_id_t opener;
} k_openers[] = {
    { SCREEN_OUTPUTS, SCREEN_MOTOR },
    { SCREEN_OUTPUTS, SCREEN_SERVO },
    { SCREEN_OUTPUTS, SCREEN_SETUP },
    { SCREEN_SUPPLY,  SCREEN_SERVO },
    { SCREEN_SETUP,   SCREEN_SERVO },
    { SCREEN_WIFI,    SCREEN_UPDATES },
    { SCREEN_WIFI,    SCREEN_LOGS },
};

static bool opens(ui_screen_id_t opener, ui_screen_id_t id)
{
    for (size_t i = 0; i < sizeof(k_openers) / sizeof(k_openers[0]); ++i) {
        if (k_openers[i].screen == id && k_openers[i].opener == opener) {
            return true;
        }
    }
    return false;
}

static const ui_screen_t *screen_for(ui_screen_id_t id)
{
    switch (id) {
    case SCREEN_SPLASH:   return splash_screen();
    case SCREEN_OVERVIEW: return overview_screen();
    case SCREEN_MOTOR:    return motor_screen();
    case SCREEN_SERVO:    return servo_screen();
    case SCREEN_ANALYSER: return analyser_screen();
    case SCREEN_BALANCE:  return balance_screen();
    case SCREEN_BATTERY:  return battery_screen();
    case SCREEN_LOGS:     return log_viewer_screen();
    case SCREEN_SETUP:    return settings_screen();
    case SCREEN_OUTPUTS:  return outputs_view_screen();
    case SCREEN_SUPPLY:   return supply_screen();
    case SCREEN_BUSFAULT: return busfault_screen();
    case SCREEN_WIFI:     return wifi_screen();
    case SCREEN_UPDATE:   return update_screen();
    case SCREEN_COPROC_UPDATE: return coproc_update_screen();
    case SCREEN_UPDATES:  return upd_screen();
    case SCREEN_UPD_REMIND: return upd_remind_screen();
    case SCREEN_FILES:    return files_screen();
    case SCREEN_LAYOUT:   return layout_screen();
    default:              return overview_screen();
    }
}

const char *ui_router_title(ui_screen_id_t id)
{
    const ui_screen_t *s_ = screen_for(id);
    return (s_ != NULL && s_->title != NULL) ? s_->title : "";
}

void ui_router_init(void)
{
    memset(&s, 0, sizeof(s));
    s.opener = SCREEN_COUNT;
    for (int id = 0; id < SCREEN_COUNT; ++id) {
        const ui_screen_t *scr = screen_for((ui_screen_id_t)id);
        if (scr != NULL && scr->reset != NULL) {
            scr->reset();
        }
    }
    s.current = SCREEN_SPLASH;
    const ui_screen_t *scr = screen_for(s.current);
    if (scr != NULL && scr->enter != NULL) {
        scr->enter();
    }
    ui_shot_reset();
    shot_hold_reset();
    ui_router_invalidate();
}

void ui_router_goto(ui_screen_id_t id)
{
    if (id < 0 || id >= SCREEN_COUNT || id == s.current) {
        return;
    }
    const ui_screen_t *old = screen_for(s.current);
    if (old != NULL && old->leave != NULL) {
        old->leave();
    }
    /* Holds for this one entry: a tile, a strip's tab or a route of the
     * application that enters the screen later finds no opener. */
    s.opener = opens(s.current, id) ? s.current : SCREEN_COUNT;
    s.current = id;
    ++s.nav_count;
    const ui_screen_t *now = screen_for(id);
    if (now != NULL && now->enter != NULL) {
        now->enter();
    }
    /* A press that began on the old screen must not land on the new one:
     * the contacts it owned are nobody's until they lift.  The help bubble
     * is the old screen's too. */
    s.band_press = false;
    s.help = UI_BAND_CHIP_NONE;
    s.guide = false;
    s.guide_press = false;
    s.alert_press = false;
    shot_hold_end();
    memset(s.alert_ids, 0, sizeof(s.alert_ids));
    memset(s.owned, 0, sizeof(s.owned));
    ui_router_invalidate();
    shot_service();
}

ui_screen_id_t ui_router_current(void) { return s.current; }

bool ui_screen_not_covered(void) { return false; }

ui_screen_id_t ui_router_parent(ui_screen_id_t id)
{
    if (s.opener != SCREEN_COUNT && opens(s.opener, id)) {
        return s.opener;
    }
    return settings_screen_opens(id) ? SCREEN_SETUP : SCREEN_OVERVIEW;
}
uint32_t ui_router_navigations(void) { return s.nav_count; }
uint32_t ui_router_dispatched(void) { return s.dispatched; }

void ui_router_invalidate(void)
{
    s.band_drawn = 0u;
    s.guide_drawn = 0u;
    ui_shot_invalidate();
    splash_invalidate();
    overview_invalidate();
    motor_invalidate();
    log_viewer_invalidate();
    settings_screen_invalidate();
    analyser_invalidate();
    balance_invalidate();
    battery_invalidate();
    servo_invalidate();
    outputs_screen_invalidate();
    picker_screen_invalidate();
    busfault_screen_invalidate();
    coproc_update_screen_invalidate();
    supply_invalidate();
    wifi_screen_invalidate();
    update_screen_invalidate();
    upd_screen_invalidate();
    files_screen_invalidate();
    layout_screen_invalidate();
}

bool ui_router_shot_gap(gfx_rect_t *out)
{
    if (!has_band(s.current)) {
        return false;
    }
    const gfx_rect_t tag = ui_home_tag_rect(ui_router_title(s.current));
    int right = ui_band_guide_rect().x;
    for (int chip = UI_BAND_CHIP_NONE + 1; chip < UI_BAND_CHIP_COUNT; ++chip) {
        gfx_rect_t r;
        if (ui_band_chip_rect(&s.status, (ui_band_chip_t)chip, &r)
            && r.x < right) {
            right = r.x;
        }
    }
    const int left = tag.x + tag.w + SHOT_GAP_M;
    right -= SHOT_GAP_M;
    if (right <= left) {
        return false;
    }
    if (out != NULL) {
        *out = gfx_rect_make(left, 0, right - left, UI_BAND_H);
    }
    return true;
}

static void clear_alert(void)
{
    if (s.has_alert) {
        /* Screens cache what they drew per framebuffer, and the band was
         * drawn over it; without a repaint its red would stay. */
        ui_router_invalidate();
    }
    s.has_alert   = false;
    s.alert_notice = false;
    s.alert_fresh = false;
    s.alert_age_s = 0.0f;
    s.alert[0]    = '\0';
}

/**
 * @brief Clear the band's notice, if one shows: for a STOP and a switch to
 *        SAFE, which outrank a hold button's hint.  An alert stays.
 */
static void drop_notice(void)
{
    if (s.has_alert && s.alert_notice) {
        clear_alert();
    }
}

/**
 * @brief Nothing is up on the screen on top: no question, no box, no list, no
 *        keypad.  A screen that cannot say counts as covered: those with no
 *        band, the IOmcu's question and the release reminder.
 * @return true when a popup may stand over it
 */
static bool shot_screen_free(void)
{
    const ui_screen_t *scr = screen_for(s.current);
    return scr != NULL && scr->covered != NULL && !scr->covered();
}

/**
 * @brief The popup against the bench and the screen.  It opens with no
 *        contact on the glass; opening ends what the screen and the guide
 *        hold, and closing repaints the screen.
 */
static void shot_service(void)
{
    const bool may = s.shot_idle && shot_screen_free();
    const ui_shot_change_t c =
        ui_shot_service(may, may && shot_hold_glass_clear());
    if (c == UI_SHOT_OPENED) {
        shot_hold_end();
        s.help = UI_BAND_CHIP_NONE;
        s.guide = false;
        s.guide_press = false;
        release_owned();
        drop_notice();
        ui_router_invalidate();
    } else if (c == UI_SHOT_CLOSED) {
        ui_router_invalidate();
    }
}

void ui_router_shot_set_idle(bool idle)
{
    s.shot_idle = idle;
    shot_service();
}

/**
 * @brief Close the guide or the bubble a hold began over, when the hold says.
 */
static void shot_over_settle(void)
{
    if (shot_hold_settled()) {
        s.help = UI_BAND_CHIP_NONE;
        guide_close();
    }
}

void ui_router_tick(float dt_s)
{
    /* The frame an alert arrives in does not count: its interval passed
     * before the alert was set, and after a long stall it would clear the
     * alert before it was ever drawn. */
    if (s.alert_fresh) {
        s.alert_fresh = false;
    } else if (s.has_alert && dt_s > 0.0f) {
        s.alert_age_s += dt_s;
        if (s.alert_age_s >= (s.alert_notice ? UI_NOTICE_SHOW_S
                                             : UI_ALERT_SHOW_S)) {
            clear_alert();
        }
    }
    shot_hold_tick(dt_s);
    shot_over_settle();
    ui_shot_poll();
    shot_service();
    const ui_screen_t *scr = screen_for(s.current);
    if (scr != NULL && scr->tick != NULL) {
        scr->tick(dt_s);
    }
}

void ui_router_set_status(const ui_bench_status_t *status)
{
    if (status == NULL) {
        return;
    }
    /*
     * Screens cache their chrome per framebuffer, so a status change that a
     * screen draws has to invalidate them.  The watermark covers the whole
     * canvas, and the menu's tile badges come from the capability bitmap, so
     * a change of either repaints everything.
     */
    const bool     was  = s.status.simulated;
    const uint16_t caps = s.status.capabilities;
    /* A fault that came up or a switch to SAFE -- STOP, an error -- ends
     * a hold button's hint at once: the hint is about a press that no
     * longer matters, and the FAULT chip says what happened. */
    if ((status->faults & (uint16_t)~s.status.faults) != 0u
        || (s.status.armed && !status->armed)) {
        drop_notice();
    }
    s.status = *status;
    if (was != s.status.simulated || caps != s.status.capabilities) {
        ui_router_invalidate();
    }
    guide_check();
    /* A bubble whose chip has gone -- the faults cleared, the run clock
     * gone -- has nothing left to explain, and closes. */
    if (s.help != UI_BAND_CHIP_NONE
        && !ui_band_chip_rect(&s.status, s.help, NULL)) {
        s.help = UI_BAND_CHIP_NONE;
    }
}

ui_band_chip_t ui_router_help(void) { return s.help; }

bool ui_router_help_rect(gfx_rect_t *out)
{
    return s.help != UI_BAND_CHIP_NONE && has_band(s.current)
           && ui_band_help_rect(&s.status, s.help, out);
}

bool ui_router_take_knob(void)
{
    if (s.guide || ui_shot_open()) {
        return false;   /* a turn moves nothing under the dock or the popup */
    }
    if (s.help != UI_BAND_CHIP_NONE) {
        s.help = UI_BAND_CHIP_NONE;
        return false;
    }
    return true;
}

const ui_bench_status_t *ui_router_status(void) { return &s.status; }

bool ui_router_take_stop(void)
{
    const bool was = s.stop_latched;
    s.stop_latched = false;
    return was;
}

/*
 * Every screen except the splash carries the band, the overview included: the
 * bench can be armed while the menu is showing.  The home tag is on every
 * screen except the splash and the overview.
 */
static bool has_band(ui_screen_id_t id)
{
    /* The bus-fault screen carries none either. Its whole claim is that the
     * bench cannot be driven, and a STOP button on it would offer to stop
     * something that is not running.  Nor does the panel's firmware write,
     * for the same reason: the bench is SAFE and the IOmcu in
     * failsafe. */
    if (id == SCREEN_COPROC_UPDATE) {
        /* Nor does an update under way: the IOmcu refuses ARM for
         * the whole session, so there is nothing for STOP to stop, and
         * the menu would lead to screens that cannot reach it.  The
         * question before it carries the band. */
        return coproc_update_screen_view() == CU_VIEW_QUESTION;
    }
    return id != SCREEN_SPLASH && id != SCREEN_BUSFAULT
           && id != SCREEN_UPDATE;
}

bool ui_router_guide_shown(void)
{
    return has_band(s.current) && !ui_shot_open()
           && ui_help_count(screen_for(s.current), s.status.armed) > 0;
}

bool ui_router_guide_open(void) { return s.guide; }

int ui_router_guide_page(void) { return s.guide ? s.guide_page : -1; }

/** @brief Close the guide; every screen repaints what the dock covered. */
static void guide_close(void)
{
    if (s.guide) {
        s.guide = false;
        s.guide_press = false;
        ui_router_invalidate();
    }
}

/**
 * @brief Open the guide at its first page.  No event reaches the screen
 *        while it is open, so what the screen owns is released first, as
 *        for a bubble.
 */
static void guide_open(void)
{
    s.help        = UI_BAND_CHIP_NONE;
    s.guide       = true;
    s.guide_page  = 0;
    s.guide_press = false;
    s.guide_set   = ui_help_set_key(screen_for(s.current), s.status.armed);
    release_owned();
    ui_router_invalidate();
}

/** @brief Close a guide whose screen has no pages any more or other ones:
 *         a tab, a level or the bench's state changed under it. */
static void guide_check(void)
{
    if (s.guide
        && (!ui_router_guide_shown()
            || ui_help_set_key(screen_for(s.current), s.status.armed)
                   != s.guide_set)) {
        guide_close();
    }
}

/**
 * @brief The page the open guide shows.
 * @param p The page, filled.
 * @return False while the guide is closed.
 */
static bool guide_page_now(ui_help_page_t *p)
{
    return s.guide
           && ui_help_page(screen_for(s.current), s.status.armed,
                           s.guide_page, p);
}

bool ui_router_guide_now(ui_help_page_t *out)
{
    return out != NULL && guide_page_now(out);
}

/**
 * @brief The release of a press on PREV, NEXT or CLOSE: a page back, a
 *        page on, or closed.  PREV on the first page and NEXT on the last
 *        do nothing.
 * @param btn The button.
 */
static void guide_act(ui_help_hit_t btn)
{
    const int count = ui_help_count(screen_for(s.current), s.status.armed);
    if (btn == UI_HELP_HIT_CLOSE) {
        guide_close();
    } else if (btn == UI_HELP_HIT_PREV && s.guide_page > 0) {
        --s.guide_page;
        ui_router_invalidate();
    } else if (btn == UI_HELP_HIT_NEXT && s.guide_page + 1 < count) {
        ++s.guide_page;
        ui_router_invalidate();
    }
}

/*
 * Whether a STOP button is on the screen at all.  The splash carries no band
 * and therefore no STOP, and anything hit-testing the band's rectangle
 * without asking would latch a stop on a tap that pressed nothing.
 */
bool ui_router_stop_live(void) { return has_band(s.current); }

static bool in_alert(const touch_event_t *evt)
{
    return evt->point.y >= PANEL_H - ALERT_H && evt->point.y < PANEL_H
           && evt->point.x >= 0 && evt->point.x < PANEL_W;
}

static bool has_home(ui_screen_id_t id)
{
    return id != SCREEN_SPLASH && id != SCREEN_OVERVIEW
           && id != SCREEN_BUSFAULT && id != SCREEN_UPDATE;
}

/* Hand @p evt, in panel coordinates, to the screen on top.  The one call
 * into a screen's event(): not inlined, so tools/stack_check.py finds it by
 * name. */
static __attribute__((noinline)) void router_hand(touch_event_t evt)
{
    const ui_screen_t *scr = screen_for(s.current);
    if (scr != NULL && scr->event != NULL) {
        /* Screens work in their own coordinates; the band's height is
         * removed here. */
        if (has_band(s.current)) {
            evt.point.y = (int16_t)(evt.point.y - UI_BAND_H);
        }
        ++s.dispatched;
        scr->event(&evt);
    }
}

/*
 * Whether @p evt, in panel coordinates, comes down on a control of the
 * screen on top that stops what the bench drives (ui_screen_t's safe_at).
 * The one call into a screen's safe_at(): not inlined, so
 * tools/stack_check.py finds it by name.
 */
static __attribute__((noinline)) bool router_safe_at(const touch_event_t *evt)
{
    if (evt->type != TOUCH_EVENT_DOWN || evt->point.y < UI_BAND_H) {
        return false;
    }
    const ui_screen_t *scr = screen_for(s.current);
    return scr != NULL && scr->safe_at != NULL
           && scr->safe_at(evt->point.x, evt->point.y - UI_BAND_H);
}

/*
 * Release every contact the screen owns at the last point it was handed,
 * flagged TOUCH_FLAG_NO_TAP, as at the band's edge: the release ends a press
 * or a drag and activates nothing.  The contacts are the router's from then
 * on, so a drag left latched cannot take a later contact's travel for its
 * own.
 */
static void release_owned(void)
{
    for (int i = 0; i < TOUCH_MAX_POINTS; ++i) {
        if (!s.owned[i].live) {
            continue;
        }
        s.owned[i].live = false;
        touch_event_t up = { 0 };
        up.type    = TOUCH_EVENT_UP;
        up.flags   = TOUCH_FLAG_NO_TAP;
        up.point.id = s.owned[i].id;
        up.point.x = s.owned[i].x;
        up.point.y = s.owned[i].y;
        router_hand(up);
    }
}

/* Open the bubble of @p chip.  While it is open no event reaches the
 * screen, so what the screen owns is released first. */
static void help_open(ui_band_chip_t chip)
{
    s.help = chip;
    release_owned();
}

/*
 * The home tag, tapped.  The screen on top goes up a level of its own when
 * it has one; otherwise it leaves for ui_router_parent(), unless the screen
 * holds that navigation.  A
 * level change inside a screen and a held navigation are a change of what
 * is on the glass, as a change of screen is: the contacts the screen owned
 * are released first and are nobody's until they lift.  Not inlined, so
 * tools/stack_check.py finds the calls into back() and hold() by name.
 */
static __attribute__((noinline)) void router_back(void)
{
    const ui_screen_t *scr = screen_for(s.current);
    const ui_screen_id_t to = ui_router_parent(s.current);
    if (scr != NULL && (scr->back != NULL || scr->hold != NULL)) {
        release_owned();
        if ((scr->back != NULL && scr->back())
            || (scr->hold != NULL && scr->hold(to))) {
            ui_router_invalidate();
            return;
        }
    }
    ui_router_goto(to);
}

/* The slot of contact @p id in the screen's table, or -1 when the screen
 * does not own it. */
static int owned_find(uint8_t id)
{
    for (int i = 0; i < TOUCH_MAX_POINTS; ++i) {
        if (s.owned[i].live && s.owned[i].id == id) {
            return i;
        }
    }
    return -1;
}

/*
 * One owner per contact, from its DOWN to its UP.
 *
 * The screen owns a contact whose DOWN it was handed, and is handed nothing
 * of any other.  A contact that came down on the band, on the alert strip,
 * on another screen or before a touch loss is the router's wherever it goes:
 * its MOVE and its UP reach no screen.
 *
 * A contact the screen owns that reaches the band is released at the edge.
 * The screen is handed an UP at the last point it was handed, flagged
 * TOUCH_FLAG_NO_TAP: it ends the press or the drag and activates nothing.
 * The contact is the router's from then on.  The release is not the contact's
 * real UP with its y moved into the body: a slider applies the horizontal
 * distance on its release and would step by the travel made on the band.
 * A screen that never saw the gesture end keeps its drag latched to a track
 * id the controller reuses, and takes a later contact's travel for its own.
 *
 * STOP and the home tag answer to a DOWN on them and to nothing else, so a
 * contact that slides onto STOP from the body presses nothing.  STOP acts
 * on that DOWN; the home tag on a tap.
 *
 * Every DOWN the screen is not handed -- on the band, on STOP, on a chip,
 * on the alert band -- releases the contacts the screen owns as at the
 * band's edge.  A screen ends a latched drag at any press it is handed, and
 * the router does the same for the presses it keeps: a dial drag must not
 * go on commanding while another finger presses STOP.
 *
 * A chip of the band opens its help bubble on a tap: a DOWN on it and a
 * release on the same chip.  While a bubble is open, a DOWN anywhere but on
 * STOP or another chip closes it and does nothing else: its contact is the
 * router's to its release.  A DOWN on another chip closes it and presses
 * that chip, so one tap moves the bubble to it.  A DOWN on STOP closes it
 * as well and presses STOP, as with no bubble.
 *
 * A DOWN in the band's gap with no other contact on the glass is the
 * screenshot's hold, over a bubble or the guide as well: they stay up for the
 * picture and close when the hold ends, with or without one.
 *
 * The guide follows the bubble's rules.  "?" opens it on a tap, as a chip
 * opens its bubble; a DOWN on "?" with the guide open closes it.  While it
 * is open, a DOWN on PREV, NEXT or CLOSE is the guide's and acts on a
 * release on the same button; PREV and NEXT cover the home tag in the band
 * (ui_help_button_rect()).  A DOWN elsewhere on the dock or on a badge
 * does nothing; a DOWN anywhere else -- the live half, the band, the alert
 * band -- closes it and does nothing else.  A DOWN on STOP or on a
 * chip closes it and presses STOP or the chip.
 *
 * A press that stops what the bench drives is never the router's to keep.
 * A DOWN on a control the screen's safe_at() names -- DISARM on an armed
 * bench, a run's ABORT or STOP -- closes the bubble or the alert that
 * covers it, or the guide, and is the screen's, as if neither were there.
 * ARM is no such
 * control: a press on ARM under the alert band closes the alert and starts
 * no hold.
 */
void ui_router_event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    const uint8_t id   = evt->point.id;
    const bool    band = has_band(s.current);
    const bool    popup = ui_shot_open();
    /* Only where the band is: no alert or bubble covers a screen without
     * one. */
    const bool    safe = band && router_safe_at(evt);

    shot_hold_feed(evt);
    shot_over_settle();

    guide_check();
    if (band) {
        const gfx_rect_t stop = ui_band_stop_rect();
        const gfx_rect_t home = ui_home_tag_rect(ui_router_title(s.current));
        const bool in_stop = gfx_rect_contains(stop, evt->point.x,
                                               evt->point.y);
        /* The popup ends by its own keys alone. */
        const bool in_home = has_home(s.current) && !popup
                             && gfx_rect_contains(home, evt->point.x,
                                                  evt->point.y);

        const ui_band_chip_t chip =
            ui_band_chip_at(&s.status, evt->point.x, evt->point.y);
        const bool on_guide = ui_router_guide_shown()
                              && ui_band_guide_at(evt->point.x, evt->point.y);

        if (s.guide_press && id == s.guide_id) {
            /* The press on a button of the dock: its release acts on the
             * button it came down on; a DOWN is a new contact. */
            s.guide_press = false;
            if (evt->type != TOUCH_EVENT_DOWN) {
                if (evt->type == TOUCH_EVENT_UP) {
                    ui_help_page_t p = { 0 };
                    if (touch_event_is_tap_up(evt) && guide_page_now(&p)
                        && ui_help_hit(&p, evt->point.x, evt->point.y)
                               == s.guide_btn) {
                        guide_act(s.guide_btn);
                    }
                } else {
                    s.guide_press = true;
                }
                return;
            }
        }
        if (evt->type == TOUCH_EVENT_DOWN && s.guide && !in_stop && !on_guide
            && chip == UI_BAND_CHIP_NONE) {
            /* A DOWN is a new contact: the one this id named before is
             * over, and this one is the guide's or nobody's. */
            const int was = owned_find(id);
            if (was >= 0) {
                s.owned[was].live = false;
            }
            /* PREV and NEXT cover the home tag, which does nothing while
             * the guide is open: a DOWN on either is the guide's. */
            ui_help_page_t p = { 0 };
            const ui_help_hit_t hit =
                (!safe && guide_page_now(&p))
                    ? ui_help_hit(&p, evt->point.x, evt->point.y)
                    : UI_HELP_HIT_NONE;
            if (hit == UI_HELP_HIT_DOCK) {
                return;
            }
            if (hit != UI_HELP_HIT_NONE) {
                s.guide_press = true;
                s.guide_id    = id;
                s.guide_btn   = hit;
                return;
            }
            /* The tap that closes the guide, except a press on DISARM or
             * a run's STOP: the guide closes and the press is the
             * screen's, below. */
            if (shot_hold_over(evt, popup)) {
                return;
            }
            guide_close();
            if (!safe) {
                return;
            }
        }
        if (evt->type == TOUCH_EVENT_DOWN && !in_stop && !on_guide
            && s.help != UI_BAND_CHIP_NONE
            && (chip == UI_BAND_CHIP_NONE || chip == s.help)) {
            /* The tap that closes the bubble.  A contact this id named
             * before is over, as at any DOWN, and this one reaches no
             * screen: the screen never owned it.  A DOWN on another chip
             * is that chip's press, below: the bubble closes with it and
             * the release opens that chip's bubble. */
            const int was = owned_find(id);
            if (was >= 0) {
                s.owned[was].live = false;
            }
            if (shot_hold_over(evt, popup)) {
                return;
            }
            s.help = UI_BAND_CHIP_NONE;
            /* Except a press on DISARM or a run's STOP: the bubble closes
             * and the press is the screen's, below. */
            if (!safe) {
                return;
            }
        }
        if (evt->type == TOUCH_EVENT_DOWN
            && (in_stop || in_home || chip != UI_BAND_CHIP_NONE
                || on_guide)) {
            /* "?" opens the guide on its release only when it was closed:
             * a DOWN on it closes an open one. */
            s.on_guide   = on_guide && !s.guide;
            guide_close();
            s.help       = UI_BAND_CHIP_NONE;
            s.band_press = true;
            s.band_id    = evt->point.id;
            s.on_stop    = in_stop;
            s.on_home    = in_home && !in_stop;
            s.on_chip    = (in_stop || in_home || on_guide)
                               ? UI_BAND_CHIP_NONE : chip;
            /* A press the screen is not handed still ends what it holds:
             * a drag left latched would go on commanding beside STOP. */
            release_owned();
            if (in_stop) {
                /* On the press: a stop never waits for a lift that may not
                 * come.  Latched rather than dispatched: the application
                 * loop that drives the heartbeat drains it, so a stop also
                 * stops the line. */
                s.stop_latched = true;
                drop_notice();
            }
            return;   /* consumed before the screen sees it */
        }
        if (safe && s.band_press && evt->point.id == s.band_id) {
            /* A DOWN is a new contact: a band press of this id whose
             * release went missing is over, and DISARM or a run's STOP is
             * not swallowed as its rest. */
            s.band_press = false;
        }
        if (s.band_press && evt->point.id == s.band_id) {
            if (evt->type == TOUCH_EVENT_UP) {
                s.band_press = false;
                if (s.on_stop) {
                    /* STOP acted on the press; its release does nothing. */
                } else if (s.on_guide) {
                    if (on_guide && touch_event_is_tap_up(evt)) {
                        guide_open();
                    }
                } else if (s.on_chip != UI_BAND_CHIP_NONE) {
                    /* A chip answers a release the finger made on the
                     * chip it came down on. */
                    if (chip == s.on_chip && touch_event_is_tap_up(evt)) {
                        help_open(chip);
                    }
                } else if (s.on_home && in_home
                           && touch_event_is_tap_up(evt)) {
                    /* The home tag takes a release the finger made. */
                    router_back();
                }
            }
            return;
        }
    }

    const uint8_t bit = (uint8_t)(1u << (id & 7u));
    if (safe) {
        /* A new contact on DISARM or a run's STOP: an earlier one of this
         * id on the alert band is over, as at any DOWN. */
        s.alert_ids[id >> 3] &= (uint8_t)~bit;
        if (s.alert_press && id == s.alert_id) {
            s.alert_press = false;
        }
    }
    if ((s.alert_ids[id >> 3] & bit) != 0u) {
        /* Asked before the band takes anything: a contact that came down on
         * the alert strip and lifts on the band ends its press here too. */
        if (evt->type == TOUCH_EVENT_UP) {
            s.alert_ids[id >> 3] &= (uint8_t)~bit;
            /* Only the first contact's lift, and only on the alert that
             * was showing when it came down: one that arrived meanwhile has
             * not been read. */
            if (s.alert_press && id == s.alert_id) {
                s.alert_press = false;
                if (s.has_alert && in_alert(evt)
                    && touch_event_is_tap_up(evt)
                    && s.alert_gen == s.alert_press_gen) {
                    clear_alert();
                }
            }
        }
        return;
    }

    if (popup) {
        /* The popup's.  A contact from before it opened presses nothing:
         * the keyboard acts on the release of a contact it saw come down, on
         * the key it came down on, so one that slid to the band ends there. */
        touch_event_t local = *evt;
        if (evt->type == TOUCH_EVENT_DOWN && s.has_alert && in_alert(evt)) {
            s.alert_ids[id >> 3] |= bit;
            if (!s.alert_press) {
                s.alert_press     = true;
                s.alert_id        = id;
                s.alert_press_gen = s.alert_gen;
            }
            return;
        }
        local.point.y = (int16_t)(local.point.y - UI_BAND_H);
        ui_shot_event(&local);
        return;
    }

    int slot = owned_find(id);
    if (evt->type == TOUCH_EVENT_DOWN && slot >= 0) {
        /* A DOWN is a new contact: the one this id named before is over. */
        s.owned[slot].live = false;
        slot = -1;
    }

    /* What the screen is handed: the event, or the release made for it. */
    touch_event_t local = *evt;

    if (band && evt->point.y < UI_BAND_H) {
        /* The band is the router's, so no screen receives an event with a
         * negative y.  A contact the screen owns ends where the screen last
         * saw it, and is not handed back when it returns to the body. */
        if (slot < 0) {
            if (evt->type == TOUCH_EVENT_DOWN) {
                release_owned();   /* a press on the band, as above */
                (void)shot_hold_begin(evt);
            }
            return;
        }
        local.type    = TOUCH_EVENT_UP;
        local.flags   = TOUCH_FLAG_NO_TAP;
        local.point.x = s.owned[slot].x;
        local.point.y = s.owned[slot].y;
        s.owned[slot].live = false;
    } else if (evt->type == TOUCH_EVENT_DOWN) {
        if (safe && s.has_alert && in_alert(evt)) {
            /* The alert covers DISARM or a run's STOP: the press is the
             * control's, and the alert goes with it.  Asking first would
             * make a stop take two presses. */
            clear_alert();
        } else if (s.has_alert && band && in_alert(evt)) {
            /* A press on the alert band ends what the screen holds, as a
             * press anywhere on the screen does. */
            release_owned();
            s.alert_ids[id >> 3] |= bit;
            if (!s.alert_press) {
                s.alert_press     = true;
                s.alert_id        = id;
                s.alert_press_gen = s.alert_gen;
            }
            return;   /* the band covers the screen here, so the screen gets none */
        }
        slot = 0;
        while (slot < TOUCH_MAX_POINTS && s.owned[slot].live) {
            ++slot;
        }
        if (slot == TOUCH_MAX_POINTS) {
            /* More contacts than the controller reports.  One the router
             * cannot follow to its release is handed to no screen. */
            return;
        }
        s.owned[slot].live = true;
        s.owned[slot].id   = id;
    } else if (slot < 0) {
        return;   /* the screen on top never saw this contact come down */
    }
    if (local.type == TOUCH_EVENT_UP) {
        s.owned[slot].live = false;
    } else {
        s.owned[slot].x = evt->point.x;
        s.owned[slot].y = evt->point.y;
    }

    router_hand(local);
}

void ui_router_cancel_gestures(void)
{
    /*
     * The band's own press first: HOME and STOP are the router's gesture,
     * not the screen's, and a band press left latched owns its track id.
     * The GT911 reuses ids, so a later contact that began on the screen
     * would be taken for this one's release and act on where it lifts.
     * The contacts the screen owned go the same way: their releases may
     * never come, so what is still on the glass is nobody's until it lifts
     * and comes down again.
     */
    s.band_press = false;
    s.guide_press = false;   /* the guide stays open */
    shot_hold_lost();
    ui_shot_cancel_press();
    memset(s.owned, 0, sizeof(s.owned));

    /*
     * Every screen, not the one on top.  The loss is observed after the
     * frame's events were dispatched, and one of those events can have
     * navigated: a HOME release that survived the loss leaves the screen
     * that took the earlier events holding its press, and the screen now on
     * top holding nothing.  A screen off the top keeps its gesture state
     * until it is entered again, so a tab press whose release went missing
     * would meet a recycled id on the next visit.  Cancelling a screen with
     * no gesture in progress asks for nothing, so every screen is told.
     * The alert band's presses go the same way: their releases may never
     * come.
     */
    s.alert_press = false;
    memset(s.alert_ids, 0, sizeof(s.alert_ids));
    for (int id = 0; id < SCREEN_COUNT; ++id) {
        const ui_screen_t *scr = screen_for((ui_screen_id_t)id);
        if (scr != NULL && scr->cancel != NULL) {
            scr->cancel();
        }
    }
}

void ui_router_set_alert(const char *text)
{
    if (text == NULL) {
        clear_alert();
        return;
    }
    snprintf(s.alert, sizeof(s.alert), "%s", text);
    s.has_alert   = true;
    s.alert_notice = false;
    s.alert_fresh = true;
    s.alert_age_s = 0.0f;
    ++s.alert_gen;
    if (s.alert_gen == 0u) {
        s.alert_gen = 1u;           /* 0 is "none" to ui_router_alert_number() */
    }
}

void ui_router_set_notice(const char *text)
{
    if (text == NULL || s.has_held || (s.has_alert && !s.alert_notice)) {
        return;
    }
    ui_router_set_alert(text);
    s.alert_notice = true;
}

bool ui_router_alert_is_notice(void)
{
    return s.has_alert && s.alert_notice;
}

uint32_t ui_router_alert_number(void)
{
    return s.has_alert ? s.alert_gen : 0u;
}

bool ui_router_withdraw_alert(uint32_t number)
{
    if (number == 0u || !s.has_alert || s.alert_gen != number) {
        return false;
    }
    clear_alert();
    return true;
}

void ui_router_hold_alert(const char *text)
{
    if (text != NULL && s.alert_notice) {
        clear_alert();                  /* a real alert replaces a notice */
    }
    if (s.has_held && !s.has_alert) {
        ui_router_invalidate();         /* the band's red, as clear_alert() */
    }
    s.has_held = (text != NULL);
    (void)snprintf(s.held, sizeof(s.held), "%s", s.has_held ? text : "");
}

const char *ui_router_alert(void)
{
    if (s.has_alert) {
        return s.alert;
    }
    return s.has_held ? s.held : NULL;
}

/* The alert's text, in UI_FONT_BAND (10 x 20): from x 12, the panel's width
 * less 12px at the left and 36px at the right, which clears the "x"
 * (976px, 97 cells of 10px, on the 1024px panel).  Text wider than one
 * line breaks at a space onto a second, 20px under the first: two rows
 * fill 40 of the band's 44px, 2px from its top. */
/** @brief The alert text's left edge: 12px. */
#define ALERT_TEXT_X 12
/** @brief The alert text's width: 976px, 97 cells, clear of the "x". */
#define ALERT_TEXT_W (PANEL_W - 2 * ALERT_TEXT_X - 24)
/** @brief An alert row to the next: 20px. */
#define ALERT_PITCH  20
/** @brief The alert's face: 10 x 20px. */
#define ALERT_FONT   UI_FONT_BAND

int ui_router_alert_rows(const char *text)
{
    return ui_text_wrap(NULL, gfx_rect_make(ALERT_TEXT_X, 0, ALERT_TEXT_W,
                                            ALERT_PITCH),
                        ALERT_PITCH, text, ALERT_FONT, 0);
}

/**
 * @brief Draw the alert band across the bottom of the panel.
 * @param c        The canvas, the panel's.
 * @param text     The alert, on one row centred or on two from the top.
 * @param closable A tap closes it: the "x" at its right end shows.
 */
static void draw_alert(gfx_canvas_t *c, const char *text, bool closable)
{
    /* A notice is amber with dark text; an alert is the danger colour. */
    const bool notice = ui_router_alert_is_notice();
    const gfx_color_t fill = ui_theme_color(notice ? UI_C_WARN : UI_C_DANGER);
    const gfx_color_t ink = ui_theme_color(notice ? UI_C_TEXT_ON_LIGHT
                                                  : UI_C_TEXT);
    const int y = PANEL_H - ALERT_H;
    /* One row centred in the band's height. */
    const int y1 = y + (ALERT_H - gfx_text_height(ALERT_FONT, 1)) / 2;
    gfx_fill_rect(c, 0, y, PANEL_W, ALERT_H, fill);
    if (ui_router_alert_rows(text) <= 1) {
        gfx_text(c, ALERT_TEXT_X, y1, text, ALERT_FONT, ink, 1);
    } else {
        (void)ui_text_wrap(c, gfx_rect_make(ALERT_TEXT_X, y + 2, ALERT_TEXT_W,
                                            ALERT_PITCH),
                           ALERT_PITCH, text, ALERT_FONT, ink);
    }
    if (closable) {
        gfx_text(c, PANEL_W - 12 - gfx_text_width(ALERT_FONT, "x", 1), y1,
                 "x", ALERT_FONT, ink, 1);
    }
}

/**
 * @brief The open guide over the screen: the dock when it changed in this
 *        buffer or the buffer has not got it, as the band; PREV and NEXT
 *        with the dock and after a band painted again, which leaves their
 *        place empty; the marks every frame, as the bubble, because the
 *        screen draws under them.  Not inlined, so tools/stack_check.py
 *        finds it by name.
 * @param c            The panel's canvas.
 * @param page         The page showing.
 * @param buffer_index The framebuffer.
 * @param band         The band was painted in this frame.
 */
static __attribute__((noinline)) void guide_render(gfx_canvas_t *c,
                                                   const ui_help_page_t *page,
                                                   int buffer_index,
                                                   bool band)
{
    const ui_screen_t *scr = screen_for(s.current);
    const unsigned buf = (unsigned)buffer_index & 1u;
    const int count = ui_help_count(scr, s.status.armed);
    const bool note = s.guide_page == 0
                      && ui_help_armed_note(scr, s.status.armed);
    const ui_help_hit_t btn = s.guide_press ? s.guide_btn : UI_HELP_HIT_NONE;
    const uint32_t key = ui_help_dock_key(page, s.guide_page, count, note,
                                          btn);
    if (s.status.simulated || (s.guide_drawn & (1u << buf)) == 0u
        || s.guide_key[buf] != key) {
        ui_help_render_dock(c, page, s.guide_page, count, note, btn);
        s.guide_key[buf] = key;
        s.guide_drawn |= (uint8_t)(1u << buf);
    } else if (band) {
        ui_help_render_nav(c, s.guide_page, count, btn);
    }
    ui_help_render_marks(c, page);
}

void ui_router_render(gfx_canvas_t *c, int buffer_index)
{
    const ui_screen_t *scr = screen_for(s.current);
    if (scr == NULL || scr->render == NULL || c == NULL) {
        return;
    }

    /* The mark goes or comes back with the setting, whichever path changed
     * it: the pixels it covered are repainted in every buffer. */
    const bool demo = settings_get_bool(SET_DEMO_MODE);
    if (demo != s.demo_seen) {
        s.demo_seen = demo;
        ui_router_invalidate();
    }

    if (!has_band(s.current)) {
        /* The screen draws where the band goes: this buffer has no band
         * any more, for when the band comes back on the same screen (the
         * IOmcu update's question after its progress). */
        s.band_drawn &= (uint8_t)~(1u << ((unsigned)buffer_index & 1u));
        scr->render(c, buffer_index);
        if (s.status.simulated) {
            ui_watermark(c);
        }
        ui_shot_capture(c, false);
        return;
    }

    guide_check();

    /*
     * The band when it changed in this buffer, or the buffer has not got
     * it.  Over a simulated bench the watermark is laid over it every
     * frame, so it is drawn every frame, as the screens under it are.
     */
    bool band_now = false;
    {
        const char *title = ui_router_title(s.current);
        const bool home = has_home(s.current);
        const bool stop_down = s.band_press && s.on_stop;
        const bool home_down = s.band_press && s.on_home;
        const unsigned buf = (unsigned)buffer_index & 1u;
        const ui_band_guide_t guide = s.guide ? UI_BAND_GUIDE_OPEN
                                      : ui_router_guide_shown()
                                          ? UI_BAND_GUIDE_SHOWN
                                          : UI_BAND_GUIDE_NONE;
        const uint32_t key = ui_band_key(title, home, &s.status, stop_down,
                                         home_down, guide);
        if (s.status.simulated || (s.band_drawn & (1u << buf)) == 0u
            || s.band_key[buf] != key) {
            ui_band_render(c, title, home, &s.status, stop_down, home_down,
                           guide);
            band_now = true;
            s.band_key[buf] = key;
            s.band_drawn |= (uint8_t)(1u << buf);
        }
    }

    /*
     * The bubble as this frame draws it.  One that closed, moved or changed
     * size since the last frame repaints every screen first: the screen
     * under it drew its chrome once per framebuffer, and the bubble's
     * pixels would stay where the chrome is not drawn again.
     */
    gfx_rect_t help;
    const bool show = ui_router_help_rect(&help);
    if (s.help_drawn
        && (!show || help.x != s.help_rect.x || help.y != s.help_rect.y
            || help.w != s.help_rect.w || help.h != s.help_rect.h)) {
        ui_router_invalidate();
    }
    s.help_drawn = show;
    if (show) {
        s.help_rect = help;
    }

    /*
     * A sub-canvas clipped to the body, so a screen cannot draw over the band
     * or STOP whatever offsets it uses.
     */
    const gfx_rect_t body = { 0, UI_BAND_H, PANEL_W, UI_BODY_H };
    gfx_canvas_t sub;
    ui_help_page_t page = { 0 };
    const bool guide = guide_page_now(&page);
    /* Marks that moved -- the arm chip on page 3 when the bench is ARMED,
     * a tile's mark that went -- repaint the screen first, as a bubble
     * that moved does: the screen drew its chrome once per framebuffer,
     * and the old marks would stay where it is not drawn again. */
    const uint32_t marks = guide ? ui_help_marks_key(&page) : 0u;
    if (s.guide_marked && (!guide || marks != s.guide_marks)) {
        ui_router_invalidate();
    }
    s.guide_marked = guide;
    s.guide_marks  = marks;
    if (gfx_canvas_sub(c, body, &sub)) {
        if (guide) {
            /* The screen draws the live half; the dock takes the other. */
            gfx_rect_t live = ui_help_live_rect(page.side);
            live.y = (int16_t)(live.y - UI_BAND_H);
            (void)gfx_clip_intersect(&sub, live);
        }
        if (ui_shot_open()) {
            ui_shot_render(&sub, buffer_index);
        } else {
            scr->render(&sub, buffer_index);
        }
    }
    if (guide) {
        guide_render(c, &page, buffer_index, band_now);
    }

    if (show) {
        ui_band_help_render(c, &s.status, s.help);
    }

    if (s.has_alert || s.has_held) {
        draw_alert(c, ui_router_alert(), s.has_alert);
    }

    /*
     * Last, over the band and the alert, so no screen can paint over the
     * watermark.
     */
    if (s.status.simulated) {
        ui_watermark(c);
    }

    /* The frame as the operator sees it, before the notice about it. */
    ui_shot_capture(c, s.shot_idle && shot_screen_free());
}
