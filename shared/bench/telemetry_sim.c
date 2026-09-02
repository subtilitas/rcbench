/*
 * SPDX-License-Identifier: MIT
 */

#include "telemetry_sim.h"

#include <math.h>
#include <string.h>

void telemetry_sim_init(telemetry_sim_t *s, const telemetry_sim_cfg_t *cfg)
{
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s));
    s->cfg = (cfg != NULL) ? *cfg : (telemetry_sim_cfg_t)TELEMETRY_SIM_DEFAULT();
    s->temp_esc   = 24.0f;
    s->temp_motor = 24.0f;
}

void telemetry_sim_step(telemetry_sim_t *s, float throttle_pct, float dt_s,
                        bench_state_t *out)
{
    if (s == NULL || out == NULL) {
        return;
    }
    if (!(dt_s > 0.0f)) {
        dt_s = 0.0f;
    }
    if (throttle_pct < 0.0f)   { throttle_pct = 0.0f; }
    if (throttle_pct > 100.0f) { throttle_pct = 100.0f; }

    const telemetry_sim_cfg_t *c = &s->cfg;
    const float duty = throttle_pct / 100.0f;

    /* The open-circuit voltage droops by up to 0.35 V per cell as the pack
     * empties. */
    const float used = (c->pack_mah > 0.0f) ? (s->drawn_mah / c->pack_mah) : 0.0f;
    const float soc  = (used > 1.0f) ? 1.0f : used;
    const float open_v = c->cells * (c->cell_nominal_v - 0.35f * soc);

    /*
     * Current grows faster than throttle: propeller load is a cube law on
     * rpm (revolutions per minute) and rpm is about linear in duty; with the
     * motor's losses the exponent is 2.5.
     */
    const float load = powf(duty, 2.5f);
    const float current = c->no_load_amps * duty + c->stall_amps * load;

    /* The bus sags with the pack's internal resistance. */
    const float voltage = open_v - current * c->internal_ohms * c->cells;

    /* rpm follows the sagging bus, not the stick, with time constant
     * rpm_tau_s. */
    const float target_rpm = c->kv * voltage * duty;
    const float tau = (c->rpm_tau_s > 0.0f) ? c->rpm_tau_s : 0.2f;
    const float alpha = 1.0f - expf(-dt_s / tau);
    s->rpm += (target_rpm - s->rpm) * alpha;

    const float power = voltage * current;
    s->drawn_mah += current * dt_s * (1000.0f / 3600.0f);
    s->drawn_wh  += power   * dt_s * (1.0f / 3600.0f);

    /* Heating with a leak back to 24 C ambient; the ESC (electronic speed
     * controller) heats faster than the motor. */
    s->temp_esc   += (power * 0.0016f - (s->temp_esc - 24.0f) * 0.08f) * dt_s;
    s->temp_motor += (power * 0.0011f - (s->temp_motor - 24.0f) * 0.05f) * dt_s;

    out->voltage    = voltage;
    out->current    = current;
    out->power      = power;
    out->rpm        = s->rpm;
    out->temp_esc   = s->temp_esc;
    out->temp_motor = s->temp_motor;
    out->charge_mah = s->drawn_mah;
    out->energy_wh  = s->drawn_wh;

    if (!out->valid) {
        out->voltage_min = voltage;
    }
    if (voltage < out->voltage_min) { out->voltage_min = voltage; }
    if (current > out->current_max) { out->current_max = current; }
    if (power   > out->power_max)   { out->power_max   = power; }
    if (s->rpm  > out->rpm_max)     { out->rpm_max     = s->rpm; }

    out->flags = LINK_BN_VOLTAGE_OK | LINK_BN_CURRENT_OK
               | LINK_BN_RPM_OK | LINK_BN_TEMP_OK
               | LINK_BN_SIMULATED;
    out->valid = true;
}
