/**
 * @file
 * @brief A label printer description: the parsed form of a printer file
 *        (format 1, protocols/printers/README.md) and its reader.
 *
 * A printer file is text: one key and its values a line, a step's lines
 * indented under it, `end` last.  prn_parse() reads it in one pass into a
 * prn_spec_t of fixed size, with no heap.  A file it refuses gives a code
 * (E01 to E30) and the line, and leaves the spec zeroed.  Nothing in a file
 * is executed, looked up as a symbol or used as a path: every kind a file
 * names is one of the enumerations below.
 *
 * Pure C: no heap, no clock read, no I/O.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PRN_SPEC_H
#define PRN_SPEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The file format this reader takes. */
#define PRN_FORMAT 1u

/** @brief Limits of a file and of the spec, in bytes or entries. @{ */
#define PRN_FILE_MAX     8192u /**< a file */
#define PRN_LINE_MAX     120u  /**< a line before its line end */
#define PRN_LINES_MAX    256u  /**< lines of a file */
#define PRN_VALUES_MAX   40u   /**< values on a line after its key */
#define PRN_ID_MAX       23u   /**< characters of an id */
#define PRN_NAME_MAX     23u   /**< characters of a name or a name match */
#define PRN_LICENCE_MAX  23u   /**< characters of a licence id */
#define PRN_SOURCE_MAX   100u  /**< characters of a source URL */
#define PRN_SOURCES      3u    /**< source lines */
#define PRN_MATCH_EACH   4u    /**< match lines of oui, name and iname each */
#define PRN_MATCH_SVC    2u    /**< match svc lines */
#define PRN_MATCHES      14u   /**< match lines in all */
#define PRN_CHRS         4u    /**< characteristics */
#define PRN_ROLLS        8u    /**< roll fixed lines */
#define PRN_TMPLS        16u   /**< named templates */
#define PRN_TMPL_NAME    11u   /**< characters of a template or step name */
#define PRN_TMPL_TOKENS  40u   /**< tokens of one template */
#define PRN_TOKENS       384u  /**< tokens of all templates together */
#define PRN_SPEED_PAIRS  12u   /**< pairs of the speed table */
#define PRN_ANSWERS      8u    /**< answer lines */
#define PRN_FIELDS       24u   /**< field lines */
#define PRN_ERRORS       12u   /**< error lines */
#define PRN_STEPS        24u   /**< steps, and steps of one job */
#define PRN_STEP_SAVES   4u    /**< save lines of a step */
#define PRN_STEP_CHECKS  4u    /**< check lines of a step */
#define PRN_RESULTS      4u    /**< values of verdict result */
#define PRN_RX_MAX       128u  /**< the longest answer */
#define PRN_CHUNK_MAX    1024u /**< the largest chunk */
#define PRN_LINE_BYTES   128u  /**< the longest head line */
#define PRN_CMD_MAX      160u  /**< the longest command: 40 tokens of 4B */
#define PRN_FRAME_MAX    (PRN_CHUNK_MAX + PRN_CMD_MAX) /**< a data frame */
#define PRN_SPEC_MAX     6144u /**< sizeof(prn_spec_t) at most */
/** @} */

/** @brief Ends of a job, and what a file's `else`, `check` and `error`
 *         lines may name (COVER to ROLL but WRITE and LINK).  The first 15
 *         values are those of sv_err_t in protocols/supvan. */
typedef enum {
    PRN_OK = 0,      /**< done; a PRINT printed one label */
    PRN_COVER,       /**< cover open */
    PRN_NO_LABEL,    /**< no label */
    PRN_LABEL,       /**< label error */
    PRN_BATTERY,     /**< printer battery low */
    PRN_HOT,         /**< print head hot */
    PRN_BUSY,        /**< busy */
    PRN_SILENT,      /**< an answer or a deadline missed */
    PRN_WRITE,       /**< the link refused a write */
    PRN_LINK,        /**< the link dropped */
    PRN_NOT_PRINTED, /**< the verdict says no label came out */
    PRN_ROLL,        /**< the roll is not the one the label was laid out for,
                          or it changed */
    PRN_SMALL,       /**< the roll is under the file's limits */
    PRN_LARGE,       /**< the roll is over them, or the stream too long */
    PRN_ABORTED,     /**< prn_job_abort() */
    PRN_ENGINE,      /**< the file asked for something the job cannot do:
                          a value too large for its encoding, a range
                          outside its frame, the 120s job limit */
    PRN_MEMORY,      /**< no memory for the job (the caller's) */
    PRN_SENT,        /**< a PRINT sent with `verdict none`: no proof */
    PRN_ERR_COUNT    /**< number of values, not a value */
} prn_err_t;

