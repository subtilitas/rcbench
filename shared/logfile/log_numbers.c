/*
 * Turning a text cell into a number, when "1,024" might be either.
 *
 * The hard problem in a locale-blind log reader is not splitting rows, it is
 * the decimal point: "1,024" is a thousand-and-twenty-four to an English
 * writer and one-point-oh-two-four to a German one, and the cell itself cannot
 * say which.  So this does not decide per cell.  log_evidence_of() classifies
 * a cell as evidence *for* a convention, evidence *against*, or genuinely
 * ambiguous, and the caller votes across the whole column -- one unambiguous
 * "1.5" settles a file that is otherwise all thousands-shaped values.
 *
 * The care is in what counts as no evidence.  A repeated separator is usually
 * grouping, but "192.168.0.1", "15.01.2024" and "1.2.3" are an address, a date
 * and a version -- numbers to nobody -- and voting on them hands the file to
 * the wrong convention.  A three-digit tail is the ambiguous shape, except
 * when the integer part cannot be a thousands group (a leading zero, or more
 * than three digits), which is exactly the millisecond-timestamp case that
 * would otherwise read an English log as German.  Each of those exceptions is
 * a real file that was read wrong before the rule existed.
 *
 * SPDX-License-Identifier: MIT
 */

#include "log_numbers.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- scanning --- */

/*
 * Two character classes that overlap, and the overlap is the whole reason this
 * is written by hand rather than with strtod():
 *
 *   grouping-only  ' '  '\''  U+00A0  U+202F   -- never a decimal point
 *   whitespace     ASCII space and friends, plus U+00A0 and U+202F
 *
 * ASCII space is in both, so "1 234,56" is one number and "12 %" is a number
 * with a unit.  What separates them is that the numeric body must end on a
 * digit: the scanner remembers where the last digit was and hands everything
 * after it to the unit.
 */

static int grouping_len(const char *p)
{
    if (*p == ' ' || *p == '\'') {
        return 1;
    }
    if ((unsigned char)p[0] == 0xC2u && (unsigned char)p[1] == 0xA0u) {
        return 2; /* U+00A0 no-break space */
    }
    if ((unsigned char)p[0] == 0xE2u && (unsigned char)p[1] == 0x80u &&
        (unsigned char)p[2] == 0xAFu) {
        return 3; /* U+202F narrow no-break space */
    }
    return 0;
}

static int space_len(const char *p)
{
    if (*p != '\0' && *p != '\'' && isspace((unsigned char)*p)) {
        return 1;
    }
    if ((unsigned char)p[0] == 0xC2u && (unsigned char)p[1] == 0xA0u) {
        return 2;
    }
    if ((unsigned char)p[0] == 0xE2u && (unsigned char)p[1] == 0x80u &&
        (unsigned char)p[2] == 0xAFu) {
        return 3;
    }
    return 0;
}

static const char *skip_space(const char *p)
{
    int n;
    while ((n = space_len(p)) > 0) {
        p += n;
    }
    return p;
}

/* Copy [begin, end) into out, dropping grouping-only characters. */
static bool copy_digits(const char *begin, const char *end, char *out,
                        size_t out_size)
{
    size_t n = 0;
    for (const char *p = begin; p < end;) {
        int g = grouping_len(p);
        if (g > 0) {
            p += g;
            continue;
        }
        if (n + 1 >= out_size) {
            return false;
        }
        out[n++] = *p++;
    }
    out[n] = '\0';
    return n > 0;
}

/* Truncate without splitting a UTF-8 sequence -- a half ° is worse than none. */
/*
 * What is left after the numeric body, read as a unit.  A trailing decimal
 * separator belongs to the number ("1500," is 1500), and a residual carrying a
 * ':' or a signed number is the tail of something the scan stopped inside --
 * a clock time, an ISO date, a hyphenated part number.  Those are not numbers
 * with units, and accepting them makes a whole column look numeric.
 */
