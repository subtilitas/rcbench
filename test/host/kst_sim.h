/*
 * A simulated KST servo on a simulated wire, for the suites that test
 * protocols/kst.
 *
 * The servo side reads a master frame from its half-cell levels with its own
 * Manchester decoder, keeps 32 registers and answers as line captures: edge
 * timestamps with the delays, the bit rate and the stretched last high that
 * the wire readings show.  A fault script bends single transactions.  The
 * wire side is a kst_driver_t on a clock the test advances, and it counts
 * what a master must never do: a frame sooner than 3ms after the last
 * window, a write to register 0x00, an address above 0x1F.
 *
 * With needs_power_up set the servo behaves as the bench measured a
 * Chaservo (2026-10-16): it enters programming mode only on the first sync
 * after a power-up (sim_power()), and a written register acts, in acting[],
 * only from the next power-up on.  Without it every sync is taken and
 * acting[] is not kept.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef KST_SIM_H
#define KST_SIM_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "kst_session.h"

/* --- reply shapes ----------------------------------------------------------- */

/* As measured: first edge 438us after a read, 3700us after a write; servo
 * half-cell 25.575us; last high 32us at a 1.4V threshold. */
#define SIM_READ_DELAY_NS  438000u
#define SIM_WRITE_DELAY_NS 3700000u
#define SIM_HALF_NS        25575u
#define SIM_TAIL_NS        32000u

typedef struct {
    uint32_t delay_ns;    /* master's last edge to the first rising edge */
    uint32_t half_ns;     /* the servo's half-cell */
    uint32_t tail_ns;     /* length of a last high half-cell */
    uint32_t glitch_at_ns;  /* 0: none; else a pulse at this time */
    uint32_t glitch_ns;
    unsigned cells;       /* bits sent, 11 for a whole reply */
} sim_shape_t;

static inline sim_shape_t sim_shape(uint32_t delay_ns)
{
    sim_shape_t s;

    s.delay_ns = delay_ns;
    s.half_ns = SIM_HALF_NS;
    s.tail_ns = SIM_TAIL_NS;
    s.glitch_at_ns = 0;
    s.glitch_ns = 0;
    s.cells = 11;
    return s;
}

static inline void sim_cap_clear(kst_capture_t *cap, uint32_t window_ns)
{
    memset(cap, 0, sizeof(*cap));
    cap->window_ns = window_ns;
}

static inline void sim_cap_edge(kst_capture_t *cap, uint32_t t_ns)
{
    unsigned at = cap->n_edges;

    if (cap->n_edges >= KST_CAP_MAX_EDGES) {
        cap->overflow = 1;
        return;
    }
    /* Keep the edges in ascending order. */
    while (at > 0u && cap->edge_ns[at - 1u] > t_ns) {
        cap->edge_ns[at] = cap->edge_ns[at - 1u];
        at--;
    }
    cap->edge_ns[at] = t_ns;
    cap->n_edges++;
}

/* The reply `1 ff vvvvvvvv` as edges. */
static inline void sim_reply(kst_capture_t *cap, uint32_t window_ns,
                             unsigned flags, unsigned value,
                             const sim_shape_t *shape)
{
    const unsigned word = (1u << 10) | ((flags & 3u) << 8) | (value & 0xFFu);
    bool level = false;
    uint32_t t = shape->delay_ns - shape->half_ns;

    sim_cap_clear(cap, window_ns);
    for (unsigned i = 0; i < 2u * shape->cells; ++i) {
        const bool bit = ((word >> (10u - i / 2u)) & 1u) != 0u;
        const bool want = bit == ((i % 2u) != 0u);

        if (want != level) {
            sim_cap_edge(cap, t);
            level = want;
        }
        t += shape->half_ns;
    }
    if (level) {
        sim_cap_edge(cap, t - shape->half_ns + shape->tail_ns);
    }
    if (shape->glitch_at_ns != 0u) {
        sim_cap_edge(cap, shape->glitch_at_ns);
        sim_cap_edge(cap, shape->glitch_at_ns + shape->glitch_ns);
    }
}

