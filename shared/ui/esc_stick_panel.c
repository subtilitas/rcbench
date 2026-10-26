/**
 * @file
 * @brief ESC STICK, the session MOTOR & ESC > PROGRAM opens: an ESC (electronic
 * speed controller) with no wire to talk on, programmed through its
 * throttle-stick menu.  Its list is the ESC profiles (esc_profile.h), its
 * parameters are the profile's menu items, and its write is a run of the
 * stick programmer (esc_stick.h): the bench arms, holds the throttle at the
 * entry position, switches the supply on and counts the beeps in the
 * supply current.  A run starts only from a red warning held for two
 * seconds, as ARM and the servo's HV warning are, and every move it makes
 * goes out as the MOTOR & ESC screen's commands do, through the arming
 * policy and the output bank.
 *
 * The profile list has two levels: the makers, alphabetical, and one
 * maker's models, by current, voltage and name, each opening its family's
 * profile.  A search field filters both.  Its keyboard docks on the right
 * while it is open and the rows narrow to its left, so the list filters
 * with every key and stays in view.  A run shows a stack light: green while
 * the detector holds a beep, red on a result that ended because something
 * was not as expected (esc_stick_reason_is_fault()).
 *
 * A profile whose ESC needs a person at it -- a jumper, a button -- lists
 * those steps (esc_profile_t's manual).  Its page carries MANUAL
 * INTERVENTION REQUIRED, which shows them over the screen, as does the
 * first opening; the warning lists those due before the power-up, and a
 * run that waits for one covers the page with DONE and ABORT.
 *
 * The pages draw on the session's layout, the body under MOTOR & ESC's
 * output strip, and take their controls through the session's tap walk
 * (esc_stick_int.h, esc_stick_pages.c).  The interface is
 * esc_stick_panel.h.
 *
 * This file is the state, a run and the application's side.  The profile
 * list is esc_stick_list.c, the presses esc_stick_touch.c, the list and a
 * profile's pages drawn esc_stick_draw.c, and a run, the RUN warning and
 * the manual steps drawn esc_stick_draw_run.c, with esc_stick_panel_int.h
 * between them.
 *
 * SPDX-License-Identifier: MIT
 */

#include "esc_stick_panel.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esc_profile.h"
#include "prog_registry.h"
#include "settings.h"
#include "supply_screen.h"
#include "ui_text.h"
#include "ui_textkey.h"
#include "ui_widgets.h"

/* Last: the layout macros of the headers it includes have short names
 * (W, H, PAD) and s names the state, which no header included after it
 * may meet. */
#include "esc_stick_panel_int.h"

/* The settings the TIMING page shows, in its order. */
const setting_id_t sp_settings_list[] = {
    SET_STICK_V, SET_STICK_I, SET_STICK_BEEP_MIN, SET_STICK_GAP_MIN,
    SET_STICK_LONG, SET_STICK_LONG_MAX, SET_STICK_GROUP_GAP, SET_STICK_ENTRY,
    SET_STICK_STORE, SET_STICK_OFF, SET_STICK_SILENCE, SET_STICK_TIMEOUT,
    SET_STICK_THRESHOLD, SET_STICK_HYST,
};
_Static_assert(sizeof(sp_settings_list) / sizeof(sp_settings_list[0])
               == SP_SETTINGS,
               "SP_SETTINGS (esc_stick_panel_int.h) counts sp_settings_list");

/* --------------------------------------------------------------- the state */

/* The state, its type in esc_stick_panel_int.h. */
sp_state_t esc_stick_s;

