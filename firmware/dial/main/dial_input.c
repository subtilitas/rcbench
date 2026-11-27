/**
 * @file
 * @brief The input task: the encoder on PCNT (the pulse counter), the screen
 *        and Stamp buttons, the touch controller, the gestures, the click
 *        per detent, and the EVENT packets they make.
 *
 * Every 5ms (DIAL_INPUT_MS) the task reads the encoder's count, the buttons
 * and, every 20ms, the touch controller.  It sends an EVENT at once when
 * the detent counter, a button, the touch point or a gesture changed, and
 * every 200ms (DK_KEEPALIVE_MS) when nothing did.  One gesture goes in each
 * packet; a second one waits for the next tick.  While the Dial's side of
 * the link is lost, no panel packet for 500ms (DK_DIAL_LOST_MS) or none
 * since the connection, no EVENT goes, keepalives included.
 *
 * The encoder: quadrature x4 on PCNT with its glitch filter at 1,000ns,
 * channel A on GPIO41 and B on GPIO40, 4 counts a detent.  The detent
 * follows the count with hysteresis (dk_detent_follow()): it moves when the
 * count is 3 counts from the current detent's center, so a detent counts
 * at three quarters of its travel, and a jitter of 1 count at any rest
 * position never clicks.  The encoder's count falls clockwise on the
 * Dial v1.1; DIAL_ENC_SIGN turns it so that clockwise counts up.  Where in
 * its 4 counts the encoder rests is not measured.
 *
 * The screen button (shared/dial/dial_button.c): released before 1,000ms
 * is PRESS, held for 1,000ms is HOLD (sent once, while it is down).  Held
 * for 5,000ms (DIAL_OFF_MS) it beeps at 600Hz for 150ms and the face
 * blinks red; a release before 10,000ms opens the choice OFF / KEEP ON,
 * and OFF switches the Dial off (dial_power_off()).  Held for 10,000ms it
 * forgets the bound panel and the Dial stays on.  While the warning or
 * the choice is up, a turn moves the choice's focus, clicks, and leaves
 * EVENT's counter alone; no gesture of the screen button goes to the
 * panel.  A tap on the glass is a touch of at most 500ms that moves at
 * most 24px; it is TAP at the point it started.
 *
 * The Guition JC3636K518C (CONFIG_RCBENCH_DIAL_BOARD_K518) runs the same
 * task with its own sources (shared/dial/dk518_input.h): the encoder's two
 * pulse lines on a PCNT unit each, GPIO8 clockwise and GPIO7
 * anticlockwise, falling edges, the glitch filter at 1,000ns, decoded by
 * dk518_enc_step(); the CST816S read every 20ms (dial_k518_io.c); and the
 * still touch as the screen button (dk518_still_step()), which sends
 * DK_BTN_TOUCH alone, never DK_BTN_SCREEN or DK_BTN_STAMP.  The cues
 * vibrate: PRESS, HOLD, the warning, the forget, and a still lift while
 * an update waits for its accept.  It has no mark pin.
 *
 * A test build (RCBENCH_DIAL_HOOKS) takes input from the console as well:
 * an injected turn moves the counter one detent every 20ms, and the packet
 * of each injected detent raises GPIO1 (PORT.B G1) for 1ms when it is
 * handed to the BLE stack.  With no link no packet goes, and GPIO1 stays
 * low.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdatomic.h>
#include <string.h>

#include "dial.h"
#include "dial_button.h"
#include "devota.h"
#if CONFIG_RCBENCH_DIAL_BOARD_K518
#include "bleota.h"
#include "dk518_input.h"
#endif

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/** @brief +1 or -1: which way of the encoder counts up.  The count falls
 *         clockwise on the Dial v1.1, and clockwise positive is the
 *         protocol's rule. */
