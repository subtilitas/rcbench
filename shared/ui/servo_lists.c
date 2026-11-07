/**
 * @file
 * @brief The SERVO screens' SETTINGS lists (ui_setlist.h), opened by the
 * footer's SETTINGS.
 *
 * The hub's list is the servo profile every function reads: TYPE, FRAME
 * RATE, PULSE MIN, PULSE CENTRE, PULSE MAX, TRIM, TRAVEL, REVERSE and HV
 * SERVO under OUTPUT, then the device under test's NAME and REPORT.  The
 * profile is for the session: STANDARD PWM at 50Hz at every restart, and a
 * type or a frame rate that can destroy a servo not made for it goes into
 * force only after its warning is held for 2s, as before.  Those rows are
 * custom rows over servo_settings_panel.c's editors: a tap on a pulse,
 * TRIM or TRAVEL opens the keypad over the left column, its keys step it.
 *
 * A function's list holds how that screen runs its function: SWEEP's
 * CURVE, SPEED and RANGE; RUDDER's LENGTH, ENC CENTRE (taken from the
 * AS5600's reading) and ANGLE SOURCE (read-only, a tap opens SETUP >
 * SENSORS on it); RUDDER AUTO's targets, MARGIN, TOLERANCE, SERVO, SERVO
 * PULSES and ANGLE SOURCE; HITEC's SERIES and RECEIVE.  TEST and LIMIT
 * RUN open their run's page of rows, KST its EXPERT MODE page, which keep
 * their 2s holds; LIVE has no list.
 *
 * A function's list opens on a SAFE bench only, with no sweep and no run;
 * a bench that goes live under it closes it.  The hub's opens at any time:
 * the hub drives nothing.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "hitec_panel.h"
#include "settings.h"
#include "ui_setlist.h"
#include "ui_text.h"

/* Last: the layout macros of the headers it includes have short names
 * (W, H, PAD), which no header included after it may meet. */
#include "servo_screen_core_int.h"

/* -------------------------------------------------- the profile's rows */

/**
 * @brief OUTPUT's heading line: whether the frame rate reached the pins,
 *        the note's first line, or its second where that is the warning
 *        (TARGET not active, PORT PWM off, PROG 5V off).
 * @param out takes the line
 * @param n   its size
 */
static void rate_line(char *out, size_t n)
{
    char l1[96], l2[96];
    const bool warn = svs_rate_note(l1, l2, sizeof(l1));
    const bool second = warn
                        && (strcmp(l2, TR(SV_IN_FORCE_NOT_SET)) == 0
                            || strcmp(l2, TR(SV_IN_FORCE_PWM_OFF)) == 0
                            || strcmp(l2, TR(PC_NEED_5V)) == 0);
    snprintf(out, n, "%s", second ? l2 : l1);
}

/** @brief The line's warning, as a heading reads it.
 *  @return non-NULL while the note takes the warning colour */
static const char *rate_warn(void)
{
    char l1[96], l2[96];
    return svs_rate_note(l1, l2, sizeof(l1)) ? "" : NULL;
}

/** @brief A profile row's value, by its kind. */
#define VALUE_OF(fn_, kind_)                                                 \
    static void fn_(char *out, size_t n) { svs_profile_value(kind_, out, n); }
/** @brief One step of a profile row. */
#define STEP_OF(fn_, kind_)                                                  \
    static void fn_(int dir) { svs_profile_step(kind_, dir); }
/** @brief A tap on a profile row: its editor over the left column. */
#define TAP_OF(fn_, kind_)                                                   \
    static void fn_(void) { svs_profile_edit(kind_); }

VALUE_OF(type_value, R_TYPE)
VALUE_OF(rate_value, R_RATE)
VALUE_OF(min_value, R_MIN)
VALUE_OF(centre_value, R_CENTRE)
VALUE_OF(max_value, R_MAX)
VALUE_OF(trim_value, R_TRIM)
VALUE_OF(travel_value, R_TRAVEL)
VALUE_OF(reverse_value, R_REVERSE)
VALUE_OF(hv_value, R_HV)
STEP_OF(min_step, R_MIN)
STEP_OF(centre_step, R_CENTRE)
STEP_OF(max_step, R_MAX)
STEP_OF(trim_step, R_TRIM)
STEP_OF(travel_step, R_TRAVEL)
STEP_OF(reverse_step, R_REVERSE)
STEP_OF(hv_step, R_HV)
TAP_OF(type_tap, R_TYPE)
TAP_OF(rate_tap, R_RATE)
TAP_OF(min_tap, R_MIN)
TAP_OF(centre_tap, R_CENTRE)
TAP_OF(max_tap, R_MAX)
TAP_OF(trim_tap, R_TRIM)
TAP_OF(travel_tap, R_TRAVEL)

