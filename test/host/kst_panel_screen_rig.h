/*
 * The programmer overlay's cases as test_kst_panel.c and
 * test_kst_panel_*.c share them: the status line, the typed value, the
 * held-port frame counters and the supply's rows, on top of
 * kst_panel_rig.h.
 *
 * Only static inline helpers and the buffers and counters they use.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "kst_panel_rig.h"

/* The SERVO tab's line @p i under its rows, "" for none. */
static inline const char *status(int i)
{
    const char *t = kst_panel_status_line(i);

    return t != NULL ? t : "";
}

/* MORE, and the entry @p i of its list. */
static inline void more_input(int i)
{
    tap(kst_panel_tab_rect(PG_SERVO));
    tap(val(PG_SERVO, 2, 1));
    CHECK_EQ(kst_panel_editor(), 2);
    tap(kst_panel_choice_rect(i));
}

/* Digits, a dot for '.', then OK. */
static inline void type_text(const char *t)
{
    static const ui_key_t d[] = { UI_KEY_0, UI_KEY_1, UI_KEY_2, UI_KEY_3,
                                  UI_KEY_4, UI_KEY_5, UI_KEY_6, UI_KEY_7,
                                  UI_KEY_8, UI_KEY_9 };

    key(UI_KEY_CLR);
    for (; *t != '\0'; ++t) {
        key(*t == '.' ? UI_KEY_DOT : d[*t - '0']);
    }
    key(UI_KEY_OK);
}