/** @brief Field names an answer may carry (`field` lines). */
typedef enum {
    PRN_F_BUSY = 0, PRN_F_PRINTING, PRN_F_BUF_FULL, PRN_F_COVER,
    PRN_F_NO_LABEL, PRN_F_LABEL_END, PRN_F_LABEL_ERR, PRN_F_LABEL_MODE,
    PRN_F_PAPER_OUT, PRN_F_BATTERY_LOW, PRN_F_HEAD_HOT, PRN_F_ROLL_W,
    PRN_F_ROLL_H, PRN_F_ROLL_GAP, PRN_F_ROLL_TYPE, PRN_F_LEFT, PRN_F_UID,
    PRN_F_COUNT, PRN_F_PAGE_INDEX, PRN_F_RESULT, PRN_F_BATTERY, PRN_F_X0,
    PRN_F_X1, PRN_F_X2, PRN_F_X3,
    PRN_F_NAMES /**< number of names, not a name */
} prn_field_name_t;

/** @brief Variables: in templates, step arguments and conditions.  0 is
 *         "no variable". */
typedef enum {
    PRN_V_NONE = 0,
    PRN_V_STREAM_LEN, PRN_V_PAGES, PRN_V_FRAMES, PRN_V_LINES,
    PRN_V_LINE_BYTES, PRN_V_HEAD_DOTS, PRN_V_DENSITY, PRN_V_SPEED,
    PRN_V_MLEAD, PRN_V_MTRAIL, PRN_V_ROLL_W, PRN_V_ROLL_H, PRN_V_ROLL_GAP,
    PRN_V_PFLAGS,  /**< page head only */
    PRN_V_IDX,     /**< chunk frame only */
    PRN_V_TOTAL,   /**< chunk frame only */
    PRN_V_LINE_NO, /**< rows only */
    PRN_V_LAY_W,   /**< the roll width the stream was laid out for, 0 any */
    PRN_V_LAY_H,   /**< its height, 0 any */
    PRN_V_C0, PRN_V_C1, PRN_V_UID0, PRN_V_W0, PRN_V_H0, /**< saved */
    PRN_V_V0, PRN_V_V1, PRN_V_V2, PRN_V_V3,             /**< saved */
    PRN_V_COUNT /**< number of values, not a variable */
} prn_var_t;

/** @brief The first variable a `save` line may set. */
#define PRN_V_SAVED PRN_V_C0

/** @brief Value encodings of template tokens and fields. */
typedef enum {
    PRN_E_U8 = 0, PRN_E_LE16, PRN_E_BE16, PRN_E_LE32, PRN_E_BE32,
    PRN_E_COUNT /**< number of values */
} prn_enc_t;

/** @brief Checksum kinds. */
typedef enum {
    PRN_CK_SUM8 = 0, PRN_CK_SUM16LE, PRN_CK_SUM16BE, PRN_CK_XOR8,
    PRN_CK_CRC8, PRN_CK_CRC16CCITT_LE, PRN_CK_CRC16CCITT_BE,
    PRN_CK_CRC32LE,
    PRN_CK_SVPAGE, /**< page head only; see prn_page_fill() */
    PRN_CK_COUNT   /**< number of values */
} prn_ck_t;

