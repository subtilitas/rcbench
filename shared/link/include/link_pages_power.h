/**
 * @file link_pages_power.h
 * @brief The POWER page (0x43, protocol 4.41): the servo rail's master
 *        switch and the 9 port switches of the remote servo node's IOmcu
 *        (board 3).
 *
 * Included by link_pages.h.  The rules in code are shared/outputs/
 * power_page.h; docs/Link.md describes the page.  Only board 3 serves it:
 * an IOmcu of board 1 or 2 refuses it with LINK_NACK_BAD_PAGE.
 *
 *   RAIL       0 or 1: the master rail switch as driven.  Written by the
 *              panel.  1 wants a live heartbeat (LINK_NACK_NOT_ARMED), the
 *              INA3221s' critical limits written (LINK_NACK_BUSY) and a chip
 *              of the A4 stepping (LINK_NACK_NOT_ARMED).  0 is always taken.
 *   FITTED     bit k: a power switch on port k.  0 on a carrier without
 *              them, where every port follows the rail.  Read only.
 *   PORT_ON    bit k: port k on as driven.  Read only.
 *   PORT_WANT  bit k: the panel asks for port k.  A new bit wants a live
 *              heartbeat (LINK_NACK_NOT_ARMED); a bit for a port whose
 *              output is bound as a throttle is refused
 *              (LINK_NACK_BAD_VALUE).  Clearing bits is always taken.
 *   TRIPPED    what cut the rail, kept until the next RAIL 1:
 *              LINK_PW_TRIP_CF(k), LINK_PW_TRIP_CRIT_N, LINK_PW_TRIP_THERMAL,
 *              LINK_PW_TRIP_LIMIT.  Read only.
 *   BEC        bit k: port k read LINK_PW_BEC_MV or more at the last BEC
 *              check (rail and every port off).  Read only.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LINK_PAGES_POWER_H
#define LINK_PAGES_POWER_H

/** @brief Registers of the POWER page. */
enum {
    LINK_PW_RAIL      = 0, /**< the master rail switch, 0 or 1          */
    LINK_PW_FITTED    = 1, /**< bit k: a switch on port k               */
    LINK_PW_PORT_ON   = 2, /**< bit k: port k on as driven              */
    LINK_PW_PORT_WANT = 3, /**< bit k: the panel asks for port k        */
    LINK_PW_TRIPPED   = 4, /**< what cut the rail, LINK_PW_TRIP_*       */
    LINK_PW_BEC       = 5, /**< bit k: port k at LINK_PW_BEC_MV or more  */
    LINK_PW_COUNT     = 6,
};

/** @brief The ports of the page: SERVO OUT 1 to 9. */
#define LINK_PW_PORTS     9u
/** @brief Every port's bit. */
#define LINK_PW_PORT_ALL  0x01FFu

/** @brief TRIPPED: channel @p k's (0 to 8) critical alert flag was read. */
#define LINK_PW_TRIP_CF(k)   ((uint16_t)(1u << (unsigned)(k)))
/** @brief TRIPPED: the INA3221s' critical alert line was low. */
#define LINK_PW_TRIP_CRIT_N  0x0200u
/** @brief TRIPPED: a switch was found in its thermal shutdown. */
#define LINK_PW_TRIP_THERMAL 0x0400u
/** @brief TRIPPED: a channel's window passed its SENSE_LIMIT. */
#define LINK_PW_TRIP_LIMIT   0x0800u

/** @brief The bus voltage at which the BEC check sets a port's bit, mV. */
#define LINK_PW_BEC_MV       500u

#endif /* LINK_PAGES_POWER_H */
