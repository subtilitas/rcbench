/*
 * The modelled ESC (electronic speed controller) and servo protocols: their
 * parameters as data.
 *
 * BLHeli_S and AM32 speak a one-wire bootloader at 19,200 baud; ESCape32
 * answers a text CLI (command-line interface); VESC uses framed packets at
 * 115,200 baud; a Hitec D-series servo has its own protocol.  No transport
 * on the IOmcu (the RP2350 board) carries any of them, so no PROGRAM list
 * shows them (owner decision, 2026-10-18): a protocol gets a registry row
 * (prog_registry.h) once a transport reaches the device.  The tables stay
 * as the parameter model such a row starts from.
 *
 * The Hitec table has no cited protocol source: its parameters are
 * placeholders.
 *
 * A definition says what kind of setting it is (a switch, a choice, a
 * bounded number); a programmer draws one widget per kind.  The names and
 * the choices are the firmware's own and stay English in every language, as
 * its configurators show them; the group and the help line are translated.
 *
 * BLHeli_32 is absent: its parameters are stored in a form the bench cannot
 * read.  docs/BLHeli32.md has the detail.
 *
 * SPDX-License-Identifier: MIT
 */

#include "prog_models.h"

#include <stddef.h>

/* ------------------------------------------------------- the parameters */

static const char *const k_dir[]    = { "NORMAL", "REVERSED", "BIDIRECTIONAL" };
/*
 * BLHeli_S puts timing in named steps, as its configurator does; the other
 * firmwares use degrees of advance, 0 to 31.  Two representations of one
 * quantity in one list is why each definition carries its own kind.
 */
static const char *const k_timing[] = { "LOW", "MEDIUM LOW", "MEDIUM",
                                        "MEDIUM HIGH", "HIGH" };
static const char *const k_pwm[]    = { "24kHz", "48kHz", "96kHz" };
static const char *const k_demag[]  = { "OFF", "LOW", "HIGH" };
static const char *const k_mtype[]  = { "BLDC", "FOC" };
static const char *const k_res[]    = { "STANDARD", "HIGH" };

static const prog_param_t k_blheli[] = {
  { TX_PG_GROUP_MOTOR, "Motor direction", PROG_PARAM_ENUM,
    TX_PG_HELP_DIRECTION,
    k_dir, 3, 0,0,0,0, NULL, 0 },
  { TX_COUNT, "Timing", PROG_PARAM_ENUM,
    TX_PG_HELP_TIMING_STEPS,
    k_timing, 5, 0,0,0,0, NULL, 2 },
  { TX_COUNT, "PWM frequency", PROG_PARAM_ENUM,
    TX_PG_HELP_PWM,
    k_pwm, 3, 0,0,0,0, NULL, 0 },
  { TX_PG_GROUP_STARTUP, "Startup power", PROG_PARAM_NUMBER,
    TX_PG_HELP_STARTUP,
    NULL, 0, 25, 150, 25, 0, "%", 100 },
  { TX_COUNT, "Demag compensation", PROG_PARAM_ENUM,
    TX_PG_HELP_DEMAG,
    k_demag, 3, 0,0,0,0, NULL, 1 },
  { TX_PG_GROUP_PROTECTION, "Brake on stop", PROG_PARAM_BOOL,
    TX_PG_HELP_BRAKE,
    NULL, 0, 0,0,0,0, NULL, 0 },
  { TX_COUNT, "Low voltage cut", PROG_PARAM_NUMBER,
    TX_PG_HELP_LVC,
    NULL, 0, 28, 38, 1, 1, "V", 35 },
  { TX_PG_GROUP_SOUND, "Beep volume", PROG_PARAM_NUMBER,
    TX_PG_HELP_BEEP,
    NULL, 0, 0, 100, 25, 0, "%", 50 },
};

static const prog_param_t k_am32[] = {
  { TX_PG_GROUP_MOTOR, "Motor direction", PROG_PARAM_ENUM,
    TX_PG_HELP_DIRECTION,
    k_dir, 3, 0,0,0,0, NULL, 0 },
  { TX_COUNT, "Timing advance", PROG_PARAM_NUMBER,
    TX_PG_HELP_TIMING_DEG,
    NULL, 0, 0, 30, 1, 0, "deg", 22 },
  { TX_COUNT, "PWM frequency", PROG_PARAM_ENUM,
    TX_PG_HELP_PWM,
    k_pwm, 3, 0,0,0,0, NULL, 1 },
  { TX_PG_GROUP_STARTUP, "Sinusoidal startup", PROG_PARAM_BOOL,
    TX_PG_HELP_SINE,
    NULL, 0, 0,0,0,0, NULL, 1 },
  { TX_COUNT, "Startup power", PROG_PARAM_NUMBER,
    TX_PG_HELP_STARTUP,
    NULL, 0, 25, 150, 25, 0, "%", 75 },
  { TX_PG_GROUP_PROTECTION, "Complementary PWM", PROG_PARAM_BOOL,
    TX_PG_HELP_COMPLEMENTARY,
    NULL, 0, 0,0,0,0, NULL, 1 },
  { TX_COUNT, "Low voltage cut", PROG_PARAM_NUMBER,
    TX_PG_HELP_LVC,
    NULL, 0, 28, 38, 1, 1, "V", 33 },
};

