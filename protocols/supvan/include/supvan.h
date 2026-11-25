/**
 * @file
 * @brief The print protocol of Supvan T50-series thermal label printers
 *        (Supvan and Katasymbol are names of their owners, used only to
 *        say which printers this talks to; README.md, "Trademarks").
 *
 * Parts:
 * - frames: the 16-byte command, the answer joiner that rebuilds a frame
 *   from notifications, the status and material record parsers;
 * - page: the 4,096-byte print buffer, the 512-byte data frame around 500
 *   stream bytes, the speed byte;
 * - job: the machine that runs a QUERY (check, roll, ready) or a PRINT
 *   (the whole sequence and the label counter's verdict) over one link,
 *   one write at a time.
 *
 * The same frames run over the classic SPP (serial port profile) channel
 * and over BLE (Bluetooth Low Energy); the job is written for BLE, where
 * data frames get no answer.  All fields are little-endian.
 *
 * Pure C: no heap, no clock read, no I/O.  Times are unsigned differences
 * of a free-running 32-bit ms clock the caller passes in.
 *
 * Protocol source: heeen/supvan-cups (MIT), docs/PROTOCOL.md, and a
 * T50M Pro with firmware 2.4 on the bench's station.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SUPVAN_H
#define SUPVAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- frames */

/** @brief Frame bytes. @{ */
#define SV_MAGIC0     0x7Eu /**< byte 0 of every frame */
#define SV_MAGIC1     0x5Au /**< byte 1 of every frame */
#define SV_HEAD       4u    /**< magic and the 16-bit length that follows */
#define SV_CMD_LEN    16u   /**< a command frame, host to printer */
#define SV_CMD_AT     7u    /**< where frames in both directions carry the
                                 command byte */
#define SV_RX_MIN     8u    /**< the shortest answer: up to the command byte */
#define SV_RX_MAX     128u  /**< the longest answer taken; a longer length
                                 field is a broken frame */
/** @} */

/** @brief Command bytes (byte 7). @{ */
#define SV_C_BUF_FULL   0x10u /**< P0 stream length, P1 speed byte */
#define SV_C_INQUIRY    0x11u /**< status */
#define SV_C_CHECK      0x12u /**< check device */
#define SV_C_START      0x13u /**< start print */
#define SV_C_STOP       0x14u /**< stop print */
#define SV_C_MATERIAL   0x30u /**< the roll's material record */
#define SV_C_BULK       0x5Cu /**< next zipped bulk: P0 512, P1 frame count */
/** @} */

/**
 * @brief Build a command frame:
 *        7E 5A 0C 00 10 01 AA CMD CHK_L CHK_H 00 01 P0_L P0_H P1_L P1_H,
 *        CHK the 16-bit sum of bytes 10 to 15.
 * @param out the 16 bytes
 * @param cmd the command byte
 * @param p0  the first parameter
 * @param p1  the second parameter
 */
void sv_cmd_build(uint8_t out[SV_CMD_LEN], uint8_t cmd, uint16_t p0,
                  uint16_t p1);

/** @brief Joins notifications to answer frames.  Fields are private. */
typedef struct {
    uint8_t buf[SV_RX_MAX]; /**< the frame being joined */
    uint16_t n;             /**< bytes held */
    uint16_t done;          /**< length of the frame last given out, 0 none */
} sv_rx_t;

/** @brief Empty the joiner. @param rx the joiner */
void sv_rx_reset(sv_rx_t *rx);

/**
 * @brief Take bytes until a frame is whole.
 *
 * A frame is 7E 5A, a 16-bit length LEN, then LEN more bytes.  Bytes
 * before a 7E 5A are dropped; a LEN that gives a frame shorter than
 * SV_RX_MIN or longer than SV_RX_MAX drops the 7E and searches again from
 * the next byte.
 *
 * @param rx    the joiner
 * @param in    the bytes of one notification, or the rest of one
 * @param n     their count
 * @param frame set to the whole frame, or NULL; valid until the next call
 * @param len   set to its length, or 0
 * @return the bytes of @p in taken; call again with the rest while it is
 *         less than @p n
 */
size_t sv_rx_feed(sv_rx_t *rx, const uint8_t *in, size_t n,
                  const uint8_t **frame, uint16_t *len);

