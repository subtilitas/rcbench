/*
 * The registry of programmers and runs: one static table that the PROGRAM
 * (and later TEST) lists of a workbench are built from.
 *
 * A row is { device, task, name, group, transport, port, needs_capability,
 * page_set, full_width }:
 *   - device and task pick the list: SERVO > PROGRAM and MOTOR & ESC >
 *     PROGRAM.
 *   - name is the row's descriptive text ("KST and Chaservo servos"), never
 *     a screen title; a maker's name is a row label only.
 *   - group is the heading the row sits under once the list has headings.
 *   - transport is what the row's line says it drives ("PROG PORT"), as
 *     the TARGET row on the output card says it.
 *   - port is the IOmcu programming port the row's session holds,
 *     PROG_REG_TESTER_PORT for the port the SERVO screen's tester drives
 *     (prog_registry_set_ports()), or PROG_REG_NO_PORT.  Two rows on one
 *     port are exclusive: while one holds the port the other is drawn
 *     dimmed, PORT IN USE, and opens nothing.  A port another page of the
 *     IOmcu holds (a HITEC session the bench console opened) dims every
 *     row on it the same way.
 *   - needs_capability holds link_cap_t bits.  A row whose bits the IOmcu
 *     does not report is drawn dimmed with its reason, not hidden, and
 *     opens nothing.
 *   - page_set is the session the row opens one level down: the hooks the
 *     host screen drives it through (prog_page_set_t).
 *   - full_width says how the host lays the session out.  Not full width,
 *     it opens over the host's left column and the output card stays on
 *     the right (SERVO's sessions).  Full width, it takes the whole body
 *     under the strip row, and the host draws a one-line output strip in
 *     that row in place of the card: what the session drives, `TARGET
 *     DSHOT300 GP0 GP1   SUPPLY 7.60 V  1.00 A   SAFE` (MOTOR & ESC's
 *     sessions, ESC STICK).  The owner grants each full-width page set;
 *     prog_reg_check() holds each device to the one layout its host draws.
 *
 * A new maker adds one row to k_rows in prog_registry.c and one page_set
 * (a session laid out for the host's left column, as kst_panel_pages is,
 * or for the full body, as esc_stick_pages is).
 * A new maker never adds a home tile, a SETUP tile or a tab.  The Hitec
 * programmer lands here as SERVO's second PROGRAM row.  A modelled
 * protocol with no transport on the IOmcu gets no row.
 *
 * The list rules: a list of more than PROG_LIST_SEARCH_OVER rows offers
 * SEARCH (the existing keyboard, "*" for any text); a list showing more
 * than PROG_LIST_HEADINGS_OVER rows draws its rows under their group
 * headings; a list that does not fit its area pages with "^" and "v".  A
 * list holds at most PROG_LIST_MAX rows (prog_reg_check()).
 *
 * The table, its checks and the list model are pure; the view draws a
 * list into a rectangle and turns touches into row taps.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "touch_types.h"
#include "ui_text.h"
#include "ui_workbench.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The devices a row belongs to: the workbench whose list shows it. */
typedef enum {
    PROG_DEV_SERVO = 0,
    PROG_DEV_MOTOR,
    PROG_DEVICES
} prog_device_t;

/** A row that holds no programming port. */
#define PROG_REG_NO_PORT 0xFFu
/** A session's pin that is not known. */
#define PROG_REG_NO_PIN  0xFFu
/** The IOmcu's programming ports a row may name: 0 to 3 (protocol
 *  LINK_PG_PORTS_MAX). */
#define PROG_REG_PORTS   4u
/** A row on the port the SERVO screen's tester drives, whichever that is:
 *  PORT 1 to PORT 4 of SERVO TESTER, none with OUTPUTS. */
#define PROG_REG_TESTER_PORT 0xFEu

/*
 * A function that calls one hook of a page set and nothing else through a
 * pointer: tools/stack_check.py follows the call by the function's name
 * (PROG_CALLS), so the compiler may neither inline it nor clone it under
 * another name.
 */
#if defined(__GNUC__) && !defined(__clang__)
#define PROG_ONE_CALL __attribute__((noinline, noclone))
#else
#define PROG_ONE_CALL __attribute__((noinline))
#endif

/*
 * A full-width session's layout, in the host's body coordinates (1024 x 540
 * px under the band): the output strip's line at the top, one line of the
 * 8 x 16 face from PROG_FULL_STRIP_Y, and the session's own area from
 * PROG_FULL_Y to the foot of the body.
 */
