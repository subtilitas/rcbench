---
screen: LAYOUT
title: Layout
order: 63
---

# Layout

LAYOUT shows the connectors of a board with soldered pins, the [rbmod carrier](https://github.com/subtilitas/rcbench/wiki/Carrier). It opens from its LAYOUT tile on [Setup](setup.md) while the IOmcu reports that board; the tile reads `CONNECTORS AND PINS` there. Its home tag leads back to SETUP. The screen drives nothing and takes no tap: the pins are soldered, and [Outputs](outputs.md) gives each servo output its protocol.

![LAYOUT on the carrier](view:layout-rbmod)

## J1 and J2

Each connector position is signal, + and GND, in the order the connector numbers them. A row shows the position, the role, its GPIO (general-purpose input/output) and the net on its +.

| Position | J1, programming ports | J2, servo outputs |
|---|---|---|
| 1 | PROGP04, GPIO 0, VCC_PROGPORT | PWM03, GPIO 5, VCC_SERVOPORT_B |
| 2 | PROGP01, GPIO 13, VCC_PROGPORT | PWM02, GPIO 4, VCC_SERVOPORT_B |
| 3 | PROGP02, GPIO 14, VCC_PROGPORT | PWM01, GPIO 2, VCC_SERVOPORT_A |
| 4 | PROGP03, GPIO 15, VCC_PROGPORT | PWM04, GPIO 1, VCC_SERVOPORT_A |

On the right of a J1 row stands `PROGRAMMING`: a programming port carries the KST and Hitec programmers. On the right of a J2 row stands the protocol bound on the output, `OFF` for none, or `NOT READ` until the binding of this board is read. A protocol bound or let go on OUTPUTS shows here on the next frame.

## Other connectors

J3 HEARTBEAT on GPIO 3, J5 SENSOR I2C on GPIO 16 and 17, J4 I2C 2 on GPIO 18 and 19 (`NOT DRIVEN`), J6 PD MINI on GPIO 7 and 6, J9 TAP on GPIO 22, and PROG 5V EN on GPIO 21, which is on no connector.

## Rev 1 boards

The line under the columns reads `A REV 1 BOARD NEEDS TWO CHANGES BY HAND. THE HELP PAGE LISTS THEM.` The two changes are in [Carrier: Rev 1 rework](https://github.com/subtilitas/rcbench/wiki/Carrier#rev-1-rework): PROGP03 moves from GPIO 26 to GPIO 15, and a pull-down holds the programming ports' 5V off while GPIO 21 is not driven. A rev 2 board carries both in copper.

## Related pages

- [Outputs](outputs.md): the protocols on PWM01 to PWM04.
- [Setup](setup.md)
