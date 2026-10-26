# Servo programmer for Hitec servos

<sub>**English** · [Deutsch](HitecProgrammer-de.md)</sub>

The programmer reads the settings of a Hitec servo over its signal wire and
shows them, decoded and as stored. It reads only: it writes nothing to the
servo, keeps no journal and makes no backup. It opens from its row
`Hitec servos` on **PROGRAM** of the [SERVO screen](Screens.md#servo), one
level down, and covers the left column, the strip's tabs with it. **CLOSE**
or the home tag returns to PROGRAM's list while the programmer holds no
port. The row is dimmed with `IOMCU HAS NO SERVO PWM` and opens nothing while
the IOmcu, the RP2350 board, reports no servo PWM (pulse-width modulation),
which includes no IOmcu answering.

Hitec is a name or trademark of its owner. rcbench is independent: it is
not affiliated with, endorsed by or sponsored by it. The name says which
servos the programmer works with.
[Legal notice and disclaimer](../README.md#legal-notice-and-disclaimer) holds
the project's notice.

Two series are read:

| Series | Servos | Wire | Read |
| --- | --- | --- | --- |
| D SERIES | D485HW and the other D series servos | 115,200bit/s, inverted | 21 registers |
| CLASSIC | HS-5xxx and HS-7xxx | 19,200bit/s | 100 cells: 0x00 to 0x23, 0x30 to 0x53, the name 0x60 to 0x6F, the date 0x70 to 0x73, the serial 0x80 to 0x87, and the version |

The register model and its encodings come from 1 D485HW (2026-10-20) and
from 1 HS-5645MG, 1 HS-7955TG and 1 HS-5955TG (2026-10-18 to 2026-10-20)
([`protocols/hitec`](../protocols/hitec/README.md)). The page it runs on is
[The programming port for Hitec servos](LinkHitec.md).

![The programmer with the tester holding the port](img/servo-hitec.png)

## Constraints

- The IOmcu runs protocol 4.23 or later. With an older one the SERVO tab
  says `Unavailable: the IOmcu has no Hitec port.` and
  `Update the IOmcu to version 0.21.1 or later.`
- SERVO TESTER on SETUP > OUTPUTS is set to a port, PORT 1, PORT 2,
  PORT 3 or PORT 4, and
  the port names a pin. With OUTPUTS the PORT row reads `NO PROG PORT SET`
  and CONNECT says `Use the programming port to program a servo.`
- The bench is SAFE. CONNECT is refused on an ARMED bench, on one asked to
  arm and with a finger on ARM: `Tap DISARM first. The port cannot be taken
  over while ARMED.` A latched STOP refuses it too:
  `A STOP is latched. It clears with the next switch to ARMED.`
- A classic servo enters programming mode only at power-up, so the bench
  cycles its supply: CONNECT for CLASSIC needs a PD mini that answers,
  otherwise `Classic servos need a PD mini: the bench cycles the supply.`
- A D series servo answers on the line as SETUP > INTERFACES > Hitec D
  receive sets it, PULL-UP or PRECHARGE
  ([Receive mode](LinkHitec.md#receive-mode)). CONNECT waits until the
  IOmcu holds that mode: `Receive mode not set on the IOmcu yet. Try
  again.`
- The programming voltage is the setting the KST programmer uses
  (SET_SERVO_PROG_V, MORE > PROGRAMMING VOLTAGE), 4.8V to 6.0V for a Hitec
  session: a higher setting runs at 6.0V and the SERVO tab says
  `Hitec programming runs at 6.0V or less.` The current limit is 50mA to
  3000mA. Both have to lie inside the SUPPLY limits, or CONNECT says
  `Programming voltage or current exceeds the SUPPLY limits.`

## The SERVO tab

| Row | What it does |
| --- | --- |
| PORT | Two lines: the port and its GPIO with the series, `PROG PORT 1 GPIO 13  D SERIES`, then the receive mode and the supply CONNECT sets, `RX PRECHARGE  5.0V 1000mA` (`SUPPLY AS IT IS` for a D series servo with no PD mini). The button reads CONNECT, ABORT while the port moves into programming or a read runs, WAIT, LEAVE while the port is held, and RELEASE PORT while another session holds it |
| SERIES | D SERIES or CLASSIC, chosen before CONNECT; kept with the SERVO settings (SET_HITEC_SERIES). Not taken while the port is held |
| SERVO | The model read, `D485HW` or `HS-5645MG`; READ ALL reads the series' set again |
| MORE | The programming voltage and current limit, each on the keypad, while the tester holds the port |
| AFTER STOP | CLEAR after a switch to SAFE at the bench: the IOmcu clears the port's STOPPED flag and the read goes on, or the entry again where the supply went off |

Under the rows the state line says what the port does now, and the last
message stands at the foot of the notes. Every control is 48px tall or more
and ends 4px above the alert band; the geometry is the KST programmer's
([Screens](Screens.md#programmer)).

![A D485HW read](img/servo-hitec-read.png)
![The SERIES list](img/servo-hitec-series.png)
![A read under way](img/servo-hitec-reading.png)

## CONNECT

1. OPEN: the IOmcu takes the port from PWM. No pulse for 50ms, then the line
   is the wire driver's, released.
2. The entry:
   - D series: POWER at the programming voltage with an off time of 0ms
     where a PD mini answers, then 1000ms for the servo to start. Without a
     PD mini the servo runs on the supply as it is.
   - Classic: POWER with the supply off for 500ms, then on, and a LISTEN
     for the servo's `+`; the servo's second `+` follows 250ms to 267ms
     later; 0x67 then reads the version.
3. READ ALL: the series' set, each address with 3 tries.
4. The model is identified: D series by register 0x00 (34485 is the
   D485HW), classic by the name cells. The message names it:
   `D series servo, model 34485, ID 0.` or
   `Classic servo HS-5645MG, version 0x16.` A model the register model does
   not name is read in the layout of the closest one, and the SERVO tab
   says which: `Model not known. The fields are read as on the D485HW.`

In the host model (`test_hitec_prog`) a D series CONNECT takes 2.4s, 1.2s of
it the entry, and a classic one 4.2s at a frame of 16ms and 7.6s at one of
33ms: a classic read takes 2 frames a cell. On the bench these times are
not measured.

While the port is held:

- ARM is drawn disabled and refused: `Cannot switch to ARMED: a port is in
  use by the servo programmer.` DISARM and STOP work.
- The set points and OUTPUT ON of the SET row and of the SUPPLY screen take
  no press; a press on them says `Servo programming holds the supply: set
  points and ON are locked.` OUTPUT OFF works. From protocol 4.29 the IOmcu
  holds its SUPPLY page from OPEN to CLOSE as well
  ([The supply while a port is held](LinkHitec.md#the-supply-while-a-port-is-held)).
- The KST and Chaservo row and MOTOR & ESC's TARGET row take no press, and
  ESC STICK does not start a run.

## The value tabs

MOTION, TRAVEL, PROTECT and INFO show what the last full read found, in the
series' rows. A value that fits its encoding shows decoded with the stored
value in brackets beside it, `CW (0)`; one that fits no known encoding
shows as `RAW 1234` in the warning colour. No row takes a tap.

| Tab | D series | Classic |
| --- | --- | --- |
| MOTION | SPEED, DEAD BAND, SOFT START | SPEED, DEAD BAND, RESOLUTION |
| TRAVEL | NEUTRAL, LEFT END, RIGHT END in servo counts with degrees (220° over 16383 counts, 8192 the centre), DIRECTION | CENTRE in microseconds, LEFT END, RIGHT END as raw values (unit unknown), DIRECTION |
| PROTECT | FAILSAFE (OFF, LIMP, or a position in microseconds), OVERLOAD PROTECTION, SMART SENSOR, SENSITIVITY (raw) | FAILSAFE, FAILSAFE POSITION, OVERLOAD PROTECTION |
| INFO | MODEL, ID, POSITION, and registers 0x04, 0x22, 0x8A, 0x8C, 0xD4 and 0xD6 raw | MODEL, VERSION, DATE, SERIAL, CHECKSUM |

A classic image whose cells 0x00 to 0x1C do not sum to 0 modulo 256 shows
CHECKSUM `WRONG` and the note `Checksum of cells 0x00-0x1C is wrong. The servo
may not follow PWM.`

![TRAVEL of a D485HW](img/servo-hitec-travel.png)
![MOTION of a D485HW](img/servo-hitec-motion.png)
![PROTECT of a D485HW](img/servo-hitec-protect.png)
![INFO of a D485HW](img/servo-hitec-info.png)
![An HS-5645MG read](img/servo-hitec-classic.png)
![TRAVEL of an HS-5645MG](img/servo-hitec-classic-travel.png)
![PROTECT of an HS-5645MG](img/servo-hitec-classic-protect.png)
![INFO of an HS-5645MG](img/servo-hitec-classic-info.png)

## LEAVE

CLOSE, the home tag or LEAVE on the PORT row opens LEAVE PROGRAMMING; a
hold of 2s on `HOLD 2s TO LEAVE` leaves:

- D series: a reboot of the servo (register 0x46 = 1), which then ignores
  signals for 1s; the supply stays as it is.
- Classic: the supply off for 500ms and on again.

Then CLOSE: the port is back in PWM and the message says
`The port is back in tester mode.` LEAVE is taken on a stopped port, an
ARMED bench and with STOP latched: the reboot is refused by a stopped port,
and the CLOSE is always taken.

![LEAVE PROGRAMMING, D series](img/servo-hitec-leave.png)
![LEAVE PROGRAMMING, classic](img/servo-hitec-classic-leave.png)

## STOP, SAFE and a lost link

| Event | The port | What the operator does |
| --- | --- | --- |
| STOP, pressed or the bench's own | The IOmcu cuts the transaction after the frame on the wire, keeps the port with the pin released and sets STOPPED. The panel latches the stop and withholds the heartbeat | `Stopped. The pin is released. LEAVE, then switch to ARMED once to clear the STOP.` CLEAR and READ ALL are refused with the STOP latched: the IOmcu would refuse the CLEAR as UNSAFE. LEAVE, then ARM and DISARM, then CONNECT |
| A switch to SAFE at the bench alone | STOPPED, no latch | `Stopped. The pin is released. Tap CLEAR to go on, or LEAVE.` CLEAR goes on with the read |
| ABORT | The operation ends; the port stays held | READ ALL again, or LEAVE |
| The link goes silent | The IOmcu's failsafe latches and the port stays held; the panel's module no longer counts it as its own | Once the link is back the overlay says `Port held by another Hitec session.` and offers RELEASE PORT |
| The panel starts again | As a lost link | As a lost link |

![STOP latched](img/servo-hitec-stopped.png)

## A port another session holds

A port the HITEC page holds and this panel did not open, one the bench
console opened or one held over a lost link or a panel restart, keeps the
Hitec row live: the overlay opens with the PORT row
`PROG PORT 1 GPIO 13  HELD ELSEWHERE` and `Another Hitec session holds it.`
RELEASE PORT opens a page; a hold of 2s on `HOLD 2s TO RELEASE` sends CLOSE,
which the IOmcu takes whether or not it trusts the heartbeat. The supply
stays as it is. Then a switch to ARMED clears the IOmcu's latched failsafe
as after any stop: ARM is refused only while the port is held.

![A port another session holds](img/servo-hitec-other.png)
![RELEASE PORT](img/servo-hitec-release.png)
![The port released](img/servo-hitec-released.png)

## Messages

| Line | When |
| --- | --- |
| `No D series servo answers. Check plug, receive mode and series.` | no byte came on the last of 3 tries of a D series read |
| `No classic servo answered after power-up. Check plug and series.` | no `+` came within 2s after the power-up |
| `The servo's answer to the entry was wrong. Check the series.` | bytes came that do not fit the entry |
| `Bad answer from the servo (code 14). Reading again.` | a try failed; the code is the decoder's (`hitec_rx_t`) |
| `Read failed at address 0x32. Tap READ ALL.` | every try of one address failed |
| `The IOmcu refused: its failsafe is latched.` | OPEN or CLEAR refused UNSAFE |
| `The IOmcu ended the session. The port is back in tester mode.` | the port went back to PWM with no command of this panel |
| `No answer from the IOmcu in time. The operation ended.` | a command not taken within 2000ms, or a request without an outcome within 30,000ms |
| `The supply did not follow the POWER request. Check the PD mini.` | POWER ended with SUPPLY |
| `The IOmcu cannot drive the line of this port.` | OPEN found no free line |
| `The IOmcu refused the request (code 7).` | a refusal with no message of its own |

![A classic CONNECT with no PD mini](img/servo-hitec-nopd.png)
![MORE](img/servo-hitec-more.png)

## Current limitations

- Nothing is written: the programmer reads only.
- The D series POWER of the entry with an off time of 0ms switches the PD
  mini's output off and on again: the servo restarts at every CONNECT.
- A classic read takes 2 render frames a cell; at 33ms a frame 100 cells
  take 6.6s in the host model.
- Station steps S1 to S5 of the plan (a D485HW at PRECHARGE and PULL-UP, the
  classic HS-5645MG and HS-7955TG, STOP and a pulled link cable during READ
  ALL, ARM and SUPPLY while held) are not run.