/** @brief The printer's status (INQUIRY answer, bytes 14 to 19). */
typedef struct {
    bool buf_full;     /**< [14] bit 0 */
    bool label_rw;     /**< [14] bit 1: label read or write error */
    bool label_end;    /**< [14] bit 2: roll at its end */
    bool label_mode;   /**< [14] bit 3: label mode error */
    bool low_battery;  /**< [14] bit 6 */
    bool busy;         /**< [15] bit 2 */
    bool head_hot;     /**< [15] bit 3 */
    bool cover_open;   /**< [16] bit 3 */
    bool usb_power;    /**< [16] bit 4 */
    bool printing;     /**< [16] bit 6 */
    bool no_label;     /**< [17] bit 0: no label installed */
    uint16_t count;    /**< [18..19] print count */
} sv_status_t;

/** @brief Bytes of an INQUIRY answer the parser needs. */
#define SV_STATUS_MIN 20u

/**
 * @brief Parse an INQUIRY answer.
 * @param f  the frame
 * @param n  its length
 * @param st the status
 * @return false when the frame is shorter than SV_STATUS_MIN or is not an
 *         INQUIRY answer
 */
bool sv_status_parse(const uint8_t *f, size_t n, sv_status_t *st);

/** @brief A job's end, and the protocol's verdicts. */
typedef enum {
    SV_OK = 0,          /**< done; a PRINT printed one label */
    SV_ERR_COVER,       /**< cover open */
    SV_ERR_NO_LABEL,    /**< no label: status bits or a zeroed record */
    SV_ERR_LABEL,       /**< label read or mode error */
    SV_ERR_BATTERY,     /**< printer battery low */
    SV_ERR_HOT,         /**< print head hot */
    SV_ERR_BUSY,        /**< busy or printing for 5s before START */
    SV_ERR_SILENT,      /**< a step's answer or deadline missed */
    SV_ERR_WRITE,       /**< the link refused a write */
    SV_ERR_LINK,        /**< the link dropped; no PRINTED from the counter */
    SV_ERR_NOT_PRINTED, /**< the counter stayed for 3 reads */
    SV_ERR_ROLL,        /**< the roll changed during the job */
    SV_ERR_SMALL,       /**< the roll is under 15mm wide or 20mm long */
    SV_ERR_LARGE,       /**< the roll is over 48mm wide or 100mm long */
    SV_ERR_ABORTED,     /**< sv_job_abort() */
    SV_ERR_COUNT        /**< number of values, not a value */
} sv_err_t;

/**
 * @brief The error a status reports, in the order cover, no label, label,
 *        battery, head.
 * @param st the status
 * @return SV_OK when no error flag is set
 */
sv_err_t sv_status_error(const sv_status_t *st);

/** @brief Bytes of the material record's tag UID. */
#define SV_UID_LEN 7u
/** @brief Bytes of a material answer the parser needs: the record from
 *         byte 22 up to the label counter at 43 to 46. */
#define SV_MAT_MIN 47u

/** @brief The roll's material record (RETURN_MAT answer). */
typedef struct {
    uint8_t uid[SV_UID_LEN]; /**< [22..28] the roll's tag UID */
    uint8_t type;            /**< [39] label type */
    uint8_t w_mm;            /**< [40] width, mm */
    uint8_t h_mm;            /**< [41] height (length along the feed), mm */
    uint8_t gap_mm;          /**< [42] gap, mm */
    uint32_t left;           /**< [43..46] labels left */
} sv_material_t;

/**
 * @brief Parse a RETURN_MAT answer.
 * @param f the frame
 * @param n its length
 * @param m the record
 * @return false when the frame is shorter than SV_MAT_MIN or is not a
 *         RETURN_MAT answer
 */
bool sv_material_parse(const uint8_t *f, size_t n, sv_material_t *m);

/** @brief The rolls a label is laid out on, in mm.  The head is 48mm. @{ */
#define SV_ROLL_W_MIN 15u  /**< narrowest roll, mm */
#define SV_ROLL_W_MAX 48u  /**< widest roll: the head, mm */
#define SV_ROLL_H_MIN 20u  /**< shortest label along the feed, mm */
#define SV_ROLL_H_MAX 100u /**< longest label along the feed, mm */
/** @} */

