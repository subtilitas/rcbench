/*
 * The automatic servo test.  See servo_test.h for the method.
 *
 * SPDX-License-Identifier: MIT
 */

#include "servo_test.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void mean_add(servo_test_mean_t *m, float v)
{
    m->sum += v;
    ++m->n;
}

static float mean_of(const servo_test_mean_t *m)
{
    return (m->n > 0u) ? m->sum / (float)m->n : 0.0f;
}

/* The angle in the commanded direction: with REVERSE on, the horn turns
 * the other way for the same command, and the angle is negated. */
static float enc_dir_deg(const servo_test_t *t, uint16_t raw)
{
    const float d = servo_test_enc_deg(raw, t->cfg.enc_centre);
    return t->cfg.reverse ? -d : d;
}

float servo_test_enc_deg(uint16_t raw, uint16_t centre)
{
    int d = (int)((unsigned)(raw - centre) & 4095u);
    if (d >= 2048) {
        d -= 4096;
    }
    return (float)d * (360.0f / 4096.0f);
}

void servo_test_meter_pdmini(servo_test_meter_t *m)
{
    if (m == NULL) {
        return;
    }
    memset(m, 0, sizeof(*m));
    snprintf(m->name, sizeof(m->name), "PD mini");
    m->lag_ms      = SERVO_TEST_PDMINI_LAG_MS;
    m->repeats     = true;
    m->upper_bound = true;
}

/* The step's threshold, once its idle readings are in: the larger of the
 * smallest and SERVO_TEST_NOISE_K times their standard deviation. */
static void set_threshold(servo_test_step_t *s)
{
    s->noise_a = servo_move_noise_a(s->idle.n, s->idle.sum, s->idle_sq);
    s->move_a  = servo_move_threshold_a(s->noise_a);
}

void servo_test_init(servo_test_t *t)
{
    if (t == NULL) {
        return;
    }
    memset(t, 0, sizeof(*t));
    t->ended = true;        /* nothing to hand over */
}

bool servo_test_running(const servo_test_t *t)
{
    return t != NULL && t->state == SERVO_TEST_RUNNING;
}

/* The meter's lag is added to the window: see servo_move_window_ms(). */
uint32_t servo_test_travel_window_ms(const servo_test_t *t)
{
    return servo_move_window_ms((t != NULL) ? (uint32_t)t->cfg.meter.lag_ms
                                            : 0u);
}

/* ------------------------------------------------------------ the outbox */

static void put_line(servo_test_t *t, servo_test_out_t kind, const char *text)
{
    if (t->box_n >= SERVO_TEST_OUTBOX) {
        /* The card fell behind: the row is lost and counted, and the
         * report says how many. */
        if (kind == SERVO_TEST_OUT_CSV) {
            ++t->rows_lost;
        }
        return;
    }
    const unsigned at = (t->box_head + t->box_n) % SERVO_TEST_OUTBOX;
    snprintf(t->box[at], SERVO_TEST_LINE_MAX, "%s", text);
    t->box_kind[at] = (uint8_t)kind;
    ++t->box_n;
    if (kind == SERVO_TEST_OUT_CSV) {
        ++t->rows;
    }
}

servo_test_out_t servo_test_peek(servo_test_t *t, const char **text)
{
    static const char empty[] = "";
    if (text != NULL) {
        *text = empty;
    }
    if (t == NULL) {
        return SERVO_TEST_OUT_NONE;
    }
    if (t->box_n > 0u) {
        if (text != NULL) {
            *text = t->box[t->box_head];
        }
        return (servo_test_out_t)t->box_kind[t->box_head];
    }
    if (t->state != SERVO_TEST_DONE || t->ended) {
        return SERVO_TEST_OUT_NONE;
    }
    if (t->cfg.report
        && servo_report_line(t, t->report_line, t->scratch,
                             sizeof(t->scratch))) {
        if (text != NULL) {
            *text = t->scratch;
        }
        return SERVO_TEST_OUT_TXT;
    }
    return SERVO_TEST_OUT_END;
}

void servo_test_pop(servo_test_t *t)
{
    if (t == NULL) {
        return;
    }
    if (t->box_n > 0u) {
        t->box_head = (uint8_t)((t->box_head + 1u) % SERVO_TEST_OUTBOX);
        --t->box_n;
        return;
    }
    if (t->state != SERVO_TEST_DONE || t->ended) {
        return;
    }
    if (t->cfg.report
        && servo_report_line(t, t->report_line, t->scratch,
                             sizeof(t->scratch))) {
        ++t->report_line;
        return;
    }
    t->ended = true;
}

