---
screen: none
title: Servo settings
order: 21
---

# Servo settings

SETTINGS on the [SERVO](servo.md) strip has three pages: OUTPUT, SWEEP and DUT (device under test). Each run has separate settings: open SETTINGS on its TEST card to see the list, then use CLOSE at the top right or the home tag to return. The output card stays visible and usable on the right. Tap a number for the keypad, a choice for its list, a switch to toggle it, or NAME for the keyboard.

## OUTPUT

![The OUTPUT page](view:servo-settings)

| Setting | Range | What it does |
|---|---|---|
| TYPE | STANDARD PWM, NARROW 760, WIDE, HELI CYCLIC, HELI TAIL 760 | selects the servo profile |
| FRAME RATE | the type's list, or CUSTOM on the keypad | sets how often a pulse is sent |
| PULSE MIN, CENTRE, MAX | 400 to 2500us; each end at least 50us from CENTRE | maps -90° to MIN, 0° to CENTRE and +90° to MAX |
| TRIM | -200 to +200us, 5us steps | adds to the centre; tap the value for a keypad with a `+/-` key |
| TRAVEL | 10 to 90° | limits horn movement in each direction |
| REVERSE | ON, OFF | reverses the mapping from angle to pulse |

| Type | Centre | Travel | Frame rates |
|---|---|---|---|
| STANDARD PWM | 1500us | 1000-2000us | 50, 60, 100, 150, 200, 250, 300, 333Hz |
| NARROW 760 | 760us | 660-860us | as STANDARD PWM |
| WIDE | 1500us | 800-2200us | as STANDARD PWM, up to 312Hz |
| HELI CYCLIC | 1520us | 820-2220us | 50, 120, 200, 333Hz |
| HELI TAIL 760 | 760us | 410-1110us | 200, 333, 560Hz |

A pulse needs a pause before the next frame: at least 1ms for STANDARD PWM, NARROW 760 and WIDE, 0.5ms for the heli types. A frame rate the pulses leave no pause for is refused.

The OUTPUT settings are kept for the session. Every restart goes back to STANDARD PWM at 50Hz.

