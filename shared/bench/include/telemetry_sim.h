/*
 * A pack, a motor and a propeller, modelled.
 *
 * This exists so that every screen can be built, reviewed and demonstrated
 * before the coprocessor does, and it is dangerous for exactly one reason: a
 * modelled number that gets read as a measured one.  So it sets
 * LINK_BN_SIMULATED on everything it produces, and the router writes
 * SIMULATION across the whole screen whenever that bit is up.
 *
 * The physics is deliberately crude and deliberately *not* flattering: the
 * bus sags under load, current grows faster than throttle, and rpm follows a
 * sagging bus rather than the stick.  A simulator that produced tidy numbers
 * would hide the misreadings a real bench exists to show.
 */
#pragma once

#include "bench_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float cells;
    float cell_nominal_v;
    float internal_ohms;
    float kv;              /**< rpm per volt, unloaded  */
    float no_load_amps;
    float stall_amps;      /**< current at full throttle, loaded */
    float pack_mah;
    float rpm_tau_s;       /**< how slowly rpm follows a step    */
} telemetry_sim_cfg_t;

#define TELEMETRY_SIM_DEFAULT()          \
    (telemetry_sim_cfg_t) {              \
        .cells = 6.0f,                   \
        .cell_nominal_v = 4.05f,         \
        .internal_ohms = 0.018f,         \
        .kv = 920.0f,                    \
        .no_load_amps = 1.4f,            \
        .stall_amps = 78.0f,             \
        .pack_mah = 5000.0f,             \
        .rpm_tau_s = 0.22f,              \
    }

typedef struct {
    telemetry_sim_cfg_t cfg;
    float rpm;             /**< the lagged state                  */
    float drawn_mah;
    float drawn_wh;
    float temp_esc;
    float temp_motor;
} telemetry_sim_t;

void telemetry_sim_init(telemetry_sim_t *s, const telemetry_sim_cfg_t *cfg);

/**
 * Advance by @p dt_s at @p throttle_pct and fill @p out.
 *
 * Peaks are accumulated into @p out the way the coprocessor would accumulate
 * them, rather than being left for the panel: the seam has to behave the same
 * from both sides or the screen finds out which one it is talking to.
 */
void telemetry_sim_step(telemetry_sim_t *s, float throttle_pct, float dt_s,
                        bench_state_t *out);

#ifdef __cplusplus
}
#endif
