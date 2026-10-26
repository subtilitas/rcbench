/**
 * @file hitec_panel_text.c
 * @brief The texts of the Hitec programmer's overlay: the rows' labels, the
 *        values as read, the PORT row, the state line and the messages.
 *
 * Hitec is a name or trademark of its owner; it is used here only to say
 * which servos this overlay works with.  The state and the seam are
 * hitec_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "hitec_panel.h"

#include <stdio.h>
#include <string.h>

#include "hitec_reg.h"
#include "link_pages.h"
#include "ui_text.h"

/* Last: its layout macros and hp, the state, which no header included
 * after it may meet. */
#include "hitec_panel_int.h"

/** @brief The classic model name row, as hitec_panel.c names it. */
#define W_NAME ((uint8_t)HITEC_P_COUNT)
/** @brief The degree sign, UTF-8. */
#define DEG "\xc2\xb0"

/* ----------------------------------------------------------------- labels */

/**
 * @brief The series' name.
 * @param series hitec_family_t
 * @return the text
 */
static const char *series_name(uint8_t series)
{
    return series == (uint8_t)HITEC_CLASSIC ? TR(HP_SERIES_C)
                                            : TR(HP_SERIES_D);
}

const char *hpn_label(const hpn_row_t *r)
{
    const hitec_model_t *m = hitec_prog_model(&hp.prog);

    if (r->kind == K_ACT && r->what == A_READ && m != NULL) {
        /* Static: the caller draws the label after this returns. */
        static char servo[24];
        const char *name = hitec_model_name(m->model);

        if (name[0] != '\0') {
            snprintf(servo, sizeof(servo), "%s", name);
        } else if (m->name[0] != '\0') {
            snprintf(servo, sizeof(servo), "%.14s", m->name);
        } else {
            snprintf(servo, sizeof(servo), "SERVO %u", (unsigned)m->number);
        }
        return servo;
    }
    return r->label >= 0 ? ui_tr((ui_text_id_t)r->label) : r->name;
}

/* ------------------------------------------------------------------ values */

/**
 * @brief The words of a CHOICE value of @p id.
 * @param id    hitec_param_t
 * @param value the decoded value
 * @return the text
 */
static const char *choice_text(uint8_t id, int32_t value)
{
    switch (id) {
    case HITEC_P_D_DIRECTION:
    case HITEC_P_C_DIRECTION:
        return value == HITEC_CCW ? "CCW" : "CW";
    case HITEC_P_C_RESOLUTION:
        return value == HITEC_RES_HIGH ? TR(HP_RES_HIGH) : TR(HP_RES_NORMAL);
    case HITEC_P_C_CHECKSUM:
        return value == HITEC_SUM_OK ? "OK" : TR(HP_SUM_BAD_V);
    default:
        return value == HITEC_ON ? TR(ON) : TR(OFF);
    }
}

/**
 * @brief A decoded value in its unit, with the stored value beside it where
 *        the two differ.
 * @param id   hitec_param_t
 * @param unit hitec_unit_t
 * @param v    the value
 * @param out  the text
 * @param n    bytes at @p out
 */