/** @brief The OUTPUT heading: the rate line, warning while a fast rate is unconfirmed. */
static const ui_setlist_custom_t k_out_line = {
    .label = TX_SV_PG_OUTPUT, .help = TX_COUNT, .value_text = rate_line,
    .locked = rate_warn,
};
/** @brief The TYPE row. */
static const ui_setlist_custom_t k_type = {
    .label = TX_SV_ROW_TYPE, .help = TX_SVL_TYPE_HELP,
    .value_text = type_value, .tap = type_tap,
};
/** @brief The RATE row. */
static const ui_setlist_custom_t k_rate = {
    .label = TX_SV_ROW_RATE, .help = TX_SVL_RATE_HELP,
    .value_text = rate_value, .tap = rate_tap,
};
/** @brief The PULSE MIN row. */
static const ui_setlist_custom_t k_min = {
    .label = TX_SV_ROW_MIN, .help = TX_SVL_PULSE_HELP,
    .value_text = min_value, .step = min_step, .tap = min_tap,
};
/** @brief The PULSE CENTRE row. */
static const ui_setlist_custom_t k_centre = {
    .label = TX_SV_ROW_CENTRE, .help = TX_SVL_PULSE_HELP,
    .value_text = centre_value, .step = centre_step, .tap = centre_tap,
};
/** @brief The PULSE MAX row. */
static const ui_setlist_custom_t k_max = {
    .label = TX_SV_ROW_MAX, .help = TX_SVL_PULSE_HELP,
    .value_text = max_value, .step = max_step, .tap = max_tap,
};
/** @brief The TRIM row. */
static const ui_setlist_custom_t k_trim = {
    .label = TX_SV_ROW_TRIM, .help = TX_SVL_TRIM_HELP,
    .value_text = trim_value, .step = trim_step, .tap = trim_tap,
};
/** @brief The TRAVEL row. */
static const ui_setlist_custom_t k_travel = {
    .label = TX_SV_ROW_TRAVEL, .help = TX_SVL_TRAVEL_HELP,
    .value_text = travel_value, .step = travel_step, .tap = travel_tap,
};
/** @brief The REVERSE switch. */
static const ui_setlist_custom_t k_reverse = {
    .label = TX_SV_ROW_REVERSE, .help = TX_SVL_REVERSE_HELP, .cycles = true,
    .value_text = reverse_value, .step = reverse_step,
};
/** @brief The HV SERVO switch. */
static const ui_setlist_custom_t k_hv = {
    .label = TX_SV_ROW_HV, .help = TX_SVL_HV_HELP, .cycles = true,
    .value_text = hv_value, .step = hv_step,
};

/** @brief The hub's rows: the profile, then the device under test. */
static const ui_setlist_row_t k_hub_rows[] = {
    UI_SL_ROW_HEADING(TX_SV_PG_OUTPUT, &k_out_line),
    UI_SL_ROW_CUSTOM(&k_type),
    UI_SL_ROW_CUSTOM(&k_rate),
    UI_SL_ROW_CUSTOM(&k_min),
    UI_SL_ROW_CUSTOM(&k_centre),
    UI_SL_ROW_CUSTOM(&k_max),
    UI_SL_ROW_CUSTOM(&k_trim),
    UI_SL_ROW_CUSTOM(&k_travel),
    UI_SL_ROW_CUSTOM(&k_reverse),
    UI_SL_ROW_CUSTOM(&k_hv),
    UI_SL_ROW_HEADING(TX_SV_PG_DUT, NULL),
    { (uint8_t)UI_SL_TEXT, (int8_t)SET_TEXT_DUT_NAME, (setting_id_t)0,
      TX_COUNT, NULL },
    UI_SL_ROW_SETTING(SET_SERVO_REPORT),
};

/* ------------------------------------------- the functions' own rows */

