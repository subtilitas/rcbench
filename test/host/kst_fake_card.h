/*
 * A card for the KST servo programmer's tests: the files in memory, and the
 * answers of the card's side to a save, a list and a load.  A file is an
 * image and what the card's side says of it; no text is read here.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdio.h>
#include <string.h>

#include "kst_card.h"
#include "kst_prog.h"

#define FAKE_CARD_FILES 12u

typedef struct {
    char name[KST_PROG_NAME_MAX];
    kst_image_t img;
    uint8_t kind;       /* kst_prog_file_kind_t */
    uint8_t type;       /* 0: the file names none */
    uint8_t result;     /* what a load of it says before any compare */
    uint8_t field;
    uint16_t line;
} fake_file_t;

typedef struct {
    uint8_t fails;      /* kst_prog_card_result_t every request gets */
    bool silent;        /* no answer */
    unsigned saves, lists, loads;
    unsigned n;
    fake_file_t file[FAKE_CARD_FILES];
} fake_card_t;

static inline void fake_card_init(fake_card_t *c)
{
    memset(c, 0, sizeof(*c));
}

static inline fake_file_t *fake_card_find(fake_card_t *c, const char *name)
{
    for (unsigned i = 0; i < c->n; ++i) {
        if (strcmp(c->file[i].name, name) == 0) {
            return &c->file[i];
        }
    }
    return NULL;
}

/* The next free preset name; "" when none is left. */
static inline void fake_card_preset(fake_card_t *c, char *out, size_t n)
{
    for (unsigned i = 1; i <= 999u; ++i) {
        snprintf(out, n, "KST%03u", i);
        if (fake_card_find(c, out) == NULL) {
            return;
        }
    }
    out[0] = '\0';
}

static inline bool fake_card_name_ok(const char *s)
{
    const size_t len = strlen(s);

    if (len == 0u || len > kst_card_name_max()) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        const char ch = s[i];

        if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
              || ch == '-' || ch == '_')) {
            return false;
        }
    }
    return true;
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

/* False: no answer. */
static inline bool fake_card_run(fake_card_t *c, const kst_prog_card_req_t *q,
                                 kst_prog_card_done_t *d)
{
    fake_file_t *f;

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
    switch (q->op) {
    case KST_PROG_CARD_SAVE:
        if (q->preset) {
            fake_card_preset(c, d->name, sizeof(d->name));
            if (d->name[0] == '\0' || c->n >= FAKE_CARD_FILES) {
                d->result = KST_PROG_CARD_FULL;
                return true;
            }
        } else if (!fake_card_name_ok(q->name)) {
            d->result = KST_PROG_CARD_NAME;
            return true;
        } else if (fake_card_find(c, q->name) != NULL) {
            d->result = KST_PROG_CARD_EXISTS;
            return true;
        } else if (c->n >= FAKE_CARD_FILES) {
            d->result = KST_PROG_CARD_FULL;
            return true;
        } else {
            snprintf(d->name, sizeof(d->name), "%s", q->name);
        }
        (void)fake_card_put(c, d->name, &q->image);
        return true;
    case KST_PROG_CARD_LIST:
        d->total = (uint16_t)c->n;
        for (unsigned i = q->first;
             i < c->n && d->count < KST_PROG_FILES_MAX; ++i) {
            snprintf(d->names[d->count], sizeof(d->names[0]), "%s",
                     c->file[i].name);
            d->types[d->count] = c->file[i].type;
            d->count++;
        }
        fake_card_preset(c, d->name, sizeof(d->name));
        return true;
    default:
        f = fake_card_find(c, q->name);
        if (f == NULL) {
            d->result = KST_PROG_CARD_READ_ERR;
            return true;
        }
        d->kind = f->kind;
        d->type = f->type;
        if (f->result != KST_PROG_CARD_OK) {
            d->result = f->result;
            d->field = f->field;
            d->line = f->line;
            return true;
        }
        if (f->type != 0u && f->type != kst_card_type(&q->image)) {
            d->result = KST_PROG_CARD_TYPE;
            d->servo_type = (uint8_t)kst_card_type(&q->image);
            return true;
        }
        d->skipped = kst_card_merge(&q->image, &f->img, &d->image);
        return true;
    }
}
