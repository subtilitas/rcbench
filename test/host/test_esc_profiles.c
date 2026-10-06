/*
 * ESC (electronic speed controller) programming profiles: the generated
 * table, the parser for the card, and the registry that merges the two.
 *
 * The first case is the one that holds the design together: every profile
 * of record is parsed from its JSON and compared with the table the
 * generator compiled from the same file.  A rule or a field that the parser
 * and tools/gen_esc_profiles.py read differently fails there.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greatest.h"

#include "esc_profile.h"

#define PROFILE_DIR "../../shared/esc/profiles"

static char *load(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    char *buf = malloc(ESC_PROFILE_MAX_BYTES + 1u);
    *len = (buf != NULL) ? fread(buf, 1, ESC_PROFILE_MAX_BYTES + 1u, f) : 0u;
    fclose(f);
    return buf;
}

/* A profile small enough to read, with every field the parser looks at. */
static const char k_min[] =
    "{\"schema\": 1, \"id\": \"test-esc\", \"brand\": \"Test\",\n"
    " \"family\": \"Test 30\", \"verified\": false,\n"
    " \"automatable\": \"full\", \"automatable_note\": \"\",\n"
    " \"scheme\": {\"type\": \"short_long\",\n"
    "   \"entry\": {\"throttle\": \"max\", \"when\": \"before_power_on\",\n"
    "             \"hold_ms\": 7000, \"steps\": [\"Throttle max\", \"Power on\"]},\n"
    "   \"announce\": {\"what\": \"item\", \"encoding\": \"short_long\",\n"
    "                \"long_equals_short\": 5, \"beep_ms\": null, \"gap_ms\": null,\n"
    "                \"group_gap_ms\": null, \"repeat\": null},\n"
    "   \"select\": {\"throttle\": \"min\"}, \"skip\": {\"throttle\": \"none\"},\n"
    "   \"changes_per_entry\": \"many\"},\n"
    " \"models\": [{\"name\": \"Test 30A\", \"cells_min\": 2, \"cells_max\": 4,\n"
    "              \"cell_type\": \"lipo\", \"v_max_mv\": 16800, \"current_a\": 30},\n"
    "             {\"name\": \"Test 40A\", \"cells_min\": null, \"cells_max\": 12,\n"
    "              \"cell_type\": \"nimh\", \"v_max_mv\": null, \"current_a\": 40}],\n"
    " \"items\": [{\"number\": 1, \"name\": \"Brake\", \"key\": \"brake\",\n"
    "             \"applies_to\": null,\n"
    "             \"values\": [{\"number\": 1, \"name\": \"off\", \"default\": true},\n"
    "                        {\"number\": 2, \"name\": \"on\"}]},\n"
    "            {\"number\": 2, \"name\": \"Timing\", \"key\": \"timing\",\n"
    "             \"applies_to\": [\"Test 40A\"], \"applies_when\": \"heli\",\n"
    "             \"values\": [{\"number\": 0, \"name\": \"auto\"}]}]}\n";

/* k_min with the first @p from replaced by @p to. */
static char *subst(const char *from, const char *to)
{
    const char *at = strstr(k_min, from);
    if (at == NULL) {
        return NULL;
    }
    const size_t head = (size_t)(at - k_min);
    char *out = malloc(sizeof(k_min) + strlen(to));
    memcpy(out, k_min, head);
    strcpy(out + head, to);
    strcat(out, at + strlen(from));
    return out;
}

static bool parses(const char *json, char *err, size_t err_size)
{
    esc_profile_t p;
    void *block = NULL;
    const bool ok = esc_profile_parse(json, strlen(json), &p, &block, err,
                                      err_size);
    free(block);
    return ok;
}

