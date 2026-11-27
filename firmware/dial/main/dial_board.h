/**
 * @file
 * @brief The board the Dial image is built for (main/Kconfig.projbuild,
 *        "Board"): its pins, its INFO hardware value, its update kind and
 *        its name.
 *
 * Two boards, one header each:
 *
 * - M5Stack Dial v1.1 (CONFIG_RCBENCH_DIAL_BOARD_M5DIAL, the default):
 *   dial_board_m5.h.
 * - Guition JC3636K518C knob (CONFIG_RCBENCH_DIAL_BOARD_K518, appended by
 *   sdkconfig.k518): dial_board_k518.h.
 *
 * Each header defines DIAL_BOARD_HW (INFO's DK_HW_*), DIAL_BOARD_KIND (the
 * BLEOTA_KIND_* of DEVOTA_ROLE() and BEGIN, so neither board takes the
 * other's image), DIAL_BOARD_NAME (the console's `status`) and its pins as
 * DIAL_PIN_*.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "sdkconfig.h"

#include "bleota.h"
#include "dialknob.h"

#if CONFIG_RCBENCH_DIAL_BOARD_K518
#include "dial_board_k518.h"
#else
#include "dial_board_m5.h"
#endif