/** @brief Template token types. */
typedef enum {
    PRN_T_BYTE = 0, /**< x the byte */
    PRN_T_ARG,      /**< x the encoding, a the argument: a, p0, p1, p2 */
    PRN_T_VAR,      /**< x the encoding, a the variable */
    PRN_T_LEN,      /**< x the encoding, a and b the range */
    PRN_T_CK,       /**< x the kind, a and b the range */
    PRN_T_DATA      /**< the chunk, the page lines or the head line */
} prn_tok_type_t;

/** @brief One template token: 4 bytes. */
typedef struct {
    uint8_t type; /**< prn_tok_type_t */
    uint8_t x;    /**< see prn_tok_type_t */
    int8_t a;     /**< see prn_tok_type_t; a range's first byte */
    int8_t b;     /**< a range's last byte; negative counts from the end */
} prn_tok_t;

/** @brief A run of tokens in prn_spec_t.tok. */
typedef struct {
    uint16_t first; /**< index of the first token */
    uint8_t n;      /**< tokens, 0 for none */
} prn_tseq_t;

/** @brief A named template. */
typedef struct {
    char name[PRN_TMPL_NAME + 1u]; /**< its name */
    prn_tseq_t seq;                /**< its tokens */
} prn_tmpl_t;

/** @brief Arguments of a send: a, p0, p1, p2. */
#define PRN_ARGS 4u

/** @brief A template and the arguments a step or `abort` sends it with. */
typedef struct {
    uint32_t n[PRN_ARGS];  /**< the numbers */
    uint8_t var[PRN_ARGS]; /**< a variable instead, PRN_V_NONE for none */
    uint8_t tmpl;          /**< index of the template */
} prn_send_t;

/** @brief Condition operators. */
typedef enum {
    PRN_OP_SET = 0, /**< field != 0 */
    PRN_OP_CLEAR,   /**< field == 0 */
    PRN_OP_EQ_N,    /**< field == n */
    PRN_OP_NE_N,    /**< field != n */
    PRN_OP_EQ_V,    /**< field == var; lay_w and lay_h of 0 match any */
    PRN_OP_EQ_V1,   /**< field == var - 1 */
    PRN_OP_NE_V     /**< field != var */
} prn_op_t;

/** @brief A condition on a field of the step's answer. */
typedef struct {
    uint32_t n;    /**< the number */
    uint8_t field; /**< prn_field_name_t */
    uint8_t op;    /**< prn_op_t */
    uint8_t var;   /**< prn_var_t */
    uint8_t err;   /**< for `check`: the end when it fails */
} prn_cond_t;

/** @brief Step kinds. */
typedef enum {
    PRN_S_SEND = 0, /**< a template */
    PRN_S_DATA,     /**< the stream's frames */
    PRN_S_WAIT      /**< a pause */
} prn_step_kind_t;

/** @brief The relink target of the verdict, where a step's is a step. */
#define PRN_RELINK_VERDICT 0xFEu
/** @brief No relink. */
#define PRN_RELINK_NONE 0xFFu

/** @brief A step. */
typedef struct {
    char name[PRN_TMPL_NAME + 1u];       /**< its name */
    char relink_name[PRN_TMPL_NAME + 1u]; /**< the relink target as the
                                               file names it, "" none */
    prn_send_t send;                     /**< PRN_S_SEND: what it sends */
    prn_cond_t until[2];                 /**< poll until both hold */
    prn_cond_t check[PRN_STEP_CHECKS];   /**< after the answer */
    uint8_t save_var[PRN_STEP_SAVES];    /**< variables set */
    uint8_t save_field[PRN_STEP_SAVES];  /**< from these fields */
    uint16_t wait_ms;   /**< PRN_S_WAIT: the pause */
    uint16_t poll_ms;   /**< 0, or the poll period after each answer */
    uint16_t within_ms; /**< send: a try's answer, or with poll the
                             step's deadline; data: a write's answer */
    uint16_t delay_ms;  /**< the first send waits this long, unless the
                             step was entered by a relink */
    uint8_t kind;       /**< prn_step_kind_t */
    uint8_t expect;     /**< the answer's key */
    bool has_expect;    /**< expect is set */
    uint8_t tries;      /**< sends without poll, 1 to 5 */
    uint8_t else_err;   /**< the end at the deadline */
    bool flags;         /**< error flags end the job */
    bool roll;          /**< the answer's roll is checked */
    uint8_t relink;     /**< a step index, PRN_RELINK_VERDICT or _NONE */
    uint8_t nuntil;     /**< conditions in until */
    uint8_t ncheck;     /**< checks */
    uint8_t nsave;      /**< saves */
} prn_step_t;