/* --- the image of the servo on the bench ----------------------------------- */

static inline kst_image_t sim_bench_image(void)
{
    static const uint8_t bytes[KST_REG_COUNT] = {
        0x00, 0xf5, 0xf5, 0x14, 0x48, 0x77, 0x04, 0x65,
        0x41, 0xa4, 0x0d, 0x80, 0x53, 0x07, 0x14, 0x00,
        0x96, 0x96, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x0a, 0x00, 0x10, 0x00, 0x00,
    };
    kst_image_t img;

    memcpy(img.r, bytes, sizeof(bytes));
    return img;
}

/* --- the travel the registers give ------------------------------------------- */

/*
 * Where a programmable servo's horn goes for pulse @p cmd_us, as the pulse
 * width a servo model (servo_sim.h) is commanded: the model the limit run
 * takes (servo_limrun.h).  NEUTRAL holds the horn at the centre; PULSE
 * LOWER and PULSE UPPER are the pulses of full travel, LEFT ANGLE or RIGHT
 * ANGLE at @p us_per_count each count; between, the travel is the share of
 * the way to that pulse (@p square: that share squared); past it the horn
 * holds full travel.  With REVERSION off LEFT ANGLE limits the short
 * pulses, with it on the long ones; the horn's direction is not turned, so
 * the stops stay where they are.  A real servo's unit, side and shape are
 * not known (docs/ServoProgrammer.md).
 */
static inline float sim_horn_us(const uint8_t *reg, uint16_t cmd_us,
                                float us_per_count, bool square)
{
    kst_image_t img;
    int32_t n = 0, lo = 0, hi = 0;
    float neutral, d, span, share;
    bool low, rev;
    unsigned angle;

    memcpy(img.r, reg, sizeof(img.r));
    (void)kst_field_display(KST_F_NEUTRAL,
                            kst_field_get(&img, KST_F_NEUTRAL), &n);
    (void)kst_field_display(KST_F_PULSE_LOWER,
                            kst_field_get(&img, KST_F_PULSE_LOWER), &lo);
    (void)kst_field_display(KST_F_PULSE_UPPER,
                            kst_field_get(&img, KST_F_PULSE_UPPER), &hi);
    neutral = (float)n / 10.0f;
    low = (float)cmd_us < neutral;
    rev = kst_field_get(&img, KST_F_REVERSION) != 0u;
    angle = kst_field_get(&img, (low != rev) ? KST_F_LEFT_ANGLE
                                             : KST_F_RIGHT_ANGLE);
    d = low ? neutral - (float)cmd_us : (float)cmd_us - neutral;
    span = low ? neutral - (float)lo / 10.0f : (float)hi / 10.0f - neutral;
    share = span > 0.0f ? d / span : 0.0f;
    if (share > 1.0f) {
        share = 1.0f;
    }
    if (square) {
        share *= share;
    }
    d = us_per_count * (float)angle * share;
    return low ? neutral - d : neutral + d;
}

/* sim_horn_us() as the whole microsecond a servo model is commanded. */
static inline uint16_t sim_horn_cmd(const uint8_t *reg, uint16_t cmd_us,
                                    float us_per_count, bool square)
{
    const float h = sim_horn_us(reg, cmd_us, us_per_count, square);

    return (uint16_t)(h < 1.0f ? 1.0f : h > 65000.0f ? 65000.0f : h + 0.5f);
}

/* --- fault script ------------------------------------------------------------ */

typedef enum {
    SIM_ANY = 0,
    SIM_READ,
    SIM_WRITE,
    SIM_SYNC,
} sim_kind_t;

