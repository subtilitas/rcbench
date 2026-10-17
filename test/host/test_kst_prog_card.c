/*
 * The servo programmer's card operations (shared/bench/kst_prog_card.c),
 * end to end over the rig of kst_prog_fix.h: the save, the list and the
 * load of servo files, and the save of the servo's file before the first
 * write, against the card model of kst_fake_card.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "kst_prog_fix.h"

/* A write the card kept from starting changed no field: the field it was
 * to change still tells another servo from the session's. */
TEST_CASE(a_write_the_backup_file_stopped_leaves_no_field_written)
{
    up_enabled();
    g_card = true;
    g_fc.fails = KST_PROG_CARD_FULL;
    frame();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_FULL);
    CHECK_EQ(g_p.wrote, 0);
    CHECK_EQ(g_p.held_wrote, 0);
    g_fc.fails = KST_PROG_CARD_OK;
    duty_swapped_is_another_servo();
}

/* ---------------------------------------------------------- the servo files */

#define BUSY (KST_PROG_MSG_START + KST_LINK_START_BUSY)

/* The raw value a line of an edited file gives for @p raw: the displayed
 * value, read back as a keypad entry is. */
static uint16_t as_typed(kst_field_id_t id, uint16_t raw)
{
    int32_t shown = 0;
    uint16_t back = 0;

    CHECK(kst_field_display(id, raw, &shown));
    CHECK(kst_field_raw_from_display(id, shown, &back));
    return back;
}

/* A file whose image is the bench servo's with @p id at @p raw. */
static fake_file_t *file_with(const char *name, kst_field_id_t id,
                              uint16_t raw)
{
    kst_image_t img = sim_bench_image();

    CHECK(kst_field_edit(&img, id, raw));
    return fake_card_put(&g_fc, name, &img);
}

static void load(const char *name)
{
    CHECK_EQ(kst_prog_load(&g_p, name), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_busy(&g_p));
    frames(3);
    CHECK(!kst_prog_card_busy(&g_p));
}

static void save(const char *name)
{
    CHECK_EQ(kst_prog_save(&g_p, name), KST_PROG_MSG_NONE);
    frames(3);
    CHECK(!kst_prog_card_busy(&g_p));
}

static void list(void)
{
    CHECK_EQ(kst_prog_files(&g_p, 0), KST_PROG_MSG_NONE);
    frames(3);
    CHECK(!kst_prog_card_busy(&g_p));
}

/* The bench servo's image as the other servo type. */
static kst_image_t other_type(void)
{
    kst_image_t img = sim_bench_image();

    CHECK(kst_field_set(&img, KST_F_20K_SEL,
                        1u - kst_field_get(&img, KST_F_20K_SEL)));
    return img;
}