#define DIAL_ENC_SIGN (-1)
/** @brief PCNT's count limits; the driver accumulates past them. @{ */
#define ENC_LIMIT 1000
/** @} */
/** @brief The glitch filter, ns. */
#define ENC_GLITCH_NS 1000u
/** @brief A button's level must hold this long to count, ms. */
#define DEBOUNCE_MS 20u
/** @brief The touch controller's poll period, ms. */
#define TOUCH_MS 20u
/** @brief A tap: at most this long, ms, and this far, px. @{ */
#define TAP_MS   500u
#define TAP_MOVE 24 /**< the most a tap moves, px */
/** @} */
/** @brief An injected turn's period per detent, ms. */
#define INJ_STEP_MS 20u
/** @brief The mark's length on GPIO1, us. */
#define MARK_US 1000u
/** @brief Gestures waiting for a packet. */
#define GESTURE_QUEUE 4u
/** @brief The input task's stack, bytes. */
#define INPUT_STACK 4096u

/** @brief The log's tag. */
static const char *TAG = "input";

/** @brief A debounced button. */
typedef struct {
    int      pin;      /**< its GPIO, low when pressed */
    bool     down;     /**< the debounced state */
    bool     raw;      /**< the last level read, pressed */
    uint32_t raw_ms;   /**< when raw last changed */
} button_t;

/** @brief A gesture waiting for a packet. */
typedef struct {
    uint8_t gesture; /**< dk_gesture_t */
    uint8_t x;       /**< TAP: column */
    uint8_t y;       /**< TAP: row */
} pending_t;

/** @brief The input task's state. */
typedef struct {
#if CONFIG_RCBENCH_DIAL_BOARD_K518
    pcnt_unit_handle_t pcnt_cw;  /**< the clockwise line's unit */
    pcnt_unit_handle_t pcnt_ccw; /**< the anticlockwise line's unit */
    dk518_enc_t   enc;           /**< the two lines' decode */
    dk518_still_t still;         /**< the still touch: the screen button */
    dk518_key_t   key;           /**< the update's confirming key */
    uint16_t      nx;            /**< the touch point, native px */
    uint16_t      ny;            /**< the touch point, native px */
#else
    pcnt_unit_handle_t pcnt;     /**< the encoder's unit */
    int32_t   detent;            /**< the last detent read from PCNT */
    dial_button_t button;        /**< the screen button's gestures */
    bool      touch_moved;       /**< it moved more than TAP_MOVE */
    uint8_t   tx0;               /**< where the touch started */
    uint8_t   ty0;               /**< where the touch started */
    uint32_t  touch_ms;          /**< when it started */
#endif
    int16_t   counter;           /**< EVENT's counter, wrapping */
    button_t  screen;            /**< GPIO42; never down on the K518 */
    button_t  stamp;             /**< GPIO0; never down on the K518 */
    int32_t   local_turns;       /**< detents the switch-off's choice takes */
    bool      touch_down;        /**< a finger is on the glass */
    uint8_t   tx;                /**< the touch point */
    uint8_t   ty;                /**< the touch point */
    uint32_t  touch_poll_ms;     /**< the last touch read */
    pending_t queue[GESTURE_QUEUE]; /**< gestures waiting */
    unsigned  queued;            /**< how many */
    uint8_t   seq;               /**< the next EVENT's seq */
    bool      sent_any;          /**< an EVENT went since start */
    int16_t   sent_counter;      /**< the last EVENT's counter */
    uint8_t   sent_buttons;      /**< the last EVENT's buttons */
    uint8_t   sent_x;            /**< the last EVENT's touch x */
    uint8_t   sent_y;            /**< the last EVENT's touch y */
    uint32_t  sent_ms;           /**< when the last EVENT went */
    bool      force;             /**< send at the next tick */
#if RCBENCH_DIAL_HOOKS
    int32_t   inj_left;          /**< injected detents still to step */
    uint32_t  inj_next_ms;       /**< when the next one steps */
    bool      inj_mark;          /**< the next packet carries one */
#endif
} input_t;