#define PROG_FULL_STRIP_X 12
#define PROG_FULL_STRIP_Y 6
#define PROG_FULL_STRIP_H 16
#define PROG_FULL_Y       26
/** The cells the output strip's line holds: 776 px of 8 px cells. */
#define PROG_FULL_STRIP_CELLS 97

/** The list rules (see the top of this file). */
#define PROG_LIST_SEARCH_OVER   9
#define PROG_LIST_HEADINGS_OVER 8
#define PROG_LIST_MAX           48
/** The characters a search takes, as ESC STICK's does. */
#define PROG_LIST_QUERY_MAX     16

/**
 * A session's hooks, in ui_screen_t's shape: the host screen opens it over
 * its left column, routes its touches and draws it while it is open, and
 * tells it of the host's entry, leave, lost touches and the bench's arm.
 * Every hook is required.
 */
typedef struct {
    /** Start over: closed, nothing held, laid out in @p area. */
    void       (*init)(gfx_rect_t area);
    /** The host screen was entered. */
    void       (*entry)(void);
    void       (*open)(void);
    bool       (*is_open)(void);
    /**
     * The home tag or BACK in the session: an editor open in it closes;
     * otherwise the session closes, or, while it holds its port, asks for
     * the way back first (KST LEAVE) and stays.
     */
    void       (*back)(void);
    /** The session holds its port: it stays open, and no other row on the
     *  port opens. */
    bool       (*holds_port)(void);
    /** The bench is armed or asked to arm. */
    void       (*armed)(bool armed);
    void       (*tick)(float dt_s);
    bool       (*has_press)(uint8_t id);
    void       (*event)(const touch_event_t *evt);
    void       (*render)(gfx_canvas_t *c);
    /** Changes whenever a render would show something else. */
    uint32_t   (*revision)(void);
    /** Changes while a hold fills; hold_rect() is where. */
    uint32_t   (*hold_revision)(void);
    gfx_rect_t (*hold_rect)(void);
    /** The host screen is left: presses and editors go, the port stays. */
    void       (*leave)(void);
    /** Touch events were lost. */
    void       (*cancel)(void);
    /** The GPIO the session's port drives, or PROG_REG_NO_PIN. */
    uint8_t    (*pin)(void);
    /** A press coming down at (x, y), in the host screen's coordinates,
     *  lands on a control of the session that stops what the bench drives
     *  and acts on it now; see ui_screen_t's safe_at. */
    bool       (*safe_at)(int x, int y);
} prog_page_set_t;

typedef struct {
    uint8_t                device;           /**< a prog_device_t        */
    uint8_t                task;             /**< a ui_wb_task_t         */
    ui_text_id_t           name;
    ui_text_id_t           group;
    ui_text_id_t           transport;
    uint8_t                port;             /**< or PROG_REG_NO_PORT    */
    uint16_t               needs_capability; /**< link_cap_t bits        */
    const prog_page_set_t *page_set;
    /** The session takes the whole body, the output strip above it, in
     *  place of the left column beside the output card. */
    bool                   full_width;
} prog_reg_row_t;

/** The table, and its row count in @p count. */
const prog_reg_row_t *prog_registry(size_t *count);

/** For the tests: the lists are built from @p count rows of @p rows; NULL
 *  goes back to the table as it ships. */
void prog_registry_use(const prog_reg_row_t *rows, size_t count);

/**
 * The ports as the panel last read them: @p tester_port, the port the
 * SERVO screen's tester drives (0 to 3), PROG_REG_NO_PORT with the
 * standard outputs or the target not known; @p held, bit n for port n
 * held by an IOmcu page no row's session drives (the HITEC page, opened
 * from the bench console).  Both start as none.
 */
void prog_registry_set_ports(uint8_t tester_port, uint8_t held);

/** Row @p r's port as it stands: its own, the tester's for
 *  PROG_REG_TESTER_PORT, or PROG_REG_NO_PORT. */
uint8_t prog_reg_port(const prog_reg_row_t *r);

/* ------------------------------------------------------------ the checks */

typedef enum {
    PROG_REG_OK = 0,
    PROG_REG_BAD_DEVICE,     /**< device past PROG_DEVICES               */
    PROG_REG_BAD_TASK,       /**< task neither PROGRAM nor TEST           */
    PROG_REG_NO_NAME,        /**< name empty in the language in force     */
    PROG_REG_NO_GROUP,       /**< group empty                             */
    PROG_REG_NO_TRANSPORT,   /**< transport empty                         */
    PROG_REG_BAD_PORT,       /**< a port past PROG_REG_PORTS              */
    PROG_REG_NO_PAGE_SET,    /**< page_set NULL, or a hook of it NULL     */
    PROG_REG_SAME_NAME,      /**< two rows of one list with one name      */
    PROG_REG_TOO_MANY,       /**< a list over PROG_LIST_MAX rows          */
    /** A layout the device's host does not draw: a full-width SERVO row,
     *  or a MOTOR row that is not full width. */
    PROG_REG_BAD_WIDTH,
} prog_reg_fault_t;