/* Field by field; the first difference is named. */
static void same(const esc_profile_t *a, const esc_profile_t *b)
{
    CHECK_STR_EQ(a->id, b->id);
    CHECK_STR_EQ(a->brand, b->brand);
    CHECK_STR_EQ(a->family, b->family);
    CHECK_EQ(a->scheme, b->scheme);
    CHECK_EQ(a->encoding, b->encoding);
    CHECK_EQ(a->announce, b->announce);
    CHECK_EQ(a->automatable, b->automatable);
    CHECK_STR_EQ(a->automatable_note, b->automatable_note);
    CHECK_EQ(a->entry_throttle, b->entry_throttle);
    CHECK_EQ(a->entry_after_power, b->entry_after_power);
    CHECK_EQ(a->entry_hold_ms, b->entry_hold_ms);
    CHECK_EQ(a->select_throttle, b->select_throttle);
    CHECK_EQ(a->skip_throttle, b->skip_throttle);
    CHECK_EQ(a->long_equals_short, b->long_equals_short);
    CHECK_EQ(a->beep_ms, b->beep_ms);
    CHECK_EQ(a->gap_ms, b->gap_ms);
    CHECK_EQ(a->group_gap_ms, b->group_gap_ms);
    CHECK_EQ(a->repeat, b->repeat);
    CHECK_EQ(a->one_change_per_entry, b->one_change_per_entry);
    CHECK_EQ(a->verified, b->verified);
    CHECK_EQ(a->step_count, b->step_count);
    for (unsigned i = 0; i < a->step_count && i < b->step_count; ++i) {
        CHECK_STR_EQ(a->steps[i], b->steps[i]);
    }
    CHECK_EQ(a->model_count, b->model_count);
    for (unsigned i = 0; i < a->model_count && i < b->model_count; ++i) {
        const esc_model_t *x = &a->models[i], *y = &b->models[i];
        CHECK_STR_EQ(x->name, y->name);
        CHECK_EQ(x->cells_min, y->cells_min);
        CHECK_EQ(x->cells_max, y->cells_max);
        CHECK_EQ(x->nimh, y->nimh);
        CHECK_EQ(x->v_max_mv, y->v_max_mv);
        CHECK_EQ(x->current_a, y->current_a);
    }
    CHECK_EQ(a->item_count, b->item_count);
    for (unsigned i = 0; i < a->item_count && i < b->item_count; ++i) {
        const esc_item_t *x = &a->items[i], *y = &b->items[i];
        CHECK_STR_EQ(x->name, y->name);
        CHECK_STR_EQ(x->key, y->key);
        CHECK_EQ(x->number, y->number);
        CHECK_STR_EQ(x->applies_when, y->applies_when);
        CHECK_EQ(x->applies_count, y->applies_count);
        for (unsigned k = 0; k < x->applies_count && k < y->applies_count;
             ++k) {
            CHECK_STR_EQ(x->applies_to[k], y->applies_to[k]);
        }
        CHECK_EQ(x->value_count, y->value_count);
        for (unsigned k = 0; k < x->value_count && k < y->value_count; ++k) {
            CHECK_STR_EQ(x->values[k].name, y->values[k].name);
            CHECK_EQ(x->values[k].number, y->values[k].number);
            CHECK_EQ(x->values[k].is_default, y->values[k].is_default);
        }
    }
}

TEST_CASE(every_profile_of_record_parses_to_its_generated_table)
{
    CHECK(esc_profiles_builtin_count > 0u);
    for (size_t i = 0; i < esc_profiles_builtin_count; ++i) {
        const esc_profile_t *b = &esc_profiles_builtin[i];
        char path[256];
        (void)snprintf(path, sizeof(path), PROFILE_DIR "/%s.json", b->id);
        size_t len = 0;
        char *json = load(path, &len);
        if (json == NULL) {
            T_FAIL("cannot read %s", path);
            continue;
        }
        esc_profile_t p;
        void *block = NULL;
        char err[96];
        if (!esc_profile_parse(json, len, &p, &block, err, sizeof(err))) {
            T_FAIL("%s: %s", b->id, err);
        } else {
            same(&p, b);
        }
        free(block);
        free(json);
    }
}

TEST_CASE(builtin_ids_are_sorted_and_unique)
{
    for (size_t i = 1; i < esc_profiles_builtin_count; ++i) {
        if (strcmp(esc_profiles_builtin[i - 1].id,
                   esc_profiles_builtin[i].id) >= 0) {
            T_FAIL("%s before %s", esc_profiles_builtin[i - 1].id,
                   esc_profiles_builtin[i].id);
        }
    }
}

