/**
 * @file ew_text.h
 * @brief The text interface of ESCape32: its lines and its replies, at
 *        38400baud, with no checksum and no echo.  No function here touches a
 *        wire.
 *
 * A reply ends with a line `OK` or `ERROR`.  A host waits EW_TEXT_FIRST_US for
 * its first byte, EW_TEXT_SAVE_US after a `save`, and takes EW_TEXT_GAP_US with
 * no byte as the end of the reply.  These three are a host's choice.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef EW_TEXT_H
#define EW_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ew_a.h"
#include "ew_id.h"
#include "ew_map.h"
#include "ew_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EW_TEXT_FIRST_US 200000u  /**< the wait for a reply's first byte    */
#define EW_TEXT_SAVE_US  2000000u /**< the wait for the reply of a `save`   */
#define EW_TEXT_GAP_US   20000u   /**< no byte this long ends a reply       */
#define EW_TEXT_LIVE_MAX 11u      /**< the most characters of a live value  */

/** @brief What `info` says.  The live values stay as the ESC spelled them. */
typedef struct {
    bool     rev_read;  /**< the revision reads `rev<N>.<M>`                */
    uint8_t  rev;       /**< N; 0 when not read or above 255                */
    uint16_t sub;       /**< M                                              */
    char     target[EW_ID_NAME_MAX + 1u]; /**< the target name, cut to fit  */
    char     temp[EW_TEXT_LIVE_MAX + 1u];     /**< "25C"                    */
    char     temp_ext[EW_TEXT_LIVE_MAX + 1u]; /**< "", or the second sensor */
    char     volt[EW_TEXT_LIVE_MAX + 1u];     /**< with its unit            */
    char     curr[EW_TEXT_LIVE_MAX + 1u];     /**< with its unit            */
    char     csum[EW_TEXT_LIVE_MAX + 1u];     /**< with its unit            */
    char     erpm[EW_TEXT_LIVE_MAX + 1u];     /**< the number               */
} ew_text_info_t;

/**
 * @brief A line of one word: `info`, `show`, `save`.
 * @param out  takes the line, its LF last
 * @param size bytes at @p out
 * @param word the word
 * @return its bytes, 0 when it does not fit
 */
size_t ew_text_word(uint8_t *out, size_t size, const char *word);

/**
 * @brief The line `set <key> <value>`.
 * @param out  takes the line, its LF last
 * @param size bytes at @p out
 * @param key  the key
 * @param v    the value
 * @return its bytes, 0 when it does not fit
 */
size_t ew_text_set(uint8_t *out, size_t size, const char *key, int16_t v);

/**
 * @brief A decimal number as the wire writes it: an optional minus, no leading
 *        zero, -32767 to 32767.
 * @param s the characters
 * @param n their count
 * @param v takes the number
 * @return true when the characters are one
 */
bool ew_text_num(const uint8_t *s, size_t n, int16_t *v);

/**
 * @brief Judge a whole reply: printable characters and LFs, its last line `OK`
 *        or `ERROR`.
 * @param c    the capture
 * @param rx   the bytes received
 * @param body takes the bytes before the last line
 * @return EW_ANS_OK, EW_ANS_TEXT_ERROR, else what went wrong: EW_ANS_LENGTH for
 *         a reply with no last line, EW_ANS_OTHER for a byte no text has
 */
ew_ans_t ew_text_reply(const ew_capture_t *c, const uint8_t *rx, size_t *body);

/**
 * @brief Read the lines of `info`.  Live values may be absent or in any order.
 * @param body the reply without its last line
 * @param n    its bytes
 * @param out  takes the result
 * @return true when the first line starts `ESCape32 rev`
 */
bool ew_text_info(const uint8_t *body, size_t n, ew_text_info_t *out);

/**
 * @brief Whether `info` names a revision this module writes.
 * @param info what `info` said
 * @return true for revision EW_E32_REVISION
 */
bool ew_text_rev_known(const ew_text_info_t *info);

/**
 * @brief Read the lines of `show` by key, into a page.  A key that does not
 *        come reads EW_KEY_NONE.
 * @param body  the reply without its last line
 * @param n     its bytes
 * @param page  takes EW_MAP_LEN_E32 bytes
 * @param music takes `music`, cut to EW_E32_MUSIC_MAX characters and a NUL, may
 *              be NULL
 * @return true when every key came once, with a number that fits, and nothing
 *         else came
 */
bool ew_text_show(const uint8_t *body, size_t n, uint8_t *page, char *music);

/**
 * @brief Read the reply of `set`: the one line `<key>: <value>`.
 * @param body the reply without its last line
 * @param n    its bytes
 * @param key  the key sent
 * @param v    takes the value stored
 * @return true when the reply is that line
 */
bool ew_text_set_reply(const uint8_t *body, size_t n, const char *key,
                       int16_t *v);

#ifdef __cplusplus
}
#endif

#endif /* EW_TEXT_H */