static const char *unit_begin(const char *p)
{
    while (*p == '.' || *p == ',') {
        ++p;
    }
    return p;
}

static bool unit_is_plausible(const char *p)
{
    for (; *p != '\0'; ++p) {
        if (*p == ':') {
            return false;
        }
        if ((*p == '-' || *p == '+') && isdigit((unsigned char)p[1])) {
            return false;
        }
    }
    return true;
}

static void copy_trimmed(const char *begin, const char *end, char *out,
                         size_t out_size)
{
    while (begin < end && space_len(begin) > 0) {
        begin += space_len(begin);
    }
    while (end > begin) {
        const char *q = end - 1;
        /* step back to the start of the last character before testing it */
        while (q > begin && ((unsigned char)*q & 0xC0u) == 0x80u) {
            --q;
        }
        int n = space_len(q);
        if (n > 0 && q + n == end) {
            end = q;
        } else {
            break;
        }
    }

    size_t n = (size_t)(end - begin);
    if (n >= out_size) {
        n = out_size - 1;
        while (n > 0 && ((unsigned char)begin[n] & 0xC0u) == 0x80u) {
            --n;
        }
    }
    memcpy(out, begin, n);
    out[n] = '\0';
}

bool log_split_value(const char *raw, log_value_t *out)
{
    if (raw == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->sign = 1;

    const char *p = skip_space(raw);
    if (*p == '+' || *p == '-') {
        out->sign = (*p == '-') ? -1 : 1;
        ++p;
        p = skip_space(p);
    }

    if (!isdigit((unsigned char)*p)) {
        return false;
    }

    /* Numeric body: digits, separators and grouping spaces, ending on a digit. */
    const char *begin = p;
    const char *last_digit = p;
    for (;;) {
        int g;
        if (isdigit((unsigned char)*p)) {
            last_digit = ++p;
        } else if (*p == '.' || *p == ',') {
            ++p;
        } else if ((g = grouping_len(p)) > 0) {
            p += g;
        } else {
            break;
        }
    }

    if (!copy_digits(begin, last_digit, out->digits, sizeof(out->digits))) {
        return false;
    }

    p = skip_space(last_digit);

    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        int sign = 1;
        if (*q == '+' || *q == '-') {
            sign = (*q == '-') ? -1 : 1;
            ++q;
        }
        if (isdigit((unsigned char)*q)) {
            int exp = 0;
            while (isdigit((unsigned char)*q)) {
                if (exp >= 10000) {
                    return false;   /* not a number we can represent */
                }
                exp = exp * 10 + (*q - '0');
                ++q;
            }
            out->exp = sign * exp;
            p = skip_space(q);
        }
    }

    /* Whatever is left is the unit -- but only if it can be one.  A residual
     * carrying digits or a ':' is the tail of something the numeric scan
     * stopped in the middle of: "2024-01-15T10:00:00" reads as 2024 with the
     * unit "-01-15T10:0", and a whole timestamp column then reports itself as
     * numeric, constant, and fit to be the time axis. */
    p = unit_begin(p);
    if (!unit_is_plausible(p)) {
        return false;
    }
    copy_trimmed(p, p + strlen(p), out->unit, sizeof(out->unit));
    return true;
}

/* ------------------------------------------------------------- evidence --- */

/* The integer part, read as a thousands group: 1-3 digits and no leading zero
 * (a bare "0" cannot be followed by a group either). */
static bool leading_group_is_legal(const char *digits, char sep)
{
    const char *end = strchr(digits, sep);
    size_t len = (end != NULL) ? (size_t)(end - digits) : strlen(digits);
    if (len == 0u || len > 3u) {
        return false;
    }
    return digits[0] != '0';
}

/* Every group of a repeated separator: leading 1-3 digits, the rest exactly
 * three, digits throughout. */