/**
 * @brief Whether a roll takes a label.
 * @param m the record
 * @return SV_OK; SV_ERR_NO_LABEL for a width or height of 0 (a zeroed
 *         record); SV_ERR_SMALL or SV_ERR_LARGE outside the limits
 */
sv_err_t sv_roll_check(const sv_material_t *m);

/* ------------------------------------------------------------------ page */

/** @brief The print head and the print buffer. @{ */
#define SV_DOTS_MM     8u    /**< dots a mm, both directions */
#define SV_HEAD_DOTS   384u  /**< dots across the 48mm head */
#define SV_LINE_BYTES  48u   /**< bytes a column: 384 dots, LSB first */
#define SV_BUF_SIZE    4096u /**< a print buffer */
#define SV_BUF_HEAD    14u   /**< its header */
#define SV_BUF_COLS    84u   /**< columns a buffer: 4,074 data bytes / 48 */
#define SV_MARGIN      8u    /**< top and bottom margin in dots */
#define SV_DENSITY     4u    /**< burn density, black and red trim */
#define SV_PAGE_START  0x02u /**< byte 2: the page's first buffer */
#define SV_PAGE_END    0x04u /**< byte 2: the page's last buffer */
#define SV_PRINT_END   0x08u /**< byte 2: the job's last buffer */
/** @} */

/**
 * @brief Print buffers for an image of @p cols columns.
 * @param cols image columns
 * @return the count; 0 for 0 columns
 */
uint16_t sv_buf_count(uint16_t cols);

/**
 * @brief Build print buffer @p index of an image.
 *
 * The image is column-major, SV_LINE_BYTES a column, least significant bit
 * first, 1 black: dot (col, row) is bit row % 8 of byte col * 48 + row / 8.
 * Buffer @p index holds columns index * 84 on, up to 84 of them.  Header:
 * [0..1] checksum, [2] page bits (SV_PAGE_START on the first buffer,
 * SV_PAGE_END and SV_PRINT_END on the last), [3] density << 2 | 1 << 6,
 * [4..5] columns, [6] 48, [8..9] and [10..11] the margins, [12] density;
 * data from [14], the rest 0.  Checksum: the 16-bit sum of bytes 2 to 13
 * plus byte i * 256 - 1 of each whole 256-byte block up to the data's end.
 *
 * @param out   SV_BUF_SIZE bytes
 * @param bits  the image, @p cols * 48 bytes
 * @param cols  its columns, 1 to 65,535
 * @param index the buffer, from 0
 * @return false when @p index is past the last buffer, or @p bits or
 *         @p out is NULL; @p out is then untouched
 */
bool sv_buf_fill(uint8_t out[SV_BUF_SIZE], const uint8_t *bits,
                 uint16_t cols, uint16_t index);

/** @brief The data frame. @{ */
#define SV_CHUNK       500u /**< stream bytes a frame */
#define SV_FRAME_LEN   512u /**< 7E 5A FC 01 10 02, then the 506-byte packet */
#define SV_FRAGMENT    128u /**< bytes a BLE write; 4 writes a frame */
#define SV_FRAMES_MAX  255u /**< index and count are single bytes */
/** @} */

/**
 * @brief Data frames for a stream.
 * @param len the stream's length in bytes
 * @return ceil(len / 500); 0 for an empty stream or one over 255 frames
 */
uint16_t sv_chunk_count(uint32_t len);

/**
 * @brief Build data frame @p index of a stream: 7E 5A FC 01 10 02, then
 *        AA BB CHK_L CHK_H index count and 500 stream bytes, the last
 *        frame's padded with 0; CHK the 16-bit sum of packet bytes 4 to
 *        505.
 * @param out    SV_FRAME_LEN bytes
 * @param stream the stream
 * @param len    its length
 * @param index  the frame, from 0
 * @return false when @p index is past the last frame or the stream needs
 *         more than 255
 */
bool sv_chunk_build(uint8_t out[SV_FRAME_LEN], const uint8_t *stream,
                    uint32_t len, uint8_t index);

