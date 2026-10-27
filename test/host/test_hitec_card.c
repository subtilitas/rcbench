/*
 * The texts of the Hitec programmer's record and servo files
 * (shared/bench/hitec_card.c).  The record: a round trip of both series,
 * the text with no record, its longest length, a wrong check, a truncated
 * text, a wrong header, each line missing or wrong, a pin of no IOmcu and
 * the limits of the caller's buffer.  A servo file: a round trip of both
 * series, the model line of each kind of model, a text at its maximum
 * length, a wrong series, a hand-edited file (a changed digit, a wrong
 * check, a cut text, an extra line, a CR, a lower-case digit, a byte after
 * the check line) and the limits of the caller's buffer.
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

/* ------------------------------------------------------------ servo files */

TEST_CASE(a_d_servo_file_survives_its_text)
{
    hitec_image_t img, got;
    char text[HITEC_CARD_FILE_MAX];
    char model[HITEC_CARD_MODEL_MAX + 1u];
    uint8_t addrs[HITEC_CARD_FILE_ADDRS];
    const unsigned n = hitec_card_file_addrs(HITEC_D, addrs);
    unsigned line = 99;
    size_t len;

    d485(&img);
    len = hitec_card_file_text(&img, text, sizeof(text));
    /* 21 registers of 4 digits: 140 bytes. */
    CHECK_EQ(n, 21u);
    CHECK_EQ(len, 140u);
    CHECK_EQ(strlen(text), len);
    CHECK_EQ(memcmp(text, "RCBENCH HITEC 1\nseries=D\nmodel=D485HW\nimage=",
                    44u), 0);
    CHECK(strstr(text, "version=") == NULL);
    memset(&got, 0x5A, sizeof(got));
    CHECK_EQ(hitec_card_file_parse(text, len, &got, model, &line),
             HITEC_CARD_OK);
    CHECK_EQ(line, 0u);
    CHECK_STR_EQ(model, "D485HW");
    CHECK_EQ(got.series, HITEC_D);
    for (unsigned i = 0; i < n; ++i) {
        CHECK(hitec_image_known(&got, addrs[i]));
        CHECK_EQ(hitec_image_reg(&got, addrs[i]),
                 hitec_image_reg(&img, addrs[i]));
    }
    /* Only those: a register outside the read is not known. */
    CHECK(!hitec_image_known(&got, 0x70u));
    /* NULL for the model and the line is taken. */
    CHECK_EQ(hitec_card_file_parse(text, len, &got, NULL, NULL),
             HITEC_CARD_OK);
}

TEST_CASE(a_classic_servo_file_survives_its_text_with_its_version)
{
    hitec_image_t img, got;
    char text[HITEC_CARD_FILE_MAX];
    char model[HITEC_CARD_MODEL_MAX + 1u];
    uint8_t addrs[HITEC_CARD_FILE_ADDRS];
    const unsigned n = hitec_card_file_addrs(HITEC_CLASSIC, addrs);
    size_t len;

    hs5645(&img);
    len = hitec_card_file_text(&img, text, sizeof(text));
    /* 100 cells of 2 digits and the version line: 270 bytes. */
    CHECK_EQ(n, 100u);
    CHECK_EQ(addrs[0], 0x00u);
    CHECK_EQ(addrs[0x23], 0x23u);
    CHECK_EQ(addrs[0x24], 0x30u);
    CHECK_EQ(addrs[99], 0x87u);
    CHECK_EQ(len, 270u);
    CHECK(strstr(text, "model=HS-5645MG\n") != NULL);
    CHECK(strstr(text, "\nversion=16\ncheck=") != NULL);
    CHECK_EQ(hitec_card_file_parse(text, len, &got, model, NULL),
             HITEC_CARD_OK);
    CHECK_STR_EQ(model, "HS-5645MG");
    CHECK_EQ(got.series, HITEC_CLASSIC);
    CHECK_EQ(got.has_version, 1u);
    CHECK_EQ(got.version, 0x16u);
    for (unsigned i = 0; i < n; ++i) {
        CHECK_EQ(hitec_image_cell(&got, addrs[i]),
                 hitec_image_cell(&img, addrs[i]));
    }
    CHECK(!hitec_image_known(&got, 0x24u));
    /* A classic image with no version read has no version line. */
    img.has_version = 0u;
    len = hitec_card_file_text(&img, text, sizeof(text));
    CHECK_EQ(len, 259u);
    CHECK_EQ(hitec_card_file_parse(text, len, &got, NULL, NULL),
             HITEC_CARD_OK);
    CHECK_EQ(got.has_version, 0u);
}

