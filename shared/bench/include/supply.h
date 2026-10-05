/*
 * A programmable supply: what it can be set to, what it was set to, and what
 * it delivers.
 *
 * The SUPPLY screen and the log see this and never the supply's own wire, so
 * the same screen runs on the panel's model now and on a driver for the PD
 * mini (a USB Power Delivery trigger controlled over a UART) once its
 * protocol is known.  Which board talks to it is not decided either; this
 * struct is what crosses whichever boundary that turns out to be.
 *
 * Pure C, no ESP-IDF (Espressif Internet-of-Things Development Framework).
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RCBENCH_SUPPLY_H
#define RCBENCH_SUPPLY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What a supply can be set to.  A driver reports its own. */
typedef struct {
    float v_min, v_max, v_step;   /**< output voltage, V               */
    float i_min, i_max, i_step;   /**< current limit, A                */
} supply_caps_t;

/**
 * The range a USB-PD PPS (Programmable Power Supply) source offers in its
 * widest profile, 3.3 to 21 V in 20 mV steps and up to 5 A in 50 mA steps.
 * The panel's model uses it until a PD mini driver reports what that board
 * and the source behind it can do; a fixed-voltage PD source offers far
 * less.
 */
#define SUPPLY_CAPS_PPS_DEFAULT { 3.3f, 21.0f, 0.02f, 0.5f, 5.0f, 0.05f }

typedef enum {
    SUPPLY_MODE_OFF = 0,   /**< output switched off                       */
    SUPPLY_MODE_CV,        /**< regulating its voltage                    */
    SUPPLY_MODE_CC,        /**< holding its current limit                 */
} supply_mode_t;

/** Which readings arrived; an absent one is drawn and logged empty. */
#define SUPPLY_OK_VOLTAGE 0x01u
#define SUPPLY_OK_CURRENT 0x02u

typedef struct {
    float         set_v;      /**< asked for, V                          */
    float         set_i;      /**< current limit asked for, A            */
    float         v;          /**< at the output, V                      */
    float         i;          /**< out of it, A                          */
    float         p;          /**< v * i, W                              */
    supply_mode_t mode;
    bool          output;     /**< the output is switched on             */
    bool          online;     /**< the supply answers                    */
    uint8_t       ok;         /**< SUPPLY_OK_*                           */
    /** The trip that switched the output off last, as supply_trip_kind_t;
     *  kept until the output is switched on again. */
    uint8_t       trip;

    /* The run's extremes, from the output's switch-on: how far the voltage
     * sagged, and the most current and power it gave. */
    float         v_min;
    float         i_max;
    float         p_max;
    bool          sag_seeded; /**< v_min is a reading yet                */

    /* What the run delivered, counted by the panel (supply_count_totals). */
    float         charge_mah;
    float         energy_wh;
    uint8_t       counted;    /**< BENCH_COUNTED_* from bench_state.h    */
} supply_state_t;

/**
 * What the operator allows, on top of what the supply can do: caps on the
 * set points, and trips that switch the output off.  A trip threshold of 0
 * is off.
 */
typedef struct {
    float v_max;          /**< highest voltage set point, V             */
    float i_max;          /**< highest current limit, A                 */
    float trip_i;         /**< output off above this current, A; 0 off  */
    float trip_v;         /**< output off above this voltage, V; 0 off  */
    float trip_s;         /**< how long over before a trip, s           */
} supply_limits_t;

/** The supply's caps narrowed by @p lim: the set points a screen offers.
 *  A cap below the supply's own minimum leaves the range at that minimum. */
supply_caps_t supply_caps_limited(const supply_caps_t *caps,
                                  const supply_limits_t *lim);

typedef enum {
    SUPPLY_TRIP_NONE = 0,
    SUPPLY_TRIP_CURRENT,
    SUPPLY_TRIP_VOLTAGE,
} supply_trip_kind_t;

/** How long each reading has been over its threshold, counted from the
 *  first reading seen over it, for the trips. */
typedef struct {
    float over_i_s;
    float over_v_s;
    bool  i_over;         /**< the last current that arrived was over  */
    bool  v_over;         /**< the last voltage that arrived was over  */
    float i_at;           /**< the threshold the current's count is for */
    float v_at;           /**< the threshold the voltage's count is for */
} supply_trip_t;

void supply_trip_reset(supply_trip_t *t);

/**
 * Take @p dt_s of the readings in @p s into the trips.  The count starts at
 * the first reading seen over a threshold, and each reading over it after
 * that adds the time since the one before; one at or under it starts the
 * count again, and one that did not arrive leaves it where it is.  A trip
 * that is off forgets its count, and so does one whose threshold changed.
 * A trip time of 0 fires on the first reading over.  Returns which trip fired,
 * once the time over reaches lim->trip_s; nothing while the output is off.
 * @p dt_s is clamped to 0 .. 1 s, as the totals' steps are.
 */
supply_trip_kind_t supply_trip_step(supply_trip_t *t,
                                    const supply_limits_t *lim,
                                    const supply_state_t *s, float dt_s);

/** @p value clamped to @p min .. @p max and rounded to a multiple of @p step
 *  above @p min.  A step of 0 or less only clamps. */
float supply_snap(float value, float min, float max, float step);

/**
 * The run's extremes start again from what is shown, and only from readings
 * that arrived; a maximum with nothing under it starts at 0.  The minimum
 * voltage is a reading only while the output is on: an output switched off
 * reads 0 V, and a floor of 0 V reads as a collapsed rail.
 */
void supply_reset_peaks(supply_state_t *s);

/** Take this sample into the run's extremes, only what arrived. */
void supply_track_peaks(supply_state_t *s);

/** The run begins: nothing delivered, nothing counted. */
void supply_reset_totals(supply_state_t *s);

/**
 * Add @p dt_s of the readings to the run's charge and energy while the
 * output is on, only what arrived; @p dt_s is clamped to 0 .. 1 s, as the
 * bench's totals are.
 */
void supply_count_totals(supply_state_t *s, float dt_s);

/* ---------------------------------------------------------------- model */

/**
 * A supply feeding a servo-like load, for the panel to run while no driver
 * exists, and for the screenshots.
 *
 * The load is a resistance with a burst on top: LOAD_OHMS at rest, and every
 * SUPPLY_SIM_PERIOD_S a burst of SUPPLY_SIM_BURST_A for SUPPLY_SIM_BURST_S,
 * which is what a servo moving under load draws.  When the load would draw
 * more than the limit the model holds the limit and the voltage falls with
 * it (CC); otherwise it holds the set voltage less a 0.05 ohm source
 * resistance (CV).  Deterministic, so a screenshot is the same every time.
 */
#define SUPPLY_SIM_LOAD_OHMS  6.0f
#define SUPPLY_SIM_PERIOD_S   3.0f
#define SUPPLY_SIM_BURST_S    0.6f
#define SUPPLY_SIM_BURST_A    1.4f
#define SUPPLY_SIM_SOURCE_OHMS 0.05f

typedef struct {
    supply_caps_t caps;
    float         t;          /**< time since the output came on, s      */
    float         set_v, set_i;
    bool          output;
} supply_sim_t;

void supply_sim_init(supply_sim_t *m);
/** Set points are snapped to the caps; the output's state is kept. */
void supply_sim_set(supply_sim_t *m, float v, float i);
void supply_sim_output(supply_sim_t *m, bool on);
/** Advance @p dt_s and write the readings, mode and set points into @p out;
 *  peaks and totals are the caller's. */
void supply_sim_step(supply_sim_t *m, float dt_s, supply_state_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_SUPPLY_H */
