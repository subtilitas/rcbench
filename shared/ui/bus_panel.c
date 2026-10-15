/*
 * The BUS page of the SERVO screen.  See bus_panel.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "bus_panel.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bus_proto.h"
#include "link_pages.h"
#include "ui_slider.h"
#include "ui_text.h"
#include "ui_theme.h"
#include "ui_widgets.h"

/* ------------------------------------------------------------------ geometry */

#define ROW0     48           /* below the area's top */
#define PITCH    42
#define ROW_H    34
#define VAL_W    100
#define LINE_H   18
#define LABEL_W  130          /* the slider row's label */
#define WARN_LINES 12
#define WARN_PITCH 22
#define NO_ID    (-1)
#define ID_COUNT 254

/* The rows. */
enum {
    A_PROTOCOL = 0, A_BAUD, A_SERIES, A_BUS, A_FROM, A_TO, A_SCAN, A_ID,
    A_TORQUE, A_SPEED, A_ACC, A_EEPROM, A_COUNT
};

typedef struct {
    uint8_t row, col, what;
    int16_t label;            /* a ui_text_id_t */
} row_t;

static const row_t k_rows[] = {
    { 0, 0, A_PROTOCOL, TX_BS_PROTOCOL },
    { 0, 1, A_BAUD,     TX_BS_BAUD },
    { 1, 0, A_SERIES,   TX_BS_SERIES },
    { 1, 1, A_BUS,      TX_BS_BUS },
    { 2, 0, A_FROM,     TX_BS_FROM },
    { 2, 1, A_TO,       TX_BS_TO },
    { 3, 0, A_SCAN,     TX_BS_FOUND },
    { 3, 1, A_ID,       TX_BS_ID },
    { 4, 0, A_TORQUE,   TX_BS_TORQUE },
    { 4, 1, A_SPEED,    TX_BS_SPEED },
    { 5, 0, A_ACC,      TX_BS_ACC },
    { 5, 1, A_EEPROM,   TX_BS_EEPROM },
};
#define ROW_COUNT ((int)(sizeof(k_rows) / sizeof(k_rows[0])))
#define SLIDER_ROW 6

/* What a command was for. */
enum {
    U_NONE = 0, U_OPEN, U_CLOSE, U_BAUD, U_SCAN, U_STOP, U_WATCH, U_TORQUE,
    U_GOAL, U_EE_READ, U_EE_WRITE, U_RESTORE, U_CHECK, U_TQ_READ, U_TQ_OFF,
    U_PROBE
};

/* What a servo does with a goal position, as the page last read it. */
enum { CTL_UNKNOWN = 0, CTL_POSITION, CTL_OTHER };

/* The open editors. */
enum { C_PROTOCOL = 0, C_BAUD, C_SERIES, C_ID, C_EEPROM, C_EE_VALUE };
enum { K_FROM = 0, K_TO, K_ID, K_SPEED, K_ACC, K_EE_VALUE };
enum { W_EEPROM = 0, W_RESTORE };

/* The presses. */
enum {
    P_NONE = 0, P_ROW, P_SLIDER, P_KEYPAD, P_CHOICE, P_CHOICE_CANCEL,
    P_APPLY, P_CANCEL
};

/* The EEPROM list's entries, past the roles: RESTORE. */
#define E_RESTORE 100

/* No second rate. */
#define NO_ALT 0xFFu

/* Results (feetech_res_t) the page tells apart. */
#define RES_NO_REPLY 2u         /* a frame no servo answered */
#define RES_DRIVER   10u        /* the driver refused a frame or the rate */

typedef struct {
    uint8_t id, series, addr, n;
    uint8_t old[2];
    uint8_t role;
    uint8_t baud;               /* the rate the servo answers at, as far as
                                   the page knows: tried first */
    uint8_t alt;                /* a baud write not read back: the other
                                   rate it may answer at, or NO_ALT */
} backup_t;

static struct {
    gfx_rect_t area;
    bool       shown;

    /* what the operator set */
    uint8_t  proto;
    uint8_t  baud;              /* the index OPEN asks for */
    uint8_t  baud_want;         /* the index a BAUD sent asks for */
    uint8_t  from, to;
    int      id;
    uint8_t  series[ID_COUNT];
    uint8_t  series_all;        /* while no ID is chosen */
    uint8_t  torque_on[(ID_COUNT + 7) / 8];  /* bit: torque sent on */
    uint8_t  ctl[ID_COUNT];     /* CTL_*: position mode, as read */
    int      chk_id;            /* the mode read's ID */
    uint8_t  chk_series, chk_n;
    int      torque_id;         /* the TORQUE command's ID */
    bool     torque_want;       /* and the state it sends */
    bool     tq_read_due;       /* a torque on went unanswered: read the
                                   register back */
    bool     tq_off_due;        /* nor did the read: torque off to all */
    uint16_t speed;
    uint8_t  acc;
    bool     goal_due;
    ui_slider_t pos;

    /* what the control task said */
    bus_panel_feed_t f;
    bool     fed;
    uint32_t stops_seen;

    /* the command */
    bool     out_due;
    bus_link_cmd_t out;
    uint32_t next_id;
    bool     waiting;
    int      use;
    uint32_t wait_id;
    uint32_t wait_ms;
    uint32_t answers_seen;
    bool     taken;
    uint16_t taken_seq;
    uint8_t  watch_series;      /* the series the last WATCH named */
    int      watch_id;

    /* an EEPROM write under way */
    uint8_t  ee_role;
    uint8_t  ee_id, ee_series, ee_addr, ee_n;
    uint16_t ee_old, ee_new;
    uint8_t  ee_baud;           /* the line's rate when it went */

    backup_t bk[BUS_PANEL_BACKUPS];
    unsigned bk_n;
    bool     restoring;

    char     msg[BUS_PANEL_LINE];
    bool     msg_warn;
    char     lines[4][BUS_PANEL_LINE];
    uint8_t  line_warn;         /* bit i: line i in the warning colour */

    ui_keypad_t kp;
    int      kp_what;

    struct {
        bool open;
        int  kind;
        int  count;
        char title[48];
        char labels[BUS_PANEL_CHOICES][32];
        int  values[BUS_PANEL_CHOICES];
        int  current;
    } ch;

    struct {
        bool      open;
        int       kind;
        bool      down;
        ui_hold_t hold;
        int       count;
        char      title[48];
        const char *apply;
        char      lines[WARN_LINES][BUS_PANEL_LINE];
    } warn;

    bool    have;
    uint8_t pid;
    int     pressed;
    int     row;

    uint32_t rev;
    uint32_t hold_rev;
    uint32_t sig;
} b;

/* ------------------------------------------------------------------ helpers */

static const bus_proto_t *proto(void)
{
    const bus_proto_t *p = bus_proto_get(b.proto);

    return p != NULL ? p : bus_proto_get(0);
}

static uint16_t sreg(unsigned r)
{
    return r < BUS_LINK_STATUS_N ? b.f.rd.status[r] : 0u;
}

static bool page_ok(void)
{
    return b.fed && b.f.rd.up && b.f.rd.page && b.f.rd.known;
}

static link_bus_state_t bus_state(void)
{
    return page_ok() ? (link_bus_state_t)LINK_BS_STATE_OF(sreg(LINK_BS_STATE))
                     : LINK_BUS_CLOSED;
}

static bool page_busy(void)
{
    return page_ok() && (sreg(LINK_BS_STATE) & LINK_BS_F_BUSY) != 0u;
}

static bool bus_open(void)
{
    return bus_state() == LINK_BUS_OPEN;
}

static uint8_t series_now(void)
{
    return b.id >= 0 ? b.series[b.id] : b.series_all;
}

/* The baud index in force: the page's while the bus is open. */
static uint8_t baud_now(void)
{
    if (bus_state() == LINK_BUS_OPEN || bus_state() == LINK_BUS_CLOSING) {
        return (uint8_t)(sreg(LINK_BS_OP) >> 8);
    }
    return b.baud;
}

/* Torque as the page last sent it to @p id: on until it sends off, a STOP
 * or a CLOSE. */
static bool torque_of(int id)
{
    return id >= 0 && id < ID_COUNT
           && (b.torque_on[id / 8] & (1u << (id % 8))) != 0u;
}

static void torque_put(int id, bool on)
{
    if (id < 0 || id >= ID_COUNT) {
        return;
    }
    if (on) {
        b.torque_on[id / 8] |= (uint8_t)(1u << (id % 8));
    } else {
        b.torque_on[id / 8] &= (uint8_t)~(1u << (id % 8));
    }
}

static bool reg_of(bus_role_t role, bus_reg_t *r)
{
    return proto()->reg(series_now(), role, r);
}

/* Whether the scan found @p id. */
static bool found(unsigned id)
{
    return id < ID_COUNT && b.f.rd.ids_known
           && (b.f.rd.ids[id / 16u] & (1u << (id % 16u))) != 0u;
}

static unsigned found_n(void)
{
    return (unsigned)(sreg(LINK_BS_SCAN) >> 8);
}

static bool scanning(void)
{
    return b.waiting && b.use == U_SCAN;
}

/* A command can go: no other waits for its answer. */
static bool idle(void)
{
    return !b.waiting && !b.out_due;
}