TEST_CASE(the_model_line_names_each_kind_of_model)
{
    hitec_image_t img;
    char model[HITEC_CARD_MODEL_MAX + 1u];

    d485(&img);
    hitec_card_model_text(&img, model);
    CHECK_STR_EQ(model, "D485HW");
    (void)hitec_image_put_reg(&img, 0x00u, 34999u);
    hitec_card_model_text(&img, model);
    CHECK_STR_EQ(model, "D series 34999");
    (void)hitec_image_put_reg(&img, 0x00u, 0u);
    hitec_card_model_text(&img, model);
    CHECK_STR_EQ(model, "D series 0");
    (void)hitec_image_put_reg(&img, 0x00u, 65535u);
    hitec_card_model_text(&img, model);
    CHECK_STR_EQ(model, "D series 65535");
    hs7955(&img);
    hitec_card_model_text(&img, model);
    CHECK_STR_EQ(model, "HS-7955TG");
    /* Another classic name: its cells as read. */
    (void)hitec_image_put_cell(&img, 0x64u, 'X');
    hitec_card_model_text(&img, model);
    CHECK_STR_EQ(model, "HS79X5");
    /* No name: "?". */
    for (unsigned c = 0; c < HITEC_C_NAME_LEN; ++c) {
        (void)hitec_image_put_cell(&img, (uint8_t)(HITEC_C_NAME + c), 0u);
    }
    hitec_card_model_text(&img, model);
    CHECK_STR_EQ(model, "?");
    /* An image the name was not read of: "?". */
    hitec_image_init(&img, HITEC_CLASSIC);
    hitec_card_model_text(&img, model);
    CHECK_STR_EQ(model, "?");
}

TEST_CASE(a_servo_file_at_its_maximum_length_parses)
{
    hitec_image_t img, got;
    char text[HITEC_CARD_FILE_MAX + 32u];
    char body[HITEC_CARD_FILE_MAX + 32u];
    char model[HITEC_CARD_MODEL_MAX + 1u];
    const char *image;
    size_t len;

    /* The longest the bench writes: 16 printable name cells. */
    hs5645(&img);
    for (unsigned c = 0; c < HITEC_C_NAME_LEN; ++c) {
        (void)hitec_image_put_cell(&img, (uint8_t)(HITEC_C_NAME + c),
                                   (uint8_t)('A' + c));
    }
    len = hitec_card_file_text(&img, text, HITEC_CARD_FILE_MAX);
    CHECK_EQ(len, 277u);
    CHECK(len < HITEC_CARD_FILE_MAX);
    CHECK_EQ(hitec_card_file_parse(text, len, &got, model, NULL),
             HITEC_CARD_OK);
    CHECK_STR_EQ(model, "ABCDEFGHIJKLMNOP");
    /* The longest the parse takes: a model line of 20 characters. */
    image = strstr(text, "image=");
    snprintf(body, sizeof(body),
             "RCBENCH HITEC 1\nseries=C\nmodel=%s\n%.*s",
             "12345678901234567890", (int)(strstr(image, "check=") - image),
             image);
    len = with_check(body, text);
    CHECK_EQ(len, 281u);
    CHECK(len < HITEC_CARD_FILE_MAX);
    CHECK_EQ(hitec_card_file_parse(text, len, &got, model, NULL),
             HITEC_CARD_OK);
    CHECK_STR_EQ(model, "12345678901234567890");
    /* One more is a line not read. */
    snprintf(body, sizeof(body),
             "RCBENCH HITEC 1\nseries=C\nmodel=%s\n%.*s",
             "123456789012345678901", (int)(strstr(image, "check=") - image),
             image);
    len = with_check(body, text);
    CHECK_EQ(hitec_card_file_parse(text, len, &got, model, NULL),
             HITEC_CARD_ERR_FIELD);
}

/* The D485HW's file text in @p text; returns its length. */
static size_t d_file(char *text)
{
    hitec_image_t img;

    d485(&img);
    return hitec_card_file_text(&img, text, HITEC_CARD_FILE_MAX);
}