typedef enum {
    SIM_F_NONE = 0,
    SIM_F_NO_REPLY,      /* the servo acts and stays silent */
    SIM_F_DEAF,          /* the servo does not act and stays silent */
    SIM_F_READ_VALUE,    /* a read answers arg */
    SIM_F_WRITE_IGNORE,  /* a write answers and stores nothing */
    SIM_F_WRITE_TO,      /* a write lands in register arg instead */
    SIM_F_WRITE_ALSO,    /* a write lands in register arg as well */
    SIM_F_WRITE_VALUE,   /* a write stores arg */
    SIM_F_SHAPE_DELAY,   /* the reply starts arg ns after the frame */
    SIM_F_SHAPE_HALF,    /* the reply runs at a half-cell of arg ns */
    SIM_F_SHAPE_GLITCH,  /* a 1us pulse arg ns after the reply's first edge */
    SIM_F_STALL,         /* the driver never finishes */
    SIM_F_POLL_LATE,     /* the driver finishes arg us late */
    SIM_F_START_FAIL,    /* the driver refuses the frame */
} sim_action_t;

typedef struct {
    sim_kind_t kind;     /* transactions the fault looks at */
    int reg;             /* -1: any register */
    unsigned skip;       /* matching transactions to let pass first */
    unsigned count;      /* transactions to bend */
    sim_action_t action;
    uint32_t arg;
} sim_fault_t;

#define SIM_MAX_FAULTS 8u
#define SIM_LOG_MAX    4096u

typedef struct {
    uint8_t kind;        /* sim_kind_t; SIM_ANY for a frame that is none */
    uint8_t reg;
    uint8_t value;
    uint32_t t_us;       /* clock at start() */
} sim_frame_t;

typedef struct {
    /* the servo */
    uint8_t reg[KST_REG_COUNT];
    bool powered;
    bool in_mode;
    bool needs_power_up;     /* only the first sync after a power-up enters */
    bool fresh;              /* powered up, and no sync since */
    unsigned power_ups;
    uint32_t power_up_us;    /* clock at the last power-up */
    uint32_t sync_taken_us;  /* clock at the sync that entered, last */
    uint8_t acting[KST_REG_COUNT];  /* what the servo acts on */
    /* the wire */
    uint32_t now_us;
    bool busy;
    bool stalled;
    uint32_t done_us;
    uint32_t end_us;         /* end of the last window */
    bool had_window;
    bool last_silent;
    kst_capture_t cap;
    /* what the master did */
    unsigned n_frames;
    unsigned n_reads;
    unsigned n_writes;
    unsigned n_syncs;
    unsigned n_bad_frames;   /* levels that are no frame */
    unsigned n_bad_writes;   /* register 0x00 or an address above 0x1F */
    unsigned n_overlap;      /* start() while a transaction ran */
    uint32_t min_gap_us;             /* window end to the next frame */
    uint32_t min_gap_after_silence_us;
    sim_frame_t log[SIM_LOG_MAX];
    /* faults */
    sim_fault_t fault[SIM_MAX_FAULTS];
    unsigned n_faults;
} sim_t;

static inline void sim_init(sim_t *sim)
{
    const kst_image_t img = sim_bench_image();

    memset(sim, 0, sizeof(*sim));
    memcpy(sim->reg, img.r, sizeof(sim->reg));
    memcpy(sim->acting, img.r, sizeof(sim->acting));
    sim->powered = true;
    sim->min_gap_us = 0xFFFFFFFFu;
    sim->min_gap_after_silence_us = 0xFFFFFFFFu;
}

static inline void sim_add_fault(sim_t *sim, sim_kind_t kind, int reg,
                                 unsigned skip, unsigned count,
                                 sim_action_t action, uint32_t arg)
{
    sim_fault_t *f = &sim->fault[sim->n_faults++];

    f->kind = kind;
    f->reg = reg;
    f->skip = skip;
    f->count = count;
    f->action = action;
    f->arg = arg;
}

/* The fault for this transaction, if one applies. */
static inline sim_action_t sim_take_fault(sim_t *sim, sim_kind_t kind,
                                          unsigned reg, uint32_t *arg)
{
    for (unsigned i = 0; i < sim->n_faults; ++i) {
        sim_fault_t *f = &sim->fault[i];

        if ((f->kind != SIM_ANY && f->kind != kind)
            || (f->reg >= 0 && (unsigned)f->reg != reg) || f->count == 0u) {
            continue;
        }
        if (f->skip > 0u) {
            f->skip--;
            continue;
        }
        f->count--;
        *arg = f->arg;
        return f->action;
    }
    return SIM_F_NONE;
}