static gfx_color_t col(int c)
{
    return ui_theme_color((ui_color_id_t)c);
}

/* ------------------------------------------------------------------ geometry */

static gfx_rect_t row_rect(const row_t *r)
{
    const int col_w = (b.area.w - 30) / 2;
    const int x = r->col == 0 ? b.area.x + 10 : b.area.x + 20 + col_w;

    return (gfx_rect_t){ (int16_t)x,
                         (int16_t)(b.area.y + ROW0 + r->row * PITCH),
                         (int16_t)col_w, ROW_H };
}

static gfx_rect_t value_rect(const row_t *r)
{
    const gfx_rect_t rr = row_rect(r);

    return (gfx_rect_t){ (int16_t)(rr.x + rr.w - VAL_W), rr.y, VAL_W,
                         rr.h };
}

static const row_t *row_at(int row, int c)
{
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (k_rows[i].row == row && k_rows[i].col == c) {
            return &k_rows[i];
        }
    }
    return NULL;
}

gfx_rect_t bus_panel_value_rect(int row, int c)
{
    const row_t *r = row_at(row, c);

    return r != NULL ? value_rect(r) : (gfx_rect_t){ 0, 0, 0, 0 };
}

gfx_rect_t bus_panel_slider_rect(void)
{
    return (gfx_rect_t){ (int16_t)(b.area.x + 10 + LABEL_W),
                         (int16_t)(b.area.y + ROW0 + SLIDER_ROW * PITCH + 6),
                         (int16_t)(b.area.w - 20 - LABEL_W - 10), 22 };
}

gfx_rect_t bus_panel_choice_rect(int i)
{
    const int col_w = (b.area.w - 30) / 2;
    const int c = i / 5;
    const int line = i % 5;

    return (gfx_rect_t){ (int16_t)(b.area.x + 10 + c * (col_w + 10)),
                         (int16_t)(b.area.y + 48 + line * 46),
                         (int16_t)col_w, 40 };
}

gfx_rect_t bus_panel_choice_cancel_rect(void)
{
    return (gfx_rect_t){ (int16_t)(b.area.x + b.area.w - 10 - 110),
                         (int16_t)(b.area.y + b.area.h - 46), 110, 36 };
}

gfx_rect_t bus_panel_apply_rect(void)
{
    return (gfx_rect_t){ (int16_t)(b.area.x + 20),
                         (int16_t)(b.area.y + b.area.h - 76), 260, 56 };
}

gfx_rect_t bus_panel_cancel_rect(void)
{
    return (gfx_rect_t){ (int16_t)(b.area.x + b.area.w - 20 - 160),
                         (int16_t)(b.area.y + b.area.h - 76), 160, 56 };
}

ui_keypad_t *bus_panel_keypad(void)
{
    return &b.kp;
}

/* ---------------------------------------------------------------- the texts */

static const char *result_text(uint8_t r)
{
    static const int16_t k[] = {
        TX_BS_M_DONE, -1, TX_BS_M_NO_REPLY, TX_BS_M_TRUNCATED,
        TX_BS_M_CHECKSUM, TX_BS_M_WRONG_ID, TX_BS_M_LENGTH, TX_BS_M_NOISE,
        TX_BS_M_FRAMING, TX_BS_M_VERIFY, TX_BS_M_DRIVER, TX_BS_M_ABORTED,
        TX_BS_M_ARG,
    };

    if (r >= sizeof(k) / sizeof(k[0]) || k[r] < 0) {
        return "?";
    }
    return ui_tr((ui_text_id_t)k[r]);
}

static const char *refusal_text(uint8_t r)
{
    static const int16_t k[] = {
        -1, TX_BS_R_BUSY, TX_BS_R_PORT, TX_BS_R_PROTOCOL, TX_BS_R_UNSAFE,
        TX_BS_R_ARMED, TX_BS_R_NOT_OPEN, TX_BS_R_NO_PIN, TX_BS_R_NO_KEY,
        TX_BS_R_RANGE, TX_BS_R_EEPROM, TX_BS_R_STAGED, TX_BS_R_LOCK,
    };

    if (r >= sizeof(k) / sizeof(k[0]) || k[r] < 0) {
        return "?";
    }
    return ui_tr((ui_text_id_t)k[r]);
}

static const char *role_name(uint8_t role)
{
    switch (role) {
    case BUS_R_ID:        return TR(BS_ID);
    case BUS_R_BAUD:      return TR(BS_BAUD);
    case BUS_R_MIN_ANGLE: return TR(BS_E_MIN);
    case BUS_R_MAX_ANGLE: return TR(BS_E_MAX);
    case BUS_R_MODE:      return TR(BS_E_MODE);
    case BUS_R_LOCK:      return TR(BS_E_LOCK);
    default:              return "?";
    }
}

/* A register's value as the page writes it: the baud rate for BAUD, the
 * mode's name for MODE, the number otherwise. */
static void fmt_reg(uint8_t role, uint16_t v, char *out, size_t n)
{
    const bus_proto_t *p = proto();

    if (role == BUS_R_BAUD && v < p->baud_count) {
        snprintf(out, n, "%lu", (unsigned long)p->baud(v));
    } else if (role == BUS_R_MODE && v < p->mode_count) {
        snprintf(out, n, "%s", p->mode_names[v]);
    } else {
        snprintf(out, n, "%u", (unsigned)v);
    }
}

static void say(bool warn, const char *s)
{
    snprintf(b.msg, sizeof(b.msg), "%s", s);
    b.msg_warn = warn;
    ++b.rev;
}

/* ---------------------------------------------------------------- commands */

static void send(int use, link_bus_op_t op, uint16_t arg, uint16_t aux)
{
    memset(&b.out, 0, sizeof(b.out));
    b.out.op = op;
    b.out.port = b.f.port;
    b.out.proto = proto()->link_proto;
    b.out.sts = series_now() != 0u;
    b.out.arg = arg;
    b.out.aux = aux;
    b.out.id = ++b.next_id;
    b.out_due = true;
    b.waiting = true;
    b.use = use;
    b.wait_id = b.out.id;
    b.wait_ms = b.f.now_ms;
    b.taken = false;
    ++b.rev;
}

static void send_bytes(int use, link_bus_op_t op, uint8_t id, uint8_t addr,
                       const uint8_t *bytes, uint8_t n, bool key)
{
    send(use, op, (uint16_t)(id | ((unsigned)addr << 8)), 0u);
    b.out.n = n;
    memcpy(b.out.data, bytes, n);
    b.out.key = key;
}

static void send_goal(void)
{
    uint8_t data[LINK_BS_BYTES];
    uint8_t addr = 0;
    const unsigned n = bus_proto_goal(proto(), series_now(),
                                      (uint16_t)lroundf(b.pos.value),
                                      b.speed, b.acc, &addr, data,
                                      sizeof(data));

    b.goal_due = false;
    if (n == 0u || b.id < 0) {
        say(true, TR(BS_M_ARG));
        return;
    }
    send_bytes(U_GOAL, LINK_BUS_OP_WRITE, (uint8_t)b.id, addr, data,
               (uint8_t)n, false);
}

/* Before the first goal to an ID: whether it runs in position mode. */
static void send_check(void)
{
    uint8_t addr = 0;
    const uint8_t n = proto()->goal_check(series_now(), &addr);

    if (n == 0u) {
        b.ctl[b.id] = CTL_POSITION;     /* nothing tells another mode */
        send_goal();
        return;
    }
    b.chk_id = b.id;
    b.chk_series = series_now();
    b.chk_n = n;
    send(U_CHECK, LINK_BUS_OP_READ,
         (uint16_t)((unsigned)b.id | ((unsigned)addr << 8)), n);
}

/* The mode of every servo is read again before its next goal. */
static void ctl_forget(void)
{
    memset(b.ctl, CTL_UNKNOWN, sizeof(b.ctl));
}

static void send_watch(void)
{
    b.watch_id = b.id;
    b.watch_series = series_now();
    send(U_WATCH, LINK_BUS_OP_WATCH, b.id >= 0 ? (uint16_t)b.id : 0xFFu, 0u);
}

/* The newest backup goes back at the rate its servo answers at: the line
 * moves there first, with torque off to every servo, as BAUD. */
static void restore_next(void)
{
    const backup_t *k = &b.bk[b.bk_n - 1u];

    if (k->baud != baud_now() && k->baud < proto()->baud_count) {
        b.baud_want = k->baud;
        send(U_BAUD, LINK_BUS_OP_BAUD, k->baud, 0u);
        return;
    }
    /* A rate not certain: a read finds out whether the servo answers here
     * before the write, which owes a lock to a servo it unlocks. */
    if (k->alt != NO_ALT) {
        send(U_PROBE, LINK_BUS_OP_READ,
             (uint16_t)((unsigned)k->id | ((unsigned)k->addr << 8)), k->n);
        return;
    }
    send_bytes(U_RESTORE, LINK_BUS_OP_EEPROM, k->id, k->addr, k->old, k->n,
               true);
    b.out.sts = k->series != 0u;
}

/* ------------------------------------------------------------------ editors */

static void close_editors(void)
{
    ui_keypad_close(&b.kp);
    b.ch.open = false;
    b.warn.open = false;
    b.warn.down = false;
    ui_hold_reset(&b.warn.hold);
    ++b.rev;
}

