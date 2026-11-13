/**
 * @file
 * @brief The Dial firmware's pins and the calls its parts make on each
 *        other.
 *
 * Pins from the M5Stack Dial v1.1's documentation, confirmed against the
 * board support package and M5GFX (RESEARCH-DIAL.md, "Pin map").  The
 * battery's reading pin is named by no source and is not used.
 *
 * Tasks: the input task (dial_input.c) polls the encoder, the buttons and
 * the touch controller every 5ms and sends EVENT; the face task
 * (dial_main.c) takes the panel's packets, draws and sends STATUS; NimBLE's
 * host task runs dial_ble.c's callbacks; the console's REPL (read-eval-
 * print loop) task runs dial_console.c.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dialknob.h"
#include "gfx.h"

/** @brief GPIO numbers. @{ */
#define DIAL_PIN_HOLD      46 /**< power latch: high keeps the Dial on */
#define DIAL_PIN_ENC_A     41 /**< encoder channel A */
#define DIAL_PIN_ENC_B     40 /**< encoder channel B */
#define DIAL_PIN_BTN       42 /**< the screen button, low when pressed */
#define DIAL_PIN_STAMP_BTN 0  /**< the Stamp-S3A's button, low when pressed;
                                   the download mode strap at reset */
#define DIAL_PIN_BUZZER    3  /**< buzzer, LEDC (LED controller) PWM */
#define DIAL_PIN_LCD_DC    4  /**< GC9A01 data/command */
#define DIAL_PIN_LCD_MOSI  5  /**< GC9A01 data */
#define DIAL_PIN_LCD_SCLK  6  /**< GC9A01 clock */
#define DIAL_PIN_LCD_CS    7  /**< GC9A01 chip select */
#define DIAL_PIN_LCD_RST   8  /**< GC9A01 reset, shared with the touch and
                                   RFID parts */
#define DIAL_PIN_LCD_BL    9  /**< backlight, LEDC PWM */
#define DIAL_PIN_I2C_SDA   11 /**< internal I2C: touch, RTC, RFID */
#define DIAL_PIN_I2C_SCL   12 /**< internal I2C clock */
#define DIAL_PIN_TOUCH_INT 14 /**< FT3267 interrupt, low on a touch */
#define DIAL_PIN_MARK      1  /**< PORT.B G1: the test hooks' 1ms mark */
/** @} */

/** @brief The display's size in pixels. */
#define DIAL_LCD_SIZE 240

/** @brief The input task's period, in ms. */
#define DIAL_INPUT_MS 5u
/** @brief The face task's period, in ms. */
#define DIAL_FACE_MS 50u

/* --- dial_power.c ---------------------------------------------------- */

/** @brief Drive GPIO46 high: the Dial stays on after the WAKE button is
 *         let go.  The first call of app_main(). */
void dial_power_hold(void);

/** @brief What the Dial knows of its supply. */
typedef struct {
    uint16_t mv;       /**< the cell's voltage, mV; 0 unknown */
    bool     known;    /**< mv is a reading */
    bool     charging; /**< the charger reports charging; false unknown */
    bool     usb;      /**< a USB host reads the console port */
} dial_supply_t;

/**
 * @brief Read the supply.  No source names the pin the cell's voltage
 *        reaches, so the reading is unknown unless a test build's console
 *        injected one (`batt <mV>`).
 * @param out the supply
 */
void dial_supply_read(dial_supply_t *out);

/**
 * @brief A test build's injected battery reading.
 * @param mv the cell's voltage, mV; 0 takes the injection back
 */
void dial_supply_inject(uint16_t mv);

/* --- dial_buzz.c ----------------------------------------------------- */

/** @brief Set up the buzzer's PWM, silent. */
void dial_buzz_init(void);
/** @brief A 3ms click, the sound of one detent. */
void dial_buzz_click(void);
/**
 * @brief A tone.
 * @param hz its frequency, 200 to 8,000Hz
 * @param ms its length, 1 to 1,000ms
 */
void dial_buzz_tone(uint32_t hz, uint32_t ms);

/* --- dial_lcd.c ------------------------------------------------------ */

/**
 * @brief Set up SPI, the GC9A01 and the backlight, and reset the display
 *        (and with it the touch controller on the shared reset line).
 * @return true when the display answered every step
 */
bool dial_lcd_init(void);
/**
 * @brief Send rows of a frame to the display.
 * @param fb  the frame, DIAL_LCD_SIZE x DIAL_LCD_SIZE RGB565 pixels
 * @param y0  the first row
 * @param y1  one past the last row
 */
void dial_lcd_flush(const uint16_t *fb, int y0, int y1);
/**
 * @brief Set the backlight.
 * @param pct 0 to 100%
 */
void dial_lcd_backlight(unsigned pct);

/* --- dial_touch.c ---------------------------------------------------- */

/** @brief Set up the internal I2C bus and the FT3267 at 0x38.
 *  @return true when the controller answered */
bool dial_touch_init(void);
/**
 * @brief Read one touch point.
 * @param x the column, 0 to 239, when touched
 * @param y the row, 0 to 239, when touched
 * @return true when a finger is on the glass
 */
bool dial_touch_read(uint8_t *x, uint8_t *y);

/* --- dial_input.c ---------------------------------------------------- */