/*
 * The servo's own reading of a master frame: one low half-cell in front,
 * then pairs of half-cells, low-high a 1 and high-low a 0.  Returns the
 * number of bits, 0 for levels that are not Manchester code.
 */
static inline unsigned sim_read_frame(const kst_frame_t *frame, uint8_t *bits,
                                      unsigned max_bits)
{
    unsigned n = 0;
    bool first = false;  /* the low half-cell in front */

    for (unsigned i = 0; i <= frame->n_half; i += 2u) {
        /* Past the end the line is low. */
        const bool second = kst_frame_level(frame, i);

        if (first == second || n == max_bits) {
            return 0;
        }
        bits[n++] = second ? 1u : 0u;
        first = kst_frame_level(frame, i + 1u);
    }
    return first ? 0u : n;
}

static inline unsigned sim_bits(const uint8_t *bits, unsigned from,
                                unsigned count)
{
    unsigned v = 0;

    for (unsigned i = 0; i < count; ++i) {
        v = (v << 1) | bits[from + i];
    }
    return v;
}

static inline void sim_store(sim_t *sim, unsigned reg, unsigned value)
{
    sim->reg[reg & KST_REG_MAX] = (uint8_t)value;
}

/* The servo's supply switched on or off.  Off ends programming mode; on
 * makes the next sync the first after a power-up and the registers the
 * values the servo acts on. */
static inline void sim_power(sim_t *sim, bool on)
{
    if (on == sim->powered) {
        return;
    }
    sim->powered = on;
    sim->in_mode = false;
    sim->fresh = on;
    if (on) {
        sim->power_ups++;
        sim->power_up_us = sim->now_us;
        memcpy(sim->acting, sim->reg, sizeof(sim->acting));
    }
}