static void choice_open(int kind, const char *title, int current)
{
    b.ch.open = true;
    b.ch.kind = kind;
    b.ch.count = 0;
    b.ch.current = current;
    snprintf(b.ch.title, sizeof(b.ch.title), "%s", title);
}

static void choice_add(const char *label, int value)
{
    if (b.ch.count < BUS_PANEL_CHOICES) {
        snprintf(b.ch.labels[b.ch.count], sizeof(b.ch.labels[0]), "%s",
                 label);
        b.ch.values[b.ch.count++] = value;
    }
}

static void keypad_open(int what, const char *title, unsigned value,
                        unsigned min, unsigned max)
{
    b.kp_what = what;
    ui_keypad_open(&b.kp, b.area, title, "", (float)value, (float)min,
                   (float)max, 0);
}

static void warn_open(int kind, const char *title, const char *apply)
{
    memset(&b.warn, 0, sizeof(b.warn));
    b.warn.open = true;
    b.warn.kind = kind;
    b.warn.apply = apply;
    snprintf(b.warn.title, sizeof(b.warn.title), "%s", title);
}

static char *warn_line(void)
{
    const int i = b.warn.count < WARN_LINES ? b.warn.count++ : WARN_LINES - 1;

    return b.warn.lines[i];
}

/* The page that asks for the hold of an EEPROM write. */
static void warn_eeprom(void)
{
    char was[24], now[24];

    fmt_reg(b.ee_role, b.ee_old, was, sizeof(was));
    fmt_reg(b.ee_role, b.ee_new, now, sizeof(now));
    warn_open(W_EEPROM, TR(BS_W_TITLE), TR(KP_HOLD_WRITE));
    snprintf(warn_line(), BUS_PANEL_LINE, TR(BS_W_CHANGE), (unsigned)b.ee_id,
             role_name(b.ee_role), was, now);
    snprintf(warn_line(), BUS_PANEL_LINE, "%s", TR(BS_W_KEEP));
    if (b.ee_role == BUS_R_ID) {
        snprintf(warn_line(), BUS_PANEL_LINE, TR(BS_W_NEW_ID),
                 (unsigned)b.ee_new);
        if (found(b.ee_new)) {
            snprintf(warn_line(), BUS_PANEL_LINE, TR(BS_W_SAME_ID),
                     (unsigned)b.ee_new);
        }
    } else if (b.ee_role == BUS_R_BAUD) {
        snprintf(warn_line(), BUS_PANEL_LINE, TR(BS_W_NEW_BAUD),
                 (unsigned long)proto()->baud(b.ee_new));
    } else if (b.ee_role == BUS_R_LOCK && b.ee_new == 0u) {
        snprintf(warn_line(), BUS_PANEL_LINE, "%s", TR(BS_W_UNLOCK));
    }
    snprintf(warn_line(), BUS_PANEL_LINE, "%s", TR(BS_W_BACKUP));
}

static void warn_restore(void)
{
    bool moves = false;

    warn_open(W_RESTORE, TR(BS_W_R_TITLE), TR(KP_HOLD_RESTORE));
    for (unsigned i = b.bk_n; i-- > 0u;) {
        const backup_t *k = &b.bk[i];
        char v[24];
        uint16_t old = k->old[0];

        if (k->n == 2u) {
            old = proto()->get16(k->series, k->old);
        }
        fmt_reg(k->role, old, v, sizeof(v));
        snprintf(warn_line(), BUS_PANEL_LINE, TR(BS_W_R_LINE),
                 (unsigned)k->id, role_name(k->role), v);
        moves = moves || k->baud != baud_now() || k->alt != NO_ALT;
    }
    snprintf(warn_line(), BUS_PANEL_LINE, TR(BS_W_R_ORDER), b.bk_n);
    if (moves) {
        snprintf(warn_line(), BUS_PANEL_LINE, "%s", TR(BS_W_R_BAUD));
    }
}

/* The EEPROM list: the roles the series has, then RESTORE. */
static void eeprom_list(void)
{
    static const uint8_t roles[] = {
        BUS_R_ID, BUS_R_BAUD, BUS_R_MIN_ANGLE, BUS_R_MAX_ANGLE, BUS_R_MODE,
        BUS_R_LOCK,
    };

    choice_open(C_EEPROM, TR(BS_EEPROM), -1);
    for (unsigned i = 0; i < sizeof(roles); ++i) {
        bus_reg_t r;

        if (reg_of((bus_role_t)roles[i], &r)) {
            choice_add(role_name(roles[i]), roles[i]);
        }
    }
    if (b.bk_n > 0u) {
        char l[32];

        snprintf(l, sizeof(l), TR(BS_E_RESTORE), b.bk_n);
        choice_add(l, E_RESTORE);
    }
}

/* An EEPROM item chosen: its old value is read first. */
static void eeprom_pick(uint8_t role)
{
    bus_reg_t r;

    if (!reg_of((bus_role_t)role, &r) || b.id < 0) {
        return;
    }
    if (b.bk_n >= BUS_PANEL_BACKUPS) {
        char m[BUS_PANEL_LINE];

        snprintf(m, sizeof(m), TR(BS_M_FULL), b.bk_n);
        say(true, m);
        return;
    }
    b.ee_role = role;
    b.ee_id = (uint8_t)b.id;
    b.ee_series = series_now();
    b.ee_addr = r.addr;
    b.ee_n = r.size;
    send(U_EE_READ, LINK_BUS_OP_READ,
         (uint16_t)(b.ee_id | ((unsigned)r.addr << 8)), r.size);
    say(false, TR(BS_M_READING));
}

/* The old value is in: the editor for the new one. */
static void eeprom_edit(void)
{
    bus_reg_t r;
    const bus_proto_t *p = proto();

    if (!p->reg(b.ee_series, (bus_role_t)b.ee_role, &r)) {
        return;
    }
    if (b.ee_role == BUS_R_BAUD || b.ee_role == BUS_R_MODE
        || b.ee_role == BUS_R_LOCK) {
        const unsigned last = b.ee_role == BUS_R_BAUD
                                  ? (unsigned)p->baud_count - 1u
                                  : r.max;

        choice_open(C_EE_VALUE, role_name(b.ee_role), (int)b.ee_old);
        for (unsigned v = r.min; v <= last && v <= r.max; ++v) {
            char l[24];

            fmt_reg(b.ee_role, (uint16_t)v, l, sizeof(l));
            choice_add(l, (int)v);
        }
        return;
    }
    keypad_open(K_EE_VALUE, role_name(b.ee_role), b.ee_old, r.min, r.max);
}

static void eeprom_value(uint16_t v)
{
    if (v == b.ee_old) {
        return;                 /* nothing to write */
    }
    b.ee_new = v;
    warn_eeprom();
}

/* ---------------------------------------------------------------- answers */

/* An EEPROM write, landed or not, may have changed the servo's mode. */
static void ee_forget(void)
{
    if (b.ee_id < ID_COUNT) {
        b.ctl[b.ee_id] = CTL_UNKNOWN;
    }
    if (b.ee_role == BUS_R_ID && b.ee_new < ID_COUNT) {
        b.ctl[b.ee_new] = CTL_UNKNOWN;
    }
}

/* @p landed: the write was read back. */
static void push_backup(bool landed)
{
    backup_t *k;

    if (b.bk_n >= BUS_PANEL_BACKUPS) {
        return;
    }
    k = &b.bk[b.bk_n++];
    memset(k, 0, sizeof(*k));
    k->id = b.ee_role == BUS_R_ID ? (uint8_t)b.ee_new : b.ee_id;
    k->series = b.ee_series;
    k->addr = b.ee_addr;
    k->n = b.ee_n;
    k->role = b.ee_role;
    /* A baud write read back moved the servo and the line with it.  One
     * not read back left the line where the coprocessor found the servo:
     * at the new rate when the lock answered there, else back at the old
     * one; the page's last read names it. */
    k->alt = NO_ALT;
    if (b.ee_role != BUS_R_BAUD) {
        k->baud = b.ee_baud;
    } else if (landed && b.ee_new < proto()->baud_count) {
        k->baud = (uint8_t)b.ee_new;
    } else {
        /* Not read back: the servo may answer at either rate.  The one the
         * page last read the line at goes first, the other is kept. */
        const uint8_t other = baud_now() == b.ee_baud ? (uint8_t)b.ee_new
                                                      : b.ee_baud;

        k->baud = baud_now();
        if (other != k->baud && other < proto()->baud_count) {
            k->alt = other;
        }
    }
    if (b.ee_n == 2u) {
        proto()->put16(b.ee_series, k->old, b.ee_old);
    } else {
        k->old[0] = (uint8_t)b.ee_old;
    }
}

/* An EEPROM write that may have landed: a failure after it went proves
 * nothing, so its old value is kept for RESTORE.  Not for an ID, whose
 * servo may answer on either. */
static void keep_uncertain(void)
{
    if (b.use == U_EE_WRITE && b.ee_role != BUS_R_ID) {
        push_backup(false);
    }
}

/* The bytes the last read gave. */
static unsigned read_bytes(uint8_t *out, unsigned room)
{
    const unsigned n = (unsigned)(sreg(LINK_BS_COUNTS) >> 8);

    for (unsigned i = 0; i < n && i < room; ++i) {
        const uint16_t r = sreg(LINK_BS_DATA + i / 2u);

        out[i] = (uint8_t)(i % 2u == 0u ? r & 0xFFu : r >> 8);
    }
    return n < room ? n : room;
}