#if CONFIG_RCBENCH_DIAL_BOARD_K518
/** @brief The screen button's gestures: the still touch's. */
#define BUTTON (&s_in.still.button)
/** @brief The update's key for the `ota` task: KEY_DOWN while
 *         dk518_key_down(), and the offer it belongs to in KEY_OFFER. */
static atomic_uint_least32_t s_key;
/** @brief s_key's fields. @{ */
#define KEY_DOWN  0x80000000u
#define KEY_OFFER 0x7FFFFFFFu /**< the offer's identity, low 31 bits */
/** @} */
#else
/** @brief The screen button's gestures. */
#define BUTTON (&s_in.button)
#endif

/** @brief The task's state; the task alone touches it. */
static input_t s_in;
/** @brief The counts the console reads. */
static dial_input_stats_t s_stats;
/** @brief The switch-off for the face: DIAL_OFF_* in bits 0-1, the
 *         blink's red half in bit 2, the focus on OFF in bit 3, the
 *         choice's seconds left in bits 8-15. */
static atomic_uint_least32_t s_off;
/** @brief Whether a detent clicks; the face and console tasks set it. */
static atomic_bool s_sound = true;
/** @brief The face task's view of the link: a panel packet came on this
 *         connection, and the last one's arrival, ms.  dk_link_lost() of
 *         them mutes the EVENTs. @{ */
static atomic_bool s_link_any;
static atomic_uint_least32_t s_link_ms; /**< the last panel packet, ms */
/** @} */

#if RCBENCH_DIAL_HOOKS
/** @brief An injected input on its way to the task. */
typedef struct {
    uint8_t kind; /**< dial_inj_kind_t */
    uint8_t x;    /**< TAP: column */
    uint8_t y;    /**< TAP: row */
    int32_t arg;  /**< TURN: detents */
} inject_t;
/** @brief The console's queue to the task. */
static QueueHandle_t s_inject;
#ifdef DIAL_PIN_MARK
/** @brief The timer that ends the mark on GPIO1. */
static esp_timer_handle_t s_mark_off;
#endif
#endif

/** @brief The ms clock, low 32 bits. @return ms since start */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void dial_input_sound(bool on)
{
    atomic_store(&s_sound, on);
}

bool dial_input_sound_on(void)
{
    return atomic_load(&s_sound);
}

void dial_input_link(bool any, uint32_t last_ms)
{
    /* The time first: a reader that sees any sees the time with it. */
    atomic_store(&s_link_ms, last_ms);
    atomic_store(&s_link_any, any);
}

void dial_input_stats(dial_input_stats_t *out)
{
    *out = s_stats;
}

void dial_input_off(dial_face_view_t *v)
{
    const uint32_t w = (uint32_t)atomic_load(&s_off);
    v->off = (uint8_t)(w & 3u);
    v->off_lit = (w & 4u) != 0u;
    v->off_focus = (w & 8u) != 0u;
    v->off_secs = (uint8_t)(w >> 8);
}

/**
 * @brief Queue a gesture for a packet; a full queue drops it.
 * @param g the gesture
 * @param x TAP: column
 * @param y TAP: row
 */
static void gesture_push(uint8_t g, uint8_t x, uint8_t y)
{
    if (s_in.queued < GESTURE_QUEUE) {
        s_in.queue[s_in.queued].gesture = g;
        s_in.queue[s_in.queued].x = x;
        s_in.queue[s_in.queued].y = y;
        ++s_in.queued;
    }
}

/**
 * @brief Take the oldest waiting gesture.
 * @param out the gesture; DK_G_NONE when none waits
 */
static void gesture_pop(pending_t *out)
{
    memset(out, 0, sizeof(*out));
    if (s_in.queued > 0u) {
        *out = s_in.queue[0];
        memmove(&s_in.queue[0], &s_in.queue[1],
                (s_in.queued - 1u) * sizeof(s_in.queue[0]));
        --s_in.queued;
    }
}

