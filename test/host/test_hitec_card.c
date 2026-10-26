/*
 * The text of the Hitec programmer's record (shared/bench/hitec_card.c):
 * a round trip of both series, the text with no record, its longest
 * length, a wrong check, a truncated text, a wrong header, each line
 * missing or wrong, a pin of no IOmcu and the limits of the caller's
 * buffer.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "hitec_card.h"
#include "link_crc.h"
#include "hitec_reg_fix.h"

/* A D485HW record: speed 100% -> 50%. */
static void d_record(hitec_card_record_t *r)
{
    memset(r, 0, sizeof(*r));
    r->series = HITEC_D;
    r->port = 1u;
    r->pin = 13u;
    d485(&r->before);
    r->target = r->before;
    CHECK_EQ(hitec_param_set(&r->target, HITEC_LAYOUT_D, HITEC_P_D_SPEED, 50),
             HITEC_SET_OK);
}

/* An HS-5645MG record: speed 100% -> 50%. */
static void c_record(hitec_card_record_t *r)
{
    memset(r, 0, sizeof(*r));
    r->series = HITEC_CLASSIC;
    r->port = 0u;
    r->pin = 47u;
    hs5645(&r->before);
    r->target = r->before;
    CHECK_EQ(hitec_param_set(&r->target, HITEC_LAYOUT_HS5, HITEC_P_C_SPEED,
                             50), HITEC_SET_OK);
}

TEST_CASE(a_d_record_survives_its_text)
{
    hitec_card_record_t r, got;
    char text[HITEC_CARD_TEXT_MAX];
    uint8_t addrs[HITEC_C_BLOCK_LEN];
    const unsigned n = hitec_card_addrs(HITEC_D, addrs);
    const size_t len = hitec_card_text((d_record(&r), &r), text, sizeof(text));

    /* The longest text: 234 bytes and the terminator. */
    CHECK_EQ(len, 234u);
    CHECK_EQ(strlen(text), len);
    CHECK_EQ(memcmp(text, "RCBENCH HITEC 1\nseries=D\nport=1\npin=0D\n", 39),
             0);
    CHECK_EQ(hitec_card_parse(text, len, &got), HITEC_CARD_OK);
    CHECK_EQ(got.series, HITEC_D);
    CHECK_EQ(got.port, 1u);
    CHECK_EQ(got.pin, 13u);
    CHECK_EQ(n, 21u);
    for (unsigned i = 0; i < n; ++i) {
        CHECK(hitec_image_known(&got.before, addrs[i]));
        CHECK_EQ(hitec_image_reg(&got.before, addrs[i]),
                 hitec_image_reg(&r.before, addrs[i]));
        CHECK_EQ(hitec_image_reg(&got.target, addrs[i]),
                 hitec_image_reg(&r.target, addrs[i]));
    }
    /* Only those: a register outside the read is not known. */
    CHECK(!hitec_image_known(&got.before, 0x70u));
    CHECK_EQ(hitec_image_reg(&got.target, 0x54u), 10u);
}

TEST_CASE(a_classic_record_survives_its_text)
{
    hitec_card_record_t r, got;
    char text[HITEC_CARD_TEXT_MAX];
    size_t len;

    c_record(&r);
    len = hitec_card_text(&r, text, sizeof(text));
    CHECK_EQ(len, 210u);
    CHECK_EQ(hitec_card_parse(text, len, &got), HITEC_CARD_OK);
    CHECK_EQ(got.series, HITEC_CLASSIC);
    CHECK_EQ(got.pin, 47u);
    for (unsigned c = 0; c < HITEC_C_BLOCK_LEN; ++c) {
        CHECK_EQ(hitec_image_cell(&got.before, (uint8_t)c),
                 hitec_image_cell(&r.before, (uint8_t)c));
        CHECK_EQ(hitec_image_cell(&got.target, (uint8_t)c),
                 hitec_image_cell(&r.target, (uint8_t)c));
    }
    CHECK(!hitec_image_known(&got.before, HITEC_C_BLOCK_LEN));
    CHECK_EQ(hitec_c_eeprom_sum(&got.target), 0u);
}

TEST_CASE(a_text_with_no_record_parses_as_none)
{
    hitec_card_record_t got;
    char text[HITEC_CARD_TEXT_MAX];
    const size_t len = hitec_card_text(NULL, text, sizeof(text));

    memset(&got, 0x5A, sizeof(got));
    CHECK_EQ(len, 27u);
    CHECK_EQ(hitec_card_parse(text, len, &got), HITEC_CARD_NONE);
    /* Untouched. */
    CHECK_EQ(got.series, 0x5Au);
}

