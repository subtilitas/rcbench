/**
 * @file
 * @brief The generic label printer engine: templates, the image stream and
 *        the print job a prn_spec_t describes.
 *
 * Parts:
 * - templates: a frame from a template, its arguments and variables, with
 *   the length and checksum tokens filled in last (prn_tmpl.c);
 * - image: head lines packed as the file says, pages, chunk frames and
 *   row frames (prn_image.c);
 * - job: the step machine that runs a QUERY or a PRINT over one link, one
 *   write at a time, with the answer joiner, the fields, the error flags,
 *   STOP on an end inside the abort range, the relink and the verdict
 *   (prn_job.c).
 *
 * Compression is the caller's: with `compress lzma_lit` it runs the
 * literal-only encoder of protocols/supvan (lzlit.h) over the pages and
 * hands the job the stream.  The engine itself calls no function outside
 * C's memcpy(), memset(), memcmp() and strlen().
 *
 * Pure C: no heap, no clock read, no I/O.  Times are unsigned differences
 * of a free-running 32-bit ms clock the caller passes in.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PRN_JOB_H
#define PRN_JOB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "prn_spec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------ templates */

/** @brief What a template is rendered with. */
typedef struct {
    uint32_t arg[PRN_ARGS];       /**< a, p0, p1, p2 */
    uint64_t var[PRN_V_COUNT];    /**< variables by prn_var_t */
    const uint8_t *data;          /**< the {data} bytes, or NULL */
    uint16_t data_len;            /**< their count */
    uint16_t data_pad;            /**< pad bytes after them */
    uint8_t pad_byte;             /**< the pad byte */
} prn_ctx_t;

/**
 * @brief Render a token run into a frame.
 *
 * Bytes, arguments, variables and data go in first; then the length
 * tokens, then the checksum tokens, each over the finished frame.  A
 * value too large for its encoding, a range outside the frame or a frame
 * over @p cap refuses the render; @p out is then not a frame.
 *
 * @param s   the spec
 * @param seq the tokens
 * @param ctx the values
 * @param out the frame
 * @param cap its size in bytes
 * @param len set to the frame's length
 * @return true when the frame is whole
 */
bool prn_tmpl_render(const prn_spec_t *s, prn_tseq_t seq,
                     const prn_ctx_t *ctx, uint8_t *out, size_t cap,
                     size_t *len);

/**
 * @brief A checksum over bytes.
 * @param kind prn_ck_t, not PRN_CK_SVPAGE
 * @param p    the bytes
 * @param n    their count
 * @return the checksum, in its low 8, 16 or 32 bits
 */
uint32_t prn_ck(uint8_t kind, const uint8_t *p, size_t n);

/**
 * @brief Bytes of a checksum kind.
 * @param kind prn_ck_t
 * @return 1, 2 or 4; 0 for no kind
 */
unsigned prn_ck_width(uint8_t kind);

/**
 * @brief Bytes of an encoding.
 * @param enc prn_enc_t
 * @return 1, 2 or 4; 0 for no encoding
 */
unsigned prn_enc_width(uint8_t enc);

/* ---------------------------------------------------------------- image */

/**
 * @brief Pack one head line from the engine's form into the file's.
 *
 * The engine's form: head_dots dots, dot d in bit d % 8 of byte d / 8,
 * 1 black, ceil(head_dots / 8) bytes.  The file's: line_bytes bytes, dot 0
 * in bit 0 (`lsb`) or bit 7 (`msb`), 1 or 0 black, the byte order reversed
 * when `reverse yes`; bytes past the head's dots are white.
 *
 * @param s   the spec
 * @param in  the line in the engine's form
 * @param out line_bytes bytes
 */
void prn_line_pack(const prn_spec_t *s, const uint8_t *in, uint8_t *out);

/**
 * @brief Pages for an image of @p lines head lines.
 * @param s     the spec, with pages
 * @param lines the lines
 * @return ceil(lines / page_lines); 0 for 0 lines or a spec without pages
 */
uint32_t prn_page_count(const prn_spec_t *s, uint32_t lines);

/**
 * @brief Build page @p index of an image.
 *
 * The page head, then the page's lines, the rest of the page 0.  In the
 * head, `{lines}` is the lines of this page and `{pflags}` the first flag
 * on page 0, the last on the last page, both on a single page.  A
 * `{ck:svpage:le16}` token is the 16-bit sum of the head's other bytes
 * plus byte i x 256 - 1 of the page for each i from 1 while i x 256 is at
 * most the head and data length (the Supvan buffer rule).
 *
 * @param s     the spec, with pages
 * @param ctx   the job's variables (mlead, mtrail, density, ...); lines,
 *              pflags and the data are set here
 * @param bits  the image, lines x line_bytes, packed (prn_line_pack())
 * @param lines its lines
 * @param index the page, from 0
 * @param out   page_size bytes
 * @return false when @p index is past the last page or the head does not
 *         render; @p out is then not a page
 */
