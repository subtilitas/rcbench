/**
 * @file sv_sim.h
 * @brief A simulated Supvan label printer for the job's suites: it takes
 *        the job's writes, answers each command after a latency in
 *        notifications of 20 bytes, joins the data frames to the stream,
 *        runs busy, printing and buf_full over time, and moves the label
 *        counter when a job with the right stream ends.
 *
 * Answers carry byte 4 = 0x55, as the station's printer does over BLE.
 * The answer lengths (status 24 bytes, material 47, the rest 16) are the
 * simulator's; H0's captures settle the printer's own.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SV_SIM_H
#define SV_SIM_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "supvan.h"

/** @brief Commands the simulator logs, data frames as one 0xDA entry. */
#define SIM_LOG_MAX 256u
/** @brief The log's entry for a whole data frame. */
#define SIM_DATA 0xDAu
/** @brief The stream the simulator can hold. */
#define SIM_STREAM_MAX 8192u
/** @brief Bytes a notification. */
#define SIM_NOTIFY 20u

/** @brief What the simulator does with a write. */
typedef enum {
    SIM_W_OK,     /**< answered at once */
    SIM_W_REFUSE, /**< answered as refused */
    SIM_W_LOST    /**< never answered */
} sim_write_t;

/** @brief The simulated printer. */
typedef struct {
    /* the test sets these */
    uint8_t flags[4];        /**< status bytes 14 to 17 ORed in */
    uint32_t flags_at;       /**< flags apply from this ms on */
    uint32_t busy_until;     /**< busy before this ms */
    uint32_t print_ms;       /**< printing lasts this long after BUF_FULL */
    uint8_t buf_full_polls;  /**< INQUIRYs after START that show buf_full */
    uint8_t printing_polls;  /**< INQUIRYs after START before printing */
    bool prints;             /**< a good job lowers the counter */
    uint8_t counter_lag;     /**< RETURN_MATs after the print before it does */
    sv_material_t roll;      /**< the record RETURN_MAT gives */
    uint8_t uid_after[SV_UID_LEN]; /**< the UID after a print, when set */
    bool change_uid;         /**< use uid_after once the print is over */
    uint8_t silent_cmd;      /**< a command not answered, 0 none */
    uint8_t silent_after;    /**< ... after this many answers of it */
    uint8_t short_mat;       /**< answer RETURN_MAT with 46 bytes this often */
    uint8_t write_cmd;       /**< the command whose write gets write_do */
    uint16_t write_frag;     /**< or the data fragment, from 1; 0 none */
    sim_write_t write_do;    /**< what that write gets */
    uint32_t latency_ms;     /**< from a command to its answer */
    /* the simulator's own */
    uint8_t log[SIM_LOG_MAX];       /**< commands in order */
    uint32_t log_at[SIM_LOG_MAX];   /**< when each came */
    uint16_t log_p0[SIM_LOG_MAX];   /**< its P0 */
    uint16_t log_p1[SIM_LOG_MAX];   /**< its P1 */
    unsigned nlog;                  /**< entries */
    uint32_t frag_at[1024];         /**< when each data fragment came */
    unsigned nfrag;                 /**< data fragments */
    uint8_t frame[SV_FRAME_LEN];    /**< the frame being joined */
    unsigned nframe;                /**< its bytes */
    unsigned bad_frames;            /**< frames with a wrong head or sum */
    uint8_t stream[SIM_STREAM_MAX]; /**< the stream so far */
    uint32_t nstream;               /**< its length, padding included */
    const uint8_t *expect;          /**< the stream a job must carry */
    uint32_t expect_len;            /**< its length */
    unsigned answers[256];          /**< answers given, per command */
    bool started;                   /**< START seen */
    uint8_t inquiries;              /**< INQUIRYs since START */
    bool full;                      /**< BUF_FULL seen */
    uint32_t full_at;               /**< when */
    bool printed;                   /**< the print is over and was good */
    uint8_t mat_reads;              /**< RETURN_MATs since the print */
    uint8_t out[64];                /**< the pending answer */
    unsigned nout;                  /**< its length */
    unsigned sent;                  /**< bytes of it given out */
    uint32_t out_at;                /**< when it may go */
} sv_sim_t;

/**
 * @brief A printer that answers every command in 5ms, idle, with a 40 x
 *        30mm roll of 295 labels, and prints a good job in 1,000ms.
 * @param s the simulator
 */