![TRIM's keypad, -25us typed](view:servo-trim-keypad)

TRIM's keypad has a `+/-` key between the decimal point and OK. It adds or removes a leading minus sign. OK accepts -200 to +200us and rounds to the nearest 5us: 13 becomes 15, -12 becomes -10. Entering -201 leaves the keypad open and highlights the range in the warning colour.

## The warning for fast rates

![CAN DESTROY THE SERVO](view:servo-warning)

A heli type or frame rate above 60Hz can destroy a servo not designed for it. Selecting either opens a warning in the danger colour, naming the choice. Hold HOLD 2s TO APPLY to apply it. CANCEL, sliding your finger off, or leaving the screen discards the choice. An active hazardous type or rate stays red on the right card.

The OUTPUT page reports the rate status:

| Note | Meaning |
|---|---|
| `Active: OUTPUTS run at 330Hz.` | the IOmcu runs the rate on the target it holds |
| `The frame rate will be sent to the IOmcu with the next position command.` | not written yet |
| `REFUSED: ...` | a pulse is longer than the frame, or a pin shares a PWM slice at another rate; the pins keep their rate |
| `No link. The active frame rate is unknown.` | no IOmcu answers |

## SWEEP

![The SWEEP page](view:servo-sweep-page)

| Setting | Range | Default | What it does |
|---|---|---|---|
| CURVE | SQUARE, SINE, TRIANGLE | SINE | the curve of SWEEP and of the test's moves |
| SPEED | 0.05 to 5Hz | 0.5Hz | curves a second |
| RANGE | 10 to 100% | 80% | the share of the travel moved through |

## The automatic test's settings

![The AUTOMATIC TEST card's settings](view:servo-test-settings)

SETTINGS on the AUTOMATIC TEST card opens them.

| Setting | Range | Default | What it does |
|---|---|---|---|
| TEST LENGTH BY | TIME, MOVES | TIME | how each supply step is measured |
| TEST TIME | 5 to 3600s | 60s | the length of a step |
| MOVEMENTS | 1 to 1000 | 20 | the moves of a step |
| DWELL TIME | 0 to 5000ms | 200ms | held at each end, by the test and by SWEEP |
| SETTLING TIME | 0 to 10000ms | 500ms | waited after a supply step before measuring |
| STALL AT | 0.1 to 5.0A | 2.00A | above this current the servo counts as stalled |
| IDLE CURRENT | 0 to 5.00A | 0, not checked | fail above this at rest |
| HOLDING CURRENT | 0 to 5.00A | 0, not checked | fail above this holding an end |
| STEP 4.8V, STEP 6.0V | ON, OFF | ON | the supply steps |
| HV SERVO | ON, OFF | OFF | adds STEP 7.4V and STEP 8.4V; off at every restart |
| BROWN-OUT | ON 3.0/0.2V, OFF | ON | the brown-out walk: a tap opens its page |
| TRAVEL TIME | 0 to 5000ms | 0, not checked | fail above this from end to end |

![HV SERVO on](view:servo-hv)

With HV SERVO on and a step above 6.0V chosen, START TEST on the card opens HV SERVOS ONLY first, naming the highest step.

## The brown-out walk

![The brown-out walk's page](view:servo-brownout)

BROWN-OUT in the automatic test settings opens the walk's page; CLOSE returns. Its row summarises LOWEST VOLTAGE and STEP, for example `ON 3.0/0.2V`. After the supply steps, the walk starts at 5.0V or the voltage cap, whichever is lower. It decreases by STEP towards LOWEST VOLTAGE, stopping at the first voltage with no movement.

| Setting | Range | Default | What it does |
|---|---|---|---|
| BROWN-OUT | ON, OFF | ON | runs the walk after the supply steps |
| LOWEST VOLTAGE | 2.0 to 4.8V | 3.0V | the walk's last voltage; a supply whose lowest set point is higher ends it there |
| STEP | 0.1 to 1.0V | 0.2V | how far each voltage goes down; a last step shorter than STEP lands on LOWEST VOLTAGE |

The note below the rows describes the walk for the active supply and the number of voltages requested, at most 31. If LOWEST VOLTAGE is at or above the starting voltage, only the starting voltage is tested.

## The limit run's settings

![The LIMIT RUN card's settings](view:servo-limits)

SETTINGS on the LIMIT RUN card opens them.

| Setting | Range | Default | What it does |
|---|---|---|---|
| LIMIT RUN | SEARCH ONLY, SEARCH + PROGRAM | SEARCH ONLY | what START LIMIT RUN does: search the ends, or also program LEFT ANGLE and RIGHT ANGLE |
| SERVO | KST/CHASERVO, HITEC | the programmer opened last on PROGRAM, else KST/CHASERVO | shown with TARGET on PROG PORT only: the programmer the run's sessions go to; HITEC in the series of the Hitec programmer's SERIES row ([Hitec servos](servo.md#hitec-servos)) |
| SERVO PULSES | KEEP, 1000-2000 | KEEP | shown with TARGET on PROG PORT and SERVO at KST/CHASERVO only: 1000-2000 writes the servo's PULSE LOWER and PULSE UPPER to 1000.0 and 2000.0us before the search |

## SUPPLY's caps

The active voltage and current caps constrain set points and run steps. They appear below the TEST cards and on DUT in a button such as `CAPS 21.00V 5.00A: OPEN SUPPLY`. Tap it to open [SUPPLY](supply.md) > SETTINGS > LIMITS, where you can set them. The home tag returns. The button is grey while TARGET is locked, because leaving SERVO would switch the bench to SAFE.

OUTPUT's first note names the port SERVO TESTER is set to, `SERVO TESTER: PORT 1. Set it on OUTPUTS.` It takes no tap: the setting's home is [Outputs](outputs.md#the-prog-port-entry).

## DUT

![NAME on the keyboard](view:servo-name)
![The DUT page with the output encoder on](view:servo-encoder)

| Setting | What it does |
|---|---|
| NAME | the device under test, up to 23 characters; it heads each report |
| REPORT | a text report, `ATESTnnn.TXT`, beside each test's log, while User logs on SETUP > SD CARD is ON |
| ENC CENTRE | a tap takes the live AS5600 count, 0 to 4095, as the centre, with the servo at neutral; with ANGLE SOURCE IMU it takes nothing |

ANGLE SOURCE in the IMU entry of [Sensors](setup-sensors.md) selects the AS5600 or IMU for run angles. IMU angles use AS5600 counts of 0.088°. The test CSV identifies the source in its last column, `angle source`; both reports use an `Angle source:` line.

Enable or disable the AS5600 output encoder on [Sensors](setup-sensors.md). DUT's `AS5600 ON OR OFF: OPEN SENSORS` button opens that entry, and its home tag returns. PORT PWM, the programming-port pulse, is set only in the PROG PORT entry of [Outputs](outputs.md#the-prog-port-entry).

## The TARGET row

![The TARGET row with PROG PORT](view:servo-target)

TARGET on the output card identifies the driven outputs as `protocol pins`. Tap it to open [Outputs](outputs.md#the-prog-port-entry): PROG PORT when the IOmcu drives the programming port, SERVO PWM when it drives the outputs, or the overview when the target is unknown. There, SERVO TESTER selects OUTPUTS (pins bound as SERVO PWM) or PROG PORT (the IOmcu programming port).

Leaving SERVO switches the bench to SAFE and ends any sweep or test. The home tag on OUTPUTS returns to the same SERVO task. TARGET remains visible below the pages and programmer on every task. It is locked while ARMED, during a sweep, automatic test or limit run, or while the servo programmer holds the port.

![The PROG PORT entry of OUTPUTS](view:outputs-port)

| Row | Meaning |
|---|---|
| `SERVO PWM GPIO 4`, `SERVO PWM GPIO 2 GPIO 4` | the GPIOs (general-purpose inputs/outputs) bound as SERVO PWM; more than fit read `+2` |
| `PROG PORT 1 GPIO 13` | programming port 1 and its pin, in the warning colour |
| `PORT 1 GPIO 13 PWM OFF` | the port is the target and renders no pulse |
| `NO SERVO PWM PIN` | no pin is bound as SERVO PWM |
| `PINS NOT READ` | no binding has been read |
| `UNKNOWN` | the IOmcu's programming-port page is not read |
| `NO LINK` | no IOmcu answers; the tester sends nothing |

The row follows the target the IOmcu holds, not the setting. A TARGET change waits for a quiet bench.

## Keeping the values

OUTPUT, HV SERVO, LIMIT RUN, SERVO and SERVO PULSES are kept for the session. SWEEP, the automatic test's settings, DUT and TARGET are kept in NVS (non-volatile storage) and written as the SUPPLY settings are: on the next frame with the bench SAFE and the supply output off.