static void fmt_value(uint8_t id, uint8_t unit, const hitec_value_t *v,
                      char *out, size_t n)
{
    const long val = (long)v->value;
    const unsigned long raw = (unsigned long)v->raw;
    char shown[40];
    bool beside = true;

    switch (unit) {
    case HITEC_UNIT_COUNTS: {
        /* 220 degrees over 16383 counts, 8192 the centre, in tenths. */
        const long d = (val - 8192L) * 2200L;
        const long tenths = (d >= 0 ? d + 8191L : d - 8191L) / 16383L;

        snprintf(shown, sizeof(shown), "%ld  %c%ld.%ld" DEG, val,
                 tenths < 0 ? '-' : '+', (tenths < 0 ? -tenths : tenths) / 10,
                 (tenths < 0 ? -tenths : tenths) % 10);
        beside = false;
        break;
    }
    case HITEC_UNIT_PERCENT:
        snprintf(shown, sizeof(shown), "%ld%%", val);
        break;
    case HITEC_UNIT_STEP:
        snprintf(shown, sizeof(shown), "%ld", val);
        break;
    case HITEC_UNIT_CHOICE:
        snprintf(shown, sizeof(shown), "%s", choice_text(id, v->value));
        break;
    case HITEC_UNIT_FS_US:
        if (val == HITEC_FS_LIMP) {
            snprintf(shown, sizeof(shown), "%s", TR(HP_FS_LIMP));
        } else if (val == HITEC_FS_OFF) {
            snprintf(shown, sizeof(shown), "%s", TR(OFF));
        } else {
            snprintf(shown, sizeof(shown), "%ldus", val);
        }
        break;
    case HITEC_UNIT_OVERLOAD:
        if (val == HITEC_OVERLOAD_OFF) {
            snprintf(shown, sizeof(shown), "%s", TR(OFF));
        } else {
            snprintf(shown, sizeof(shown), "%ld%%", val);
        }
        break;
    case HITEC_UNIT_QUARTER_US:
        snprintf(shown, sizeof(shown), "%ld.%02ldus", val / 4L,
                 (val % 4L) * 25L);
        break;
    case HITEC_UNIT_DATE:
        snprintf(shown, sizeof(shown), "%04ld-%02ld-%02ld", val / 10000L,
                 val / 100L % 100L, val % 100L);
        beside = false;
        break;
    case HITEC_UNIT_NUMBER:
        snprintf(shown, sizeof(shown), "%07ld", val);
        beside = false;
        break;
    default:
        /* RAW: the register as a number, a D series one in hexadecimal
         * too. */
        if (id == HITEC_P_C_VERSION) {
            snprintf(shown, sizeof(shown), "0x%02lX", raw & 0xFFul);
        } else if (id == HITEC_P_D_MODEL || id == HITEC_P_D_ID
                   || id == HITEC_P_C_LEFT_END || id == HITEC_P_C_RIGHT_END
                   || id == HITEC_P_C_FS_POSITION) {
            snprintf(shown, sizeof(shown), "%lu", raw);
        } else {
            snprintf(shown, sizeof(shown), "%lu  0x%04lX", raw,
                     raw & 0xFFFFul);
        }
        beside = false;
        break;
    }
    if (beside) {
        snprintf(out, n, "%s (%lu)", shown, raw);
    } else {
        snprintf(out, n, "%s", shown);
    }
}

/**
 * @brief The classic model's name: Hitec's, the name cells as read, or
 *        "--".
 * @param out the text
 * @param n   bytes at @p out
 */
static void name_value(char *out, size_t n)
{
    const hitec_model_t *m = hitec_prog_model(&hp.prog);
    const char *name = m != NULL ? hitec_model_name(m->model) : "";

    if (m == NULL) {
        snprintf(out, n, "--");
    } else if (name[0] != '\0') {
        snprintf(out, n, "%s", name);
    } else {
        snprintf(out, n, "%s", m->name[0] != '\0' ? m->name : "?");
    }
}

/**
 * @brief The text on an action row's button.
 * @param r   the row
 * @param out the text
 * @param n   bytes at @p out
 */