TEST_CASE(a_save_writes_the_servos_image_under_the_name_given)
{
    const kst_image_t sv = sim_bench_image();

    up();
    CHECK(!kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "");
    CHECK_EQ(kst_prog_save(&g_p, "WING-L"), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_busy(&g_p));
    /* One request at a time. */
    CHECK_EQ(kst_prog_save(&g_p, "B"), BUSY);
    CHECK_EQ(kst_prog_files(&g_p, 0), BUSY);
    CHECK_EQ(kst_prog_load(&g_p, "B"), BUSY);
    frames(3);
    CHECK(!kst_prog_card_busy(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    CHECK_STR_EQ(kst_prog_card_name(&g_p), "WING-L");
    CHECK_EQ(g_fc.n, 1);
    CHECK_STR_EQ(g_fc.file[0].name, "WING-L");
    CHECK(same(&g_fc.file[0].img, &sv));
    /* A file of this servo is the session's backup. */
    CHECK(kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "WING-L");
    CHECK(!kst_prog_save_first(&g_p));
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(g_starts, 2);
}

TEST_CASE(a_name_the_card_does_not_take_saves_nothing)
{
    up();
    CHECK_EQ(kst_prog_save(&g_p, ""), KST_PROG_MSG_NAME_BAD);
    CHECK_EQ(kst_prog_save(&g_p, NULL), KST_PROG_MSG_NAME_BAD);
    CHECK_EQ(kst_prog_save(&g_p, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456"), KST_PROG_MSG_NAME_BAD);
    CHECK(!kst_prog_card_busy(&g_p));
    /* The card's side knows the names: a character no file system takes,
     * a device name, the journal's own name, a full stop at an end. */
    save("a:b");
    CHECK_EQ(msg(), KST_PROG_MSG_NAME_BAD);
    save("CON");
    CHECK_EQ(msg(), KST_PROG_MSG_NAME_BAD);
    save("kst");
    CHECK_EQ(msg(), KST_PROG_MSG_NAME_BAD);
    save(".A");
    CHECK_EQ(msg(), KST_PROG_MSG_NAME_BAD);
    CHECK_EQ(g_fc.n, 0);
    CHECK(!kst_prog_backed_up(&g_p));
    save("A");
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    /* No file is replaced, in either case of the name. */
    g_sim.reg[0x03] ^= 0x01u;
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    save("A");
    CHECK_EQ(msg(), KST_PROG_MSG_NAME_EXISTS);
    save("a");
    CHECK_EQ(msg(), KST_PROG_MSG_NAME_EXISTS);
    CHECK_EQ(g_fc.n, 1);
    CHECK_EQ(g_fc.file[0].img.r[0x03], sim_bench_image().r[0x03]);
}

TEST_CASE(a_card_that_takes_no_file_says_which_way)
{
    static const struct {
        uint8_t result;
        unsigned msg;
    } k[] = {
        { KST_PROG_CARD_NO_CARD, KST_PROG_MSG_CARD_NONE },
        { KST_PROG_CARD_FULL, KST_PROG_MSG_CARD_FULL },
        { KST_PROG_CARD_WRITE_ERR, KST_PROG_MSG_CARD_WRITE },
        { KST_PROG_CARD_DENIED, KST_PROG_MSG_CARD_DENIED },
        { KST_PROG_CARD_READ_ERR, KST_PROG_MSG_CARD_WRITE },
        { KST_PROG_CARD_RESULTS, KST_PROG_MSG_CARD_WRITE },
    };

    up();
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        g_fc.fails = k[i].result;
        save("A");
        CHECK_EQ(msg(), k[i].msg);
        CHECK(!kst_prog_backed_up(&g_p));
    }
    /* No card in the slot: said at once, nothing is asked. */
    g_fc.fails = KST_PROG_CARD_OK;
    g_card = false;
    frame();
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_CARD_NONE);
    CHECK_EQ(kst_prog_files(&g_p, 0), KST_PROG_MSG_CARD_NONE);
    CHECK_EQ(kst_prog_load(&g_p, "A"), KST_PROG_MSG_CARD_NONE);
    CHECK(!kst_prog_card_busy(&g_p));
    CHECK_EQ(g_fc.saves, 6);
    /* A card that does not answer: KST_PROG_CARD_MS. */
    g_card = true;
    g_fc.silent = true;
    frame();
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NONE);
    frames(KST_PROG_CARD_MS - 2u);
    CHECK(kst_prog_card_busy(&g_p));
    frames(3);
    CHECK(!kst_prog_card_busy(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_WRITE);
    CHECK_EQ(kst_prog_files(&g_p, 0), KST_PROG_MSG_NONE);
    frames(KST_PROG_CARD_MS + 1u);
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_READ);
    CHECK_EQ(kst_prog_files_seq(&g_p), 0);
    CHECK_EQ(g_fc.n, 0);
}

TEST_CASE(the_list_names_the_files_their_types_and_the_next_preset)
{
    const kst_image_t sv = sim_bench_image();
    const kst_image_t ot = other_type();
    unsigned total = 9;
    uint32_t said;

    up();
    /* An empty card. */
    list();
    CHECK_EQ(msg(), KST_PROG_MSG_NO_FILES);
    CHECK_EQ(kst_prog_files_seq(&g_p), 1);
    CHECK_EQ(kst_prog_file_count(&g_p, &total), 0);
    CHECK_EQ(total, 0);
    CHECK_STR_EQ(kst_prog_preset_name(&g_p), "KST001");

    (void)fake_card_put(&g_fc, "KST001", &sv);
    (void)fake_card_put(&g_fc, "WING", &ot);
    fake_card_put(&g_fc, "HAND", &sv)->type = 0;
    list();
    CHECK_EQ(kst_prog_files_seq(&g_p), 2);
    CHECK_EQ(kst_prog_file_count(&g_p, NULL), 3);
    /* Sorted by name, whatever the order on the card. */
    CHECK_STR_EQ(kst_prog_file_name(&g_p, 0), "HAND");
    CHECK_STR_EQ(kst_prog_file_name(&g_p, 1), "KST001");
    CHECK_STR_EQ(kst_prog_file_name(&g_p, 2), "WING");
    CHECK_STR_EQ(kst_prog_file_name(&g_p, 3), "");
    CHECK_EQ(kst_prog_file_type(&g_p, 1), kst_card_type(&sv));
    CHECK_EQ(kst_prog_file_type(&g_p, 2), kst_card_type(&ot));
    CHECK_EQ(kst_prog_file_type(&g_p, 1) + kst_prog_file_type(&g_p, 2), 14);
    CHECK_EQ(kst_prog_file_type(&g_p, 0), 0);
    CHECK_EQ(kst_prog_file_type(&g_p, 3), 0);
    CHECK_STR_EQ(kst_prog_preset_name(&g_p), "KST002");

    /* More files than the list holds. */
    for (unsigned i = 0; i < 6u; ++i) {
        char name[KST_PROG_NAME_MAX];

        snprintf(name, sizeof(name), "F%u", i);
        (void)fake_card_put(&g_fc, name, &sv);
    }
    list();
    CHECK_EQ(kst_prog_file_count(&g_p, &total), KST_PROG_FILES_MAX);
    CHECK_EQ(total, 9);
    CHECK_EQ(kst_prog_files_first(&g_p), 0);
    /* The files behind them, and a page past the last. */
    CHECK_EQ(kst_prog_files(&g_p, KST_PROG_FILES_MAX), KST_PROG_MSG_NONE);
    frames(3);
    CHECK_EQ(kst_prog_files_first(&g_p), KST_PROG_FILES_MAX);
    CHECK_EQ(kst_prog_file_count(&g_p, &total), 9u - KST_PROG_FILES_MAX);
    CHECK_EQ(total, 9);
    CHECK_STR_EQ(kst_prog_file_name(&g_p, 0), "F5");
    CHECK_STR_EQ(kst_prog_file_name(&g_p, 3), "WING");
    CHECK_STR_EQ(kst_prog_file_name(&g_p, 4), "");
    CHECK_STR_EQ(kst_prog_preset_name(&g_p), "KST002");
    said = kst_prog_message(&g_p)->seq;
    CHECK_EQ(kst_prog_files(&g_p, 0x12345u), KST_PROG_MSG_NONE);
    frames(3);
    CHECK_EQ(kst_prog_files_first(&g_p), 0xFFFFu);
    CHECK_EQ(kst_prog_file_count(&g_p, &total), 0);
    CHECK_EQ(total, 9);
    /* Files are there: an empty page is no message. */
    CHECK_EQ(kst_prog_message(&g_p)->seq, said);
    list();
    CHECK_EQ(kst_prog_files_seq(&g_p), 6);
    /* A directory that does not read: the list stays as it was. */
    g_fc.fails = KST_PROG_CARD_READ_ERR;
    list();
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_READ);
    CHECK_EQ(kst_prog_files_seq(&g_p), 6);
    CHECK_EQ(kst_prog_file_count(&g_p, NULL), KST_PROG_FILES_MAX);
}

TEST_CASE(a_file_becomes_the_draft_and_is_written_as_edits)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t duty = kst_field_get(&sv, KST_F_DUTY);

    up();
    (void)file_with("LOW", KST_F_DUTY, (uint16_t)(duty - 1u));
    load("LOW");
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 1);
    CHECK_STR_EQ(kst_prog_card_name(&g_p), "LOW");
    CHECK_EQ(kst_prog_load_kind(&g_p), KST_PROG_FILE_VERIFIED);
    CHECK_EQ(kst_prog_load_skipped(&g_p), 0);
    CHECK_EQ(draft(KST_F_DUTY), duty - 1u);
    CHECK_EQ(kst_prog_changed(&g_p), KST_FIELD_BIT(KST_F_DUTY));
    frames(20);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK_EQ(g_starts, 2);
    /* The write is the ordinary one: the switch, then the changed field. */
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NEED_ENABLE);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(servo().r[KST_REG_DUTY], duty - 1u);
    CHECK_EQ(servo().r[KST_REG_DUTY_COPY], duty - 1u);
}

