/**
 * @file test_text_formats.c
 * @brief The format strings of ui_text.def against their declared fields,
 *        as the panel shows them: formatted with the widest arguments
 *        their callers pass.
 *
 * test_text.c holds a string to its declared width as it stands in the
 * table.  A format string stands there as its template, where "%s" is 2
 * cells whatever it becomes.  This suite formats every UI_TEXT entry that
 * has a declared width and a conversion, each conversion with the widest
 * argument of its kind (widest_of()) or with the arguments k_args names
 * for the entry, and holds the result to the declared width in every
 * language.
 *
 * Cases:
 * - the_formatter_writes_each_conversion_at_its_widest
 * - every_format_fits_its_declared_field_as_formatted
 * - every_listed_argument_belongs_to_a_format_with_a_declared_field
 * - a_logs_message_with_an_8_3_name_fits_the_footers_two_rows
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "gfx.h"
#include "log_viewer_int.h"
#include "ui_text.h"
#include "ui_widgets.h"

/** @brief The most conversions one entry's arguments are listed for. */
#define ARGS_MAX 6

/** @brief The widest arguments an entry's caller passes, in the order of
 *         its conversions, where they differ from widest_of(). */
typedef struct {
    ui_text_id_t id;              /**< the entry */
    const char  *arg[ARGS_MAX];   /**< each conversion as it shows; NULL
                                       from the first one left to
                                       widest_of() */
} format_args_t;

/* A pin is GPIO 0 to 47, a PROG PORT 1 to 9, a file an 8.3 name of 12
 * cells, an output's role name 7 cells (outputs_role_name()'s buffer). */
static const format_args_t k_args[] = {
    /* LOGS' footer messages: the name of a log the bench wrote. */
    { TX_LOG_CANNOT_OPEN,   { "ATEST001.CSV" } },
    { TX_LOG_IS_FOLDER,     { "ATEST001.CSV" } },
    { TX_LOG_TOO_BIG,       { "ATEST001.CSV" } },
    { TX_LOG_CANNOT_DELETE, { "ATEST001.CSV" } },
    { TX_LOG_DELETED,       { "ATEST001.CSV" } },
    /* RUDDER AUTO: an error of "%+.2f°" at its widest; the longest phase
     * name, a pulse of 4 digits, SERVO_RAUTO_WRITES_MAX (8) writes and 99
     * sessions. */
    { TX_RA_VERIFY, { "+180.00\xc2\xb0", "-180.00\xc2\xb0",
                      "+180.00\xc2\xb0" } },
    { TX_RA_STEP,   { "SERVO AUSLESEN", "2000", "8", "8", "99" } },
    /* The password's least length, UI_TEXTKEY_PASS_MIN. */
    { TX_TEXTKEY_SHORT, { "63" } },
    /* Pins, ports and role names. */
    { TX_OUT_SLICE_OF,         { "47" } },
    { TX_OUT_COMPARE_OF,       { "47" } },
    { TX_OUT_OUT_SLICE_WHY,    { "WWWWWWW", "47", "WWWWWWW", "47" } },
    { TX_SV_TARGET_PN_PIN,     { "9", "47" } },
    { TX_SV_TARGET_PN_PIN_OFF, { "9", "47" } },
    { TX_SUP_PD_PINS,          { "47", "47" } },
    { TX_OUT_NEEDS_CHANNEL,    { "16", "16" } },
    { TX_OUT_NEEDS_CHANNELS,   { "16", "16" } },
    /* A frame rate of 3 digits on a port of 1. */
    { TX_SV_IN_FORCE_PORT, { "999", "9" } },
    /* Counts: a profile's steps, a maker's profiles, a page of files. */
    { TX_SP_WARN_HAND_READ, { "99" } },
    { TX_SP_MAKER_FOUND,    { "999", "999", "999" } },
    { TX_KP_FILES_TITLE_N,  { "999", "999", "999" } },
    /* A node's role and protocol, a byte each. */
    { TX_BNODE_ST_WRONG, { "255", "255" } },
    /* An input below 15V, in volts and tenths. */
    { TX_P5_IN_TAG, { "14", "9" } },
    /* The angle sensor's name: AS5600, the only one with a magnet and the
     * longest the run row names. */
    { TX_SV_ANG_RUN_NONE,   { "AS5600" } },
    { TX_RUD_SRC_NO_MAGNET, { "AS5600" } },
    /* A current limit as rcur_amps() writes it. */
    { TX_RDY_SEQ_ON_AT, { "10.00A" } },
    /* MOVE SERVO's notes: the longer end's name in either language, then
     * HITEC_LIVE_FIND_STEP, HITEC_LIVE_FIND_PASSES and HITEC_LIVE_FIND_MS
     * in seconds; a pulse of 4 digits and a share of 100%. */
    { TX_HP_FIND_RUN, { "ENDE RECHTS", "37", "2", "40" } },
    { TX_HP_LIVE_END, { "ENDE RECHTS", "2000", "2000", "100" } },
};

