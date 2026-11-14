/**
 * @file
 * @brief The balance screen: sensor placement guides and the correction readout.
 *
 * A balance answer is a magnitude and an angle, both derived from two
 * sensors whose placement decides whether the answer is valid.  A vibration
 * sensor on a compliant mount reads a filtered version of the bearing's
 * motion; an index mark on a blade instead of the bell gives one pulse per
 * blade and an angle wrong by a blade spacing.  Neither error is visible on
 * screen, so the screen carries the placement diagrams.
 *
 * The blade count takes 2 to 6 from its "-" and "+" keys, which step on the
 * press, or from the keypad a tap on the count opens over both cards.  A
 * tab tapped while the keypad is open closes it, the count as it was.
 *
 * With a balance node linked (node_balance.h) the readings are the node's:
 * the 1x magnitude and angle, the rpm and the quality word, "--" for a
 * report older than BAL_STALE_MS, and ADD and WHERE from the runs taken on
 * MOTOR & ESC > BALANCE.  Without a node the readings stay the model's,
 * marked SIMULATED.
 *
 * SPDX-License-Identifier: MIT
 */

#include "balance_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "node_balance.h"
#include "ui_keypad.h"
#include "ui_footer.h"
#include "ui_tabs.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

#define W UI_PANEL_W /**< The panel's width, px. */
#define H UI_BODY_H  /**< The body's height under the band, px. */

#define PAD     UI_MARGIN /**< The screen's outer margin, px. */

/**
 * @brief The pane tabs, BALANCE | TEST RIG | AIRCRAFT: three tabs of
 *        150 x 48px, 4px apart, from x 11 and y 8, a finger tall.
 */
#define TAB_X   (PAD + 3)
#define TAB_Y   8                       /**< The tabs' top, px. */
#define TAB_H   UI_TAP_MIN              /**< A tab's height: 48px. */
#define TAB_ROW_W (3 * 150 + 2 * 4)     /**< The row: 458px. */
#define CARD_Y  64                      /**< Both cards' top, px. */

/**
 * @brief The cards' foot, on the screen's footer (ui_footer.h), and the
 *        lowest row a control or a line of text may reach, 8px above it:
 *        body y 428.
 */
#define CARD_B     UI_FOOTER_FLOOR
#define TEXT_FLOOR (CARD_B - 8) /**< The text floor: body y 428. */

/** @brief The rotor's card: 640px, the right card 360. */
#define LCARD_W 640
#define RCARD_X (PAD + LCARD_W + 8)            /**< x 656. */
#define RCARD_W (W - RCARD_X - PAD)            /**< 360px. */
#define CARD_H  (CARD_B - CARD_Y)              /**< 372px, to y 436. */
#define RC_X    (RCARD_X + 14)                 /**< The right column: x 670. */
#define RC_W    (RCARD_W - 28)                 /**< Its width: 332px. */

/**
 * @brief The faces of the 1024 x 600 type scale this screen draws in:
 *        BL_FONT_LABEL (10 x 20) for labels, buttons, rows, callouts and
 *        the rules' heads, BL_FONT_FINE (8 x 16) for the notes and the
 *        rules' two lines, which the 33 cells of the 10 x 20 face in the
 *        right column do not hold.
 */
#define BL_FONT_LABEL (&gfx_font_10x20)
#define BL_FONT_FINE  UI_FONT_FINE       /**< 8 x 16. */

/*
 * The BALANCE pane's right column, top down: ROTOR and its two buttons,
 * BLADES with its count and its "-" and "+" keys, the four readings in
 * rows of 28px, the SIMULATED pill and the note.
 */
#define ROTOR_Y   (CARD_Y + 38)   /**< PROPELLER and EDF: y 102. */
#define ROTOR_H   UI_TAP_MIN      /**< Their height: 48px. */
#define BLADES_Y  (ROTOR_Y + ROTOR_H + 10)  /**< The BLADES row: y 160. */
#define STEP_W    56              /**< The "-" and "+" keys' width, px. */
#define STEP_H    UI_TAP_MIN      /**< Their height: 48px. */
#define READ_Y    (BLADES_Y + STEP_H + 10)  /**< The first reading: y 218. */
#define READ_H    28              /**< A reading's row, px. */
/** @brief The SIMULATED pill under the readings, 20px high. */
#define MARK_Y    (READ_Y + 4 * READ_H + 8)  /**< y 338. */
#define NOTE_Y    (MARK_Y + 32)   /**< The note's first line: y 370. */
/** @brief With a node: six readings to y 386, the line under them. */
#define NODE_NOTE_Y (READ_Y + 6 * READ_H + 14)  /**< y 400. */
_Static_assert(NODE_NOTE_Y + 16 <= TEXT_FLOOR,
               "the node's line ends above the cards' foot");
_Static_assert(NOTE_Y + 42 + 16 <= TEXT_FLOOR,
               "the note's last line ends above the cards' foot");
_Static_assert(ROTOR_H >= UI_TAP_MIN && STEP_H >= UI_TAP_MIN
               && STEP_W >= UI_TAP_MIN, "every key is a finger tall");

/** @brief The rules' first row, TEST RIG and AIRCRAFT, px: y 98. */
#define RULE_Y0   (CARD_Y + 34)
#define RULE_PITCH 55             /**< A rule under the one before, px. */
#define RULE_L1   22              /**< A rule's first line under its head. */
#define RULE_L2   39              /**< Its second line, 16px tall. */
_Static_assert(RULE_L2 + 16 <= RULE_PITCH, "a rule ends above the next");
_Static_assert(RULE_Y0 + 5 * RULE_PITCH + RULE_L2 + 16 <= TEXT_FLOOR,
               "the sixth rule ends above the cards' foot");

/*
 * The placement drawings of TEST RIG and AIRCRAFT keep their 450px card's
 * coordinates and are drawn in a window centered in the 640px card, from
 * the card's top.
 */
#define FIG_X     ((LCARD_W - 450) / 2)              /**< x 95. */
#define FIG_Y     (CARD_Y - 40)                      /**< y 24. */

/* The blade count's keypad: over both cards and the footer, from the
 * cards' top to 4px above the alert band, 1008 x 428px. */
/** @brief The keypad's top, px. */
#define KP_Y    CARD_Y
/** @brief The keypad's height, px: 428. */
#define KP_H    (UI_ALERT_Y - UI_BAND_H - 4 - KP_Y)
/** @brief The fewest blades the count takes. */
#define BLADES_MIN 2
/** @brief The most blades the count takes. */
#define BLADES_MAX 6

/* The rig, drawn side on: a post, an arm, a motor, and the disc it turns. */
#define BASE_Y   356  /**< the rig's bench top, y px */
#define POST_X   150  /**< the post's left edge, px */
#define POST_W   40  /**< the post's width, px */
#define ARM_Y    196  /**< the arm's top, px */
#define ARM_H    28  /**< the arm's height, px */
#define MOTOR_X  300  /**< the motor's left edge, px */
#define MOTOR_W  62  /**< the motor's width, px */
#define SHAFT_Y  210  /**< the shaft's y, px */
#define SPIN_X   406  /**< the disc's center, x px */
#define BLADE_L  92  /**< a blade's length from the shaft, px */