bool servo_test_drained(const servo_test_t *t)
{
    return t == NULL || (t->state != SERVO_TEST_RUNNING && t->box_n == 0u
                         && t->ended);
}

/* --------------------------------------------------------------- the run */

static servo_test_step_t *cur(servo_test_t *t)
{
    return &t->steps[t->step];
}

/* The move the encoder was judging is over: counted into its step. */
static void enc_close(servo_test_t *t)
{
    if (!t->enc_open) {
        return;
    }
    t->enc_open = false;
    if (!t->enc_counted || !t->enc_ok || t->step_count == 0u) {
        return;
    }
    servo_test_step_t *s = cur(t);
    ++s->enc_moves;
    if (!t->enc_moved) {
        ++s->enc_unmoved;
    } else if (!t->enc_settled) {
        ++s->enc_late;
    } else {
        s->enc_travel_sum_ms += t->enc_travel_ms;
        ++s->enc_travels;
        if (t->enc_travel_ms > s->enc_travel_max_ms) {
            s->enc_travel_max_ms = t->enc_travel_ms;
        }
        mean_add(&s->enc_end[t->enc_end], t->enc_last_deg);
    }
}

/* A move to @p end is commanded at @p now_ms: the encoder judges it from
 * the angle read before. */
static void enc_open(servo_test_t *t, uint8_t end, bool counted,
                     uint32_t now_ms)
{
    if (!t->cfg.enc_on) {
        return;
    }
    t->enc_open       = true;
    t->enc_counted    = counted;
    t->enc_end        = end;
    t->enc_cmd_ms     = now_ms;
    float start = 0.0f;
    t->enc_ok         = servo_test_enc_at(t, now_ms, &start);
    t->enc_start_deg  = start;
    t->enc_last_deg   = start;
    t->enc_moved      = false;
    t->enc_settled    = false;
    t->enc_travel_ms  = 0u;
}

/* The run ends: the output off and the servo let go, whatever ended it. */
static void finish(servo_test_t *t, servo_test_abort_t why, uint32_t now_ms)
{
    if (why != SERVO_TEST_AB_NONE && !t->enc_settled) {
        /* An aborted run cut the move's window short: not counted, as
         * the current-based results do not count it. */
        t->enc_open = false;
    }
    enc_close(t);
    t->state  = SERVO_TEST_DONE;
    t->why    = why;
    t->end_ms = now_ms;
    t->phase  = SERVO_TEST_PH_NONE;
    t->pend.off     = true;
    t->pend.release = true;
    t->pend.on      = false;
    t->pend.set     = false;
    t->pend.command = false;
    t->report_line  = 0u;
    t->ended        = false;
}

void servo_test_abort(servo_test_t *t, servo_test_abort_t why,
                      uint32_t now_ms)
{
    if (servo_test_running(t)) {
        finish(t, why, now_ms);
    }
}

static void command(servo_test_t *t, uint16_t us, uint32_t now_ms)
{
    t->cmd_us = us;
    t->cmd_ms = now_ms;
    t->pend.command = true;
    t->pend.cmd_us  = us;
}

/* The voltage step @p idx begins: its set point, the servo at its centre,
 * and on the first step the output on. */
static void begin_step(servo_test_t *t, uint8_t idx, uint32_t now_ms)
{
    enc_close(t);
    t->step = idx;
    servo_test_step_t *s = cur(t);
    if (s->set_v > t->v_max + 0.001f) {
        finish(t, SERVO_TEST_AB_CAP, now_ms);
        return;
    }
    s->begun      = true;
    t->pend.set   = true;
    t->pend.set_v = s->set_v;
    t->pend.set_i = t->cfg.i_limit;
    if (!t->on_seen) {
        t->pend.on = true;
    }
    command(t, t->cfg.centre_us, now_ms);
    t->hold_known[0] = false;
    t->hold_known[1] = false;
    t->moves_done    = 0u;
    t->placed        = 0u;
    t->phase    = SERVO_TEST_PH_SET;
    t->phase_ms = now_ms;
}

/* A move to end @p end: counted or not, measured against that end's
 * holding level, or the idle current while it is not known. */