/** @brief Field kinds. */
typedef enum {
    PRN_FK_BIT = 0, PRN_FK_U8, PRN_FK_LE16, PRN_FK_BE16, PRN_FK_LE32,
    PRN_FK_BE32, PRN_FK_BYTES
} prn_fkind_t;

/** @brief Where a field sits in an answer. */
typedef struct {
    uint8_t key;  /**< the answer's key */
    uint8_t name; /**< prn_field_name_t */
    uint8_t off;  /**< byte offset */
    uint8_t kind; /**< prn_fkind_t */
    uint8_t n;    /**< bit number, or byte count of PRN_FK_BYTES */
} prn_field_t;

/** @brief Match kinds. */
typedef enum {
    PRN_M_OUI = 0, PRN_M_NAME, PRN_M_INAME, PRN_M_SVC
} prn_mkind_t;

/** @brief A match line. */
typedef struct {
    uint8_t kind;                /**< prn_mkind_t */
    uint8_t len;                 /**< bytes used of b */
    uint8_t b[PRN_NAME_MAX];     /**< OUI 3 bytes, a name prefix, or a
                                      UUID of 2 or 16 bytes in text order */
} prn_match_t;

/** @brief A UUID of 2 or 16 bytes, in the order the text writes them. */
typedef struct {
    uint8_t len;   /**< 2 or 16 */
    uint8_t b[16]; /**< the bytes */
} prn_uuid_t;

/** @brief Characteristic roles. */
typedef enum {
    PRN_CHR_CMD = 0, PRN_CHR_DATA, PRN_CHR_RX, PRN_CHR_OTHER
} prn_role_t;

#define PRN_P_WRITE    0x01u /**< characteristic property: write */
#define PRN_P_WRITE_NR 0x02u /**< characteristic property: write_nr */
#define PRN_P_NOTIFY   0x04u /**< characteristic property: notify */

/** @brief A characteristic. */
typedef struct {
    prn_uuid_t uuid; /**< its UUID */
    uint8_t props;   /**< PRN_P_* */
    bool used;       /**< the role is declared */
} prn_chr_t;

/** @brief A roll the operator may pick (`roll fixed`), in mm. */
typedef struct {
    uint8_t w;   /**< width */
    uint8_t h;   /**< height along the feed */
    uint8_t gap; /**< gap */
} prn_roll_t;

/** @brief Edges of the label as it is read. */
typedef enum {
    PRN_EDGE_TOP = 0, PRN_EDGE_BOTTOM, PRN_EDGE_LEFT, PRN_EDGE_RIGHT
} prn_edge_t;

/** @brief Verdict kinds. */
typedef enum {
    PRN_VD_COUNTER = 0, PRN_VD_RESULT, PRN_VD_DONE, PRN_VD_NONE
} prn_verdict_kind_t;

/** @brief Answer joiner kinds. */
typedef enum {
    PRN_J_MAGIC_LEN = 0, PRN_J_NOTIFY
} prn_join_kind_t;