/** @brief the pane tabs' labels */
static const ui_text_id_t k_tabs[] = { TX_BAL_TAB_BALANCE, TX_BAL_TAB_RIG,
                                       TX_BAL_TAB_AIRCRAFT };

/*
 * The rotor, drawn face on.
 *
 * The blade count sets where the blades are drawn, and the screen names the
 * two blades the correction falls between: an angle from the index mark is
 * actionable only relative to a blade.
 */
#define DISC_CX  (PAD + LCARD_W / 2)            /**< x 328. */
#define DISC_CY  ((CARD_Y + TEXT_FLOOR) / 2)     /**< y 246. */
/** @brief The disc's radius, px: a ducted fan's numbers at DISC_R + 38
 *         and 10px tall end inside the card and above its foot. */
#define DISC_R   128
#define HUB_R    32                              /**< The hub's, px. */
_Static_assert(DISC_CY + DISC_R + 38 + 10 <= TEXT_FLOOR,
               "a blade number ends above the cards' foot");
_Static_assert(DISC_CY - DISC_R - 38 - 10 >= CARD_Y + 6,
               "a blade number begins inside the card");

static struct {
    ui_tabs_t tabs;              /**< BALANCE, TEST RIG, AIRCRAFT */
    int   blades;                /**< 2 to 6                      */
    int   rotor;                 /**< propeller or ducted fan     */
    gfx_rect_t rotor_btn[ROTOR_COUNT]; /**< PROPELLER and EDF        */
    gfx_rect_t blade_dn;         /**< the count's "-"             */
    gfx_rect_t blade_up;         /**< the count's "+"             */
    ui_keypad_t kp;              /**< the blade count's keypad     */
    bool      value_down;        /**< a press on the count, opening
                                      on its release               */
    uint8_t   value_id;          /**< that press's touch id        */
    uint32_t  kp_rev;            /**< the keypad's revision last
                                      seen                         */
    uint32_t  rev;               /**< bumped on every change      */
    uint32_t  node_rev;          /**< node_balance_rev() last seen */
    uint32_t  res_rev;           /**< node_balance_result_rev() last
                                      seen                         */
    bool      node_was;          /**< node_live() last seen        */
    uint32_t  read_rev;          /**< bumped on a new reading     */
    uint32_t  read_drawn[2];     /**< each buffer's read_rev      */
    uint32_t  drawn[2];          /**< each buffer's revision      */
    unsigned  drawn_mask;        /**< the buffers cleared         */
} s; /**< The screen's state. */

void balance_invalidate(void)
{
    s.drawn_mask = 0;
    s.drawn[0] = UINT32_MAX;
    s.drawn[1] = UINT32_MAX;
}

/** @brief Whether a balance node is on and bound: the readings are its own. */
static bool node_live(void)
{
    return node_balance_live()->live != NB_LIVE_SIMULATED;
}

/**
 * @brief The screen on top: the node taken as it is, drawn whole on the
 *        next frame, so the first tick finds nothing new.
 */
static void enter(void)
{
    s.node_rev = node_balance_rev();
    s.res_rev = node_balance_result_rev();
    s.node_was = node_live();
    ++s.rev;
}

/**
 * @brief The screen's state cleared: the BALANCE pane, two blades, a
 *        propeller, the controls laid out and the keypad closed.
 */
static void reset(void)
{
    memset(&s, 0, sizeof(s));
    s.drawn[0] = UINT32_MAX;
    s.drawn[1] = UINT32_MAX;
    s.blades = BLADES_MIN;
    s.rotor  = ROTOR_PROP;
    s.kp.pressed = -1;
    ui_tabs_init_text(&s.tabs, k_tabs, BALANCE_PANE_COUNT,
                      (gfx_rect_t){ TAB_X, TAB_Y, TAB_ROW_W, TAB_H });

    for (int i = 0; i < ROTOR_COUNT; ++i) {
        s.rotor_btn[i] = (gfx_rect_t){ (int16_t)(RC_X + i * (RC_W / 2 + 4)),
                                       (int16_t)ROTOR_Y,
                                       (int16_t)(RC_W / 2 - 4), ROTOR_H };
    }
    s.blade_dn = (gfx_rect_t){ (int16_t)(RC_X + RC_W - 2 * STEP_W - 8),
                               (int16_t)BLADES_Y, STEP_W, STEP_H };
    s.blade_up = (gfx_rect_t){ (int16_t)(RC_X + RC_W - STEP_W),
                               (int16_t)BLADES_Y, STEP_W, STEP_H };
}

int  balance_screen_blades(void) { return s.blades; }
int  balance_screen_rotor(void)  { return s.rotor; }

const ui_keypad_t *balance_screen_keypad(void) { return &s.kp; }

gfx_rect_t balance_screen_blades_rect(void)
{
    return (gfx_rect_t){ (int16_t)(RC_X + 90), (int16_t)BLADES_Y,
                         (int16_t)(s.blade_dn.x - 8 - (RC_X + 90)), STEP_H };
}

/**
 * @brief The keypad's revision moved: the frame draws it again.
 */
static void kp_follow(void)
{
    if (s.kp.revision != s.kp_rev) {
        s.kp_rev = s.kp.revision;
        ++s.rev;
    }
}

/**
 * @brief One event while the keypad is open: it takes them all.  OK sets
 *        the count; the keypad holds it to 2 to 6.
 * @param evt The event.
 */
static void kp_event(const touch_event_t *evt)
{
    float v = 0.0f;
    if (ui_keypad_event(&s.kp, evt, &v) == UI_KEYPAD_OK) {
        s.blades = (int)lrintf(v);
    }
    kp_follow();
}

/**
 * @brief The keypad closes, with nothing typed taken: the home tag over it
 *        and leaving the screen.
 */
static void kp_close(void)
{
    if (s.kp.open) {
        ui_keypad_close(&s.kp);
        kp_follow();
    }
}

/**
 * @brief The home tag: over the keypad it is CANCEL.
 * @return True when it closed the keypad; false to go home.
 */
static bool back(void)
{
    s.value_down = false;
    if (!s.kp.open) {
        return false;
    }
    kp_close();
    return true;
}

/**
 * @brief The screen is left: the keypad closes, with nothing typed taken.
 */
static void leave(void)
{
    s.value_down = false;
    kp_close();
}

/**
 * @brief The correction the node's runs solved.
 * @param out Receives it.
 * @return false while there is none with a mass and an angle.
 */
static bool node_correction(vibe_solution_t *out)
{
    return node_balance_result(out, NULL)
           && (out->status == VIBE_SOLVE_OK
               || out->status == VIBE_SOLVE_WEAK);
}