/* A baud write or BAUD, which start with torque off to every servo, ended
 * in @p result short of success.  DRIVER may be the broadcast itself
 * refused: the marks stay and torque off goes again, whose answer settles
 * them.  Any other result came after the broadcast went. */
static void torque_off_went(uint8_t result)
{
    if (result == RES_DRIVER) {
        b.tq_off_due = true;
    } else {
        memset(b.torque_on, 0, sizeof(b.torque_on));
    }
}

/* The command in hand starts with torque off to every servo: a baud write
 * by the EEPROM list or RESTORE, or BAUD. */
static bool sends_torque_off(void)
{
    return (b.use == U_EE_WRITE && b.ee_role == BUS_R_BAUD)
           || (b.use == U_RESTORE && b.bk_n > 0u
               && b.bk[b.bk_n - 1u].role == BUS_R_BAUD)
           || b.use == U_BAUD;
}

/* A command handed over came to no answer: its torque off may have gone.
 * The marks stay and torque off goes again, whose answer settles them. */
static void handed_over_unanswered(void)
{
    keep_uncertain();
    if (sends_torque_off()) {
        b.tq_off_due = true;
    }
}

/* The command came to @p result (feetech_res_t), with nothing refused. */
static void done(uint8_t result)
{
    const int use = b.use;
    const bool ok = result == 0u;

    b.waiting = false;
    ++b.rev;
    switch (use) {
    case U_EE_READ: {
        uint8_t bytes[2] = { 0, 0 };

        if (!ok || read_bytes(bytes, 2u) != b.ee_n) {
            say(true, ok ? TR(BS_M_LENGTH) : result_text(result));
            return;
        }
        b.ee_old = b.ee_n == 2u ? proto()->get16(b.ee_series, bytes)
                                : bytes[0];
        b.msg[0] = '\0';
        eeprom_edit();
        return;
    }
    case U_EE_WRITE:
        ee_forget();
        if (!ok) {
            if (b.ee_role == BUS_R_BAUD) {
                torque_off_went(result);
            }
            keep_uncertain();
            say(true, result_text(result));
            return;
        }
        push_backup(true);
        if (b.ee_role == BUS_R_ID && b.ee_new < ID_COUNT) {
            b.series[b.ee_new] = b.ee_series;
            torque_put(b.ee_new, torque_of(b.ee_id));
            torque_put(b.ee_id, false);
            b.id = b.ee_new;
        } else if (b.ee_role == BUS_R_BAUD
                   && b.ee_new < proto()->baud_count) {
            /* The coprocessor followed the servo; the next OPEN does too.
             * The write sent torque off to every servo first. */
            b.baud = (uint8_t)b.ee_new;
            memset(b.torque_on, 0, sizeof(b.torque_on));
        }
        say(false, TR(BS_M_WRITTEN));
        return;
    case U_RESTORE:
        if (!ok) {
            if (b.bk_n > 0u && b.bk[b.bk_n - 1u].role == BUS_R_BAUD) {
                torque_off_went(result);
            }
            b.restoring = false;
            say(true, result_text(result));
            return;
        }
        {
            const backup_t *k = &b.bk[b.bk_n - 1u];

            if (k->role == BUS_R_ID && k->old[0] < ID_COUNT) {
                torque_put(k->old[0], torque_of(k->id));
                torque_put(k->id, false);
                if (b.id == (int)k->id) {
                    b.id = k->old[0];
                    b.series[k->old[0]] = k->series;
                }
            } else if (k->role == BUS_R_BAUD
                       && k->old[0] < proto()->baud_count) {
                /* The write sent torque off to every servo first. */
                b.baud = k->old[0];
                memset(b.torque_on, 0, sizeof(b.torque_on));
            }
        }
        b.bk_n--;
        if (b.bk_n == 0u) {
            b.restoring = false;
            say(false, TR(BS_M_RESTORED));
        }
        return;
    case U_CHECK: {
        uint8_t bytes[4] = { 0, 0, 0, 0 };
        const unsigned n = ok ? read_bytes(bytes, sizeof(bytes)) : 0u;
        bool pos = false;

        if (!ok || n != b.chk_n) {
            b.goal_due = false;
            say(true, ok ? TR(BS_M_LENGTH) : result_text(result));
            return;
        }
        pos = proto()->goal_mode(b.chk_series, bytes, n);
        if (b.chk_id >= 0 && b.chk_id < ID_COUNT
            && b.series[b.chk_id] == b.chk_series) {
            b.ctl[b.chk_id] = (uint8_t)(pos ? CTL_POSITION : CTL_OTHER);
        }
        if (!pos) {
            b.goal_due = false;
            say(true, TR(BS_M_MODE));
        }
        return;                 /* the goal goes next */
    }
    case U_BAUD:
        if (!ok) {
            torque_off_went(result);
            b.restoring = false;
            say(true, result_text(result));
            return;
        }
        /* BAUD sent torque off at the old rate; the next OPEN uses the
         * new one. */
        b.baud = b.baud_want;
        memset(b.torque_on, 0, sizeof(b.torque_on));
        ctl_forget();
        if (!b.restoring) {
            say(false, TR(BS_M_DONE));
        }
        return;
    case U_TORQUE:
        if (ok) {
            torque_put(b.torque_id, b.torque_want);
        } else if (b.torque_want && result == RES_NO_REPLY) {
            /* A servo whose response level is 0 takes a write without an
             * answer and still answers a read: the register says. */
            b.tq_read_due = true;
        }
        say(!ok, result_text(result));
        return;
    case U_TQ_READ: {
        uint8_t v = 0;

        if (ok && read_bytes(&v, 1u) == 1u) {
            torque_put(b.torque_id, v != 0u);
            return;
        }
        /* Nothing says the torque is off: off to every servo. */
        torque_put(b.torque_id, false);
        b.tq_off_due = true;
        say(true, ok ? TR(BS_M_LENGTH) : result_text(result));
        return;
    }
    case U_TQ_OFF:
        /* Only an answer of OK says torque off went out; otherwise the
         * marks stay and the message stands. */
        if (ok) {
            memset(b.torque_on, 0, sizeof(b.torque_on));
        } else {
            say(true, result_text(result));
        }
        return;
    case U_PROBE:
        if (b.bk_n == 0u) {
            b.restoring = false;
            return;
        }
        {
            backup_t *k = &b.bk[b.bk_n - 1u];

            if (ok) {
                k->alt = NO_ALT;        /* it answers here */
            } else if (result == RES_NO_REPLY && k->alt != NO_ALT) {
                k->baud = k->alt;       /* the other rate next */
                k->alt = NO_ALT;
            } else {
                b.restoring = false;
                say(true, result_text(result));
            }
        }
        return;
    default:
        /* A scan: the line under the rows has what it found. */
        if (!ok) {
            say(true, result_text(result));
        }
        return;
    }
}

/* The command started nothing, or never reached the page. */
static void failed(const char *why)
{
    if (b.use == U_RESTORE || b.use == U_BAUD || b.use == U_PROBE) {
        b.restoring = false;
    }
    if (b.use == U_TQ_READ) {
        torque_put(b.torque_id, false);
        b.tq_off_due = true;
    }
    if (b.use == U_CHECK) {
        b.goal_due = false;
    }
    b.waiting = false;
    say(true, why);
}

static void watch_answer(void)
{
    const bus_link_readout_t *r = &b.f.rd;

    if (!b.f.rd.up || !b.f.rd.page) {
        /* A command handed to the control task may have reached the
         * servo: an EEPROM write keeps its old value for RESTORE, and the
         * mode it may have changed is read again. */
        if (!b.out_due) {
            handed_over_unanswered();
            if (b.use == U_EE_WRITE) {
                ee_forget();
            }
        }
        b.out_due = false;
        failed(TR(BS_A_DOWN));
        return;
    }
    if (!b.taken) {
        if (r->answers != b.answers_seen && r->ans_id == b.wait_id) {
            switch ((bus_link_ans_t)r->ans) {
            case BUS_LINK_ANS_TAKEN:
                b.taken = true;
                b.taken_seq = r->ans_seq;
                break;
            case BUS_LINK_ANS_NACK: {
                char m[BUS_PANEL_LINE];

                snprintf(m, sizeof(m), TR(BS_A_NACK), (unsigned)r->ans_nack);
                failed(m);
                return;
            }
            case BUS_LINK_ANS_DROPPED:
                failed(TR(BS_A_DROPPED));
                return;
            case BUS_LINK_ANS_DOWN:
                failed(TR(BS_A_DOWN));
                return;
            default:
                handed_over_unanswered();
                failed(TR(BS_A_NO_ANSWER));
                return;
            }
        } else if (!b.out_due
                   && (uint32_t)(b.f.now_ms - b.wait_ms)
                          >= BUS_PANEL_ANSWER_MS) {
            handed_over_unanswered();
            failed(TR(BS_A_NO_ANSWER));
            return;
        }
    }
    if (b.taken && r->known && sreg(LINK_BS_SEQ) != b.taken_seq
        && (uint32_t)(b.f.now_ms - b.wait_ms) >= BUS_PANEL_ANSWER_MS) {
        /* Taken, and no read of the page shows it: another command took
         * the page over, or the coprocessor started again. */
        handed_over_unanswered();
        failed(TR(BS_A_NO_ANSWER));
        return;
    }
    if (!b.taken || !r->known || sreg(LINK_BS_SEQ) != b.taken_seq) {
        return;
    }
    {
        const uint8_t refusal = (uint8_t)(sreg(LINK_BS_RESULT) >> 8);
        const uint8_t result = (uint8_t)(sreg(LINK_BS_RESULT) & 0xFFu);

        if (refusal != 0u) {
            failed(refusal_text(refusal));
            return;
        }
        if (b.use == U_WATCH) {
            b.waiting = false;
            ++b.rev;
            return;
        }
        if (result == 1u || page_busy()) {
            return;             /* it runs */
        }
        done(result);
    }
}