/** @brief ENC CENTRE's value: the count the AS5600 reads at neutral. */
static void enc_value(char *out, size_t n)
{
    snprintf(out, n, "%d", settings_get_int(SET_ENC_CENTRE));
}

/** @brief A tap on ENC CENTRE: the live count is the centre.  An IMU's
 *         zero is taken on the IMU screen, not here. */
static void enc_tap(void)
{
    if (enc_shown() && enc_src() == SERVO_ANGLE_AS5600) {
        settings_set(SET_ENC_CENTRE, (float)s.enc_raw);
        ++s.sl_rev;
    }
}

/** @brief Why ENC CENTRE takes no tap: no AS5600 reading on show.
 *  @return the reason, NULL while it takes one */
static const char *enc_locked(void)
{
    return (enc_shown() && enc_src() == SERVO_ANGLE_AS5600)
               ? NULL : TR(SVL_ENC_NO_READING);
}

/** @brief ANGLE SOURCE's value: the source in force, read-only. */
static void src_value(char *out, size_t n)
{
    char name[40];
    snprintf(out, n, "%s", enc_name(name, sizeof(name)));
}

/** @brief A tap on ANGLE SOURCE: SETUP > SENSORS on it. */
static void src_tap(void)
{
    svs_list_close();
    wb_open_encoder();
}

/** @brief SERVO's value: KST/CHASERVO or HITEC, as LIMIT RUN's row. */
static void fam_value(char *out, size_t n)
{
    snprintf(out, n, "%s",
             s.lim_hitec ? TR(SV_LIM_SERVO_HITEC) : TR(SV_LIM_SERVO_KST));
}

/** @brief SERVO stepped: the other family, for a run started after it. */
static void fam_step(int dir)
{
    (void)dir;
    if (!servo_limrun_running(&s.lim) && !servo_rauto_running(&s.ra.run)) {
        s.lim_hitec = !s.lim_hitec;
        ++s.ctrl_rev;
    }
}

/** @brief Why SERVO and SERVO PULSES take no press: the target is no
 *         programming port.  @return the reason, NULL while they take one */
static const char *fam_locked(void)
{
    return lim_reads() ? NULL : TR(SVL_PORT_ONLY);
}

/** @brief SERVO PULSES' value, as LIMIT RUN's row. */
static void pulses_value(char *out, size_t n)
{
    snprintf(out, n, "%s",
             s.lim_pulses ? TR(SV_LIM_PULSES_STD) : TR(SV_LIM_PULSES_KEEP));
}

/** @brief SERVO PULSES stepped, for a run started after it. */
static void pulses_step(int dir)
{
    (void)dir;
    if (lim_reads() && !lim_hitec_now() && lim_write_why() == NULL) {
        s.lim_pulses = !s.lim_pulses;
        ++s.ctrl_rev;
    }
}

/** @brief Why SERVO PULSES takes no press.  @return the reason or NULL */
static const char *pulses_locked(void)
{
    if (!lim_reads()) {
        return TR(SVL_PORT_ONLY);
    }
    if (lim_hitec_now()) {
        return TR(SVL_KST_ONLY);
    }
    return lim_write_why();
}

/** @brief RUDDER's ENC CENTRE row: a tap takes the live count. */
static const ui_setlist_custom_t k_enc = {
    .label = TX_SV_ROW_ENC_CENTRE, .help = TX_SVL_ENC_HELP,
    .value_text = enc_value, .tap = enc_tap, .locked = enc_locked,
};
/** @brief The ANGLE SOURCE row. */
static const ui_setlist_custom_t k_src = {
    .label = TX_SVL_ANGLE_SOURCE, .help = TX_SVL_ANGLE_HELP,
    .value_text = src_value, .tap = src_tap,
};
/** @brief RUDDER AUTO's SERVO row: KST/CHASERVO or HITEC. */
static const ui_setlist_custom_t k_fam = {
    .label = TX_SV_ROW_LIM_SERVO, .help = TX_SVL_FAMILY_HELP, .cycles = true,
    .value_text = fam_value, .step = fam_step, .locked = fam_locked,
};
/** @brief RUDDER AUTO's SERVO PULSES row. */
static const ui_setlist_custom_t k_pulses = {
    .label = TX_SV_ROW_LIM_PULSES, .help = TX_SVL_PULSES_HELP,
    .cycles = true, .value_text = pulses_value, .step = pulses_step,
    .locked = pulses_locked,
};

