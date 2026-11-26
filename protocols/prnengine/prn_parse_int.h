/**
 * @file
 * @brief The printer file reader's state and the helpers its two files
 *        share (prn_parse.c, prn_parse_ext.c).  Private to the module.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PRN_PARSE_INT_H
#define PRN_PARSE_INT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "prn_spec.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Tokens of a line: the key, a sub-key or name, 40 values. */
#define PP_TOKENS (PRN_VALUES_MAX + 2u)

/** @brief Keys that appear once, as bits of pp_t.seen. */
typedef enum {
    K_ID = 0, K_NAME, K_VERSION, K_LICENCE, K_SERVICE, K_CCCD, K_MTU,
    K_CONN, K_HEAD, K_DOTS, K_LINE, K_FEED, K_DOT0, K_ALIGN, K_MARGIN,
    K_RSOURCE, K_RLIMITS, K_DENSITY, K_PSIZE, K_PLINES, K_PHEAD, K_PFLAGS,
    K_COMPRESS, K_CSIZE, K_CPAD, K_CMAX, K_CFRAME, K_ROWS, K_WCMD, K_WDATA,
    K_SPEED, K_RXJOIN, K_RXTAIL, K_RXCHECK, K_RXKEY, K_QUERY, K_PRINT,
    K_ABORT, K_RELINKMS, K_ANSWERMS, K_VERDICT, K_CMD,
    K_COUNT /**< number of keys */
} pp_key_t;

/** @brief Step sub-keys that appear once, as bits of pp_t.sub. */
typedef enum {
    S_SEND = 0, S_DATA, S_WAIT, S_EXPECT, S_POLL, S_UNTIL, S_WITHIN,
    S_TRIES, S_ELSE, S_FLAGS, S_RELINK, S_ROLL, S_DELAY
} pp_sub_t;

/** @brief The reader's state: about 300 bytes. */
typedef struct {
    prn_spec_t *s;                 /**< the spec being filled */
    const char *fname;             /**< the file's name, or NULL */
    char line[PRN_LINE_MAX + 1u];  /**< the line, NUL-terminated */
    uint8_t at[PP_TOKENS];         /**< each token's offset in line */
    uint8_t len[PP_TOKENS];        /**< each token's length */
    uint8_t nt;                    /**< tokens */
    uint16_t lineno;               /**< the line's number */
    uint64_t seen;                 /**< pp_key_t bits */
    uint16_t sub;                  /**< pp_sub_t bits of the open step */
    bool in_step;                  /**< a step is open */
    uint16_t step_line;            /**< its line */
    uint16_t relink_line[PRN_STEPS]; /**< each step's relink line */
    uint16_t dot0_line;            /**< label dot0 */
    uint16_t rsource_line;         /**< roll source */
    uint8_t nmatch_kind[4];        /**< match lines by prn_mkind_t */
} pp_t;

/** @brief A token's text; p->len[i] bytes, not NUL-terminated. */
#define PP_T(p, i) (&(p)->line[(p)->at[(i)]])

/**
 * @brief Whether token @p i is @p word.
 * @param p    the state
 * @param i    the token
 * @param word the word
 * @return true when equal
 */
bool pp_is(const pp_t *p, unsigned i, const char *word);

/**
 * @brief Read a number from text: decimal or 0x hexadecimal, at most 10
 *        characters.
 * @param t  the text
 * @param n  its length
 * @param lo the least value
 * @param hi the largest value
 * @param v  set to the value
 * @return 0, or 7 (E07) when it is no number or out of range
 */
uint8_t pp_num_text(const char *t, size_t n, uint32_t lo, uint32_t hi,
                    uint32_t *v);

/**
 * @brief Read token @p i as a number, decimal or 0x hexadecimal, at most
 *        10 characters.
 * @param p  the state
 * @param i  the token
 * @param lo the least value
 * @param hi the largest value
 * @param v  set to the value
 * @return 0, or 7 (E07) when it is no number or out of range
 */
uint8_t pp_num(const pp_t *p, unsigned i, uint32_t lo, uint32_t hi,
               uint32_t *v);

/**
 * @brief Read hexadecimal bytes: two digits each.
 * @param t   the text
 * @param n   its length
 * @param out the bytes
 * @param max room in @p out
 * @return the bytes read, 0 when the text is not 2 to 2 x max digits
 */
unsigned pp_hex(const char *t, size_t n, uint8_t *out, unsigned max);

/**
 * @brief The first @p c in @p n bytes of @p t.
 * @param t the text
 * @param n its length
 * @param c the character
 * @return its offset, or @p n when it is not there
 */
size_t pp_find(const char *t, size_t n, char c);