/**
 * @brief The speed byte of BUF_FULL from the mean stream bytes a print
 *        buffer: above 3,000 10; 2,800 15; 2,500 20; 2,000 25; 1,500 40;
 *        1,000 45; 500 55; else 60.
 * @param mean stream length / print buffers
 * @return the speed byte
 */
uint8_t sv_speed(uint32_t mean);

/* ------------------------------------------------------------------- job */

/** @brief The longest stream: BUF_FULL carries its length in 16 bits.
 *         132 data frames. */
#define SV_STREAM_MAX 65535u

/** @brief The stream a PRINT sends, and what it was built for. */
typedef struct {
    const uint8_t *data; /**< the LZMA stream */
    uint32_t len;        /**< its length, 1 to SV_STREAM_MAX bytes */
    uint16_t buffers;    /**< the print buffers it holds, for the speed */
    uint8_t w_mm;        /**< the roll's width it was laid out for, 0 any */
    uint8_t h_mm;        /**< the roll's height it was laid out for, 0 any */
} sv_stream_t;

/** @brief What a job does. */
typedef enum {
    SV_JOB_QUERY, /**< steps 1 to 3: the device, the roll, the status */
    SV_JOB_PRINT  /**< steps 1 to 12: one label and the counter's verdict */
} sv_job_kind_t;

/** @brief A job's steps; the numbers are the protocol's. */
typedef enum {
    SV_STEP_IDLE = 0,  /**< never started */
    SV_STEP_CHECK,     /**< 1: CHECK_DEVICE, answered; 1s, 1 retry */
    SV_STEP_MATERIAL,  /**< 2: RETURN_MAT, the roll and counter c0; 1s,
                            1 retry */
    SV_STEP_READY,     /**< 3: INQUIRY every 100ms until neither busy nor
                            printing, then for a PRINT RETURN_MAT once more:
                            the counter c0 just before START; 5s, then
                            SV_ERR_BUSY */
    SV_STEP_START,     /**< 4: START_PRINT, answered; 1s */
    SV_STEP_PRINTING,  /**< 5: INQUIRY until printing; 3s */
    SV_STEP_BUFFER,    /**< 6: INQUIRY until buf_full clear; 3s */
    SV_STEP_BULK,      /**< 7: NEXT_ZIPPEDBULK 512, frames; answered; 1s */
    SV_STEP_DATA,      /**< 8: each frame as 4 writes of 128 bytes, 10ms
                            after the last write's answer; 1s a write */
    SV_STEP_PAUSE,     /**< 9: 20ms */
    SV_STEP_FULL,      /**< 10: BUF_FULL length, speed; answered; 1s */
    SV_STEP_DONE,      /**< 11: INQUIRY every 100ms until neither printing
                            nor busy; 15s */
    SV_STEP_COUNTER,   /**< 12: RETURN_MAT up to 3 times, 300ms apart */
    SV_STEP_STOP,      /**< STOP_PRINT after an end in steps 4 to 11 */
    SV_STEP_RELINK,    /**< the link dropped after START: 5s for it to
                            come back, then the counter decides */
    SV_STEP_END        /**< ended; sv_job_t.err says how */
} sv_step_t;

/** @brief The job's times, in ms. @{ */
#define SV_ANSWER_MS    1000u  /**< a command's answer, a write's answer */
#define SV_POLL_MS      100u   /**< INQUIRY period */
#define SV_READY_MS     5000u  /**< step 3 */
#define SV_PRINTING_MS  3000u  /**< step 5 */
#define SV_BUFFER_MS    3000u  /**< step 6 */
#define SV_FRAG_GAP_MS  10u    /**< between a write's answer and the next */
#define SV_PAUSE_MS     20u    /**< step 9 */
#define SV_DONE_MS      15000u /**< step 11 */
#define SV_COUNTER_MS   300u   /**< between counter reads */
#define SV_COUNTER_READS 3u    /**< counter reads at most */
#define SV_RELINK_MS    5000u  /**< for the link to come back */
/** @} */