TEST_CASE(a_profile_reads_into_every_field)
{
    esc_profile_t p;
    void *block = NULL;
    char err[96];
    CHECK(esc_profile_parse(k_min, strlen(k_min), &p, &block, err,
                            sizeof(err)));
    CHECK_STR_EQ(p.id, "test-esc");
    CHECK_EQ(p.scheme, ESC_SCHEME_SHORT_LONG);
    CHECK_EQ(p.encoding, ESC_ENC_SHORT_LONG);
    CHECK_EQ(p.long_equals_short, 5);
    CHECK_EQ(p.entry_throttle, ESC_THR_MAX);
    CHECK_EQ(p.entry_after_power, false);
    CHECK_EQ(p.entry_hold_ms, 7000);
    CHECK_EQ(p.select_throttle, ESC_THR_MIN);
    CHECK_EQ(p.skip_throttle, ESC_THR_NONE);
    CHECK_EQ(p.repeat, -1);                     /* null: not known */
    CHECK_EQ(p.beep_ms, 0);
    CHECK_EQ(p.one_change_per_entry, false);
    CHECK_EQ(p.step_count, 2);
    CHECK_STR_EQ(p.steps[1], "Power on");
    CHECK_EQ(p.model_count, 2);
    CHECK_EQ(p.models[0].v_max_mv, 16800);
    CHECK_EQ(p.models[1].cells_min, 0);         /* null: not known */
    CHECK_EQ(p.models[1].nimh, true);
    CHECK_EQ(p.item_count, 2);
    CHECK_EQ(p.items[0].values[0].is_default, true);
    CHECK_EQ(p.items[0].values[1].is_default, false);
    CHECK_EQ(p.items[0].applies_count, 0);
    CHECK_STR_EQ(p.items[0].applies_when, "");
    CHECK_EQ(p.items[1].applies_count, 1);
    CHECK_STR_EQ(p.items[1].applies_to[0], "Test 40A");
    CHECK_STR_EQ(p.items[1].applies_when, "heli");
    CHECK_EQ(p.items[1].values[0].number, 0);
    free(block);
}

TEST_CASE(escapes_become_the_characters_they_name)
{
    char *j = subst("\"Test 30\"", "\"T\\u00fcst \\\"30\\\" \\/ \\u20ac\"");
    esc_profile_t p;
    void *block = NULL;
    CHECK(esc_profile_parse(j, strlen(j), &p, &block, NULL, 0));
    if (block != NULL) {
        CHECK_STR_EQ(p.family, "T\xC3\xBCst \"30\" / \xE2\x82\xAC");
    }
    free(block);
    free(j);
}

TEST_CASE(control_escapes_and_a_surrogate_pair_are_read)
{
    char *j = subst("\"Test 30\"",
                    "\"a\\b\\f\\n\\r\\t \\ud83d\\ude00 z\"");
    esc_profile_t p;
    void *block = NULL;
    CHECK(esc_profile_parse(j, strlen(j), &p, &block, NULL, 0));
    if (block != NULL) {
        /* The pair is one character, U+1F600, in four bytes of UTF-8. */
        CHECK_STR_EQ(p.family, "a\b\f\n\r\t \xF0\x9F\x98\x80 z");
    }
    free(block);
    free(j);
}

/* Keys and values are compared as they decode, as json.loads() has them:
 * an escaped spelling is the same key, the same value. */
TEST_CASE(escaped_keys_and_values_read_as_their_plain_spelling)
{
    char *j = subst("\"brand\": \"Test\"", "\"br\\u0061nd\": \"Test\"");
    esc_profile_t p;
    void *block = NULL;
    CHECK(esc_profile_parse(j, strlen(j), &p, &block, NULL, 0));
    if (block != NULL) {
        CHECK_STR_EQ(p.brand, "Test");
    }
    free(block);
    free(j);

    j = subst("\"throttle\": \"max\"", "\"throttle\": \"m\\u0061x\"");
    block = NULL;
    CHECK(esc_profile_parse(j, strlen(j), &p, &block, NULL, 0));
    if (block != NULL) {
        CHECK_EQ(p.entry_throttle, ESC_THR_MAX);
    }
    free(block);
    free(j);
}

