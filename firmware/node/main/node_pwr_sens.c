/**
 * @file node_pwr_sens.c
 * @brief The power role's INA228: the search, the shunt (node_pwr_sens.h).
 *
 * SPDX-License-Identifier: MIT
 */
#include "node_pwr_sens.h"

#include <string.h>

_Static_assert(RCNODE_SHUNT_MAX_UOHM <= UINT16_MAX,
               "INFO and the control write carry the shunt in 16 bits");
_Static_assert((uint64_t)PSENS_FS_NV
                   <= (uint64_t)INA228_FS_MA_LIMIT * RCNODE_SHUNT_MIN_UOHM,
               "the smallest shunt's full scale fits the driver's limit");
_Static_assert(PSENS_FS_NV / RCNODE_SHUNT_DEF_UOHM == 204800u,
               "the MATEK's 204.8A at ADCRANGE 1");

/** @brief What one address answered. */
typedef enum {
    ASK_NONE = 0,  /**< no INA228: a NACK, another part, a failed read */
    ASK_FOUND,     /**< an INA228 */
    ASK_WAIT,      /**< nothing sent: the bus is stuck */
} ask_t;

bool psens_shunt_ok(uint32_t uohm)
{
    return uohm >= RCNODE_SHUNT_MIN_UOHM && uohm <= RCNODE_SHUNT_MAX_UOHM;
}

uint32_t psens_max_ma(uint16_t uohm)
{
    return uohm != 0u ? PSENS_FS_NV / uohm : 0u;
}

/**
 * @brief The calibration of a shunt at ADCRANGE 1.
 * @param s The sensor; its shunt_uohm is set, cal is filled.
 */
static void calibrate(psens_t *s)
{
    /* psens_shunt_ok() bounds the shunt, so the call cannot refuse: the
     * full scale is 819.2A at most (the static assertions above). */
    (void)ina228_calibrate(s->shunt_uohm, psens_max_ma(s->shunt_uohm),
                           &s->cal);
}

void psens_init(psens_t *s, sense_bus_t *bus, uint16_t uohm)
{
    memset(s, 0, sizeof *s);
    s->bus = bus;
    s->shunt_uohm = psens_shunt_ok(uohm) ? uohm : RCNODE_SHUNT_DEF_UOHM;
    s->state = SENSE_PART_UNPROBED;
    calibrate(s);
}

/**
 * @brief A 16-bit register of the part at @p addr, on the wire directly:
 *        the bus hears the result for its stuck rule, a NACK is not
 *        counted as an error.
 * @param bus The bus.  @param addr The address.  @param reg The register.
 * @param v Receives the value.
 * @return The wire's result.
 */
static sense_err_t ask16(sense_bus_t *bus, uint8_t addr, uint8_t reg,
                         uint16_t *v)
{
    uint8_t b[2];
    const sense_err_t e = bus->io.read(bus->io.ctx, addr, reg, b, sizeof b);
    sense_bus_note(bus, e);
    if (e == SENSE_OK) {
        *v = sense_be16(b);
    }
    return e;
}

/**
 * @brief Whether @p addr holds an INA228: MANUFACTURER_ID, then DEVICE_ID
 *        from a part that answered.
 * @param bus The bus.  @param addr The address.
 * @param other_ti Set true for 5449h with another die; may be NULL.
 * @return ASK_FOUND; ASK_NONE for a NACK or another part; ASK_WAIT while
 *         the bus is stuck and after a held line or a timeout.
 */
static ask_t ask(sense_bus_t *bus, uint8_t addr, bool *other_ti)
{
    if (bus->stuck && !bus->trial) {
        return ASK_WAIT;
    }
    uint16_t maker = 0u;
    uint16_t device = 0u;
    sense_err_t e = ask16(bus, addr, INA228_MANUFACTURER, &maker);
    if (e == SENSE_OK) {
        e = ask16(bus, addr, INA228_DEVICE_ID, &device);
    }
    if (e == SENSE_NACK) {
        return ASK_NONE;
    }
    if (e != SENSE_OK) {
        /* A held line or a timeout says nothing of the address: it is
         * asked again, after the recovery once the bus is stuck. */
        return ASK_WAIT;
    }
    if (ina228_identity_ok(maker, device)) {
        return ASK_FOUND;
    }
    if (maker == INA228_MAKER_ID && other_ti != NULL) {
        *other_ti = true;
    }
    return ASK_NONE;
}