/**
 * @brief Where the correction goes: the node's solution, or the model's
 *        without a node.
 *
 * The model's 265 degrees lies between two blades at every count from 2
 * to 6, so the between-blades wording is exercised.
 *
 * @param ang Receives the angle, degrees from the index mark.
 * @return false when a node is linked and has no correction: nothing is
 *         drawn there.
 */
static bool balance_screen_angle(float *ang)
{
    vibe_solution_t sol;
    if (!node_live()) {
        *ang = 265.0f;
        return true;
    }
    if (node_correction(&sol)) {
        *ang = sol.deg;
        return true;
    }
    return false;
}

const char *balance_screen_model_mark(void)
{
    return (s.tabs.selected == BALANCE_PANE_MEASURE && !node_live())
               ? TR(BAL_MODELLED) : NULL;
}

const char *balance_screen_model_note(void)
{
    return (s.tabs.selected == BALANCE_PANE_MEASURE && !node_live())
               ? TR(BAL_MODEL_NOTE) : NULL;
}

/**
 * @brief Each frame, on the BALANCE pane with the keypad closed: a new
 *        solution or the node coming or going draws the screen again (the
 *        disc marks the correction); a new reading repaints the readings'
 *        values alone.  Other panes show nothing of the node: the
 *        revisions are only followed there.
 * @param dt Unused.
 */
static void tick(float dt)
{
    (void)dt;
    const uint32_t r = node_balance_rev();
    const uint32_t res = node_balance_result_rev();
    const bool live = node_live();
    const bool shown = s.tabs.selected == BALANCE_PANE_MEASURE && !s.kp.open;
    if (shown && (res != s.res_rev || live != s.node_was)) {
        ++s.rev;
    } else if (shown && r != s.node_rev) {
        ++s.read_rev;
    }
    s.node_rev = r;
    s.res_rev = res;
    s.node_was = live;
}

/**
 * @brief One touch event: the tabs first, then the keypad while it is
 *        open, then the BALANCE pane's controls.  The rotor buttons and the
 *        count's "-" and "+" act on the press; the count opens its keypad
 *        on the release of a tap on it.
 * @param evt The event, or NULL.
 */
static void event(const touch_event_t *evt)
{
    /* The tabs stay above the keypad: a pane switched takes the keypad
     * with it, the count as it was. */
    if (ui_tabs_event(&s.tabs, evt)) {
        s.value_down = false;
        kp_close();
        ++s.rev;
        return;
    }
    if (evt != NULL && s.kp.open) {
        kp_event(evt);
        return;
    }
    if (evt == NULL || s.tabs.selected != BALANCE_PANE_MEASURE) {
        return;
    }
    const int px = evt->point.x, py = evt->point.y;
    /* The count opens its keypad on the release of the press that came
     * down on it, over it, as a tap the finger made. */
    if (evt->type != TOUCH_EVENT_DOWN) {
        if (s.value_down && evt->point.id == s.value_id
            && evt->type == TOUCH_EVENT_UP) {
            s.value_down = false;
            if (touch_event_is_tap_up(evt)
                && gfx_rect_contains(balance_screen_blades_rect(), px, py)) {
                ui_keypad_open(&s.kp,
                               (gfx_rect_t){ PAD, KP_Y,
                                             (int16_t)(W - 2 * PAD), KP_H },
                               TR(BAL_BLADES), "", (float)s.blades,
                               (float)BLADES_MIN, (float)BLADES_MAX, 0);
                kp_follow();
            }
        }
        return;
    }
    if (gfx_rect_contains(balance_screen_blades_rect(), px, py)) {
        s.value_down = true;
        s.value_id   = evt->point.id;
        return;
    }
    for (int i = 0; i < ROTOR_COUNT; ++i) {
        if (gfx_rect_contains(s.rotor_btn[i], px, py)) {
            if (s.rotor != i) { s.rotor = i; ++s.rev; }
            return;
        }
    }
    /* Two to six.  Clamped, like every other stepper on this bench. */
    if (gfx_rect_contains(s.blade_dn, px, py)) {
        if (s.blades > BLADES_MIN) { --s.blades; ++s.rev; }
    } else if (gfx_rect_contains(s.blade_up, px, py)) {
        if (s.blades < BLADES_MAX) { ++s.blades; ++s.rev; }
    }
}

/* ----------------------------------------------------------------- drawing */

/**
 * @brief A dot on the part, a leader to a stated place, and the word there.
 *
 * The label position is a parameter rather than derived from the leader
 * direction, so a leader can be routed clear of the propeller.
 */
static void callout(gfx_canvas_t *c, int px, int py, int lx, int ly,
                    const char *text, gfx_color_t ink)
{
    const int w = gfx_text_width(BL_FONT_LABEL, text, 1);
    /* The leader meets the label at whichever end is nearer the part. */
    const int join_x = (px > lx + w) ? lx + w + 4 : lx - 4;
    gfx_capsule_aa(c, px, py, join_x, ly + 10, 2, ink);
    gfx_fill_circle_aa(c, px, py, 4, ink);
    gfx_text(c, lx, ly, text, BL_FONT_LABEL, ink, 1);
}

/**
 * @brief Where a callout's label starts so that it ends @p gap px left of
 *        @p right, or at @p lx where it already does.
 * @param lx    The label's start as drawn for a short word.
 * @param right The column it must end left of.
 * @param gap   The room left before @p right, px.
 * @param text  The label.
 * @return The label's start.
 */
static int left_of(int lx, int right, int gap, const char *text)
{
    const int end = right - gap - gfx_text_width(BL_FONT_LABEL, text, 1);
    return (lx < end) ? lx : end;
}