/* Each mutation, and the start of the line the operator is shown. */
TEST_CASE(a_broken_profile_is_refused_with_its_place_named)
{
    static const struct {
        const char *from, *to, *err;
    } k[] = {
        { "\"schema\": 1", "\"schema\": 2", "schema: outside" },
        { "\"schema\": 1,", "", "schema: missing" },
        { "\"test-esc\"", "\"Test ESC\"", "id: not 1-48" },
        { "\"brand\": \"Test\"", "\"brand\": \"\"", "brand: empty" },
        { "\"verified\": false", "\"verified\": 0", "verified: not a boolean" },
        { "{\"number\": 1, \"name\": \"off\", \"default\": true}",
          "{\"number\": 1, \"name\": \"off\", \"default\": 1}",
          "items[0].values[0].default: not a boolean" },
        { "\"automatable\": \"full\"", "\"automatable\": \"assisted\"",
          "automatable_note: needed" },
        { "\"type\": \"short_long\"", "\"type\": \"morse\"",
          "scheme.type: not a known" },
        { "\"long_equals_short\": 5", "\"long_equals_short\": null",
          "scheme.announce.long_equals_short: needed" },
        { "\"throttle\": \"max\"", "\"throttle\": \"none\"",
          "scheme.entry.throttle: not a known" },
        { "\"hold_ms\": 7000", "\"hold_ms\": 7000.5",
          "scheme.entry.hold_ms: not a whole" },
        { "\"hold_ms\": 7000", "\"hold_ms\": -1",
          "scheme.entry.hold_ms: outside" },
        { "[\"Throttle max\", \"Power on\"]", "[]",
          "scheme.entry.steps: not 1-255" },
        { "\"changes_per_entry\": \"many\"", "\"changes_per_entry\": \"all\"",
          "scheme.changes_per_entry: not a known" },
        { "\"cell_type\": \"lipo\"", "\"cell_type\": \"lion\"",
          "models[0].cell_type: not a known" },
        { "\"current_a\": 30", "\"current_a\": 70000",
          "models[0].current_a: outside" },
        { "\"Test 40A\", \"cells_min\"", "\"Test 30A\", \"cells_min\"",
          "models[1].name: duplicate" },
        { "\"key\": \"brake\"", "\"key\": \"Brake\"", "items[0].key: not" },
        { "{\"number\": 1, \"name\": \"Brake\"",
          "{\"number\": 0, \"name\": \"Brake\"", "items[0].number: outside" },
        { "{\"number\": 2, \"name\": \"on\"}", "{\"number\": 1, \"name\": \"on\"}",
          "items[0].values[1].number: duplicate" },
        { "{\"number\": 2, \"name\": \"on\"}",
          "{\"number\": 2, \"name\": \"on\", \"default\": true}",
          "items[0].values: more than one default" },
        { "[{\"number\": 0, \"name\": \"auto\"}]", "[]",
          "items[1].values: not 1-255" },
        { "[\"Test 40A\"]", "[\"Test 50A\"]",
          "items[1].applies_to: not a model" },
        { "{\"number\": 2, \"name\": \"Timing\"",
          "{\"number\": 1, \"name\": \"Timing\"",
          "items: number 1 twice" },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        char *j = subst(k[i].from, k[i].to);
        if (j == NULL) {
            T_FAIL("case %u: \"%s\" not in the base profile", (unsigned)i,
                   k[i].from);
            continue;
        }
        char err[96] = "";
        if (parses(j, err, sizeof(err))) {
            T_FAIL("case %u accepted: %s", (unsigned)i, k[i].to);
        } else if (strncmp(err, k[i].err, strlen(k[i].err)) != 0) {
            T_FAIL("case %u: got \"%s\", want \"%s...\"", (unsigned)i, err,
                   k[i].err);
        }
        free(j);
    }
}

/* A card can hold a file cut off at any byte.  Every prefix is refused,
 * and none is read past its end (the sanitizer job sees to the second). */