bool prn_page_fill(const prn_spec_t *s, prn_ctx_t *ctx,
                   const uint8_t *bits, uint32_t lines, uint32_t index,
                   uint8_t *out);

/**
 * @brief Data frames of a stream.
 * @param s   the spec
 * @param len the stream's bytes: with chunks the stream, with rows the
 *            packed lines
 * @return chunks (ceil(len / chunk size)) or rows (len / line_bytes); 0
 *         for an empty stream, one over chunk max, or rows not whole
 */
uint32_t prn_frame_count(const prn_spec_t *s, uint32_t len);

/**
 * @brief Build data frame @p index of a stream: the chunk frame around
 *        chunk @p index ({idx}, {total}), padded when the file says, or
 *        the row frame around line @p index ({line_no}).
 * @param s      the spec
 * @param ctx    the job's variables; data and the frame variables are set
 *               here
 * @param stream the stream
 * @param len    its length
 * @param index  the frame, from 0
 * @param out    PRN_FRAME_MAX bytes
 * @param n      set to the frame's length
 * @return false when @p index is past the last frame or the frame does
 *         not render
 */
bool prn_frame_build(const prn_spec_t *s, prn_ctx_t *ctx,
                     const uint8_t *stream, uint32_t len, uint32_t index,
                     uint8_t *out, size_t *n);

/**
 * @brief The speed table's value.
 * @param s     the spec
 * @param input mean bytes a page, or the stream length, as the table says
 * @return the value of the first pair whose threshold @p input exceeds,
 *         else the table's else value; 0 without a table
 */
uint16_t prn_speed(const prn_spec_t *s, uint32_t input);

/* ------------------------------------------------------------------ job */

/** @brief The stream a PRINT sends, and what it was built for. */
typedef struct {
    const uint8_t *data; /**< the stream, or the packed lines for rows */
    uint32_t len;        /**< its length in bytes */
    uint32_t pages;      /**< pages it holds, 0 without pages */
    uint32_t lines;      /**< head lines of the image */
    uint8_t w_mm;        /**< the roll's width it was laid out for, 0 any */
    uint8_t h_mm;        /**< the roll's height, 0 any */
} prn_stream_t;

/** @brief What a job does. */
typedef enum {
    PRN_JOB_QUERY, /**< the file's `job query` steps */
    PRN_JOB_PRINT  /**< the file's `job print` steps and the verdict */
} prn_job_kind_t;

/** @brief A job's phase. */
typedef enum {
    PRN_PH_IDLE = 0, /**< never started */
    PRN_PH_STEP,     /**< in a step of the job's list */
    PRN_PH_VERDICT,  /**< reading the verdict */
    PRN_PH_STOP,     /**< sending the abort template after an end */
    PRN_PH_RELINK,   /**< waiting relink_ms for the link */
    PRN_PH_END       /**< ended; prn_job_t.err says how */
} prn_phase_t;

/** @brief How a write goes. */
typedef enum {
    PRN_W_REQ = 0, /**< a write request: the link answers it */
    PRN_W_NR       /**< a write without response */
} prn_write_t;

/** @brief The longest job, in ms. */
#define PRN_JOB_MS 120000u