void esc_stick_panel_reset(void)
{
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < SP_ROWS; ++i) {
        s.st.rows[i] = (gfx_rect_t){ SP_ROW_X,
                                     (int16_t)(SP_ROW_Y0 + i * SP_ROW_P),
                                     (int16_t)(W - 2 * SP_ROW_X), SP_ROW_H };
    }
    s.st.find_box = (gfx_rect_t){ SP_FIND_X, CRUMB_Y, SP_FIND_W, CRUMB_H };
    s.st.find_clr = (gfx_rect_t){ (int16_t)(SP_FIND_X + SP_FIND_W - SP_CLR_W),
                                  CRUMB_Y, SP_CLR_W, CRUMB_H };
    s.st.list_up = (gfx_rect_t){ SP_LIST_UP_X, CRUMB_Y, SP_KEY_W, CRUMB_H };
    s.st.list_dn = (gfx_rect_t){ SP_LIST_DN_X, CRUMB_Y, SP_KEY_W, CRUMB_H };
    s.st.hold_btn   = (gfx_rect_t){ PAD + 20, SP_PBTN_Y, 260, SP_PBTN_H };
    s.st.cancel_btn = (gfx_rect_t){ (int16_t)(W - PAD - 20 - 180), SP_PBTN_Y,
                                    180, SP_PBTN_H };
    /* Between HOLD TO RUN and CANCEL, on the warning. */
    s.st.steps_btn  = (gfx_rect_t){ PAD + 20 + 260 + 24, SP_PBTN_Y, 266,
                                    SP_PBTN_H };
    /* In the side column, under the page keys. */
    s.st.hand_btn   = (gfx_rect_t){ SIDE_IN, HAND_Y,
                                    (int16_t)(SIDE_CELLS * SP_CELL),
                                    UI_TAP_MIN };
    /* On a result, in the foot row left of OK. */
    s.st.hand_res   = (gfx_rect_t){ (int16_t)(W - PAD - 12 - FOOT_BTN_W - 8
                                              - 330),
                                    BTN_Y, 330, BTN_H };
    /* In the profile page's crumb row, at its right, as tall as BACK. */
    s.st.timing_btn = (gfx_rect_t){ (int16_t)(W - PAD - 12 - FOOT_BTN_W),
                                    CRUMB_Y, FOOT_BTN_W, CRUMB_H };
    s.st.kp.pressed = -1;
}

void esc_stick_panel_enter(void)
{
    pg.tap.live = false;
    if (pg.stage != STAGE_CLOSED) {
        sp_build_list(true);
        ++pg.rev;
    }
}

void esc_stick_panel_open(void)
{
    stick_t *t = &s.st;
    pg.tap.live = false;
    pg.stage = STAGE_LIST;
    t->level = 0;
    t->maker = NULL;
    t->shown = false;
    t->timing = false;
    sp_kp_close();
    t->bscroll = 0;
    sp_build_list(false);
    ++pg.rev;
}

/* The picks as changes, in item order; how many were picked in all. */
size_t sp_changes(esc_stick_change_t *ch, size_t *picked)
{
    const stick_t *t = &s.st;
    size_t n = 0, all = 0;
    for (unsigned i = 0; t->p != NULL && i < t->p->item_count; ++i) {
        if (t->pick[i] < 0) {
            continue;
        }
        ++all;
        if (n < ESC_STICK_MAX_CHANGES) {
            ch[n].item = (uint8_t)i;
            ch[n].value = (uint8_t)t->pick[i];
            ++n;
        }
    }
    *picked = all;
    return n;
}

/* The timing as the settings hold it.  The entry is the profile's own where
 * it states one: the manual's number, not the bench's guess. */
void sp_timing(esc_stick_timing_t *t)
{
    const esc_profile_t *p = s.st.p;
    t->beep_min_ms   = (uint32_t)settings_get_int(SET_STICK_BEEP_MIN);
    t->gap_min_ms    = (uint32_t)settings_get_int(SET_STICK_GAP_MIN);
    t->long_ms       = (uint32_t)settings_get_int(SET_STICK_LONG);
    t->long_max_ms   = (uint32_t)settings_get_int(SET_STICK_LONG_MAX);
    t->group_gap_ms  = (uint32_t)settings_get_int(SET_STICK_GROUP_GAP);
    t->entry_ms      = (p != NULL && p->entry_hold_ms != 0u)
                           ? p->entry_hold_ms
                           : (uint32_t)settings_get_int(SET_STICK_ENTRY);
    t->store_ms      = (uint32_t)settings_get_int(SET_STICK_STORE);
    t->off_ms        = (uint32_t)settings_get_int(SET_STICK_OFF);
    t->silence_ms    = (uint32_t)settings_get_int(SET_STICK_SILENCE);
    t->timeout_ms    = (uint32_t)settings_get_int(SET_STICK_TIMEOUT);
    t->threshold_ma  = (uint32_t)settings_get_int(SET_STICK_THRESHOLD);
    t->hysteresis_ma = (uint32_t)settings_get_int(SET_STICK_HYST);
}

/* The supply's set points for a run, mV and mA: VOLTAGE, or the profile's
 * cell count when it is 0. */
