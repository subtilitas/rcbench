/**
 * @file
 * @brief What a column means, from what it is called.
 *
 * A log names its columns but not their roles: "Time(ms)", "t", "timestamp"
 * and "Zeit" are all the x-axis, and "V", "Volt" and "Spannung" are all a
 * voltage.  This maps a header name to a role and a unit by matching the names
 * that actually appear in the field, case-folded, so the plot knows which
 * column is time and what each trace is measured in without being told.
 *
 * SPDX-License-Identifier: MIT
 */

#include "log_fields.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

/** @brief How a rule's pattern is matched against a column name. */
typedef enum {
    M_PREFIX = 0,   /**< name starts with pattern            */
    M_EXACT,        /**< name equals pattern                 */
    M_PREFIX_CI,    /**< starts with, case-insensitively     */
    M_ANY_OF        /**< equals one of a '|'-separated list  */
} match_kind_t;

/** @brief One naming rule: how to match, and the group and unit it gives. */
typedef struct {
    match_kind_t kind;       /**< how pattern is matched                   */
    const char *pattern;     /**< the name, prefix or list to match        */
    const char *group;       /**< group the column belongs to              */
    const char *unit;        /**< unit the column is measured in           */
} rule_t;

/**
 * @brief Order matters: the first match wins.
 */
static const rule_t k_rules[] = {
    { M_EXACT,  "loopIteration",  "Meta",  ""      },
    { M_EXACT,  "time",           "Meta",  "s"     },
    { M_PREFIX, "axisP[",         "PID",   ""      },
    { M_PREFIX, "axisI[",         "PID",   ""      },
    { M_PREFIX, "axisD[",         "PID",   ""      },
    { M_PREFIX, "axisF[",         "PID",   ""      },
    { M_PREFIX, "axisSum[",       "PID",   ""      },
    { M_PREFIX, "axisError[",     "PID",   ""      },
    { M_PREFIX, "rcCommand[",     "RC",    ""      },
    { M_PREFIX, "setpoint[",      "RC",    ""      },
    { M_EXACT,  "rssi",           "RC",    ""      },
    { M_PREFIX, "gyroADC[",       "Gyro",  "\xC2\xB0" "/s" },
    { M_PREFIX, "gyroUnfilt[",    "Gyro",  "\xC2\xB0" "/s" },
    { M_PREFIX, "accSmooth[",     "Accel", "g"     },
    { M_PREFIX, "motor[",         "Motor", ""      },
    { M_PREFIX, "eRPM[",          "RPM",   "eRPM"  },
    { M_PREFIX, "eRPMkiss[",      "RPM",   "eRPM"  },
    { M_PREFIX, "rpm[",           "RPM",   "eRPM"  },
    { M_PREFIX, "escTemperature", "ESC",   "\xC2\xB0" "C" },
    { M_PREFIX, "escConsumption", "ESC",   "mAh"   },
    { M_PREFIX, "escStress",      "ESC",   ""      },
    { M_PREFIX_CI, "vbat",        "Power", "V"     },
    { M_PREFIX_CI, "amperage",    "Power", "A"     },
    { M_EXACT,  "energyCumulative", "Power", "mAh" },
    { M_PREFIX, "debug[",         "Debug", ""      },
    { M_PREFIX, "magADC[",        "Mag",   ""      },
    { M_EXACT,  "BaroAlt",        "Baro",  "m"     },
    { M_PREFIX, "heading[",       "Attitude", "rad" },
    { M_ANY_OF, "flightModeFlags|stateFlags|failsafePhase|"
                "rxSignalReceived|rxFlightChannelsValid", "Flags", "" },
    { M_PREFIX, "GPS_",           "GPS",   ""      },
    /* The bench's own log names its source beside the voltage and current
     * it shows: the INA228's two, and the ESC's own current. */
    { M_EXACT,  "ina voltage",    "INA228", "V"    },
    { M_EXACT,  "ina current",    "INA228", "A"    },
    { M_EXACT,  "esc current",    "ESC",   "A"     },
    /* And the INA3221's 50 ms window, once per window: CH1 to CH3, or with
     * SENSE_MAP a group per watched target (log_cols.h). */
    { M_ANY_OF, "window|ch1 current|ch1 max|ch1 voltage|ch2 current|"
                "ch2 max|ch2 voltage|ch3 current|ch3 max|ch3 voltage",
                "INA3221", "" },
    { M_PREFIX, "out ",           "INA3221", "" },
    { M_PREFIX, "port ",          "INA3221", "" },
    { M_PREFIX, "supply ",        "INA3221", "" },
};

/**
 * @brief Whether a name starts with a prefix.
 *
 * @param s       The column name.
 * @param prefix  The prefix to find.
 * @param fold    true to compare letters without regard to case.
 * @return true when @p s starts with @p prefix.
 */
static bool starts_with(const char *s, const char *prefix, bool fold)
{
    while (*prefix != '\0') {
        char a = *s++;
        char b = *prefix++;
        if (fold) {
            a = (char)tolower((unsigned char)a);
            b = (char)tolower((unsigned char)b);
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Whether a name equals one of a '|'-separated list.
 *
 * @param s     The column name.
 * @param list  The names, separated by '|'.
 * @return true when @p s equals one entry of @p list.
 */
static bool any_of(const char *s, const char *list)
{
    size_t n = strlen(s);
    const char *p = list;
    while (*p != '\0') {
        const char *end = strchr(p, '|');
        size_t len = (end != NULL) ? (size_t)(end - p) : strlen(p);
        if (len == n && memcmp(p, s, n) == 0) {
            return true;
        }
        if (end == NULL) {
            break;
        }
        p = end + 1;
    }
    return false;
}

log_field_meta_t log_field_meta(const char *name)
{
    log_field_meta_t other = { "Other", "" };
    if (name == NULL) {
        return other;
    }

    for (size_t i = 0; i < sizeof(k_rules) / sizeof(k_rules[0]); ++i) {
        const rule_t *r = &k_rules[i];
        bool hit = false;
        switch (r->kind) {
        case M_EXACT:
            hit = (strcmp(name, r->pattern) == 0);
            break;
        case M_PREFIX:
            hit = starts_with(name, r->pattern, false);
            break;
        case M_PREFIX_CI:
            hit = starts_with(name, r->pattern, true);
            break;
        case M_ANY_OF:
        default:
            hit = any_of(name, r->pattern);
            break;
        }
        if (hit) {
            log_field_meta_t m = { r->group, r->unit };
            return m;
        }
    }
    return other;
}
