/*
 * A card for the servo programmer's tests: the directory in memory behind
 * the 5 functions of kst_store_io_t, and kst_card_run() on it.
 *
 * A file is kept as what a test states of it: an image, a kind and a type.
 * Its text is made by kst_file_format() when the store reads it, and a text
 * the store writes is read back by kst_file_parse(), so every answer passes
 * the code the panel runs.  A file may also be a text given as it is.
 *
 * A test of the model alone sets an answer instead: fake_card_t.fails for
 * every request, fake_file_t.result for a load of one file.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "kst_card.h"
#include "kst_file.h"
#include "kst_prog.h"
#include "kst_store.h"

#define FAKE_CARD_FILES 210u

typedef struct {
    char name[KST_PROG_NAME_MAX];   /* the base name */
    kst_image_t img;
    uint8_t kind;       /* kst_prog_file_kind_t the text is written as */
    uint8_t type;       /* 0: the text names none */
    uint8_t result;     /* not 0: the answer to a load of it */
    uint8_t field;      /* with result */
    uint16_t line;      /* with result */
    const char *text;   /* not NULL: the file's text as it is */
    const char *as;     /* not NULL: the directory's name for it */
    bool dir;           /* a directory */
} fake_file_t;

typedef struct {
    uint8_t fails;      /* kst_prog_card_result_t every request gets */
    bool silent;        /* no answer */
    bool no_card;       /* nothing is mounted */
    bool walk_fails;    /* the directory does not read to its end */
    bool read_fails;
    bool full;          /* no space for a new file */
    bool write_fails;
    unsigned pull_at;   /* not 0: the card is gone from this card call of
                           the request on, counted from 1 */
    unsigned calls;     /* card calls of the request */
    unsigned walks;     /* directory walks; a test sets it to 0 */
    unsigned walk_fails_at; /* not 0: this walk and each later one does
                               not read to its end, counted from 1 */
    unsigned saves, lists, loads;
    unsigned n;
    fake_file_t file[FAKE_CARD_FILES];
    kst_store_io_t io;
} fake_card_t;

static inline bool fake_card_same(const char *a, const char *b)
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

/* The name the directory has for @p f. */
static inline void fake_card_entry(const fake_file_t *f, char *out, size_t n)
{
    if (f->as != NULL) {
        snprintf(out, n, "%s", f->as);
    } else {
        snprintf(out, n, "%s%s", f->name, KST_STORE_EXT);
    }
}

/* The file by its base name. */
static inline fake_file_t *fake_card_find(fake_card_t *c, const char *name)
{
    for (unsigned i = 0; i < c->n; ++i) {
        if (strcmp(c->file[i].name, name) == 0) {
            return &c->file[i];
        }
    }
    return NULL;
}

/* The file by the name the directory has for it. */
static inline fake_file_t *fake_card_entry_find(fake_card_t *c,
                                                const char *entry)
{
    char name[64];

    for (unsigned i = 0; i < c->n; ++i) {
        fake_card_entry(&c->file[i], name, sizeof(name));
        if (!c->file[i].dir && fake_card_same(name, entry)) {
            return &c->file[i];
        }
    }
    return NULL;
}

/* Whether a line of a written text is one an edited text of the kind
 * leaves out.  An edited text is a person's lines: it keeps the type line
 * when it states a type and never the register block, so each value is the
 * one its line converts to. */
static inline bool fake_card_drops(const char *l, uint8_t kind, bool typed)
{
    if (kind == KST_PROG_FILE_VERIFIED) {
        return false;
    }
    if (strncmp(l, "Type:", 5) == 0) {
        return !typed;
    }
    return strncmp(l, "checksum", 8) == 0 || strncmp(l, "registers", 9) == 0
           || (isxdigit((unsigned char)l[0]) && isxdigit((unsigned char)l[1])
               && l[2] == ':');
}

/* The text of @p f; returns its length. */
static inline size_t fake_card_text(const fake_file_t *f, char *out,
                                    size_t cap)
{
    char full[KST_FILE_MAX];
    const bool typed = f->type != 0u && f->kind < KST_PROG_FILE_UNTYPED;
    const uint8_t kind = typed ? f->kind : (uint8_t)KST_PROG_FILE_UNTYPED;
    size_t len, n = 0, at = 0;

    if (f->text != NULL) {
        len = strlen(f->text);
        memcpy(out, f->text, len < cap ? len : cap);
        return len;
    }
    len = kst_file_format(&f->img, full, sizeof(full));
    while (at < len) {
        size_t end = at;

        while (end < len && full[end] != '\n') {
            end++;
        }
        end++;
        if (!fake_card_drops(&full[at], kind, typed) && n + (end - at) <= cap) {
            memcpy(&out[n], &full[at], end - at);
            n += end - at;
        }
        at = end;
    }
    return n;
}

static inline bool fake_card_gone(fake_card_t *c)
{
    c->calls++;
    return c->no_card || (c->pull_at != 0u && c->calls >= c->pull_at);
}

static inline bool fake_card_mounted(void *ctx)
{
    return !fake_card_gone((fake_card_t *)ctx);
}