void sp_supply(uint32_t *mv, uint32_t *ma)
{
    const float v = settings_get(SET_STICK_V);
    *mv = (v > 0.0f) ? (uint32_t)lroundf(v * 1000.0f)
                     : esc_stick_model_mv(s.st.p, s.st.model);
    *ma = (uint32_t)lroundf(settings_get(SET_STICK_I) * 1000.0f);
}

/*
 * Everything a run needs, and whether it can start.  When it cannot, the
 * note says why; with nothing picked the note stays empty and RUN is simply
 * not offered.  Refused, never adjusted: a voltage over the SUPPLY cap is
 * the operator's to change.
 */

/*
 * Why the bench cannot carry a run, or NULL: the supply is the panel's
 * model and switches nothing, the output binding is not read, or it binds
 * no pin as a throttle.  The stick has no pin to move then, or the ESC no
 * supply to be powered from.
 */
static const char *sp_bench_why(void)
{
    const stick_t *t = &s.st;
    if (supply_screen_model()) {
        return TR(SP_SUPPLY_MODEL);
    }
    if (!t->bind_read) {
        return TR(SP_BIND_NOT_READ);
    }
    if (t->throttle_mask == 0u) {
        return TR(SP_NO_THROTTLE);
    }
    return NULL;
}

static bool sp_plan(esc_stick_change_t *ch, size_t *n,
                    esc_stick_timing_t *t, uint32_t *mv, uint32_t *ma)
{
    stick_t *st = &s.st;
    st->note[0] = '\0';
    size_t picked = 0;
    *n = sp_changes(ch, &picked);
    sp_timing(t);
    sp_supply(mv, ma);
    if (st->p == NULL || picked == 0u) {
        return false;
    }
    if (picked > ESC_STICK_MAX_CHANGES) {
        snprintf(st->note, sizeof(st->note), TR(SP_AT_MOST),
                 (unsigned)ESC_STICK_MAX_CHANGES);
        return false;
    }
    /* The ESC's own ratings, for the model picked: a set point the data
     * cannot vouch for is refused, never sent. */
    if (sp_rating_why(st->p, st->model, *mv, *ma,
                      settings_get(SET_STICK_V) > 0.0f, true, st->note,
                      sizeof(st->note)) != NULL) {
        return false;
    }
    if (*mv == 0u) {
        snprintf(st->note, sizeof(st->note), "%s", TR(SP_NO_CELLS));
        return false;
    }
    /* Every change on the model picked: an item another model has may
     * share its number and values with one this model has, and the run
     * would change that one instead. */
    for (size_t i = 0; i < *n; ++i) {
        if (!esc_item_applies(st->p, ch[i].item, st->model)) {
            const esc_item_t *it = &st->p->items[ch[i].item];
            snprintf(st->note, sizeof(st->note), TR(SP_ITEM_NOT_HERE),
                     (unsigned)it->number, it->name);
            return false;
        }
    }
    const supply_caps_t caps = supply_screen_caps();
    const unsigned vmax = (unsigned)lroundf(caps.v_max * 1000.0f);
    const unsigned vmin = (unsigned)lroundf(caps.v_min * 1000.0f);
    const unsigned imax = (unsigned)lroundf(caps.i_max * 1000.0f);
    if (*mv > vmax) {
        snprintf(st->note, sizeof(st->note), TR(SP_ABOVE_CAP),
                 (unsigned)(*mv / 1000u), (unsigned)(*mv % 1000u / 10u),
                 vmax / 1000u, vmax % 1000u / 10u);
        return false;
    }
    if (*mv < vmin) {
        snprintf(st->note, sizeof(st->note), TR(SP_BELOW_MIN),
                 vmin / 1000u, vmin % 1000u / 10u);
        return false;
    }
    if (*ma > imax) {
        snprintf(st->note, sizeof(st->note), TR(SP_I_ABOVE_CAP),
                 imax / 1000u, imax % 1000u / 10u);
        return false;
    }
    const char *bench = sp_bench_why();
    if (bench != NULL) {
        snprintf(st->note, sizeof(st->note), "%s", bench);
        return false;
    }
    /* Asked on, or read live: the output on, or current through it. */
    if (supply_screen_output_live() || st->sup_live) {
        snprintf(st->note, sizeof(st->note), "%s", TR(SP_OUTPUT_LIVE));
        return false;
    }
    /* No reading, an old one, or a supply that does not answer is not a
     * supply known off: a run moves the stick and may ask for a hand at
     * the ESC on that word. */
    if (!sp_supply_reads_off()) {
        snprintf(st->note, sizeof(st->note), "%s", TR(SP_SUPPLY_NOT_OFF));
        return false;
    }
    /* A run arms the bench, and programming a servo and an armed bench
     * are exclusive. */
    if (prog_reg_servo_holds()) {
        snprintf(st->note, sizeof(st->note), "%s", TR(SP_KST_HELD));
        return false;
    }
    const char *why = NULL;
    if (!esc_stick_check(st->p, ch, *n, t, &why)) {
        snprintf(st->note, sizeof(st->note), "%s", sp_why_text(why));
        return false;
    }
    /* It runs; a voltage the data cannot vouch for is said, not refused. */
    (void)sp_rating_warn(st->p, st->model, *mv,
                         settings_get(SET_STICK_V) > 0.0f, true, st->note,
                         sizeof(st->note));
    return true;
}