/* What the page wants sent next while nothing waits. */
static void reconcile(void)
{
    const link_bus_state_t s = bus_state();

    if (!idle() || !page_ok()) {
        return;
    }
    if (!b.shown) {
        if (s == LINK_BUS_OPEN || s == LINK_BUS_OPENING) {
            send(U_CLOSE, LINK_BUS_OP_CLOSE, 0u, 0u);
        }
        return;
    }
    if (s != LINK_BUS_OPEN || page_busy()) {
        return;
    }
    b.baud = baud_now();        /* the line's rate, for the next OPEN */
    if (b.tq_off_due) {
        /* The marks go with its answer (done()). */
        b.tq_off_due = false;
        b.tq_read_due = false;
        send(U_TQ_OFF, LINK_BUS_OP_TORQUE_OFF, 0u, 0u);
        return;
    }
    if (b.tq_read_due) {
        bus_reg_t reg;

        b.tq_read_due = false;
        if (reg_of(BUS_R_TORQUE, &reg) && b.torque_id >= 0) {
            send(U_TQ_READ, LINK_BUS_OP_READ,
                 (uint16_t)((unsigned)b.torque_id
                            | ((unsigned)reg.addr << 8)), 1u);
            return;
        }
    }
    if (b.restoring) {
        if (b.bk_n == 0u) {
            b.restoring = false;
            return;
        }
        restore_next();
        return;
    }
    {
        const uint8_t watched = (uint8_t)(sreg(LINK_BS_WATCH) & 0xFFu);
        const int want = b.id;

        if ((want < 0 && watched != 0xFFu)
            || (want >= 0 && (watched != (uint8_t)want
                              || b.watch_series != series_now()
                              || b.watch_id != want))) {
            send_watch();
            return;
        }
    }
    if (b.goal_due && b.id >= 0) {
        if (b.ctl[b.id] == CTL_UNKNOWN) {
            send_check();
        } else if (b.ctl[b.id] == CTL_POSITION) {
            send_goal();
        } else {
            b.goal_due = false;
            say(true, TR(BS_M_MODE));
        }
    }
}

/* ------------------------------------------------------------------ actions */

static bool row_live(const row_t *r)
{
    const link_bus_state_t s = bus_state();
    const bool open_idle = s == LINK_BUS_OPEN && idle() && !page_busy();
    bus_reg_t reg;

    if (!page_ok()) {
        return r->what == A_FROM || r->what == A_TO || r->what == A_ID
               || r->what == A_SERIES || r->what == A_SPEED
               || (r->what == A_ACC && reg_of(BUS_R_ACC, &reg));
    }
    switch (r->what) {
    case A_PROTOCOL:
        return s == LINK_BUS_CLOSED && idle();
    case A_BAUD:
        return (s == LINK_BUS_CLOSED && idle()) || open_idle;
    case A_BUS:
        return ((s == LINK_BUS_CLOSED && idle())
                || ((s == LINK_BUS_OPEN || s == LINK_BUS_OPENING)
                    && (idle() || b.taken)));
    case A_SCAN:
        return open_idle || (scanning() && b.taken);
    case A_TORQUE:
    case A_EEPROM:
        return open_idle && b.id >= 0;
    case A_ACC:
        return reg_of(BUS_R_ACC, &reg);
    default:
        return true;
    }
}

bool bus_panel_row_live(int row, int c)
{
    const row_t *r = row_at(row, c);

    return r != NULL && row_live(r);
}

/* A command that ends one the page runs: the wait for it is dropped. */
static void preempt(void)
{
    if (b.waiting && b.taken) {
        b.waiting = false;
    }
}

static void act(const row_t *r)
{
    const bus_proto_t *p = proto();
    bus_reg_t reg;

    switch (r->what) {
    case A_PROTOCOL:
        choice_open(C_PROTOCOL, TR(BS_PROTOCOL), b.proto);
        for (unsigned i = 0; i < bus_proto_count(); ++i) {
            choice_add(bus_proto_get(i)->name, (int)i);
        }
        break;
    case A_BAUD:
        choice_open(C_BAUD, TR(BS_BAUD), baud_now());
        for (unsigned i = 0; i < p->baud_count; ++i) {
            char l[16];

            snprintf(l, sizeof(l), "%lu", (unsigned long)p->baud(i));
            choice_add(l, (int)i);
        }
        break;
    case A_SERIES:
        choice_open(C_SERIES, TR(BS_SERIES), series_now());
        for (unsigned i = 0; i < p->series_count; ++i) {
            choice_add(p->series_names[i], (int)i);
        }
        break;
    case A_BUS:
        if (bus_state() == LINK_BUS_CLOSED) {
            if (b.f.armed) {
                say(true, TR(BS_R_ARMED));
                break;
            }
            b.msg[0] = '\0';
            ctl_forget();
            send(U_OPEN, LINK_BUS_OP_OPEN, b.baud, 0u);
        } else {
            preempt();
            if (idle()) {
                b.restoring = false;
                send(U_CLOSE, LINK_BUS_OP_CLOSE, 0u, 0u);
            }
        }
        break;
    case A_FROM:
        keypad_open(K_FROM, TR(BS_FROM), b.from, 0u, p->id_max);
        break;
    case A_TO:
        keypad_open(K_TO, TR(BS_TO), b.to, 0u, p->id_max);
        break;
    case A_SCAN:
        if (scanning()) {
            preempt();
            send(U_STOP, LINK_BUS_OP_STOP, 0u, 0u);
        } else {
            b.msg[0] = '\0';
            ctl_forget();
            send(U_SCAN, LINK_BUS_OP_SCAN,
                 (uint16_t)(b.from | ((unsigned)b.to << 8)), 0u);
        }
        break;
    case A_ID: {
        unsigned n = 0;

        for (unsigned id = 0; id <= p->id_max; ++id) {
            n += found(id) ? 1u : 0u;
        }
        if (n == 0u) {
            keypad_open(K_ID, TR(BS_ID), b.id >= 0 ? (unsigned)b.id : 1u, 0u,
                        p->id_max);
            break;
        }
        choice_open(C_ID, TR(BS_ID), b.id);
        for (unsigned id = 0; id <= p->id_max
                              && b.ch.count < BUS_PANEL_CHOICES - 1; ++id) {
            if (found(id)) {
                char l[8];

                snprintf(l, sizeof(l), "%u", id);
                choice_add(l, (int)id);
            }
        }
        choice_add(TR(BS_OTHER_ID), -1);
        break;
    }
    case A_TORQUE:
        if (reg_of(BUS_R_TORQUE, &reg)) {
            const uint8_t v = (uint8_t)(torque_of(b.id) ? 0u : 1u);

            b.torque_id = b.id;
            b.torque_want = v != 0u;

            send_bytes(U_TORQUE, LINK_BUS_OP_WRITE, (uint8_t)b.id, reg.addr,
                       &v, 1u, false);
        }
        break;
    case A_SPEED:
        if (reg_of(BUS_R_SPEED, &reg)) {
            keypad_open(K_SPEED, TR(BS_SPEED), b.speed, reg.min, reg.max);
        }
        break;
    case A_ACC:
        if (reg_of(BUS_R_ACC, &reg)) {
            keypad_open(K_ACC, TR(BS_ACC), b.acc, reg.min, reg.max);
        }
        break;
    case A_EEPROM:
        eeprom_list();
        break;
    default:
        break;
    }
}

/* The slider follows the series' range; a goal outside it goes to the
 * middle, not to an end. */
static void slider_range(void)
{
    const uint16_t max = proto()->position_max(series_now());

    b.pos.max = (float)max;
    if (b.pos.value > (float)max) {
        const unsigned mid = max / 2u + 1u;

        b.pos.value = (float)mid;
    }
}

static void id_chosen(int id)
{
    if (id < 0 || id > (int)proto()->id_max) {
        return;
    }
    b.id = id;
    slider_range();
}