TEST_CASE(a_hand_edited_servo_file_is_refused_whole)
{
    char text[HITEC_CARD_FILE_MAX + 32u];
    char body[HITEC_CARD_FILE_MAX + 32u];
    hitec_image_t got;
    unsigned line = 0;
    size_t len = d_file(text);

    /* A digit of the image changed: the check fails. */
    text[60] = text[60] == '0' ? '1' : '0';
    CHECK_EQ(hitec_card_file_parse(text, len, &got, NULL, &line),
             HITEC_CARD_ERR_CHECK);
    CHECK_EQ(line, 0u);
    /* Cut anywhere. */
    len = d_file(text);
    for (size_t cut = 16u; cut < len; ++cut) {
        CHECK(hitec_card_file_parse(text, cut, &got, NULL, NULL)
              != HITEC_CARD_OK);
    }
    CHECK_EQ(hitec_card_file_parse(text, len - 1u, &got, NULL, NULL),
             HITEC_CARD_ERR_TRUNCATED);
    /* A byte after the check line. */
    text[len] = ' ';
    CHECK_EQ(hitec_card_file_parse(text, len + 1u, &got, NULL, NULL),
             HITEC_CARD_ERR_TRUNCATED);
    /* No servo file. */
    CHECK_EQ(hitec_card_file_parse("KST servo configuration, format 1\n",
                                   34u, &got, NULL, NULL),
             HITEC_CARD_ERR_HEADER);
    CHECK_EQ(hitec_card_file_parse(NULL, 10u, &got, NULL, NULL),
             HITEC_CARD_ERR_HEADER);
    /* Line ends of CR LF: the check line does not end the text. */
    snprintf(body, sizeof(body), "RCBENCH HITEC 1\r\nseries=D\r\n");
    len = with_check(body, text);
    CHECK(hitec_card_file_parse(text, len, &got, NULL, NULL)
          != HITEC_CARD_OK);
    /* Lines not the ones due, each with a check that holds. */
    {
        static const struct {
            const char *body;
            unsigned line;
        } k_bad[] = {
            { "series=X\n", 2u },
            { "series=D\nport=0\npin=0D\n", 3u },     /* a record */
            { "series=D\nmodel=\n", 3u },
            { "series=D\nmodel=\nimage=%s\n", 3u },
            { "series=D\nmodel=D\x01" "485\n", 3u },
            { "series=D\nmodel=D485HW\n", 4u },
            { "series=D\nmodel=D485HW\nimage=12\n", 4u },
            { "series=D\nmodel=D485HW\nimage=%s\nversion=16\n", 5u },
            { "series=C\nmodel=HS-5645MG\nimage=%s\nnote=1\n", 5u },
            { "series=C\nmodel=HS-5645MG\nimage=%s\nversion=16\nx=1\n",
              6u },
            { "series=D\nmodel=D485HW\nimage=%s\n", 4u },
        };
        static const char k_d[] =
            "0000000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000";
        char k_c[201];

        memset(k_c, '0', 200u);
        k_c[200] = '\0';
        for (size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); ++i) {
            char lines[HITEC_CARD_FILE_MAX];
            const bool c = strstr(k_bad[i].body, "series=C") != NULL;

            snprintf(lines, sizeof(lines), k_bad[i].body, c ? k_c : k_d);
            if (i == sizeof(k_bad) / sizeof(k_bad[0]) - 1u) {
                /* The last: a lower-case digit in the image. */
                char *at = strstr(lines, "image=") + 6;

                at[3] = 'a';
            }
            snprintf(body, sizeof(body), "RCBENCH HITEC 1\n%s", lines);
            len = with_check(body, text);
            line = 0;
            CHECK_EQ(hitec_card_file_parse(text, len, &got, NULL, &line),
                     HITEC_CARD_ERR_FIELD);
            CHECK_EQ(line, k_bad[i].line);
        }
        /* The same lines in order parse. */
        snprintf(body, sizeof(body),
                 "RCBENCH HITEC 1\nseries=D\nmodel=D485HW\nimage=%s\n", k_d);
        len = with_check(body, text);
        CHECK_EQ(hitec_card_file_parse(text, len, &got, NULL, NULL),
                 HITEC_CARD_OK);
    }
    /* A record's text is no servo file, and a servo file no record. */
    {
        hitec_card_record_t r;

        d_record(&r);
        len = hitec_card_text(&r, text, sizeof(text));
        CHECK_EQ(hitec_card_file_parse(text, len, &got, NULL, &line),
                 HITEC_CARD_ERR_FIELD);
        CHECK_EQ(line, 3u);
        len = d_file(text);
        CHECK_EQ(hitec_card_parse(text, len, &r), HITEC_CARD_ERR_FIELD);
    }
}

TEST_CASE(a_servo_file_text_needs_its_buffer_and_a_known_series)
{
    hitec_image_t img;
    char text[HITEC_CARD_FILE_MAX];

    d485(&img);
    CHECK_EQ(hitec_card_file_text(&img, text, HITEC_CARD_FILE_MAX - 1u), 0u);
    CHECK_EQ(hitec_card_file_text(&img, NULL, sizeof(text)), 0u);
    CHECK_EQ(hitec_card_file_text(NULL, text, sizeof(text)), 0u);
    img.series = 2u;
    CHECK_EQ(hitec_card_file_text(&img, text, sizeof(text)), 0u);
}

int main(void)
{
    RUN(a_d_record_survives_its_text);
    RUN(a_classic_record_survives_its_text);
    RUN(a_text_with_no_record_parses_as_none);
    RUN(a_wrong_check_a_truncated_text_and_a_wrong_header_are_refused);
    RUN(a_line_that_is_not_the_one_due_is_refused);
    RUN(the_text_needs_its_buffer_and_a_known_series_and_port);
    RUN(a_d_servo_file_survives_its_text);
    RUN(a_classic_servo_file_survives_its_text_with_its_version);
    RUN(the_model_line_names_each_kind_of_model);
    RUN(a_servo_file_at_its_maximum_length_parses);
    RUN(a_hand_edited_servo_file_is_refused_whole);
    RUN(a_servo_file_text_needs_its_buffer_and_a_known_series);
    return test_summary("hitec_card");
}