/*
 * Whether the supply reads off now: a reading no older than
 * ESC_STICK_STALE_MS in which the supply itself reports its output off
 * with the current at or under ESC_STICK_OFF_MA, and has for
 * ESC_STICK_OFF_SETTLE_MS -- the rule a run holds the supply to.  The
 * warning shows a step at an unpowered ESC only then.
 */
bool sp_supply_reads_off(void)
{
    const stick_t *t = &s.st;
    if (!t->sup_have || !t->sup_off_known || supply_screen_output_live()) {
        return false;
    }
    const int32_t age = (int32_t)(t->now_ms - t->sup_at);
    const uint32_t held = t->sup_at - t->sup_off_since;
    return age <= (int32_t)ESC_STICK_STALE_MS
           && held >= ESC_STICK_OFF_SETTLE_MS;
}

/* Whether the warning asks a step at an unpowered ESC of the operator and
 * may not, as the supply does not read off. */
bool sp_warn_gated(void)
{
    const stick_t *t = &s.st;
    return t->p != NULL
           && esc_profile_manual_count(t->p, ESC_MANUAL_BEFORE_POWER) > 0u
           && !sp_supply_reads_off();
}

/*
 * Whether the warning shows every before-power step whole: under its
 * lines (sp_draw_warning(): five, the profile, the supply and the
 * unverified line from the panel's top + 68 at 22px, then the steps'
 * label), each
 * step wrapped to two lines, and room kept for the line about later steps,
 * above HOLD TO RUN.
 */

bool sp_warn_steps_fit(const esc_profile_t *p)
{
    if (p == NULL || esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER)
                         == 0u) {
        return true;
    }
    const int last = s.st.hold_btn.y - 20;
    const unsigned later = sp_later_steps(p);
    const int keep = (later > 0u) ? 22 : 0;
    int y = SP_PANEL_Y + 68 + 5 * 22 + 66 + 22;
    for (unsigned i = 0; i < p->manual_count; ++i) {
        const esc_manual_t *m = &p->manual[i];
        if (m->when == ESC_MANUAL_BEFORE_POWER) {
            const int need = sp_wrap_lines(sp_action(m), 90, 2);
            if (y + (need - 1) * 22 > last - keep) {
                return false;
            }
            y += 22 * need;
        }
    }
    return true;
}

/* The steps the run stops for on its way, after the warning's: at the
 * power-up, before the menu, and before the supply goes off. */
unsigned sp_later_steps(const esc_profile_t *p)
{
    return esc_profile_manual_count(p, ESC_MANUAL_AT_POWER_UP)
           + esc_profile_manual_count(p, ESC_MANUAL_BEFORE_MENU)
           + esc_profile_manual_count(p, ESC_MANUAL_BEFORE_POWER_OFF);
}

/* Whether HOLD TO RUN may not count: the supply does not read off, or the
 * before-power steps do not fit and ALL STEPS has not been read. */
bool sp_hold_blocked(void)
{
    const stick_t *t = &s.st;
    return sp_warn_gated() || (!sp_warn_steps_fit(t->p) && !t->warn_read);
}

bool sp_can_run(void)
{
    esc_stick_change_t ch[ESC_STICK_MAX_CHANGES];
    size_t n;
    esc_stick_timing_t t;
    uint32_t mv, ma;
    return sp_plan(ch, &n, &t, &mv, &ma);
}

/* A command for the application.  The queue keeps the newest: a DISARM is
 * never the one dropped, because nothing is queued behind one. */
