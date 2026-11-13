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
 * packet; a second one waits for the next tick.
 *
 * The encoder: quadrature x4 on PCNT with its glitch filter at 1,000ns,
 * channel A on GPIO41 and B on GPIO40, 4 counts a detent.  The detent
 * follows the count with hysteresis (dk_detent_follow()): it moves when the
 * count is 3 counts from the current detent's center, so a detent counts
 * at three quarters of its travel, and a jitter of 1 count at any rest
 * position never clicks.  Which
 * direction counts up, and where in its 4 counts the encoder rests, are
 * not measured: DIAL_ENC_SIGN holds the direction.
 *
 * The screen button: released before 1,000ms is PRESS, held for 1,000ms is
 * HOLD (sent once, while it is down), held for 10,000ms forgets the bound
 * panel.  A tap on the glass is a touch of at most 500ms that moves at most
 * 24px; it is TAP at the point it started.
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

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/** @brief +1 or -1: which way of the encoder counts up.  Not measured on
 *         the device: clockwise positive is the protocol's rule. */
#define DIAL_ENC_SIGN 1
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
    pcnt_unit_handle_t pcnt;     /**< the encoder's unit */
    int32_t   detent;            /**< the last detent read from PCNT */
    int16_t   counter;           /**< EVENT's counter, wrapping */
    button_t  screen;            /**< GPIO42 */
    button_t  stamp;             /**< GPIO0 */
    uint32_t  down_ms;           /**< when the screen button went down */
    bool      hold_sent;         /**< HOLD went for this press */
    bool      forgot;            /**< the bind was forgotten for it */
    bool      touch_down;        /**< a finger is on the glass */
    bool      touch_moved;       /**< it moved more than TAP_MOVE */
    uint8_t   tx;                /**< the touch point */
    uint8_t   ty;                /**< the touch point */
    uint8_t   tx0;               /**< where the touch started */
    uint8_t   ty0;               /**< where the touch started */
    uint32_t  touch_ms;          /**< when it started */
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

/** @brief The task's state; the task alone touches it. */
static input_t s_in;
/** @brief The counts the console reads. */
static dial_input_stats_t s_stats;
/** @brief Whether a detent clicks; the face and console tasks set it. */
static atomic_bool s_sound = true;

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
/** @brief The timer that ends the mark on GPIO1. */
static esp_timer_handle_t s_mark_off;
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

void dial_input_stats(dial_input_stats_t *out)
{
    *out = s_stats;
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
 *        link's latency back to 0.
 * @param d   detents
 * @param now the time
 */
static void turned(int32_t d, uint32_t now)
{
    s_in.counter = (int16_t)(uint16_t)((uint16_t)s_in.counter + (uint16_t)d);
    if (atomic_load(&s_sound)) {
        dial_buzz_click();
    }
    dial_ble_turned(now);
}

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

/** @brief The screen button's gestures: PRESS, HOLD and the forget.
 *  @param now the time */
static void screen_button(uint32_t now)
{
    if (button_poll(&s_in.screen, now)) {
        if (s_in.screen.down) {
            s_in.down_ms = now;
            s_in.hold_sent = false;
            s_in.forgot = false;
        } else if (!s_in.hold_sent) {
            gesture_push(DK_G_PRESS, 0, 0);
        }
    }
    if (!s_in.screen.down) {
        return;
    }
    if (!s_in.hold_sent && now - s_in.down_ms >= DK_HOLD_MS) {
        s_in.hold_sent = true;
        gesture_push(DK_G_HOLD, 0, 0);
    }
    if (!s_in.forgot && now - s_in.down_ms >= DK_FORGET_MS) {
        s_in.forgot = true;
        ESP_LOGI(TAG, "10s hold: the bound panel is forgotten");
        dial_ble_forget();
        dial_buzz_tone(1000, 200);
    }
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

#if RCBENCH_DIAL_HOOKS
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
    if (!dial_ble_ready() || dk_evt_encode(&e, pkt) == 0u) {
        ++s_stats.unsent;       /* no link: the gesture and turn go stale */
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

/** @brief The input task: every DIAL_INPUT_MS. @param arg unused */
static void input_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    (void)arg;
    for (;;) {
        const uint32_t now = now_ms();
        int count = 0;
        if (pcnt_unit_get_count(s_in.pcnt, &count) == ESP_OK) {
            const int32_t d = dk_detent_follow(s_in.detent,
                                               (int32_t)count * DIAL_ENC_SIGN);
            if (d != s_in.detent) {
                turned(d - s_in.detent, now);
                s_in.detent = d;
            }
        }
#if RCBENCH_DIAL_HOOKS
        inject_poll(now);
#endif
        screen_button(now);
        (void)button_poll(&s_in.stamp, now);
        touch_poll(now);
        send_poll(now);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(DIAL_INPUT_MS));
    }
}

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