/**
 * @brief Move the counter by detents that turned, with a click and the
 *        link's latency back to 0; while the switch-off's warning or
 *        choice is up, the detents go to the choice instead.
 * @param d   detents
 * @param now the time
 */
static void turned(int32_t d, uint32_t now)
{
    if (atomic_load(&s_sound)) {
        dial_buzz_click();
    }
    if (dial_button_local(BUTTON)) {
        s_in.local_turns += d;
        return;
    }
    s_in.counter = (int16_t)(uint16_t)((uint16_t)s_in.counter + (uint16_t)d);
    dial_ble_turned(now);
}

#if !CONFIG_RCBENCH_DIAL_BOARD_K518
/**
 * @brief Debounce one button.
 * @param b   the button
 * @param now the time
 * @return true when its debounced state changed
 */
static bool button_poll(button_t *b, uint32_t now)
{
    const bool raw = gpio_get_level(b->pin) == 0;
    if (raw != b->raw) {
        b->raw = raw;
        b->raw_ms = now;
    }
    if (b->down != b->raw && now - b->raw_ms >= DEBOUNCE_MS) {
        b->down = b->raw;
        return true;
    }
    return false;
}
#endif

/** @brief The screen button's actions, and the switch-off's state for
 *         the face.  @param act DIAL_BTN_* bits  @param now the time */
static void button_acts(unsigned act, uint32_t now)
{
    dial_face_view_t v;
    if ((act & DIAL_BTN_PRESS) != 0u) {
        gesture_push(DK_G_PRESS, 0, 0);
        dial_buzz_cue(DIAL_CUE_PRESS);
    }
    if ((act & DIAL_BTN_HOLD) != 0u) {
        gesture_push(DK_G_HOLD, 0, 0);
        dial_buzz_cue(DIAL_CUE_HOLD);
    }
    if ((act & DIAL_BTN_WARN) != 0u) {
        ESP_LOGI(TAG, "5s hold: release to switch off, hold on to unbind");
        dial_buzz_cue(DIAL_CUE_WARN);
    }
    if ((act & DIAL_BTN_FORGET) != 0u) {
        ESP_LOGI(TAG, "10s hold: the bound panel is forgotten");
        dial_ble_forget();
        dial_buzz_cue(DIAL_CUE_FORGET);
    }
    if ((act & DIAL_BTN_KEEP) != 0u) {
        ESP_LOGI(TAG, "switch-off: KEEP ON");
    }
    if ((act & DIAL_BTN_OFF) != 0u) {
        ESP_LOGI(TAG, "switch-off: OFF");
        dial_power_off();
    }
    dial_button_view(BUTTON, now, &v);
    atomic_store(&s_off, (uint_least32_t)v.off | (v.off_lit ? 4u : 0u)
                             | (v.off_focus ? 8u : 0u)
                             | ((uint32_t)v.off_secs << 8));
}

#if CONFIG_RCBENCH_DIAL_BOARD_K518
/** @brief The touch, read every TOUCH_MS, stepped every DIAL_INPUT_MS as
 *         the still touch: the screen button, the TAP, and the update's
 *         accept.  @param now the time */
