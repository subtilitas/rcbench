---
screen: none
title: Servo settings
order: 21
---

# Servo settings

SETTINGS, the third tab of [SERVO](servo.md)'s strip, shows the servo's settings in the page area on three pages: OUTPUT, SWEEP and DUT (device under test). Each run's own settings open from its card on TEST: the card's SETTINGS shows them as a list one level down, and the home tag goes back to the cards. The output card on the right stays in place and works. A value opens the keypad, a list opens a list, a switch flips on the tap, and NAME opens a keyboard.

## OUTPUT

![The OUTPUT page](view:servo-settings)

| Setting | Range | What it does |
|---|---|---|
| TYPE | STANDARD PWM, NARROW 760, WIDE, HELI CYCLIC, HELI TAIL 760 | the servo profile |
| FRAME RATE | the type's list, or CUSTOM on the keypad | how often a pulse is sent |
| PULSE MIN, CENTRE, MAX | 400 to 2500 us; each end at least 50 us from CENTRE | -90 deg is MIN, 0 is CENTRE, +90 deg is MAX |
| TRIM | -200 to +200 us, 5 us a step | added to the centre |
| TRAVEL | 10 to 90 deg | how far the horn may go each way |
| REVERSE | ON, OFF | the direction the angle maps onto the pulse |

| Type | Centre | Travel | Frame rates |
|---|---|---|---|
| STANDARD PWM | 1500 us | 1000-2000 us | 50, 60, 100, 150, 200, 250, 300, 333 Hz |
| NARROW 760 | 760 us | 660-860 us | as STANDARD PWM |
| WIDE | 1500 us | 800-2200 us | as STANDARD PWM, up to 312 Hz |
| HELI CYCLIC | 1520 us | 820-2220 us | 50, 120, 200, 333 Hz |
| HELI TAIL 760 | 760 us | 410-1110 us | 200, 333, 560 Hz |

A pulse needs a pause before the next frame: at least 1 ms for STANDARD PWM, NARROW 760 and WIDE, 0.5 ms for the heli types. A frame rate the pulses leave no pause for is refused.

The OUTPUT settings are kept for the session. Every restart goes back to STANDARD PWM at 50 Hz.

## The warning for fast rates

![CAN DESTROY THE SERVO](view:servo-warning)

A heli type, or any frame rate above 60 Hz, can destroy a servo not made for it. Choosing one opens a warning in the danger colour that names the choice. It is applied after HOLD TO APPLY is held for 2 s. CANCEL, a finger that slides off and leaving the screen apply nothing. The type and rate in force stay red on the right card.

The OUTPUT page says what became of the rate:

| Note | Meaning |
|---|---|
| `Active: OUTPUTS run at 330 Hz.` | the coprocessor runs the rate on the target it holds |
| `The frame rate will be sent to the coprocessor with the next position command.` | not written yet |
| `REFUSED: ...` | a pulse is longer than the frame, or a pin shares a PWM slice at another rate; the pins keep their rate |
| `No link. The active frame rate is unknown.` | no coprocessor answers |

## SWEEP

![The SWEEP page](view:servo-sweep-page)

| Setting | Range | Default | What it does |
|---|---|---|---|
| CURVE | SQUARE, SINE, TRIANGLE | SINE | the curve of SWEEP and of the test's moves |
| SPEED | 0.05 to 5 Hz | 0.5 Hz | curves a second |
| RANGE | 10 to 100 % | 80 % | the share of the travel moved through |

## The automatic test's settings