/** @brief The arguments listed for @p id, or NULL. */
static const format_args_t *args_of(ui_text_id_t id)
{
    for (size_t i = 0; i < sizeof(k_args) / sizeof(k_args[0]); ++i) {
        if (k_args[i].id == id) {
            return &k_args[i];
        }
    }
    return NULL;
}

/**
 * @brief The widest argument of a conversion, as it shows: a string of 8
 *        cells or its precision, an integer of 4 digits or its width, a
 *        long of 10 digits, hexadecimal of its width (4 without one), and
 *        a float as -180 with its decimals.
 * @param conv  The conversion's letter.
 * @param width Its field width, 0 for none.
 * @param prec  Its precision, -1 for none.
 * @param lng   It has the length l or ll.
 * @param out   Where the argument is written.
 * @param n     The size of @p out.
 * @return false for an argument longer than @p out.
 */
static bool widest_of(char conv, int width, int prec, bool lng, char *out,
                      size_t n)
{
    int cells;
    switch (conv) {
    case 's':
        cells = (prec >= 0) ? prec : 8;
        break;
    case 'd': case 'i': case 'u':
        cells = lng ? 10 : 4;
        break;
    case 'x': case 'X':
        cells = (width > 0) ? width : 4;
        break;
    case 'c':
        cells = 1;
        break;
    case 'f': {
        const int p = (prec >= 0) ? prec : 6;
        (void)snprintf(out, n, "-180%s%.*s", (p > 0) ? "." : "", p,
                       "000000000");
        return true;
    }
    default:
        cells = 1;
        break;
    }
    if (cells < width) {
        cells = width;
    }
    if ((size_t)cells >= n) {
        return false;
    }
    memset(out, (conv == 's' || conv == 'c') ? 'W' : '8', (size_t)cells);
    out[cells] = '\0';
    return true;
}

/**
 * @brief @p fmt as the panel shows it at its widest.
 * @param fmt  The format.
 * @param args The entry's listed arguments, or NULL.
 * @param out  Where the text is written.
 * @param n    The size of @p out.
 * @return The conversions formatted, or -1 for a text longer than @p out.
 *         A percent sign that starts no conversion is text.
 */
static int format_widest(const char *fmt, const format_args_t *args,
                         char *out, size_t n)
{
    size_t len = 0;
    int count = 0;
    bool listed = (args != NULL);
    for (const char *p = fmt; *p != '\0'; ++p) {
        char piece[40];
        if (*p != '%') {
            piece[0] = *p;
            piece[1] = '\0';
        } else if (p[1] == '%') {
            ++p;
            piece[0] = '%';
            piece[1] = '\0';
        } else {
            const char *const sign = p;
            ++p;
            while (*p != '\0' && strchr("-+ 0#", *p) != NULL) {
                ++p;
            }
            int width = 0, prec = -1;
            bool lng = false;
            for (; *p >= '0' && *p <= '9'; ++p) {
                width = width * 10 + (*p - '0');
            }
            if (*p == '.') {
                prec = 0;
                for (++p; *p >= '0' && *p <= '9'; ++p) {
                    prec = prec * 10 + (*p - '0');
                }
            }
            for (; *p == 'l' || *p == 'h' || *p == 'z'; ++p) {
                lng = lng || *p == 'l';
            }
            if (*p == '\0' || strchr("sdiuxXcf", *p) == NULL) {
                /* A percent sign of the text ("0 to 100%."), which no
                 * call formats. */
                p = sign;
                piece[0] = '%';
                piece[1] = '\0';
            } else {
                listed = listed && count < ARGS_MAX
                         && args->arg[count] != NULL;
                if (listed) {
                    (void)snprintf(piece, sizeof(piece), "%s",
                                   args->arg[count]);
                } else if (!widest_of(*p, width, prec, lng, piece,
                                      sizeof(piece))) {
                    return -1;
                }
                ++count;
            }
        }
        const size_t add = strlen(piece);
        if (len + add >= n) {
            return -1;
        }
        memcpy(out + len, piece, add);
        len += add;
    }
    out[len] = '\0';
    return count;
}

TEST_CASE(the_formatter_writes_each_conversion_at_its_widest)
{
    char b[96];
    CHECK_EQ(format_widest("GPIO %u, %s", NULL, b, sizeof(b)), 2);
    CHECK_STR_EQ(b, "GPIO 8888, WWWWWWWW");
    CHECK_EQ(format_widest("%02X %04X %lu %d%%", NULL, b, sizeof(b)), 4);
    CHECK_STR_EQ(b, "88 8888 8888888888 8888%");
    CHECK_EQ(format_widest("%.2fA %+.1f %.0f %-6s|", NULL, b, sizeof(b)), 4);
    CHECK_STR_EQ(b, "-180.00A -180.0 -180 WWWWWWWW|");
    CHECK_EQ(format_widest("plain", NULL, b, sizeof(b)), 0);
    CHECK_STR_EQ(b, "plain");
    /* Listed arguments in order, the rest at their widest. */
    const format_args_t one = { TX_COUNT, { "AB.CSV" } };
    CHECK_EQ(format_widest("%s is %u", &one, b, sizeof(b)), 2);
    CHECK_STR_EQ(b, "AB.CSV is 8888");
    /* A percent sign of the text, and a text longer than the buffer. */
    CHECK_EQ(format_widest("0 to 100%.", NULL, b, sizeof(b)), 0);
    CHECK_STR_EQ(b, "0 to 100%.");
    CHECK_EQ(format_widest("in 0.1%/ms, %u", NULL, b, sizeof(b)), 1);
    CHECK_STR_EQ(b, "in 0.1%/ms, 8888");
    CHECK_EQ(format_widest("%s%s", NULL, b, 12), -1);
}

