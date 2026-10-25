/**
 * @file kst_panel_run.c
 * @brief The limit run's sessions on the programmer.
 *
 * The limit run's sessions on the programmer for KST and Chaservo servos:
 * the CONNECT, the write and the LEAVE the limit run (servo_limrun.h)
 * asks of the overlay's model, and the poll that reports each step's end.
 * The state and the seam are kst_panel_int.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_panel.h"

#include <math.h>

#include "kst_card.h"
#include "kst_port.h"
#include "kst_reg.h"
#include "supply_screen.h"

/* Last: its layout macros have short names (ROW0, PITCH, ROW_H) and k
 * names the state, which no header included after it may meet. */
#include "kst_panel_int.h"

enum { RUN_OP_NONE = 0, RUN_OP_CONNECT, RUN_OP_WRITE, RUN_OP_LEAVE };

/* --- the limit run's sessions ---------------------------------------------- */

/**
 * @brief Whatever the overlay has open goes: the run's step is no operator's.
 */
static void run_close_editors(void)
{
    kpn_close_editors();
    ++k.rev;
}

kst_prog_msg_id_t kst_panel_run_connect(bool supply, uint16_t mv,
                                        uint16_t ma)
{
    kst_prog_t *p = &k.prog;
    kst_prog_msg_id_t m;

    if (kst_prog_state(p) != KST_PROG_PWM) {
        return (kst_prog_msg_id_t)(KST_PROG_MSG_START + KST_LINK_START_BUSY);
    }
    run_close_editors();
    if (!k.run.kept) {
        k.run.kept = true;
        k.run.keep_mv = kst_prog_supply_mv(p);
        k.run.keep_ma = kst_prog_supply_ma(p);
    }
    if (supply) {
        /* The PD mini's set point as it stands: the ENTER changes
         * nothing of it. */
        kst_prog_supply_set(p, mv, ma);
    }
    {
        const supply_caps_t caps = supply_screen_caps();

        kst_prog_supply_caps(p, (uint16_t)lroundf(caps.v_max * 1000.0f),
                             (uint16_t)lroundf(caps.i_max * 1000.0f));
    }
    kst_prog_supply_floor(p, (uint16_t)KST_PORT_SUPPLY_MIN_MV);
    k.run.said = kst_prog_message(p)->seq;
    m = kst_prog_connect(p, kpn_port_no());
    k.run.op = m == KST_PROG_MSG_NONE ? RUN_OP_CONNECT : RUN_OP_NONE;
    return m;
}

/**
 * @brief A field set for the run's write; false where the model refuses it.
 */
static bool run_edit(kst_field_id_t id, uint16_t raw)
{
    kst_prog_t *p = &k.prog;
    const kst_prog_edit_t e = kst_prog_edit(p, id, raw);

    if (e == KST_PROG_EDIT_SOFT) {
        /* The start's hold is the consent: the run lowers the angles and
         * puts back the servo's own values. */
        kst_prog_soft_confirm(p);
        return kst_field_get(kst_prog_draft(p), id) == raw;
    }
    return e == KST_PROG_EDIT_OK;
}

/**
 * @brief Write two fields, @p a to @p va and @p b to @p vb, and nothing else.
 */
static kst_prog_msg_id_t run_write2(kst_field_id_t a, uint16_t va,
                                    kst_field_id_t b, uint16_t vb)
{
    kst_prog_t *p = &k.prog;
    kst_prog_msg_id_t m;

    if (kst_prog_state(p) != KST_PROG_PROGRAMMING || !kst_prog_editable(p)) {
        return KST_PROG_MSG_NEED_READ;
    }
    run_close_editors();
    if (k.run.writes == 0u && !kst_prog_run_hold(p)) {
        return KST_PROG_MSG_NEED_READ;
    }
    m = kst_prog_enable(p, true);
    if (m != KST_PROG_MSG_NONE) {
        return m;
    }
    /* From here every way out but the write on its way switches write
     * enable off again, as the write's end does (kst_panel_run_poll()):
     * a session the run gives up holds no enabled write. */
    kst_prog_discard(p);
    if (!run_edit(a, va) || !run_edit(b, vb)) {
        const kst_prog_msg_id_t why = kst_prog_message(p)->id;

        kst_prog_discard(p);
        (void)kst_prog_enable(p, false);
        return why != KST_PROG_MSG_NONE ? why : KST_PROG_MSG_EDIT_LOCKED;
    }
    m = kst_prog_write(p);
    if (m != KST_PROG_MSG_NONE) {
        kst_prog_discard(p);
        (void)kst_prog_enable(p, false);
        return m;
    }
    k.run.op = RUN_OP_WRITE;
    k.run.want_id[0] = a;
    k.run.want_id[1] = b;
    k.run.want[0] = va;
    k.run.want[1] = vb;
    return KST_PROG_MSG_NONE;
}

kst_prog_msg_id_t kst_panel_run_write(uint16_t left, uint16_t right)
{
    return run_write2(KST_F_LEFT_ANGLE, left, KST_F_RIGHT_ANGLE, right);
}

kst_prog_msg_id_t kst_panel_run_pulses(uint16_t lower_dus, uint16_t upper_dus)
{
    uint16_t lo = 0u, hi = 0u;

    if (!kst_field_raw_from_display(KST_F_PULSE_LOWER, (int32_t)lower_dus,
                                    &lo)
        || !kst_field_raw_from_display(KST_F_PULSE_UPPER, (int32_t)upper_dus,
                                       &hi)) {
        return KST_PROG_MSG_EDIT_LOCKED;
    }
    return run_write2(KST_F_PULSE_LOWER, lo, KST_F_PULSE_UPPER, hi);
}