static bool grouping_is_legal(const char *digits, char sep)
{
    if (!leading_group_is_legal(digits, sep)) {
        return false;
    }
    const char *p = strchr(digits, sep);
    while (p != NULL) {
        const char *q = p + 1;
        size_t len = 0u;
        while (isdigit((unsigned char)*q)) {
            ++q;
            ++len;
        }
        if (len != 3u || (*q != '\0' && *q != sep)) {
            return false;
        }
        p = (*q == sep) ? q : NULL;
    }
    return true;
}

log_evidence_t log_evidence_of(const char *digits)
{
    if (digits == NULL) {
        return LOG_EV_NONE;
    }
    const char *dot = strrchr(digits, '.');
    const char *comma = strrchr(digits, ',');

    /* Both present: whichever comes last is the decimal point. */
    if (dot != NULL && comma != NULL) {
        return (comma > dot) ? LOG_EV_DE : LOG_EV_EN;
    }

    char sep = (dot != NULL) ? '.' : (comma != NULL) ? ',' : '\0';
    if (sep == '\0') {
        return LOG_EV_NONE;
    }

    int count = 0;
    for (const char *p = digits; *p != '\0'; ++p) {
        if (*p == sep) {
            ++count;
        }
    }
    /* A repeated separator can only be grouping: "1.234.567".  But only if the
     * groups are actually grouping -- "1.2.3", "192.168.0.1" and "15.01.2024"
     * are a version, an address and a date, and voting on them hands the file
     * to the wrong convention on the strength of a column that is not a
     * number at all. */
    if (count > 1) {
        return grouping_is_legal(digits, sep)
                   ? ((sep == '.') ? LOG_EV_DE : LOG_EV_EN)
                   : LOG_EV_NONE;
    }

    const char *tail = ((dot != NULL) ? dot : comma) + 1;
    /* A grouping separator is always followed by exactly three digits.
     * Anything else settles it as a decimal point. */
    if (strlen(tail) != 3u) {
        return (sep == ',') ? LOG_EV_DE : LOG_EV_EN;
    }

    /* Three trailing digits are the ambiguous shape -- "1,024" is a legal
     * thousands group and a legal three-decimal value.  Unless the integer
     * part cannot be a leading group at all: a thousands group is 1-3 digits
     * and never has a leading zero, so "0.000" and "12345,678" are decimal
     * readings and nothing else.  Millisecond timestamps are the common case,
     * and treating them as no evidence is what lets a whole English log be
     * read as German. */
    if (!leading_group_is_legal(digits, sep)) {
        return (sep == ',') ? LOG_EV_DE : LOG_EV_EN;
    }

    return LOG_EV_AMBIGUOUS;
}

void log_votes_reset(log_votes_t *v)
{
    if (v != NULL) {
        memset(v, 0, sizeof(*v));
    }
}

void log_votes_add(log_votes_t *v, const char *raw)
{
    log_value_t parts;
    if (v == NULL || !log_split_value(raw, &parts)) {
        return;
    }
    switch (log_evidence_of(parts.digits)) {
    case LOG_EV_DE:
        v->de++;
        break;
    case LOG_EV_EN:
        v->en++;
        break;
    case LOG_EV_AMBIGUOUS:
        v->ambiguous++;
        if (v->ambiguous_sep == '\0') {
            v->ambiguous_sep = (strchr(parts.digits, ',') != NULL) ? ',' : '.';
        }
        break;
    case LOG_EV_NONE:
    default:
        break;
    }
}

log_conv_t log_votes_result(const log_votes_t *v, log_ambig_t fallback,
                            bool *confident, bool *conflict)
{
    bool conf = false;
    bool clash = false;
    log_conv_t conv = LOG_CONV_EN;

    if (v != NULL && (v->de > 0 || v->en > 0)) {
        conv = (v->de >= v->en) ? LOG_CONV_DE : LOG_CONV_EN;
        conf = true;
        /* Both conventions proven in one file means the file is inconsistent. */
        clash = (v->de > 0 && v->en > 0);
    } else if (v != NULL) {
        /* No evidence anywhere.  Interpret the ambiguous separator per the
         * fallback: treating "1,234" as thousands means comma groups, i.e.
         * English. */
        if (v->ambiguous_sep == ',') {
            conv = (fallback == LOG_AMBIG_THOUSANDS) ? LOG_CONV_EN : LOG_CONV_DE;
        } else if (v->ambiguous_sep == '.') {
            conv = (fallback == LOG_AMBIG_THOUSANDS) ? LOG_CONV_DE : LOG_CONV_EN;
        }
    }

    if (confident != NULL) {
        *confident = conf;
    }
    if (conflict != NULL) {
        *conflict = clash;
    }
    return conv;
}