TEST_CASE(every_truncation_is_refused)
{
    const size_t n = strlen(k_min);
    /* The text ends in "}\n"; cutting the newline alone leaves it whole. */
    for (size_t len = 0; len + 1u < n; ++len) {
        char *buf = malloc(len > 0u ? len : 1u);
        memcpy(buf, k_min, len);
        esc_profile_t p;
        void *block = NULL;
        if (esc_profile_parse(buf, len, &p, &block, NULL, 0)) {
            T_FAIL("a prefix of %u bytes was accepted", (unsigned)len);
        }
        CHECK(block == NULL);
        free(block);
        free(buf);
    }
}

TEST_CASE(text_that_is_not_json_is_refused)
{
    static const char *const k[] = {
        "",
        "   ",
        "[1, 2, 3]",
        "{\"schema\": 1} trailing",
        "{\"schema\": 1,}",
        "{\"schema\" 1}",
        "{\"a\": \"tab\there\"}",
        "{\"a\": \"bad \\q escape\"}",
        "{\"a\": \"\\u12G4\"}",
        "{\"a\": tru}",
        "{\"a\": -}",
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        char err[96] = "";
        if (parses(k[i], err, sizeof(err))) {
            T_FAIL("accepted: %s", k[i]);
        }
        CHECK(err[0] != '\0');
    }
}

TEST_CASE(nesting_past_the_limit_is_refused_not_followed)
{
    char deep[4096];
    size_t n = 0;
    for (int i = 0; i < 2000; ++i) {
        deep[n++] = '[';
    }
    deep[n] = '\0';
    CHECK(!parses(deep, NULL, 0));
}

TEST_CASE(a_file_over_the_limit_is_refused_before_it_is_read)
{
    char err[96] = "";
    esc_profile_t p;
    void *block = NULL;
    CHECK(!esc_profile_parse(k_min, ESC_PROFILE_MAX_BYTES + 1u, &p, &block,
                             err, sizeof(err)));
    CHECK(strstr(err, "larger than") != NULL);
    CHECK(!esc_profile_parse(NULL, 0, &p, &block, err, sizeof(err)));
}

TEST_CASE(a_card_file_is_named_after_its_id)
{
    CHECK(esc_profile_file_is("hobbywing-flyfun-8item.json",
                              "hobbywing-flyfun-8item"));
    CHECK(esc_profile_file_is("HOBBYWING-FLYFUN-8ITEM.JSON",
                              "hobbywing-flyfun-8item"));
    CHECK(!esc_profile_file_is("hobbywing-flyfun-8item copy.json",
                               "hobbywing-flyfun-8item"));
    CHECK(!esc_profile_file_is("hobbywing.json", "hobbywing-flyfun-8item"));
    CHECK(!esc_profile_file_is("hobbywing-flyfun-8item.jso",
                               "hobbywing-flyfun-8item"));
    CHECK(!esc_profile_file_is("hobbywing-flyfun-8item.txt",
                               "hobbywing-flyfun-8item"));
    CHECK(!esc_profile_file_is(NULL, "x"));
    CHECK(!esc_profile_file_is("x.json", NULL));
}

/* What the generator refuses, the card reader refuses: a key twice, a NUL
 * inside a string, a number JSON does not allow, even in a field the panel
 * never reads. */