![The AUTOMATIC TEST card's settings](view:servo-test-settings)

SETTINGS on the AUTOMATIC TEST card opens them.

| Setting | Range | Default | What it does |
|---|---|---|---|
| TEST LENGTH BY | TIME, MOVES | TIME | how each supply step is measured |
| TEST TIME | 5 to 3600 s | 60 s | the length of a step |
| MOVEMENTS | 1 to 1000 | 20 | the moves of a step |
| DWELL TIME | 0 to 5000 ms | 200 ms | held at each end, by the test and by SWEEP |
| SETTLING TIME | 0 to 10000 ms | 500 ms | waited after a supply step before measuring |
| STALL AT | 0.1 to 5.0 A | 2.00 A | above this current the servo counts as stalled |
| IDLE CURRENT | 0 to 5.00 A | 0, not checked | fail above this at rest |
| HOLD CURRENT | 0 to 5.00 A | 0, not checked | fail above this holding an end |
| STEP 4.8 V, STEP 6.0 V | ON, OFF | ON | the supply steps |
| HV SERVO | ON, OFF | OFF | adds STEP 7.4 V and STEP 8.4 V; off at every restart |
| BROWN-OUT | ON, OFF | ON | from 5.0 V down until the servo stops moving |
| TRAVEL TIME | 0 to 5000 ms | 0, not checked | fail above this from end to end |

![HV SERVO on](view:servo-hv)

With HV SERVO on and a step above 6.0 V chosen, START TEST on the card opens HV SERVOS ONLY first, naming the highest step.

## The limit run's settings

![The LIMIT RUN card's settings](view:servo-limits)

SETTINGS on the LIMIT RUN card opens them.

| Setting | Range | Default | What it does |
|---|---|---|---|
| LIMIT RUN | SEARCH ONLY, SEARCH + PROGRAM | SEARCH ONLY | what START LIMIT RUN does: search the ends, or also program LEFT ANGLE and RIGHT ANGLE |
| SERVO PULSES | KEEP, 1000-2000 | KEEP | shown with TARGET on PROG PORT only: 1000-2000 writes the servo's PULSE LOWER and PULSE UPPER to 1000.0 and 2000.0 us before the search |

## SUPPLY's caps

The voltage and current caps the set points and a run's steps stay inside show read-only under TEST's cards and on DUT: `Caps 21.00 V 5.00 A: SUPPLY > SETTINGS`, the caps in force. They are set on [SUPPLY](supply.md) > SETTINGS.

## DUT

![NAME on the keyboard](view:servo-name)
![The DUT page with the output encoder on](view:servo-encoder)

| Setting | What it does |
|---|---|
| NAME | the device under test, up to 23 characters; it heads each report |
| REPORT | a text report, `SERVOnnn.TXT`, beside each test's log |
| ENC CENTRE | a tap takes the live count, 0 to 4095, as the centre, with the servo at neutral |
| PORT PWM | ON or OFF: the pulse on the programming port while it is the target |

AS5600, the output encoder, is switched on [Interfaces](setup-interfaces.md) alone.

## The TARGET row

![The TARGET row with PROG PORT](view:servo-target)

The TARGET row of the output card names what the tester drives now as `protocol pins`, and picks it: a tap opens OUTPUTS, the pins bound as SERVO PWM, or PROG PORT, the coprocessor's programming port. It stays in sight on every task, under the pages and under the servo programmer. It is locked while ARMED, during a sweep, an automatic test or a limit run, and while the servo programmer holds the port.

![The choice of target](view:servo-target-choice)

| Row | Meaning |
|---|---|
| `SERVO PWM GP4`, `SERVO PWM GP2 GP4` | the GPIOs (general-purpose inputs/outputs) bound as SERVO PWM; more than fit read `+2` |
| `PROG PORT GP13` | the programming port's pin, in the warning colour |
| `PROG PORT GP13 PWM OFF` | the port is the target and renders no pulse |
| `NO SERVO PWM PIN` | no pin is bound as SERVO PWM |
| `PINS NOT READ` | no binding has been read |
| `UNKNOWN` | the coprocessor's programming-port page is not read |
| `NO LINK` | no coprocessor answers; the tester sends nothing |

The row follows the target the coprocessor holds, not the setting. A TARGET change waits for a quiet bench.

## Keeping the values

OUTPUT, HV SERVO, LIMIT RUN and SERVO PULSES are kept for the session. SWEEP, the automatic test's settings, DUT and TARGET are kept in NVS (non-volatile storage) and written as the SUPPLY settings are: on the next frame with the bench SAFE and the supply output off.