kst_prog_msg_id_t kst_panel_run_leave(void)
{
    kst_prog_t *p = &k.prog;
    kst_prog_msg_id_t m;

    if (kst_prog_state(p) != KST_PROG_PROGRAMMING) {
        return (kst_prog_msg_id_t)(KST_PROG_MSG_START + KST_LINK_START_BUSY);
    }
    run_close_editors();
    (void)kst_prog_enable(p, false);
    kst_prog_discard(p);
    m = kst_prog_leave(p);
    k.run.op = m == KST_PROG_MSG_NONE ? RUN_OP_LEAVE : RUN_OP_NONE;
    return m;
}

/**
 * @brief Reports how the limit run's step ended and takes the end.
 * @return KST_PANEL_RUN_NONE when no step is asked, KST_PANEL_RUN_BUSY while
 *        it runs, then OK or FAILED once
 */
kst_panel_run_t kst_panel_run_poll(void)
{
    kst_prog_t *p = &k.prog;
    const kst_prog_state_t st = kst_prog_state(p);
    const kst_prog_msg_id_t msg = kst_prog_message(p)->id;
    bool ok;

    if (k.run.op == RUN_OP_NONE) {
        return KST_PANEL_RUN_NONE;
    }
    if (st == KST_PROG_ENTERING || st == KST_PROG_WORKING
        || st == KST_PROG_LEAVING) {
        return KST_PANEL_RUN_BUSY;
    }
    switch (k.run.op) {
    case RUN_OP_CONNECT:
        /* The read the ENTER chains is part of it. */
        ok = st == KST_PROG_PROGRAMMING && kst_prog_have_servo(p);
        break;
    case RUN_OP_WRITE:
        ok = st == KST_PROG_PROGRAMMING && msg == KST_PROG_MSG_WRITTEN
             && kst_field_get(kst_prog_servo(p), k.run.want_id[0])
                    == k.run.want[0]
             && kst_field_get(kst_prog_servo(p), k.run.want_id[1])
                    == k.run.want[1];
        (void)kst_prog_enable(p, false);
        /* Sent, whether it ended well or not: the file saved before it
         * holds the servo as the run found it. */
        if (k.run.writes == 0u) {
            kst_prog_run_written(p);
        }
        if (ok) {
            ++k.run.writes;
        }
        break;
    default:
        ok = st == KST_PROG_PWM;
        break;
    }
    k.run.op = RUN_OP_NONE;
    ++k.rev;
    return ok ? KST_PANEL_RUN_OK : KST_PANEL_RUN_FAILED;
}

bool kst_panel_run_no_servo(void)
{
    const kst_prog_msg_t *m = kst_prog_message(&k.prog);

    return m->id == KST_PROG_MSG_SES + (unsigned)KST_SES_ERR_NO_SERVO
           && m->seq != k.run.said;
}

bool kst_panel_run_servo(uint16_t angle[2], uint16_t *lo, uint16_t *hi,
                         bool *reversion, unsigned *type,
                         uint16_t pulse_dus[3])
{
    const kst_prog_t *p = &k.prog;
    const kst_image_t *img = kst_prog_servo(p);
    uint16_t lo2 = 0u, hi2 = 0u;
    static const kst_field_id_t k_pulse[3] = {
        KST_F_NEUTRAL, KST_F_PULSE_LOWER, KST_F_PULSE_UPPER,
    };

    if (!kst_prog_have_servo(p)) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        int32_t d = 0;

        pulse_dus[i] = (kst_field_display(k_pulse[i],
                                          kst_field_get(img, k_pulse[i]), &d)
                        && d > 0 && d <= 0xFFFF)
                           ? (uint16_t)d : 0u;
    }
    angle[0] = kst_field_get(img, KST_F_LEFT_ANGLE);
    angle[1] = kst_field_get(img, KST_F_RIGHT_ANGLE);
    *reversion = kst_field_get(img, KST_F_REVERSION) != 0u;
    *type = kst_card_type(img);
    if (!kst_prog_editable(p)
        || !kst_prog_range(p, KST_F_LEFT_ANGLE, lo, hi)
        || !kst_prog_range(p, KST_F_RIGHT_ANGLE, &lo2, &hi2)) {
        *lo = 0u;
        *hi = 0u;
        return false;
    }
    /* The narrower of the two: one value for both registers. */
    *lo = *lo > lo2 ? *lo : lo2;
    *hi = *hi < hi2 ? *hi : hi2;
    return *lo <= *hi;
}

const char *kst_panel_run_backup(void)
{
    return kst_prog_run_backup(&k.prog);
}

bool kst_panel_run_ready(void)
{
    return kst_prog_state(&k.prog) == KST_PROG_PWM;
}

void kst_panel_run_begin(void)
{
    k.run.op = RUN_OP_NONE;
    k.run.writes = 0u;
    k.run.kept = false;
}

void kst_panel_run_end(void)
{
    kst_prog_t *p = &k.prog;

    k.run.op = RUN_OP_NONE;
    kst_prog_run_close(p);
    if (k.run.kept) {
        k.run.kept = false;
        kst_prog_supply_set(p, k.run.keep_mv, k.run.keep_ma);
    }
    ++k.rev;
}

/**
 * @brief Gives the model the overlay holds.
 * @return the model
 */
kst_prog_t *kst_panel_model(void)
{
    return &k.prog;
}