/** @brief A job.  Read the documented fields; the rest are private. */
typedef struct {
    uint8_t kind;          /**< sv_job_kind_t */
    uint8_t step;          /**< sv_step_t */
    uint8_t err;           /**< sv_err_t once step is SV_STEP_END */
    bool relinked;         /**< the verdict came after a link drop */
    uint16_t frame;        /**< data frames sent */
    uint16_t frames;       /**< data frames of the stream */
    sv_material_t mat;     /**< the roll as step 2 read it */
    sv_material_t last;    /**< the roll as the last read gave it */
    sv_status_t st;        /**< the last status */
    /* private */
    sv_stream_t z;         /**< the stream */
    uint8_t end_err;       /**< the end STOP_PRINT carries */
    uint8_t want;          /**< command due, 0 none */
    uint8_t await;         /**< command whose answer is awaited, 0 none */
    uint8_t tries;         /**< sends of the step's command */
    uint8_t frag;          /**< fragment of the frame, 0 to 3 */
    uint8_t reads;         /**< counter reads */
    bool started;          /**< START_PRINT was handed out */
    bool rebased;          /**< step 3 re-read the counter after the wait */
    bool link;             /**< the link is up */
    bool in_flight;        /**< a write is out, its answer not in */
    uint8_t out_kind;      /**< what that write is: its command byte, or
                                0xFF for a data fragment; its answer is
                                booked against this, not against the step,
                                so a notification may come before it */
    bool data_due;         /**< a data fragment is due */
    uint32_t t_step;       /**< step entered */
    uint32_t t_sent;       /**< last write handed out */
    uint32_t t_due;        /**< the next write is due */
    uint8_t cmd[SV_CMD_LEN];     /**< the command written */
    uint8_t buf[SV_FRAME_LEN];   /**< the data frame being written */
    sv_rx_t rx;            /**< the answer joiner */
} sv_job_t;

/**
 * @brief Start a job on a link that is up.
 * @param j      the job
 * @param kind   QUERY or PRINT
 * @param z      the stream for a PRINT, copied (its bytes are not); NULL
 *               for a QUERY
 * @param now_ms the clock
 * @return false for a PRINT without a stream, with an empty one, one over
 *         SV_STREAM_MAX bytes or one of 0 buffers; the job is then not
 *         started
 */
bool sv_job_start(sv_job_t *j, sv_job_kind_t kind, const sv_stream_t *z,
                  uint32_t now_ms);

/**
 * @brief Feed notification bytes from the printer.
 * @param j      the job
 * @param data   the bytes, any piece of one or more answers
 * @param n      their count
 * @param now_ms the clock
 */
void sv_job_rx(sv_job_t *j, const uint8_t *data, size_t n, uint32_t now_ms);

/**
 * @brief The answer to the write sv_job_tx() handed out last.
 *
 * It is booked against that write, a command or a data fragment, so it may
 * come before or after the printer's notification for the same write.
 * @param j      the job
 * @param ok     false: the link refused it
 * @param now_ms the clock
 */
void sv_job_written(sv_job_t *j, bool ok, uint32_t now_ms);

/**
 * @brief The link went down or came back.
 * @param j      the job
 * @param up     the link's state
 * @param now_ms the clock
 */
void sv_job_link(sv_job_t *j, bool up, uint32_t now_ms);

/**
 * @brief Stop the job: SV_ERR_ABORTED, with STOP_PRINT once when START was
 *        sent and printing is not over.
 * @param j      the job
 * @param now_ms the clock
 */
void sv_job_abort(sv_job_t *j, uint32_t now_ms);

/**
 * @brief The next write, when one is due and none is out.
 *
 * Write it as a write request, then call sv_job_written().
 *
 * @param j      the job
 * @param now_ms the clock
 * @param data   set to the bytes, valid until the next sv_job_* call
 * @param n      set to their count: 16 or 128
 * @return true when a write is handed out
 */
bool sv_job_tx(sv_job_t *j, uint32_t now_ms, const uint8_t **data,
               uint16_t *n);

/**
 * @brief Run the timeouts.  Call every poll.
 * @param j      the job
 * @param now_ms the clock
 */
void sv_job_tick(sv_job_t *j, uint32_t now_ms);

/**
 * @brief Whether the job has ended.
 * @param j the job
 * @return true at SV_STEP_END
 */
bool sv_job_ended(const sv_job_t *j);

#ifdef __cplusplus
}
#endif

#endif /* SUPVAN_H */