static void act_value(const hpn_row_t *r, char *out, size_t n)
{
    const hitec_prog_t *p = &hp.prog;
    const hitec_prog_state_t st = hitec_prog_state(p);

    switch (r->what) {
    case A_PORT:
        snprintf(out, n, "%s",
                 st == HITEC_PROG_OTHER ? TR(HP_RELEASE)
                 : st == HITEC_PROG_HELD ? TR(KP_LEAVE)
                 : st == HITEC_PROG_OPENING || st == HITEC_PROG_ENTERING
                       || st == HITEC_PROG_READING ? TR(KP_ABORT)
                 : st == HITEC_PROG_CLEARING || st == HITEC_PROG_LEAVING
                       || st == HITEC_PROG_RELEASING ? TR(KP_WAIT)
                                                     : TR(KP_CONNECT));
        break;
    case A_SERIES:
        snprintf(out, n, "%s", series_name(hitec_prog_series(p)));
        break;
    case A_READ:
        snprintf(out, n, "%s", TR(KP_READ_ALL));
        break;
    case A_MORE:
        snprintf(out, n, "%s", TR(KP_CHOOSE));
        break;
    default:
        snprintf(out, n, "%s", TR(HP_CLEAR));
        break;
    }
}

void hpn_value_text(const hpn_row_t *r, char *out, size_t n, bool *warn)
{
    const hitec_image_t *img = hitec_prog_servo(&hp.prog);
    const uint8_t layout = hitec_prog_layout(&hp.prog);
    const hitec_param_info_t *info;
    hitec_value_t v;
    hitec_get_t got;

    *warn = false;
    if (r->kind == K_ACT) {
        act_value(r, out, n);
        return;
    }
    if (r->what == W_NAME) {
        name_value(out, n);
        return;
    }
    info = hitec_param(layout, r->what);
    got = img != NULL && info != NULL
              ? hitec_param_get(img, layout, r->what, &v)
              : HITEC_GET_UNKNOWN;
    if (got == HITEC_GET_RAW) {
        /* Fits no encoding: the stored value, as it is. */
        *warn = true;
        snprintf(out, n, "RAW %lu", (unsigned long)v.raw);
    } else if (got == HITEC_GET_OK) {
        fmt_value(r->what, info->unit, &v, out, n);
    } else {
        /* Not read, or a parameter the layout lacks. */
        snprintf(out, n, "--");
    }
}

/* -------------------------------------------------------------- PORT row */

bool hpn_port_unbound(void)
{
    const hitec_prog_t *p = &hp.prog;

    return hitec_prog_state(p) == HITEC_PROG_PWM
           && (!hitec_prog_on_port(p) || hitec_prog_pin(p) == HITEC_PROG_NO_PIN);
}

/**
 * @brief The supply a CONNECT asks, or the session's, into @p out.
 * @param out the text
 * @param n   bytes at @p out
 */
static void supply_text(char *out, size_t n)
{
    const hitec_prog_t *p = &hp.prog;
    const bool held = p->owned;
    const unsigned mv = held ? p->mv_open : hitec_prog_mv(p);
    const unsigned ma = held ? p->ma_open : hitec_prog_ma(p);

    if (hitec_prog_series(p) == (uint8_t)HITEC_D && !hitec_prog_rail(p)) {
        snprintf(out, n, "%s", TR(HP_SUPPLY_AS_IS));
    } else {
        snprintf(out, n, "%u.%uV %umA", mv / 1000u, mv % 1000u / 100u, ma);
    }
}

bool hpn_port_line(int i, char *out, size_t n)
{
    const hitec_prog_t *p = &hp.prog;
    const hitec_prog_state_t st = hitec_prog_state(p);
    const unsigned port = (unsigned)hitec_prog_port(p) + 1u;
    const uint8_t pin = hitec_prog_pin(p);
    char at[40];

    if (i < 0 || i > 1) {
        return false;
    }
    if (st != HITEC_PROG_OTHER && !p->owned && !hitec_prog_on_port(p)) {
        snprintf(out, n, "%s", i == 0 ? TR(HP_PORT_NONE) : "");
        return true;
    }
    if (pin == HITEC_PROG_NO_PIN) {
        snprintf(at, sizeof(at), TR(SV_TARGET_PN), port);
    } else {
        snprintf(at, sizeof(at), TR(SV_TARGET_PN_PIN), port, (unsigned)pin);
    }
    if (i == 0) {
        snprintf(out, n, "%s  %s", at,
                 st == HITEC_PROG_OTHER ? TR(HP_HELD)
                                        : series_name(hitec_prog_series(p)));
        return true;
    }
    if (st == HITEC_PROG_OTHER) {
        snprintf(out, n, "%s", TR(HP_OTHER_LINE));
        return true;
    }
    {
        char supply[40];

        supply_text(supply, sizeof(supply));
        if (hitec_prog_series(p) == (uint8_t)HITEC_D) {
            snprintf(out, n, "%s  %s",
                     hitec_prog_precharge(p) ? "RX PRECHARGE" : "RX PULL-UP",
                     supply);
        } else {
            snprintf(out, n, "%s", supply);
        }
    }
    return true;
}

