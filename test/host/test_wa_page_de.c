/**
 * @file
 * @brief The Wi-Fi page in German with one German entry NULL.
 *
 * The failure under test: a text with no German that shows nothing on the
 * German page, where ui_text.def and docs/Language.md promise the English.
 * The page is built here from its source, with the German of P_CODE_NOTE
 * blanked through WA_PAGE_DE.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

/** @brief The table's German, NULL for P_CODE_NOTE. */
#define WA_PAGE_DE(id, de) (((id) == P_CODE_NOTE) ? NULL : (de))
/* The page's source itself, built with the seam above; its tables are
 * static and no other way reaches them. */
/* NOLINTNEXTLINE(bugprone-suspicious-include) */
#include "../../shared/webaccess/wa_page.c"

/** @brief A page written into one growing buffer. */
typedef struct {
    char  *buf; /**< The page so far, NUL-terminated */
    size_t len; /**< Bytes in buf */
    size_t cap; /**< Bytes buf holds */
} page_t;

/**
 * @brief Appends one piece of the page.
 *
 * @param ctx  The page_t.
 * @param data The piece.
 * @param n    Its length in bytes.
 * @return true: the piece is kept.
 */
static bool sink(void *ctx, const void *data, size_t n)
{
    page_t *p = ctx;
    if (p->len + n + 1u > p->cap) {
        p->cap = (p->len + n + 1u) * 2u;
        REQUIRE_REALLOC(p->buf, p->cap);
    }
    memcpy(p->buf + p->len, data, n);
    p->len += n;
    p->buf[p->len] = '\0';
    return true;
}

TEST_CASE(a_text_without_german_shows_its_english_on_the_german_page)
{
    static const wa_page_info_t de = { "rcbench-1A2B", "0.19.0", 0x3E0000u,
                                       true, NULL };
    CHECK(k_de[P_CODE_NOTE] == NULL);
    CHECK(k_de[P_CODE] != NULL);
    page_t p = { 0 };
    wa_out_t o;
    wa_out_init(&o, sink, &p);
    wa_page_code(&o, &de, false);
    CHECK(wa_out_flush(&o));
    CHECK(p.buf != NULL);
    if (p.buf != NULL) {
        /* The blanked entry in English, its neighbour still German. */
        CHECK(strstr(p.buf, k_en[P_CODE_NOTE]) != NULL);
        CHECK(strstr(p.buf, k_de[P_CODE]) != NULL);
        CHECK(strstr(p.buf, k_en[P_CODE]) == NULL);
        CHECK(strstr(p.buf, "lang=\"de\"") != NULL);
    }
    free(p.buf);
}

int main(void)
{
    RUN(a_text_without_german_shows_its_english_on_the_german_page);
    return test_summary("wa_page_de");
}