/**
 * Checks @p n rows of @p rows: every field filled and in range, names in
 * the language in force, no two rows of one list named alike, no list over
 * PROG_LIST_MAX, each row laid out as its device's host draws it.  The first fault found, with its row in @p at (may be
 * NULL); PROG_REG_OK with @p at untouched for a sound table.
 */
prog_reg_fault_t prog_reg_check(const prog_reg_row_t *rows, size_t n,
                                size_t *at);

/* ------------------------------------------------- a device's sessions */

/**
 * The page sets @p device's rows open, each once in the table's order,
 * into @p out: at most PROG_LIST_MAX.  Their count.
 */
int prog_reg_sets(uint8_t device, const prog_page_set_t *out[PROG_LIST_MAX]);

/** Whether a row that opens @p ps is full width. */
bool prog_reg_full_width(const prog_page_set_t *ps);

/*
 * A page set's hooks, each called from one function of its own, so
 * tools/stack_check.py follows the call by name (PROG_CALLS there).  The
 * hosts call a session through these and through nothing else.
 */
void prog_ps_init(const prog_page_set_t *ps, gfx_rect_t area);
void prog_ps_entry(const prog_page_set_t *ps);
void prog_ps_open(const prog_page_set_t *ps);
bool prog_ps_is_open(const prog_page_set_t *ps);
void prog_ps_back(const prog_page_set_t *ps);
bool prog_ps_holds(const prog_page_set_t *ps);
void prog_ps_armed(const prog_page_set_t *ps, bool armed);
void prog_ps_tick(const prog_page_set_t *ps, float dt_s);
bool prog_ps_has_press(const prog_page_set_t *ps, uint8_t id);
void prog_ps_event(const prog_page_set_t *ps, const touch_event_t *evt);
void prog_ps_render(const prog_page_set_t *ps, gfx_canvas_t *c);
uint32_t prog_ps_revision(const prog_page_set_t *ps);
uint32_t prog_ps_hold_revision(const prog_page_set_t *ps);
gfx_rect_t prog_ps_hold_rect(const prog_page_set_t *ps);
void prog_ps_leave(const prog_page_set_t *ps);
void prog_ps_cancel(const prog_page_set_t *ps);
uint8_t prog_ps_pin(const prog_page_set_t *ps);
bool prog_ps_safe_at(const prog_page_set_t *ps, int x, int y);

/* ------------------------------------------------------- a row's state */

typedef enum {
    PROG_ROW_LIVE = 0,       /**< a tap opens its session                 */
    PROG_ROW_NO_CAP,         /**< the IOmcu lacks a needed capability      */
    PROG_ROW_PORT_BUSY,      /**< another row's session holds the port    */
} prog_row_state_t;

/** Row @p i of @p rows with the IOmcu reporting @p caps (link_cap_t). */
prog_row_state_t prog_reg_state(const prog_reg_row_t *rows, size_t n,
                                size_t i, uint16_t caps);

/** The lowest link_cap_t bit row @p i needs and @p caps lacks; 0 none. */
uint16_t prog_reg_missing(const prog_reg_row_t *row, uint16_t caps);

/** A capability bit's name as the link names it ("SERVO PWM"), the same
 *  in every language; "" for a bit with no name, or for 0. */
const char *prog_reg_cap_name(uint16_t bit);

/**
 * The text right of row @p i's name: for a live row on a port its number
 * and the session's pin ("PROG PORT 2 GP14", "PROG PORT 2" with the pin
 * not known), its transport and the pin for any other ("PROG PORT" with
 * the tester's port not known); its reason for a dimmed one ("IOMCU HAS
 * NO SERVO PWM", "PORT IN USE").
 * Writes at most @p size - 1 characters and returns whether the row is
 * dimmed.
 */
bool prog_reg_detail(const prog_reg_row_t *rows, size_t n, size_t i,
                     uint16_t caps, char *out, size_t size);

/* -------------------------------------------------------- the list model */

/** An entry of a list: a row, or the heading of the rows under it. */
typedef struct {
    uint8_t row;             /**< the table's row; for a heading, its first */
    bool    heading;
} prog_list_entry_t;