static void screen_button(uint32_t now)
{
    dk518_still_out_t o;
    if (now - s_in.touch_poll_ms >= TOUCH_MS) {
        s_in.touch_poll_ms = now;
        s_in.touch_down = dial_k518_touch(&s_in.nx, &s_in.ny);
        if (s_in.touch_down) {
            s_in.tx = dk518_touch_scale(s_in.nx);
            s_in.ty = dk518_touch_scale(s_in.ny);
        }
    }
    /* The update's offer reads the touch itself (dial_ble.c,
     * dial_input_update_key()): its press must not answer a hidden
     * choice. */
    const bool yield = devota_active();
    const unsigned act = dk518_still_step(&s_in.still, s_in.touch_down,
                                          s_in.nx, s_in.ny,
                                          s_in.local_turns, yield, now, &o);
    s_in.local_turns = 0;
    /* The update takes the still touch's events, never the raw touch:
     * a swipe, or a touch from before the offer, must not answer it. */
    uint32_t id = 0u;
    const bool open = devota_offer(&id);
    if (dk518_key_step(&s_in.key, open, id, &o, now)) {
        dial_buzz_cue(DIAL_CUE_ACCEPT);
    }
    /* The key and its offer in one word, so the `ota` task never pairs
     * one offer's key with another's identity. */
    atomic_store(&s_key, (dk518_key_down(&s_in.key, now) ? KEY_DOWN : 0u)
                             | (dk518_key_offer(&s_in.key) & KEY_OFFER));
    if (o.tap) {
        gesture_push(DK_G_TAP, o.tap_x, o.tap_y);
    }
    button_acts(act, now);
}
#else
/** @brief The screen button: PRESS and HOLD for the panel, the
 *         switch-off's warning and choice, the forget.  @param now the
 *         time */
static void screen_button(uint32_t now)
{
    unsigned act = 0u;
    (void)button_poll(&s_in.screen, now);
    if (devota_active()) {
        /* The update's offer reads the screen button itself
         * (dial_ble.c): its press must not answer a hidden choice. */
        dial_button_yield(&s_in.button, s_in.screen.down, now);
    } else {
        act = dial_button_step(&s_in.button, s_in.screen.down,
                               s_in.local_turns, now);
    }
    s_in.local_turns = 0;
    button_acts(act, now);
}

/** @brief Read the touch controller every TOUCH_MS; a short touch that
 *         stays put is a TAP.  @param now the time */
static void touch_poll(uint32_t now)
{
    uint8_t x = 0;
    uint8_t y = 0;
    bool down;
    if (now - s_in.touch_poll_ms < TOUCH_MS) {
        return;
    }
    s_in.touch_poll_ms = now;
    down = dial_touch_read(&x, &y);
    if (down && !s_in.touch_down) {
        s_in.touch_down = true;
        s_in.touch_moved = false;
        s_in.touch_ms = now;
        s_in.tx0 = x;
        s_in.ty0 = y;
    } else if (!down && s_in.touch_down) {
        s_in.touch_down = false;
        if (!s_in.touch_moved && now - s_in.touch_ms <= TAP_MS) {
            gesture_push(DK_G_TAP, s_in.tx0, s_in.ty0);
        }
    }
    if (down) {
        if (x - s_in.tx0 > TAP_MOVE || s_in.tx0 - x > TAP_MOVE
            || y - s_in.ty0 > TAP_MOVE || s_in.ty0 - y > TAP_MOVE) {
            s_in.touch_moved = true;
        }
        s_in.tx = x;
        s_in.ty = y;
    }
}
#endif

#if RCBENCH_DIAL_HOOKS
#ifdef DIAL_PIN_MARK
/** @brief End the mark on GPIO1. @param arg unused */
static void mark_off(void *arg)
{
    (void)arg;
    gpio_set_level(DIAL_PIN_MARK, 0);
}

/** @brief Raise GPIO1 for MARK_US: a packet of an injected detent went. */
static void mark(void)
{
    gpio_set_level(DIAL_PIN_MARK, 1);
    esp_timer_stop(s_mark_off);
    esp_timer_start_once(s_mark_off, MARK_US);
}
#else
/** @brief The K518 has no mark pin: GPIO1 is its cell's divider. */
static void mark(void)
{
}
#endif

bool dial_input_inject(uint8_t kind, int32_t arg, uint8_t x, uint8_t y)
{
    const inject_t i = { kind, x, y, arg };
    return s_inject != NULL && xQueueSend(s_inject, &i, 0) == pdTRUE;
}

/** @brief Take the console's injections: turns step one detent every
 *         INJ_STEP_MS.  @param now the time */