/** @brief SWEEP's list: CURVE, SPEED and RANGE. */
static const ui_setlist_row_t k_sweep_rows[] = {
    UI_SL_ROW_SETTING(SET_SERVO_CURVE),
    UI_SL_ROW_SETTING(SET_SERVO_TEST_HZ),
    UI_SL_ROW_SETTING(SET_SERVO_TEST_RANGE),
};
/** @brief RUDDER's list: LENGTH, ENC CENTRE and ANGLE SOURCE. */
static const ui_setlist_row_t k_rudder_rows[] = {
    UI_SL_ROW_SETTING(SET_RUDDER_LEN_MM),
    UI_SL_ROW_CUSTOM(&k_enc),
    UI_SL_ROW_CUSTOM(&k_src),
};
/** @brief RUDDER AUTO's list: the targets, MARGIN, TOLERANCE, the servo, its pulses and ANGLE SOURCE. */
static const ui_setlist_row_t k_rauto_rows[] = {
    UI_SL_ROW_SETTING(SET_RUDDER_DEFL_LO),
    UI_SL_ROW_SETTING(SET_RUDDER_DEFL_HI),
    UI_SL_ROW_SETTING(SET_RUDDER_MARGIN),
    UI_SL_ROW_SETTING(SET_RUDDER_TOL),
    UI_SL_ROW_CUSTOM(&k_fam),
    UI_SL_ROW_CUSTOM(&k_pulses),
    UI_SL_ROW_CUSTOM(&k_src),
};
/** @brief HITEC's list: SERIES and D receive. */
static const ui_setlist_row_t k_hitec_rows[] = {
    UI_SL_ROW_SETTING(SET_HITEC_SERIES),
    UI_SL_ROW_SETTING(SET_HITEC_RX),
};

/** @brief A function's list: its rows and their count. */
typedef struct {
    const ui_setlist_row_t *rows; /**< the rows */
    int n;                        /**< their count */
} fn_rows_t;

/** @brief A fn_rows_t of table @p a. */
#define ROWS(a) { (a), (int)(sizeof(a) / sizeof((a)[0])) }

/** @brief Each function's rows; none for LIVE and the pages' functions. */
static const fn_rows_t k_rows_of[SERVO_FN_COUNT] = {
    [SERVO_FN_HUB]    = ROWS(k_hub_rows),
    [SERVO_FN_SWEEP]  = ROWS(k_sweep_rows),
    [SERVO_FN_RUDDER] = ROWS(k_rudder_rows),
    [SERVO_FN_RAUTO]  = ROWS(k_rauto_rows),
    [SERVO_FN_HITEC]  = ROWS(k_hitec_rows),
};

/* ------------------------------------------------------------ the host */

/** @brief The function the list is open for, its rows. */
static const fn_rows_t *rows_now(void)
{
    return &k_rows_of[(s.fn < SERVO_FN_COUNT) ? s.fn : SERVO_FN_HUB];
}

/** @brief The open list's row count. @return the count */
static int list_count(void) { return rows_now()->n; }

/** @brief The open list's row @p i. @param i the row @return the row */
static ui_setlist_row_t list_row(int i)
{
    return ui_setlist_table_row(rows_now()->rows, rows_now()->n, i);
}

/** @brief The strip's name: the function's. */
static const char *list_name(void) { return svs_fn_name((servo_fn_t)s.fn); }

/** @brief The line under the name: the port the screen drives. */
static bool list_line(char *out, size_t n, bool *warn)
{
    *warn = svs_port_line(out, n);
    return true;
}

/**
 * @brief A row changed by a step, the keypad, RESET or DISCARD: HITEC's
 *        SERIES reaches the programmer before its next CONNECT.
 * @param id The setting that changed; SETTING_COUNT for several.
 */
static void list_changed(setting_id_t id)
{
    if (id == SET_HITEC_SERIES || (int)id == SETTING_COUNT) {
        hitec_panel_settings_changed();
    }
}

/**
 * @brief RESET: the list's own settings to their defaults; on the hub
 *        also the profile to STANDARD PWM at 50Hz, TRIM 0, TRAVEL 90°,
 *        REVERSE and HV SERVO off, as at a restart.
 */
