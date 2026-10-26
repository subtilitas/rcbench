/**
 * @file
 * @brief The programmer's private seam between kst_prog.c, kst_prog_card.c and
 *        kst_prog_file.c: the wait and file-write kinds the state holds, and
 *        the helpers of kst_prog.c that the card part and the journal file part
 *        call.
 *
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

/**
 * @brief Whether two images hold the same values.
 *
 * @param a The first image.
 * @param b The second image.
 * @return true when they are equal.
 */
bool kst_prog_same(const kst_image_t *a, const kst_image_t *b);
/**
 * @brief The index of the lowest field bit set in a set.
 *
 * @param set A set of fields, one bit each.
 * @return The index of the lowest bit set in set.
 */
unsigned kst_prog_lowest(uint64_t set);
/**
 * @brief The number of field bits set in a set.
 *
 * @param set A set of fields, one bit each.
 * @return The number of bits set in set.
 */
unsigned kst_prog_ones(uint64_t set);
/**
 * @brief Whether a field may be edited.
 *
 * @param id The field.
 * @return true when the field may be edited.
 */
bool kst_prog_field_editable(kst_field_id_t id);
/**
 * @brief The fields in which two images differ.
 *
 * @param a The first image.
 * @param b The second image.
 * @return The set of fields that differ.
 */
kst_field_set_t kst_prog_differ(const kst_image_t *a, const kst_image_t *b);
/**
 * @brief The hard rules the target image breaks.
 *
 * @param p      The programmer state.
 * @param target The image to check.
 * @return The rules broken.
 */
kst_rules_t kst_prog_hard_new(const kst_prog_t *p,
                              const kst_image_t *target);
/**
 * @brief The soft rules an edit from one image to another asks about.
 *
 * @param p      The programmer state.
 * @param from   The image before the edit.
 * @param target The image after the edit.
 * @return The soft rules the edit asks a confirmation for.
 */
kst_rules_t kst_prog_soft_asks(const kst_prog_t *p, const kst_image_t *from,
                               const kst_image_t *target);
/**
 * @brief The image the file before a write takes: the servo as the session
 *         found it.
 *
 * @param p The programmer state.
 * @return The image the file takes.
 */
const kst_image_t *kst_prog_file_image(const kst_prog_t *p);
/**
 * @brief Sets the message with an argument and a register.
 *
 * @param p   The programmer state.
 * @param id  The message id.
 * @param arg The argument of the message.
 * @param reg The register the message names.
 * @return The message id.
 */
kst_prog_msg_id_t kst_prog_say_reg(kst_prog_t *p, unsigned id, unsigned arg,
                                   unsigned reg);
/**
 * @brief Sets the message with an argument.
 *
 * @param p   The programmer state.
 * @param id  The message id.
 * @param arg The argument of the message.
 * @return The message id.
 */
kst_prog_msg_id_t kst_prog_say(kst_prog_t *p, unsigned id, unsigned arg);
/**
 * @brief Queues a command for the task that owns the link module.
 *
 * @param p   The programmer state.
 * @param req The request to send.
 */
void kst_prog_send(kst_prog_t *p, const kst_link_request_t *req);
/**
 * @brief Asks the card side for a save, a list or a load.
 *
 * @param p    The programmer state.
 * @param op   The card operation.
 * @param name The file name, or NULL.
 */
void kst_prog_card_ask(kst_prog_t *p, kst_prog_card_op_t op,
                       const char *name);
/**
 * @brief Starts the write that is held: its journal, its file, then the
 *         request.
 *
 * @param p The programmer state.
 */
void kst_prog_write_go(kst_prog_t *p);
/**
 * @brief The start gate of the held write as it is now, the wait for its
 *        own files left out: the port in PROGRAMMING and idle, the port the
 *        write was held for, no STOP, the bench not ARMED and the switch
 *        on.
 *
 * @param p The programmer state.
 * @return KST_PROG_MSG_NONE when the write may go, else the message why not.
 */
kst_prog_msg_id_t kst_prog_held_gate(const kst_prog_t *p);
/**
 * @brief Classifies the held journal record against the servo read.
 *
 * @param p The programmer state.
 * @return true when the record is another servo's.
 */
bool kst_prog_journal_classify(kst_prog_t *p);
/**
 * @brief Ends a command that was not taken, with its message.
 *
 * @param p   The programmer state.
 * @param id  The message id.
 * @param arg The argument of the message.
 */
void kst_prog_not_started(kst_prog_t *p, unsigned id, unsigned arg);
/**
 * @brief Takes a loaded file's target into the draft.
 *
 * @param p      The programmer state.
 * @param target The file's target image.
 */
void kst_prog_load_take(kst_prog_t *p, const kst_image_t *target);
