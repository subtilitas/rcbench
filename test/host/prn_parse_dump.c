/*
 * The C side of prn_parity.py: parses each printer file named on stdin
 * with prn_parse(), the file's own name given for the id rule, and prints
 * one line for it,
 *
 *     <path> TAB BAD TAB E<code> <line>
 *     <path> TAB OK  TAB <every parsed field, in a fixed order>
 *
 * Texts are printed as hexadecimal bytes, so no byte of a file can be
 * taken for the line's own.  The order and the spelling are
 * tools/printer_files.py's dump(), which prints the Python reader's result
 * the same way; the two lines are compared as text.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prn_spec.h"

/* A text field as hexadecimal bytes between quotes. */
static void hx(const char *s)
{
    printf("'");
    for (const unsigned char *c = (const unsigned char *)s; *c; ++c) {
        printf("%02x", *c);
    }
    printf("' ");
}

/* Bytes as hexadecimal. */
static void bytes(const uint8_t *b, unsigned n)
{
    printf("h");
    for (unsigned i = 0; i < n; ++i) {
        printf("%02x", b[i]);
    }
    printf(" ");
}

/* A token run. */
static void seq(const prn_spec_t *s, prn_tseq_t q)
{
    printf("[");
    for (unsigned i = 0; i < q.n; ++i) {
        const prn_tok_t *t = &s->tok[q.first + i];
        printf("%u.%u.%d.%d,", t->type, t->x, t->a, t->b);
    }
    printf("] ");
}

/* A send. */
static void send(const prn_send_t *d)
{
    printf("S%u:%lu/%u:%lu/%u:%lu/%u:%lu/%u ", d->tmpl, (unsigned long)d->n[0],
           d->var[0], (unsigned long)d->n[1], d->var[1],
           (unsigned long)d->n[2], d->var[2], (unsigned long)d->n[3],
           d->var[3]);
}

/* A condition. */
static void cond(const prn_cond_t *c)
{
    printf("C%u:%u:%lu:%u:%u ", c->field, c->op, (unsigned long)c->n, c->var,
           c->err);
}

