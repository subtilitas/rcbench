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

/* Set by test/host/CMakeLists.txt to an absolute path; this one holds when
 * the suite is built some other way and run from test/host. */
#ifndef PROFILE_DIR
#define PROFILE_DIR "../../shared/esc/profiles"
#endif

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
    CHECK_EQ(a->select_within_ms, b->select_within_ms);
    CHECK_EQ(a->value_select_throttle, b->value_select_throttle);
    CHECK_EQ(a->value_select_within_ms, b->value_select_within_ms);
    CHECK_EQ(a->skip_throttle, b->skip_throttle);
    CHECK_EQ(a->listen_throttle, b->listen_throttle);
    CHECK_EQ(a->store_throttle, b->store_throttle);
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
            CHECK_EQ(x->values[k].entry_throttle,
                     y->values[k].entry_throttle);
            CHECK_EQ(x->values[k].entry_hold_ms,
                     y->values[k].entry_hold_ms);
            CHECK_EQ(x->values[k].after_count, y->values[k].after_count);
            for (unsigned m = 0; m < x->values[k].after_count
                                 && m < ESC_AFTER_MAX; ++m) {
                CHECK_EQ(x->values[k].after[m], y->values[k].after[m]);
            }
        }
    }
    CHECK_EQ(a->manual_count, b->manual_count);
    CHECK_EQ(a->manual == NULL, a->manual_count == 0u);
    for (unsigned i = 0; i < a->manual_count && i < b->manual_count; ++i) {
        CHECK_EQ(a->manual[i].when, b->manual[i].when);
        CHECK_STR_EQ(a->manual[i].action, b->manual[i].action);
        CHECK_EQ(a->manual[i].hold_ms, b->manual[i].hold_ms);
    }
}

/* k_min as an assisted profile with the manual steps @p steps. */
static char *with_manual(const char *steps)
{
    char to[1024];
    (void)snprintf(to, sizeof(to),
                   "\"automatable\": \"assisted\", \"automatable_note\": "
                   "\"a jumper\", \"manual\": %s", steps);
    return subst("\"automatable\": \"full\", \"automatable_note\": \"\"", to);
}

/* The steps a person does at the ESC, read in order with their times. */
TEST_CASE(a_profile_reads_its_manual_steps)
{
    esc_profile_t p;
    void *block = NULL;
    CHECK(esc_profile_parse(k_min, strlen(k_min), &p, &block, NULL, 0));
    if (block != NULL) {
        CHECK_EQ(p.manual_count, 0u);
        CHECK(p.manual == NULL);
        CHECK_EQ(esc_profile_manual_count(&p, ESC_MANUAL_BEFORE_POWER), 0u);
    }
    free(block);
    char *j = with_manual(
        "[{\"when\": \"before_power\", \"action\": \"Fit the jumper.\"},"
        " {\"when\": \"at_power_up\", \"action\": \"Hold SET.\","
        "  \"hold_ms\": 3000, \"source\": \"p. 5\"},"
        " {\"when\": \"before_menu\", \"action\": \"Pull the jumper.\"},"
        " {\"when\": \"after_programming\", \"action\": \"T\\u00fcr zu.\","
        "  \"hold_ms\": null}]");
    block = NULL;
    char err[96] = "";
    CHECK(esc_profile_parse(j, strlen(j), &p, &block, err, sizeof(err)));
    if (block != NULL) {
        CHECK_EQ(p.manual_count, 4u);
        CHECK_EQ(p.manual[0].when, ESC_MANUAL_BEFORE_POWER);
        CHECK_STR_EQ(p.manual[0].action, "Fit the jumper.");
        CHECK_EQ(p.manual[0].hold_ms, 0u);
        CHECK_EQ(p.manual[1].when, ESC_MANUAL_AT_POWER_UP);
        CHECK_EQ(p.manual[1].hold_ms, 3000u);
        CHECK_EQ(p.manual[2].when, ESC_MANUAL_BEFORE_MENU);
        CHECK_EQ(p.manual[3].when, ESC_MANUAL_AFTER_PROGRAMMING);
        CHECK_STR_EQ(p.manual[3].action, "T\xC3\xBCr zu.");
        CHECK_EQ(esc_profile_manual_count(&p, ESC_MANUAL_BEFORE_MENU), 1u);
        CHECK_EQ(esc_profile_manual_count(&p, ESC_MANUAL_DURING_MENU), 0u);
    }
    free(block);
    free(j);
    CHECK_EQ(esc_profile_manual_count(NULL, ESC_MANUAL_BEFORE_POWER), 0u);

    /* Null is none, on any profile. */
    j = subst("\"automatable\": \"full\",",
              "\"automatable\": \"full\", \"manual\": null,");
    CHECK(parses(j, NULL, 0));
    free(j);
}