TEST_CASE(an_edited_file_is_named_as_one)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t duty = kst_field_get(&sv, KST_F_DUTY);
    kst_prog_card_req_t q;
    kst_prog_card_done_t d;
    fake_file_t *f;
    unsigned of = 0;

    up();
    f = file_with("ED", KST_F_DUTY, (uint16_t)(duty - 10u));
    f->kind = KST_PROG_FILE_EDITED;
    load("ED");
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED_EDITED);
    CHECK_EQ(kst_prog_load_kind(&g_p), KST_PROG_FILE_EDITED);
    /* The fields its lines give, of the 27 a file has. */
    CHECK(kst_prog_load_named(&g_p, &of) >= 1u);
    CHECK(kst_prog_load_named(&g_p, NULL) <= 27u);
    CHECK_EQ(of, 27);
    CHECK(as_typed(KST_F_DUTY, (uint16_t)(duty - 10u)) != duty);
    CHECK_EQ(draft(KST_F_DUTY), as_typed(KST_F_DUTY, (uint16_t)(duty - 10u)));
    /* A line that reads as the servo shows the value changes nothing: 1
     * step of 255 is below the 1 % the line is written in. */
    f = file_with("FINE", KST_F_DUTY, (uint16_t)(duty - 1u));
    f->kind = KST_PROG_FILE_EDITED;
    load("FINE");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SAME);
    f = file_with("UN", KST_F_DUTY, (uint16_t)(duty - 20u));
    f->kind = KST_PROG_FILE_UNTYPED;
    f->type = 0;
    load("UN");
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED_UNTYPED);
    CHECK_EQ(kst_prog_load_kind(&g_p), KST_PROG_FILE_UNTYPED);
    CHECK_EQ(draft(KST_F_DUTY), as_typed(KST_F_DUTY, (uint16_t)(duty - 20u)));
    /* A kind this code does not know is the least trusted one. */
    kst_prog_discard(&g_p);
    CHECK_EQ(kst_prog_load(&g_p, "UN"), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_take(&g_p, &q));
    kst_card_run(&g_fc.io, &q, &d);
    CHECK_EQ(d.kind, KST_PROG_FILE_UNTYPED);
    d.kind = KST_PROG_FILE_KINDS;
    /* A count above what a file has is none. */
    d.named = (uint8_t)(d.fields + 1u);
    kst_prog_card_done(&g_p, &d);
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED_UNTYPED);
    CHECK_EQ(kst_prog_load_kind(&g_p), KST_PROG_FILE_UNTYPED);
    CHECK_EQ(kst_prog_load_named(&g_p, &of), 0);
    CHECK_EQ(of, 0);
}

TEST_CASE(a_file_of_the_other_servo_type_is_not_loaded)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t duty = kst_field_get(&sv, KST_F_DUTY);
    kst_image_t ot = other_type();
    const unsigned t_servo = kst_card_type(&sv), t_other = kst_card_type(&ot);
    fake_file_t *f;

    CHECK(kst_field_edit(&ot, KST_F_DUTY, (uint16_t)(duty - 10u)));
    up();
    f = fake_card_put(&g_fc, "OTHER", &ot);
    load("OTHER");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_TYPE);
    CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
    CHECK_EQ(kst_prog_message(&g_p)->arg, t_other | (t_servo << 8));
    CHECK_EQ(kst_prog_changes(&g_p), 0);

    /* The same values in a file that names no type: the type is a locked
     * field, listed and not written. */
    f->type = 0;
    f->kind = KST_PROG_FILE_UNTYPED;
    load("OTHER");
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED_UNTYPED);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 1u | (1u << 8));
    CHECK_EQ(kst_prog_load_skipped(&g_p), KST_FIELD_BIT(KST_F_20K_SEL));
    CHECK_EQ(kst_prog_changed(&g_p), KST_FIELD_BIT(KST_F_DUTY));
    CHECK_EQ(kst_field_get(kst_prog_draft(&g_p), KST_F_20K_SEL),
             kst_field_get(&sv, KST_F_20K_SEL));

    /* The other way round: the servo is of the other type. */
    kst_prog_discard(&g_p);
    CHECK(kst_field_set((kst_image_t *)(void *)g_sim.reg, KST_F_20K_SEL,
                        kst_field_get(&ot, KST_F_20K_SEL)));
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_BACKUP_OTHER);
    CHECK_EQ(kst_prog_load_skipped(&g_p), 0);
    (void)fake_card_put(&g_fc, "FIRST", &sv);
    load("FIRST");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_TYPE);
    CHECK_EQ(kst_prog_message(&g_p)->arg, t_servo | (t_other << 8));
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    frames(20);
    CHECK_EQ(g_sim.n_writes, 0);
}

TEST_CASE(a_file_that_does_not_read_says_where)
{
    const kst_image_t sv = sim_bench_image();
    fake_file_t *f;

    up();
    f = file_with("X", KST_F_DUTY,
                  (uint16_t)(kst_field_get(&sv, KST_F_DUTY) - 1u));
    f->result = KST_PROG_CARD_BAD;
    load("X");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_BAD);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 0);
    f->line = 7;
    load("X");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_BAD);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 7);
    f->result = KST_PROG_CARD_CHECKSUM;
    load("X");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_CHECKSUM);
    f->result = KST_PROG_CARD_NEWER;
    load("X");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_NEWER);
    /* A value past a hard limit: its field and its line. */
    f->result = KST_PROG_CARD_VALUE;
    f->field = KST_F_BOOST;
    f->line = 5;
    load("X");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_VALUE);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 5);
    CHECK_EQ(kst_prog_message(&g_p)->reg, KST_F_BOOST);
    f->result = KST_PROG_CARD_READ_ERR;
    load("X");
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_READ);
    f->result = KST_PROG_CARD_NO_CARD;
    load("X");
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_NONE);
    /* A file that is gone, and a name that is none. */
    load("GONE");
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_READ);
    CHECK_EQ(kst_prog_load(&g_p, ""), KST_PROG_MSG_CARD_READ);
    CHECK_EQ(kst_prog_load(&g_p, NULL), KST_PROG_MSG_CARD_READ);
    CHECK_EQ(kst_prog_load(&g_p, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456"),
             KST_PROG_MSG_CARD_READ);
    CHECK_EQ(kst_prog_changes(&g_p), 0);
}