static void begin_move(servo_test_t *t, uint8_t end, bool counted,
                       uint32_t now_ms)
{
    const servo_test_step_t *s = cur(t);
    const float idle = mean_of(&s->idle);
    const float to   = t->hold_known[end] ? t->hold_ref[end] : idle;
    /* Where it starts from: the end it holds, or the centre's idle. */
    const uint8_t from = (uint8_t)(end ^ 1u);
    float start = idle;
    if (t->phase == SERVO_TEST_PH_HOLD && t->hold_known[from]) {
        start = t->hold_ref[from];
    }
    enc_close(t);
    t->end     = end;
    t->counted = counted;
    /* The supply's readings one by one: the settled rule over two in a
     * row, and no filter. */
    const servo_move_cfg_t mc = {
        .cmd_t    = now_ms,
        .window_t = servo_test_travel_window_ms(t),
        .rise_a   = start,
        .ref_a    = to,
        .move_a   = s->move_a,
        .band_a   = SERVO_TEST_BAND_A,
        .settle_n = 2u,
        .filter_n = 1u,
    };
    servo_move_begin(&t->move, &mc);
    command(t, end ? t->cfg.end_hi_us : t->cfg.end_lo_us, now_ms);
    enc_open(t, end, counted, now_ms);
    t->phase = SERVO_TEST_PH_MOVE;
}

static void begin_hold(servo_test_t *t, uint32_t from_ms)
{
    t->phase        = SERVO_TEST_PH_HOLD;
    t->hold_from_ms = from_ms;
    memset(&t->hold_now, 0, sizeof(t->hold_now));
}

/* The move is over: arrived at @p at_ms, or timed out. */
static void end_move(servo_test_t *t, bool arrived, uint32_t at_ms)
{
    servo_test_step_t *s = cur(t);
    const bool rose = servo_move_moved(&t->move);
    if (rose) {
        s->moved = true;
    }
    if (t->counted) {
        ++s->moves;
        if (!rose) {
            /* Unseen: whether it moved, the current cannot say. */
            ++s->no_rise;
        }
        if (arrived) {
            const uint32_t ms = at_ms - t->cmd_ms;
            s->travel_sum_ms += ms;
            ++s->travels;
            if (ms > s->travel_max_ms) {
                s->travel_max_ms = ms;
            }
            t->travel_now_ms = (ms > 0u) ? ms : 1u;
            s->move.sum += t->move.sum;
            s->move.n   += t->move.n;
            if (t->move.peak > s->move_peak_a) {
                s->move_peak_a = t->move.peak;
            }
        } else if (rose) {
            ++s->timeouts;
        }
        ++t->moves_done;
    } else {
        ++t->placed;
    }
    begin_hold(t, at_ms);
}

static uint32_t hold_ms(const servo_test_t *t)
{
    return (t->cfg.dwell_ms > SERVO_TEST_HOLD_MIN_MS) ? t->cfg.dwell_ms
                                                     : SERVO_TEST_HOLD_MIN_MS;
}

/* Append the brown-out step at @p v and begin it. */
static void begin_brownout(servo_test_t *t, float v, uint32_t now_ms)
{
    if (t->step_count >= SERVO_TEST_STEP_SLOTS) {
        finish(t, SERVO_TEST_AB_NONE, now_ms);
        return;
    }
    servo_test_step_t *s = &t->steps[t->step_count];
    memset(s, 0, sizeof(*s));
    s->set_v    = v;
    s->brownout = true;
    ++t->step_count;
    begin_step(t, (uint8_t)(t->step_count - 1u), now_ms);
}

static void end_step(servo_test_t *t, uint32_t now_ms)
{
    servo_test_step_t *s = cur(t);
    s->done = true;
    if (!s->brownout) {
        /* The next characterisation step, or the brown-out walk. */
        if ((unsigned)t->step + 1u < t->cfg.step_count) {
            begin_step(t, (uint8_t)(t->step + 1u), now_ms);
        } else if (t->cfg.brownout && t->bo_start_v >= t->floor_v - 0.001f) {
            begin_brownout(t, t->bo_start_v, now_ms);
        } else {
            finish(t, SERVO_TEST_AB_NONE, now_ms);
        }
        return;
    }
    /* Down a step while it still moves; done at the first that shows no
     * movement, or at the floor.  A floor off the step's grid -- a supply
     * whose lowest set point is 3.3 V -- is the last step itself. */
    float next = s->set_v - SERVO_TEST_BROWNOUT_STEP_V;
    if (next < t->floor_v - 0.001f && s->set_v > t->floor_v + 0.001f) {
        next = t->floor_v;
    }
    if (!s->moved || next < t->floor_v - 0.001f) {
        finish(t, SERVO_TEST_AB_NONE, now_ms);
        return;
    }
    begin_brownout(t, next, now_ms);
}