static void choice_done(int v)
{
    const int kind = b.ch.kind;

    b.ch.open = false;
    switch (kind) {
    case C_PROTOCOL:
        if (v >= 0 && (unsigned)v < bus_proto_count()) {
            b.proto = (uint8_t)v;
            b.baud = proto()->baud_default;
        }
        break;
    case C_BAUD:
        if (v < 0 || v >= proto()->baud_count) {
            break;
        }
        if (bus_open()) {
            /* The next OPEN keeps the old rate until the line moved. */
            b.baud_want = (uint8_t)v;
            send(U_BAUD, LINK_BUS_OP_BAUD, (uint16_t)v, 0u);
        } else {
            b.baud = (uint8_t)v;
        }
        break;
    case C_SERIES:
        if (v < 0 || v >= proto()->series_count) {
            break;
        }
        if (b.id >= 0) {
            b.series[b.id] = (uint8_t)v;
            b.ctl[b.id] = CTL_UNKNOWN;
        } else {
            b.series_all = (uint8_t)v;
            memset(b.series, v, sizeof(b.series));
            ctl_forget();
        }
        slider_range();
        break;
    case C_ID:
        if (v < 0) {
            keypad_open(K_ID, TR(BS_ID), b.id >= 0 ? (unsigned)b.id : 1u, 0u,
                        proto()->id_max);
        } else {
            id_chosen(v);
        }
        break;
    case C_EEPROM:
        if (v == E_RESTORE) {
            if (b.bk_n > 0u) {
                warn_restore();
            }
        } else {
            eeprom_pick((uint8_t)v);
        }
        break;
    case C_EE_VALUE:
        if (v >= 0) {
            eeprom_value((uint16_t)v);
        }
        break;
    default:
        break;
    }
}

static void keypad_done(float v)
{
    const long n = lroundf(v);

    if (n < 0L || n > 65535L) {
        return;
    }
    switch (b.kp_what) {
    case K_FROM:
        b.from = (uint8_t)n;
        if (b.to < b.from) {
            b.to = b.from;
        }
        break;
    case K_TO:
        b.to = (uint8_t)n;
        if (b.from > b.to) {
            b.from = b.to;
        }
        break;
    case K_ID:
        id_chosen((int)n);
        break;
    case K_SPEED:
        b.speed = (uint16_t)n;
        break;
    case K_ACC:
        b.acc = (uint8_t)n;
        break;
    case K_EE_VALUE:
        eeprom_value((uint16_t)n);
        break;
    default:
        break;
    }
}

static void warn_apply(void)
{
    const int kind = b.warn.kind;

    b.warn.open = false;
    b.warn.down = false;
    ui_hold_reset(&b.warn.hold);
    b.pressed = P_NONE;
    if (!bus_open() || !idle()) {
        say(true, TR(BS_M_NEED_OPEN));
        return;
    }
    if (kind == W_RESTORE) {
        ctl_forget();
        b.restoring = b.bk_n > 0u;
        reconcile();
        return;
    }
    {
        uint8_t data[2];

        if (b.ee_n == 2u) {
            proto()->put16(b.ee_series, data, b.ee_new);
        } else {
            data[0] = (uint8_t)b.ee_new;
        }
        b.ee_baud = baud_now();
        send_bytes(U_EE_WRITE, LINK_BUS_OP_EEPROM, b.ee_id, b.ee_addr, data,
                   b.ee_n, true);
        b.out.sts = b.ee_series != 0u;
    }
}

/* ------------------------------------------------------------------ touches */

static void take(const touch_event_t *evt, int what, int row)
{
    b.have = true;
    b.pid = evt->point.id;
    b.pressed = what;
    b.row = row;
}

static void let_go(void)
{
    b.have = false;
    b.pressed = P_NONE;
}

static void down(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;

    if (b.warn.open) {
        if (gfx_rect_contains(bus_panel_apply_rect(), x, y)) {
            take(evt, P_APPLY, -1);
            b.warn.down = true;
            ui_hold_begin(&b.warn.hold);
        } else if (gfx_rect_contains(bus_panel_cancel_rect(), x, y)) {
            take(evt, P_CANCEL, -1);
        }
        return;
    }
    if (b.kp.open) {
        take(evt, P_KEYPAD, -1);
        (void)ui_keypad_event(&b.kp, evt, NULL);
        return;
    }
    if (b.ch.open) {
        for (int i = 0; i < b.ch.count; ++i) {
            if (gfx_rect_contains(bus_panel_choice_rect(i), x, y)) {
                take(evt, P_CHOICE, i);
                return;
            }
        }
        if (gfx_rect_contains(bus_panel_choice_cancel_rect(), x, y)) {
            take(evt, P_CHOICE_CANCEL, -1);
        }
        return;
    }
    if (gfx_rect_contains(bus_panel_slider_rect(), x, y)) {
        if (bus_open() && b.id >= 0) {
            take(evt, P_SLIDER, -1);
            if (ui_slider_event(&b.pos, evt)) {
                b.goal_due = true;
            }
        }
        return;
    }
    for (int i = 0; i < ROW_COUNT; ++i) {
        if (row_live(&k_rows[i])
            && gfx_rect_contains(value_rect(&k_rows[i]), x, y)) {
            take(evt, P_ROW, i);
            return;
        }
    }
}

static void rest(const touch_event_t *evt)
{
    const int x = evt->point.x, y = evt->point.y;
    const bool up = evt->type == TOUCH_EVENT_UP;
    const int was = b.pressed;
    const int which = b.row;

    switch (was) {
    case P_KEYPAD: {
        float v = 0.0f;
        const ui_keypad_result_t r = ui_keypad_event(&b.kp, evt, &v);

        if (up) {
            let_go();
        }
        if (r == UI_KEYPAD_OK) {
            keypad_done(v);
        }
        return;
    }
    case P_SLIDER:
        /* A move writes the goal, and so does the release: a tap on the
         * value the slider has sends it too. */
        if (ui_slider_event(&b.pos, evt) || up) {
            b.goal_due = true;
        }
        if (up) {
            let_go();
        }
        return;
    case P_APPLY:
        if (!up) {
            /* A finger that leaves the button abandons the hold. */
            if (!gfx_rect_contains(bus_panel_apply_rect(), x, y)
                && ui_hold_leave(&b.warn.hold)) {
                b.warn.down = false;
                let_go();
            }
            return;
        }
        (void)ui_hold_end(&b.warn.hold);
        b.warn.down = false;
        let_go();
        return;
    default:
        break;
    }
    if (!up) {
        return;
    }
    let_go();
    if (!touch_event_is_tap_up(evt)) {
        return;
    }
    switch (was) {
    case P_ROW:
        if (which >= 0 && gfx_rect_contains(value_rect(&k_rows[which]), x, y)
            && row_live(&k_rows[which])) {
            act(&k_rows[which]);
        }
        break;
    case P_CHOICE:
        if (b.ch.open && which < b.ch.count
            && gfx_rect_contains(bus_panel_choice_rect(which), x, y)) {
            choice_done(b.ch.values[which]);
        }
        break;
    case P_CHOICE_CANCEL:
        if (gfx_rect_contains(bus_panel_choice_cancel_rect(), x, y)) {
            b.ch.open = false;
        }
        break;
    case P_CANCEL:
        if (gfx_rect_contains(bus_panel_cancel_rect(), x, y)) {
            close_editors();
        }
        break;
    default:
        break;
    }
}

bool bus_panel_has_press(uint8_t id)
{
    return b.have && b.pid == id;
}

void bus_panel_event(const touch_event_t *evt)
{
    if (evt == NULL) {
        return;
    }
    if (b.have && evt->point.id == b.pid && evt->type != TOUCH_EVENT_DOWN) {
        rest(evt);
    } else if (evt->type == TOUCH_EVENT_DOWN && !b.have) {
        down(evt);
    }
    ++b.rev;
}

void bus_panel_cancel(void)
{
    ui_keypad_cancel_press(&b.kp);
    ui_slider_release(&b.pos);
    ui_hold_reset(&b.warn.hold);
    b.warn.down = false;
    let_go();
    ++b.rev;
}

void bus_panel_leave(void)
{
    bus_panel_cancel();
    close_editors();
    bus_panel_show(false);
}

void bus_panel_tick(float dt_s)
{
    if (b.warn.open && b.warn.down) {
        if (ui_hold_tick(&b.warn.hold, dt_s)) {
            warn_apply();
            ++b.rev;
        }
        ++b.hold_rev;
    }
}

/* --------------------------------------------------------------- the caller */

void bus_panel_init(gfx_rect_t area)
{
    const bus_proto_t *p = bus_proto_get(0);

    memset(&b, 0, sizeof(b));
    b.area = area;
    b.proto = 0;
    b.baud = p->baud_default;
    b.from = BUS_PANEL_FROM;
    b.to = BUS_PANEL_TO;
    b.id = NO_ID;
    b.watch_id = NO_ID;
    b.chk_id = NO_ID;
    b.series_all = p->series_default;
    memset(b.series, p->series_default, sizeof(b.series));
    b.speed = 1000u;
    b.acc = 0u;
    b.kp.pressed = -1;
    ui_slider_init(&b.pos, bus_panel_slider_rect(), 0.0f,
                   (float)p->position_max(p->series_default),
                   ui_theme_color(UI_C_ACCENT));
    ui_slider_set_ticks(&b.pos, 4);
    ui_slider_set_tap_to_set(&b.pos, true);
    {
        const unsigned mid = p->position_max(p->series_default) / 2u + 1u;

        b.pos.value = (float)mid;
    }
    ++b.rev;
}