static void sp_push(motor_cmd_kind_t kind, float value)
{
    stick_t *t = &s.st;
    if (t->qn == SP_QUEUE) {
        memmove(&t->q[0], &t->q[1], (SP_QUEUE - 1) * sizeof(t->q[0]));
        t->qn--;
    }
    t->q[t->qn].kind = kind;
    t->q[t->qn].value = value;
    t->qn++;
}

/* What the run asks for, onto the paths an operator's own presses take:
 * ARM, DISARM and THROTTLE as the MOTOR screen sends them, the supply as
 * SUPPLY's own switch. */
void sp_follow(void)
{
    stick_t *t = &s.st;
    const esc_stick_out_t *o = esc_stick_out(&t->run);
    if (o->arm != t->sent_arm) {
        t->sent_arm = o->arm;
        sp_push(o->arm ? MOTOR_CMD_ARM : MOTOR_CMD_DISARM, 0.0f);
    }
    if (o->throttle_pct != t->sent_pct) {
        t->sent_pct = o->throttle_pct;
        sp_push(MOTOR_CMD_THROTTLE, o->throttle_pct);
    }
    if (o->supply_on != t->sent_supply) {
        t->sent_supply = o->supply_on;
        if (o->supply_on) {
            supply_screen_put((float)o->supply_mv / 1000.0f,
                              (float)o->supply_ma / 1000.0f);
            supply_screen_ask_on(SUPPLY_BY_STICK);
        } else {
            supply_screen_ask_off();
        }
    }
}

static void sp_start(void)
{
    stick_t *t = &s.st;
    t->warn = false;
    t->warn_down = false;
    ui_hold_reset(&t->hold);
    ++pg.rev;
    esc_stick_change_t ch[ESC_STICK_MAX_CHANGES];
    size_t n;
    esc_stick_timing_t tm;
    uint32_t mv, ma;
    if (!sp_plan(ch, &n, &tm, &mv, &ma) || sp_hold_blocked()) {
        return;     /* the note says why */
    }
    const esc_stick_bench_t b = { t->now_ms, t->armed, t->stops,
                                  t->link_up, t->pressed };
    const char *why = NULL;
    if (!esc_stick_start(&t->run, t->p, ch, n, &tm, mv, ma, &b, &why)) {
        snprintf(t->note, sizeof(t->note), "%s", sp_why_text(why));
        return;
    }
    t->runs++;
    t->shown = true;
    esc_stick_light_reset(&t->light, &t->run);
    t->green = false;
    t->sent_arm = false;
    /* Sent as on, so the run's off goes out: the supply is asked off at
     * the start, whatever it was left as. */
    t->sent_supply = true;
    t->sent_pct = ESC_STICK_PCT_MIN;
    t->sig = 0u;
    /* The set points before anything else, so an ON never meets old ones. */
    supply_screen_put((float)mv / 1000.0f, (float)ma / 1000.0f);
    sp_follow();
}

void sp_end_hold(void)
{
    if (s.st.warn_down || s.st.hold.held_s > 0.0f) {
        s.st.warn_down = false;
        ui_hold_reset(&s.st.hold);
        ++pg.rev;
    }
}

/* What the display shows of a run, to repaint only when it changes. */
static uint32_t sp_signature(void)
{
    const esc_stick_t *e = &s.st.run;
    const uint32_t tenths = (e->phase == ESC_STICK_ENTRY
                             || e->phase == ESC_STICK_SIGNAL)
                                ? e->now_ms / 100u : 0u;
    const uint32_t parts[] = {
        (uint32_t)e->phase, esc_stick_beeps(e), e->groups, e->last_count,
        e->last_valid ? 1u : 0u, e->last_trusted ? 1u : 0u,
        e->last_in_order ? 1u : 0u,
        (uint32_t)e->ma, (uint32_t)esc_det_floor_ma(&e->det), e->iv_ms,
        esc_stick_done_count(e), e->entries, e->active, tenths,
        s.st.green ? 1u : 0u,
        esc_stick_hand_ready(e) ? 1u : 0u,
        (esc_stick_hand_left_ms(e) + 999u) / 1000u, e->hand,
        e->hand_menu ? 1u : 0u,
        /* The phase tap's readout, which a run's page shows beside it. */
        (uint32_t)s.tone.state, s.tone.read ? 1u : 0u,
        s.tone.overrun ? 1u : 0u,
        s.tone.win_freq_dhz, s.tone.lost, s.tone.missed,
        s.tone.n, (s.tone.n > 0u) ? s.tone.beeps[0].seq : 0u,
    };
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i) {
        h = (h ^ parts[i]) * 16777619u;
    }
    return h;
}