/* A value programmed from another stick position than the entry's, as
 * Kontronik's car modes are from the middle; the generator's self-test
 * holds it to the same spellings. */
TEST_CASE(a_value_reads_the_stick_position_it_is_set_from)
{
    static const char from[] = "{\"number\": 2, \"name\": \"on\"}";
    static const struct {
        const char *to, *err;
        esc_throttle_t et;
    } k[] = {
        { "{\"number\": 2, \"name\": \"on\", \"entry_throttle\": \"mid\"}",
          NULL, ESC_THR_MID },
        { "{\"number\": 2, \"name\": \"on\", \"entry_throttle\": null}",
          NULL, ESC_THR_NONE },
        { "{\"number\": 2, \"name\": \"on\", \"entry_throttle\": \"none\"}",
          "items[0].values[1].entry_throttle: not a known value",
          ESC_THR_NONE },
        { "{\"number\": 2, \"name\": \"on\", \"entry_throttle\": \"MID\"}",
          "items[0].values[1].entry_throttle: not a known value",
          ESC_THR_NONE },
        { "{\"number\": 2, \"name\": \"on\", \"entry_throttle\": false}",
          "items[0].values[1].entry_throttle: not a known value",
          ESC_THR_NONE },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        char *j = subst(from, k[i].to);
        esc_profile_t p;
        void *block = NULL;
        char err[96] = "";
        const bool ok = esc_profile_parse(j, strlen(j), &p, &block, err,
                                          sizeof(err));
        if (k[i].err == NULL) {
            CHECK(ok);
            if (ok) {
                CHECK_EQ(p.items[0].values[1].entry_throttle, k[i].et);
                CHECK_EQ(p.items[0].values[0].entry_throttle, ESC_THR_NONE);
            }
        } else {
            CHECK(!ok);
            CHECK_STR_EQ(err, k[i].err);
        }
        free(block);
        free(j);
    }
}

/* A value's own entry time, as Kontronik SUN PLUS waits 5 s for modes 4
 * to 6 and 2 s for the rest; the generator holds it to the same range. */
TEST_CASE(a_value_reads_its_own_entry_time)
{
    static const char from[] = "{\"number\": 2, \"name\": \"on\"}";
    static const struct {
        const char *to, *err;
        uint32_t ms;
    } k[] = {
        { "{\"number\": 2, \"name\": \"on\", \"entry_hold_ms\": 5000}",
          NULL, 5000u },
        { "{\"number\": 2, \"name\": \"on\", \"entry_hold_ms\": null}",
          NULL, 0u },
        { "{\"number\": 2, \"name\": \"on\", \"entry_hold_ms\": 600000}",
          NULL, 600000u },
        { "{\"number\": 2, \"name\": \"on\", \"entry_hold_ms\": 600001}",
          "items[0].values[1].entry_hold_ms: outside", 0u },
        { "{\"number\": 2, \"name\": \"on\", \"entry_hold_ms\": -1}",
          "items[0].values[1].entry_hold_ms: outside", 0u },
        { "{\"number\": 2, \"name\": \"on\", \"entry_hold_ms\": 2.5}",
          "items[0].values[1].entry_hold_ms: not a whole", 0u },
        { "{\"number\": 2, \"name\": \"on\", \"entry_hold_ms\": \"5000\"}",
          "items[0].values[1].entry_hold_ms: not a whole", 0u },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        char *j = subst(from, k[i].to);
        esc_profile_t p;
        void *block = NULL;
        char err[96] = "";
        const bool ok = esc_profile_parse(j, strlen(j), &p, &block, err,
                                          sizeof(err));
        if (k[i].err == NULL) {
            CHECK(ok);
            if (ok) {
                CHECK_EQ(p.items[0].values[1].entry_hold_ms, k[i].ms);
                CHECK_EQ(p.items[0].values[0].entry_hold_ms, 0u);
            }
        } else {
            CHECK(!ok);
            if (strncmp(err, k[i].err, strlen(k[i].err)) != 0) {
                T_FAIL("case %u: got \"%s\", want \"%s...\"", (unsigned)i,
                       err, k[i].err);
            }
        }
        free(block);
        free(j);
    }
}

