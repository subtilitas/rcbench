---
screen: LAYOUT
title: Layout
order: 63
---

# Layout

LAYOUT shows the connectors and soldered pins of the [rbmod carrier](https://github.com/subtilitas/rcbench/wiki/Carrier). Open it from the LAYOUT tile on [Setup](setup.md), available while the IOmcu reports that board. The tile reads `CONNECTORS AND PINS`, and the home tag returns to SETUP. The diagram does not drive outputs or accept taps. The pins are soldered; use [Outputs](outputs.md) to assign each servo output its protocol.

![LAYOUT on the carrier](view:layout-rbmod)

## J1 and J2

Each connector position has signal, + and GND pins. Rows follow the connector's numbering and show the position, role, GPIO (general-purpose input/output) and the net connected to +.

| Position | J1, programming ports | J2, servo outputs |
|---|---|---|
| 1 | PROGP04, GPIO 0, VCC_PROGPORT | PWM03, GPIO 5, VCC_SERVOPORT_B |
| 2 | PROGP01, GPIO 13, VCC_PROGPORT | PWM02, GPIO 4, VCC_SERVOPORT_B |
| 3 | PROGP02, GPIO 14, VCC_PROGPORT | PWM01, GPIO 2, VCC_SERVOPORT_A |
| 4 | PROGP03, GPIO 15, VCC_PROGPORT | PWM04, GPIO 1, VCC_SERVOPORT_A |

The right side of each J1 row reads `PROGRAMMING`: these ports serve the KST and Hitec programmers. Each J2 row shows its output's assigned protocol, `OFF` if none is assigned, or `NOT READ` until the board's assignment has been read. Changes made on OUTPUTS appear here on the next frame.

## Other connectors

J3 HEARTBEAT on GPIO 3, J5 SENSOR I2C on GPIO 16 and 17, J4 I2C 2 on GPIO 18 and 19 (`NOT DRIVEN`), J6 PD MINI on GPIO 7 and 6, J9 TAP on GPIO 22, and PROG 5V EN on GPIO 21, which is on no connector.

## Rev 1 boards

The line under the columns reads `A REV 1 BOARD NEEDS THREE MANUAL CHANGES. THE HELP PAGE LISTS THEM.` [Carrier: Rev 1 rework](https://github.com/subtilitas/rcbench/wiki/Carrier#rev-1-rework) describes all three: moving PROGP03 from GPIO 26 to GPIO 15; adding a pull-down to keep the programming ports' 5V off while GPIO 21 is not driven; and adding a 5.6V 1W zener from M7's output to GND to prevent dropouts caused by a braking servo. The firmware does not rely on the third change. Rev 2 includes the first two in its copper layout; the third remains an open design point.

## Related pages

- [Outputs](outputs.md): the protocols on PWM01 to PWM04.
- [Setup](setup.md)
