/*
 * The card reader's side of fuzz_parity.py: parses each profile file named
 * on stdin with esc_profile_parse() and prints one line for it,
 *
 *     <path> TAB BAD TAB <the reason>, the parser's, or that the id is
 *                        not the file's name (esc_profile_file_is())
 *     <path> TAB OK  TAB <every parsed field, in a fixed order>
 *
 * Text fields are printed as hexadecimal bytes between quotes, so a tab, a
 * newline or a quote in one cannot be taken for the line's own.  The order
 * and the spelling of the fields are fuzz_parity.py's dump(), which prints
 * the generator's result the same way; the two lines are compared as text.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esc_profile.h"

static void hx(const char *s)
{
    printf("'");
    for (const unsigned char *c = (const unsigned char *)(s ? s : ""); *c; ++c) {
        printf("%02x", *c);
    }
    printf("'");
}

static void dump(const esc_profile_t *p)
{
    hx(p->id);
    hx(p->brand);
    hx(p->family);
    printf("|%d %d %d %d|", p->scheme, p->encoding, p->announce,
           p->automatable);
    hx(p->automatable_note);
    printf("|%d %d %u|%d %u|%d %u|%d %d %d|%u %u %u %u %d %d %d|",
           p->entry_throttle, p->entry_after_power,
           (unsigned)p->entry_hold_ms, p->select_throttle,
           (unsigned)p->select_within_ms, p->value_select_throttle,
           (unsigned)p->value_select_within_ms, p->skip_throttle,
           p->listen_throttle, p->store_throttle, p->long_equals_short,
           (unsigned)p->beep_ms, (unsigned)p->gap_ms,
           (unsigned)p->group_gap_ms, p->repeat, p->one_change_per_entry,
           p->verified);
    printf("S%u", p->step_count);
    for (unsigned i = 0; i < p->step_count; ++i) {
        hx(p->steps[i]);
    }
    printf("|M%u", p->model_count);
    for (unsigned i = 0; i < p->model_count; ++i) {
        const esc_model_t *m = &p->models[i];
        hx(m->name);
        printf("(%u %u %d %u %u %u)", m->cells_min, m->cells_max, m->nimh,
               (unsigned)m->v_max_mv, m->current_a, (unsigned)m->v_min_mv);
    }
    printf("|I%u", p->item_count);
    for (unsigned i = 0; i < p->item_count; ++i) {
        const esc_item_t *it = &p->items[i];
        hx(it->name);
        hx(it->key);
        printf("(%u)V%u", it->number, it->value_count);
        for (unsigned k = 0; k < it->value_count; ++k) {
            const esc_value_t *v = &it->values[k];
            hx(v->name);
            printf("(%u %d %d %u A%u", v->number, v->is_default,
                   v->entry_throttle, (unsigned)v->entry_hold_ms,
                   v->after_count);
            for (unsigned a = 0; a < v->after_count; ++a) {
                printf(" %d", v->after[a]);
            }
            printf(")");
        }
        printf("P%u", it->applies_count);
        for (unsigned a = 0; a < it->applies_count; ++a) {
            hx(it->applies_to[a]);
        }
        hx(it->applies_when);
    }
    printf("|H%u", p->manual_count);
    for (unsigned i = 0; i < p->manual_count; ++i) {
        const esc_manual_t *m = &p->manual[i];
        printf("(%d ", m->when);
        hx(m->action);
        printf(" %u ", (unsigned)m->hold_ms);
        hx(m->action_de);
        printf(" %d %d)", m->starts_menu, m->locks);
    }
}

int main(void)
{
    char path[4096];
    while (fgets(path, sizeof(path), stdin) != NULL) {
        path[strcspn(path, "\n")] = '\0';
        FILE *f = fopen(path, "rb");
        if (f == NULL) {
            printf("%s\tNOFILE\t\n", path);
            continue;
        }
        /* Parsed from a block of the file's own size, so the sanitizer
         * build sees a read 1 byte past the text. */
        static char in[ESC_PROFILE_MAX_BYTES + 4096u];
        const size_t n = fread(in, 1, sizeof(in), f);
        fclose(f);
        char *buf = malloc(n > 0 ? n : 1);
        if (buf == NULL) {
            return 2;
        }
        memcpy(buf, in, n);
        esc_profile_t p;
        void *block = NULL;
        char err[160] = "";
        const char *name = strrchr(path, '/');
        name = name != NULL ? name + 1 : path;
        if (!esc_profile_parse(buf, n, &p, &block, err, sizeof(err))) {
            printf("%s\tBAD\t%s\n", path, err);
        } else if (!esc_profile_file_is(name, p.id)) {
            /* The panel refuses a card file its id does not name. */
            printf("%s\tBAD\tid: differs from the file name\n", path);
            free(block);
        } else {
            printf("%s\tOK\t", path);
            dump(&p);
            printf("\n");
            free(block);
        }
        free(buf);
    }
    return 0;
}
