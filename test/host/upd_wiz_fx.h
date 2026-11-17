/*
 * The step model's test fixture (test_upd_wizard.c, test_upd_wizard_map.c):
 * the four views UPDATES hands the model, filled as every step current or
 * every step open, and the checks of a step's state, tag and reason.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "devupd.h"
#include "upd_wizard.h"

/** @brief The versions the cases run and carry. @{ */
#define K_OLD    ((fw_version_t){ 0u, 23u, 14u })
#define K_NEW    ((fw_version_t){ 0u, 23u, 15u })
#define K_C6     ((fw_version_t){ 2u, 13u, 0u })
#define K_C6_NEW ((fw_version_t){ 2u, 14u, 0u })
#define K_CU_OLD ((cu_version_t){ 0u, 23u, 14u })
#define K_CU_NEW ((cu_version_t){ 0u, 23u, 15u })
/** @} */

/** @brief The four views and the input that points at them. */
typedef struct {
    upd_view_t     p;                  /**< UPDATES' view */
    upd_cu_view_t  cu;                 /**< the IOmcu section */
    upd_c6_view_t  c6;                 /**< the C6 row */
    upd_dev_view_t d[UPD_DEV_COUNT];   /**< the device rows */
    upd_wiz_in_t   in;                 /**< run, skip, results */
} fx_t;

/** @brief A device row: connected and on @p running, or as told. */
static inline upd_dev_view_t dev_of(devupd_row_t row, bool bound, bool known,
                             fw_version_t running)
{
    upd_dev_view_t v;
    memset(&v, 0, sizeof(v));
    v.row = row;
    v.bound = bound;
    v.known = known;
    v.running = known ? running : (fw_version_t){ 0u, 0u, 0u };
    v.have_image = true;
    v.carried = K_NEW;
    return v;
}

/** @brief A check ended with @p got. */
static inline void panel_ended(fx_t *f, upd_got_t got, const char *found)
{
    f->p.job = UPD_JOB_CHECK;
    f->p.phase = UPD_PH_ENDED;
    f->p.got = got;
    (void)snprintf(f->p.found, sizeof(f->p.found), "%s", found);
}

/** @brief Every step on its newest version; PANEL not checked. */
static inline void all_current(fx_t *f)
{
    memset(f, 0, sizeof(*f));
    (void)snprintf(f->p.running, sizeof(f->p.running), "0.23.15");
    f->p.have_net = true;
    f->cu = (upd_cu_view_t){ .sec = CU_SEC_SAME, .known = true,
                             .have_image = true, .running = K_CU_NEW,
                             .carried = K_CU_NEW };
    f->c6 = (upd_c6_view_t){ .row = FW_C6_ROW_REINSTALL, .known = true,
                             .running = K_C6, .have_image = true,
                             .carried = K_C6 };
    for (unsigned i = 0u; i < UPD_DEV_COUNT; ++i) {
        f->d[i] = dev_of(DEVUPD_ROW_REINSTALL, true, true, K_NEW);
    }
}

/** @brief Every step with another version and its hold live. */
static inline void all_open(fx_t *f)
{
    all_current(f);
    (void)snprintf(f->p.running, sizeof(f->p.running), "0.23.14");
    panel_ended(f, UPD_GOT_NEWER, "0.23.15");
    f->cu.sec = CU_SEC_READY;
    f->cu.running = K_CU_OLD;
    f->c6.row = FW_C6_ROW_READY;
    f->c6.carried = K_C6_NEW;
    for (unsigned i = 0u; i < UPD_DEV_COUNT; ++i) {
        f->d[i] = dev_of(DEVUPD_ROW_READY, true, true, K_OLD);
    }
}

/** @brief The model's answer for @p f. */
static inline upd_wiz_t eval(fx_t *f)
{
    f->in.panel = &f->p;
    f->in.cu = &f->cu;
    f->in.c6 = &f->c6;
    for (unsigned i = 0u; i < UPD_DEV_COUNT; ++i) {
        f->in.dev[i] = &f->d[i];
    }
    upd_wiz_t w;
    memset(&w, 0xA5, sizeof(w));
    upd_wizard_eval(&f->in, &w);
    return w;
}

/** @brief A step's state and tag. */
#define CHECK_STEP(w, i, st, tg)                                              \
    do {                                                                      \
        CHECK_EQ((w).step[i].state, (st));                                    \
        CHECK_EQ((w).step[i].tag, (tg));                                      \
    } while (0)

/** @brief One expected mapping of a table. */
typedef struct {
    upd_wiz_state_t state; /**< the step's state */
    upd_wiz_tag_t   tag;   /**< its tag */
    upd_wiz_why_t   why;   /**< its reason */
} want_t;

/** @brief Checks a step against a table's row, naming the row. */
static inline void check_want(const upd_wiz_step_t *st, want_t want,
                       const char *what, unsigned a, unsigned b)
{
    if (st->state != want.state || st->tag != want.tag
        || st->why != want.why) {
        T_FAIL("%s %u/%u: state %d tag %d why %d, want %d %d %d", what, a, b,
               (int)st->state, (int)st->tag, (int)st->why, (int)want.state,
               (int)want.tag, (int)want.why);
    }
}