/** @brief Draws the rig pane. */
static void draw_rig(gfx_canvas_t *c)
{
    const gfx_color_t steel = ui_theme_color(UI_C_PANEL_HI);
    const gfx_color_t edge  = ui_theme_color(UI_C_EDGE);
    const gfx_color_t ink   = ui_theme_color(UI_C_ACCENT);
    const gfx_color_t warn  = ui_theme_color(UI_C_WARN);

    /* The bench itself, in the faint colour: everything else is bolted to
     * it and nothing here is about it. */
    gfx_fill_round_rect(c, 60, BASE_Y, 380, 18, 4, steel);
    gfx_draw_round_rect(c, 60, BASE_Y, 380, 18, 4, edge);
    for (int x = 70; x < 440; x += 22) {
        gfx_vline(c, x, BASE_Y + 18, 8, ui_theme_color(UI_C_GRID));
    }

    gfx_fill_round_rect(c, POST_X, 150, POST_W, BASE_Y - 150, 4, steel);
    gfx_draw_round_rect(c, POST_X, 150, POST_W, BASE_Y - 150, 4, edge);

    gfx_fill_round_rect(c, POST_X + POST_W, ARM_Y, MOTOR_X - POST_X - POST_W,
                        ARM_H, 4, steel);
    gfx_draw_round_rect(c, POST_X + POST_W, ARM_Y,
                        MOTOR_X - POST_X - POST_W, ARM_H, 4, edge);

    /*
     * An outrunner: the small static end bolted to the arm, and the bell --
     * the whole outer can -- turning in front of it.
     */
    gfx_fill_round_rect(c, MOTOR_X, SHAFT_Y - 22, 22, 44, 4,
                        ui_theme_color(UI_C_PANEL));
    gfx_draw_round_rect(c, MOTOR_X, SHAFT_Y - 22, 22, 44, 4, edge);

    const int bell_x = MOTOR_X + 20;
    const int bell_w = 54;
    gfx_fill_round_rect(c, bell_x, SHAFT_Y - 38, bell_w, 76, 8, steel);
    gfx_draw_round_rect(c, bell_x, SHAFT_Y - 38, bell_w, 76, 8, edge);
    /* Its cooling slots, which are also what tells you it is the part that
     * turns. */
    for (int i = 0; i < 4; ++i) {
        gfx_fill_round_rect(c, bell_x + 8 + i * 11, SHAFT_Y - 26, 5, 52, 2,
                            ui_theme_color(UI_C_GRID_STRONG));
    }
    gfx_capsule_aa(c, bell_x + bell_w, SHAFT_Y, SPIN_X, SHAFT_Y, 10, steel);

    /* The disc, edge on: two blades and the hub between them. */
    gfx_capsule_aa(c, SPIN_X, SHAFT_Y - 14, SPIN_X, SHAFT_Y - BLADE_L, 13,
                   ui_theme_color(UI_C_PANEL_HI));
    gfx_capsule_aa(c, SPIN_X, SHAFT_Y + 14, SPIN_X, SHAFT_Y + BLADE_L, 13,
                   ui_theme_color(UI_C_PANEL_HI));
    /* A hub, not a hole: filled light with a dark center.  A dark disc with a
     * light center reads as a gap in the blades. */
    gfx_fill_circle_aa(c, SPIN_X, SHAFT_Y, 18, steel);
    gfx_fill_circle_aa(c, SPIN_X, SHAFT_Y, 6, ui_theme_color(UI_C_PANEL));

    /*
     * The accelerometer, on the arm and hard against the motor: everything
     * between the bearing and the sensor is a spring, and the arm by the
     * motor is the last rigid place.
     */
    gfx_fill_round_rect(c, MOTOR_X - 42, ARM_Y - 22, 34, 22, 3, ink);
    gfx_draw_round_rect(c, MOTOR_X - 42, ARM_Y - 22, 34, 22, 3, edge);
    /* Its sensitive axis, across the shaft rather than along it: that is the
     * direction an out-of-balance disc pulls. */
    const int ax = MOTOR_X - 25;
    gfx_capsule_aa(c, ax, ARM_Y - 54, ax, ARM_Y - 26, 3, ink);
    gfx_capsule_aa(c, ax - 7, ARM_Y - 47, ax, ARM_Y - 56, 3, ink);
    gfx_capsule_aa(c, ax + 7, ARM_Y - 47, ax, ARM_Y - 56, 3, ink);
    gfx_capsule_aa(c, ax, ARM_Y + 30, ax, ARM_Y + 58, 3, ink);
    gfx_capsule_aa(c, ax - 7, ARM_Y + 51, ax, ARM_Y + 60, 3, ink);
    gfx_capsule_aa(c, ax + 7, ARM_Y + 51, ax, ARM_Y + 60, 3, ink);

    /*
     * The mark goes on the bell, and the sensor looks straight up at it.
     *
     * Not the spinner: a rig often runs without one, and a beam aimed at the
     * nose has to come from in front, across the disc.  The bell turns with
     * the shaft, is rigid, is present whichever prop is fitted, and can be
     * watched from underneath where nothing is in the way.
     */
    const int mark_x = bell_x + bell_w / 2;
    /* Drawn dark with a bright edge, because it is a pen line and not a
     * flag: anything stuck on the bell is mass, on the one part whose mass
     * is the thing being measured. */
    gfx_fill_round_rect(c, mark_x - 9, SHAFT_Y + 30, 18, 10, 2,
                        ui_theme_color(UI_C_BG));
    gfx_draw_round_rect(c, mark_x - 9, SHAFT_Y + 30, 18, 10, 2, warn);

    gfx_fill_round_rect(c, mark_x - 17, 296, 34, 24, 3, warn);
    gfx_draw_round_rect(c, mark_x - 17, 296, 34, 24, 3, edge);
    for (int i = 0; i < 5; ++i) {
        gfx_fill_circle_aa(c, mark_x, 292 - i * 10, 2, warn);
    }

    callout(c, MOTOR_X - 25, ARM_Y - 12, 24, 62, TR(BAL_ACCELEROMETER), ink);
    /* The mark's label right of the sensor and under the disc, where the
     * longest word has room; the sensor's label left, under the arm. */
    callout(c, mark_x, SHAFT_Y + 36, SPIN_X - 18, 322, TR(BAL_ONE_MARK),
            warn);
    callout(c, mark_x, 308, left_of(196, mark_x - 17, 10, TR(BAL_OPTICAL)),
            332, TR(BAL_OPTICAL), warn);
}

/**
 * @brief The same two sensors on a complete aircraft, where the rigid faces are
 * wherever the designer put them, the cowl is in the way, and the airframe
 * rocks unless it is tied down.
 */