/* ------------------------------------------------------------ state line */

/**
 * @brief The state line while the tester has the port.
 * @param out the text
 * @param n   bytes at @p out
 */
static void pwm_line(char *out, size_t n)
{
    const hitec_prog_t *p = &hp.prog;
    const unsigned port = (unsigned)hitec_prog_port(p) + 1u;
    const unsigned mv = hitec_prog_mv(p);

    if (!hitec_prog_on_port(p)) {
        snprintf(out, n, "%s", TR(KP_NEED_TARGET));
    } else if (hitec_prog_series(p) == (uint8_t)HITEC_CLASSIC
               || hitec_prog_rail(p)) {
        snprintf(out, n, TR(KP_STATE_PWM), port, mv / 1000u,
                 mv % 1000u / 100u, (unsigned)hitec_prog_ma(p));
    } else {
        snprintf(out, n, TR(KP_STATE_PWM_AS_IS), port);
    }
}

/**
 * @brief The state line while the port is this panel's and nothing runs.
 * @param out the text
 * @param n   bytes at @p out
 */
static void held_line(char *out, size_t n)
{
    const hitec_prog_t *p = &hp.prog;
    const unsigned port = (unsigned)hitec_prog_port(p) + 1u;

    if (hitec_prog_stopped(p)) {
        snprintf(out, n, "%s", p->f.stopped ? TR(HP_ST_STOP_LATCHED)
                                            : TR(HP_ST_STOPPED));
    } else if (!hitec_prog_in_mode(p)) {
        snprintf(out, n, TR(HP_ST_NOT_IN_MODE), port);
    } else if (p->f.supply_read) {
        snprintf(out, n, TR(KP_STATE_PROG_V), port,
                 (unsigned)(p->f.out_mv / 1000u),
                 (unsigned)(p->f.out_mv % 1000u / 10u),
                 (unsigned)p->f.out_ma);
    } else {
        snprintf(out, n, TR(KP_STATE_PROGRAMMING), port);
    }
}

void hpn_state_line(char *out, size_t n)
{
    const hitec_prog_t *p = &hp.prog;
    const bool classic = hitec_prog_series(p) == (uint8_t)HITEC_CLASSIC;
    unsigned done = 0;
    unsigned total = 0;

    switch (hitec_prog_state(p)) {
    case HITEC_PROG_DOWN:
        snprintf(out, n, "%s", TR(KP_ST_DOWN));
        break;
    case HITEC_PROG_NO_PAGE:
        snprintf(out, n, "%s", TR(HP_NO_PAGE));
        break;
    case HITEC_PROG_SYNC:
        snprintf(out, n, "%s", TR(KP_STATE_SYNC));
        break;
    case HITEC_PROG_PWM:
        pwm_line(out, n);
        break;
    case HITEC_PROG_OTHER:
        snprintf(out, n, "%s", TR(HP_HELD_OTHER));
        break;
    case HITEC_PROG_OPENING:
        snprintf(out, n, TR(KP_STATE_ENTERING),
                 (unsigned)hitec_prog_port(p) + 1u);
        break;
    case HITEC_PROG_ENTERING:
        snprintf(out, n, "%s",
                 classic ? TR(HP_ST_ENTRY_C)
                 : hitec_prog_rail(p) ? TR(HP_ST_ENTRY_D) : TR(HP_ST_MODEL));
        break;
    case HITEC_PROG_HELD:
        held_line(out, n);
        break;
    case HITEC_PROG_READING:
        if (hitec_prog_retry_code(p) != 0u) {
            snprintf(out, n, TR(HP_BAD_ANSWER), hitec_prog_retry_code(p));
        } else {
            (void)hitec_prog_progress(p, &done, &total);
            snprintf(out, n, TR(HP_ST_READ), done < total ? done + 1u : done,
                     total);
        }
        break;
    case HITEC_PROG_CLEARING:
        snprintf(out, n, "%s", TR(HP_ST_CLEARING));
        break;
    case HITEC_PROG_LEAVING:
        snprintf(out, n, "%s", classic ? TR(HP_ST_LEAVE_C)
                                       : TR(HP_ST_LEAVE_D));
        break;
    default:
        snprintf(out, n, "%s", TR(HP_ST_RELEASING));
        break;
    }
}