void esc_stick_panel_tick(float dt_s)
{
    stick_t *t = &s.st;
    if (pg.stage == STAGE_LIST && sp_list_key() != t->built_key) {
        sp_build_list(true);
        ++pg.rev;
    }
    if (t->shown && t->p != NULL && t->p->manual_count > 0u) {
        const bool off = sp_supply_reads_off();
        if (off != t->sup_off_drawn) {
            t->sup_off_drawn = off;
            ++pg.rev;
        }
    }
    if (t->warn && sp_bench_why() != NULL) {
        /* A bench that can no longer carry a run closes the warning: its
         * HOLD TO RUN would end in a refusal, and the note says why. */
        t->warn = false;
        sp_end_hold();
        ++pg.rev;
    }
    if (t->warn) {
        /* The steps at an unpowered ESC show, and HOLD TO RUN counts, only
         * while the supply reads off; a hold under way ends when it stops
         * reading so. */
        const bool gated = sp_hold_blocked();
        if (gated != t->sup_gate) {
            t->sup_gate = gated;
            ++pg.rev;
        }
        if (gated) {
            sp_end_hold();
        }
    }
    if (t->warn && t->warn_down) {
        ++pg.rev;            /* the hold's fill */
        if (ui_hold_tick(&t->hold, dt_s)) {
            sp_start();
        }
    }
    const bool running = esc_stick_running(&t->run);
    /* Not gated on the result still showing: a tap queued in the frame
     * the run ended -- on ABORT, which the result's OK replaces -- closes
     * the result before this tick sees the end, and the steps are still
     * due. */
    if (t->was_running && !running && t->run.p != NULL
        && esc_profile_manual_count(t->run.p,
                                    ESC_MANUAL_AFTER_PROGRAMMING) > 0u) {
        /* The run is over: what is to be done now, every step of it. */
        t->hand_open = true;
        t->hand_p = t->run.p;
        t->hand_model = t->model;
        ++pg.rev;
    }
    t->was_running = running;
    if (running) {
        const esc_stick_bench_t b = { t->now_ms, t->armed, t->stops,
                                      t->link_up, t->pressed };
        esc_stick_step(&t->run, &b);
        sp_follow();
        /* A light that changes repaints both buffers: the signature moves
         * the revision, and each buffer redraws on a revision it has not
         * drawn. */
        t->green = esc_stick_light_green(&t->light, &t->run, t->now_ms);
        const uint32_t sig = sp_signature();
        if (sig != t->sig) {
            t->sig = sig;
            ++pg.rev;
        }
        return;
    }
    if (pg.stage == STAGE_PAGE && t->p != NULL && !t->shown) {
        /* RUN follows the supply and the caps, which change elsewhere. */
        char was[SP_NOTE];
        memcpy(was, t->note, sizeof(was));
        const bool could = sp_can_run();
        if (strcmp(was, t->note) != 0 || could != t->could) {
            t->could = could;
            ++pg.rev;
        }
    }
}

void esc_stick_panel_leave(void)
{
    stick_t *t = &s.st;
    pg.tap.live = false;
    if (esc_stick_running(&t->run)) {
        esc_stick_abort(&t->run, ESC_STICK_R_LEFT);
        sp_follow();
    }
    /* The run's abort asks the supply off; and whatever a run switched on
     * goes off with the screen in any case. */
    supply_screen_left(SUPPLY_BY_STICK);
    /* A warning not held is a run not started.  The search keeps what was
     * typed. */
    t->warn = false;
    t->hand_open = false;
    sp_end_hold();
    sp_find_close();
    sp_kp_close();
    if (t->timing) {
        t->timing = false;
        if (settings_dirty()) {
            settings_request_save();
        }
    }
}

/*
 * Drop the ARM and the THROTTLE a run has queued and the application has
 * not taken: the hold completes in the frame's tick, after the frame has
 * handed its commands over, so they wait a frame here -- and the next frame
 * would stamp them with what it knows then, past a stop or a loss that
 * came in between.  Returns whether an ARM was among them.
 */