static inline void sim_init(sv_sim_t *s)
{
    memset(s, 0, sizeof(*s));
    s->print_ms = 1000u;
    s->prints = true;
    s->latency_ms = 5u;
    s->roll.w_mm = 40u;
    s->roll.h_mm = 30u;
    s->roll.gap_mm = 3u;
    s->roll.type = 1u;
    s->roll.left = 295u;
    static const uint8_t uid[SV_UID_LEN] = { 0xE0, 0x04, 0x01, 0x50,
                                             0x12, 0x34, 0x56 };
    memcpy(s->roll.uid, uid, sizeof uid);
}

/**
 * @brief Whether the print runs at @p now.
 * @param s   the simulator
 * @param now the clock
 * @return true from START until print_ms after BUF_FULL
 */
static inline bool sim_printing(const sv_sim_t *s, uint32_t now)
{
    if (!s->started || s->inquiries < s->printing_polls) {
        return false;
    }
    return !s->full || (uint32_t)(now - s->full_at) < s->print_ms;
}

/**
 * @brief Queue an answer frame of @p len bytes for @p cmd.
 * @param s   the simulator
 * @param cmd the command echoed
 * @param len the frame's length, 16 to 64
 * @param now the clock
 * @return the frame, zeroed past byte 7, to fill
 */
static inline uint8_t *sim_answer(sv_sim_t *s, uint8_t cmd, unsigned len,
                                  uint32_t now)
{
    memset(s->out, 0, sizeof s->out);
    s->out[0] = SV_MAGIC0;
    s->out[1] = SV_MAGIC1;
    s->out[2] = (uint8_t)(len - SV_HEAD);
    s->out[3] = 0u;
    s->out[4] = 0x55u;
    s->out[SV_CMD_AT] = cmd;
    s->nout = len;
    s->sent = 0u;
    s->out_at = now + s->latency_ms;
    s->answers[cmd]++;
    return s->out;
}

/**
 * @brief Finish the print when its time is over: the counter's move.
 * @param s   the simulator
 * @param now the clock
 */
static inline void sim_settle(sv_sim_t *s, uint32_t now)
{
    if (s->full && !s->printed && !sim_printing(s, now)) {
        s->printed = true;
        s->mat_reads = 0u;
    }
}

/**
 * @brief Answer one command.
 * @param s   the simulator
 * @param c   the 16-byte command
 * @param now the clock
 */
static inline void sim_command(sv_sim_t *s, const uint8_t *c, uint32_t now)
{
    const uint8_t cmd = c[SV_CMD_AT];
    if (s->nlog < SIM_LOG_MAX) {
        s->log[s->nlog] = cmd;
        s->log_at[s->nlog] = now;
        s->log_p0[s->nlog] = (uint16_t)(c[12] | (c[13] << 8));
        s->log_p1[s->nlog] = (uint16_t)(c[14] | (c[15] << 8));
        s->nlog++;
    }
    if (cmd == SV_C_START) {
        s->started = true;
        s->inquiries = 0u;
    }
    if (cmd == SV_C_BUF_FULL) {
        s->full = true;
        s->full_at = now;
    }
    sim_settle(s, now);
    if (cmd == s->silent_cmd && s->answers[cmd] >= s->silent_after) {
        return;
    }
    if (cmd == SV_C_INQUIRY) {
        uint8_t *f = sim_answer(s, cmd, 24u, now);
        if (s->started) {
            s->inquiries++;
        }
        if ((uint32_t)(now - s->flags_at) < 0x80000000u) {
            for (unsigned i = 0; i < 4u; ++i) {
                f[14u + i] = s->flags[i];
            }
        }
        if (s->started && s->inquiries <= s->buf_full_polls) {
            f[14] |= 0x01u;
        }
        if ((uint32_t)(s->busy_until - now) - 1u < 0x7FFFFFFFu) {
            f[15] |= 0x04u;
        }
        if (sim_printing(s, now)) {
            f[16] |= 0x40u;
        }
        f[18] = 0x2Au;
        return;
    }
    if (cmd == SV_C_MATERIAL) {
        const bool cut = s->short_mat > 0u;
        if (cut) {
            s->short_mat--;
        }
        uint8_t *f = sim_answer(s, cmd, cut ? 46u : 47u, now);
        sv_material_t m = s->roll;
        if (s->printed) {
            if (s->mat_reads++ >= s->counter_lag && s->prints
                && s->nstream >= s->expect_len && s->expect != NULL
                && memcmp(s->stream, s->expect, s->expect_len) == 0
                && s->bad_frames == 0u) {
                m.left = s->roll.left - 1u;
            }
            if (s->change_uid) {
                memcpy(m.uid, s->uid_after, SV_UID_LEN);
            }
        }
        memcpy(f + 22, m.uid, SV_UID_LEN);
        f[39] = m.type;
        f[40] = m.w_mm;
        f[41] = m.h_mm;
        f[42] = m.gap_mm;
        if (!cut) {
            f[43] = (uint8_t)m.left;
            f[44] = (uint8_t)(m.left >> 8);
            f[45] = (uint8_t)(m.left >> 16);
            f[46] = (uint8_t)(m.left >> 24);
        }
        return;
    }
    (void)sim_answer(s, cmd, 16u, now);
}