TEST_CASE(a_file_beyond_a_hard_limit_is_refused_whole)
{
    kst_prog_card_req_t q;
    kst_prog_card_done_t d;

    up();
    (void)file_with("HARD", KST_F_DUTY, 0xFFu);
    load("HARD");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_REFUSED);
    CHECK_EQ(kst_prog_message(&g_p)->arg, KST_H_DUTY_HIGH);
    CHECK_EQ(kst_prog_changes(&g_p), 0);

    /* An answer that changes a locked field is not a file's. */
    CHECK_EQ(kst_prog_load(&g_p, "HARD"), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_take(&g_p, &q));
    CHECK(!kst_prog_card_take(&g_p, &q));
    CHECK_EQ(q.op, KST_PROG_CARD_LOAD);
    CHECK_STR_EQ(q.name, "HARD");
    memset(&d, 0, sizeof(d));
    d.image = q.image;
    d.image.r[KST_REG_NODE_ADDR] ^= 0x01u;
    d.id = q.id + 1u;                 /* not this request's */
    kst_prog_card_done(&g_p, &d);
    CHECK(kst_prog_card_busy(&g_p));
    d.id = q.id;
    kst_prog_card_done(&g_p, &d);
    CHECK(!kst_prog_card_busy(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_BAD);
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    kst_prog_card_done(&g_p, &d);     /* nothing on its way: ignored */
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_BAD);
}

TEST_CASE(a_file_beyond_a_soft_limit_waits_for_the_confirmation)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t duty = kst_field_get(&sv, KST_F_DUTY);

    up();
    CHECK(!kst_prog_soft_is_load(&g_p));
    (void)file_with("UP", KST_F_DUTY, (uint16_t)(duty + 1u));
    load("UP");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SOFT);
    CHECK(kst_prog_soft_is_load(&g_p));
    CHECK_EQ(kst_prog_soft_rules(&g_p), KST_RULE_BIT(KST_S_DUTY_RAISED));
    CHECK_EQ(kst_field_get(kst_prog_soft_image(&g_p), KST_F_DUTY), duty + 1u);
    CHECK_EQ(draft(KST_F_DUTY), duty);
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    kst_prog_soft_cancel(&g_p);
    CHECK(!kst_prog_soft_is_load(&g_p));
    CHECK_EQ(kst_prog_changes(&g_p), 0);

    load("UP");
    CHECK(kst_prog_soft_is_load(&g_p));
    kst_prog_soft_confirm(&g_p);
    CHECK(!kst_prog_soft_is_load(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_LOADED);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 1);
    CHECK_EQ(draft(KST_F_DUTY), duty + 1u);
    /* An edit that waits is not a file. */
    kst_prog_discard(&g_p);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 1u)),
             KST_PROG_EDIT_SOFT);
    CHECK(!kst_prog_soft_is_load(&g_p));
    kst_prog_soft_cancel(&g_p);
    frames(20);
    CHECK_EQ(g_sim.n_writes, 0);
}

TEST_CASE(a_file_equal_to_the_servo_changes_nothing)
{
    const kst_image_t sv = sim_bench_image();
    kst_image_t pair = sv;

    up();
    (void)fake_card_put(&g_fc, "SAME", &sv);
    load("SAME");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SAME);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 0);
    /* Only the pairing value and a protection value differ: both are
     * locked, counted and not written. */
    pair.r[KST_REG_NODE_ADDR] ^= 0x22u;
    pair.r[0x14] ^= 0x01u;
    (void)fake_card_put(&g_fc, "PAIR", &pair);
    load("PAIR");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SAME);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 2);
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NO_CHANGES);
    frames(20);
    CHECK_EQ(g_sim.n_writes, 0);
}

TEST_CASE(a_load_needs_a_servo_that_is_read_and_may_be_written)
{
    const kst_image_t sv = sim_bench_image();

    /* No servo read yet. */
    fresh_at(0u);
    link_up();
    (void)fake_card_put(&g_fc, "F", &sv);
    CHECK_EQ(kst_prog_load(&g_p, "F"), KST_PROG_MSG_NEED_PORT);
    CHECK_EQ(kst_prog_save(&g_p, "G"), KST_PROG_MSG_NEED_READ);
    /* The list needs the card only. */
    list();
    CHECK_EQ(kst_prog_file_count(&g_p, NULL), 1);
    /* The card is out as well: the servo's lack is said first. */
    g_card = false;
    frame();
    CHECK_EQ(kst_prog_load(&g_p, "F"), KST_PROG_MSG_NEED_PORT);
    CHECK(!kst_prog_card_busy(&g_p));

    /* A servo that fails the fingerprint. */
    fresh_at(0u);
    g_sim.reg[0x06] = 0x05u;
    link_up();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    (void)fake_card_put(&g_fc, "F", &sv);
    CHECK_EQ(kst_prog_load(&g_p, "F"), KST_PROG_MSG_READ_ONLY);
    /* Its values are saved all the same. */
    save("RO");
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);

    /* A servo in doubt. */
    by_hand();
    g_f.card = true;
    kst_prog_feed(&g_p, &g_f);
    g_p.need_read = true;
    CHECK_EQ(kst_prog_load(&g_p, "F"), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_save(&g_p, "F"), KST_PROG_MSG_NEED_READ);
    CHECK_EQ(kst_prog_files(&g_p, 0), KST_PROG_MSG_NEED_READ);
    CHECK(!kst_prog_card_busy(&g_p));
    /* Not the servo of the record. */
    by_hand();
    g_p.journal = true;
    g_p.journal_open = true;
    g_p.journal_other = true;
    g_p.journal_port = 6;
    CHECK_EQ(kst_prog_load(&g_p, "F"), KST_PROG_MSG_JOURNAL_OTHER);
    CHECK_EQ(kst_prog_message(&g_p)->arg, 7);
    /* While an operation waits. */
    by_hand();
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_load(&g_p, "F"), BUSY);
    CHECK_EQ(kst_prog_files(&g_p, 0), BUSY);
}

TEST_CASE(a_servo_that_goes_while_its_file_is_read_takes_nothing)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_card_req_t q;
    kst_prog_card_done_t d;

    up();
    (void)file_with("LOW", KST_F_DUTY,
                    (uint16_t)(kst_field_get(&sv, KST_F_DUTY) - 1u));
    CHECK_EQ(kst_prog_load(&g_p, "LOW"), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_take(&g_p, &q));
    CHECK(fake_card_run(&g_fc, &q, &d));
    /* The link goes before the answer. */
    kst_link_lost(&g_k);
    frame();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    kst_prog_card_done(&g_p, &d);
    CHECK(msg() != KST_PROG_MSG_LOADED);
    CHECK_EQ(kst_prog_changes(&g_p), 0);
    CHECK(!kst_prog_card_busy(&g_p));
}