static void inject_poll(uint32_t now)
{
    inject_t i;
    if (s_in.inj_left == 0 && xQueueReceive(s_inject, &i, 0) == pdTRUE) {
        switch (i.kind) {
        case DIAL_INJ_TURN:
            s_in.inj_left = i.arg;
            s_in.inj_next_ms = now;
            break;
        case DIAL_INJ_PRESS:
            gesture_push(DK_G_PRESS, 0, 0);
            break;
        case DIAL_INJ_HOLD:
            gesture_push(DK_G_HOLD, 0, 0);
            break;
        case DIAL_INJ_TAP:
            gesture_push(DK_G_TAP, i.x, i.y);
            break;
        default:
            s_in.force = true;
            break;
        }
    }
    if (s_in.inj_left != 0 && (int32_t)(now - s_in.inj_next_ms) >= 0) {
        const int32_t d = s_in.inj_left > 0 ? 1 : -1;
        s_in.inj_left -= d;
        s_in.inj_next_ms = now + INJ_STEP_MS;
        s_in.inj_mark = true;
        turned(d, now);
    }
}
#endif

/** @brief Build and send an EVENT when something changed or the keepalive
 *         is due.  @param now the time */
static void send_poll(uint32_t now)
{
    dk_evt_t e;
    pending_t g;
    uint8_t pkt[DK_LEN_EVENT];
    bool changed;
    e.buttons = (uint8_t)((s_in.screen.down ? DK_BTN_SCREEN : 0u)
                          | (s_in.touch_down ? DK_BTN_TOUCH : 0u)
                          | (s_in.stamp.down ? DK_BTN_STAMP : 0u));
    e.touch_x = s_in.touch_down ? s_in.tx : 0u;
    e.touch_y = s_in.touch_down ? s_in.ty : 0u;
    changed = !s_in.sent_any || s_in.counter != s_in.sent_counter
              || e.buttons != s_in.sent_buttons || e.touch_x != s_in.sent_x
              || e.touch_y != s_in.sent_y || s_in.queued > 0u || s_in.force;
    if (!changed && now - s_in.sent_ms < DK_KEEPALIVE_MS) {
        return;
    }
    gesture_pop(&g);
    if (g.gesture == DK_G_TAP) {
        e.touch_x = g.x;
        e.touch_y = g.y;
    }
    e.seq = s_in.seq;
    e.counter = s_in.counter;
    e.gesture = g.gesture;
    e.ms = (uint16_t)now;
    e.echo = dial_ble_echo();
    s_in.sent_any = true;
    s_in.sent_counter = s_in.counter;
    s_in.sent_buttons = e.buttons;
    s_in.sent_x = e.touch_x;
    s_in.sent_y = e.touch_y;
    s_in.sent_ms = now;
    s_in.force = false;
    const bool any = atomic_load(&s_link_any);
    const bool lost = dk_link_lost(any, (uint32_t)atomic_load(&s_link_ms),
                                   now);
    if (!dial_ble_ready() || lost || dk_evt_encode(&e, pkt) == 0u) {
        /* No link, or LINK LOST: no EVENT and no keepalive, so the panel's
         * clock runs out as the Dial's did.  The gesture and the turn go
         * stale; the panel takes the next EVENT, more than 300ms later, as
         * a new reference (DK_STALE_MS). */
        ++s_stats.unsent;
        s_in.queued = 0u;
#if RCBENCH_DIAL_HOOKS
        s_in.inj_mark = false;
#endif
        return;
    }
    ++s_in.seq;
    if (dial_ble_send_event(pkt, sizeof pkt)) {
        ++s_stats.sent;
#if RCBENCH_DIAL_HOOKS
        if (s_in.inj_mark) {
            mark();
        }
#endif
    } else {
        ++s_stats.unsent;
    }
#if RCBENCH_DIAL_HOOKS
    s_in.inj_mark = false;
#endif
    s_stats.seq = e.seq;
    s_stats.counter = e.counter;
}

#if CONFIG_RCBENCH_DIAL_BOARD_K518
/** @brief Read both lines' counts and turn by the detents they give.
 *  @param now the time */