/** @brief Set up the encoder, the buttons and the mark pin, and start the
 *         input task. */
void dial_input_start(void);

/** @brief Input a test build's console injects, as if the hardware did. */
typedef enum {
    DIAL_INJ_TURN = 1, /**< arg detents, one every 20ms */
    DIAL_INJ_PRESS,    /**< a press of the screen button */
    DIAL_INJ_HOLD,     /**< a hold of the screen button */
    DIAL_INJ_TAP,      /**< a tap on the glass at x, y */
    DIAL_INJ_PING,     /**< an EVENT at once */
} dial_inj_kind_t;

/**
 * @brief Queue an injected input for the input task.
 * @param kind dial_inj_kind_t
 * @param arg  TURN: detents, -1,000 to 1,000
 * @param x    TAP: column, 0 to 239
 * @param y    TAP: row, 0 to 239
 * @return false when the queue is full
 */
bool dial_input_inject(uint8_t kind, int32_t arg, uint8_t x, uint8_t y);

/** @brief Whether the Dial clicks per detent.  Default ON; the panel's
 *         SCREEN flag and the console's `sound` set it. @param on ON */
void dial_input_sound(bool on);
/** @brief Whether the Dial clicks per detent. @return ON */
bool dial_input_sound_on(void);

/** @brief What the input task last sent, for the console. */
typedef struct {
    uint8_t  seq;       /**< the last EVENT's seq */
    int16_t  counter;   /**< the detent counter */
    uint32_t sent;      /**< EVENTs handed to the stack */
    uint32_t unsent;    /**< EVENTs the stack refused or no link took */
} dial_input_stats_t;
/** @brief Copy the input task's counts. @param out the counts */
void dial_input_stats(dial_input_stats_t *out);

/* --- dial_ble.c ------------------------------------------------------ */

/** @brief Start NimBLE: the GATT server, the bound panel from NVS
 *         (non-volatile storage), the advertising. */
void dial_ble_start(void);
/** @brief Whether the panel is connected and subscribed to EVENT.
 *  @return true when an EVENT can go */
bool dial_ble_ready(void);
/**
 * @brief Notify an EVENT.
 * @param pkt the packet
 * @param len DK_LEN_EVENT
 * @return true when the packet was handed to the stack
 */
bool dial_ble_send_event(const uint8_t *pkt, size_t len);
/**
 * @brief Set the STATUS value and notify it when subscribed.
 * @param pkt the packet
 * @param len DK_LEN_STATUS
 */
void dial_ble_send_status(const uint8_t *pkt, size_t len);
/**
 * @brief Take one packet the panel wrote to SCREEN.
 * @param buf  DK_PKT_MAX bytes
 * @param len  its length
 * @return false when none waits
 */
bool dial_ble_take(uint8_t *buf, size_t *len);
/** @brief A turn happened: ask for latency 0 if the link idles at
 *         DK_LATENCY_IDLE. @param now_ms the time */
void dial_ble_turned(uint32_t now_ms);
/** @brief Ask for DK_LATENCY_IDLE after DK_IDLE_MS without a turn; called
 *         by the face task. @param now_ms the time */
void dial_ble_poll(uint32_t now_ms);
/** @brief Forget the bound panel: clear NVS and the accept list, end the
 *         link, advertise to anyone. */
void dial_ble_forget(void);
/** @brief End the link (a test hook): the panel sees a disconnect. */
void dial_ble_drop(void);
/** @brief Whether a panel is bound. @return true when bound */
bool dial_ble_bound(void);
/** @brief The connection count: it changes at every connect, so the face
 *         task resets its model. @return the count */
uint32_t dial_ble_link_count(void);
/** @brief The link's RSSI on the Dial. @return dBm, or DK_RSSI_NONE */
int8_t dial_ble_rssi(void);
/** @brief The advertised name, "DIAL-3A10". @return the name */
const char *dial_ble_name(void);
/**
 * @brief The Dial's and the bound panel's addresses as text.
 * @param own   20 bytes: "A0:B1:C2:7F:3A:10"
 * @param panel 20 bytes: the bound panel with " P" or " R", or ""
 */
void dial_ble_addresses(char *own, char *panel);
/** @brief The low 16 bits of the last STATE's panel ms, for EVENT's echo.
 *  @param ms the value */
void dial_ble_set_echo(uint16_t ms);
/** @brief The echo EVENT carries. @return the value */
uint16_t dial_ble_echo(void);

/* --- dial_console.c -------------------------------------------------- */

/** @brief Start the USB console's REPL with the `dial>` prompt. */
void dial_console_start(void);

/* --- dial_screen.c --------------------------------------------------- */

/** @brief What the placeholder face shows besides the panel's model. */
typedef struct {
    bool        lost;    /**< no panel packet for DK_DIAL_LOST_MS */
    bool        ready;   /**< connected and subscribed */
    bool        bound;   /**< a panel is bound */
    const char *name;    /**< the Dial's name */
} dial_view_t;

/**
 * @brief Draw the placeholder face: a plain frame, the panel screen's title
 *        and the value the knob acts on as text, or the link's state.
 * @param c the canvas, DIAL_LCD_SIZE square
 * @param f the panel's model
 * @param v the link's state
 */
void dial_screen_draw(gfx_canvas_t *c, const dk_face_t *f,
                      const dial_view_t *v);