static const prog_param_t k_escape32[] = {
  { TX_PG_GROUP_MOTOR, "Motor direction", PROG_PARAM_ENUM,
    TX_PG_HELP_DIRECTION,
    k_dir, 3, 0,0,0,0, NULL, 0 },
  { TX_COUNT, "Timing", PROG_PARAM_NUMBER,
    TX_PG_HELP_TIMING_DEG,
    NULL, 0, 0, 30, 1, 0, "deg", 18 },
  { TX_COUNT, "PWM frequency", PROG_PARAM_ENUM,
    TX_PG_HELP_PWM,
    k_pwm, 3, 0,0,0,0, NULL, 2 },
  { TX_PG_GROUP_STARTUP, "Sine startup", PROG_PARAM_BOOL,
    TX_PG_HELP_SINE,
    NULL, 0, 0,0,0,0, NULL, 1 },
  { TX_PG_GROUP_PROTECTION, "Brake on stop", PROG_PARAM_BOOL,
    TX_PG_HELP_BRAKE,
    NULL, 0, 0,0,0,0, NULL, 1 },
  { TX_COUNT, "Telemetry", PROG_PARAM_BOOL,
    TX_PG_HELP_TELEMETRY,
    NULL, 0, 0,0,0,0, NULL, 1 },
};

static const prog_param_t k_vesc[] = {
  { TX_PG_GROUP_MOTOR, "Motor type", PROG_PARAM_ENUM,
    TX_PG_HELP_MOTOR_TYPE,
    k_mtype, 2, 0,0,0,0, NULL, 1 },
  { TX_COUNT, "Current limit", PROG_PARAM_NUMBER,
    TX_PG_HELP_CURRENT,
    NULL, 0, 10, 100, 10, 0, "A", 40 },
  { TX_PG_GROUP_PROTECTION, "Regen braking", PROG_PARAM_BOOL,
    TX_PG_HELP_REGEN,
    NULL, 0, 0,0,0,0, NULL, 1 },
  { TX_COUNT, "Low voltage cut", PROG_PARAM_NUMBER,
    TX_PG_HELP_LVC,
    NULL, 0, 28, 38, 1, 1, "V", 34 },
  { TX_PG_GROUP_REPORTING, "Telemetry", PROG_PARAM_BOOL,
    TX_PG_HELP_TELEMETRY,
    NULL, 0, 0,0,0,0, NULL, 1 },
};

static const prog_param_t k_hitec[] = {
  { TX_PG_GROUP_TRAVEL, "Centre", PROG_PARAM_NUMBER,
    TX_PG_HELP_CENTRE,
    NULL, 0, -50, 50, 5, 0, "us", 0 },
  { TX_COUNT, "Endpoint travel", PROG_PARAM_NUMBER,
    TX_PG_HELP_ENDPOINT,
    NULL, 0, 50, 150, 10, 0, "%", 100 },
  { TX_COUNT, "Direction", PROG_PARAM_ENUM,
    TX_PG_HELP_HORN,
    k_dir, 2, 0,0,0,0, NULL, 0 },
  { TX_PG_GROUP_RESPONSE, "Speed", PROG_PARAM_NUMBER,
    TX_PG_HELP_SPEED,
    NULL, 0, 20, 100, 10, 0, "%", 100 },
  { TX_COUNT, "Dead band", PROG_PARAM_NUMBER,
    TX_PG_HELP_DEADBAND,
    NULL, 0, 1, 10, 1, 0, "us", 2 },
  { TX_COUNT, "Resolution", PROG_PARAM_ENUM,
    TX_PG_HELP_RESOLUTION,
    k_res, 2, 0,0,0,0, NULL, 0 },
  { TX_PG_GROUP_PROTECTION, "Overload protect", PROG_PARAM_BOOL,
    TX_PG_HELP_OVERLOAD,
    NULL, 0, 0,0,0,0, NULL, 1 },
  { TX_COUNT, "Fail-safe", PROG_PARAM_BOOL,
    TX_PG_HELP_FAILSAFE,
    NULL, 0, 0,0,0,0, NULL, 1 },
};

static const prog_model_t k_models[] = {
    { "BLHeli_S", TX_PG_TRANSPORT_ONEWIRE, TX_PG_DEVICE_BLHELI,
      k_blheli,   8, PROG_MODEL_ESC },
    { "AM32",     TX_PG_TRANSPORT_ONEWIRE, TX_PG_DEVICE_AM32,
      k_am32,     7, PROG_MODEL_ESC },
    { "ESCape32", TX_PG_TRANSPORT_CLI,     TX_PG_DEVICE_ESCAPE32,
      k_escape32, 6, PROG_MODEL_ESC },
    { "VESC",     TX_PG_TRANSPORT_PACKETS, TX_PG_DEVICE_VESC,
      k_vesc,     5, PROG_MODEL_ESC },
    /* A maker's servos and no firmware name: named in the panel's
     * language, by prog_model_name().  The device text names the maker
     * and the series, no model. */
    { NULL,       TX_PG_TRANSPORT_HITEC,   TX_PG_DEVICE_HITEC,
      k_hitec,    8, PROG_MODEL_SERVO },
};
#define MODEL_COUNT (sizeof(k_models) / sizeof(k_models[0]))

const prog_model_t *prog_models(size_t *count)
{
    if (count != NULL) {
        *count = MODEL_COUNT;
    }
    return k_models;
}

const char *prog_model_name(const prog_model_t *m)
{
    if (m == NULL) {
        return "";
    }
    return (m->name != NULL) ? m->name : TR(PG_NAME_HITEC);
}