TEST_CASE(input_the_generator_refuses_is_refused_here_too)
{
    static const struct {
        const char *from, *to, *err;
    } k[] = {
        { "\"schema\": 1,", "\"schema\": 1, \"schema\": 1,", "a key twice" },
        { "\"schema\": 1,", "\"schema\": 1, \"sch\\u0065ma\": 1,",
          "a key twice" },
        /* A raw character and its escape are one key, whatever the
         * byte count of each spelling. */
        { "\"schema\": 1,", "\"schema\": 1, \"\xC3\xA9\": 1, \"\\u00e9\": 1,",
          "a key twice" },
        { "\"schema\": 1,", "\"schema\": 1, \"\\u00e9\": 1, \"\xC3\xA9\": 1,",
          "a key twice" },
        { "\"Test 30\"", "\"Test\\u0000 30\"", "a string holds" },
        /* In a field the panel never reads: refused all the same. */
        { "\"schema\": 1,", "\"schema\": 1, \"notes\": [\"a\\u0000b\"],",
          "a string holds" },
        { "\"Test 30\"", "\"Test \\ud83d 30\"", "half a surrogate" },
        { "\"Test 30\"", "\"Test \\ude00 30\"", "half a surrogate" },
        { "\"Test 30\"", "\"Test \\ud83d\\u0041\"", "half a surrogate" },
        { "\"schema\": 1,", "\"schema\": 1, \"notes\": \"\\ud800\",",
          "half a surrogate" },
        { "\"schema\": 1,", "\"schema\": 1, \"n\": 1e,", "not JSON" },
        { "\"schema\": 1,", "\"schema\": 1, \"n\": 1+2,", "not JSON" },
        { "\"schema\": 1,", "\"schema\": 1, \"n\": 01,", "not JSON" },
        { "\"schema\": 1,", "\"schema\": 1, \"n\": 1.,", "not JSON" },
        { "\"schema\": 1,", "\"schema\": 1, \"n\": .5,", "not JSON" },
        { "\"schema\": 1,", "\"schema\": 1, \"n\": NaN,", "not JSON" },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        char *j = subst(k[i].from, k[i].to);
        char err[96] = "";
        if (j == NULL) {
            T_FAIL("case %u: not in the base profile", (unsigned)i);
            continue;
        }
        if (parses(j, err, sizeof(err))) {
            T_FAIL("case %u accepted: %s", (unsigned)i, k[i].to);
        } else if (strncmp(err, k[i].err, strlen(k[i].err)) != 0) {
            T_FAIL("case %u: got \"%s\", want \"%s...\"", (unsigned)i, err,
                   k[i].err);
        }
        free(j);
    }
    /* And what JSON does allow is still read. */
    char *j = subst("\"schema\": 1,",
                    "\"schema\": 1, \"n\": [-0, 0.5, 1e3, 2E-2, 10],");
    CHECK(parses(j, NULL, 0));
    free(j);
}

/* Raw bytes in a string are UTF-8 or the file is refused, as Python's
 * decoder refuses it before the generator sees a key. */
TEST_CASE(a_string_that_is_not_utf8_is_refused)
{
    static const char *const bad[] = {
        "\"T\xFF\"",                       /* never a UTF-8 byte      */
        "\"T\x80\"",                       /* a stray continuation    */
        "\"T\xC0\x80\"",                   /* overlong NUL            */
        "\"T\xE0\x80\xAF\"",               /* overlong '/'            */
        "\"T\xED\xA0\x80\"",               /* an encoded surrogate    */
        "\"T\xF4\x90\x80\x80\"",           /* past U+10FFFF           */
        "\"T\xE2\x82\"",                   /* cut short               */
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        char *j = subst("\"Test 30\"", bad[i]);
        char err[96] = "";
        if (parses(j, err, sizeof(err))) {
            T_FAIL("case %u accepted", (unsigned)i);
        } else if (strncmp(err, "not UTF-8", 9) != 0) {
            T_FAIL("case %u: got \"%s\"", (unsigned)i, err);
        }
        free(j);
    }
    /* Two-, three- and four-byte characters as they should be. */
    char *j = subst("\"Test 30\"",
                    "\"\xC3\xBC \xE2\x82\xAC \xF0\x9F\x98\x80\"");
    CHECK(parses(j, NULL, 0));
    free(j);
}

/* An object of 64 members is read; of 65, refused before the duplicate
 * check can cost anything. */
TEST_CASE(an_object_past_64_members_is_refused)
{
    for (int members = 64; members <= 65; ++members) {
        char obj[2048];
        size_t n = 0;
        n += (size_t)snprintf(obj + n, sizeof(obj) - n, "\"schema\": 1, \"x\": {");
        for (int k = 0; k < members; ++k) {
            n += (size_t)snprintf(obj + n, sizeof(obj) - n, "%s\"k%d\": %d",
                                  k ? ", " : "", k, k);
        }
        (void)snprintf(obj + n, sizeof(obj) - n, "},");
        char *j = subst("\"schema\": 1,", obj);
        char err[96] = "";
        const bool ok = parses(j, err, sizeof(err));
        if (members == 64) {
            CHECK(ok);
        } else {
            CHECK(!ok);
            CHECK(strncmp(err, "an object with more than 64", 27) == 0);
        }
        free(j);
    }
}