static void list_reset(void)
{
    const fn_rows_t *r = rows_now();
    setting_id_t ids[16] = { 0 };
    int k = 0;
    for (int i = 0; i < r->n && k < 16; ++i) {
        if (r->rows[i].kind == UI_SL_SETTING) {
            ids[k++] = r->rows[i].id;
        }
    }
    settings_reset_rows(ids, k);
    if (s.fn == SERVO_FN_HUB) {
        s.trim_us = 0;
        s.travel_deg = 90.0f;
        s.reverse = false;
        s.test_hv = false;
        s.type = -1;            /* the pulses go back with the type */
        apply_profile(0, k_types[0].default_hz);
    }
    list_changed((setting_id_t)SETTING_COUNT);
    ++s.sl_rev;
}

/** @brief A change the list shows: its count moves. */
static void list_invalidate(void)
{
    ++s.sl_rev;
}


/** @brief The leave warning's SAVE and DISCARD: the list closes. */
static void list_way_out(void) { svs_list_close(); }

/** @brief The SERVO lists' host (ui_setlist.h). */
static const ui_setlist_host_t k_host = {
    .count      = list_count,
    .row        = list_row,
    .headings   = true,
    .name       = list_name,
    .name_line  = list_line,
    .reset      = list_reset,
    .changed    = list_changed,
    .invalidate = list_invalidate,
    .way_out    = list_way_out,
};

void svs_lists_init(void)
{
    ui_setlist_init(&s.sl, &k_host);
}

bool svs_list_offered(void)
{
    switch ((servo_fn_t)s.fn) {
    case SERVO_FN_TEST:
    case SERVO_FN_LIMRUN:
    case SERVO_FN_KST:
        return true;
    default:
        return k_rows_of[(s.fn < SERVO_FN_COUNT) ? s.fn : 0].n > 0;
    }
}

const char *svs_list_refused(void)
{
    /* A hold under way keeps its finger: the list would take every event,
     * the hold's release among them, and the hold would fire without the
     * finger on it. */
    if (s.out_down || s.p5_down || s.arm_down || s.test_down || s.lim_down) {
        return TR(FOOT_SETTINGS_SAFE);
    }
    if (s.fn == SERVO_FN_HUB) {
        return NULL;
    }
    if (s.gate.armed || s.gate.arm_asked || s.sweeping || svs_start_pending()
        || servo_test_running(&s.test) || servo_limrun_running(&s.lim)
        || servo_rauto_running(&s.ra.run)) {
        return TR(FOOT_SETTINGS_SAFE);
    }
    return NULL;
}

void svs_list_open(void)
{
    if (!svs_list_offered() || svs_list_refused() != NULL) {
        return;
    }
    close_panels();
    switch ((servo_fn_t)s.fn) {
    case SERVO_FN_TEST:
        s.run_list = RL_AUTO;
        break;
    case SERVO_FN_LIMRUN:
        s.run_list = RL_LIMIT;
        break;
    case SERVO_FN_KST:
        s.run_list = RL_KST;
        break;
    default:
        ui_setlist_init(&s.sl, &k_host);
        ui_setlist_open(&s.sl);
        break;
    }
    ++s.sl_rev;
    ++s.ctrl_rev;
    servo_invalidate();
}

void svs_list_close(void)
{
    if (s.sl.open || ui_setlist_covered(&s.sl)) {
        ui_setlist_close(&s.sl);
        ++s.sl_rev;
        servo_invalidate();
    }
}

bool svs_list_shown(void) { return s.sl.open; }

bool svs_list_back(void)
{
    if (!s.sl.open) {
        return false;
    }
    if (ui_setlist_back(&s.sl) || ui_setlist_warn(&s.sl)) {
        ++s.sl_rev;
        return true;
    }
    svs_list_close();
    return true;
}

void svs_list_tick(float dt_s)
{
    if (!s.sl.open) {
        return;
    }
    /* A function screen's list closes as the bench goes live under it:
     * the screen's output shows again. */
    if (svs_list_refused() != NULL) {
        svs_list_close();
        return;
    }
    ui_setlist_tick(&s.sl, dt_s);
}

/* ------------------------------------------------------------ for tests */

const ui_setlist_t *servo_screen_list(void) { return &s.sl; }