void bus_panel_show(bool shown)
{
    if (shown != b.shown) {
        b.shown = shown;
        if (!shown) {
            b.goal_due = false;
            b.restoring = false;
            /* The page goes, and the CLOSE goes next: a command not sent
             * goes with it, and the wait for one the control task runs is
             * dropped (the CLOSE ends it).  An EEPROM write it ran may
             * have landed: its old value is kept for RESTORE, as at STOP.
             * A command still on the link is answered within
             * BUS_PANEL_ANSWER_MS and waited for. */
            if (b.out_due) {
                b.out_due = false;
                b.waiting = false;
            } else if (b.waiting && b.taken) {
                keep_uncertain();
                if (b.use == U_EE_WRITE) {
                    ee_forget();
                }
                b.waiting = false;
            }
        }
        ++b.rev;
    }
}

bool bus_panel_is_shown(void)
{
    return b.shown;
}

bool bus_panel_holds_port(void)
{
    return (page_ok() && bus_state() != LINK_BUS_CLOSED)
           || (b.waiting && b.use == U_OPEN);
}

int bus_panel_editor(void)
{
    return b.kp.open ? 1 : b.ch.open ? 2 : b.warn.open ? 3 : 0;
}

/* The four lines under the rows, from what was fed. */
static void make_lines(void)
{
    const bus_proto_t *p = proto();
    const link_bus_state_t s = bus_state();
    const unsigned pin = b.f.pin;
    char *l = b.lines[0];

    memset(b.lines, 0, sizeof(b.lines));
    b.line_warn = 0;
    if (!b.fed || !b.f.rd.up) {
        snprintf(l, BUS_PANEL_LINE, "%s", TR(BS_L_NO_LINK));
        b.line_warn |= 1u;
    } else if (!b.f.rd.page) {
        snprintf(l, BUS_PANEL_LINE, "%s", TR(BS_L_NO_PAGE));
        b.line_warn |= 1u;
    } else if (s == LINK_BUS_OPEN) {
        snprintf(l, BUS_PANEL_LINE, TR(BS_L_OPEN),
                 (unsigned)sreg(LINK_BS_PIN), p->name,
                 (unsigned long)p->baud(baud_now()));
    } else if (s == LINK_BUS_OPENING) {
        snprintf(l, BUS_PANEL_LINE, TR(BS_L_OPENING), pin);
    } else if (s == LINK_BUS_CLOSING) {
        snprintf(l, BUS_PANEL_LINE, TR(BS_L_CLOSING),
                 (unsigned)sreg(LINK_BS_PIN));
    } else if (pin == 0xFFu) {
        snprintf(l, BUS_PANEL_LINE, "%s", TR(BS_L_NO_PIN));
        b.line_warn |= 1u;
    } else {
        snprintf(l, BUS_PANEL_LINE, TR(BS_L_CLOSED), pin);
    }

    if (!page_ok() || s == LINK_BUS_CLOSED) {
        /* nothing on the bus to show */
    } else if ((uint8_t)(sreg(LINK_BS_OP) & 0xFFu) == LINK_BUS_OP_SCAN
               && (b.use == U_SCAN || b.id < 0)) {
        const unsigned from = sreg(LINK_BS_RANGE) & 0xFFu;
        const unsigned to = sreg(LINK_BS_RANGE) >> 8;

        if (scanning() || page_busy()) {
            snprintf(b.lines[1], BUS_PANEL_LINE, TR(BS_L_SCANNING), from, to,
                     (unsigned)(sreg(LINK_BS_SCAN) & 0xFFu), found_n());
        } else {
            snprintf(b.lines[1], BUS_PANEL_LINE, TR(BS_L_SCANNED), from, to,
                     found_n(), (unsigned)(sreg(LINK_BS_AT) >> 8));
        }
    } else if (b.id >= 0
               && (sreg(LINK_BS_WATCH) & 0xFFu) == (unsigned)b.id) {
        uint8_t bytes[LINK_BS_BYTES];
        const unsigned n = sreg(LINK_BS_COUNTS) & 0xFFu;
        bus_live_t live;

        for (unsigned i = 0; i < n && i < sizeof(bytes); ++i) {
            const uint16_t r = sreg(LINK_BS_LIVE + i / 2u);

            bytes[i] = (uint8_t)(i % 2u == 0u ? r & 0xFFu : r >> 8);
        }
        /* Only the last read's block is live: a failed read shows none. */
        if (n > 0u && (sreg(LINK_BS_WATCH) >> 8) == 0u
            && p->live(series_now(), bytes, n, &live)) {
            const unsigned load = live.load > 1000u ? 1000u : live.load;
            int k = snprintf(b.lines[1], BUS_PANEL_LINE, TR(BS_L_LIVE),
                             (unsigned)b.id, (unsigned)live.position,
                             load / 10u, load % 10u, live.mv / 1000u,
                             (live.mv / 100u) % 10u, (unsigned)live.temp_c);

            if (live.has_current && k > 0 && k < BUS_PANEL_LINE) {
                snprintf(b.lines[1] + k, (size_t)(BUS_PANEL_LINE - k),
                         TR(BS_L_MA), (unsigned long)live.current_ma);
            }
            if (live.status == 0u) {
                snprintf(b.lines[2], BUS_PANEL_LINE, "%s",
                         TR(BS_L_FLAGS_NONE));
            } else {
                static const int16_t names[BUS_E_COUNT] = {
                    TX_BS_F_VOLTAGE, TX_BS_F_SENSOR, TX_BS_F_TEMPERATURE,
                    TX_BS_F_CURRENT, TX_BS_F_OVERLOAD,
                };
                int at = snprintf(b.lines[2], BUS_PANEL_LINE, "%s",
                                  TR(BS_L_FLAGS));

                for (unsigned e = 0; e < BUS_E_COUNT; ++e) {
                    if (p->error_bit[e] != 0u
                        && (live.status & p->error_bit[e]) != 0u
                        && at > 0 && at < BUS_PANEL_LINE) {
                        at += snprintf(b.lines[2] + at,
                                       (size_t)(BUS_PANEL_LINE - at), " %s",
                                       ui_tr((ui_text_id_t)names[e]));
                    }
                }
                b.line_warn |= 4u;
            }
        } else {
            snprintf(b.lines[1], BUS_PANEL_LINE, TR(BS_L_NO_READING),
                     (unsigned)b.id);
        }
    }
    snprintf(b.lines[3], BUS_PANEL_LINE, "%s", b.msg);
    if (b.msg_warn) {
        b.line_warn |= 8u;
    }
}

static uint32_t hash(uint32_t h, const void *p, size_t n)
{
    const uint8_t *c = (const uint8_t *)p;

    for (size_t i = 0; i < n; ++i) {
        h = (h ^ c[i]) * 16777619u;
    }
    return h;
}

void bus_panel_feed(const bus_panel_feed_t *f)
{
    if (f == NULL) {
        return;
    }
    if (!b.fed) {
        b.stops_seen = f->stops;
        b.answers_seen = f->rd.answers;
    }
    b.f = *f;
    b.fed = true;
    if (page_ok() && (bus_state() == LINK_BUS_CLOSED
                      || bus_state() == LINK_BUS_CLOSING)) {
        memset(b.torque_on, 0, sizeof(b.torque_on));  /* closing sends
                                                         torque off */
        b.tq_read_due = false;
        b.tq_off_due = false;
        ctl_forget();
    }
    if (f->stops != b.stops_seen) {
        b.stops_seen = f->stops;
        /* STOP: a command not sent goes, a restore and a goal with it, and
         * a warning's hold.  The control task stops the bus: a command
         * still on the link is dropped there, one the page runs ends, and
         * no answer of either is waited for.  An EEPROM write handed to
         * the control task may have reached the servo: its old value is
         * kept for RESTORE, but for an ID, whose servo may answer on
         * either. */
        const bool was = b.waiting;

        if (b.warn.open) {
            close_editors();
        }
        if (b.waiting && !b.out_due && b.use == U_EE_WRITE
            && b.ee_role != BUS_R_ID) {
            push_backup(false);
        }
        if (b.waiting && b.use == U_EE_WRITE) {
            ee_forget();
        }
        b.restoring = false;
        b.goal_due = false;
        b.out_due = false;
        b.waiting = false;
        b.tq_read_due = false;
        b.tq_off_due = false;
        memset(b.torque_on, 0, sizeof(b.torque_on));  /* the stop sent
                                                         torque off */
        if (was || bus_state() != LINK_BUS_CLOSED) {
            say(false, TR(BS_M_STOPPED));
        }
    }
    if (!b.shown && scanning() && b.taken) {
        preempt();              /* the page went: the CLOSE ends the scan */
    }
    if (b.waiting) {
        watch_answer();
    }
    b.answers_seen = f->rd.answers;
    if (!b.waiting) {
        reconcile();
    }
    make_lines();
    {
        uint32_t h = 2166136261u;
        const uint8_t s = (uint8_t)bus_state();

        h = hash(h, b.lines, sizeof(b.lines));
        h = hash(h, &s, 1u);
        h = hash(h, &b.waiting, sizeof(b.waiting));
        h = hash(h, &b.taken, sizeof(b.taken));
        h = hash(h, &b.f.rd.ids, sizeof(b.f.rd.ids));
        h = hash(h, &b.f.armed, sizeof(b.f.armed));
        if (h != b.sig) {
            b.sig = h;
            ++b.rev;
        }
    }
}

