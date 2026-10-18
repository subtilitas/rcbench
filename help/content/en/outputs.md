---
screen: OUTPUTS
title: Outputs
order: 62
---

# Outputs

OUTPUTS binds the IOmcu's pins to output protocols: which pins drive servos, which drive ESCs (electronic speed controllers), and with what signal. It opens from OUTPUTS on [SETUP](setup.md) and from TARGET on [MOTOR & ESC](motor.md#target), on the [overview](#the-overview-off): OFF selected and every bound pin named by its protocol. The home tag goes back to the screen it opened from. The protocol list and LAST WRITE take the left column; the IOmcu's pins take the right. [PICK A PIN](picker.md) holds the same binding on a drawing of the board.

## Binding pins

![Four pins bound as SERVO PWM](view:outputs)

1. Pick a protocol in the PROTOCOL list. Until then a tap on a pin does nothing.
2. Tap a pin to tick it. A tick binds the pin to the protocol at once; there is no APPLY key.
3. Tap a ticked pin to give it back.
4. Pick OFF to go back to the overview.

The count under the list, `4 OF 8 PINS`, says how many pins the protocol holds and how many it takes. A pin belongs to one protocol at most. Several protocols can be bound at once.

Each ticked pin is one slot of the IOmcu's OUTPUTS page, in pin order across every protocol. The lowest ticked pin is channel 0. There are 8 slots and 8 channels, shared by all protocols.

## The protocols

![The protocol list](view:outputs-protocol)

| Protocol | Signal | Pins |
|---|---|---|
| OFF | binds nothing; shows the whole binding | 0 |
| SERVO PWM | PWM (pulse-width modulation) at 50 Hz, for a servo or a control surface | up to 8 |
| MOTOR PWM | the same pulse at 50 Hz, for an ESC; from Idle pulse to Full pulse | up to 8 |
| PPM | PPM (pulse-position modulation): 8 channels on one pin at 40 Hz | 1 |
| DSHOT300, DSHOT600 | DShot digital throttle at 300 or 600 kbit/s | up to 8 |
| DSHOT300 BIDIR, DSHOT600 BIDIR | DShot with rpm telemetry from the ESC on the same wire | up to 8 |

A servo channel centres when nothing commands it; a motor channel stops. The pulse cannot say which is on the pin, so the protocol says it. The MOTOR & ESC throttle drives every channel bound as MOTOR PWM or a DShot entry, and leaves the servo channels alone. PPM needs 8 channels, so a bench with PPM bound has room for nothing else.

![The longest protocol names](view:outputs-bidir)

Picking a protocol writes nothing. It chooses which pins show as ticked and which protocol the next tick joins. The pins of the other protocols stay bound.

## Idle pulse and Full pulse

A MOTOR PWM channel sends 0 % throttle as Idle pulse and 100 % as Full pulse, 1000 us and 2000 us by default. Both are on [MOTOR & ESC, SETTINGS](motor-settings.md), in steps of 10 us: Idle pulse 800 to 1600 us, Full pulse 1400 to 2400 us. A SERVO PWM channel keeps 1000 to 2000 us, or the range SERVO sends. A change reaches the IOmcu 300 ms after the last edit while the bench is SAFE. An Idle pulse that is not below Full pulse is not sent, and the band says `Idle pulse must be shorter than full-throttle pulse. Not sent.`.

An ESC calibrated on a transmitter takes that transmitter's shortest pulse as zero. Set Idle pulse at or below that pulse and Full pulse at or above the longest one: for a transmitter that sends 985 to 2012 us, 980 us and 2020 us.

## What a pin cell shows

![DSHOT300 picked, with four pins held by SERVO PWM](view:outputs-held)