/* ------------------------------------------- the file before the first write */

TEST_CASE(the_first_write_saves_the_servo_to_the_card_first)
{
    const kst_image_t sv = sim_bench_image();

    up_enabled();
    edit(KST_F_DUTY, 200);
    CHECK(kst_prog_save_first(&g_p));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    /* The file, then the write's command. */
    CHECK_EQ(g_fc.saves, 1);
    CHECK_EQ(g_starts_at_save, 2);
    CHECK_EQ(g_starts, 3);
    CHECK_STR_EQ(g_fc.file[0].name, "KST001");
    CHECK(same(&g_fc.file[0].img, &sv));
    CHECK(kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "KST001");
    CHECK(!kst_prog_save_first(&g_p));
    /* Once a session. */
    edit(KST_F_DUTY, 210);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_fc.saves, 1);

    /* Another session: another file, of the servo as it is then. */
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(!kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "");
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_READ);
    CHECK(kst_prog_save_first(&g_p));
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 205);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_fc.saves, 2);
    CHECK_STR_EQ(g_fc.file[1].name, "KST002");
    CHECK_EQ(g_fc.file[1].img.r[KST_REG_DUTY], 210);
    CHECK_EQ(g_fc.file[0].img.r[KST_REG_DUTY], sv.r[KST_REG_DUTY]);
}

TEST_CASE(a_file_saved_by_hand_is_the_sessions_and_no_other_is_written)
{
    up_enabled();
    save("MINE");
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_fc.saves, 1);
    CHECK_EQ(g_fc.n, 1);
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "MINE");
    /* A save after the write holds the written servo: the session's
     * backup stays the file of the servo as it was found. */
    save("LATER");
    CHECK_EQ(g_fc.saves, 2);
    CHECK(kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "MINE");
}

TEST_CASE(a_save_after_the_first_write_does_not_rename_the_backup)
{
    up_enabled();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "KST001");
    save("AFTER");
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "KST001");
    /* Before any write, a second save by hand leaves the first named. */
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    save("ONE");
    save("TWO");
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "ONE");
}

TEST_CASE(a_first_write_with_no_card_goes_without_a_file)
{
    const kst_image_t sv = sim_bench_image();
    kst_prog_restore_t plan;
    kst_image_t now;

    up_enabled();
    g_card = false;
    frame();
    CHECK(kst_prog_save_first(&g_p));
    CHECK(!kst_prog_no_file(&g_p));
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    /* Not said of a write that has not started. */
    CHECK(!kst_prog_no_file(&g_p));
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(servo().r[KST_REG_DUTY], 200);
    CHECK_EQ(g_fc.saves, 0);
    CHECK(kst_prog_no_file(&g_p));
    CHECK(!kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "");
    CHECK(!kst_prog_first_write(&g_p));
    /* The next write still owes the file. */
    CHECK(kst_prog_save_first(&g_p));

    /* The second without a card as well. */
    edit(KST_F_DUTY, 198);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_fc.saves, 0);
    CHECK(kst_prog_no_file(&g_p));

    /* The session's restore, the one copy, with no card and no file. */
    CHECK_EQ(kst_prog_restore_plan(&g_p, KST_PROG_FROM_SESSION, &plan),
             KST_PROG_MSG_NONE);
    CHECK_EQ(kst_prog_restore(&g_p, &plan), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RESTORED);
    now = servo();
    CHECK(same(&now, &sv));
    CHECK_EQ(g_fc.saves, 0);
    CHECK(kst_prog_no_file(&g_p));

    /* The session ends: so does the line. */
    CHECK_EQ(kst_prog_leave(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(!kst_prog_no_file(&g_p));
}

TEST_CASE(a_pairing_release_with_no_card_goes_without_a_file)
{
    up_enabled();
    g_card = false;
    frame();
    CHECK_EQ(kst_prog_release(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_RELEASED);
    CHECK_EQ(g_fc.saves, 0);
    CHECK(kst_prog_no_file(&g_p));
}

TEST_CASE(a_card_put_in_after_a_write_takes_the_servo_as_the_session_found_it)
{
    const kst_image_t sv = sim_bench_image();

    up_enabled();
    g_card = false;
    frame();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);

    /* A save by hand holds the written servo: it is not the backup. */
    g_card = true;
    frame();
    save("NOW");
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    CHECK_EQ(g_fc.file[0].img.r[KST_REG_DUTY], 200);
    CHECK(!kst_prog_backed_up(&g_p));
    CHECK(kst_prog_no_file(&g_p));
    CHECK(kst_prog_save_first(&g_p));

    /* The next write saves the image read before the first one. */
    edit(KST_F_DUTY, 198);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(servo().r[KST_REG_DUTY], 198);
    CHECK_EQ(g_fc.saves, 2);
    CHECK_STR_EQ(g_fc.file[1].name, "KST001");
    CHECK(same(&g_fc.file[1].img, &sv));
    CHECK(kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "KST001");
    CHECK(!kst_prog_no_file(&g_p));
    CHECK(!kst_prog_save_first(&g_p));

    /* And the one after it takes no file. */
    edit(KST_F_DUTY, 196);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(g_fc.saves, 2);
}

/* The link goes and comes back under a session the coprocessor keeps. */
static void link_gap(void)
{
    kst_link_lost(&g_k);
    g_safe = false;
    frames(200);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    g_safe = true;
    kst_link_came_up(&g_k, LINK_PROTOCOL_MINOR);
    settle();
    frames(600);
}

/* A first write with no card, a link loss the coprocessor's session
 * survives, a read, a card: the file the next write saves holds the servo
 * as the session found it, not the written one. */
TEST_CASE(a_session_kept_over_a_link_loss_saves_the_servo_as_found)
{
    const kst_image_t sv = sim_bench_image();

    up_enabled();
    g_card = false;
    frame();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK(kst_prog_no_file(&g_p));

    link_gap();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(!kst_prog_have_servo(&g_p));
    CHECK(!kst_prog_save_first(&g_p));
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(kst_prog_have_servo(&g_p));
    CHECK_EQ(kst_prog_servo(&g_p)->r[KST_REG_DUTY], 200);
    CHECK(g_p.f.have_backup);
    CHECK(same(&g_p.f.backup, &sv));

    /* A save by hand holds the written servo: it is not the backup. */
    g_card = true;
    frame();
    save("NOW");
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    CHECK_EQ(g_fc.file[0].img.r[KST_REG_DUTY], 200);
    CHECK(!kst_prog_backed_up(&g_p));
    CHECK(kst_prog_save_first(&g_p));
    CHECK(!kst_prog_save_none(&g_p));

    /* The next write saves the image the session began with. */
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 198);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(servo().r[KST_REG_DUTY], 198);
    CHECK_EQ(g_fc.saves, 2);
    CHECK(same(&g_fc.file[1].img, &sv));
    CHECK(kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), g_fc.file[1].name);
    CHECK(!kst_prog_no_file(&g_p));
}