/* -------------------------------------------------------------- messages */

/** @brief A message's first line and, where it has one, its second. */
static const int16_t k_lines[HITEC_PROG_MSG_COUNT][2] = {
    [HITEC_PROG_MSG_NONE]          = { -1, -1 },
    [HITEC_PROG_MSG_READ]          = { -1, -1 },
    [HITEC_PROG_MSG_DOWN]          = { TX_KP_ST_DOWN, -1 },
    [HITEC_PROG_MSG_NO_PAGE]       = { TX_HP_NO_PAGE, TX_HP_NO_PAGE_2 },
    [HITEC_PROG_MSG_BUSY]          = { TX_KP_ST_BUSY, -1 },
    [HITEC_PROG_MSG_NEED_DISARM]   = { TX_KP_M_NEED_DISARM, -1 },
    [HITEC_PROG_MSG_STOP_LATCHED]  = { TX_HP_STOP_LATCHED,
                                       TX_HP_STOP_LATCHED_2 },
    [HITEC_PROG_MSG_NEED_TARGET]   = { TX_KP_NEED_TARGET, TX_KP_NEED_TARGET_2 },
    [HITEC_PROG_MSG_NO_PIN]        = { TX_KP_R_NO_PIN, -1 },
    [HITEC_PROG_MSG_RX_PENDING]    = { TX_HP_RX_PENDING, -1 },
    [HITEC_PROG_MSG_NEED_PD_C]     = { TX_HP_NEED_PD_C, -1 },
    [HITEC_PROG_MSG_SUPPLY_CAPS]   = { TX_KP_M_SUPPLY_CAPS,
                                       TX_KP_M_SUPPLY_CAPS_2 },
    [HITEC_PROG_MSG_HELD_KST]      = { TX_HP_HELD_KST, -1 },
    [HITEC_PROG_MSG_HELD_OTHER]    = { TX_HP_HELD_OTHER, TX_HP_RELEASE_HINT },
    [HITEC_PROG_MSG_NO_C]          = { TX_HP_NO_C, -1 },
    [HITEC_PROG_MSG_NO_D]          = { TX_HP_NO_D, -1 },
    [HITEC_PROG_MSG_ENTRY_BAD]     = { TX_HP_ENTRY_BAD, -1 },
    [HITEC_PROG_MSG_READ_FAIL]     = { -1, -1 },
    [HITEC_PROG_MSG_NOT_IN_MODE]   = { TX_HP_NOT_IN_MODE, -1 },
    [HITEC_PROG_MSG_STOPPED]       = { TX_HP_ST_STOPPED, -1 },
    [HITEC_PROG_MSG_ABORTED]       = { TX_HP_ABORTED, -1 },
    [HITEC_PROG_MSG_CLEARED]       = { TX_HP_CLEARED, -1 },
    [HITEC_PROG_MSG_UNSAFE]        = { TX_HP_UNSAFE, TX_HP_UNSAFE_2 },
    [HITEC_PROG_MSG_LEFT]          = { TX_KP_M_LEFT, -1 },
    [HITEC_PROG_MSG_RELEASED]      = { -1, -1 },
    [HITEC_PROG_MSG_SESSION_ENDED] = { TX_HP_SESSION_ENDED, -1 },
    [HITEC_PROG_MSG_TIMEOUT]       = { TX_HP_TIMEOUT, -1 },
    [HITEC_PROG_MSG_NO_ANSWER]     = { TX_HP_NO_ANSWER, -1 },
    [HITEC_PROG_MSG_RESTARTED]     = { TX_HP_RESTARTED, -1 },
    [HITEC_PROG_MSG_NACK]          = { -1, -1 },
    [HITEC_PROG_MSG_REFUSED]       = { -1, -1 },
    [HITEC_PROG_MSG_NO_LINE]       = { TX_HP_NO_LINE, -1 },
    [HITEC_PROG_MSG_SUPPLY]        = { TX_HP_SUPPLY, -1 },
    [HITEC_PROG_MSG_UNKNOWN]       = { -1, -1 },
};