/** @brief A parsed printer file. */
typedef struct {
    /* identity */
    char id[PRN_ID_MAX + 1u];                 /**< id */
    char name[PRN_NAME_MAX + 1u];             /**< name */
    char licence[PRN_LICENCE_MAX + 1u];       /**< licence, "" none */
    char source[PRN_SOURCES][PRN_SOURCE_MAX + 1u]; /**< sources */
    uint16_t version;                         /**< version */
    uint8_t nsource;                          /**< sources */
    uint8_t nmatch;                           /**< match lines */
    prn_match_t match[PRN_MATCHES];           /**< match lines */
    /* GATT and link */
    prn_uuid_t service;                       /**< the printer service */
    prn_chr_t chr[PRN_CHRS];                  /**< by prn_role_t */
    bool cccd_next;                           /**< the CCCD follows rx */
    uint8_t mtu_min;                          /**< the least ATT MTU */
    uint16_t conn[4];                         /**< itvl_min, itvl_max,
                                                   latency, supervision */
    /* head and label */
    uint16_t head_dots;                       /**< dots across the head */
    uint8_t dots_mm;                          /**< dots a mm */
    uint8_t line_bytes;                       /**< bytes a head line */
    bool line_msb;                            /**< head dot 0 in bit 7 */
    bool line_black0;                         /**< 0 is black */
    bool line_reverse;                        /**< byte order reversed */
    uint8_t feed;                             /**< prn_edge_t */
    uint8_t dot0;                             /**< prn_edge_t */
    bool align_center;                        /**< centered across the head */
    uint16_t align_dot;                       /**< else the first dot */
    uint8_t mlead;                            /**< margin lines before */
    uint8_t mtrail;                           /**< margin lines after */
    bool roll_fixed;                          /**< roll source fixed */
    uint8_t nroll;                            /**< roll fixed lines */
    prn_roll_t roll[PRN_ROLLS];               /**< roll fixed lines */
    uint8_t roll_lim[4];                      /**< wmin wmax hmin hmax */
    uint8_t density;                          /**< density */
    /* image stream */
    uint16_t page_size;                       /**< 0: no pages */
    uint16_t page_lines;                      /**< lines a page */
    prn_tseq_t page_head;                     /**< page header */
    uint8_t page_flags[2];                    /**< first, last */
    bool lzma;                                /**< compress lzma_lit */
    uint32_t lzma_dict;                       /**< its dictionary */
    uint16_t chunk_size;                      /**< 0: rows */
    bool chunk_pad;                           /**< pad the last chunk */
    uint8_t chunk_pad_byte;                   /**< with this */
    uint8_t chunk_max;                        /**< chunks at most */
    prn_tseq_t frame;                         /**< chunk frame or rows */
    bool cmd_nr;                              /**< write cmd nr */
    bool data_nr;                             /**< write data nr */
    uint8_t frag;                             /**< bytes a data write */
    uint16_t gap_ms;                          /**< between data writes */
    bool speed_stream;                        /**< table input stream_len,
                                                   else mean_page */
    uint8_t nspeed;                           /**< pairs, 0 no table */
    uint32_t speed_above[PRN_SPEED_PAIRS];    /**< thresholds */
    uint16_t speed_value[PRN_SPEED_PAIRS];    /**< values */
    uint16_t speed_else;                      /**< the value below all */
    /* templates */
    uint8_t ntmpl;                            /**< named templates */
    uint16_t ntok;                            /**< tokens used */
    prn_tmpl_t tmpl[PRN_TMPLS];               /**< named templates */
    prn_tok_t tok[PRN_TOKENS];                /**< all tokens */
    /* answers */
    uint8_t join;                             /**< prn_join_kind_t */
    uint8_t magic[4];                         /**< magic_len: the magic */
    uint8_t magic_len;                        /**< its bytes */
    uint8_t len_off;                          /**< length field offset */
    uint8_t len_enc;                          /**< prn_enc_t, 16 bits max */
    uint8_t len_add;                          /**< added to the field */
    uint8_t rx_min;                           /**< shortest frame */
    uint8_t rx_max;                           /**< longest frame */
    uint8_t tail[4];                          /**< rx tail */
    uint8_t tail_len;                         /**< its bytes, 0 none */
    bool has_rx_check;                        /**< rx check */
    prn_tok_t rx_check;                       /**< a PRN_T_CK token */
    uint8_t key_off;                          /**< rx key */
    uint8_t nanswer;                          /**< answer lines */
    uint8_t answer_key[PRN_ANSWERS];          /**< keys */
    uint8_t answer_min[PRN_ANSWERS];          /**< their least length */
    uint8_t nfield;                           /**< field lines */
    prn_field_t field[PRN_FIELDS];            /**< fields */
    uint8_t nerror;                           /**< error lines */
    uint8_t error_field[PRN_ERRORS];          /**< prn_field_name_t */
    uint8_t error_code[PRN_ERRORS];           /**< prn_err_t */
    /* steps and jobs */
    uint8_t nstep;                            /**< steps */
    prn_step_t step[PRN_STEPS];               /**< steps */
    uint8_t nquery;                           /**< steps of the QUERY */
    uint8_t query[PRN_STEPS];                 /**< their indices */
    uint8_t nprint;                           /**< steps of the PRINT */
    uint8_t print[PRN_STEPS];                 /**< their indices */
    bool has_abort;                           /**< abort line */
    prn_send_t abort;                         /**< what it sends */
    uint8_t abort_from;                       /**< print job position */
    uint8_t abort_to;                         /**< print job position */
    uint16_t relink_ms;                       /**< 0 none */
    uint16_t answer_ms;                       /**< a write's link answer
                                                   and a poll's answer */
    uint8_t verdict;                          /**< prn_verdict_kind_t */
    uint8_t vd_field;                         /**< counter, result */
    uint8_t vd_key;                           /**< counter: its answer */
    uint8_t vd_reads;                         /**< counter reads */
    uint16_t vd_gap_ms;                       /**< between them */
    prn_send_t vd_send;                       /**< counter: what it sends */
    uint8_t nresult;                          /**< result: values */
    uint32_t result[PRN_RESULTS];             /**< result: values */
} prn_spec_t;