static void encoder_poll(uint32_t now)
{
    int cw = 0;
    int ccw = 0;
    if (pcnt_unit_get_count(s_in.pcnt_cw, &cw) == ESP_OK
        && pcnt_unit_get_count(s_in.pcnt_ccw, &ccw) == ESP_OK) {
        const int32_t d = dk518_enc_step(&s_in.enc, cw, ccw);
        if (d != 0) {
            turned(d, now);
        }
    }
}
#else
/** @brief Read the count and turn when the detent moved.
 *  @param now the time */
static void encoder_poll(uint32_t now)
{
    int count = 0;
    if (pcnt_unit_get_count(s_in.pcnt, &count) == ESP_OK) {
        const int32_t d = dk_detent_follow(s_in.detent,
                                           (int32_t)count * DIAL_ENC_SIGN);
        if (d != s_in.detent) {
            turned(d - s_in.detent, now);
            s_in.detent = d;
        }
    }
}
#endif

/** @brief The input task: every DIAL_INPUT_MS. @param arg unused */
static void input_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    (void)arg;
    for (;;) {
        const uint32_t now = now_ms();
        encoder_poll(now);
#if RCBENCH_DIAL_HOOKS
        inject_poll(now);
#endif
        screen_button(now);
#if !CONFIG_RCBENCH_DIAL_BOARD_K518
        (void)button_poll(&s_in.stamp, now);
        touch_poll(now);
#endif
        send_poll(now);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(DIAL_INPUT_MS));
    }
}

#if CONFIG_RCBENCH_DIAL_BOARD_K518
/**
 * @brief One line's PCNT unit: its falling edges counted up, the glitch
 *        filter at ENC_GLITCH_NS, the pull-up on.
 * @param pin  the line
 * @param unit its unit
 * @return true when the unit runs
 */
static bool line_init(int pin, pcnt_unit_handle_t *unit)
{
    const pcnt_unit_config_t cfg = {
        .low_limit = -ENC_LIMIT,
        .high_limit = ENC_LIMIT,
        .flags.accum_count = 1,
    };
    const pcnt_glitch_filter_config_t filter = {
        .max_glitch_ns = ENC_GLITCH_NS,
    };
    const pcnt_chan_config_t ch = {
        .edge_gpio_num = pin,
        .level_gpio_num = -1,
    };
    pcnt_channel_handle_t c = NULL;
    if (pcnt_new_unit(&cfg, unit) != ESP_OK
        || pcnt_unit_set_glitch_filter(*unit, &filter) != ESP_OK
        || pcnt_new_channel(*unit, &ch, &c) != ESP_OK
        || pcnt_channel_set_edge_action(c, PCNT_CHANNEL_EDGE_ACTION_HOLD,
                                        PCNT_CHANNEL_EDGE_ACTION_INCREASE)
               != ESP_OK) {
        return false;
    }
    gpio_pullup_en(pin);
    return pcnt_unit_add_watch_point(*unit, ENC_LIMIT) == ESP_OK
           && pcnt_unit_enable(*unit) == ESP_OK
           && pcnt_unit_clear_count(*unit) == ESP_OK
           && pcnt_unit_start(*unit) == ESP_OK;
}

/** @brief Set up a PCNT unit for each of the encoder's lines.
 *  @return true when both run */
static bool encoder_init(void)
{
    return line_init(DIAL_PIN_ENC_CW, &s_in.pcnt_cw)
           && line_init(DIAL_PIN_ENC_CCW, &s_in.pcnt_ccw);
}

void dial_input_start(void)
{
    memset(&s_in, 0, sizeof s_in);
    dk518_enc_init(&s_in.enc);
    dk518_still_init(&s_in.still);
    dk518_key_init(&s_in.key);
    if (!encoder_init()) {
        ESP_LOGE(TAG, "the encoder's PCNT units did not start");
    }
#if RCBENCH_DIAL_HOOKS
    s_inject = xQueueCreate(8, sizeof(inject_t));
#endif
    xTaskCreate(input_task, "input", INPUT_STACK, NULL, 5, NULL);
}