/**
 * @brief The lines of a message whose text takes its arguments.
 * @param m   the message
 * @param out the lines
 * @return the lines used
 */
static int formatted(const hitec_prog_msg_t *m,
                     char out[HITEC_PANEL_MSG_LINES][HITEC_PANEL_LINE])
{
    switch (m->id) {
    case HITEC_PROG_MSG_READ:
        if (hitec_prog_series(&hp.prog) == (uint8_t)HITEC_D) {
            snprintf(out[0], HITEC_PANEL_LINE, TR(HP_READ_D),
                     (unsigned)m->arg, (unsigned)m->reg);
        } else {
            char name[24];

            name_value(name, sizeof(name));
            snprintf(out[0], HITEC_PANEL_LINE, TR(HP_READ_C), name,
                     (unsigned)(m->reg & 0xFFu));
        }
        return 1;
    case HITEC_PROG_MSG_READ_FAIL:
        snprintf(out[0], HITEC_PANEL_LINE, TR(HP_READ_FAIL),
                 (unsigned)(m->arg & 0xFFu));
        return 1;
    case HITEC_PROG_MSG_RELEASED:
        snprintf(out[0], HITEC_PANEL_LINE, TR(HP_RELEASED),
                 (unsigned)m->arg + 1u);
        snprintf(out[1], HITEC_PANEL_LINE, "%s", TR(HP_RELEASED_2));
        return 2;
    case HITEC_PROG_MSG_NACK:
        snprintf(out[0], HITEC_PANEL_LINE, TR(KP_O_NACK), (unsigned)m->arg);
        return 1;
    case HITEC_PROG_MSG_REFUSED:
        snprintf(out[0], HITEC_PANEL_LINE, TR(HP_REFUSED), (unsigned)m->arg);
        return 1;
    default:
        snprintf(out[0], HITEC_PANEL_LINE, TR(KP_M_UNKNOWN_CODE),
                 (unsigned)m->arg);
        return 1;
    }
}

int hitec_panel_message_lines(const hitec_prog_msg_t *m,
                              char out[HITEC_PANEL_MSG_LINES]
                                      [HITEC_PANEL_LINE])
{
    int n = 0;

    if (m == NULL || m->id == HITEC_PROG_MSG_NONE
        || m->id >= HITEC_PROG_MSG_COUNT) {
        return 0;
    }
    if (k_lines[m->id][0] < 0) {
        return formatted(m, out);
    }
    for (int i = 0; i < 2; ++i) {
        if (k_lines[m->id][i] >= 0) {
            snprintf(out[n++], HITEC_PANEL_LINE, "%s",
                     ui_tr((ui_text_id_t)k_lines[m->id][i]));
        }
    }
    return n;
}