| Cell | Meaning | A tap |
|---|---|---|
| ticked, `PAD 6` | bound in the selected protocol; the pad number printed on the board | gives it back |
| greyed, the other protocol's name | held by another protocol | does nothing |
| red, struck through | reserved: GP3 carries the safety heartbeat, GP8 to GP12 the CAN (Controller Area Network) controller | does nothing |
| `PROG PORT` | the pin of the IOmcu's programming port, GP13 by default | does nothing |
| greyed, `SLICE OF GP13` | under SERVO PWM and MOTOR PWM: the pin shares a PWM slice with the programming port's pin; GP28 and GP13 are slice 6 | does nothing; `GP28: SHARES PWM SLICE WITH PROG PORT GP13` under the count |
| greyed, `PWM CH OF GP0` | under SERVO PWM and MOTOR PWM: the pin shares a PWM compare register with a pin bound as PWM (GP0 and GP16, GP1 and GP17, GP2 and GP18, GP4 and GP20, GP5 and GP21, GP6 and GP22) | does nothing; `GP16: SAME PWM CHANNEL AS GP0` under the count |
| free, `PAD 7` | not bound | binds it |

![PPM with servo pins already bound](view:outputs-full)

When the selected protocol can take no more pins, the reason stands under the count in amber: `NEEDS 8 CHANNELS, 4 FREE`, `ALL 8 SLOTS IN USE` or `SERVO PWM TAKES 8 PINS`.

![GP13 held by the programming port](view:outputs-prog)

A stored binding that holds GP13 keeps it: the programming port then has no pin, and the [servo programmer](servo-programmer.md) says so on CONNECT.

## The overview: OFF

![OFF shows the protocol of every bound pin](view:outputs-off)

Every visit to OUTPUTS opens with OFF selected. OFF binds nothing and unbinds nothing. Every bound pin shows the name of its protocol, so the whole binding and every pin it drives are on one screen. A tap on a pin does nothing, and the line under the list reads `CHOOSE A PROTOCOL, THEN TAP PINS`. Opening the screen writes nothing to the IOmcu. OFF stays selected until a protocol is picked, also across leaving the screen. A change of board, such as an IOmcu that answers during the visit or one swapped for another, selects OFF whatever was selected, so a protocol picked before is not carried to it. Pick a protocol to edit its pins; pick OFF to come back here. [PICK A PIN](picker.md) opens with OFF selected too.

## LAST WRITE

Every tick is written at once. The panel then reads the IOmcu's pages back and shows what it holds.

| LAST WRITE | Meaning |
|---|---|
| `NOT WRITTEN` | nothing written since the panel started |
| `WRITTEN` | the IOmcu acknowledged the write |
| `NO LINK` | an exchange got no answer within 1000 ms, or the link is down |
| `REFUSED` | the IOmcu refused: a pin it cannot bind, a change while ARMED, or a frame rate it could not put back |

An IOmcu of protocol 4.10 or later takes the binding whole: `NO LINK` and `REFUSED` leave the binding in force as it was.

## A binding that is not confirmed

![The last binding read, after a read that failed](view:outputs-unread)

The screen edits a binding only after reading it. While a page did not read, the screen keeps the last binding read, dimmed, with `ASSIGNMENT NOT READ. EDITING LOCKED.`. A tap on a pin does nothing, here and on PICK A PIN. The panel reads again at every link-up and every 500 ms.

![Pages no binding describes](view:outputs-odd)

When the pages read but describe no binding the screen knows, for example one pin in two slots, it reads `NO VALID ASSIGNMENT IN THE PAGES` and offers `HOLD: CLEAR ALL PIN ASSIGNMENTS`. Held for 2 s, it writes a binding with no pin, and edits are taken again. A finger that leaves the key, a lift before 2 s or leaving the screen abandons the hold.

## The IOmcu keeps the binding

The IOmcu stores the binding in its own flash and restores it at boot; the panel never stores one. A restored binding claims its pins and holds them at rest until the bench is switched to ARMED. A power cut during a save leaves the binding from before it.

## Related pages

- [Pin picker](picker.md): the same binding on the board.
- [Motor and ESC](motor.md#target): the pins the throttle drives.
- [Servo settings](servo-settings.md#the-target-row): the pins the servo tester drives.