bool dial_input_update_key(void)
{
    return (atomic_load(&s_key) & KEY_DOWN) != 0u;
}

bool dial_input_offer_key(uint32_t *offer)
{
    const uint32_t w = (uint32_t)atomic_load(&s_key);
    *offer = w & KEY_OFFER;
    return (w & KEY_DOWN) != 0u;
}
#else
/** @brief Set up PCNT for the encoder, x4 with the glitch filter.
 *  @return true when the unit runs */
static bool encoder_init(void)
{
    const pcnt_unit_config_t unit = {
        .low_limit = -ENC_LIMIT,
        .high_limit = ENC_LIMIT,
        .flags.accum_count = 1,
    };
    const pcnt_glitch_filter_config_t filter = {
        .max_glitch_ns = ENC_GLITCH_NS,
    };
    const pcnt_chan_config_t a = {
        .edge_gpio_num = DIAL_PIN_ENC_A,
        .level_gpio_num = DIAL_PIN_ENC_B,
    };
    const pcnt_chan_config_t b = {
        .edge_gpio_num = DIAL_PIN_ENC_B,
        .level_gpio_num = DIAL_PIN_ENC_A,
    };
    pcnt_channel_handle_t ca = NULL;
    pcnt_channel_handle_t cb = NULL;
    if (pcnt_new_unit(&unit, &s_in.pcnt) != ESP_OK
        || pcnt_unit_set_glitch_filter(s_in.pcnt, &filter) != ESP_OK
        || pcnt_new_channel(s_in.pcnt, &a, &ca) != ESP_OK
        || pcnt_new_channel(s_in.pcnt, &b, &cb) != ESP_OK) {
        return false;
    }
    pcnt_channel_set_edge_action(ca, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                 PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(ca, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                  PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(cb, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                 PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(cb, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                  PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    gpio_pullup_en(DIAL_PIN_ENC_A);
    gpio_pullup_en(DIAL_PIN_ENC_B);
    return pcnt_unit_add_watch_point(s_in.pcnt, ENC_LIMIT) == ESP_OK
           && pcnt_unit_add_watch_point(s_in.pcnt, -ENC_LIMIT) == ESP_OK
           && pcnt_unit_enable(s_in.pcnt) == ESP_OK
           && pcnt_unit_clear_count(s_in.pcnt) == ESP_OK
           && pcnt_unit_start(s_in.pcnt) == ESP_OK;
}

void dial_input_start(void)
{
    const gpio_config_t buttons = {
        .pin_bit_mask = (1ULL << DIAL_PIN_BTN) | (1ULL << DIAL_PIN_STAMP_BTN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    memset(&s_in, 0, sizeof s_in);
    dial_button_init(&s_in.button);
    s_in.screen.pin = DIAL_PIN_BTN;
    s_in.stamp.pin = DIAL_PIN_STAMP_BTN;
    gpio_config(&buttons);
    if (!encoder_init()) {
        ESP_LOGE(TAG, "the encoder's PCNT unit did not start");
    }
#if RCBENCH_DIAL_HOOKS
    {
        const gpio_config_t mark_pin = {
            .pin_bit_mask = 1ULL << DIAL_PIN_MARK,
            .mode = GPIO_MODE_OUTPUT,
        };
        const esp_timer_create_args_t off = {
            .callback = mark_off,
            .name = "mark",
        };
        gpio_set_level(DIAL_PIN_MARK, 0);
        gpio_config(&mark_pin);
        esp_timer_create(&off, &s_mark_off);
        s_inject = xQueueCreate(8, sizeof(inject_t));
    }
#endif
    xTaskCreate(input_task, "input", INPUT_STACK, NULL, 5, NULL);
}

bool dial_input_update_key(void)
{
    return gpio_get_level(DIAL_PIN_BTN) == 0;
}
#endif