/* The moves a value asks for after its selection, read in order; the
 * generator's self-test holds it to the same spellings. */
TEST_CASE(a_value_reads_its_moves_after_the_selection)
{
    static const char from[] = "{\"number\": 2, \"name\": \"on\"}";
    static const char pre[] = "{\"number\": 2, \"name\": \"on\", "
                              "\"after_select\": ";
    static const struct {
        const char *moves, *err;
        uint8_t n;
        esc_throttle_t first, last;
    } k[] = {
        { "[\"min\"]", NULL, 1u, ESC_THR_MIN, ESC_THR_MIN },
        { "[\"min\", \"mid\", \"max\", \"m\\u0069n\"]", NULL, 4u,
          ESC_THR_MIN, ESC_THR_MIN },
        { "[\"max\", \"mid\"]", NULL, 2u, ESC_THR_MAX, ESC_THR_MID },
        { "null", NULL, 0u, ESC_THR_MIN, ESC_THR_MIN },
        { "\"min\"", "items[0].values[1].after_select: not 1-4", 0u,
          ESC_THR_MIN, ESC_THR_MIN },
        { "[]", "items[0].values[1].after_select: not 1-4", 0u, ESC_THR_MIN,
          ESC_THR_MIN },
        { "[\"min\", \"max\", \"min\", \"max\", \"min\"]",
          "items[0].values[1].after_select: not 1-4", 0u, ESC_THR_MIN,
          ESC_THR_MIN },
        { "[\"none\"]", "items[0].values[1].after_select[0]: not a known",
          0u, ESC_THR_MIN, ESC_THR_MIN },
        { "[\"min\", \"MIN\"]",
          "items[0].values[1].after_select[1]: not a known", 0u,
          ESC_THR_MIN, ESC_THR_MIN },
        { "[null]", "items[0].values[1].after_select[0]: not a known", 0u,
          ESC_THR_MIN, ESC_THR_MIN },
        { "[{\"throttle\": \"min\"}]",
          "items[0].values[1].after_select[0]: not a known", 0u,
          ESC_THR_MIN, ESC_THR_MIN },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        char to[160];
        (void)snprintf(to, sizeof(to), "%s%s}", pre, k[i].moves);
        char *j = subst(from, to);
        esc_profile_t p;
        void *block = NULL;
        char err[96] = "";
        const bool ok = esc_profile_parse(j, strlen(j), &p, &block, err,
                                          sizeof(err));
        if (k[i].err == NULL) {
            CHECK(ok);
            if (ok) {
                const esc_value_t *v = &p.items[0].values[1];
                CHECK_EQ(v->after_count, k[i].n);
                if (k[i].n > 0u) {
                    CHECK_EQ(v->after[0], k[i].first);
                    CHECK_EQ(v->after[k[i].n - 1u], k[i].last);
                }
                CHECK_EQ(p.items[0].values[0].after_count, 0u);
            }
        } else {
            CHECK(!ok);
            if (strncmp(err, k[i].err, strlen(k[i].err)) != 0) {
                T_FAIL("case %u: got \"%s\", want \"%s...\"", (unsigned)i,
                       err, k[i].err);
            }
        }
        free(block);
        free(j);
    }
}

/* Every rule the generator holds a manual step to, held here as its
 * self-test holds it there; the last two are the limits, accepted. */