static bool sp_drop_drive(void)
{
    stick_t *t = &s.st;
    bool arm = false;
    int k = 0;
    for (int i = 0; i < t->qn; ++i) {
        const motor_cmd_kind_t kind = t->q[i].kind;
        if (kind == MOTOR_CMD_ARM || kind == MOTOR_CMD_THROTTLE) {
            arm = arm || kind == MOTOR_CMD_ARM;
            continue;
        }
        t->q[k++] = t->q[i];
    }
    t->qn = k;
    return arm;
}

/* The run ends now, and nothing it queued to drive survives the end. */
void sp_end_run(esc_stick_reason_t why)
{
    stick_t *t = &s.st;
    if (!esc_stick_running(&t->run)) {
        return;
    }
    (void)sp_drop_drive();
    esc_stick_abort(&t->run, why);
    sp_follow();
    ++pg.rev;
}

/*
 * Touch went missing: a hold under way may have no finger on it, and an
 * arm the warning's hold asked for may have come from a contact the panel
 * lost.  Before the arm is in force the run ends; once the bench is armed
 * the control task's watch over the arm decides, as it does for ARM.
 */
void esc_stick_panel_cancel(void)
{
    /* A press that may become a tap has no release to wait for. */
    pg.tap.live = false;
    sp_end_hold();
    stick_t *t = &s.st;
    if (t->tk.open && t->tk.pressed >= 0) {
        ui_textkey_cancel_press(&t->tk);
        t->tk_rev = t->tk.revision;
        ++pg.rev;
    }
    if (t->kp.open && t->kp.pressed >= 0) {
        ui_keypad_cancel_press(&t->kp);
        t->kp_rev = t->kp.revision;
        ++pg.rev;
    }
    bool queued = false;
    for (int i = 0; i < t->qn; ++i) {
        queued = queued || t->q[i].kind == MOTOR_CMD_ARM;
    }
    if (esc_stick_running(&t->run)
        && (queued || t->run.phase == ESC_STICK_ARMING)) {
        sp_end_run(ESC_STICK_R_TOUCH);
    }
}

/* ------------------------------------------- the application's side ----- */

void esc_stick_panel_bench(uint32_t now_ms, bool armed, uint32_t stops,
                             uint32_t pressed, bool link_up)
{
    stick_t *t = &s.st;
    /* A stop ends a hold under way; the warning stays, and a new hold is
     * a new decision.  It ends a run too, with any ARM it queued and the
     * application has not taken: that one would be stamped with this
     * frame's count and clear the stop. */
    if (stops != t->stops) {
        sp_end_hold();
        sp_end_run(esc_stick_stop_reason(stops - t->stops,
                                         pressed - t->pressed));
    }
    t->now_ms = now_ms;
    t->armed = armed;
    t->stops = stops;
    t->pressed = pressed;
    t->link_up = link_up;
}

void esc_stick_panel_set_throttle(bool read, uint8_t channels)
{
    s.st.bind_read = read;
    s.st.throttle_mask = read ? channels : 0u;
}

void esc_stick_panel_tone(const tone_readout_t *r)
{
    if (r != NULL) {
        s.tone = *r;
    }
}

void esc_stick_panel_supply(const supply_state_t *st)
{
    stick_t *t = &s.st;
    if (st == NULL) {
        return;
    }
    /* Every reading, run or no run: whether the supply is live, and since
     * when it has read off -- its own state, the current at or under
     * ESC_STICK_OFF_MA. */
    const bool reads_off = st->online && !st->output
                           && st->mode == SUPPLY_MODE_OFF
                           && (st->ok & SUPPLY_OK_CURRENT) != 0u
                           && lroundf(st->i * 1000.0f) <= ESC_STICK_OFF_MA;
    t->sup_have = true;
    t->sup_at = st->taken_ms;
    t->sup_live = !reads_off && st->online;
    if (!reads_off) {
        t->sup_off_known = false;
    } else if (!t->sup_off_known) {
        t->sup_off_known = true;
        t->sup_off_since = st->taken_ms;
    }
    if (!esc_stick_running(&t->run)) {
        return;
    }
    const esc_stick_sample_t x = {
        .seq = st->samples,
        .at_ms = st->taken_ms,
        .ma = (int32_t)lroundf(st->i * 1000.0f),
        .current_ok = (st->ok & SUPPLY_OK_CURRENT) != 0u,
        /* What the panel asked, and apart from it what the supply reports:
         * the PD mini's output follows a link exchange and a module
         * transaction behind the request, on and off alike.  On counts
         * only when both say so, off only when the supply says so. */
        .output = st->output,
        .reported_on = st->online && st->mode != SUPPLY_MODE_OFF,
        .online = st->online,
    };
    esc_stick_sample(&t->run, &x);
    sp_follow();        /* a reading can end the run */
}