/* The same gap in a session that wrote nothing: the servo is as found, and
 * a save by hand is the session's backup. */
TEST_CASE(a_save_by_hand_after_a_link_loss_is_the_backup_of_an_unwritten_servo)
{
    const kst_image_t sv = sim_bench_image();

    up();
    link_gap();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK(kst_prog_have_servo(&g_p));
    save("A");
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    CHECK(same(&g_fc.file[0].img, &sv));
    CHECK(kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "A");
    CHECK(!kst_prog_save_first(&g_p));
}

/* The coprocessor starts again in the gap: its session and its backup are
 * gone, and the session that follows saves the servo as it finds it. */
TEST_CASE(a_session_that_ended_over_a_link_loss_is_followed_by_one_with_its_own_file)
{
    const kst_image_t sv = sim_bench_image();

    up_enabled();
    g_card = false;
    frame();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK(kst_prog_no_file(&g_p));

    kst_link_lost(&g_k);
    g_safe = false;
    frames(200);
    port_fresh();
    g_safe = true;
    kst_link_came_up(&g_k, LINK_PROTOCOL_MINOR);
    settle();
    frames(600);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PWM);
    CHECK(!kst_prog_no_file(&g_p));

    g_card = true;
    CHECK_EQ(kst_prog_connect(&g_p, CH), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(kst_prog_have_servo(&g_p));
    CHECK(g_p.wrote_known);
    CHECK_EQ(kst_prog_servo(&g_p)->r[KST_REG_DUTY], 200);
    /* The backup held is this session's. */
    frames(600);
    CHECK(g_p.f.have_backup);
    CHECK(same(&g_p.f.backup, kst_prog_servo(&g_p)));
    CHECK(kst_prog_save_first(&g_p));
    CHECK(!kst_prog_save_none(&g_p));
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 198);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_fc.saves, 1);
    /* What this session found: the servo the one before it wrote. */
    CHECK_EQ(g_fc.file[0].img.r[KST_REG_DUTY], 200);
    CHECK(!same(&g_fc.file[0].img, &sv));
    CHECK(kst_prog_backed_up(&g_p));
    CHECK(!kst_prog_no_file(&g_p));
}

TEST_CASE(a_card_put_in_after_a_write_and_full_ends_the_next_write)
{
    static const struct {
        uint8_t result;
        unsigned msg;
    } k[] = {
        { KST_PROG_CARD_FULL, KST_PROG_MSG_CARD_FULL },
        { KST_PROG_CARD_WRITE_ERR, KST_PROG_MSG_CARD_WRITE },
        /* Pulled between the request and the file. */
        { KST_PROG_CARD_NO_CARD, KST_PROG_MSG_CARD_NONE },
    };

    up_enabled();
    g_card = false;
    frame();
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    g_card = true;
    frame();
    edit(KST_F_DUTY, 198);
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        const unsigned starts = g_starts;

        g_fc.fails = k[i].result;
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        settle();
        CHECK_EQ(msg(), k[i].msg);
        CHECK_EQ(g_starts, starts);
        CHECK_EQ(servo().r[KST_REG_DUTY], 200);
        CHECK(kst_prog_no_file(&g_p));
        CHECK(!kst_prog_backed_up(&g_p));
    }
}

TEST_CASE(the_file_after_a_write_is_of_the_backup_while_the_session_holds_it)
{
    kst_prog_card_req_t req;
    char text[KST_PROG_FILE_MAX];

    /* The session holds its backup: the file is of it. */
    by_hand();
    g_p.backed_up = false;
    g_p.no_file = true;
    g_p.written = true;
    g_f.card = true;
    CHECK(kst_field_edit(&g_f.backup, KST_F_DUTY, 190));
    pass(1);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_take(&g_p, &req));
    CHECK_EQ(req.op, KST_PROG_CARD_SAVE);
    CHECK(req.preset);
    CHECK(same(&req.image, &g_f.backup));
    CHECK(!same(&req.image, kst_prog_servo(&g_p)));

    /* It does not, after a write with no card or in a session whose
     * writes are not all known: the servo as read is not the servo as
     * found, and the write goes with no file. */
    for (unsigned i = 0; i < 2u; ++i) {
        by_hand();
        g_p.backed_up = false;
        g_p.no_file = i == 0u;
        g_p.wrote_known = i == 0u;
        g_p.written = true;
        g_f.card = true;
        g_f.have_backup = false;
        g_f.rd.flags &= (uint16_t)~LINK_KS_F_BACKUP;
        pass(1);
        CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
        edit(KST_F_DUTY, 200);
        CHECK(kst_prog_save_first(&g_p));
        CHECK(kst_prog_save_none(&g_p));
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        CHECK(!kst_prog_card_take(&g_p, &req));
        /* The record of the write goes to the card; no image does. */
        CHECK(kst_prog_file_take(&g_p, text, sizeof(text)) != 0u);
        kst_prog_file_done(&g_p, true);
        answer(KST_LINK_START_OK);
        CHECK(kst_prog_no_file(&g_p));
        CHECK(!kst_prog_backed_up(&g_p));
    }

    /* A session that began here and wrote with a file or not at all: the
     * servo as read is the servo as found, with no backup held either. */
    by_hand();
    g_p.backed_up = false;
    g_p.wrote_known = true;
    g_f.card = true;
    g_f.have_backup = false;
    g_f.rd.flags &= (uint16_t)~LINK_KS_F_BACKUP;
    pass(1);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK(!kst_prog_save_none(&g_p));
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_take(&g_p, &req));
    CHECK_EQ(req.op, KST_PROG_CARD_SAVE);
    CHECK(same(&req.image, kst_prog_servo(&g_p)));

    /* A save by hand is of the servo as read, whatever the session
     * holds. */
    by_hand();
    g_p.backed_up = false;
    g_p.no_file = true;
    g_f.card = true;
    CHECK(kst_field_edit(&g_f.backup, KST_F_DUTY, 190));
    pass(1);
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_take(&g_p, &req));
    CHECK(same(&req.image, kst_prog_servo(&g_p)));
}