static bool add_card(const char *json)
{
    esc_profile_t p;
    void *block = NULL;
    if (!esc_profile_parse(json, strlen(json), &p, &block, NULL, 0)) {
        return false;
    }
    return esc_profiles_override(&p, block);
}

TEST_CASE(a_card_profile_takes_a_built_in_profiles_place)
{
    esc_profiles_clear_overrides();
    const esc_profile_t *first = &esc_profiles_builtin[0];
    char id[64];
    (void)snprintf(id, sizeof(id), "\"%s\"", first->id);
    char *j = subst("\"test-esc\"", id);
    CHECK(add_card(j));
    free(j);

    CHECK_EQ(esc_profiles_count(), esc_profiles_builtin_count);
    const esc_profile_t *at = esc_profiles_at(0);
    CHECK(at != first);
    CHECK(esc_profiles_is_override(at));
    CHECK_STR_EQ(at->family, "Test 30");
    CHECK(esc_profiles_find(first->id) == at);
    CHECK(!esc_profiles_is_override(esc_profiles_at(1)));

    esc_profiles_clear_overrides();
    CHECK(esc_profiles_at(0) == first);
}

TEST_CASE(a_card_profile_with_a_new_id_follows_the_built_in_ones)
{
    esc_profiles_clear_overrides();
    CHECK(add_card(k_min));
    CHECK_EQ(esc_profiles_count(), esc_profiles_builtin_count + 1u);
    const esc_profile_t *last = esc_profiles_at(esc_profiles_builtin_count);
    CHECK(last != NULL && strcmp(last->id, "test-esc") == 0);
    CHECK(esc_profiles_at(esc_profiles_builtin_count + 1u) == NULL);

    /* A second file with the same id replaces the first: still one. */
    char *j = subst("\"Test 30\"", "\"Test 30 corrected\"");
    CHECK(add_card(j));
    free(j);
    CHECK_EQ(esc_profiles_override_count(), 1);
    CHECK_STR_EQ(esc_profiles_find("test-esc")->family, "Test 30 corrected");
    CHECK(esc_profiles_find("no-such-esc") == NULL);
    CHECK(esc_profiles_find(NULL) == NULL);
    esc_profiles_clear_overrides();
}

TEST_CASE(the_registry_refuses_past_its_capacity)
{
    esc_profiles_clear_overrides();
    for (unsigned i = 0; i < ESC_PROFILE_MAX_OVERRIDES; ++i) {
        char id[32];
        (void)snprintf(id, sizeof(id), "\"card-%u\"", i);
        char *j = subst("\"test-esc\"", id);
        CHECK(add_card(j));
        free(j);
    }
    CHECK(!add_card(k_min));                    /* freed, not leaked */
    CHECK_EQ(esc_profiles_override_count(), ESC_PROFILE_MAX_OVERRIDES);
    CHECK(!esc_profiles_override(NULL, NULL));
    esc_profiles_clear_overrides();
    CHECK_EQ(esc_profiles_override_count(), 0);
}

int main(void)
{
    RUN(every_profile_of_record_parses_to_its_generated_table);
    RUN(builtin_ids_are_sorted_and_unique);
    RUN(a_profile_reads_into_every_field);
    RUN(escapes_become_the_characters_they_name);
    RUN(control_escapes_and_a_surrogate_pair_are_read);
    RUN(escaped_keys_and_values_read_as_their_plain_spelling);
    RUN(a_broken_profile_is_refused_with_its_place_named);
    RUN(every_truncation_is_refused);
    RUN(text_that_is_not_json_is_refused);
    RUN(nesting_past_the_limit_is_refused_not_followed);
    RUN(a_file_over_the_limit_is_refused_before_it_is_read);
    RUN(a_card_file_is_named_after_its_id);
    RUN(a_string_that_is_not_utf8_is_refused);
    RUN(an_object_past_64_members_is_refused);
    RUN(input_the_generator_refuses_is_refused_here_too);
    RUN(a_card_profile_takes_a_built_in_profiles_place);
    RUN(a_card_profile_with_a_new_id_follows_the_built_in_ones);
    RUN(the_registry_refuses_past_its_capacity);
    return test_summary("esc_profiles");
}