static void draw_aircraft(gfx_canvas_t *c)
{
    const gfx_color_t skin  = ui_theme_color(UI_C_PANEL_HI);
    const gfx_color_t edge  = ui_theme_color(UI_C_EDGE);
    const gfx_color_t ink   = ui_theme_color(UI_C_ACCENT);
    const gfx_color_t warn  = ui_theme_color(UI_C_WARN);
    const gfx_color_t faint = ui_theme_color(UI_C_TEXT_FAINT);

    const int ground = 372;
    const int axis   = 214;              /* the thrust line          */
    const int fw     = 268;              /* the firewall             */
    const int prop   = 388;              /* the disc, edge on        */

    gfx_hline(c, 24, ground, 412, faint);
    for (int x = 28; x < 436; x += 16) {
        gfx_capsule_aa(c, x, ground + 2, x - 6, ground + 10, 1, faint);
    }

    /*
     * The fuselage: a deep nose and a tapering boom, recognisable as an
     * aeroplane.  The subject is the sensor placement.
     */
    gfx_fill_round_rect(c, 28, axis - 16, 120, 34, 12, skin);
    gfx_draw_round_rect(c, 28, axis - 16, 120, 34, 12, edge);
    gfx_fill_round_rect(c, 100, axis - 32, fw - 100, 64, 16, skin);
    gfx_draw_round_rect(c, 100, axis - 32, fw - 100, 64, 16, edge);

    /* Wing at the root, tailplane and fin at the back. */
    gfx_fill_round_rect(c, 120, axis + 24, 130, 12, 5,
                        ui_theme_color(UI_C_PANEL));
    gfx_draw_round_rect(c, 120, axis + 24, 130, 12, 5, edge);
    gfx_fill_round_rect(c, 24, axis - 4, 56, 10, 4,
                        ui_theme_color(UI_C_PANEL));
    gfx_draw_round_rect(c, 24, axis - 4, 56, 10, 4, edge);
    gfx_capsule_aa(c, 34, axis - 14, 50, axis - 58, 13, skin);

    /*
     * The cowl, drawn as a separate piece: a fairing screwed to a former,
     * often on rubber, and free to move relative to the motor mount.  Drawn
     * short, because on most electric models the outrunner stands proud of
     * it.
     */
    gfx_fill_round_rect(c, fw, axis - 36, 62, 72, 12,
                        ui_theme_color(UI_C_PANEL));
    gfx_draw_round_rect(c, fw, axis - 36, 62, 72, 12, edge);

    /* The firewall behind it: the one rigid face at this end of the model. */
    for (int y = axis - 32; y < axis + 32; y += 8) {
        gfx_vline(c, fw, y, 5, ink);
    }

    /* The bell, ahead of the cowl and turning. */
    const int bell_x = fw + 58;
    const int bell_w = 50;
    gfx_fill_round_rect(c, bell_x, axis - 32, bell_w, 64, 8, skin);
    gfx_draw_round_rect(c, bell_x, axis - 32, bell_w, 64, 8, edge);
    for (int i = 0; i < 4; ++i) {
        gfx_fill_round_rect(c, bell_x + 7 + i * 10, axis - 22, 5, 44, 2,
                            ui_theme_color(UI_C_GRID_STRONG));
    }

    /* Spinner and disc. */
    gfx_capsule_aa(c, bell_x + bell_w, axis, prop - 6, axis, 9, skin);
    gfx_capsule_aa(c, prop, axis - 12, prop, axis - 86, 12, skin);
    gfx_capsule_aa(c, prop, axis + 12, prop, axis + 86, 12, skin);
    gfx_fill_circle_aa(c, prop, axis, 15, skin);
    gfx_fill_circle_aa(c, prop, axis, 5, ui_theme_color(UI_C_PANEL));

    /*
     * The accelerometer on the firewall, flat.
     *
     * Flat is fine and worth saying: the chip has three axes, and mounting it
     * against the firewall puts two of them in the firewall's plane, which is
     * across the shaft.  Use one of those and ignore the third.
     */
    gfx_fill_round_rect(c, fw - 34, axis - 44, 32, 20, 3, ink);
    gfx_draw_round_rect(c, fw - 34, axis - 44, 32, 20, 3, edge);
    const int ax = fw - 18;
    gfx_capsule_aa(c, ax, axis - 76, ax, axis - 50, 3, ink);
    gfx_capsule_aa(c, ax - 7, axis - 69, ax, axis - 78, 3, ink);
    gfx_capsule_aa(c, ax + 7, axis - 69, ax, axis - 78, 3, ink);

    /*
     * The mark on the bell here too: a spinner comes off, and every time it
     * is refitted it lands at a new angle, taking the phase reference of the
     * previous balance with it.  The bell is part of the rotor.
     */
    const int mark_x = bell_x + bell_w / 2;
    gfx_fill_round_rect(c, mark_x - 9, axis + 24, 18, 10, 2,
                        ui_theme_color(UI_C_BG));
    gfx_draw_round_rect(c, mark_x - 9, axis + 24, 18, 10, 2, warn);

    gfx_fill_round_rect(c, mark_x - 15, 300, 30, 24, 3, warn);
    gfx_draw_round_rect(c, mark_x - 15, 300, 30, 24, 3, edge);
    gfx_capsule_aa(c, mark_x, 324, mark_x, ground, 4, faint);
    for (int i = 0; i < 4; ++i) {
        gfx_fill_circle_aa(c, mark_x, 294 - i * 10, 2, warn);
    }

    /* Tied down at the tail: a free-standing airframe is a spring in series
     * with the measurement. */
    gfx_capsule_aa(c, 60, axis + 16, 52, ground, 3, warn);
    gfx_capsule_aa(c, 42, ground - 4, 62, ground - 4, 6, warn);

    callout(c, ax, axis - 34, 24, 62, TR(BAL_ACCELEROMETER), ink);
    callout(c, fw, axis + 18, 132, 288, TR(BAL_FIREWALL), ink);
    /* Down and right, under the disc: between the bell and the disc there
     * is no gap for a leader to climb through, and SECURED holds the room
     * on the left. */
    callout(c, mark_x, axis + 30, prop + 6, 344, TR(BAL_ONE_MARK), warn);
    callout(c, 52, ground - 12, 96, 344, TR(BAL_TIED_DOWN), warn);
}

/**
 * @brief A point on the disc, measured from the index mark and drawn with zero at
 * the top so the picture agrees with the number.
 */
static void disc_at(float deg, int r, int *x, int *y)
{
    const float k = 3.14159265358979f / 180.0f;
    const float a = (90.0f - deg) * k;
    *x = DISC_CX + (int)((float)r * cosf(a) + 0.5f);
    *y = DISC_CY - (int)((float)r * sinf(a) + 0.5f);
}