static inline bool fake_card_walk(void *ctx, kst_store_visit_fn visit,
                                  void *vctx)
{
    fake_card_t *c = (fake_card_t *)ctx;
    char name[64];

    bool fails;

    if (fake_card_gone(c)) {
        return false;
    }
    c->walks++;
    fails = c->walk_fails
            || (c->walk_fails_at != 0u && c->walks >= c->walk_fails_at);
    for (unsigned i = 0; i < c->n; ++i) {
        if (fails && i >= c->n / 2u) {
            return false;
        }
        fake_card_entry(&c->file[i], name, sizeof(name));
        visit(name, c->file[i].dir, vctx);
    }
    return !fails;
}

static inline int32_t fake_card_size(void *ctx, const char *name)
{
    fake_card_t *c = (fake_card_t *)ctx;
    const fake_file_t *f = fake_card_entry_find(c, name);
    char text[KST_FILE_MAX];

    if (fake_card_gone(c) || f == NULL) {
        return -1;
    }
    return (int32_t)fake_card_text(f, text, f->text != NULL ? 0u
                                                             : sizeof(text));
}

static inline int32_t fake_card_read(void *ctx, const char *name, char *buf,
                                     size_t cap)
{
    fake_card_t *c = (fake_card_t *)ctx;
    const fake_file_t *f = fake_card_entry_find(c, name);
    size_t len;

    if (fake_card_gone(c) || f == NULL || c->read_fails) {
        return -1;
    }
    len = fake_card_text(f, buf, cap);
    return (int32_t)(len < cap ? len : cap);
}

static inline kst_store_io_result_t fake_card_create(void *ctx,
                                                     const char *name,
                                                     const char *data,
                                                     size_t len)
{
    fake_card_t *c = (fake_card_t *)ctx;
    kst_file_t file;
    fake_file_t *f;
    size_t base = strlen(name);

    if (fake_card_gone(c) || c->write_fails) {
        return KST_STORE_IO_ERROR;
    }
    if (fake_card_entry_find(c, name) != NULL) {
        return KST_STORE_IO_EXISTS;
    }
    if (c->full || c->n >= FAKE_CARD_FILES) {
        return KST_STORE_IO_FULL;
    }
    /* What is written reads back as a whole servo file, or the card's side
     * wrote none. */
    if (kst_file_parse(data, len, &file, NULL) != KST_FILE_OK
        || file.kind != KST_FILE_VERIFIED
        || base <= strlen(KST_STORE_EXT)
        || base - strlen(KST_STORE_EXT) >= KST_PROG_NAME_MAX) {
        return KST_STORE_IO_ERROR;
    }
    f = &c->file[c->n++];
    memset(f, 0, sizeof(*f));
    memcpy(f->name, name, base - strlen(KST_STORE_EXT));
    f->img = file.image;
    f->kind = KST_PROG_FILE_VERIFIED;
    f->type = (uint8_t)kst_card_type(&file.image);
    return KST_STORE_IO_OK;
}

static inline void fake_card_init(fake_card_t *c)
{
    memset(c, 0, sizeof(*c));
    c->io.ctx = c;
    c->io.mounted = fake_card_mounted;
    c->io.walk = fake_card_walk;
    c->io.size = fake_card_size;
    c->io.read = fake_card_read;
    c->io.create = fake_card_create;
}

/* A file as a save writes it. */
static inline fake_file_t *fake_card_put(fake_card_t *c, const char *name,
                                         const kst_image_t *img)
{
    fake_file_t *f = &c->file[c->n++];

    memset(f, 0, sizeof(*f));
    snprintf(f->name, sizeof(f->name), "%s", name);
    f->img = *img;
    f->kind = KST_PROG_FILE_VERIFIED;
    f->type = (uint8_t)kst_card_type(img);
    return f;
}

/* A file with the text @p text, which outlives the card. */
static inline fake_file_t *fake_card_put_text(fake_card_t *c,
                                              const char *name,
                                              const char *text)
{
    fake_file_t *f = &c->file[c->n++];

    memset(f, 0, sizeof(*f));
    snprintf(f->name, sizeof(f->name), "%s", name);
    f->text = text;
    return f;
}

/* False: no answer. */
static inline bool fake_card_run(fake_card_t *c, const kst_prog_card_req_t *q,
                                 kst_prog_card_done_t *d)
{
    const fake_file_t *f;

    memset(d, 0, sizeof(*d));
    d->id = q->id;
    d->field = 0xFFu;
    if (q->op == KST_PROG_CARD_SAVE) {
        c->saves++;
    } else if (q->op == KST_PROG_CARD_LIST) {
        c->lists++;
    } else {
        c->loads++;
    }
    if (c->silent) {
        return false;
    }
    if (c->fails != KST_PROG_CARD_OK) {
        d->result = c->fails;
        return true;
    }
    f = q->op == KST_PROG_CARD_LOAD ? fake_card_find(c, q->name) : NULL;
    if (f != NULL && f->result != KST_PROG_CARD_OK) {
        d->result = f->result;
        d->kind = f->kind;
        d->type = f->type;
        d->field = f->field;
        d->line = f->line;
        return true;
    }
    c->calls = 0;
    kst_card_run(&c->io, q, d);
    return true;
}
