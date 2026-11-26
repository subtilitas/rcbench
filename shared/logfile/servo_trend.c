/**
 * @file
 * @brief A run filed in a servo's folder.  See servo_trend.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_trend.h"

#include <stdio.h>

/** @brief The last call. */
static servo_trend_last_t s_last;

void servo_trend_filed(const char *id, servo_kind_t kind, int number)
{
    ++s_last.count;
    (void)snprintf(s_last.id, sizeof(s_last.id), "%s",
                   (id != NULL) ? id : "");
    s_last.kind = kind;
    s_last.number = number;
}

const servo_trend_last_t *servo_trend_last(void)
{
    return &s_last;
}