typedef struct {
    uint8_t total;           /**< the list's rows, before a search         */
    uint8_t rows;            /**< the rows shown                           */
    uint8_t count;           /**< entries: rows shown and headings         */
    bool    search;          /**< the list offers SEARCH                   */
    bool    headings;        /**< the rows are under their headings        */
    prog_list_entry_t e[2 * PROG_LIST_MAX];
} prog_list_t;

/**
 * The list of @p device's @p task from @p n rows of @p rows, narrowed to
 * the names @p query finds (esc_text_matches(): letters without regard to
 * case, "*" any text; NULL or "" finds all).  SEARCH stays offered while a
 * search narrows the list.  With headings, the groups come in the order
 * the table first names them and the rows keep the table's order inside
 * each.  Rows past PROG_LIST_MAX are not listed.
 */
void prog_list_build(prog_list_t *l, const prog_reg_row_t *rows, size_t n,
                     uint8_t device, uint8_t task, const char *query);

/* --------------------------------------------------------------- the view */

/*
 * A list drawn into a rectangle: a bar on top while the list offers SEARCH
 * or does not fit (SEARCH, "^", "v"), then its entries.  A row is 34 px
 * with 6 px under it, a heading 24 px with 4 px under it.  A row's press
 * is taken on its DOWN and is a tap on its release over the same row.
 */
#define PROG_VIEW_PAD     10
#define PROG_VIEW_BAR_H   30
#define PROG_VIEW_ROW_H   34
#define PROG_VIEW_ROW_P   40
#define PROG_VIEW_HEAD_H  24
#define PROG_VIEW_HEAD_P  28

/** What a press is on: an entry from 0, or one of these. */
enum { PROG_VIEW_P_NONE = -1, PROG_VIEW_P_SEARCH = -2, PROG_VIEW_P_UP = -3,
       PROG_VIEW_P_DOWN = -4 };

typedef struct {
    gfx_rect_t area;
    int        top;          /**< the first entry shown                    */
    int        pressed;      /**< an entry, a PROG_VIEW_P_*                */
    uint8_t    id;           /**< the press's contact                      */
    /** The search typed, the host's to keep: "" for none. */
    char       query[PROG_LIST_QUERY_MAX + 1];
} prog_view_t;

typedef enum {
    PROG_VIEW_NONE = 0,      /**< not the view's                           */
    PROG_VIEW_TAKEN,         /**< the view's, nothing to do                */
    PROG_VIEW_ROW,           /**< a row tapped: *row is the table's row     */
    PROG_VIEW_SEARCH,        /**< SEARCH tapped                            */
} prog_view_ev_t;

void prog_view_init(prog_view_t *v, gfx_rect_t area);

/** The list was rebuilt (a search, another language): back to its top. */
void prog_view_top(prog_view_t *v);

/** Whether the bar is drawn: SEARCH, or a list longer than the area. */
bool prog_view_has_bar(const prog_view_t *v, const prog_list_t *l);

/** The entries shown from the top, at least 1 for a list that has one. */
int prog_view_shown(const prog_view_t *v, const prog_list_t *l);

/** Entry @p k's rectangle, empty when it is not shown. */
gfx_rect_t prog_view_entry_rect(const prog_view_t *v, const prog_list_t *l,
                                int k);

/** SEARCH, "^" and "v" on the bar; empty when not drawn. */
gfx_rect_t prog_view_search_rect(const prog_view_t *v, const prog_list_t *l);
gfx_rect_t prog_view_up_rect(const prog_view_t *v, const prog_list_t *l);
gfx_rect_t prog_view_down_rect(const prog_view_t *v, const prog_list_t *l);

/**
 * A touch event.  "^" and "v" page the list themselves.  A heading takes a
 * press and does nothing.  A row tap is reported whatever its state: the
 * host opens a live row only (prog_reg_state()).
 */
prog_view_ev_t prog_view_event(prog_view_t *v, const prog_list_t *l,
                               const touch_event_t *evt, uint8_t *row);

/** Drop a press under way. */
void prog_view_cancel(prog_view_t *v);

/**
 * Draws the list: @p query in the SEARCH box ("SEARCH" while empty), each
 * row's name and prog_reg_detail(), a live row with ">" at its right
 * end, a dimmed one in the dim colour with its reason in the warning
 * colour.
 */
void prog_view_render(const prog_view_t *v, const prog_list_t *l,
                      const prog_reg_row_t *rows, size_t n, uint16_t caps,
                      const char *query, gfx_canvas_t *c);

#ifdef __cplusplus
}
#endif