static void dump(const prn_spec_t *s)
{
    hx(s->id);
    hx(s->name);
    hx(s->licence);
    printf("v%u src%u ", s->version, s->nsource);
    for (unsigned i = 0; i < s->nsource; ++i) {
        hx(s->source[i]);
    }
    printf("m%u ", s->nmatch);
    for (unsigned i = 0; i < s->nmatch; ++i) {
        printf("%u:", s->match[i].kind);
        bytes(s->match[i].b, s->match[i].len);
    }
    bytes(s->service.b, s->service.len);
    for (unsigned i = 0; i < PRN_CHRS; ++i) {
        printf("c%u:%u:", s->chr[i].used, s->chr[i].props);
        bytes(s->chr[i].uuid.b, s->chr[i].uuid.len);
    }
    printf("%u %u %u %u %u %u|", s->cccd_next, s->mtu_min, s->conn[0],
           s->conn[1], s->conn[2], s->conn[3]);
    printf("%u %u %u %u %u %u %u %u %u %u %u %u|", s->head_dots, s->dots_mm,
           s->line_bytes, s->line_msb, s->line_black0, s->line_reverse,
           s->feed, s->dot0, s->align_center, s->align_dot, s->mlead,
           s->mtrail);
    printf("%u %u ", s->roll_fixed, s->nroll);
    for (unsigned i = 0; i < s->nroll; ++i) {
        printf("%u.%u.%u ", s->roll[i].w, s->roll[i].h, s->roll[i].gap);
    }
    printf("%u %u %u %u %u|", s->roll_lim[0], s->roll_lim[1], s->roll_lim[2],
           s->roll_lim[3], s->density);
    printf("%u %u ", s->page_size, s->page_lines);
    seq(s, s->page_head);
    printf("%u %u %u %lu %u %u %u %u ", s->page_flags[0], s->page_flags[1],
           s->lzma, (unsigned long)s->lzma_dict, s->chunk_size, s->chunk_pad,
           s->chunk_pad_byte, s->chunk_max);
    seq(s, s->frame);
    printf("%u %u %u %u %u %u ", s->cmd_nr, s->data_nr, s->frag, s->gap_ms,
           s->speed_stream, s->nspeed);
    for (unsigned i = 0; i < s->nspeed; ++i) {
        printf("%lu:%u ", (unsigned long)s->speed_above[i], s->speed_value[i]);
    }
    printf("e%u|T%u ", s->speed_else, s->ntmpl);
    for (unsigned i = 0; i < s->ntmpl; ++i) {
        hx(s->tmpl[i].name);
        seq(s, s->tmpl[i].seq);
    }
    printf("J%u ", s->join);
    bytes(s->magic, s->magic_len);
    printf("%u %u %u %u %u ", s->len_off, s->len_enc, s->len_add, s->rx_min,
           s->rx_max);
    bytes(s->tail, s->tail_len);
    printf("%u %u.%u.%d.%d k%u|A%u ", s->has_rx_check, s->rx_check.type,
           s->rx_check.x, s->rx_check.a, s->rx_check.b, s->key_off,
           s->nanswer);
    for (unsigned i = 0; i < s->nanswer; ++i) {
        printf("%u:%u ", s->answer_key[i], s->answer_min[i]);
    }
    printf("F%u ", s->nfield);
    for (unsigned i = 0; i < s->nfield; ++i) {
        const prn_field_t *f = &s->field[i];
        printf("%u.%u.%u.%u.%u ", f->key, f->name, f->off, f->kind, f->n);
    }
    printf("E%u ", s->nerror);
    for (unsigned i = 0; i < s->nerror; ++i) {
        printf("%u:%u ", s->error_field[i], s->error_code[i]);
    }
    printf("|P%u ", s->nstep);
    for (unsigned i = 0; i < s->nstep; ++i) {
        const prn_step_t *st = &s->step[i];
        hx(st->name);
        hx(st->relink_name);
        printf("k%u ", st->kind);
        send(&st->send);
        printf("%u %u %u %u %u:%u %u %u %u %u %u ", st->wait_ms, st->poll_ms,
               st->within_ms, st->delay_ms, st->has_expect, st->expect,
               st->tries, st->else_err, st->flags, st->roll, st->relink);
        for (unsigned k = 0; k < st->nuntil; ++k) {
            cond(&st->until[k]);
        }
        printf("/ ");
        for (unsigned k = 0; k < st->ncheck; ++k) {
            cond(&st->check[k]);
        }
        printf("/ ");
        for (unsigned k = 0; k < st->nsave; ++k) {
            printf("%u=%u ", st->save_var[k], st->save_field[k]);
        }
        printf("; ");
    }
    printf("Q");
    for (unsigned i = 0; i < s->nquery; ++i) {
        printf("%u,", s->query[i]);
    }
    printf(" R");
    for (unsigned i = 0; i < s->nprint; ++i) {
        printf("%u,", s->print[i]);
    }
    printf(" a%u ", s->has_abort);
    send(&s->abort);
    printf("%u %u %u %u|V%u %u %u %u %u ", s->abort_from, s->abort_to,
           s->relink_ms, s->answer_ms, s->verdict, s->vd_field, s->vd_key,
           s->vd_reads, s->vd_gap_ms);
    send(&s->vd_send);
    for (unsigned i = 0; i < s->nresult; ++i) {
        printf("%lu,", (unsigned long)s->result[i]);
    }
    printf("\n");
}

int main(void)
{
    static char path[4096];
    static char buf[PRN_FILE_MAX + 2u];
    static prn_spec_t spec;
    while (fgets(path, sizeof path, stdin) != NULL) {
        path[strcspn(path, "\r\n")] = '\0';
        if (path[0] == '\0') {
            continue;
        }
        FILE *f = fopen(path, "rb");
        if (f == NULL) {
            printf("%s\tBAD\tE00 0\n", path);
            continue;
        }
        const size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        const char *base = strrchr(path, '/');
        base = (base != NULL) ? base + 1 : path;
        prn_perr_t err;
        if (!prn_parse(buf, n, base, &spec, &err)) {
            printf("%s\tBAD\tE%02u %u\n", path, err.code, err.line);
            continue;
        }
        printf("%s\tOK\t", path);
        dump(&spec);
    }
    return 0;
}
