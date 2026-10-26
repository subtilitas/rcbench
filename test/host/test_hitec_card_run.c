/*
 * The card's side of the Hitec programmer's servo files
 * (shared/bench/hitec_card_run.c) over a folder held in memory: a save
 * named by the card and by the operator, each refusal of the card, all 999
 * proposed names taken, a list
 * of 1 page and of pages behind the first 64 files with each file's model,
 * a load, a file larger than any servo file, a file that does not parse
 * and one the folder does not hold.
 *
 * SPDX-License-Identifier: MIT
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "greatest.h"

#include "hitec_card_run.h"
#include "hitec_reg_fix.h"
#include "servo_store.h"

/* --- a card in memory ------------------------------------------------------ */

#define FILES 1000u

typedef struct {
    char   name[48];
    char   text[HITEC_CARD_FILE_MAX + 64u];
    size_t len;
} file_t;

static struct {
    file_t   f[FILES];
    unsigned n;
    bool     mounted;
    bool     walk_fails;
    bool     read_fails;
    servo_store_io_result_t create;
    unsigned walks;
} g;

static bool same(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static file_t *find(const char *name)
{
    for (unsigned i = 0; i < g.n; ++i) {
        if (same(g.f[i].name, name)) {
            return &g.f[i];
        }
    }
    return NULL;
}

static bool io_mounted(void *ctx)
{
    (void)ctx;
    return g.mounted;
}

static bool io_walk(void *ctx, servo_store_visit_fn visit, void *visit_ctx)
{
    (void)ctx;
    g.walks++;
    for (unsigned i = 0; i < g.n; ++i) {
        visit(g.f[i].name, false, visit_ctx);
    }
    return !g.walk_fails;
}

static int32_t io_size(void *ctx, const char *name)
{
    const file_t *f = find(name);

    (void)ctx;
    return f != NULL ? (int32_t)f->len : -1;
}

static int32_t io_read(void *ctx, const char *name, char *buf, size_t cap)
{
    const file_t *f = find(name);
    size_t n;

    (void)ctx;
    if (f == NULL || g.read_fails) {
        return -1;
    }
    n = f->len < cap ? f->len : cap;
    memcpy(buf, f->text, n);
    return (int32_t)n;
}

static void put(const char *name, const char *text, size_t len)
{
    file_t *f = &g.f[g.n++];

    snprintf(f->name, sizeof(f->name), "%s", name);
    memcpy(f->text, text, len);
    f->len = len;
}

static servo_store_io_result_t io_create(void *ctx, const char *name,
                                         const char *data, size_t len)
{
    (void)ctx;
    if (g.create != SERVO_STORE_IO_OK) {
        return g.create;
    }
    put(name, data, len);
    return SERVO_STORE_IO_OK;
}

static const servo_store_io_t k_io = {
    NULL, HITEC_CARD_STEM, io_mounted, io_walk, io_size, io_read, io_create,
};

static void reset(void)
{
    memset(&g, 0, sizeof(g));
    g.mounted = true;
    g.create = SERVO_STORE_IO_OK;
}

static void put_image(const char *name, const hitec_image_t *img)
{
    char text[HITEC_CARD_FILE_MAX];
    const size_t len = hitec_card_file_text(img, text, sizeof(text));

    CHECK(len > 0u);
    put(name, text, len);
}

static hitec_card_done_t run(uint8_t op, bool preset, const char *name,
                             unsigned first, const hitec_image_t *img)
{
    static hitec_card_req_t req;
    static hitec_card_done_t done;

    memset(&req, 0, sizeof(req));
    req.op = op;
    req.preset = preset;
    req.first = (uint16_t)first;
    req.id = 77u;
    if (name != NULL) {
        snprintf(req.name, sizeof(req.name), "%s", name);
    }
    if (img != NULL) {
        req.image = *img;
    }
    memset(&done, 0x5A, sizeof(done));
    hitec_card_run(&k_io, &req, &done);
    CHECK_EQ(done.op, op);
    CHECK_EQ(done.id, 77u);
    return done;
}

/* --- saves --------------------------------------------------------------- */

TEST_CASE(a_save_named_by_the_card_takes_the_next_number)
{
    hitec_image_t img, got;
    hitec_card_done_t d;

    reset();
    d485(&img);
    d = run(HITEC_CARD_SAVE, true, NULL, 0u, &img);
    CHECK_EQ(d.result, HITEC_CARD_DONE_OK);
    CHECK_STR_EQ(d.name, "HITEC001");
    CHECK_EQ(g.n, 1u);
    CHECK_STR_EQ(g.f[0].name, "HITEC001.TXT");
    CHECK_EQ(hitec_card_file_parse(g.f[0].text, g.f[0].len, &got, NULL,
                                   NULL), HITEC_CARD_OK);
    CHECK_EQ(hitec_image_reg(&got, 0x54u), hitec_image_reg(&img, 0x54u));
    d = run(HITEC_CARD_SAVE, true, NULL, 0u, &img);
    CHECK_STR_EQ(d.name, "HITEC002");
    /* By the operator's name; no file is replaced. */
    d = run(HITEC_CARD_SAVE, false, "WING-L", 0u, &img);
    CHECK_EQ(d.result, HITEC_CARD_DONE_OK);
    CHECK_STR_EQ(d.name, "WING-L");
    d = run(HITEC_CARD_SAVE, false, "wing-l", 0u, &img);
    CHECK_EQ(d.result, HITEC_CARD_DONE_EXISTS);
    CHECK_EQ(d.name[0], '\0');
    /* The record's name and a name with a full stop in front. */
    d = run(HITEC_CARD_SAVE, false, "HITEC", 0u, &img);
    CHECK_EQ(d.result, HITEC_CARD_DONE_NAME);
    d = run(HITEC_CARD_SAVE, false, ".A", 0u, &img);
    CHECK_EQ(d.result, HITEC_CARD_DONE_NAME);
    /* The longest name: 32 characters. */
    d = run(HITEC_CARD_SAVE, false, "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345", 0u,
            &img);
    CHECK_EQ(d.result, HITEC_CARD_DONE_OK);
    CHECK_STR_EQ(d.name, "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345");
    CHECK_EQ(g.n, 4u);
}

TEST_CASE(each_refusal_of_the_card_has_its_answer)
{
    hitec_image_t img;
    hitec_card_done_t d;
    char name[16];

    d485(&img);
    reset();
    g.mounted = false;
    d = run(HITEC_CARD_SAVE, true, NULL, 0u, &img);
    CHECK_EQ(d.result, HITEC_CARD_DONE_NO_CARD);
    reset();
    g.create = SERVO_STORE_IO_FULL;
    CHECK_EQ(run(HITEC_CARD_SAVE, true, NULL, 0u, &img).result,
             HITEC_CARD_DONE_FULL);
    g.create = SERVO_STORE_IO_DENIED;
    CHECK_EQ(run(HITEC_CARD_SAVE, true, NULL, 0u, &img).result,
             HITEC_CARD_DONE_DENIED);
    g.create = SERVO_STORE_IO_ERROR;
    CHECK_EQ(run(HITEC_CARD_SAVE, true, NULL, 0u, &img).result,
             HITEC_CARD_DONE_WRITE);
    g.create = SERVO_STORE_IO_OK;
    g.walk_fails = true;
    CHECK_EQ(run(HITEC_CARD_SAVE, true, NULL, 0u, &img).result,
             HITEC_CARD_DONE_WRITE);
    CHECK_EQ(run(HITEC_CARD_LIST, false, NULL, 0u, NULL).result,
             HITEC_CARD_DONE_READ);
    CHECK_EQ(g.n, 0u);
    /* 200 numbers taken, the last of them renamed to 999: with 999 taken
     * the lowest free number is the next. */
    reset();
    for (unsigned i = 1; i <= 200u; ++i) {
        snprintf(name, sizeof(name), "HITEC%03u.TXT", i);
        put(name, "x", 1u);
    }
    snprintf(g.f[199].name, sizeof(g.f[0].name), "HITEC999.TXT");
    d = run(HITEC_CARD_SAVE, true, NULL, 0u, &img);
    CHECK_EQ(d.result, HITEC_CARD_DONE_OK);
    CHECK_STR_EQ(d.name, "HITEC200");
    /* All 999 numbers taken: a preset save finds the card full, and a
     * list proposes no name. */
    reset();
    for (unsigned i = 1; i <= 999u; ++i) {
        snprintf(name, sizeof(name), "HITEC%03u.TXT", i);
        put(name, "x", 1u);
    }
    CHECK_EQ(run(HITEC_CARD_SAVE, true, NULL, 0u, &img).result,
             HITEC_CARD_DONE_FULL);
    d = run(HITEC_CARD_LIST, false, NULL, 0u, NULL);
    CHECK_EQ(d.result, HITEC_CARD_DONE_OK);
    CHECK_EQ(d.total, 999u);
    CHECK_EQ(d.count, 5u);
    CHECK_STR_EQ(d.names[0], "HITEC999");
    CHECK_STR_EQ(d.name, "");
    d = run(HITEC_CARD_LIST, false, NULL, 995u, NULL);
    CHECK_EQ(d.count, 4u);
    CHECK_STR_EQ(d.names[3], "HITEC001");
    /* A named save still goes. */
    CHECK_EQ(run(HITEC_CARD_SAVE, false, "LAST", 0u, &img).result,
             HITEC_CARD_DONE_OK);
    /* An image of no series makes no text and no file. */
    reset();
    img.series = 5u;
    CHECK_EQ(run(HITEC_CARD_SAVE, true, NULL, 0u, &img).result,
             HITEC_CARD_DONE_WRITE);
    CHECK_EQ(g.n, 0u);
    /* A request of no operation. */
    CHECK_EQ(run(HITEC_CARD_IDLE, false, NULL, 0u, NULL).result,
             HITEC_CARD_DONE_READ);
}

/* --- lists --------------------------------------------------------------- */

TEST_CASE(a_list_gives_a_page_its_models_and_the_next_name)
{
    hitec_image_t d, c;
    hitec_card_done_t r;

    reset();
    d485(&d);
    hs7955(&c);
    put_image("HITEC002.TXT", &d);
    put_image("WING.TXT", &c);
    put_image("HITEC001.TXT", &c);
    put("ALPHA.TXT", "not a servo file\n", 17u);
    put("HITEC.TXT", "the record\n", 11u);
    put("NOTES.LOG", "x", 1u);
    r = run(HITEC_CARD_LIST, false, NULL, 0u, NULL);
    CHECK_EQ(r.result, HITEC_CARD_DONE_OK);
    CHECK_EQ(r.total, 4u);
    CHECK_EQ(r.count, 4u);
    CHECK_STR_EQ(r.names[0], "ALPHA");
    CHECK_STR_EQ(r.models[0], "");
    CHECK_STR_EQ(r.names[1], "HITEC002");
    CHECK_STR_EQ(r.models[1], "D485HW");
    CHECK_STR_EQ(r.names[2], "HITEC001");
    CHECK_STR_EQ(r.models[2], "HS-7955TG");
    CHECK_STR_EQ(r.names[3], "WING");
    CHECK_STR_EQ(r.models[3], "HS-7955TG");
    CHECK_STR_EQ(r.name, "HITEC003");
    /* Past the last file: none. */
    r = run(HITEC_CARD_LIST, false, NULL, 4u, NULL);
    CHECK_EQ(r.result, HITEC_CARD_DONE_OK);
    CHECK_EQ(r.count, 0u);
    CHECK_EQ(r.total, 4u);
    /* An empty folder. */
    reset();
    r = run(HITEC_CARD_LIST, false, NULL, 0u, NULL);
    CHECK_EQ(r.result, HITEC_CARD_DONE_OK);
    CHECK_EQ(r.count, 0u);
    CHECK_EQ(r.total, 0u);
    CHECK_STR_EQ(r.name, "HITEC001");
    g.mounted = false;
    CHECK_EQ(run(HITEC_CARD_LIST, false, NULL, 0u, NULL).result,
             HITEC_CARD_DONE_NO_CARD);
}

TEST_CASE(a_page_behind_64_files_is_reached_and_none_straddles_a_walk)
{
    hitec_card_done_t r;
    char name[16];
    unsigned walks;

    reset();
    /* 140 files A000 to A139: the order is the names'. */
    for (unsigned i = 0; i < 140u; ++i) {
        snprintf(name, sizeof(name), "A%03u.TXT", i);
        put(name, "x", 1u);
    }
    for (unsigned first = 0; first < 140u; first += 5u) {
        r = run(HITEC_CARD_LIST, false, NULL, first, NULL);
        CHECK_EQ(r.result, HITEC_CARD_DONE_OK);
        CHECK_EQ(r.total, 140u);
        CHECK_EQ(r.count, 5u);
        for (unsigned i = 0; i < r.count; ++i) {
            snprintf(name, sizeof(name), "A%03u", first + i);
            CHECK_STR_EQ(r.names[i], name);
        }
    }
    /* Pages over the end of the first and of the second walk. */
    r = run(HITEC_CARD_LIST, false, NULL, 62u, NULL);
    CHECK_EQ(r.count, 5u);
    CHECK_STR_EQ(r.names[0], "A062");
    CHECK_STR_EQ(r.names[4], "A066");
    r = run(HITEC_CARD_LIST, false, NULL, 126u, NULL);
    CHECK_EQ(r.count, 5u);
    CHECK_STR_EQ(r.names[0], "A126");
    CHECK_STR_EQ(r.names[4], "A130");
    r = run(HITEC_CARD_LIST, false, NULL, 138u, NULL);
    CHECK_EQ(r.count, 2u);
    CHECK_STR_EQ(r.names[1], "A139");
    /* The page at 0 takes 1 walk for the list and 1 for the next name. */
    g.walks = 0;
    (void)run(HITEC_CARD_LIST, false, NULL, 0u, NULL);
    walks = g.walks;
    CHECK(walks <= 2u);
    /* A walk that fails ends the list. */
    g.walk_fails = true;
    CHECK_EQ(run(HITEC_CARD_LIST, false, NULL, 65u, NULL).result,
             HITEC_CARD_DONE_READ);
}

/* --- loads --------------------------------------------------------------- */

TEST_CASE(a_load_gives_the_image_and_its_model)
{
    hitec_image_t img;
    hitec_card_done_t r;

    reset();
    hs5645(&img);
    put_image("HITEC004.TXT", &img);
    r = run(HITEC_CARD_LOAD, false, "hitec004", 0u, NULL);
    CHECK_EQ(r.result, HITEC_CARD_DONE_OK);
    CHECK_EQ(r.parse, HITEC_CARD_OK);
    CHECK_STR_EQ(r.model, "HS-5645MG");
    CHECK_EQ(r.image.series, HITEC_CLASSIC);
    CHECK_EQ(hitec_image_cell(&r.image, 0x06u),
             hitec_image_cell(&img, 0x06u));
    CHECK_EQ(r.image.version, 0x16u);
}

TEST_CASE(a_load_that_finds_no_servo_file_says_why)
{
    static char big[HITEC_CARD_FILE_MAX + 10u];
    char text[HITEC_CARD_FILE_MAX];
    hitec_image_t img;
    hitec_card_done_t r;
    size_t len;

    reset();
    memset(big, 'x', sizeof(big));
    put("BIG.TXT", big, sizeof(big));
    r = run(HITEC_CARD_LOAD, false, "BIG", 0u, NULL);
    CHECK_EQ(r.result, HITEC_CARD_DONE_BAD);
    CHECK_EQ(r.parse, HITEC_CARD_ERR_HEADER);
    /* A file of exactly the largest size is read and parsed. */
    reset();
    put("FULL.TXT", big, HITEC_CARD_FILE_MAX);
    r = run(HITEC_CARD_LOAD, false, "FULL", 0u, NULL);
    CHECK_EQ(r.result, HITEC_CARD_DONE_BAD);
    CHECK_EQ(r.parse, HITEC_CARD_ERR_HEADER);
    /* A changed digit: the check fails. */
    d485(&img);
    len = hitec_card_file_text(&img, text, sizeof(text));
    text[50] = text[50] == '0' ? '1' : '0';
    put("EDITED.TXT", text, len);
    r = run(HITEC_CARD_LOAD, false, "EDITED", 0u, NULL);
    CHECK_EQ(r.result, HITEC_CARD_DONE_BAD);
    CHECK_EQ(r.parse, HITEC_CARD_ERR_CHECK);
    /* A record of a write is no servo file: line 3 is not due. */
    {
        static hitec_card_record_t rec;
        static char rtext[HITEC_CARD_TEXT_MAX];

        memset(&rec, 0, sizeof(rec));
        rec.series = HITEC_D;
        rec.port = 1u;
        rec.pin = 13u;
        rec.before = img;
        rec.target = img;
        len = hitec_card_text(&rec, rtext, sizeof(rtext));
        put("LINE.TXT", rtext, len);
    }
    r = run(HITEC_CARD_LOAD, false, "LINE", 0u, NULL);
    CHECK_EQ(r.result, HITEC_CARD_DONE_BAD);
    CHECK_EQ(r.parse, HITEC_CARD_ERR_FIELD);
    CHECK_EQ(r.line, 3u);
    /* Not in the folder, a name no file may have, a card that does not
     * read, no card. */
    CHECK_EQ(run(HITEC_CARD_LOAD, false, "GONE", 0u, NULL).result,
             HITEC_CARD_DONE_READ);
    CHECK_EQ(run(HITEC_CARD_LOAD, false, "A:B", 0u, NULL).result,
             HITEC_CARD_DONE_READ);
    g.read_fails = true;
    CHECK_EQ(run(HITEC_CARD_LOAD, false, "EDITED", 0u, NULL).result,
             HITEC_CARD_DONE_READ);
    g.mounted = false;
    CHECK_EQ(run(HITEC_CARD_LOAD, false, "EDITED", 0u, NULL).result,
             HITEC_CARD_DONE_NO_CARD);
}

int main(void)
{
    RUN(a_save_named_by_the_card_takes_the_next_number);
    RUN(each_refusal_of_the_card_has_its_answer);
    RUN(a_list_gives_a_page_its_models_and_the_next_name);
    RUN(a_page_behind_64_files_is_reached_and_none_straddles_a_walk);
    RUN(a_load_gives_the_image_and_its_model);
    RUN(a_load_that_finds_no_servo_file_says_why);
    return test_summary("hitec_card_run");
}