static inline bool sim_start(void *ctx, const kst_frame_t *frame,
                             uint32_t window_ns)
{
    sim_t *sim = (sim_t *)ctx;
    uint8_t frame_bits[64] = {0};
    const unsigned n = sim_read_frame(frame, frame_bits, 64);
    sim_kind_t kind = SIM_ANY;
    unsigned reg_no = 0;
    unsigned value = 0;
    sim_action_t action;
    uint32_t arg = 0;
    sim_shape_t shape = sim_shape(SIM_READ_DELAY_NS);
    bool reply = false;
    unsigned flags = 3;
    unsigned reply_value = 0;

    if (sim->busy) {
        sim->n_overlap++;
    }
    if (n == KST_FRAME_BITS && frame_bits[0] == 1u) {
        const unsigned prefix = sim_bits(frame_bits, 1, 10);

        reg_no = sim_bits(frame_bits, 11, 8);
        value = sim_bits(frame_bits, 19, 8);
        if (prefix == 0x200u && value == 0u) {
            kind = SIM_READ;
        } else if (prefix == 0x080u) {
            kind = SIM_WRITE;
        }
    } else if (n == KST_SYNC_PULSES && sim_bits(frame_bits, 0, 32) == 0xFFFFFFFFu
               && sim_bits(frame_bits, 32, 32) == 0xFFFFFFFFu) {
        kind = SIM_SYNC;
    }

    action = sim_take_fault(sim, kind, reg_no, &arg);
    if (action == SIM_F_START_FAIL) {
        return false;
    }

    /* The master's conduct. */
    if (sim->had_window) {
        const uint32_t gap = sim->now_us - sim->end_us;

        if (gap < sim->min_gap_us) {
            sim->min_gap_us = gap;
        }
        if (sim->last_silent && gap < sim->min_gap_after_silence_us) {
            sim->min_gap_after_silence_us = gap;
        }
    }
    if (sim->n_frames < SIM_LOG_MAX) {
        sim_frame_t *l = &sim->log[sim->n_frames];

        l->kind = (uint8_t)kind;
        l->reg = (uint8_t)reg_no;
        l->value = (uint8_t)value;
        l->t_us = sim->now_us;
    }
    sim->n_frames++;
    if (kind == SIM_ANY) {
        sim->n_bad_frames++;
    }
    if (kind == SIM_WRITE && (reg_no == 0u || reg_no > KST_REG_MAX)) {
        sim->n_bad_writes++;
    }

    /* The servo. */
    if (sim->powered && action != SIM_F_DEAF) {
        if (kind == SIM_SYNC) {
            const bool takes = !sim->needs_power_up || sim->fresh;

            sim->n_syncs++;
            sim->fresh = false;
            if (!sim->in_mode && takes) {
                sim->in_mode = true;
                sim->sync_taken_us = sim->now_us;
                reply = true;
                flags = 2;
            }
        } else if (kind == SIM_READ && sim->in_mode) {
            sim->n_reads++;
            reply = true;
            reply_value = action == SIM_F_READ_VALUE
                              ? arg : sim->reg[reg_no & KST_REG_MAX];
        } else if (kind == SIM_WRITE && sim->in_mode) {
            sim->n_writes++;
            reply = true;
            shape.delay_ns = SIM_WRITE_DELAY_NS;
            /* The reply to a write is not an echo: 0x1E answers 0x26. */
            reply_value = (reg_no & KST_REG_MAX) == 0x1Eu ? 0x26u : value;
            if (action == SIM_F_WRITE_TO) {
                sim_store(sim, arg, value);
            } else if (action == SIM_F_WRITE_VALUE) {
                sim_store(sim, reg_no, arg);
            } else if (action != SIM_F_WRITE_IGNORE) {
                sim_store(sim, reg_no, value);
                if (action == SIM_F_WRITE_ALSO) {
                    sim_store(sim, arg, value);
                }
            }
        }
    }
    if (action == SIM_F_NO_REPLY) {
        reply = false;
    } else if (action == SIM_F_SHAPE_DELAY) {
        shape.delay_ns = arg;
    } else if (action == SIM_F_SHAPE_HALF) {
        shape.half_ns = arg;
    } else if (action == SIM_F_SHAPE_GLITCH) {
        shape.glitch_at_ns = shape.delay_ns + arg;
        shape.glitch_ns = 1000;
    }

    if (reply) {
        sim_reply(&sim->cap, window_ns, flags, reply_value, &shape);
    } else {
        sim_cap_clear(&sim->cap, window_ns);
    }
    sim->last_silent = !reply;
    sim->busy = true;
    sim->stalled = action == SIM_F_STALL;
    sim->done_us = sim->now_us
                   + (kst_frame_duration_ns(frame) + window_ns + 999u) / 1000u
                   + (action == SIM_F_POLL_LATE ? arg : 0u);
    return true;
}

static inline bool sim_poll(void *ctx, kst_capture_t *out)
{
    sim_t *sim = (sim_t *)ctx;

    if (!sim->busy || sim->stalled
        || (int32_t)(sim->now_us - sim->done_us) < 0) {
        return false;
    }
    *out = sim->cap;
    sim->busy = false;
    sim->had_window = true;
    sim->end_us = sim->now_us;
    return true;
}

static inline uint32_t sim_now(void *ctx)
{
    return ((sim_t *)ctx)->now_us;
}

static inline kst_driver_t sim_driver(sim_t *sim)
{
    kst_driver_t d;

    d.ctx = sim;
    d.start = sim_start;
    d.poll = sim_poll;
    d.now_us = sim_now;
    return d;
}

/* Step the session until the operation ends; the clock moves 100us per
 * step.  10s of simulated time is the bound. */
static inline kst_ses_t sim_run(sim_t *sim, kst_session_t *s)
{
    for (unsigned i = 0; i < 100000u; ++i) {
        const kst_ses_t r = kst_session_step(s);

        if (r != KST_SES_BUSY) {
            return r;
        }
        sim->now_us += 100u;
    }
    return KST_SES_BUSY;
}

#endif /* KST_SIM_H */
