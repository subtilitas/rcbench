/*
 * One servo move judged from the servo's current, sample by sample: when
 * it started moving and when it arrived, from a step command to one end.
 *
 * Nothing measures the horn.  Current rises while the motor drives and
 * falls back to the holding level as it stops, which can lead or lag the
 * horn's arrival.  The rules:
 *
 *   Threshold  the larger of SERVO_MOVE_MIN_A and SERVO_MOVE_NOISE_K times
 *              the noise at rest, the standard deviation of the idle
 *              samples (servo_move_noise_a(), servo_move_threshold_a()).
 *   Movement   the first sample more than the threshold away from the
 *              level before the command: above it, or below it when the
 *              servo leaves an end it was pushing on.
 *   Arrival    after movement, a sample more than the threshold above the
 *              destination's holding level, then the first sample back
 *              within the band of it, on either side.  A sample above the
 *              level by the threshold is never an arrival, so a band wider
 *              than the threshold times no move early.  A sample more than
 *              the band below the level instead hands the move to the
 *              settled rule.
 *   Settled    an end held harder than the servo moves, an end pushing on
 *              a stop, is never passed: there the move has arrived at the
 *              first of settle_n samples in a row, after movement, within
 *              the band of the level and of each other (the highest less
 *              the lowest at most the band).
 *   Window     a move not arrived window_t after its command is over: late
 *              with movement seen, unseen without.  servo_move_window_ms()
 *              adds the meter's lag to SERVO_MOVE_TIMEOUT_MS, since its
 *              samples show an arrival that much later.  A sample at or
 *              past the window's end ends the move there and is never
 *              its arrival.
 *
 * The same rules run at two rates.  The panel's servo test feeds the PD
 * mini's readings, about 10 a second, with time in ms, settle_n 2 and no
 * filter.  The coprocessor feeds INA3221 CH1 samples at 1 kHz with time in
 * 0.1 ms (sense_sched.h).  The time unit is the caller's: cmd_t, window_t
 * and every sample's time are in it.
 *
 * Filter.  With filter_n above 1 each sample is judged as the mean of the
 * last filter_n samples, those before the command included; the filtered
 * sample carries the newest sample's time; a step shows half after
 * (filter_n - 1) / 2 samples and whole after filter_n - 1.  With filter_n
 * 1 a sample is judged as it is.  Samples held from before the move began
 * go in with servo_move_prime().
 *
 * Clipping.  A sample at the end of the meter's range carries no value: it
 * is at or past @p a, by how much is not known.  It decides only what that
 * bound decides -- movement when the bound lies more than the threshold
 * from the level before the command, not yet there when the bound lies
 * more than the threshold above the destination's level -- and is never an
 * arrival.  It adds nothing to the mean and the peak, and marks them
 * clipped.  A filtered sample with a clipped sample in its window is
 * clipped the same way; one with both ends clipped in its window decides
 * nothing.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The smallest threshold.  A sample more than the threshold from the
 *  level before a command is the servo moving, and more than it above an
 *  end's holding level is the move not yet there. */
#define SERVO_MOVE_MIN_A        0.020f
/** The threshold in idle noises, where that is larger. */
#define SERVO_MOVE_NOISE_K      3.0f
/** Back within this of the holding level, and not above it by the
 *  threshold, is the move arrived. */
#define SERVO_MOVE_BAND_A       0.05f
/** A move not arrived this long after its command is late, or unseen; a
 *  meter's lag is added (servo_move_window_ms()). */
#define SERVO_MOVE_TIMEOUT_MS   3000u
/** The longest filter. */
#define SERVO_MOVE_FILTER_MAX   8u

/** Where a move stands. */
typedef enum {
    SERVO_MOVE_IDLE = 0,    /**< not begun                               */
    SERVO_MOVE_WAITING,     /**< begun, no movement yet                  */
    SERVO_MOVE_MOVING,      /**< movement seen, not arrived              */
    SERVO_MOVE_ARRIVED,     /**< back at the level after passing it      */
    SERVO_MOVE_SETTLED,     /**< arrived at an end held harder than it
                                 moves                                   */
    SERVO_MOVE_LATE,        /**< movement, no arrival in the window      */
    SERVO_MOVE_UNSEEN,      /**< no movement in the window               */
} servo_move_state_t;