/* ---------------------------------------------------------------- parse --- */

static bool all_digits(const char *begin, const char *end)
{
    if (begin >= end) {
        return false;
    }
    for (const char *p = begin; p < end; ++p) {
        if (!isdigit((unsigned char)*p)) {
            return false;
        }
    }
    return true;
}

bool log_is_well_formed(const char *digits, log_conv_t convention)
{
    if (digits == NULL) {
        return false;
    }
    char decimal_sep = (convention == LOG_CONV_DE) ? ',' : '.';
    char group_sep = (convention == LOG_CONV_DE) ? '.' : ',';

    const char *first_dec = strchr(digits, decimal_sep);
    if (first_dec != NULL && strchr(first_dec + 1, decimal_sep) != NULL) {
        return false; /* more than one decimal point */
    }

    const char *int_begin = digits;
    const char *int_end = (first_dec != NULL) ? first_dec : digits + strlen(digits);

    /* The fractional part may not contain any separator at all. */
    if (first_dec != NULL && !all_digits(first_dec + 1, digits + strlen(digits))) {
        return false;
    }

    if (memchr(int_begin, group_sep, (size_t)(int_end - int_begin)) != NULL) {
        /* Leading group is 1-3 digits, every later group exactly 3. */
        const char *p = int_begin;
        bool first = true;
        while (p <= int_end) {
            const char *q = p;
            while (q < int_end && *q != group_sep) {
                ++q;
            }
            size_t len = (size_t)(q - p);
            if (!all_digits(p, q)) {
                return false;
            }
            if (first) {
                if (len > 3u) {
                    return false;
                }
                first = false;
            } else if (len != 3u) {
                return false;
            }
            if (q >= int_end) {
                break;
            }
            p = q + 1;
        }
    } else if (!all_digits(int_begin, int_end) && int_end != int_begin) {
        return false;
    }

    for (const char *p = digits; *p != '\0'; ++p) {
        if (isdigit((unsigned char)*p)) {
            return true;
        }
    }
    return false;
}

bool log_parse_with(const char *raw, log_conv_t convention, double *value,
                    char *unit, size_t unit_size)
{
    log_value_t parts;
    if (!log_split_value(raw, &parts)) {
        return false;
    }
    if (!log_is_well_formed(parts.digits, convention)) {
        return false;
    }

    char decimal_sep = (convention == LOG_CONV_DE) ? ',' : '.';
    char group_sep = (convention == LOG_CONV_DE) ? '.' : ',';

    char plain[LOG_DIGITS_MAX];
    size_t n = 0;
    for (const char *p = parts.digits; *p != '\0'; ++p) {
        if (*p == group_sep) {
            continue;
        }
        plain[n++] = (*p == decimal_sep) ? '.' : *p;
    }
    plain[n] = '\0';

    char *end = NULL;
    double v = strtod(plain, &end);
    if (end == plain) {
        return false;
    }
    if (parts.exp != 0) {
        v *= pow(10.0, (double)parts.exp);
    }
    /* After the exponent, not before it: "1e400" overflows here, and an inf
     * that reaches a column's min/max collapses the whole plot to one pixel. */
    v = (double)parts.sign * v;
    if (!isfinite(v)) {
        return false;
    }

    if (value != NULL) {
        *value = v;
    }
    if (unit != NULL && unit_size > 0u) {
        size_t len = strlen(parts.unit);
        if (len >= unit_size) {
            len = unit_size - 1u;
        }
        memcpy(unit, parts.unit, len);
        unit[len] = '\0';
    }
    return true;
}