/** @brief Draws the measurement pane. */
static void draw_measure(gfx_canvas_t *c)
{
    const gfx_color_t skin  = ui_theme_color(UI_C_PANEL_HI);
    const gfx_color_t edge  = ui_theme_color(UI_C_EDGE);
    const gfx_color_t ink   = ui_theme_color(UI_C_ACCENT);
    const gfx_color_t warn  = ui_theme_color(UI_C_WARN);
    const gfx_color_t faint = ui_theme_color(UI_C_TEXT_FAINT);

    /* A duct, if there is one.  It is the reason an EDF is a different job:
     * you cannot reach a blade tip, so the correction goes on the hub. */
    /*
     * The rings are pairs of filled discs, not thin arcs.  An arc closed on
     * itself lays some pixels twice and misses others, and at 1 or 2px wide
     * that stipples.  They are safe to fill because they are drawn before
     * the blades.
     */
    const gfx_color_t card = ui_theme_color(UI_C_PANEL);
    if (s.rotor == ROTOR_EDF) {
        gfx_fill_circle_aa(c, DISC_CX, DISC_CY, DISC_R + 26, edge);
        gfx_fill_circle_aa(c, DISC_CX, DISC_CY, DISC_R + 22, card);
        gfx_fill_circle_aa(c, DISC_CX, DISC_CY, DISC_R + 13,
                           ui_theme_color(UI_C_GRID));
        gfx_fill_circle_aa(c, DISC_CX, DISC_CY, DISC_R + 11, card);
    }
    gfx_fill_circle_aa(c, DISC_CX, DISC_CY, DISC_R,
                       ui_theme_color(UI_C_GRID));
    gfx_fill_circle_aa(c, DISC_CX, DISC_CY, DISC_R - 1, card);

    for (int i = 0; i < s.blades; ++i) {
        const float a = (float)i * 360.0f / (float)s.blades;
        int rx, ry, tx, ty;
        /* Two strokes of falling width: a blade is wide at the root and
         * narrow at the tip, and a single capsule reads as a stick. */
        int mx2, my2;
        disc_at(a, HUB_R - 4, &rx, &ry);
        disc_at(a, (DISC_R + HUB_R) / 2, &mx2, &my2);
        disc_at(a, DISC_R - 6, &tx, &ty);
        gfx_capsule_aa(c, rx, ry, mx2, my2, 24, skin);
        gfx_capsule_aa(c, mx2, my2, tx, ty, 16, skin);
        /* Numbered from the index mark, which is what makes an angle into an
         * instruction rather than a reading. */
        char n[16];
        snprintf(n, sizeof(n), "%d", (i + 1) & 0xFF);
        int lx, ly;
        disc_at(a, DISC_R + (s.rotor == ROTOR_EDF ? 38 : 18), &lx, &ly);
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(lx - 12), (int16_t)(ly - 10),
                                     24, 20 },
                    n, BL_FONT_LABEL, faint, 1, GFX_ALIGN_CENTER);
    }

    gfx_fill_circle_aa(c, DISC_CX, DISC_CY, HUB_R, skin);
    gfx_fill_circle_aa(c, DISC_CX, DISC_CY, 10, ui_theme_color(UI_C_PANEL));

    /* The index mark, at zero, because every angle here is from it. */
    int mx, my;
    disc_at(0.0f, HUB_R - 9, &mx, &my);
    gfx_fill_round_rect(c, mx - 6, my - 5, 12, 10, 2, ui_theme_color(UI_C_BG));
    gfx_draw_round_rect(c, mx - 6, my - 5, 12, 10, 2, warn);

    /* And where the mass goes: nothing while a node has no answer yet. */
    float ang;
    if (!balance_screen_angle(&ang)) {
        return;
    }
    int ax, ay, bx, by;
    disc_at(ang, HUB_R + 6, &ax, &ay);
    disc_at(ang, DISC_R - 16, &bx, &by);
    gfx_capsule_aa(c, ax, ay, bx, by, 3, ink);
    /* A band of arc at the answer, so the eye finds it before the numbers. */
    gfx_arc(c, DISC_CX, DISC_CY, DISC_R - 16, 10, 90.0f - ang - 16.0f,
            90.0f - ang + 16.0f, ink);
    gfx_fill_circle_aa(c, bx, by, 11, ink);
    gfx_fill_circle_aa(c, bx, by, 5, ui_theme_color(UI_C_PANEL));
}

/* The placement rules, numbered. */
/** @brief One placement rule: a heading and two lines. */
typedef struct {
    ui_text_id_t head,   /**< the rule's heading */
                 line1,  /**< its first line */
                 line2;  /**< its second line */
} rule_t;

/** @brief the rig's placement rules */
static const rule_t k_rig_rules[] = {
    { TX_BAL_RIG_1, TX_BAL_RIG_1A, TX_BAL_RIG_1B },
    { TX_BAL_RIG_2, TX_BAL_RIG_2A, TX_BAL_RIG_2B },
    { TX_BAL_RIG_3, TX_BAL_RIG_3A, TX_BAL_RIG_3B },
    { TX_BAL_RIG_4, TX_BAL_RIG_4A, TX_BAL_RIG_4B },
    { TX_BAL_RIG_5, TX_BAL_RIG_5A, TX_BAL_RIG_5B },
    { TX_BAL_RIG_6, TX_BAL_RIG_6A, TX_BAL_RIG_6B },
};
/** @brief how many rules k_rig_rules holds */
#define RIG_RULES ((int)(sizeof(k_rig_rules) / sizeof(k_rig_rules[0])))

#define AIR_RULES 6  /**< how many rules k_air_rules holds */
/** @brief the aircraft's placement rules */
static const rule_t k_air_rules[] = {
    { TX_BAL_AIR_1, TX_BAL_AIR_1A, TX_BAL_AIR_1B },
    { TX_BAL_AIR_2, TX_BAL_AIR_2A, TX_BAL_AIR_2B },
    { TX_BAL_AIR_3, TX_BAL_AIR_3A, TX_BAL_AIR_3B },
    { TX_BAL_AIR_4, TX_BAL_AIR_4A, TX_BAL_AIR_4B },
    { TX_BAL_AIR_5, TX_BAL_AIR_5A, TX_BAL_AIR_5B },
    { TX_BAL_AIR_6, TX_BAL_AIR_6A, TX_BAL_AIR_6B },
};

/* Which blades the correction falls between, which is the whole reason the
 * count is asked for.  The arithmetic is public and takes the count as an
 * argument so it can be checked on its own, away from the screen's state. */
void balance_nearest_blades(float angle, int blades, int *lo, int *hi)
{
    if (blades < 1) { blades = 1; }
    const float step = 360.0f / (float)blades;
    int before = (int)(angle / step);
    if (before < 0) { before = 0; }
    *lo = (before % blades) + 1;
    *hi = ((before + 1) % blades) + 1;
}

/**
 * @brief WHERE's text: the hub angle on a ducted fan, else the two blades
 *        the angle falls between.
 * @param ang The angle, degrees from the index mark.
 * @param out The text.  @param n Its size.
 */
static void where_text(float ang, char *out, size_t n)
{
    if (s.rotor == ROTOR_EDF) {
        /* A blade tip inside a duct is not reachable, so the answer is an
         * angle on the hub rather than a blade to tape. */
        snprintf(out, n, TR(BAL_HUB), (int)(ang + 0.5f) % 360 & 0x1FF);
    } else {
        int lo, hi;
        balance_nearest_blades(ang, s.blades, &lo, &hi);
        /* Bounded so the compiler can see it: both are 1..6, but the
         * format is not told so. */
        snprintf(out, n, TR(BAL_BETWEEN), lo & 0xFF, hi & 0xFF);
    }
}

/**
 * @brief The readings' rows from READ_Y, READ_H each: the name dim at the
 *        left, the value at the right, accent from row @p accent_from.
 * @param c The canvas.  @param k The names.  @param v The values.
 * @param n The rows.  @param accent_from The first row in accent.
 */
