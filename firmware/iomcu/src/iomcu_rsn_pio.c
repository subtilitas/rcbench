/**
 * @file
 * @brief The remote servo node's PIO placement on the SDK (board 3); see
 *        iomcu_rsn_pio.h.  The rules are shared/outputs/rsn_pio.c's,
 *        host-tested in test_rsn_pio.
 *
 * SPDX-License-Identifier: MIT
 */
#include "iomcu_rsn_pio.h"

#include "hardware/pio.h"

#include "iomcu.h"
#include "pd_uart.h"

#if IOMCU_BOARD_RSN

/** @brief The placement's records. */
static rsn_pio_t s_place;

/**
 * @brief pio_sm_is_claimed() on a block by number.
 *
 * @param ctx Not used.
 * @param block The block, 0 to 2.
 * @param sm The state machine, 0 to 3.
 * @return true when claimed.
 */
static bool sm_claimed(void *ctx, unsigned block, unsigned sm)
{
    (void)ctx;
    return pio_sm_is_claimed(pio_get_instance(block), sm);
}

/**
 * @brief pio_sm_claim() on a block by number.
 *
 * @param ctx Not used.
 * @param block The block.
 * @param sm The state machine.
 */
static void sm_claim(void *ctx, unsigned block, unsigned sm)
{
    (void)ctx;
    pio_sm_claim(pio_get_instance(block), sm);
}

/**
 * @brief pio_sm_unclaim() on a block by number.
 *
 * @param ctx Not used.
 * @param block The block.
 * @param sm The state machine.
 */
static void sm_unclaim(void *ctx, unsigned block, unsigned sm)
{
    (void)ctx;
    pio_sm_unclaim(pio_get_instance(block), sm);
}

/** @brief The SDK's claim calls. */
static const rsn_pio_ops_t k_ops = { sm_claimed, sm_claim, sm_unclaim, NULL };

void place_boot(void)
{
    rsn_pio_init(&s_place);
    for (unsigned b = 0; b < RSN_PIO_BLOCKS; ++b) {
        /* Refused only while a program is loaded: none is at boot. */
        (void)pio_set_gpio_base(pio_get_instance(b), RSN_PIO_GPIO_BASE);
    }
}

bool place_begin(rsn_pio_use_t use, uint8_t pin)
{
    return rsn_pio_begin(&s_place, &k_ops, use, pin) == RSN_PIO_OK;
}

void place_end(bool ok)
{
    rsn_pio_end(&s_place, &k_ops, ok);
}

void place_gone(uint8_t pin)
{
    rsn_pio_gone(&s_place, pin);
}

void place_line_gone(void)
{
    rsn_pio_line_gone(&s_place);
}

#else /* the other boards: the SDK's search places every program */

void place_boot(void) {}

bool place_begin(rsn_pio_use_t use, uint8_t pin)
{
    (void)use;
    (void)pin;
    return true;
}

void place_end(bool ok) { (void)ok; }

void place_gone(uint8_t pin) { (void)pin; }

void place_line_gone(void) {}

#endif /* IOMCU_BOARD_RSN */

bool pd_open(void)
{
    if (!place_begin(RSN_PIO_FIXED, 0u)) {
        return false;
    }
    const bool ok = pd_uart_open(supply_page_tx(&s_supply),
                                 supply_page_rx(&s_supply),
                                 supply_page_uart_baud(&s_supply));
    place_end(ok);
    return ok;
}