/* Every language, English included: a format no screenshot shows is held
 * to the width ui_text.def gives its field with its conversions at their
 * widest. */
TEST_CASE(every_format_fits_its_declared_field_as_formatted)
{
    int seen = 0;
    for (int l = 0; l < UI_LANG_COUNT; ++l) {
        for (int i = 0; i < TX_COUNT; ++i) {
            const ui_text_id_t id = (ui_text_id_t)i;
            const int cells = ui_text_cells(id);
            const char *fmt = ui_tr_in((ui_lang_t)l, id);
            if (cells <= 0 || strchr(fmt, '%') == NULL) {
                continue;
            }
            char text[512];
            const int n = format_widest(fmt, args_of(id), text,
                                        sizeof(text));
            if (n < 0) {
                T_FAIL("%s in language %d is longer than 511 bytes: \"%s\"",
                       ui_text_name(id), l, fmt);
                continue;
            }
            seen += (n > 0) ? 1 : 0;
            if (gfx_text_cells(text) > cells) {
                T_FAIL("%s in language %d is %d cells as shown, the field "
                       "%d: \"%s\"", ui_text_name(id), l,
                       gfx_text_cells(text), cells, text);
            }
        }
    }
    CHECK(seen >= 600);
}

/* An entry of k_args that no longer has a declared width or a conversion
 * is measured by nothing here: it leaves the list. */
TEST_CASE(every_listed_argument_belongs_to_a_format_with_a_declared_field)
{
    for (size_t i = 0; i < sizeof(k_args) / sizeof(k_args[0]); ++i) {
        const ui_text_id_t id = k_args[i].id;
        int listed = 0;
        while (listed < ARGS_MAX && k_args[i].arg[listed] != NULL) {
            ++listed;
        }
        char text[512];
        const int n = format_widest(ui_tr_in(UI_LANG_EN, id), &k_args[i],
                                    text, sizeof(text));
        if (ui_text_cells(id) <= 0 || listed < 1 || n < listed) {
            T_FAIL("%s lists %d arguments, has %d conversions and %d cells",
                   ui_text_name(id), listed, n, ui_text_cells(id));
        }
    }
}

/* LOGS draws its footer message between WI-FI and DELETE on two rows of
 * the 12 x 24 face (log_viewer_draw.c).  The five that name a file carry
 * both rows as their width; with the name of a log the bench wrote, each
 * breaks into at most those two rows. */
TEST_CASE(a_logs_message_with_an_8_3_name_fits_the_footers_two_rows)
{
    static const ui_text_id_t k_ids[] = {
        TX_LOG_CANNOT_OPEN, TX_LOG_IS_FOLDER, TX_LOG_TOO_BIG,
        TX_LOG_CANNOT_DELETE, TX_LOG_DELETED,
    };
    const gfx_rect_t del = BR_DELETE;
    const gfx_rect_t two = gfx_rect_make(BR_MSG_X, BR_FOOT_Y,
                                         del.x - 8 - BR_MSG_X, BR_MSG_LH);
    const int row = two.w / gfx_text_width(LV_FONT_BODY, "W", 1);
    CHECK_EQ(row, 37);
    for (int l = 0; l < UI_LANG_COUNT; ++l) {
        for (size_t i = 0; i < sizeof(k_ids) / sizeof(k_ids[0]); ++i) {
            char text[160];
            CHECK_EQ(ui_text_cells(k_ids[i]), 2 * row);
            (void)format_widest(ui_tr_in((ui_lang_t)l, k_ids[i]),
                                args_of(k_ids[i]), text, sizeof(text));
            const int rows = ui_text_wrap(NULL, two, BR_MSG_LH, text,
                                          LV_FONT_BODY, 0);
            if (rows < 1 || rows > 2) {
                T_FAIL("%s in language %d takes %d rows: \"%s\"",
                       ui_text_name(k_ids[i]), l, rows, text);
            }
        }
    }
}

int main(void)
{
    RUN(the_formatter_writes_each_conversion_at_its_widest);
    RUN(every_format_fits_its_declared_field_as_formatted);
    RUN(every_listed_argument_belongs_to_a_format_with_a_declared_field);
    RUN(a_logs_message_with_an_8_3_name_fits_the_footers_two_rows);
    return test_summary("text_formats");
}