TEST_CASE(a_write_with_no_card_that_does_not_start_leaves_no_line)
{
    by_hand();
    g_p.backed_up = false;
    pass(1);
    CHECK_EQ(kst_prog_enable(&g_p, true), KST_PROG_MSG_NONE);
    edit(KST_F_DUTY, 200);
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_BUSY);
    CHECK_EQ(msg(), BUSY);
    CHECK(!kst_prog_no_file(&g_p));
    CHECK(kst_prog_first_write(&g_p));
    /* The one that starts does. */
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    answer(KST_LINK_START_OK);
    CHECK(kst_prog_no_file(&g_p));
    CHECK(!kst_prog_first_write(&g_p));
}

TEST_CASE(a_write_without_its_file_is_not_sent)
{
    static const struct {
        uint8_t result;
        unsigned msg;
    } k[] = {
        { KST_PROG_CARD_NO_CARD, KST_PROG_MSG_CARD_NONE },
        { KST_PROG_CARD_FULL, KST_PROG_MSG_CARD_FULL },
        { KST_PROG_CARD_WRITE_ERR, KST_PROG_MSG_CARD_WRITE },
        { KST_PROG_CARD_DENIED, KST_PROG_MSG_CARD_DENIED },
    };

    up_enabled();
    edit(KST_F_DUTY, 200);
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); ++i) {
        g_fc.fails = k[i].result;
        CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
        settle();
        CHECK_EQ(msg(), k[i].msg);
        CHECK_EQ(kst_prog_message(&g_p)->level, KST_PROG_WARN);
        CHECK_EQ(g_starts, 2);
        CHECK_EQ(g_sim.n_writes, 0);
        CHECK(!kst_prog_journal(&g_p));
        CHECK(!kst_prog_in_doubt(&g_p));
        CHECK_EQ(kst_prog_changes(&g_p), 1);
        CHECK(kst_prog_first_write(&g_p));
        CHECK(!kst_prog_backed_up(&g_p));
        CHECK(kst_prog_write_enabled(&g_p));
    }
    CHECK(!kst_prog_no_file(&g_p));
    CHECK_EQ(g_fc.saves, 4);

    /* A card that never answers: KST_PROG_CARD_MS. */
    g_fc.fails = KST_PROG_CARD_OK;
    g_fc.silent = true;
    frame();
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frames(KST_PROG_CARD_MS - 2u);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    frames(3);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_WRITE);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK(kst_prog_first_write(&g_p));

    /* The card is back: the same write goes. */
    g_fc.silent = false;
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(servo().r[KST_REG_DUTY], 200);
}

TEST_CASE(a_save_given_up_sends_no_write)
{
    /* The operator's abort. */
    up_enabled();
    edit(KST_F_DUTY, 200);
    g_fc.silent = true;
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frames(5);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_WORKING);
    kst_prog_abort(&g_p);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    frames(KST_PROG_CARD_MS + 10u);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_aborts, 0);
    CHECK_EQ(g_sim.n_writes, 0);
    CHECK(kst_prog_first_write(&g_p));
    CHECK(!kst_prog_in_doubt(&g_p));

    /* STOP. */
    up_enabled();
    edit(KST_F_DUTY, 200);
    g_fc.silent = true;
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frames(5);
    g_stopped = true;
    frame();
    CHECK_EQ(msg(), KST_PROG_MSG_STOPPED);
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_PROGRAMMING);
    CHECK(!kst_prog_write_enabled(&g_p));
    frames(KST_PROG_CARD_MS + 10u);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);

    /* The link goes. */
    up_enabled();
    edit(KST_F_DUTY, 200);
    g_fc.silent = true;
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frames(5);
    kst_link_lost(&g_k);
    frame();
    CHECK_EQ(kst_prog_state(&g_p), KST_PROG_DOWN);
    frames(KST_PROG_CARD_MS + 10u);
    CHECK_EQ(g_starts, 2);
    CHECK_EQ(g_sim.n_writes, 0);
}

TEST_CASE(no_backup_is_taken_while_an_operation_runs)
{
    kst_link_outcome_t o;

    by_hand();
    g_f.card = true;
    kst_prog_feed(&g_p, &g_f);
    CHECK_EQ(kst_prog_read(&g_p), KST_PROG_MSG_NONE);
    /* The command waits for the control task. */
    CHECK_EQ(kst_prog_save(&g_p, "A"),
             KST_PROG_MSG_START + KST_LINK_START_BUSY);
    CHECK(!kst_prog_card_busy(&g_p));
    answer(KST_LINK_START_OK);
    /* The read runs. */
    CHECK_EQ(kst_prog_save(&g_p, "A"),
             KST_PROG_MSG_START + KST_LINK_START_BUSY);
    CHECK(!kst_prog_card_busy(&g_p));
    memset(&o, 0, sizeof(o));
    o.kind = KST_LINK_OUT_DONE;
    o.op = LINK_KST_OP_READ_ALL;
    o.result = KST_SES_OK;
    o.fail_reg = 0xFF;
    end(&o);
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NONE);
    CHECK(kst_prog_card_busy(&g_p));
}

/* The card ends a save after KST_PROG_CARD_MS: the file is on the card, so
 * it is named, it is the session's backup, and the write that follows asks
 * for no second one. */