bool log_parse_loose(const char *raw, log_ambig_t fallback, double *value)
{
    log_votes_t v;
    log_votes_reset(&v);
    log_votes_add(&v, raw);
    log_conv_t conv = log_votes_result(&v, fallback, NULL, NULL);
    return log_parse_with(raw, conv, value, NULL, 0);
}

/* ---------------------------------------------------------------- units --- */

void log_unit_tally_reset(log_unit_tally_t *t)
{
    if (t != NULL) {
        memset(t, 0, sizeof(*t));
    }
}

void log_unit_tally_add(log_unit_tally_t *t, const char *unit)
{
    if (t == NULL || unit == NULL || unit[0] == '\0') {
        return;
    }
    for (int i = 0; i < t->used; ++i) {
        if (strcmp(t->units[i], unit) == 0) {
            t->counts[i]++;
            return;
        }
    }
    if (t->used >= LOG_UNIT_SLOTS) {
        /* More distinct suffixes than slots.  The exact winner stops mattering
         * at that point; that the column is mixed is the reportable fact. */
        t->overflowed = true;
        return;
    }
    size_t len = strlen(unit);
    if (len >= LOG_UNIT_MAX) {
        len = LOG_UNIT_MAX - 1u;
    }
    memcpy(t->units[t->used], unit, len);
    t->units[t->used][len] = '\0';
    t->counts[t->used] = 1;
    t->used++;
}

void log_unit_dominant(const log_unit_tally_t *t, char *out, size_t out_size,
                       bool *mixed)
{
    if (out != NULL && out_size > 0u) {
        out[0] = '\0';
    }
    if (mixed != NULL) {
        *mixed = false;
    }
    if (t == NULL || t->used == 0) {
        return;
    }

    int best = 0;
    for (int i = 1; i < t->used; ++i) {
        if (t->counts[i] > t->counts[best]) {
            best = i;
        }
    }
    if (out != NULL && out_size > 0u) {
        size_t len = strlen(t->units[best]);
        if (len >= out_size) {
            len = out_size - 1u;
        }
        memcpy(out, t->units[best], len);
        out[len] = '\0';
    }
    if (mixed != NULL) {
        *mixed = (t->used > 1) || t->overflowed;
    }
}

/* Units we will believe when they follow a space, e.g. "altitude m".  A
 * whitelist rather than a shape test, because "power output" also has the
 * shape of a name followed by a short word. */
static const char *const k_bare_units[] = {
    "m", "km", "cm", "mm", "s", "ms", "us", "min", "h", "A", "mA", "V", "mV",
    "W", "kW", "Wh", "mAh", "Hz", "kHz", "rpm", "g", "kg", "N", "Pa", "hPa",
    "bar", "%", "\xC2\xB0" "C", "\xC2\xB0" "F", "deg", "rad", "m/s", "km/h",
    "deg/s",
};

static bool ieq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

static bool is_bare_unit(const char *s)
{
    for (size_t i = 0; i < sizeof(k_bare_units) / sizeof(k_bare_units[0]); ++i) {
        if (ieq(s, k_bare_units[i])) {
            return true;
        }
    }
    return false;
}

static void copy_range(const char *begin, const char *end, char *out,
                       size_t out_size)
{
    size_t n = (size_t)(end - begin);
    if (out == NULL || out_size == 0u) {
        return;
    }
    if (n >= out_size) {
        n = out_size - 1u;
        while (n > 0u && ((unsigned char)begin[n] & 0xC0u) == 0x80u) {
            --n;
        }
    }
    memcpy(out, begin, n);
    out[n] = '\0';
}