bool psens_is_ina228(sense_bus_t *bus, uint8_t addr, bool *other_ti)
{
    if (bus == NULL || addr < INA228_ADDR_MIN || addr > INA228_ADDR_MAX) {
        return false;
    }
    return ask(bus, addr, other_ti) == ASK_FOUND;
}

uint8_t psens_shut_down(sense_bus_t *bus, uint8_t online_addr, uint8_t kept)
{
    if (bus == NULL) {
        return 0u;
    }
    uint8_t a = online_addr;
    if (a == 0u && kept != 0u && psens_is_ina228(bus, kept, NULL)) {
        a = kept;
    }
    if (a == 0u) {
        return 0u;
    }
    (void)sense_bus_write16(bus, a, INA228_ADC_CONFIG,
                            ina228_adc_config(INA228_MODE_SHUTDOWN, 4, 4, 0, 0));
    return a;
}

/**
 * @brief The driver set up at @p addr with the shunt in force; it probes
 *        the part at its next step.
 * @param s The sensor.  @param addr The address.
 */
static void bind(psens_t *s, uint8_t addr)
{
    s->addr = addr;
    s->bound = ina228_init(&s->ina, s->bus, addr, s->shunt_uohm,
                           psens_max_ma(s->shunt_uohm), INA228_ADC_BENCH)
               == INA228_SETUP_OK;
}

/**
 * @brief One address of the search, the search started when it is due.
 * @param s The sensor.  @param now_ms The clock.
 */
static void search_step(psens_t *s, uint32_t now_ms)
{
    if (s->next == 0u) {
        if (s->searched && (int32_t)(now_ms - s->search_at) < 0) {
            return;
        }
        s->next = s->fixed != 0u ? s->fixed : INA228_ADDR_MIN;
        s->other_ti = false;
    }
    const uint8_t a = s->next;
    const ask_t r = ask(s->bus, a, &s->other_ti);
    if (r == ASK_WAIT) {
        return;
    }
    if (r == ASK_FOUND) {
        s->next = 0u;
        s->searched = true;
        bind(s, a);
        return;
    }
    if (s->fixed == 0u && a < INA228_ADDR_MAX) {
        s->next = (uint8_t)(a + 1u);
        return;
    }
    s->next = 0u;
    s->searched = true;
    s->addr = 0u;
    s->state = s->other_ti ? SENSE_PART_WRONG_ID : SENSE_PART_ABSENT;
    s->search_at = now_ms + SENSE_RETRY_MS;
}

bool psens_step(psens_t *s, uint32_t now_ms)
{
    if (s->bound) {
        if (ina228_step(&s->ina, now_ms)) {
            return true;
        }
        /* The probe failed, or the part went offline or reset: the
         * search starts again, as a probe would have, SENSE_RETRY_MS on. */
        s->state = ina228_state(&s->ina);
        s->bound = false;
        s->rearmed = false;
        s->search_at = now_ms + SENSE_RETRY_MS;
        return false;
    }
    search_step(s, now_ms);
    return false;
}

bool psens_set_shunt(psens_t *s, uint32_t uohm)
{
    if (!psens_shunt_ok(uohm)) {
        return false;
    }
    s->shunt_uohm = (uint16_t)uohm;
    calibrate(s);
    if (s->bound) {
        bind(s, s->addr);
        s->rearmed = s->bound;
    }
    return true;
}

bool psens_set_addr(psens_t *s, uint8_t setting)
{
    const uint8_t f = (setting >= INA228_ADDR_MIN && setting <= INA228_ADDR_MAX)
                      ? setting : 0u;
    if (f == s->fixed) {
        return false;
    }
    s->fixed = f;
    s->bound = false;
    s->rearmed = false;
    s->addr = 0u;
    s->next = 0u;
    s->searched = false;
    s->state = SENSE_PART_UNPROBED;
    return true;
}

bool psens_take_rearm(psens_t *s)
{
    const bool r = s->rearmed;
    s->rearmed = false;
    return r;
}

uint8_t psens_addr(const psens_t *s)
{
    return s->addr;
}

sense_state_t psens_state(const psens_t *s)
{
    return s->bound ? ina228_state(&s->ina) : s->state;
}
