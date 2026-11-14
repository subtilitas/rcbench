/**
 * @file settings_panes.c
 * @brief SETUP's split panes: SENSORS' (sensors_pane.h) and WIRELESS'
 *        (wireless_pane.h) left lists behind one set of calls, so the
 *        screen, its drawing and its rows treat both alike
 *        (settings_screen_int.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#include "sensors_pane.h"
#include "settings.h"
#include "wireless_pane.h"

#include "settings_screen_int.h"

int stg_pane(setting_cat_t cat)
{
    switch (cat) {
    case SET_CAT_SENSORS:  return STG_PANE_SENSORS;
    case SET_CAT_WIRELESS: return STG_PANE_WIRELESS;
    default:               return -1;
    }
}

int stg_pane_count(int pane)
{
    switch (pane) {
    case STG_PANE_SENSORS:  return SENS_ENTRY_COUNT;
    case STG_PANE_WIRELESS: return WL_ENTRY_COUNT;
    default:                return 0;
    }
}

int stg_pane_selected(int pane)
{
    switch (pane) {
    case STG_PANE_SENSORS:  return sensors_pane_selected();
    case STG_PANE_WIRELESS: return wireless_pane_selected();
    default:                return 0;
    }
}

void stg_pane_select(int pane, int entry)
{
    if (pane == STG_PANE_SENSORS) {
        sensors_pane_select(entry);
    } else if (pane == STG_PANE_WIRELESS) {
        wireless_pane_select(entry);
    }
}

const char *stg_pane_name(int pane, int entry, char *buf, size_t n)
{
    if (pane == STG_PANE_WIRELESS) {
        return wireless_pane_name(entry, buf, n);
    }
    if (pane == STG_PANE_SENSORS) {
        return sensors_pane_name(entry, buf, n);
    }
    if (buf != NULL && n > 0u) {
        buf[0] = '\0';
    }
    return buf != NULL ? buf : "";
}

const char *stg_pane_state(int pane, int entry, char *buf, size_t n,
                           gfx_color_t *ink)
{
    if (pane == STG_PANE_WIRELESS) {
        return wireless_pane_state(entry, buf, n, ink);
    }
    if (pane == STG_PANE_SENSORS) {
        return sensors_pane_state(entry, buf, n, ink);
    }
    if (buf != NULL && n > 0u) {
        buf[0] = '\0';
    }
    return buf != NULL ? buf : "";
}

void stg_pane_reset(int pane, int entry)
{
    if (pane == STG_PANE_SENSORS) {
        sensors_pane_reset(entry);
    } else if (pane == STG_PANE_WIRELESS) {
        wireless_pane_reset(entry);
    }
}
