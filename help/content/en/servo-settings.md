---
screen: none
title: Servo settings
order: 21
---

# Servo settings

SETTINGS in the footer of a [SERVO](servo.md) screen opens that screen's list. The hub's list is the servo profile every function uses: OUTPUT and DUT (device under test); LIVE's SETTINGS opens the same list. SWEEP, TEST, RUDDER, RUDDER AUTO, LIMIT RUN and HITEC have lists of their own, KST its EXPERT MODE page. A tap on the type pill of LIVE, SWEEP and RUDDER opens the profile in the list's place with TYPE in view; a tap on the RANGE row of LIMIT RUN's and RUDDER AUTO's READY cards, or on OUTPUT in TEST's list, opens it with PULSE MIN in view. Use CLOSE at the top right of a run's page or the home tag to return. A function's list opens on a SAFE bench only, and a bench that goes live closes it and any page of rows; the hub's opens at any time. No list opens while a programmer session holds its port, or while a finger is still on another control. A change on a list stays marked unsaved until SAVE in the strip. Tap a number for the keypad, a choice for its list, a switch to toggle it, or NAME for the keyboard.

## OUTPUT, on the hub's list

![The hub's list](view:servo-settings)

| Setting | Range | What it does |
|---|---|---|
| TYPE | STANDARD PWM, NARROW 760, WIDE, HELI CYCLIC, HELI TAIL 760 | selects the servo profile |
| FRAME RATE | the type's list, or CUSTOM on the keypad | sets how often a pulse is sent |
| PULSE MIN, CENTER, MAX | 400 to 2500us; each end at least 50us from CENTER | maps -90° to MIN, 0° to CENTER and +90° to MAX |
| TRIM | -200 to +200us, 5us steps | adds to the center; tap the value for a keypad with a `+/-` key |
| TRAVEL | 10 to 90° | limits horn movement in each direction |
| REVERSE | ON, OFF | reverses the mapping from angle to pulse |

| Type | Center | Travel | Frame rates |
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

OUTPUT's heading line reports the rate status, in the warning colour while the rate has not reached the pins:

| Note | Meaning |
|---|---|
| `Active: OUTPUTS run at 330Hz.` | the IOmcu runs the rate on the target it holds |
| `The frame rate will be sent to the IOmcu with the next position command.` | not written yet |
| `REFUSED: ...` | a pulse is longer than the frame, or a pin shares a PWM slice at another rate; the pins keep their rate |
| `No link. The active frame rate is unknown.` | no IOmcu answers |

## SWEEP's list

![SWEEP's list](view:servo-sweep-page)

| Setting | Range | Default | What it does |
|---|---|---|---|
| CURVE | SQUARE, SINE, TRIANGLE | SINE | the curve of SWEEP and of the test's moves |
| SPEED | 0.05 to 5Hz | 0.5Hz | curves a second |
| RANGE | 10 to 100% | 80% | the share of the travel moved through; the automatic test's Range |
| Dwell time | 0 to 5000ms | 200ms | held at each end; the automatic test's Dwell time |

## The automatic test's settings

![TEST's SETTINGS list](view:servo-test-settings)

SETTINGS in TEST's footer, or a tap on RANGE on TEST's READY card, opens the automatic test's list, on a SAFE bench only. It has 21 rows under six headings; each heading's line names what its rows give, RANGE's the run's ends, `1100-1900us`. A change is kept by SAVE in the strip; leaving with a change asks SAVE or DISCARD. RESET TEST puts every row to its default and HV SERVO off. The list closes as the bench arms or a run starts. Range and Dwell time are SWEEP's too, DUT name and Report the hub's: one setting each, whichever list sets it.

| Heading | Setting | Range | Default | What it does |
|---|---|---|---|---|
| VOLTAGE STEPS | Step 4.8V, Step 6.0V | ON, OFF | ON | the supply steps |
| VOLTAGE STEPS | HV SERVO | ON, OFF | OFF | adds Step 7.4V and Step 8.4V; off at every restart |
| VOLTAGE STEPS | Step 7.4V, Step 8.4V | ON, OFF | ON | the HV steps; a press with HV SERVO on only |
| BROWN-OUT | Brown-out | ON, OFF | ON | runs the walk after the supply steps |
| BROWN-OUT | Brown-out minimum voltage | 2.0 to 4.8V | 3.0V | the walk's last voltage; a supply whose lowest set point is higher ends it there |
| BROWN-OUT | Brown-out step | 0.1 to 1.0V | 0.2V | how far each voltage goes down; a last step shorter than this lands on the minimum |
| PASS/FAIL LIMITS | Stall threshold | 0.1 to 5.0A | 2.00A | above this current the servo counts as stalled |
| PASS/FAIL LIMITS | Idle current max | 0 to 5.00A | 0, not checked | fail above this at rest |
| PASS/FAIL LIMITS | Holding current max | 0 to 5.00A | 0, not checked | fail above this holding an end |
| PASS/FAIL LIMITS | Travel time max | 0 to 5000ms | 0, not checked | fail above this from end to end |
| LENGTH AND TIMING | Length by | TIME, MOVES | TIME | how each supply step is measured |
| LENGTH AND TIMING | Test time | 5 to 3600s | 60s | the length of a step; a press with Length by TIME only |
| LENGTH AND TIMING | Movements | 1 to 1000 | 20 | the moves of a step; a press with Length by MOVES only |
| LENGTH AND TIMING | Dwell time | 0 to 5000ms | 200ms | held at each end, by the test and by SWEEP |
| LENGTH AND TIMING | Settling time | 0 to 10000ms | 500ms | waited after a supply step before measuring |
| RANGE | Range | 10 to 100% | 80% | the share of the travel the moves cover, either side of PULSE CENTER |
| RANGE | OUTPUT | the profile | STANDARD PWM 50Hz 1000-2000 | the type, frame rate and pulse range in force; a tap opens the profile |
| DUT | DUT name | up to 23 characters | SERVO | heads each report |
| DUT | Report | ON, OFF | ON | a text report beside each test's log |

![HV SERVO on](view:servo-hv)

With HV SERVO on and a step above 6.0V chosen, HOLD 2s: ARM + START is a tap that opens HV SERVOS ONLY first, naming the highest step; its HOLD 2s TO APPLY arms and starts.

## The brown-out walk

![The list at BROWN-OUT](view:servo-brownout)

After the supply steps, the walk starts at 5.0V or the voltage cap, whichever is lower. It decreases by the step towards the minimum voltage, stopping at the first voltage with no movement. The BROWN-OUT heading's line describes the walk for the active supply and the number of voltages, at most 31: `From 5.00V down in 0.20V steps to 3.30V, 10 voltages`. If the minimum is at or above the starting voltage, only the starting voltage is tested.

## The limit run's settings

![LIMIT RUN's list on the bench's outputs](view:servo-limits)

SETTINGS in LIMIT RUN's footer opens the list. Off a PROG PORT, MODE, SERVO and SERVO PULSES are locked with `On a PROG PORT only: choose one with PORT`.

| Setting | Range | Default | What it does |
|---|---|---|---|
| MODE | SEARCH ONLY, SEARCH + PROGRAM | SEARCH ONLY | what ARM + START does: search the ends, or also program LEFT ANGLE and RIGHT ANGLE; a tap switches it |
| SERVO | KST/CHASERVO, HITEC D SERIES, HITEC CLASSIC | KST/CHASERVO | PORT on PROG PORT only: the programmer and series the run's sessions go to ([Hitec servos](servo.md#hitec-servos)); a tap opens the three. LIMIT RUN's own, apart from RUDDER AUTO's; the same as the READY card's SERVO row |
| Hitec D receive | PULL-UP, PRECHARGE | PULL-UP | shown under SERVO at HITEC D SERIES only: how the IOmcu receives a D series answer; the HITEC page's setting |
| SERVO PULSES | KEEP, 1000-2000 | KEEP | PORT on PROG PORT and SERVO at KST/CHASERVO only: 1000-2000 writes the servo's PULSE LOWER and PULSE UPPER to 1000.0 and 2000.0us before the search |
| CURRENT | SUPPLY's caps | SUPPLY's SET A | the run's current limit, SUPPLY's SET A, where a PD mini feeds the servo: a tap opens SUPPLY's keypad. Without a PD mini `--`, locked: the servo's own supply limits the current |
| Wait before PWM | 0 to 10000ms | 100ms | from the end of programming to the first pulse |
| DUT name | up to 23 characters | SERVO | the hub's NAME; heads each report |
| User logs | ON, OFF | ON | SETUP > SD CARD's: OFF, a run writes no LIMITnnn.TXT |

The list has no voltage: a run programs at 5.0V ([The supply: 5.0V](servo.md#the-supply-5-0v)). RUDDER AUTO's list ends with the same DUT name and User logs. The READY cards of both hold ten rows, the most they have room for, so User logs is a list row there and a card row on TEST only.

## SUPPLY's caps

The active voltage and current caps constrain set points and run steps. They appear below TEST's card in a button such as `CAPS 21.00V 5.00A: OPEN SUPPLY`. Tap it to open [SUPPLY](supply.md) > SETTINGS > LIMITS, where you can set them. The home tag returns. The button is grey while the PORT button is locked, because leaving SERVO would switch the bench to SAFE.

## DUT, on the hub's list

![NAME on the keyboard](view:servo-name)

| Setting | What it does |
|---|---|
| NAME | the device under test, up to 23 characters; it heads each report |
| REPORT | a text report, `ATESTnnn.TXT`, beside each test's log, while User logs on SETUP > SD CARD is ON |

## RUDDER's list and KST's page

![RUDDER's list with the output encoder on](view:servo-encoder)
![KST EXPERT MODE ON](view:servo-dut-expert)

| Setting | Where | What it does |
|---|---|---|
| ENC CENTER | RUDDER's list | a tap takes the live AS5600 count, 0 to 4095, as the center, with the servo at neutral; with ANGLE SOURCE IMU it takes nothing |
| ANGLE SOURCE | RUDDER's and RUDDER AUTO's lists | a tap opens the [ANGLE SOURCE list](servo-rudder.md#the-angle-source): the AS5600, the IMUs found and SEARCH FOR IMU |
| KST EXPERT MODE | SETTINGS on SERVO > KST | whether the [KST and Chaservo programmer](servo-programmer.md#kst-expert-mode) shows DEAD BAND, BOOST, STRETCH, RELEASE PAIRING and the LOCKED tab; OFF by default, ON after a 2s hold, OFF with a tap |

ANGLE SOURCE selects the AS5600 or IMU for run angles. IMU angles use AS5600 counts of 0.088°. The test CSV identifies the source in its last column, `angle source`; both reports use an `Angle source:` line.

Enable or disable the AS5600 output encoder on [Sensors](setup-sensors.md). PORT PWM, the programming-port pulse, is set only in the PROG PORT entry of [Outputs](outputs.md#the-prog-port-entry).

## The PORT button

![The PORT button with PROG PORT](view:servo-target)
![PORT's list](view:servo-port)

The PORT button over the output card identifies the driven outputs as `protocol pins`; the footer's port line repeats it. Tap it for the list of OUTPUTS (pins bound as SERVO PWM) and PROG PORT 1 to PROG PORT 4. A pick sets SERVO TESTER, as the PROG PORT entry of [Outputs](outputs.md#the-prog-port-entry) does, and is saved. On the rbmod carrier a port the carrier refuses shows why and takes no pick. Off the carrier with no pin bound as SERVO PWM, the first entry reads `OUTPUTS: NO PIN, OPEN OUTPUTS`: a pick sets OUTPUTS and opens [Outputs](outputs.md) on the SERVO PWM protocol to bind one. The home tag there returns to the SERVO screen.

The button is grey and locked while ARMED or ARM is asked, during a sweep, automatic test or limit run, or while the servo programmer holds the port; a tap names the lock in the alert band. It stays in view beside the lists, pages and programmer.

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

The button follows the target the IOmcu holds, not the setting. A change of SERVO TESTER waits for a quiet bench.

## Keeping the values

OUTPUT, HV SERVO, LIMIT RUN's MODE and SERVO PULSES are kept for the session. LIMIT RUN's and RUDDER AUTO's SERVO are written at the pick. SWEEP, the automatic test's settings, DUT, KST EXPERT MODE included, and TARGET are kept in NVS (non-volatile storage) and written as the SUPPLY settings are: on the next frame with the bench SAFE and the supply output off. A change on a list is written by SAVE in its strip.