/**
 * @brief Take one data fragment; a whole frame is checked and its bytes
 *        go to the stream.
 * @param s   the simulator
 * @param d   the 128 bytes
 * @param now the clock
 */
static inline void sim_fragment(sv_sim_t *s, const uint8_t *d, uint32_t now)
{
    if (s->nfrag < 1024u) {
        s->frag_at[s->nfrag] = now;
    }
    s->nfrag++;
    memcpy(s->frame + s->nframe, d, SV_FRAGMENT);
    s->nframe += SV_FRAGMENT;
    if (s->nframe < SV_FRAME_LEN) {
        return;
    }
    s->nframe = 0u;
    if (s->nlog < SIM_LOG_MAX) {
        s->log_at[s->nlog] = now;
        s->log[s->nlog++] = SIM_DATA;
    }
    uint16_t sum = 0u;
    for (unsigned i = 10u; i < SV_FRAME_LEN; ++i) {
        sum = (uint16_t)(sum + s->frame[i]);
    }
    static const uint8_t head[8] = { 0x7E, 0x5A, 0xFC, 0x01,
                                     0x10, 0x02, 0xAA, 0xBB };
    if (memcmp(s->frame, head, sizeof head) != 0
        || s->frame[8] != (uint8_t)sum || s->frame[9] != (uint8_t)(sum >> 8)
        || s->nstream + SV_CHUNK > SIM_STREAM_MAX
        || s->frame[10] != s->nstream / SV_CHUNK) {
        s->bad_frames++;
        return;
    }
    memcpy(s->stream + s->nstream, s->frame + 12, SV_CHUNK);
    s->nstream += SV_CHUNK;
}

/**
 * @brief Take one write from the job.
 * @param s   the simulator
 * @param d   the bytes
 * @param n   16 or 128
 * @param now the clock
 * @return what the write gets
 */
static inline sim_write_t sim_write(sv_sim_t *s, const uint8_t *d,
                                    uint16_t n, uint32_t now)
{
    if (n == SV_FRAGMENT) {
        const bool hit = s->write_frag != 0u && s->nfrag + 1u == s->write_frag;
        if (hit && s->write_do != SIM_W_OK) {
            s->nfrag++;
            return s->write_do;
        }
        sim_fragment(s, d, now);
        return SIM_W_OK;
    }
    if (s->write_cmd != 0u && d[SV_CMD_AT] == s->write_cmd
        && s->write_do != SIM_W_OK) {
        return s->write_do;
    }
    sim_command(s, d, now);
    return SIM_W_OK;
}

/**
 * @brief The next notification due, up to 20 bytes.
 * @param s   the simulator
 * @param now the clock
 * @param out set to the bytes
 * @return their count, 0 none
 */
static inline unsigned sim_notify(sv_sim_t *s, uint32_t now,
                                  const uint8_t **out)
{
    if (s->sent >= s->nout || (uint32_t)(now - s->out_at) >= 0x80000000u) {
        return 0u;
    }
    unsigned k = s->nout - s->sent;
    if (k > SIM_NOTIFY) {
        k = SIM_NOTIFY;
    }
    *out = s->out + s->sent;
    s->sent += k;
    return k;
}

/**
 * @brief Count the log's entries of @p cmd.
 * @param s   the simulator
 * @param cmd the command
 * @return the count
 */