/* The hold at an end is over: its level, and the next move or the step's
 * end. */
static void end_hold(servo_test_t *t, uint32_t now_ms)
{
    if (t->hold_now.n > 0u) {
        t->hold_ref[t->end]   = mean_of(&t->hold_now);
        t->hold_known[t->end] = true;
    }
    const servo_test_step_t *s = cur(t);
    if (s->brownout) {
        if (t->moves_done >= SERVO_TEST_BROWNOUT_MOVES) {
            end_step(t, now_ms);
        } else {
            begin_move(t, (uint8_t)(t->end ^ 1u), true, now_ms);
        }
        return;
    }
    /* Both ends placed first, uncounted, so each end's holding level is
     * known before a counted move falls back to it. */
    if (t->placed < 2u) {
        begin_move(t, 1u, false, now_ms);
        return;
    }
    if (t->moves_done == 0u) {
        t->moves_from_ms = now_ms;   /* the counted moves start here */
    }
    bool more = t->moves_done < SERVO_TEST_MOVES_MAX;
    if (t->cfg.by_moves) {
        more = more && t->moves_done < t->cfg.moves;
    } else {
        more = more && (now_ms - t->moves_from_ms)
                           < (uint32_t)t->cfg.time_s * 1000u;
    }
    if (more) {
        begin_move(t, (uint8_t)(t->end ^ 1u), true, now_ms);
    } else {
        end_step(t, now_ms);
    }
}

servo_test_start_t servo_test_start(servo_test_t *t,
                                    const servo_test_cfg_t *cfg,
                                    uint32_t now_ms,
                                    const servo_test_reading_t *last,
                                    float v_min, float v_max)
{
    if (t == NULL || cfg == NULL) {
        return SERVO_TEST_START_NO_STEPS;
    }
    if (cfg->step_count == 0u && !cfg->brownout) {
        return SERVO_TEST_START_NO_STEPS;
    }
    if (cfg->step_count > SERVO_TEST_STEPS_MAX) {
        return SERVO_TEST_START_NO_STEPS;
    }
    for (unsigned k = 0; k < cfg->step_count; ++k) {
        if (cfg->steps_v[k] > v_max + 0.001f
            || cfg->steps_v[k] < v_min - 0.001f) {
            return SERVO_TEST_START_ABOVE_CAP;
        }
    }
    if (last == NULL || !last->online) {
        return SERVO_TEST_START_NO_SUPPLY;
    }
    if (!(cfg->end_lo_us < cfg->centre_us && cfg->centre_us < cfg->end_hi_us)) {
        return SERVO_TEST_START_BAD_ENDS;
    }

    servo_test_init(t);
    t->cfg      = *cfg;
    t->cfg.dut[sizeof(t->cfg.dut) - 1u]           = '\0';
    t->cfg.type[sizeof(t->cfg.type) - 1u]         = '\0';
    t->cfg.danger[sizeof(t->cfg.danger) - 1u]     = '\0';
    t->cfg.firmware[sizeof(t->cfg.firmware) - 1u] = '\0';
    t->cfg.meter.name[sizeof(t->cfg.meter.name) - 1u] = '\0';
    t->state    = SERVO_TEST_RUNNING;
    t->ended    = false;
    t->start_ms = now_ms;
    t->v_max    = v_max;
    t->floor_v  = (v_min > SERVO_TEST_BROWNOUT_FLOOR_V)
                      ? v_min : SERVO_TEST_BROWNOUT_FLOOR_V;
    t->bo_start_v = (v_max < SERVO_TEST_BROWNOUT_START_V)
                        ? v_max : SERVO_TEST_BROWNOUT_START_V;
    /* The output already on is not switched on again, and is watched from
     * the start. */
    t->on_seen  = last->output;
    for (unsigned k = 0; k < cfg->step_count; ++k) {
        t->steps[k].set_v = cfg->steps_v[k];
    }
    t->step_count = cfg->step_count;

    put_line(t, SERVO_TEST_OUT_OPEN, "");
    put_line(t, SERVO_TEST_OUT_CSV, cfg->enc_on ? servo_test_csv_header_enc()
                                                : servo_test_csv_header());
    t->rows = 0u;           /* the header is not a reading */

    if (cfg->step_count > 0u) {
        begin_step(t, 0u, now_ms);
    } else if (t->bo_start_v >= t->floor_v - 0.001f) {
        begin_brownout(t, t->bo_start_v, now_ms);
    } else {
        finish(t, SERVO_TEST_AB_NONE, now_ms);
    }
    return SERVO_TEST_START_OK;
}