TEST_CASE(a_manual_step_the_generator_refuses_is_refused_here_too)
{
    char x121[160], u61[160], x120[160], u60[160];
    memset(x121, 'x', 121);
    x121[121] = '\0';
    memcpy(x120, x121, 121);
    x120[120] = '\0';
    u61[0] = '\0';
    for (int i = 0; i < 61; ++i) {
        strcat(u61, "\xC3\xBC");
    }
    memcpy(u60, u61, 121);
    u60[120] = '\0';
    char long121[256], umlaut61[256], long120[256], umlaut60[256];
    (void)snprintf(long121, sizeof(long121),
                   "[{\"when\": \"before_menu\", \"action\": \"%s\"}]", x121);
    (void)snprintf(umlaut61, sizeof(umlaut61),
                   "[{\"when\": \"before_menu\", \"action\": \"%s\"}]", u61);
    (void)snprintf(long120, sizeof(long120),
                   "[{\"when\": \"before_menu\", \"action\": \"%s\"}]", x120);
    (void)snprintf(umlaut60, sizeof(umlaut60),
                   "[{\"when\": \"before_menu\", \"action\": \"%s\"}]", u60);
    static const char jumper[] =
        "{\"when\": \"before_power\", \"action\": \"Fit the jumper.\"}";
    static const char pull[] =
        "{\"when\": \"before_menu\", \"action\": \"Pull the jumper.\"}";
    char five[512], order[256], four[512], two[256];
    (void)snprintf(five, sizeof(five), "[%s, %s, %s, %s, %s]", jumper, jumper,
                   jumper, jumper, jumper);
    (void)snprintf(order, sizeof(order), "[%s, %s]", pull, jumper);
    (void)snprintf(four, sizeof(four), "[%s, %s, %s, %s]", pull, pull, pull,
                   pull);
    (void)snprintf(two, sizeof(two), "[%s, %s]", jumper, pull);
    const struct {
        const char *steps, *err;
    } k[] = {
        { "[]", "manual: not 1-4 steps" },
        { jumper, "manual: not 1-4 steps" },
        { five, "manual: not 1-4 steps" },
        { "[\"Fit the jumper.\"]", "manual[0]: not an object" },
        { "[{\"when\": \"later\", \"action\": \"x\"}]",
          "manual[0].when: not a known value" },
        { "[{\"action\": \"x\"}]", "manual[0].when: not a known value" },
        { order, "manual[1].when: before the step above it" },
        { "[{\"when\": \"before_menu\", \"action\": \"\"}]",
          "manual[0].action: empty" },
        { "[{\"when\": \"before_menu\"}]", "manual[0].action: not a string" },
        { long121, "manual[0].action: longer than 120 bytes" },
        { umlaut61, "manual[0].action: longer than 120 bytes" },
        { "[{\"when\": \"before_menu\", \"action\": \"x\", \"hold_ms\": 0}]",
          "manual[0].hold_ms: only for at_power_up" },
        { "[{\"when\": \"at_power_up\", \"action\": \"x\","
          " \"hold_ms\": 60001}]", "manual[0].hold_ms: outside" },
        { "[{\"when\": \"at_power_up\", \"action\": \"x\","
          " \"hold_ms\": \"2\"}]", "manual[0].hold_ms: not a whole" },
        { long120, NULL },
        { umlaut60, NULL },
        { four, NULL },
        { two, NULL },
        { "[{\"when\": \"at_power_up\", \"action\": \"x\","
          " \"hold_ms\": 60000}]", NULL },
        { "[{\"when\": \"during_menu\", \"action\": \"x\","
          " \"source\": \"p. 5\"}]", NULL },
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        char *j = with_manual(k[i].steps);
        char err[96] = "";
        const bool ok = parses(j, err, sizeof(err));
        if (k[i].err == NULL) {
            if (!ok) {
                T_FAIL("case %u refused: %s", (unsigned)i, err);
            }
        } else if (ok) {
            T_FAIL("case %u accepted: %s", (unsigned)i, k[i].steps);
        } else if (strncmp(err, k[i].err, strlen(k[i].err)) != 0) {
            T_FAIL("case %u: got \"%s\", want \"%s...\"", (unsigned)i, err,
                   k[i].err);
        }
        free(j);
    }
    /* Only an assisted profile has steps: one the bench runs alone has
     * none to do, and one nobody runs has no procedure to do them in. */
    static const char *const k_auto[] = { "full", "none" };
    for (size_t i = 0; i < 2; ++i) {
        char to[256];
        (void)snprintf(to, sizeof(to),
                       "\"automatable\": \"%s\", \"automatable_note\": \"x\","
                       " \"manual\": [%s]", k_auto[i], jumper);
        char *j = subst("\"automatable\": \"full\", \"automatable_note\": \"\"",
                        to);
        char err[96] = "";
        CHECK(!parses(j, err, sizeof(err)));
        CHECK_STR_EQ(err, "manual: only on an assisted profile");
        free(j);
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
    CHECK_EQ(p.select_within_ms, 0);            /* absent: not known */
    CHECK_EQ(p.value_select_throttle, ESC_THR_NONE);  /* one stage */
    CHECK_EQ(p.value_select_within_ms, 0);
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

/* A two-stage menu: the select move picks the item, a second move stores
 * the value sounded. */
TEST_CASE(a_two_stage_menu_reads_its_value_select_move)
{
    char *j = subst("\"select\": {\"throttle\": \"min\"}",
                    "\"select\": {\"throttle\": \"min\", \"within_ms\": 3000},"
                    " \"value_select\": {\"throttle\": \"max\","
                    " \"within_ms\": null}");
    esc_profile_t p;
    void *block = NULL;
    CHECK(esc_profile_parse(j, strlen(j), &p, &block, NULL, 0));
    if (block != NULL) {
        CHECK_EQ(p.select_throttle, ESC_THR_MIN);
        CHECK_EQ(p.select_within_ms, 3000);
        CHECK_EQ(p.value_select_throttle, ESC_THR_MAX);
        CHECK_EQ(p.value_select_within_ms, 0);  /* null: not known */
    }
    free(block);
    free(j);

    /* null is a one-stage menu, as an absent member is. */
    j = subst("\"select\": {", "\"value_select\": null, \"select\": {");
    block = NULL;
    CHECK(esc_profile_parse(j, strlen(j), &p, &block, NULL, 0));
    if (block != NULL) {
        CHECK_EQ(p.value_select_throttle, ESC_THR_NONE);
    }
    free(block);
    free(j);
}

/* The stick's resting place while the menu sounds: absent and null are
 * where the entry left it; "none" is no place and is refused, as the
 * generator refuses it. */
TEST_CASE(a_menu_reads_where_the_stick_rests)
{
    esc_profile_t p;
    void *block = NULL;
    CHECK(esc_profile_parse(k_min, strlen(k_min), &p, &block, NULL, 0));
    if (block != NULL) {
        CHECK_EQ(p.listen_throttle, ESC_THR_NONE);
    }
    free(block);

    char *j = subst("\"select\": {",
                    "\"listen\": {\"throttle\": \"min\"}, \"select\": {");
    block = NULL;
    CHECK(esc_profile_parse(j, strlen(j), &p, &block, NULL, 0));
    if (block != NULL) {
        CHECK_EQ(p.listen_throttle, ESC_THR_MIN);
    }
    free(block);
    free(j);

    j = subst("\"select\": {", "\"listen\": null, \"select\": {");
    block = NULL;
    CHECK(esc_profile_parse(j, strlen(j), &p, &block, NULL, 0));
    if (block != NULL) {
        CHECK_EQ(p.listen_throttle, ESC_THR_NONE);
    }
    free(block);
    free(j);

    /* An escaped spelling is the plain one, and members beside it are
     * not read; the generator takes both. */
    j = subst("\"select\": {", "\"listen\": {\"throttle\": \"m\\u0069d\", "
              "\"x\": [1, 2]}, \"select\": {");
    block = NULL;
    CHECK(esc_profile_parse(j, strlen(j), &p, &block, NULL, 0));
    if (block != NULL) {
        CHECK_EQ(p.listen_throttle, ESC_THR_MID);
    }
    free(block);
    free(j);
}

/* The move that stores a selection, as the listen move is read. */
TEST_CASE(a_menu_reads_the_move_that_stores)
{
    esc_profile_t p;
    void *block = NULL;
    CHECK(esc_profile_parse(k_min, strlen(k_min), &p, &block, NULL, 0));
    if (block != NULL) {
        CHECK_EQ(p.store_throttle, ESC_THR_NONE);
    }
    free(block);
    char *j = subst("\"select\": {",
                    "\"store\": {\"throttle\": \"min\"}, \"select\": {");
    block = NULL;
    CHECK(esc_profile_parse(j, strlen(j), &p, &block, NULL, 0));
    if (block != NULL) {
        CHECK_EQ(p.store_throttle, ESC_THR_MIN);
    }
    free(block);
    free(j);
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
        { "\"select\": {\"throttle\": \"min\"}",
          "\"select\": {\"throttle\": \"min\", \"within_ms\": 60001}",
          "scheme.select.within_ms: outside" },
        { "\"select\": {", "\"value_select\": \"max\", \"select\": {",
          "scheme.value_select: not an object" },
        { "\"select\": {", "\"value_select\": {}, \"select\": {",
          "scheme.value_select.throttle: not a known" },
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
        { "\"select\": {", "\"listen\": \"min\", \"select\": {",
          "scheme.listen: not an object" },
        { "\"select\": {", "\"listen\": {\"throttle\": \"none\"}, "
          "\"select\": {", "scheme.listen.throttle: not a known value" },
        { "\"select\": {", "\"listen\": {}, \"select\": {",
          "scheme.listen.throttle: not a known value" },
        { "\"select\": {", "\"listen\": {\"throttle\": null}, "
          "\"select\": {", "scheme.listen.throttle: not a known value" },
        { "\"select\": {", "\"listen\": {\"throttle\": []}, "
          "\"select\": {", "scheme.listen.throttle: not a known value" },
        { "\"select\": {", "\"listen\": {\"throttle\": {}}, "
          "\"select\": {", "scheme.listen.throttle: not a known value" },
        { "\"select\": {", "\"listen\": {\"throttle\": 0}, "
          "\"select\": {", "scheme.listen.throttle: not a known value" },
        { "\"select\": {", "\"listen\": {\"throttle\": \"MIN\"}, "
          "\"select\": {", "scheme.listen.throttle: not a known value" },
        { "\"select\": {", "\"listen\": {\"throttle\": \"min\", "
          "\"throttle\": \"max\"}, \"select\": {", "a key twice" },
        { "\"select\": {", "\"store\": \"min\", \"select\": {",
          "scheme.store: not an object" },
        { "\"select\": {", "\"store\": {\"throttle\": \"none\"}, "
          "\"select\": {", "scheme.store.throttle: not a known value" },
        { "\"select\": {", "\"store\": {\"throttle\": []}, "
          "\"select\": {", "scheme.store.throttle: not a known value" },
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
    /* A whole number of any length in a field the panel skips; the
     * generator lifts Python's 4,300-digit limit to match. */
    char digits[4400] = "\"schema\": 1, \"n\": ";
    const size_t at = strlen(digits);
    memset(digits + at, '1', 4301);
    strcpy(digits + at + 4301, ",");
    j = subst("\"schema\": 1,", digits);
    CHECK(parses(j, NULL, 0));
    free(j);
}

/* The first name that repeats an earlier one is the one named, as the
 * generator names it, however the names sort. */
TEST_CASE(a_repeated_model_name_is_named_where_it_first_repeats)
{
    char *j = subst("\"models\": [",
                    "\"models\": [{\"name\": \"B\"}, {\"name\": \"A\"},"
                    " {\"name\": \"B\"}, {\"name\": \"A\"}, ");
    char err[96] = "";
    CHECK(!parses(j, err, sizeof(err)));
    CHECK_STR_EQ(err, "models[2].name: duplicate");
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

/* ------------------------------------------------------------- search */

TEST_CASE(a_pattern_is_found_anywhere_and_case_does_not_matter)
{
    CHECK(esc_text_matches("Skywalker V2 15A-100A", "walker"));
    CHECK(esc_text_matches("Skywalker V2 15A-100A", "WALKER"));
    CHECK(esc_text_matches("SKYWALKER", "skyWalker"));
    CHECK(esc_text_matches("Skywalker V2 15A-100A", "100a"));
    CHECK(esc_text_matches("Skywalker V2 15A-100A", "Skywalker V2 15A-100A"));
    CHECK(!esc_text_matches("Skywalker V2 15A-100A", "walker v3"));
    CHECK(!esc_text_matches("Skywalker", "Skywalkers"));
    CHECK(!esc_text_matches("", "a"));
    /* Only letters fold: a digit or a mark matches itself alone. */
    CHECK(!esc_text_matches("A-B", "A_B"));
    CHECK(esc_text_matches("[x]", "[X]"));
    CHECK(!esc_text_matches("@", "`"));             /* 0x40 and 0x60 */
}

TEST_CASE(an_empty_pattern_or_stars_alone_find_everything)
{
    CHECK(esc_text_matches("Kontronik", ""));
    CHECK(esc_text_matches("Kontronik", "*"));
    CHECK(esc_text_matches("Kontronik", "**"));
    CHECK(esc_text_matches("Kontronik", "*****"));
    CHECK(esc_text_matches("", ""));
    CHECK(esc_text_matches("", "*"));
    CHECK(esc_text_matches("", "**"));
    CHECK(esc_text_matches("Kontronik", NULL));
    CHECK(esc_text_matches(NULL, ""));
    CHECK(!esc_text_matches(NULL, "a"));
}

TEST_CASE(a_star_stands_for_any_run_of_characters)
{
    const char *t = "Skywalker V2 15A-100A, 11-item menu";
    CHECK(esc_text_matches(t, "sky*v2"));
    CHECK(esc_text_matches(t, "*sky*v2"));          /* leading star */
    CHECK(esc_text_matches(t, "sky*v2*"));          /* trailing star */
    CHECK(esc_text_matches(t, "**sky**v2**"));
    CHECK(esc_text_matches(t, "s*k*y*m*u"));
    CHECK(esc_text_matches(t, "walker*"));          /* none at the end */
    CHECK(esc_text_matches(t, "*menu"));
    CHECK(esc_text_matches(t, "V2*15"));            /* a run of one */
    CHECK(esc_text_matches(t, "V2* 15"));           /* a run of none */
    CHECK(!esc_text_matches(t, "v2*sky"));          /* the order holds */
    CHECK(!esc_text_matches(t, "sky*v3"));
    CHECK(!esc_text_matches(t, "menu*x"));
    /* The first place a part fits is not the only one tried. */
    CHECK(esc_text_matches("aab aac", "a*ac"));
    CHECK(esc_text_matches("ab ab abc", "ab*abc"));
    CHECK(!esc_text_matches("ab ab abd", "ab*abc"));
}

TEST_CASE(a_letter_outside_ascii_matches_only_itself)
{
    /* No built-in profile names one (all are ASCII); a card's may.  UTF-8
     * bytes compare as they are: no case folding outside A to Z, and a
     * star spans a two-byte letter whole. */
    const char *t = "Müller Fahrtregler GRÖSSE";
    CHECK(esc_text_matches(t, "Müller"));
    CHECK(esc_text_matches(t, "mÜller") == false);
    CHECK(esc_text_matches(t, "M*LLER"));
    CHECK(esc_text_matches(t, "m*ller fahrt"));
    CHECK(esc_text_matches(t, "gr*sse"));
    CHECK(esc_text_matches(t, "GRÖSSE"));
    CHECK(!esc_text_matches(t, "grösse"));
    CHECK(!esc_text_matches(t, "MULLER"));
    for (size_t i = 0; i < esc_profiles_builtin_count; ++i) {
        const esc_profile_t *p = &esc_profiles_builtin[i];
        for (const char *c = p->brand; *c != '\0'; ++c) {
            CHECK(((unsigned char)*c) < 0x80u);
        }
        for (const char *c = p->family; *c != '\0'; ++c) {
            CHECK(((unsigned char)*c) < 0x80u);
        }
    }
}

TEST_CASE(the_longest_pattern_and_a_long_text_are_matched_whole)
{
    /* 23 characters, the text keyboard's most. */
    CHECK(esc_text_matches("Hobbywing Skywalker 130A/160A HV OPTO V2",
                           "SKYWALKER 130A/160A HV "));
    CHECK(!esc_text_matches("Hobbywing Skywalker 130A/160A HV OPTO V2",
                            "SKYWALKER 130A/160A HVX"));
    CHECK(esc_text_matches("Hobbywing", "***********************"));
    CHECK(esc_text_matches("abcdefghijklmnopqrstuvw",
                           "A*B*C*D*E*F*G*H*I*J*K*L"));
    CHECK(!esc_text_matches("abcdefghijklmnopqrstuvw",
                            "A*B*C*D*E*F*G*H*I*J*K*Z"));
    /* A text of 4000 characters with the part at its very end, and the
     * worst case for the retry: no match after many near ones. */
    static char big[4001];
    memset(big, 'a', 4000u);
    big[4000] = '\0';
    CHECK(!esc_text_matches(big, "a*aaaaaaaaaaaaaaaaab"));
    big[3999] = 'b';
    CHECK(esc_text_matches(big, "a*aaaaaaaaaaaaaaaaab"));
    CHECK(esc_text_matches(big, "B"));
}

TEST_CASE(a_profile_is_found_by_maker_and_name_read_as_one)
{
    /* The owner's example: a Kontronik Jazz 55.  The built-in profile is
     * "Kontronik" "JAZZ / MINIJAZZ"; the 55 is in its model "JAZZ 55 LV". */
    const esc_profile_t *jazz = esc_profiles_find("kontronik-jazz");
    CHECK(jazz != NULL);
    CHECK(esc_profile_matches(jazz, "*kontr*jazz*55*"));
    CHECK(esc_profile_matches(jazz, "KONTR*Jazz"));     /* mixed case */
    CHECK(esc_profile_matches(jazz, "kontronik jazz"));   /* across the join */
    CHECK(esc_profile_matches(jazz, "nik JAZZ / mini"));
    CHECK(esc_profile_matches(jazz, "minijazz 20"));      /* a model alone */
    CHECK(!esc_profile_matches(jazz, "kontr*jazz*56"));
    CHECK(!esc_profile_matches(jazz, "jazz*kontr"));
    CHECK(!esc_profile_matches(jazz, "kontronikjazz"));   /* the space holds */
    CHECK(!esc_profile_matches(NULL, ""));

    /* The same rule on a profile whose name carries the size: the maker and
     * the name are one text, the models need not say it. */
    esc_profile_t p;
    memset(&p, 0, sizeof(p));
    p.brand = "Kontronik";
    p.family = "Jazz 55-10-18";
    CHECK(esc_profile_matches(&p, "*kontr*jazz*55*"));
    CHECK(esc_profile_matches(&p, "KONTR*Jazz"));
    CHECK(esc_profile_matches(&p, "ik j"));
    CHECK(!esc_profile_matches(&p, "jazz 56"));
    p.brand = NULL;
    p.family = NULL;
    CHECK(esc_profile_matches(&p, ""));
    CHECK(esc_profile_matches(&p, " "));                  /* the join */
    CHECK(!esc_profile_matches(&p, "k"));

    /* Every built-in profile is found by its own maker and name. */
    for (size_t i = 0; i < esc_profiles_builtin_count; ++i) {
        const esc_profile_t *b = &esc_profiles_builtin[i];
        char both[256];
        (void)snprintf(both, sizeof(both), "%s %s", b->brand, b->family);
        CHECK(esc_profile_matches(b, both));
        CHECK(esc_profile_matches(b, b->brand));
    }
}

TEST_CASE(sky_v2_finds_the_three_skywalker_v2_profiles)
{
    unsigned n = 0;
    for (size_t i = 0; i < esc_profiles_count(); ++i) {
        const esc_profile_t *p = esc_profiles_at(i);
        if (esc_profile_matches(p, "SKY*V2")) {
            CHECK(strstr(p->family, "Skywalker") != NULL);
            ++n;
        }
    }
    CHECK_EQ(n, 3u);
}

int main(void)
{
    RUN(every_profile_of_record_parses_to_its_generated_table);
    RUN(builtin_ids_are_sorted_and_unique);
    RUN(a_profile_reads_into_every_field);
    RUN(a_two_stage_menu_reads_its_value_select_move);
    RUN(a_menu_reads_where_the_stick_rests);
    RUN(a_menu_reads_the_move_that_stores);
    RUN(a_profile_reads_its_manual_steps);
    RUN(a_value_reads_the_stick_position_it_is_set_from);
    RUN(a_value_reads_its_own_entry_time);
    RUN(a_value_reads_its_moves_after_the_selection);
    RUN(a_manual_step_the_generator_refuses_is_refused_here_too);
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
    RUN(a_repeated_model_name_is_named_where_it_first_repeats);
    RUN(a_card_profile_takes_a_built_in_profiles_place);
    RUN(a_card_profile_with_a_new_id_follows_the_built_in_ones);
    RUN(the_registry_refuses_past_its_capacity);
    RUN(a_pattern_is_found_anywhere_and_case_does_not_matter);
    RUN(an_empty_pattern_or_stars_alone_find_everything);
    RUN(a_star_stands_for_any_run_of_characters);
    RUN(a_letter_outside_ascii_matches_only_itself);
    RUN(the_longest_pattern_and_a_long_text_are_matched_whole);
    RUN(a_profile_is_found_by_maker_and_name_read_as_one);
    RUN(sky_v2_finds_the_three_skywalker_v2_profiles);
    return test_summary("esc_profiles");
}
