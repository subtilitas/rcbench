/**
 * @file test_esc_profile_family.c
 * @brief The German family beside an ESC profile's family: the card reader
 *        takes family_de as the generator does (absent or null: none; 1 to
 *        120 bytes; escapes read), and refuses an empty or mistyped one.
 *
 * Cases:
 * - a_profile_without_family_de_reads_an_empty_one
 * - family_de_is_read_with_its_escaped_umlauts
 * - a_null_family_de_reads_as_none
 * - a_german_family_of_120_bytes_is_taken_and_121_refused
 * - an_empty_or_mistyped_german_family_is_refused
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "esc_profile.h"

/** @brief The smallest profile the reader takes; @c %s is where family_de goes. */
static const char k_head[] =
    "{\"schema\": 1, \"id\": \"test-esc\", \"brand\": \"Test\",\n"
    " \"family\": \"Test 30\", %s \"verified\": false,\n"
    " \"automatable\": \"full\", \"automatable_note\": \"\",\n"
    " \"scheme\": {\"type\": \"short_long\",\n"
    "   \"entry\": {\"throttle\": \"max\", \"when\": \"before_power_on\",\n"
    "             \"hold_ms\": 7000, \"steps\": [\"Throttle max\"]},\n"
    "   \"announce\": {\"what\": \"item\", \"encoding\": \"short_long\",\n"
    "                \"long_equals_short\": 5, \"beep_ms\": null,\n"
    "                \"gap_ms\": null, \"group_gap_ms\": null,\n"
    "                \"repeat\": null},\n"
    "   \"select\": {\"throttle\": \"min\"}, \"skip\": {\"throttle\": \"none\"},\n"
    "   \"changes_per_entry\": \"many\"},\n"
    " \"models\": [{\"name\": \"Test 30A\", \"cells_min\": 2, \"cells_max\": 4,\n"
    "              \"cell_type\": \"lipo\", \"v_max_mv\": 16800,\n"
    "              \"current_a\": 30, \"v_min_mv\": 6000}],\n"
    " \"items\": [{\"number\": 1, \"name\": \"Brake\", \"key\": \"brake\",\n"
    "             \"applies_to\": null,\n"
    "             \"values\": [{\"number\": 1, \"name\": \"off\"}]}]}\n";

/**
 * @brief Parse the profile with @p member (a family_de member or "") in it.
 * @param member The JSON text put after "family".
 * @param p      Receives the profile.
 * @param block  Receives the block @p p lives in; the caller frees it.
 * @param err    Receives the reason on a refusal.
 * @param n      Size of @p err.
 * @return true when the reader took it.
 */
static bool read_with(const char *member, esc_profile_t *p, void **block,
                      char *err, size_t n)
{
    char *j = malloc(sizeof(k_head) + strlen(member) + 1u);
    REQUIRE_ALLOC(j);
    (void)snprintf(j, sizeof(k_head) + strlen(member), k_head, member);
    *block = NULL;
    const bool ok = esc_profile_parse(j, strlen(j), p, block, err, n);
    free(j);
    return ok;
}

TEST_CASE(a_profile_without_family_de_reads_an_empty_one)
{
    esc_profile_t p;
    void *block;
    CHECK(read_with("", &p, &block, NULL, 0u));
    if (block != NULL) {
        CHECK_STR_EQ(p.family, "Test 30");
        CHECK_STR_EQ(p.family_de, "");
    }
    free(block);
}

TEST_CASE(family_de_is_read_with_its_escaped_umlauts)
{
    esc_profile_t p;
    void *block;
    CHECK(read_with("\"family_de\": \"Z\\u00e4hlmen\\u00fc mit 8 Punkten\",",
                    &p, &block, NULL, 0u));
    if (block != NULL) {
        CHECK_STR_EQ(p.family, "Test 30");
        CHECK_STR_EQ(p.family_de, "Z\xC3\xA4hlmen\xC3\xBC mit 8 Punkten");
    }
    free(block);
}

TEST_CASE(a_null_family_de_reads_as_none)
{
    esc_profile_t p;
    void *block;
    CHECK(read_with("\"family_de\": null,", &p, &block, NULL, 0u));
    if (block != NULL) {
        CHECK_STR_EQ(p.family_de, "");
    }
    free(block);
}

TEST_CASE(a_german_family_of_120_bytes_is_taken_and_121_refused)
{
    for (int over = 0; over < 2; ++over) {
        char member[256];
        char de[160];
        (void)memset(de, 'x', (size_t)120 + (size_t)over);
        de[120 + over] = '\0';
        (void)snprintf(member, sizeof(member), "\"family_de\": \"%s\",", de);
        esc_profile_t p;
        void *block;
        char err[96] = "";
        CHECK_EQ(read_with(member, &p, &block, err, sizeof(err)), over == 0);
        if (over == 0) {
            CHECK_EQ(strlen(p.family_de), 120u);
        } else {
            CHECK_STR_EQ(err, "family_de: longer than 120 bytes");
        }
        free(block);
    }
    /* Escapes count as the bytes they decode to: 60 umlauts are 120 bytes,
     * 61 are 122. */
    for (int n = 60; n <= 61; ++n) {
        char member[512] = "\"family_de\": \"";
        for (int i = 0; i < n; ++i) {
            (void)strcat(member, "\\u00fc");
        }
        (void)strcat(member, "\",");
        esc_profile_t p;
        void *block;
        CHECK_EQ(read_with(member, &p, &block, NULL, 0u), n == 60);
        free(block);
    }
}

TEST_CASE(an_empty_or_mistyped_german_family_is_refused)
{
    static const struct { const char *member, *why; } k[] = {
        { "\"family_de\": \"\",", "family_de: empty" },
        { "\"family_de\": 1,", "family_de: not a string" },
        { "\"family_de\": true,", "family_de: not a string" },
        { "\"family_de\": [\"x\"],", "family_de: not a string" },
        { "\"family_de\": {},", "family_de: not a string" },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        esc_profile_t p;
        void *block;
        char err[96] = "";
        CHECK(!read_with(k[i].member, &p, &block, err, sizeof(err)));
        CHECK_STR_EQ(err, k[i].why);
        free(block);
    }
}

int main(void)
{
    RUN(a_profile_without_family_de_reads_an_empty_one);
    RUN(family_de_is_read_with_its_escaped_umlauts);
    RUN(a_null_family_de_reads_as_none);
    RUN(a_german_family_of_120_bytes_is_taken_and_121_refused);
    RUN(an_empty_or_mistyped_german_family_is_refused);
    return test_summary("esc_profile_family");
}
