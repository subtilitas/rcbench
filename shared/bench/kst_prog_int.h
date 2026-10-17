/*
 * The programmer's private seam between kst_prog.c, kst_prog_card.c and
 * kst_prog_file.c: the wait and file-write kinds the state holds, and the
 * helpers of kst_prog.c that the card part and the journal file part call.
 * Nothing outside shared/bench includes this header; the programmer's
 * interface is kst_prog.h.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "kst_prog.h"

/* What is awaited. */
enum {
    W_NONE = 0,
    W_SAVE,      /* the servo's file, before the first write      */
    W_FILE,      /* the journal's file, before the request goes   */
    W_ANSWER,    /* the control task's answer to the command      */
    W_OUTCOME,   /* the operation's outcome                       */
};

/* What a write of the file is for. */
enum {
    FILE_NONE = 0,
    FILE_KEEP,     /* the state changed; nobody waits            */
    FILE_JOURNAL,  /* a write to the servo waits                 */
};

bool kst_prog_same(const kst_image_t *a, const kst_image_t *b);
unsigned kst_prog_lowest(uint64_t set);
unsigned kst_prog_ones(uint64_t set);
bool kst_prog_field_editable(kst_field_id_t id);
kst_field_set_t kst_prog_differ(const kst_image_t *a, const kst_image_t *b);
kst_rules_t kst_prog_hard_new(const kst_prog_t *p,
                              const kst_image_t *target);
kst_rules_t kst_prog_soft_asks(const kst_prog_t *p, const kst_image_t *from,
                               const kst_image_t *target);
const kst_image_t *kst_prog_file_image(const kst_prog_t *p);
kst_prog_msg_id_t kst_prog_say_reg(kst_prog_t *p, unsigned id, unsigned arg,
                                   unsigned reg);
kst_prog_msg_id_t kst_prog_say(kst_prog_t *p, unsigned id, unsigned arg);
void kst_prog_send(kst_prog_t *p, const kst_link_request_t *req);
void kst_prog_card_ask(kst_prog_t *p, kst_prog_card_op_t op,
                       const char *name);
void kst_prog_write_go(kst_prog_t *p);
bool kst_prog_journal_classify(kst_prog_t *p);
void kst_prog_not_started(kst_prog_t *p, unsigned id, unsigned arg);
void kst_prog_load_take(kst_prog_t *p, const kst_image_t *target);