void log_unit_from_header(const char *header, char *name, size_t name_size,
                          char *unit, size_t unit_size)
{
    if (unit != NULL && unit_size > 0u) {
        unit[0] = '\0';
    }
    if (header == NULL) {
        if (name != NULL && name_size > 0u) {
            name[0] = '\0';
        }
        return;
    }

    const char *s = skip_space(header);
    const char *e = s + strlen(s);
    while (e > s) {
        const char *q = e - 1;
        while (q > s && ((unsigned char)*q & 0xC0u) == 0x80u) {
            --q;
        }
        int n = space_len(q);
        if (n > 0 && q + n == e) {
            e = q;
        } else {
            break;
        }
    }

    /* "voltage (V)" / "current [A]" -- but not "gyroADC[0]" or "motor[3]",
     * where the brackets hold an axis index that belongs to the name. */
    if (e > s) {
        char close = e[-1];
        if (close == ')' || close == ']' || close == '}') {
            const char *open = NULL;
            for (const char *p = e - 2; p >= s; --p) {
                if (*p == '(' || *p == '[' || *p == '{') {
                    open = p;
                    break;
                }
                if (*p == ')' || *p == ']' || *p == '}') {
                    break; /* the inner text may not contain a closing bracket */
                }
            }
            if (open != NULL) {
                char inner[LOG_UNIT_MAX * 2];
                copy_trimmed(open + 1, e - 1, inner, sizeof(inner));

                const char *name_end = open;
                while (name_end > s &&
                       (space_len(name_end - 1) > 0 || name_end[-1] == '_')) {
                    --name_end;
                }

                bool numeric_index = inner[0] != '\0';
                for (const char *p = inner; *p != '\0'; ++p) {
                    if (!isdigit((unsigned char)*p)) {
                        numeric_index = false;
                        break;
                    }
                }

                if (name_end > s && inner[0] != '\0' && !numeric_index) {
                    copy_range(s, name_end, name, name_size);
                    copy_range(inner, inner + strlen(inner), unit, unit_size);
                    return;
                }
            }
        }
    }

    /* Trailing unit after a space, e.g. "altitude m". */
    const char *last = e;
    while (last > s && space_len(last - 1) == 0) {
        --last;
    }
    if (last > s && last < e) {
        char cand[LOG_UNIT_MAX];
        copy_range(last, e, cand, sizeof(cand));
        if (is_bare_unit(cand)) {
            const char *name_end = last;
            while (name_end > s && space_len(name_end - 1) > 0) {
                --name_end;
            }
            if (name_end > s) {
                copy_range(s, name_end, name, name_size);
                copy_range(last, e, unit, unit_size);
                return;
            }
        }
    }

    copy_range(s, e, name, name_size);
}

/* Seconds per unit for the time units an exporter is likely to write. */
static const struct {
    const char *name;
    double scale;
} k_time_units[] = {
    { "s", 1.0 },          { "sec", 1.0 },       { "secs", 1.0 },
    { "second", 1.0 },     { "seconds", 1.0 },   { "sekunde", 1.0 },
    { "sekunden", 1.0 },   { "ms", 1e-3 },       { "msec", 1e-3 },
    { "millis", 1e-3 },    { "millisecond", 1e-3 }, { "milliseconds", 1e-3 },
    { "millisekunden", 1e-3 }, { "us", 1e-6 },   { "\xC2\xB5" "s", 1e-6 },
    { "microsecond", 1e-6 }, { "microseconds", 1e-6 },
    { "mikrosekunden", 1e-6 }, { "ns", 1e-9 },   { "min", 60.0 },
    { "minute", 60.0 },    { "minutes", 60.0 },  { "minuten", 60.0 },
    { "h", 3600.0 },       { "hr", 3600.0 },     { "hour", 3600.0 },
    { "hours", 3600.0 },   { "stunden", 3600.0 },
};

double log_time_unit_scale(const char *unit)
{
    if (unit == NULL || unit[0] == '\0') {
        return 0.0;
    }
    for (size_t i = 0; i < sizeof(k_time_units) / sizeof(k_time_units[0]); ++i) {
        if (ieq(unit, k_time_units[i].name)) {
            return k_time_units[i].scale;
        }
    }
    return 0.0;
}