static inline unsigned sim_count(const sv_sim_t *s, uint8_t cmd)
{
    unsigned k = 0u;
    for (unsigned i = 0; i < s->nlog; ++i) {
        k += (s->log[i] == cmd) ? 1u : 0u;
    }
    return k;
}

/** @brief The rig's stream: 3 data frames, the last one 234 bytes. */
#define RIG_STREAM 1234u

/** @brief The job and the printer on one link, with the clock. */
typedef struct {
    sv_job_t job;         /**< the job */
    uint8_t stream[RIG_STREAM]; /**< the stream a PRINT sends */
    sv_stream_t z;        /**< its description */
    sv_sim_t sim;         /**< the printer */
    uint32_t now;         /**< the clock, ms */
    bool link;            /**< the link's state */
    bool owed;            /**< a write's answer is owed to the job */
    bool owed_ok;         /**< that answer */
} sv_rig_t;

/**
 * @brief One ms of the link: the job's tick, its write, the write's
 *        answer and the printer's notifications.
 * @param r the rig
 */
static inline void rig_step(sv_rig_t *r)
{
    sv_job_tick(&r->job, r->now);
    if (r->link) {
        if (r->owed) {
            r->owed = false;
            sv_job_written(&r->job, r->owed_ok, r->now);
        }
        const uint8_t *d = NULL;
        uint16_t n = 0u;
        if (sv_job_tx(&r->job, r->now, &d, &n)) {
            const sim_write_t w = sim_write(&r->sim, d, n, r->now);
            if (w != SIM_W_LOST) {
                r->owed = true;
                r->owed_ok = (w == SIM_W_OK);
            }
        }
        const uint8_t *b = NULL;
        unsigned k;
        while ((k = sim_notify(&r->sim, r->now, &b)) != 0u) {
            sv_job_rx(&r->job, b, k, r->now);
        }
    }
    r->now++;
}

/**
 * @brief Run the rig until the job ends or @p ms pass.
 * @param r  the rig
 * @param ms the time
 * @return true when the job ended
 */
static inline bool rig_run(sv_rig_t *r, uint32_t ms)
{
    const uint32_t start = r->now;
    while (!sv_job_ended(&r->job) && (uint32_t)(r->now - start) < ms) {
        rig_step(r);
    }
    return sv_job_ended(&r->job);
}

/**
 * @brief Run the rig until the job reaches @p step, at most @p ms.
 * @param r    the rig
 * @param step the step
 * @param ms   the time
 * @return true when it did
 */
static inline bool rig_run_to(sv_rig_t *r, sv_step_t step, uint32_t ms)
{
    const uint32_t start = r->now;
    while (r->job.step != step && !sv_job_ended(&r->job)
           && (uint32_t)(r->now - start) < ms) {
        rig_step(r);
    }
    return r->job.step == step;
}

/**
 * @brief Drop or restore the link for both ends.
 * @param r  the rig
 * @param up the state
 */
static inline void rig_link(sv_rig_t *r, bool up)
{
    r->link = up;
    r->owed = false;
    if (!up) {
        r->sim.nout = 0u;
        r->sim.sent = 0u;
        r->sim.nframe = 0u;
    }
    sv_job_link(&r->job, up, r->now);
}

/**
 * @brief Start a job at @p t0 against a fresh simulator; a PRINT sends
 *        RIG_STREAM bytes of 3 print buffers laid out for 40 x 30mm.
 * @param r    the rig
 * @param kind QUERY or PRINT
 * @param t0   the clock
 * @return sv_job_start()'s answer
 */
static inline bool rig_start(sv_rig_t *r, sv_job_kind_t kind, uint32_t t0)
{
    sim_init(&r->sim);
    r->now = t0;
    r->link = true;
    r->owed = false;
    uint32_t x = 5u;
    for (unsigned i = 0; i < RIG_STREAM; ++i) {
        x = x * 1103515245u + 12345u;
        r->stream[i] = (uint8_t)(x >> 24);
    }
    r->z.data = r->stream;
    r->z.len = RIG_STREAM;
    r->z.buffers = 3u;
    r->z.w_mm = 40u;
    r->z.h_mm = 30u;
    r->sim.expect = r->stream;
    r->sim.expect_len = RIG_STREAM;
    return sv_job_start(&r->job, kind, kind == SV_JOB_PRINT ? &r->z : NULL,
                        t0);
}

#endif /* SV_SIM_H */