/** A sample at the end of the meter's range. */
typedef enum {
    SERVO_MOVE_CLIP_LOW  = -1,  /**< at or below the value given         */
    SERVO_MOVE_CLIP_NONE =  0,
    SERVO_MOVE_CLIP_HIGH =  1,  /**< at or above the value given         */
} servo_move_clip_t;

typedef struct {
    uint32_t cmd_t;         /**< the command, in the caller's time unit  */
    uint32_t window_t;      /**< late or unseen this long after cmd_t    */
    float    rise_a;        /**< the level before the command, A         */
    float    ref_a;         /**< the destination's holding level, A      */
    float    move_a;        /**< the threshold, A                        */
    float    band_a;        /**< the arrival band, A                     */
    uint8_t  settle_n;      /**< samples in a row at a stop, 2 or more   */
    uint8_t  filter_n;      /**< moving mean length, 1 to
                                 SERVO_MOVE_FILTER_MAX                   */
} servo_move_cfg_t;

typedef struct {
    servo_move_cfg_t   cfg;
    servo_move_state_t state;
    bool     left;          /**< since movement, above ref_a by the
                                 threshold: not there yet                */
    /* The samples in a row near ref_a, after movement. */
    uint8_t  run_n;
    float    run_lo, run_hi;
    uint32_t run_t;         /**< the first of them                       */
    /* The filter. */
    float    fbuf[SERVO_MOVE_FILTER_MAX];
    int8_t   fclip[SERVO_MOVE_FILTER_MAX];
    uint8_t  f_n, f_head;
    /* What it measured. */
    uint32_t moved_t;       /**< the first sample of the movement        */
    uint32_t end_t;         /**< the arrival, or when the window ran out */
    float    sum;           /**< valued samples from the command to the
                                 arrival, the arriving ones excluded     */
    uint32_t n;
    float    peak;          /**< the highest of them, 0 for none         */
    bool     clipped;       /**< a clipped sample among them: the mean
                                 and the peak lack it                    */
} servo_move_t;

/** The standard deviation of @p n samples given as their sum and the sum
 *  of their squares; 0 for none. */
float servo_move_noise_a(uint32_t n, float sum, float sum_sq);

/** The threshold for a noise at rest of @p noise_a. */
float servo_move_threshold_a(float noise_a);

/** SERVO_MOVE_TIMEOUT_MS plus a meter's lag of @p lag_ms. */
uint32_t servo_move_window_ms(uint32_t lag_ms);

/** A move begins: commanded at cfg->cmd_t, from cfg->rise_a towards
 *  cfg->ref_a.  settle_n below 2 is taken as 2; filter_n is held to 1 to
 *  SERVO_MOVE_FILTER_MAX. */
void servo_move_begin(servo_move_t *m, const servo_move_cfg_t *cfg);

/** A sample taken before the move began, into the filter only, as the
 *  newest so far: a caller that held samples before it knew the command's
 *  time hands them over oldest first.  Decides nothing; nothing with
 *  filter_n 1 or once the move is over. */
void servo_move_prime(servo_move_t *m, float a, servo_move_clip_t clip);

/**
 * A sample taken at @p at: @p a amps, or for a clip the end of the range
 * it is at or past.  A sample taken before the command only fills the
 * filter; one at or past window_t after the command ends the move late
 * or unseen, as servo_move_tick() would, and is not judged.  Returns the
 * state after it; ARRIVED and SETTLED leave end_t at the arrival.  Nothing
 * once the move is over.
 */
servo_move_state_t servo_move_sample(servo_move_t *m, uint32_t at, float a,
                                     servo_move_clip_t clip);

/** The time is @p now: a move still under way at window_t after its
 *  command is over, late or unseen, with end_t at @p now.  A @p now before
 *  the command decides nothing. */
servo_move_state_t servo_move_tick(servo_move_t *m, uint32_t now);

/** Whether the move is over: arrived, settled, late or unseen. */
bool servo_move_over(const servo_move_t *m);

/** Whether the move arrived: ARRIVED or SETTLED. */
bool servo_move_arrived(const servo_move_t *m);

/** Whether movement was seen. */
bool servo_move_moved(const servo_move_t *m);

#ifdef __cplusplus
}
#endif