/* One row of the CSV, for a reading. */
static void log_row(servo_test_t *t, const servo_test_reading_t *r,
                    uint16_t position_us)
{
    static const char *const k_mode[] = { "OFF", "CV", "CC" };
    const servo_test_step_t *s = cur(t);
    const uint32_t since = ((int32_t)(r->taken_ms - t->start_ms) > 0)
                               ? r->taken_ms - t->start_ms : 0u;
    char pos[8] = "";
    if (position_us != 0u) {
        snprintf(pos, sizeof(pos), "%u", (unsigned)position_us);
    }
    char travel[12] = "";
    if (t->travel_now_ms != 0u) {
        snprintf(travel, sizeof(travel), "%lu", (unsigned long)t->travel_now_ms);
        t->travel_now_ms = 0u;
    }
    char line[SERVO_TEST_LINE_MAX];
    snprintf(line, sizeof(line),
             "%lu.%03lu;%s;%u;%s;%u;%s;%.2f;%.3f;%.2f;%.3f;%.3f;%s;%s",
             (unsigned long)(since / 1000u), (unsigned long)(since % 1000u),
             servo_str(s->brownout ? SERVO_STR_TEST_BROWNOUT
                                   : SERVO_STR_TEST_STEP),
             (unsigned)t->step + 1u, servo_test_phase_name(t->phase),
             (unsigned)t->cmd_us, pos, (double)s->set_v, (double)r->v,
             (double)t->cfg.i_limit, (double)r->i, (double)(r->v * r->i),
             (r->mode < 3u) ? k_mode[r->mode] : "", travel);
    if (t->cfg.enc_on) {
        /* The angle, while it is a reading younger than the start angle may
         * be, and the settle once, on the row after it was found. */
        const size_t used = strlen(line);
        char angle[12] = "";
        float deg = 0.0f;
        if (servo_test_enc_at(t, r->taken_ms, &deg)) {
            snprintf(angle, sizeof(angle), "%.2f", (double)deg);
        }
        char settle[12] = "";
        /* On the first row taken at or after the reading that found it:
         * a row older than that reading waits for the next. */
        if (t->enc_travel_now_ms != 0u
            && (int32_t)(r->taken_ms - t->enc_travel_at_ms) >= 0) {
            snprintf(settle, sizeof(settle), "%lu",
                     (unsigned long)t->enc_travel_now_ms);
            t->enc_travel_now_ms = 0u;
        }
        snprintf(line + used, sizeof(line) - used, ";%s;%s", angle, settle);
    }
    put_line(t, SERVO_TEST_OUT_CSV, line);
}

/* A new reading, measured by the phase it falls in. */
static void measure(servo_test_t *t, const servo_test_reading_t *r)
{
    servo_test_step_t *s = cur(t);
    const float i = r->i;
    const uint32_t at = r->taken_ms;

    if (t->phase != SERVO_TEST_PH_SET && t->phase != SERVO_TEST_PH_SETTLE) {
        mean_add(&s->v, r->v);
        if (i > s->peak_a) {
            s->peak_a = i;
        }
        if (!s->brownout && i > t->stall_peak_a) {
            t->stall_peak_a = i;
        }
    }
    /* STALL AT, wherever the run is: counted on a characterisation step,
     * and ending the run once it lasts. */
    if (i > t->cfg.stall_a) {
        if (!s->brownout && t->phase != SERVO_TEST_PH_SET
            && t->phase != SERVO_TEST_PH_SETTLE) {
            t->stalled = true;
        }
        if (!t->stalling) {
            t->stalling       = true;
            t->stall_since_ms = at;
        } else if (at - t->stall_since_ms >= SERVO_TEST_STALL_ABORT_MS) {
            finish(t, SERVO_TEST_AB_STALL, at);
            return;
        }
    } else {
        t->stalling = false;
    }

    switch (t->phase) {
    case SERVO_TEST_PH_SET:
        if (t->on_seen && fabsf(r->set_v - s->set_v) <= SERVO_TEST_SET_TOL_V) {
            t->phase    = SERVO_TEST_PH_SETTLE;
            t->phase_ms = at;
        }
        break;
    case SERVO_TEST_PH_IDLE:
        if ((int32_t)(at - t->phase_ms) >= 0) {
            mean_add(&s->idle, i);
            s->idle_sq += i * i;
        }
        break;
    case SERVO_TEST_PH_MOVE:
        /* Movement and arrival by servo_move's rules; a reading taken
         * before the command is not the move. */
        servo_move_sample(&t->move, at, i, SERVO_MOVE_CLIP_NONE);
        if (servo_move_arrived(&t->move)) {
            end_move(t, true, t->move.end_t);
        }
        break;
    case SERVO_TEST_PH_HOLD:
        if ((int32_t)(at - t->hold_from_ms) > 0) {
            mean_add(&t->hold_now, i);
            mean_add(&s->hold[t->end], i);
        }
        break;
    default:
        break;
    }
}