/** @brief A job.  Read the documented fields; the rest are private. */
typedef struct {
    const prn_spec_t *spec;  /**< the printer */
    uint8_t kind;            /**< prn_job_kind_t */
    uint8_t phase;           /**< prn_phase_t */
    uint8_t err;             /**< prn_err_t once phase is PRN_PH_END */
    bool relinked;           /**< the verdict came after a link drop */
    bool counted;            /**< the counter verdict read its reads and
                                  the counter stayed */
    uint8_t pos;             /**< the step's position in the job's list */
    uint32_t frame;          /**< data frames sent */
    uint32_t frames;         /**< data frames of the stream */
    uint32_t fields_have;    /**< bit per prn_field_name_t read */
    uint64_t field[PRN_F_NAMES]; /**< the last value of each field */
    uint32_t rx_bad;         /**< joined frames dropped by tail or check */
    /* private */
    prn_stream_t z;          /**< the stream */
    uint8_t end_err;         /**< the end the abort template carries */
    bool want;               /**< a command is due */
    bool await;              /**< an answer is awaited */
    uint8_t await_key;       /**< its key */
    uint8_t tries;           /**< sends in the step */
    uint8_t reads;           /**< verdict reads */
    uint8_t relink_to;       /**< where a relink goes */
    bool armed;              /**< a write of the abort range went out */
    bool relink_armed;       /**< a write of a relink step went out */
    bool link;               /**< the link is up */
    bool in_flight;          /**< a write is out, its answer not in */
    uint8_t out_kind;        /**< 1 command, 2 data, 3 abort */
    uint8_t out_pos;         /**< the step position that wrote it */
    bool data_due;           /**< a data fragment is due */
    uint16_t frag_at;        /**< offset of the next fragment */
    uint16_t frame_len;      /**< length of the frame being sent */
    uint32_t t_job;          /**< the job started */
    uint32_t t_step;         /**< the step entered */
    uint32_t t_sent;         /**< the last write handed out */
    uint32_t t_due;          /**< the next write is due */
    uint16_t rx_n;           /**< bytes held by the joiner */
    uint16_t rx_done;        /**< length of the frame last given out */
    uint16_t cmd_len;        /**< length of the command written */
    uint8_t rx[PRN_RX_MAX];  /**< the joiner's bytes */
    prn_ctx_t ctx;           /**< the values templates render with; the
                                  saved variables c0 to v3 live here */
    uint8_t cmd[PRN_CMD_MAX];     /**< the command written */
    uint8_t buf[PRN_FRAME_MAX];   /**< the data frame being written */
} prn_job_t;

/**
 * @brief Start a job on a link that is up.
 * @param j      the job
 * @param s      the spec; it must outlive the job
 * @param kind   QUERY or PRINT
 * @param z      the stream for a PRINT, copied (its bytes are not); NULL
 *               for a QUERY
 * @param now_ms the clock
 * @return false for a spec that fails prn_spec_check(), a PRINT without a
 *         stream, an empty one, one of more frames than the file allows,
 *         or one without pages where the speed table needs them; the job
 *         is then not started
 */
bool prn_job_start(prn_job_t *j, const prn_spec_t *s, prn_job_kind_t kind,
                   const prn_stream_t *z, uint32_t now_ms);

/**
 * @brief Feed notification bytes from the printer.
 * @param j      the job
 * @param data   the bytes of one notification
 * @param n      their count
 * @param now_ms the clock
 */
void prn_job_rx(prn_job_t *j, const uint8_t *data, size_t n,
                uint32_t now_ms);

/**
 * @brief The answer to the write prn_job_tx() handed out last: for a
 *        write request the link's answer, for a write without response
 *        the host stack taking it.
 * @param j      the job
 * @param ok     false: refused
 * @param now_ms the clock
 */
void prn_job_written(prn_job_t *j, bool ok, uint32_t now_ms);

/**
 * @brief The link went down or came back.
 * @param j      the job
 * @param up     the link's state
 * @param now_ms the clock
 */
void prn_job_link(prn_job_t *j, bool up, uint32_t now_ms);

/**
 * @brief Stop the job: PRN_ABORTED, with the abort template once when a
 *        write of the abort range went out and the job is inside it.
 * @param j      the job
 * @param now_ms the clock
 */
void prn_job_abort(prn_job_t *j, uint32_t now_ms);

/**
 * @brief The next write, when one is due and none is out.
 * @param j      the job
 * @param now_ms the clock
 * @param data   set to the bytes, valid until the next prn_job_* call
 * @param n      set to their count
 * @param kind   set to how it goes
 * @return true when a write is handed out
 */
bool prn_job_tx(prn_job_t *j, uint32_t now_ms, const uint8_t **data,
                uint16_t *n, prn_write_t *kind);

/**
 * @brief Run the timeouts.  Call every poll.
 * @param j      the job
 * @param now_ms the clock
 */
void prn_job_tick(prn_job_t *j, uint32_t now_ms);

/**
 * @brief Whether the job has ended.
 * @param j the job
 * @return true at PRN_PH_END
 */
bool prn_job_ended(const prn_job_t *j);

/**
 * @brief The last value of a field.
 * @param j    the job
 * @param name prn_field_name_t
 * @param v    set to the value
 * @return false when no answer has carried it
 */
bool prn_job_field(const prn_job_t *j, uint8_t name, uint64_t *v);

#ifdef __cplusplus
}
#endif

#endif /* PRN_JOB_H */
