/*
 * Moves replayed through the move rules of shared/servo/servo_move.c, for
 * tools/sense_trace.py: the samples of a trace in, what the rules make of
 * each move out.  The rules are the ones the coprocessor's capture runs,
 * linked here and not written again, and the samples are fed as the
 * capture feeds them (sense_sched.c, cap_step()): each sample judged,
 * then the clock moved to it.
 *
 * Input, on stdin, one record a line:
 *
 *   m CMD WINDOW RISE REF MOVE BAND SETTLE FILTER
 *       a move begins: the command's time and the window (the caller's
 *       time unit), the level before the command, the destination's
 *       holding level, the threshold and the band (µA), the settle count
 *       and the filter length
 *   s T UA CLIP
 *       a sample at T: UA µA, CLIP 1 at or past the top of the range, -1
 *       at or past the bottom, 0 a value.  A sample from before the
 *       command fills the filter.
 *   e   the move's samples end
 *
 * Output, one line a move:
 *
 *   STATE MOVED ARRIVED PEAK MEAN N CLIPPED
 *       STATE is waiting, moving, arrived, settled, late or unseen;
 *       waiting and moving are a move its samples did not end.  MOVED and
 *       ARRIVED are times from the command, -1 for none; PEAK and MEAN
 *       are µA over the N samples the rules counted; CLIPPED is 1 when a
 *       clipped sample was among them.
 *
 * Exit code 0, or 2 with a message on stderr for a line it cannot read.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "servo_move.h"

static const char *state_name(servo_move_state_t s)
{
    switch (s) {
    case SERVO_MOVE_WAITING: return "waiting";
    case SERVO_MOVE_MOVING:  return "moving";
    case SERVO_MOVE_ARRIVED: return "arrived";
    case SERVO_MOVE_SETTLED: return "settled";
    case SERVO_MOVE_LATE:    return "late";
    case SERVO_MOVE_UNSEEN:  return "unseen";
    default:                 return "idle";
    }
}

static long ua_of(float a)
{
    const float ua = a * 1e6f;
    return (long)((ua >= 0.0f) ? ua + 0.5f : ua - 0.5f);
}

static void report(const servo_move_t *m)
{
    const long moved = servo_move_moved(m)
                           ? (long)(m->moved_t - m->cfg.cmd_t) : -1L;
    const long arrived = servo_move_arrived(m)
                             ? (long)(m->end_t - m->cfg.cmd_t) : -1L;
    printf("%s %ld %ld %ld %ld %lu %d\n", state_name(m->state), moved,
           arrived, ua_of(m->peak),
           (m->n > 0u) ? ua_of(m->sum / (float)m->n) : 0L,
           (unsigned long)m->n, m->clipped ? 1 : 0);
}

int main(void)
{
    char line[160];
    servo_move_t m;
    bool open = false;
    unsigned long at = 0u;
    memset(&m, 0, sizeof(m));
    while (fgets(line, sizeof(line), stdin) != NULL) {
        ++at;
        unsigned long cmd = 0u;
        unsigned long window = 0u;
        unsigned long t = 0u;
        long rise = 0;
        long ref = 0;
        long move = 0;
        long band = 0;
        long ua = 0;
        unsigned settle = 0u;
        unsigned filter = 0u;
        int clip = 0;
        if (sscanf(line, "m %lu %lu %ld %ld %ld %ld %u %u", &cmd, &window,
                   &rise, &ref, &move, &band, &settle, &filter) == 8
            && !open) {
            const servo_move_cfg_t cfg = {
                .cmd_t    = (uint32_t)cmd,
                .window_t = (uint32_t)window,
                .rise_a   = (float)rise * 1e-6f,
                .ref_a    = (float)ref * 1e-6f,
                .move_a   = (float)move * 1e-6f,
                .band_a   = (float)band * 1e-6f,
                .settle_n = (uint8_t)settle,
                .filter_n = (uint8_t)filter,
            };
            servo_move_begin(&m, &cfg);
            open = true;
        } else if (sscanf(line, "s %lu %ld %d", &t, &ua, &clip) == 3
                   && open && clip >= -1 && clip <= 1) {
            (void)servo_move_sample(&m, (uint32_t)t, (float)ua * 1e-6f,
                                    (servo_move_clip_t)clip);
            (void)servo_move_tick(&m, (uint32_t)t);
        } else if (line[0] == 'e' && open) {
            report(&m);
            open = false;
        } else {
            fprintf(stderr, "sense_trace_replay: line %lu not understood: %s",
                    at, line);
            return 2;
        }
    }
    if (open) {
        fprintf(stderr, "sense_trace_replay: the last move has no end\n");
        return 2;
    }
    return 0;
}