void servo_test_reading(servo_test_t *t, const servo_test_reading_t *r,
                        uint16_t position_us)
{
    if (!servo_test_running(t) || r == NULL) {
        return;
    }
    /* The supply's state, on every sample. */
    if (!r->online) {
        finish(t, SERVO_TEST_AB_SUPPLY_LOST, r->taken_ms);
        return;
    }
    if (t->on_seen && !r->output) {
        finish(t, (r->trip != 0u) ? SERVO_TEST_AB_TRIPPED
                                  : SERVO_TEST_AB_SUPPLY_OFF, r->taken_ms);
        return;
    }
    if (r->output) {
        t->on_seen = true;
    }
    /* A new reading only once: the sample count moved, and both values
     * arrived. */
    if (!r->ok || (t->have_reading && r->samples == t->samples)) {
        return;
    }
    if (t->have_reading) {
        const uint16_t step = (uint16_t)(r->samples - t->samples);
        /* A step of half the range or more is the count starting again
         * after a coprocessor restart, not readings taken: counted as one. */
        const bool restarted = step >= 0x8000u;
        t->module_samples += restarted ? 1u : step;
        /* Readings the supply took between two the test saw: not measured,
         * and counted for the report.  A move between them is timed from
         * the next one that arrives, so the interval the report states is
         * the measured one, skips included. */
        if (!restarted && step > 1u) {
            t->skipped += (uint32_t)step - 1u;
        }
    } else {
        t->first_ms = r->taken_ms;
    }
    t->have_reading = true;
    t->samples      = r->samples;
    t->last_ms      = r->taken_ms;
    ++t->readings;

    measure(t, r);
    log_row(t, r, position_us);
}

bool servo_test_enc_at(const servo_test_t *t, uint32_t at_ms, float *deg)
{
    bool     found = false;
    uint32_t best  = 0u;                 /* the age of the best so far */
    uint16_t raw   = 0u;
    for (unsigned k = 0u; k < t->enc_hist_n; ++k) {
        const servo_test_enc_sample_t *h = &t->enc_hist[k];
        if ((int32_t)(at_ms - h->ms) < 0) {
            continue;                    /* taken after the row */
        }
        const uint32_t age = at_ms - h->ms;
        if (!found || age < best) {
            found = true;
            best  = age;
            raw   = h->raw;
        }
    }
    if (!found || best > SERVO_TEST_ENC_STALE_MS) {
        return false;
    }
    *deg = enc_dir_deg(t, raw);
    return true;
}

void servo_test_encoder(servo_test_t *t, const servo_test_enc_t *e)
{
    if (t == NULL || e == NULL) {
        return;
    }
    if (!e->valid || e->gap) {
        /* The angle is not known across a gap: nor is the move open over
         * it, unless it had settled before.  A reading that follows lost
         * ones ends the validity as one that is not valid does, and is
         * kept after it. */
        t->enc_hist_n    = 0u;
        t->enc_hist_next = 0u;
        if (t->enc_open && !t->enc_settled) {
            t->enc_ok = false;
        }
        if (!e->valid) {
            return;
        }
    }
    t->enc_hist[t->enc_hist_next].ms  = e->taken_ms;
    t->enc_hist[t->enc_hist_next].raw = e->raw;
    t->enc_hist_next = (uint8_t)((t->enc_hist_next + 1u) % SERVO_TEST_ENC_HIST);
    if (t->enc_hist_n < SERVO_TEST_ENC_HIST) {
        ++t->enc_hist_n;
    }
    const float deg = enc_dir_deg(t, e->raw);
    if (!servo_test_running(t) || !t->cfg.enc_on) {
        return;
    }
    ++t->enc_reads;
    /* A reading taken before the command is not the move. */
    if (!t->enc_open || !t->enc_ok
        || (int32_t)(e->taken_ms - t->enc_cmd_ms) < 0) {
        return;
    }
    t->enc_last_deg = deg;
    if (!t->enc_moved
        && fabsf(deg - t->enc_start_deg) > SERVO_TEST_ENC_MOVED_DEG) {
        t->enc_moved = true;
    }
    if (t->enc_moved && !t->enc_settled
        && e->still_ms >= SERVO_TEST_ENC_HOLD_MS) {
        /* When the stillness began, on the panel's clock; it must have
         * begun after the command, or it is the stillness before it. */
        const int32_t began =
            (int32_t)(e->taken_ms - e->still_ms - t->enc_cmd_ms);
        if (began >= 0) {
            t->enc_settled   = true;
            t->enc_travel_ms = (uint32_t)began;
            t->enc_travel_now_ms = (began > 0) ? (uint32_t)began : 1u;
            t->enc_travel_at_ms  = e->taken_ms;
        }
    }
}