bool bus_panel_take(bus_link_cmd_t *out)
{
    if (!b.out_due || out == NULL) {
        return false;
    }
    *out = b.out;
    b.out_due = false;
    b.wait_ms = b.f.now_ms;
    return true;
}

uint32_t bus_panel_revision(void)
{
    return b.rev + b.kp.revision;
}

uint32_t bus_panel_hold_revision(void)
{
    return b.hold_rev;
}

/* ------------------------------------------------------------- for the tests */

const char *bus_panel_line(int i)
{
    return i >= 0 && i < 4 ? b.lines[i] : NULL;
}

static void row_value(const row_t *r, char *out, size_t n)
{
    const bus_proto_t *p = proto();
    bus_reg_t reg;

    switch (r->what) {
    case A_PROTOCOL:
        snprintf(out, n, "%s", p->name);
        break;
    case A_BAUD:
        snprintf(out, n, "%lu", (unsigned long)p->baud(baud_now()));
        break;
    case A_SERIES:
        snprintf(out, n, "%s", p->series_names[series_now()]);
        break;
    case A_BUS:
        snprintf(out, n, "%s", bus_state() == LINK_BUS_CLOSED
                                   ? TR(BS_OPEN) : TR(SUP_CLOSE));
        break;
    case A_FROM:
        snprintf(out, n, "%u", (unsigned)b.from);
        break;
    case A_TO:
        snprintf(out, n, "%u", (unsigned)b.to);
        break;
    case A_SCAN:
        snprintf(out, n, "%s", scanning() ? TR(BS_STOP) : TR(BS_SCAN));
        break;
    case A_ID:
        if (b.id >= 0) {
            snprintf(out, n, "%d", b.id);
        } else {
            snprintf(out, n, "--");
        }
        break;
    case A_TORQUE:
        snprintf(out, n, "%s", torque_of(b.id) ? TR(ON) : TR(OFF));
        break;
    case A_SPEED:
        snprintf(out, n, "%u", (unsigned)b.speed);
        break;
    case A_ACC:
        if (reg_of(BUS_R_ACC, &reg)) {
            snprintf(out, n, "%u", (unsigned)b.acc);
        } else {
            snprintf(out, n, "--");
        }
        break;
    case A_EEPROM:
        snprintf(out, n, "%s", TR(BS_CHANGE));
        break;
    default:
        out[0] = '\0';
        break;
    }
}

const char *bus_panel_button(int row, int c)
{
    static char s[32];
    const row_t *r = row_at(row, c);

    if (r == NULL) {
        return NULL;
    }
    row_value(r, s, sizeof(s));
    return s;
}

static const char *row_label(const row_t *r)
{
    if (r->what == A_SCAN) {
        static char s[32];

        snprintf(s, sizeof(s), TR(BS_FOUND), found_n());
        return s;
    }
    return ui_tr((ui_text_id_t)r->label);
}

const char *bus_panel_label(int row, int c)
{
    const row_t *r = row_at(row, c);

    return r != NULL ? row_label(r) : NULL;
}

const char *bus_panel_choice_label(int i)
{
    return b.ch.open && i >= 0 && i < b.ch.count ? b.ch.labels[i] : NULL;
}

const char *bus_panel_choice_title(void)
{
    return b.ch.open ? b.ch.title : NULL;
}

const char *bus_panel_warning_line(int i)
{
    return b.warn.open && i >= 0 && i < b.warn.count ? b.warn.lines[i]
                                                       : NULL;
}

unsigned bus_panel_backups(void)
{
    return b.bk_n;
}

int bus_panel_id(void)
{
    return b.id;
}

uint8_t bus_panel_series(void)
{
    return series_now();
}

/* ------------------------------------------------------------------ drawing */

static void draw_row(gfx_canvas_t *c, int i)
{
    const row_t *r = &k_rows[i];
    const gfx_rect_t rr = row_rect(r);
    const bool live = row_live(r);
    const bool pressed = b.have && b.pressed == P_ROW && b.row == i;
    gfx_color_t fill = col(UI_C_PANEL_SUNK);
    char v[32];

    gfx_text(c, rr.x, rr.y + 9, row_label(r), &gfx_font_8x16,
             live ? col(UI_C_TEXT_DIM) : col(UI_C_TEXT_FAINT), 1);
    row_value(r, v, sizeof(v));
    if ((r->what == A_BUS && bus_state() != LINK_BUS_CLOSED)
        || (r->what == A_TORQUE && torque_of(b.id))) {
        fill = col(UI_C_ACCENT);
    }
    ui_button(c, value_rect(r), v, fill, pressed, live);
}

static void draw_page(gfx_canvas_t *c)
{
    const gfx_rect_t a = b.area;
    const int top = a.y + ROW0;
    char l[32];

    /* Under the overlay's tabs: the screen draws them and CLOSE. */
    gfx_fill_rect(c, a.x + 1, top - 4, a.w - 2, a.h - ROW0 + 3,
                  col(UI_C_PANEL));
    for (int i = 0; i < ROW_COUNT; ++i) {
        draw_row(c, i);
    }
    snprintf(l, sizeof(l), TR(BS_POSITION), (unsigned)lroundf(b.pos.value));
    gfx_text(c, a.x + 10, top + SLIDER_ROW * PITCH + 9, l, &gfx_font_8x16,
             bus_open() && b.id >= 0 ? col(UI_C_TEXT_DIM)
                                     : col(UI_C_TEXT_FAINT), 1);
    b.pos.dim = !(bus_open() && b.id >= 0);
    ui_slider_render(&b.pos, c);
    for (int i = 0; i < 4; ++i) {
        gfx_color_t lc = col(UI_C_TEXT_DIM);

        if ((b.line_warn & (1u << i)) != 0u) {
            lc = col(UI_C_WARN);
        } else if (i == 0 && bus_state() == LINK_BUS_OPEN) {
            lc = col(UI_C_ACCENT);
        }
        gfx_text(c, a.x + 10, top + 7 * PITCH + i * LINE_H, b.lines[i],
                 &gfx_font_8x16, lc, 1);
    }
}

static void draw_choice(gfx_canvas_t *c)
{
    const gfx_rect_t a = b.area;

    gfx_text(c, a.x + 10, a.y + 16, b.ch.title, &gfx_font_8x16,
             col(UI_C_TEXT), 1);
    for (int i = 0; i < b.ch.count; ++i) {
        ui_button(c, bus_panel_choice_rect(i), b.ch.labels[i],
                  b.ch.values[i] == b.ch.current ? col(UI_C_ACCENT)
                                                 : col(UI_C_PANEL_SUNK),
                  b.have && b.pressed == P_CHOICE && b.row == i, true);
    }
    if (b.ch.kind == C_SERIES) {
        gfx_text(c, a.x + 10, a.y + 48 + 5 * 46 + 4, TR(BS_SERIES_NOTE),
                 &gfx_font_8x16, col(UI_C_TEXT_DIM), 1);
        gfx_text(c, a.x + 10, a.y + 48 + 5 * 46 + 4 + LINE_H,
                 TR(BS_SMS_NOTE), &gfx_font_8x16, col(UI_C_WARN), 1);
    }
    ui_button(c, bus_panel_choice_cancel_rect(), TR(CANCEL),
              col(UI_C_PANEL_SUNK),
              b.have && b.pressed == P_CHOICE_CANCEL, true);
}

static void draw_warning(gfx_canvas_t *c)
{
    const gfx_rect_t a = b.area;
    const gfx_color_t edge = col(UI_C_WARN);

    gfx_draw_rect(c, a.x, a.y, a.w, a.h, edge);
    gfx_draw_rect(c, a.x + 1, a.y + 1, a.w - 2, a.h - 2, edge);
    gfx_draw_rect(c, a.x + 2, a.y + 2, a.w - 4, a.h - 4, edge);
    gfx_text(c, a.x + 12, a.y + 20, b.warn.title, &gfx_font_8x16, edge, 2);
    for (int i = 0; i < b.warn.count; ++i) {
        gfx_text(c, a.x + 12, a.y + 72 + i * WARN_PITCH, b.warn.lines[i],
                 &gfx_font_8x16, i == 0 ? col(UI_C_TEXT) : col(UI_C_TEXT_DIM),
                 1);
    }
    ui_button(c, bus_panel_apply_rect(), b.warn.apply,
              ui_hold_fill(col(UI_C_PANEL_SUNK), edge, b.warn.hold.held_s),
              b.warn.down, true);
    ui_button(c, bus_panel_cancel_rect(), TR(CANCEL), col(UI_C_PANEL_SUNK),
              b.have && b.pressed == P_CANCEL, true);
}

void bus_panel_render(gfx_canvas_t *c)
{
    const gfx_rect_t a = b.area;

    if (b.kp.open) {
        ui_keypad_render(&b.kp, c);
        return;
    }
    if (b.warn.open) {
        gfx_fill_rect(c, a.x, a.y, a.w, a.h, col(UI_C_PANEL));
        draw_warning(c);
        return;
    }
    if (b.ch.open) {
        gfx_fill_rect(c, a.x, a.y, a.w, a.h, col(UI_C_PANEL));
        gfx_draw_rect(c, a.x, a.y, a.w, a.h, col(UI_C_ACCENT));
        draw_choice(c);
        return;
    }
    draw_page(c);
}