/**
 * @brief Whether a key was seen; mark it seen.
 * @param p the state
 * @param k the key
 * @return 0, or 8 (E08) when it was seen before
 */
uint8_t pp_once(pp_t *p, pp_key_t k);

/**
 * @brief Mark a key seen and hold the line to a token count.
 * @param p        the state
 * @param k        the key
 * @param nt       the count
 * @param at_least the count is a least count
 * @return 0, 8 (E08) when seen before, or 7 (E07) for another count
 */
uint8_t pp_once_n(pp_t *p, pp_key_t k, unsigned nt, bool at_least);

/**
 * @brief Whether a key was seen.
 * @param p the state
 * @param k the key
 * @return true when seen
 */
bool pp_has(const pp_t *p, pp_key_t k);

/**
 * @brief The index of token @p i in a NULL-ended word list.
 * @param p     the state
 * @param i     the token
 * @param words the list
 * @return the index, or -1
 */
int pp_tok_word(const pp_t *p, unsigned i, const char *const *words);

/**
 * @brief Read the page keys: size, lines, head, flags (prn_parse_img.c).
 * @param p the state, the line in p->line
 * @return 0, a code, or 255 for another key
 */
uint8_t pp_page(pp_t *p);

/**
 * @brief Read compress, rows and the chunk keys (prn_parse_img.c).
 * @param p the state, the line in p->line
 * @return 0, a code, or 255 for another key
 */
uint8_t pp_frames(pp_t *p);

/**
 * @brief Read write cmd, write data and table speed (prn_parse_img.c).
 * @param p the state, the line in p->line
 * @return 0, a code, or 255 for another key
 */
uint8_t pp_writes(pp_t *p);

/**
 * @brief Read the rx keys: join, tail, check, key (prn_parse_img.c).
 * @param p the state, the line in p->line
 * @return 0, a code, or 255 for another key
 */
uint8_t pp_rx(pp_t *p);

/**
 * @brief Read relink_ms and answer_ms (prn_parse_img.c).
 * @param p the state, the line in p->line
 * @return 0, a code, or 255 for another key
 */
uint8_t pp_times(pp_t *p);

/**
 * @brief Read a line of a template, answer, field, error, step, job,
 *        abort, verdict or step sub-line (prn_parse_ext.c).
 * @param p the state; p->nt >= 1
 * @return 0, a code, or 255 when the key is none of these
 */
uint8_t pp_line_ext(pp_t *p);

/**
 * @brief Close the open step: its lines must fit together (E24).
 * @param p the state
 * @return 0 or a code
 */
uint8_t pp_close_step(pp_t *p);

/**
 * @brief Resolve the relink targets named by name and check the roll
 *        source against the fields, at `end`.
 * @param p    the state
 * @param line set to the line of a refusal
 * @return 0 or a code
 */
uint8_t pp_finish_ext(pp_t *p, uint16_t *line);

/**
 * @brief Read a template's tokens from token @p from on.
 * @param p     the state
 * @param from  the first token
 * @param ctx   where it is used: 0 tmpl, 1 page head, 2 chunk frame,
 *              3 rows, 4 rx check
 * @param max   tokens at most
 * @param seq   set to the run
 * @return 0 or a code
 */
uint8_t pp_tokens(pp_t *p, unsigned from, unsigned ctx, unsigned max,
                  prn_tseq_t *seq);

#define PP_C_TMPL   0u /**< pp_tokens() context: a named template */
#define PP_C_PAGE   1u /**< pp_tokens() context: the page head */
#define PP_C_CHUNK  2u /**< pp_tokens() context: the chunk frame */
#define PP_C_ROWS   3u /**< pp_tokens() context: the row frame */
#define PP_C_CHECK  4u /**< pp_tokens() context: the rx check */

/**
 * @brief Whether a run's ranges lie inside frames of @p lo and @p hi
 *        bytes and every length fits its encoding.
 * @param s   the spec
 * @param seq the run
 * @param lo  the shortest frame
 * @param hi  the longest frame
 * @return true when they do
 */
bool pp_ranges_ok(const prn_spec_t *s, prn_tseq_t seq, size_t lo,
                  size_t hi);

/**
 * @brief Bytes a run takes without its data token.
 * @param s   the spec
 * @param seq the run
 * @return the bytes
 */
size_t pp_fixed_len(const prn_spec_t *s, prn_tseq_t seq);

/** @brief Field names in prn_field_name_t order, NULL-ended. */
extern const char *const pp_fields[];
/** @brief Variable names in prn_var_t order from 1, NULL-ended. */
extern const char *const pp_vars[];

#ifdef __cplusplus
}
#endif

#endif /* PRN_PARSE_INT_H */