void servo_test_step(servo_test_t *t, uint32_t now_ms,
                     const servo_test_in_t *in, servo_test_do_t *out)
{
    if (t == NULL) {
        return;
    }
    if (servo_test_running(t) && in != NULL) {
        t->v_max = in->v_max;
        if (!in->armed) {
            finish(t, SERVO_TEST_AB_DISARMED, now_ms);
        }
    }
    if (servo_test_running(t)) {
        const uint32_t heard = t->have_reading ? t->last_ms : t->start_ms;
        if ((int32_t)(now_ms - heard) > (int32_t)SERVO_TEST_STALE_MS) {
            finish(t, SERVO_TEST_AB_STALE, now_ms);
        }
    }
    if (servo_test_running(t)) {
        const uint32_t in_phase = now_ms - t->phase_ms;
        switch (t->phase) {
        case SERVO_TEST_PH_SET:
            if (in_phase > SERVO_TEST_SET_TIMEOUT_MS) {
                finish(t, t->on_seen ? SERVO_TEST_AB_SET_NOT_TAKEN
                                     : SERVO_TEST_AB_NOT_ON, now_ms);
            }
            break;
        case SERVO_TEST_PH_SETTLE:
            if ((int32_t)(now_ms - t->phase_ms) >= (int32_t)t->cfg.settle_ms) {
                t->phase    = SERVO_TEST_PH_IDLE;
                t->phase_ms = now_ms;
            }
            break;
        case SERVO_TEST_PH_IDLE:
            if (in_phase >= SERVO_TEST_IDLE_MS) {
                if (cur(t)->idle.n == 0u) {
                    finish(t, SERVO_TEST_AB_STALE, now_ms);
                    break;
                }
                set_threshold(cur(t));
                if (cur(t)->brownout) {
                    /* From the centre to the high end, then back. */
                    begin_move(t, 1u, true, now_ms);
                } else {
                    /* To the low end, not counted: the counted moves go
                     * end to end. */
                    begin_move(t, 0u, false, now_ms);
                }
            }
            break;
        case SERVO_TEST_PH_MOVE:
            servo_move_tick(&t->move, now_ms);
            if (servo_move_over(&t->move)) {
                end_move(t, false, now_ms);
            }
            break;
        case SERVO_TEST_PH_HOLD:
            if ((int32_t)(now_ms - t->hold_from_ms) >= (int32_t)hold_ms(t)) {
                end_hold(t, now_ms);
            }
            break;
        default:
            break;
        }
    }
    if (out != NULL) {
        *out = t->pend;
    }
    memset(&t->pend, 0, sizeof(t->pend));
}

/* ----------------------------------------------------------- the results */

