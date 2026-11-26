/**
 * @file
 * @brief The active servo.  See servo_active.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_active.h"

#include <stdio.h>
#include <string.h>

#include "servo_id.h"

/** @brief The active servo and its changes. */
typedef struct {
    servo_id_t          id;       /**< the id, when from is not NONE */
    servo_active_from_t from;     /**< its source; NONE for none     */
    bool                printed;  /**< a label of it printed         */
    uint32_t            serial;   /**< changes counted               */
} active_t;

/** @brief The one instance, none after power-on. */
static active_t s;

const char *servo_active_id(void)
{
    return (s.from != SERVO_ACTIVE_NONE) ? s.id.text : NULL;
}

servo_active_from_t servo_active_from(void)
{
    return s.from;
}

bool servo_active_set(const char *id, servo_active_from_t from,
                      bool run_active)
{
    servo_id_t p;
    if (run_active || (int)from <= (int)SERVO_ACTIVE_NONE
        || (int)from > (int)SERVO_ACTIVE_TYPED
        || !servo_id_parse(id, &p)) {
        return false;
    }
    const bool same = s.from != SERVO_ACTIVE_NONE
                      && strcmp(s.id.text, p.text) == 0;
    if (same && s.from == from) {
        return true;
    }
    if (!same) {
        s.id = p;
        s.printed = false;
    }
    s.from = from;
    ++s.serial;
    return true;
}

bool servo_active_clear(bool run_active)
{
    if (run_active) {
        return false;
    }
    if (s.from != SERVO_ACTIVE_NONE) {
        memset(&s.id, 0, sizeof(s.id));
        s.from = SERVO_ACTIVE_NONE;
        s.printed = false;
        ++s.serial;
    }
    return true;
}

uint32_t servo_active_serial(void)
{
    return s.serial;
}

bool servo_active_printed(void)
{
    return s.from != SERVO_ACTIVE_NONE && s.printed;
}

void servo_active_set_printed(void)
{
    if (s.from != SERVO_ACTIVE_NONE && !s.printed) {
        s.printed = true;
        ++s.serial;
    }
}

/* ------------------------------------------------------------ the scan */

/** @brief The last line taken, for the repeat check. */
typedef struct {
    char     line[SERVO_SCAN_LINE_MAX + 1u];  /**< its first bytes          */
    bool     too_long;                        /**< it was cut                */
    bool     valid;                           /**< a line has been taken     */
    uint32_t at_ms;                           /**< when                      */
    uint32_t serial;                          /**< servo_active_serial() after it */
} scan_last_t;

/** @brief The one instance: no line after power-on. */
static scan_last_t s_scan;

/**
 * @brief Whether @p line is the last line taken again, within
 *        SERVO_SCAN_REPEAT_MS and with no change of the servo since.
 */
static bool scan_repeat(const char *line, bool too_long, uint32_t now_ms)
{
    return s_scan.valid && s_scan.too_long == too_long
           && s_scan.serial == s.serial
           && (uint32_t)(now_ms - s_scan.at_ms) < SERVO_SCAN_REPEAT_MS
           && strncmp(s_scan.line, line, SERVO_SCAN_LINE_MAX) == 0
           && strlen(line) <= SERVO_SCAN_LINE_MAX;
}

/** @brief What @p line does, before the repeat memory takes it. */
static servo_scan_t scan_judge(const char *line, bool too_long,
                               bool run_active, servo_active_from_t from)
{
    servo_id_t p;
    if (too_long) {
        return SERVO_SCAN_TOO_LONG;
    }
    if (!servo_id_parse(line, &p) || strcmp(p.text, line) != 0) {
        return SERVO_SCAN_MALFORMED;
    }
    const bool same = s.from != SERVO_ACTIVE_NONE
                      && strcmp(s.id.text, p.text) == 0;
    if (run_active) {
        return same ? SERVO_SCAN_RUN_OWN : SERVO_SCAN_REFUSED_RUN;
    }
    (void)servo_active_set(p.text, from, false);
    return same ? SERVO_SCAN_SAME : SERVO_SCAN_SET;
}

servo_scan_t servo_scan_decide(const char *line, bool too_long,
                               bool run_active, uint32_t now_ms)
{
    return servo_scan_decide_from(line, too_long, run_active, now_ms,
                                  SERVO_ACTIVE_SCAN);
}

servo_scan_t servo_scan_decide_from(const char *line, bool too_long,
                                    bool run_active, uint32_t now_ms,
                                    servo_active_from_t from)
{
    if (line == NULL) {
        line = "";
    }
    if (from == SERVO_ACTIVE_TYPED) {
        /* Typed on purpose: no repeat, and the scanner's memory stays. */
        return scan_judge(line, too_long, run_active, SERVO_ACTIVE_TYPED);
    }
    if (scan_repeat(line, too_long, now_ms)) {
        return SERVO_SCAN_REPEAT;
    }
    const servo_scan_t r = scan_judge(line, too_long, run_active,
                                      SERVO_ACTIVE_SCAN);
    (void)snprintf(s_scan.line, sizeof(s_scan.line), "%s", line);
    s_scan.too_long = too_long;
    s_scan.valid = true;
    s_scan.at_ms = now_ms;
    s_scan.serial = s.serial;
    return r;
}