bool esc_stick_panel_poll_cmd(motor_cmd_t *out)
{
    stick_t *t = &s.st;
    if (t->qn == 0 || out == NULL) {
        return false;
    }
    *out = t->q[0];
    memmove(&t->q[0], &t->q[1], (size_t)(t->qn - 1) * sizeof(t->q[0]));
    t->qn--;
    return true;
}

const esc_profile_t *esc_stick_panel_profile(void)
{
    return esc_stick_running(&s.st.run) ? s.st.run.p : NULL;
}

uint32_t esc_stick_panel_runs(void) { return s.st.runs; }

const esc_stick_t *esc_stick_panel_stick(void) { return &s.st.run; }

const char *esc_stick_panel_search(void) { return s.st.find; }

bool esc_stick_panel_typing(void) { return s.st.tk.open; }

int esc_stick_panel_listed(int *top)
{
    if (top != NULL) {
        *top = s.st.scroll;
    }
    return s.st.count;
}

int esc_stick_panel_level(void) { return s.st.level; }

const char *esc_stick_panel_maker(void)
{
    return (s.st.level == 1 && s.st.maker != NULL) ? s.st.maker : "";
}

int esc_stick_panel_model(void)
{
    return (pg.stage == STAGE_PAGE) ? s.st.model
                                                               : -1;
}

const esc_profile_t *esc_stick_panel_row(int i, int *model)
{
    const stick_t *t = &s.st;
    if (t->level != 1 || i < 0 || i >= t->count) {
        return NULL;
    }
    if (model != NULL) {
        *model = t->rows_m[i].model;
    }
    return esc_profiles_at(t->rows_m[i].prof);
}

const char *esc_stick_panel_row_why(int i)
{
    static char why[64];
    int model = -1;
    const esc_profile_t *p = esc_stick_panel_row(i, &model);
    return (p != NULL) ? sp_model_why(p, model, why, sizeof(why)) : NULL;
}

int esc_stick_panel_item_at(int i)
{
    return sp_item_at(i);
}

const char *esc_stick_panel_row_warn(int i)
{
    static char warn[64];
    int model = -1;
    const esc_profile_t *p = esc_stick_panel_row(i, &model);
    return (p != NULL && esc_stick_panel_row_why(i) == NULL)
               ? sp_model_warn(p, model, warn, sizeof(warn)) : NULL;
}

const char *esc_stick_panel_result_head(void)
{
    static char line[128];
    sp_result_head(&s.st.run, line, sizeof(line));
    return line;
}

const char *esc_stick_panel_result_more(void)
{
    static char line[128];
    const unsigned shown = (s.st.run.n > 5u) ? 4u : s.st.run.n;
    line[0] = '\0';
    if (shown < s.st.run.n) {
        sp_result_more(&s.st.run, shown, line, sizeof(line));
    }
    return line;
}

const char *esc_stick_panel_left_text(void)
{
    return sp_left_text(&s.st.run);
}

const char *esc_stick_panel_note(void)
{
    return s.st.note;
}

const char *esc_stick_panel_maker_at(int i)
{
    const stick_t *t = &s.st;
    return (t->level == 0 && i >= 0 && i < t->count) ? t->makers[i].name
                                                     : NULL;
}

uint32_t esc_stick_panel_entry_shown(void)
{
    return (s.st.p != NULL) ? sp_entry_shown() : 0u;
}

bool esc_stick_panel_supply_reads_off(void)
{
    return sp_supply_reads_off();
}

const char *esc_stick_panel_hand_why(void)
{
    const stick_t *t = &s.st;
    static char why[64];
    if (!t->hand_open || t->hand_p == NULL) {
        return NULL;
    }
    return sp_model_why(t->hand_p, t->hand_model, why, sizeof(why));
}

const esc_profile_t *esc_stick_panel_page(void)
{
    return (pg.stage == STAGE_PAGE) ? s.st.p
                                                               : NULL;
}

bool esc_stick_panel_hand_shown(void)
{
    return s.st.hand_open && s.st.hand_p != NULL;
}

void esc_stick_panel_lights(bool *red, bool *green)
{
    if (red != NULL) {
        *red = sp_red();
    }
    if (green != NULL) {
        *green = esc_stick_running(&s.st.run) && s.st.green;
    }
}