servo_test_verdict_t servo_test_verdict(const servo_test_t *t)
{
    if (t == NULL || t->why != SERVO_TEST_AB_NONE) {
        return SERVO_TEST_ABORTED;
    }
    bool fail = t->stalled;
    float a;
    uint32_t ms;
    if (t->cfg.idle_max_a > 0.0f && servo_test_max_idle(t, &a)
        && a > t->cfg.idle_max_a) {
        fail = true;
    }
    if (t->cfg.hold_max_a > 0.0f && servo_test_max_hold(t, &a)
        && a > t->cfg.hold_max_a) {
        fail = true;
    }
    /* A meter whose travel times are an upper bound cannot fail one. */
    if (t->cfg.travel_max_ms > 0u && !t->cfg.meter.upper_bound
        && servo_test_max_travel(t, &ms) && ms > t->cfg.travel_max_ms) {
        fail = true;
    }
    bool unseen = false;
    bool walked = false;
    for (unsigned k = 0; k < t->step_count; ++k) {
        const servo_test_step_t *s = &t->steps[k];
        if (s->brownout) {
            /* A walk that saw no movement at its first voltage measured
             * nothing: the servo may move there under the threshold. */
            if (s->done && !walked) {
                walked = true;
                unseen = unseen || !s->moved;
            }
            continue;
        }
        /* A move that started and never came back to its holding level. */
        if (s->timeouts > 0u) {
            fail = true;
        }
        if (s->no_rise > 0u) {
            unseen = true;
        }
    }
    if (fail) {
        return SERVO_TEST_FAIL;
    }
    return unseen ? SERVO_TEST_NOT_MEASURABLE : SERVO_TEST_PASS;
}

unsigned servo_test_steps_planned(const servo_test_t *t)
{
    if (t == NULL) {
        return 0u;
    }
    return (unsigned)t->cfg.step_count + (t->cfg.brownout ? 1u : 0u);
}

unsigned servo_test_step_now(const servo_test_t *t)
{
    if (t == NULL || t->step_count == 0u) {
        return 0u;
    }
    if (t->steps[t->step].brownout) {
        return (unsigned)t->cfg.step_count + 1u;
    }
    return (unsigned)t->step + 1u;
}

bool servo_test_max_idle(const servo_test_t *t, float *a)
{
    bool any = false;
    float best = 0.0f;
    for (unsigned k = 0; t != NULL && k < t->step_count; ++k) {
        const servo_test_step_t *s = &t->steps[k];
        if (!s->brownout && s->idle.n > 0u) {
            const float v = mean_of(&s->idle);
            if (!any || v > best) {
                best = v;
            }
            any = true;
        }
    }
    if (a != NULL) {
        *a = best;
    }
    return any;
}

bool servo_test_max_hold(const servo_test_t *t, float *a)
{
    bool any = false;
    float best = 0.0f;
    for (unsigned k = 0; t != NULL && k < t->step_count; ++k) {
        const servo_test_step_t *s = &t->steps[k];
        for (unsigned e = 0; e < 2u && !s->brownout; ++e) {
            if (s->hold[e].n > 0u) {
                const float v = mean_of(&s->hold[e]);
                if (!any || v > best) {
                    best = v;
                }
                any = true;
            }
        }
    }
    if (a != NULL) {
        *a = best;
    }
    return any;
}

bool servo_test_max_travel(const servo_test_t *t, uint32_t *ms)
{
    bool any = false;
    uint32_t best = 0u;
    for (unsigned k = 0; t != NULL && k < t->step_count; ++k) {
        const servo_test_step_t *s = &t->steps[k];
        if (!s->brownout && s->travels > 0u) {
            if (s->travel_max_ms > best) {
                best = s->travel_max_ms;
            }
            any = true;
        }
    }
    if (ms != NULL) {
        *ms = best;
    }
    return any;
}

bool servo_test_brownout(const servo_test_t *t, float *moved_v, bool *stopped)
{
    bool any = false;
    float lowest = 0.0f;
    bool none = false;
    for (unsigned k = 0; t != NULL && k < t->step_count; ++k) {
        const servo_test_step_t *s = &t->steps[k];
        if (!s->brownout || !s->done) {
            continue;
        }
        if (s->moved) {
            if (!any || s->set_v < lowest) {
                lowest = s->set_v;
            }
            any = true;
        } else {
            none = true;
        }
    }
    if (moved_v != NULL) {
        *moved_v = lowest;
    }
    if (stopped != NULL) {
        *stopped = none;
    }
    return any;
}

bool servo_test_rates(const servo_test_t *t, float *per_s,
                      float *module_per_s, uint32_t *interval_ms)
{
    if (t == NULL || t->readings < 2u || t->last_ms == t->first_ms) {
        return false;
    }
    const uint32_t span = t->last_ms - t->first_ms;
    const float s = (float)span / 1000.0f;
    if (per_s != NULL) {
        *per_s = (float)(t->readings - 1u) / s;
    }
    if (module_per_s != NULL) {
        *module_per_s = (float)t->module_samples / s;
    }
    if (interval_ms != NULL) {
        const uint32_t gaps = t->readings - 1u;
        *interval_ms = (span + gaps / 2u) / gaps;
    }
    return true;
}