static void draw_rows(gfx_canvas_t *c, const char *const *k,
                      const char *const *v, int n, int accent_from)
{
    for (int i = 0; i < n; ++i) {
        const int y = READ_Y + i * READ_H + (READ_H - 20) / 2;
        gfx_text(c, RC_X, y, k[i], BL_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
        gfx_text_in(c, (gfx_rect_t){ (int16_t)(RC_X + 110), (int16_t)y,
                                     (int16_t)(RC_W - 110), 20 },
                    v[i], BL_FONT_LABEL,
                    (i >= accent_from) ? ui_theme_color(UI_C_ACCENT)
                                       : ui_theme_color(UI_C_TEXT), 1,
                    GFX_ALIGN_RIGHT);
    }
}

/**
 * @brief Without a node: the model's four readings, the SIMULATED pill and
 *        the note.
 * @param c The canvas.
 */
static void draw_model_rows(gfx_canvas_t *c)
{
    const int x = RC_X;
    const int w = RC_W;
    float ang = 0.0f;
    (void)balance_screen_angle(&ang);
    char v_amp[32], v_ang[32], v_mass[32], v_where[32];
    snprintf(v_amp,  sizeof(v_amp),  "0.42g");
    snprintf(v_ang,  sizeof(v_ang),  "%ddeg", (int)ang);
    snprintf(v_mass, sizeof(v_mass), "0.35g");
    where_text(ang, v_where, sizeof(v_where));
    const char *const k[] = { TR(BAL_VIBRATION), TR(BAL_ANGLE), TR(BAL_ADD),
                              TR(BAL_WHERE) };
    const char *const v[] = { v_amp, v_ang, v_mass, v_where };
    draw_rows(c, k, v, 4, 2);

    /*
     * The angle and both masses are fixed values, not a measurement: the
     * overview's pill, 88px or as wide as a longer word, 20px high, and
     * the words beside it.
     */
    int mw = 24 + gfx_text_width(UI_FONT_LABEL, TR(BAL_MODELLED), 1);
    mw = (mw < 88) ? 88 : mw;
    ui_pill(c, (gfx_rect_t){ (int16_t)x, MARK_Y, (int16_t)mw, 20 },
            TR(BAL_MODELLED), ui_theme_color(UI_C_WARN),
            ui_theme_color(UI_C_PANEL_SUNK));
    gfx_text(c, x + mw + 8, MARK_Y, TR(BAL_MODEL_NOTE),
             BL_FONT_LABEL, ui_theme_color(UI_C_TEXT_DIM), 1);

    gfx_hline(c, x, NOTE_Y - 10, w, ui_theme_color(UI_C_EDGE));
    gfx_text(c, x, NOTE_Y, TR(BAL_NOTE_1), BL_FONT_FINE,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    gfx_text(c, x, NOTE_Y + 18, TR(BAL_NOTE_2), BL_FONT_FINE,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
    gfx_text(c, x, NOTE_Y + 42, TR(BAL_NOTE_3), BL_FONT_FINE,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
}

/** @brief The node's live rows: VIBRATION, ANGLE, SPEED and SIGNAL. */
#define NODE_LIVE_ROWS 4

/** @brief The text of one live row's value. */
typedef char live_text_t[32];

/**
 * @brief The node's live values: magnitude, angle, rpm and quality word,
 *        "--" while its report is stale.
 * @param v Receives the four values.
 */
static void live_values(live_text_t v[NODE_LIVE_ROWS])
{
    const nb_reading_t *r = node_balance_live();
    (void)nb_fmt_mag(r, v[0], sizeof(v[0]));
    (void)nb_fmt_deg(r, v[1], sizeof(v[1]));
    (void)nb_fmt_rpm(r, v[2], sizeof(v[2]));
    const nb_quality_t q = (nb_quality_t)r->word;
    if (r->live == NB_LIVE_FRESH && q >= NB_Q_GOOD && q <= NB_Q_POOR) {
        snprintf(v[3], sizeof(v[3]), "%s %u%%", nb_quality_text(q),
                 (unsigned)r->quality);
    } else {
        snprintf(v[3], sizeof(v[3]), "%s", r->live == NB_LIVE_FRESH
                                               ? nb_quality_text(q) : "--");
    }
}

/**
 * @brief A new reading: the four live values repainted in their boxes on
 *        the card, nothing else.
 * @param c The canvas.
 */
static void draw_node_live(gfx_canvas_t *c)
{
    live_text_t v[NODE_LIVE_ROWS];
    live_values(v);
    for (int i = 0; i < NODE_LIVE_ROWS; ++i) {
        const gfx_rect_t box = { (int16_t)(RC_X + 110),
                                 (int16_t)(READ_Y + i * READ_H
                                           + (READ_H - 20) / 2),
                                 (int16_t)(RC_W - 110), 20 };
        gfx_fill_rect(c, box.x, box.y, box.w, box.h,
                      ui_theme_color(UI_C_PANEL));
        gfx_text_in(c, box, v[i], BL_FONT_LABEL, ui_theme_color(UI_C_TEXT),
                    1, GFX_ALIGN_RIGHT);
    }
}

/**
 * @brief With a node: its magnitude, angle, rpm and quality word, "--"
 *        while its report is stale, then ADD and WHERE from the runs, and
 *        the line that names where the runs are taken.
 * @param c The canvas.
 */
static void draw_node_rows(gfx_canvas_t *c)
{
    live_text_t live[NODE_LIVE_ROWS];
    live_values(live);
    char v_mass[24], v_where[32];
    vibe_solution_t sol;
    if (node_correction(&sol)) {
        snprintf(v_mass, sizeof(v_mass), "%.2fg", (double)sol.mass);
        where_text(sol.deg, v_where, sizeof(v_where));
    } else {
        snprintf(v_mass, sizeof(v_mass), "--");
        snprintf(v_where, sizeof(v_where), "--");
    }
    const char *const k[] = { TR(BAL_VIBRATION), TR(BAL_ANGLE),
                              TR(BAL_SPEED), TR(BAL_SIGNAL), TR(BAL_ADD),
                              TR(BAL_WHERE) };
    const char *const v[] = { live[0], live[1], live[2], live[3], v_mass,
                              v_where };
    draw_rows(c, k, v, 6, NODE_LIVE_ROWS);
    gfx_hline(c, RC_X, NODE_NOTE_Y - 10, RC_W, ui_theme_color(UI_C_EDGE));
    gfx_text(c, RC_X, NODE_NOTE_Y, TR(BAL_LIVE_NOTE), BL_FONT_FINE,
             ui_theme_color(UI_C_TEXT_FAINT), 1);
}

/**
 * @brief The BALANCE pane's right column: ROTOR, BLADES, then the node's
 *        six readings, or the model's four with the SIMULATED pill and the
 *        note.
 * @param c The canvas.
 */
static void draw_readings(gfx_canvas_t *c)
{
    const int x = RC_X;
    const int w = RC_W;

    gfx_text(c, x, CARD_Y + 12, "ROTOR", BL_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);
    for (int i = 0; i < ROTOR_COUNT; ++i) {
        ui_button_font(c, s.rotor_btn[i],
                       (i == ROTOR_PROP) ? "PROPELLER" : "EDF",
                       (s.rotor == i) ? ui_theme_color(UI_C_ACCENT)
                                      : ui_theme_color(UI_C_PANEL_HI),
                       false, true, BL_FONT_LABEL);
    }

    char buf[32];
    const int ty = BLADES_Y + (STEP_H - 20) / 2;
    gfx_text(c, x, ty, TR(BAL_BLADES), BL_FONT_LABEL,
             ui_theme_color(UI_C_TEXT_DIM), 1);
    snprintf(buf, sizeof(buf), "%d", s.blades & 0xFF);
    const gfx_rect_t count = balance_screen_blades_rect();
    gfx_text_in(c, (gfx_rect_t){ count.x, (int16_t)ty, 40, 20 },
                buf, BL_FONT_LABEL, ui_theme_color(UI_C_TEXT), 1,
                GFX_ALIGN_RIGHT);
    ui_button_font(c, s.blade_dn, "-", ui_theme_color(UI_C_PANEL_HI),
                   false, s.blades > 2, BL_FONT_LABEL);
    ui_button_font(c, s.blade_up, "+", ui_theme_color(UI_C_PANEL_HI),
                   false, s.blades < 6, BL_FONT_LABEL);

    gfx_hline(c, x, READ_Y - 8, w, ui_theme_color(UI_C_EDGE));

    if (node_live()) {
        draw_node_rows(c);
    } else {
        draw_model_rows(c);
    }
}

/**
 * @brief TEST RIG's or AIRCRAFT's right column: COMMON MISTAKES and the
 *        numbered rules, each its head over its two lines.
 * @param c     The canvas.
 * @param rules The rules.
 * @param count How many.
 */
static void draw_rules(gfx_canvas_t *c, const rule_t *rules, int count)
{
    const int x = RC_X;
    gfx_text(c, x, CARD_Y + 10, TR(BAL_WRONG), BL_FONT_LABEL,
             ui_theme_color(UI_C_ACCENT), 1);

    for (int i = 0; i < count; ++i) {
        const int y = RULE_Y0 + i * RULE_PITCH;
        /* Bounded so the compiler can see it: count is small, but the
         * format is not told so. */
        char n[16];
        snprintf(n, sizeof(n), "%d", (i + 1) & 0xFF);
        gfx_fill_round_rect(c, x, y - 2, 24, 24, 4,
                            ui_theme_color(UI_C_PANEL_HI));
        gfx_text_in(c, (gfx_rect_t){ (int16_t)x, (int16_t)y, 24, 20 },
                    n, BL_FONT_LABEL, ui_theme_color(UI_C_ACCENT), 1,
                    GFX_ALIGN_CENTER);
        gfx_text(c, x + 32, y, ui_tr(rules[i].head), BL_FONT_LABEL,
                 ui_theme_color(UI_C_TEXT), 1);
        gfx_text(c, x + 32, y + RULE_L1, ui_tr(rules[i].line1),
                 BL_FONT_FINE, ui_theme_color(UI_C_TEXT_DIM), 1);
        gfx_text(c, x + 32, y + RULE_L2, ui_tr(rules[i].line2), BL_FONT_FINE,
                 ui_theme_color(UI_C_TEXT_DIM), 1);
    }
}

/**
 * @brief The screen drawn on a revision not yet drawn: the tabs, then the
 *        keypad alone while it is open, else both cards of the pane.
 * @param c The canvas.
 * @param buffer_index The framebuffer, whose revision it follows.
 */
static void render(gfx_canvas_t *c, int buffer_index)
{
    const unsigned bit = 1u << (buffer_index & 1);
    const int buf = buffer_index & 1;

    if ((s.drawn_mask & bit) == 0) {
        gfx_clear(c, ui_theme_color(UI_C_BG));
        s.drawn_mask |= bit;
    }
    if (s.drawn[buf] == s.rev) {
        if (s.read_drawn[buf] != s.read_rev) {
            s.read_drawn[buf] = s.read_rev;
            if (s.tabs.selected == BALANCE_PANE_MEASURE && !s.kp.open
                && node_live()) {
                draw_node_live(c);
            }
        }
        return;
    }
    s.drawn[buf] = s.rev;
    s.read_drawn[buf] = s.read_rev;
    gfx_clear(c, ui_theme_color(UI_C_BG));

    ui_tabs_render(&s.tabs, c);
    if (s.kp.open) {
        /* The blade count typed: the keypad covers both cards, and
         * nothing under it is drawn, as nothing under it presses. */
        ui_keypad_render(&s.kp, c);
        return;
    }
    ui_card(c, (gfx_rect_t){ PAD, CARD_Y, LCARD_W, CARD_H },
            ui_theme_color(UI_C_PANEL));
    ui_card(c, (gfx_rect_t){ RCARD_X, CARD_Y, RCARD_W, CARD_H },
            ui_theme_color(UI_C_PANEL));
    /* The footer: the screen's name; BALANCE has no hold and no list. */
    ui_footer_name(c, ui_footer_rect(UI_FOOTER_NAME), "BALANCE", NULL, false);

    if (s.tabs.selected == BALANCE_PANE_MEASURE) {
        draw_measure(c);
        draw_readings(c);
        return;
    }
    /* The drawing in its window, centered in the card. */
    gfx_canvas_t fig;
    if (gfx_canvas_sub(c, (gfx_rect_t){ FIG_X, FIG_Y,
                                        (int16_t)(PAD + LCARD_W - 4 - FIG_X),
                                        (int16_t)(TEXT_FLOOR - FIG_Y) },
                       &fig)) {
        if (s.tabs.selected == BALANCE_PANE_RIG) {
            draw_rig(&fig);
        } else {
            draw_aircraft(&fig);
        }
    }
    draw_rules(c, (s.tabs.selected == BALANCE_PANE_RIG) ? k_rig_rules
                                                        : k_air_rules,
               (s.tabs.selected == BALANCE_PANE_RIG) ? RIG_RULES
                                                     : AIR_RULES);
}

/**
 * @brief The panel could not hand over every touch event, so this screen's
 *        record of the glass is stale.
 *
 * The tab row, a press on the blade count and a keypad key hold state
 * between a press and its release: a press kept would own a track id the
 * controller reuses, and a later contact that began elsewhere and lifts
 * over the tab would switch the pane on a tap nobody made.  Each is let
 * go; what the keypad has typed stays.
 */
static void cancel(void)
{
    ui_tabs_cancel(&s.tabs);
    s.value_down = false;
    ui_keypad_cancel_press(&s.kp);
    /* The tab row may already be drawn pressed in both framebuffers; a
     * cleared press with the same revision would leave it painted held. */
    ++s.rev;
}

/**
 * @brief ui_screen_t's covered: the keypad.
 * @return true while one is up
 */
static bool covered(void) { return s.kp.open; }

/** @brief The screen's table. */
static const ui_screen_t k_screen = {
    .covered = covered,
    .title  = "BALANCE",
    .reset  = reset,
    .enter  = enter,
    .leave  = leave,
    .tick   = tick,
    .event  = event,
    .cancel = cancel,
    .render = render,
    .back   = back,
};

const ui_screen_t *balance_screen(void) { return &k_screen; }