TEST_CASE(a_wrong_check_a_truncated_text_and_a_wrong_header_are_refused)
{
    hitec_card_record_t r, got;
    char text[HITEC_CARD_TEXT_MAX];
    size_t len;

    d_record(&r);
    len = hitec_card_text(&r, text, sizeof(text));
    /* One digit of the image changed. */
    text[60] = text[60] == '0' ? '1' : '0';
    CHECK_EQ(hitec_card_parse(text, len, &got), HITEC_CARD_ERR_CHECK);
    len = hitec_card_text(&r, text, sizeof(text));
    /* The check itself changed. */
    text[len - 2u] = text[len - 2u] == '0' ? '1' : '0';
    CHECK_EQ(hitec_card_parse(text, len, &got), HITEC_CARD_ERR_CHECK);
    len = hitec_card_text(&r, text, sizeof(text));
    /* Cut anywhere: no check line at the end. */
    for (size_t cut = 16u; cut < len; cut += 7u) {
        CHECK(hitec_card_parse(text, cut, &got) != HITEC_CARD_OK);
    }
    CHECK_EQ(hitec_card_parse(text, len - 1u, &got),
             HITEC_CARD_ERR_TRUNCATED);
    CHECK_EQ(hitec_card_parse(text, 100u, &got), HITEC_CARD_ERR_TRUNCATED);
    CHECK_EQ(hitec_card_parse(text, 16u, &got), HITEC_CARD_ERR_TRUNCATED);
    CHECK_EQ(hitec_card_parse(text, 5u, &got), HITEC_CARD_ERR_HEADER);
    CHECK_EQ(hitec_card_parse(NULL, len, &got), HITEC_CARD_ERR_HEADER);
    text[8] = 'K';
    CHECK_EQ(hitec_card_parse(text, len, &got), HITEC_CARD_ERR_HEADER);
    /* A lower-case digit is no digit. */
    CHECK_EQ(hitec_card_parse("RCBENCH HITEC 1\ncheck=abcd\n", 27u, &got),
             HITEC_CARD_ERR_CHECK);
}

/* @p body with its check line, as the text would be. */
static size_t with_check(const char *body, char *out)
{
    static const char k_hex[] = "0123456789ABCDEF";
    const size_t n = strlen(body);
    const uint16_t crc = link_crc(LINK_CRC_INIT, body, n);

    memcpy(out, body, n);
    memcpy(out + n, "check=", 6u);
    for (unsigned i = 0; i < 4u; ++i) {
        out[n + 6u + i] = k_hex[(crc >> (12u - 4u * i)) & 0xFu];
    }
    out[n + 10u] = '\n';
    out[n + 11u] = '\0';
    return n + 11u;
}

TEST_CASE(a_line_that_is_not_the_one_due_is_refused)
{
    static const char k_c36[] =
        "000000000000000000000000000000000000000000000000000000000000000000000000";
    char body[2u * HITEC_CARD_TEXT_MAX];
    char text[2u * HITEC_CARD_TEXT_MAX + 16u];
    hitec_card_record_t got;
    static const char *const k_bad[] = {
        "series=X\nport=0\npin=0D\nbefore=%s\ntarget=%s\n",
        "series=C\nport=G\npin=0D\nbefore=%s\ntarget=%s\n",
        "series=C\nport=0\npin=0\nbefore=%s\ntarget=%s\n",
        "series=C\nport=0\npin=0D\nbefore=%.70s\ntarget=%s\n",
        "series=C\nport=0\npin=0D\nbefore=%s\ntarget=%s0\n",
        "series=C\nport=0\npin=0D\ntarget=%s\nbefore=%s\n",
        "series=C\nport=0\npin=0D\nbefore=%s\ntarget=%s\nmore=1\n",
        "series=C\nport=0\nbefore=%s\ntarget=%s\n",
        "series=C\n",
    };
    size_t len;

    for (size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); ++i) {
        char line[HITEC_CARD_TEXT_MAX];

        snprintf(line, sizeof(line), k_bad[i], k_c36, k_c36);
        snprintf(body, sizeof(body), "RCBENCH HITEC 1\n%s", line);
        len = with_check(body, text);
        CHECK_EQ(hitec_card_parse(text, len, &got), HITEC_CARD_ERR_FIELD);
    }
    /* The same lines in order parse, and a pin of no IOmcu reads as not
     * known. */
    snprintf(body, sizeof(body),
             "RCBENCH HITEC 1\nseries=C\nport=F\npin=30\nbefore=%s\n"
             "target=%s\n", k_c36, k_c36);
    len = with_check(body, text);
    CHECK_EQ(hitec_card_parse(text, len, &got), HITEC_CARD_OK);
    CHECK_EQ(got.port, 15u);
    CHECK_EQ(got.pin, HITEC_CARD_NO_PIN);
}

TEST_CASE(the_text_needs_its_buffer_and_a_known_series_and_port)
{
    hitec_card_record_t r;
    char text[HITEC_CARD_TEXT_MAX];

    d_record(&r);
    CHECK_EQ(hitec_card_text(&r, text, HITEC_CARD_TEXT_MAX - 1u), 0u);
    CHECK_EQ(hitec_card_text(&r, NULL, sizeof(text)), 0u);
    r.port = 16u;
    CHECK_EQ(hitec_card_text(&r, text, sizeof(text)), 0u);
    r.port = 15u;
    r.series = 2u;
    CHECK_EQ(hitec_card_text(&r, text, sizeof(text)), 0u);
    r.series = HITEC_D;
    r.pin = HITEC_CARD_NO_PIN;
    CHECK(hitec_card_text(&r, text, sizeof(text)) != 0u);
    CHECK(strstr(text, "pin=FF\n") != NULL);
}

int main(void)
{
    RUN(a_d_record_survives_its_text);
    RUN(a_classic_record_survives_its_text);
    RUN(a_text_with_no_record_parses_as_none);
    RUN(a_wrong_check_a_truncated_text_and_a_wrong_header_are_refused);
    RUN(a_line_that_is_not_the_one_due_is_refused);
    RUN(the_text_needs_its_buffer_and_a_known_series_and_port);
    return test_summary("hitec_card");
}