TEST_CASE(a_save_that_ends_late_is_the_file_it_made)
{
    kst_prog_card_done_t d;
    unsigned saves;

    /* The save of the first write. */
    up_enabled();
    edit(KST_F_DUTY, 200);
    g_fc.silent = true;
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frames(KST_PROG_CARD_MS + 3u);
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_WRITE);
    CHECK(!kst_prog_backed_up(&g_p));
    CHECK(g_p.late_id != 0u);
    saves = g_fc.saves;
    memset(&d, 0, sizeof(d));
    d.id = g_p.late_id;
    d.result = KST_PROG_CARD_OK;
    snprintf(d.name, sizeof(d.name), "PRESET07");
    kst_prog_card_done(&g_p, &d);
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    CHECK_STR_EQ(kst_prog_card_name(&g_p), "PRESET07");
    CHECK(kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "PRESET07");
    /* The same answer a second time changes nothing. */
    d.name[0] = 'X';
    kst_prog_card_done(&g_p, &d);
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "PRESET07");
    g_fc.silent = false;
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    settle();
    CHECK_EQ(msg(), KST_PROG_MSG_WRITTEN);
    CHECK_EQ(g_fc.saves, saves);

    /* A late answer that failed leaves the session without its file. */
    up_enabled();
    edit(KST_F_DUTY, 200);
    g_fc.silent = true;
    CHECK_EQ(kst_prog_write(&g_p), KST_PROG_MSG_NONE);
    frames(KST_PROG_CARD_MS + 3u);
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_WRITE);
    memset(&d, 0, sizeof(d));
    d.id = g_p.late_id;
    d.result = KST_PROG_CARD_WRITE_ERR;
    kst_prog_card_done(&g_p, &d);
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_WRITE);
    CHECK(!kst_prog_backed_up(&g_p));
    CHECK_EQ(g_p.late_id, 0);

    /* A late answer under another card request names no file of its own,
     * and the request under way keeps its answer. */
    up_enabled();
    g_fc.silent = true;
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NONE);
    frames(KST_PROG_CARD_MS + 3u);
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_WRITE);
    memset(&d, 0, sizeof(d));
    d.id = g_p.late_id;
    d.result = KST_PROG_CARD_OK;
    snprintf(d.name, sizeof(d.name), "A");
    CHECK_EQ(kst_prog_files(&g_p, 0), KST_PROG_MSG_NONE);
    frames(2);
    CHECK(kst_prog_card_busy(&g_p));
    kst_prog_card_done(&g_p, &d);
    CHECK(kst_prog_card_busy(&g_p));
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_WRITE);
    CHECK(kst_prog_backed_up(&g_p));
    CHECK_STR_EQ(kst_prog_backup_name(&g_p), "A");

    /* A servo read anew since is not the image the file holds. */
    up_enabled();
    g_fc.silent = true;
    CHECK_EQ(kst_prog_save(&g_p, "A"), KST_PROG_MSG_NONE);
    frames(KST_PROG_CARD_MS + 3u);
    memset(&d, 0, sizeof(d));
    d.id = g_p.late_id;
    d.result = KST_PROG_CARD_OK;
    snprintf(d.name, sizeof(d.name), "A");
    g_p.servo.r[KST_REG_DUTY] ^= 1u;
    kst_prog_card_done(&g_p, &d);
    CHECK_EQ(msg(), KST_PROG_MSG_SAVED);
    CHECK(!kst_prog_backed_up(&g_p));

    /* A load given up leaves nothing to end late. */
    up_enabled();
    g_fc.silent = true;
    CHECK_EQ(kst_prog_files(&g_p, 0), KST_PROG_MSG_NONE);
    frames(KST_PROG_CARD_MS + 3u);
    CHECK_EQ(msg(), KST_PROG_MSG_CARD_READ);
    CHECK_EQ(g_p.late_id, 0);
}

TEST_CASE(a_file_confirmed_is_the_value_confirmed)
{
    const kst_image_t sv = sim_bench_image();
    const uint16_t duty = kst_field_get(&sv, KST_F_DUTY);

    up();
    (void)file_with("UP", KST_F_DUTY, (uint16_t)(duty + 2u));
    load("UP");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SOFT);
    kst_prog_soft_confirm(&g_p);
    CHECK_EQ(draft(KST_F_DUTY), duty + 2u);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 1u)),
             KST_PROG_EDIT_OK);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 2u)),
             KST_PROG_EDIT_OK);
    CHECK_EQ(kst_prog_edit(&g_p, KST_F_DUTY, (uint16_t)(duty + 3u)),
             KST_PROG_EDIT_SOFT);
    kst_prog_soft_cancel(&g_p);
    /* The file again, over a draft that holds its value: it is a step
     * from the servo and is asked as one. */
    load("UP");
    CHECK_EQ(msg(), KST_PROG_MSG_LOAD_SOFT);
    kst_prog_soft_cancel(&g_p);
}

int main(void)
{
    RUN(a_write_the_backup_file_stopped_leaves_no_field_written);
    RUN(a_save_writes_the_servos_image_under_the_name_given);
    RUN(a_name_the_card_does_not_take_saves_nothing);
    RUN(a_card_that_takes_no_file_says_which_way);
    RUN(the_list_names_the_files_their_types_and_the_next_preset);
    RUN(a_file_becomes_the_draft_and_is_written_as_edits);
    RUN(an_edited_file_is_named_as_one);
    RUN(a_file_of_the_other_servo_type_is_not_loaded);
    RUN(a_file_that_does_not_read_says_where);
    RUN(a_file_beyond_a_hard_limit_is_refused_whole);
    RUN(a_file_beyond_a_soft_limit_waits_for_the_confirmation);
    RUN(a_file_equal_to_the_servo_changes_nothing);
    RUN(a_load_needs_a_servo_that_is_read_and_may_be_written);
    RUN(a_servo_that_goes_while_its_file_is_read_takes_nothing);
    RUN(the_first_write_saves_the_servo_to_the_card_first);
    RUN(a_file_saved_by_hand_is_the_sessions_and_no_other_is_written);
    RUN(a_save_after_the_first_write_does_not_rename_the_backup);
    RUN(a_first_write_with_no_card_goes_without_a_file);
    RUN(a_pairing_release_with_no_card_goes_without_a_file);
    RUN(a_card_put_in_after_a_write_takes_the_servo_as_the_session_found_it);
    RUN(a_session_kept_over_a_link_loss_saves_the_servo_as_found);
    RUN(a_save_by_hand_after_a_link_loss_is_the_backup_of_an_unwritten_servo);
    RUN(a_session_that_ended_over_a_link_loss_is_followed_by_one_with_its_own_file);
    RUN(a_card_put_in_after_a_write_and_full_ends_the_next_write);
    RUN(the_file_after_a_write_is_of_the_backup_while_the_session_holds_it);
    RUN(a_write_with_no_card_that_does_not_start_leaves_no_line);
    RUN(a_write_without_its_file_is_not_sent);
    RUN(a_save_given_up_sends_no_write);
    RUN(no_backup_is_taken_while_an_operation_runs);
    RUN(a_save_that_ends_late_is_the_file_it_made);
    RUN(a_file_confirmed_is_the_value_confirmed);
    return test_summary("kst_prog_card");
}