/** @brief Why a file is refused: the code (1 to 30, 0 none) and its line
 *         (from 1; 0 for the file as a whole). */
typedef struct {
    uint8_t code; /**< E01 to E30, 0 when the file is taken */
    uint16_t line; /**< the line */
} prn_perr_t;

/**
 * @brief Read a printer file.
 *
 * One pass over the bytes; no heap, no recursion.  On a refusal @p out is
 * zeroed and @p err names the first rule broken (protocols/printers/
 * README.md, "Error codes").
 *
 * @param txt  the file's bytes
 * @param n    their count
 * @param name the file's name, `<id>.prn`, for E17; NULL skips that rule
 * @param out  the spec
 * @param err  the refusal; code 0 when the file is taken
 * @return true when the file is taken
 */
bool prn_parse(const char *txt, size_t n, const char *name, prn_spec_t *out,
               prn_perr_t *err);

/**
 * @brief Check a spec again: every value in its range and every reference
 *        to a template, step, field or answer inside its table.
 * @param s the spec
 * @return 0 when it holds, else the code of the first rule broken
 */
uint8_t prn_spec_check(const prn_spec_t *s);

/**
 * @brief Find the field line of a name under an answer key.
 * @param s    the spec
 * @param key  the answer's key
 * @param name prn_field_name_t
 * @return the index in s->field, or -1
 */
int prn_field_find(const prn_spec_t *s, uint8_t key, uint8_t name);

/** @brief An advertisement as the matcher sees it. */
typedef struct {
    uint8_t addr[6];       /**< the address, most significant byte first */
    bool addr_public;      /**< a public address: the OUI means something */
    const char *name;      /**< the advertised name, NUL-terminated or NULL */
    const uint8_t *uuid16; /**< 16-bit UUIDs, 2 bytes each, high first */
    uint8_t n16;           /**< their count */
    const uint8_t *uuid128; /**< 128-bit UUIDs, 16 bytes each, text order */
    uint8_t n128;          /**< their count */
} prn_adv_t;

/**
 * @brief Whether a device matches a spec, and how well.
 *
 * Each match kind the spec uses must hit at least once (AND across kinds,
 * OR inside one).  Score: oui 4, name or iname 2, svc 1, added per kind
 * that hit.
 *
 * @param s   the spec
 * @param adv the advertisement
 * @return 0 for no match, else the score, 1 to 7
 */
unsigned prn_match(const prn_spec_t *s, const prn_adv_t *adv);

#ifdef __cplusplus
}
#endif

#endif /* PRN_SPEC_H */
