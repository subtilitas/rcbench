---
screen: PROGRAMMER
title: Programmer
order: 90
---

# Programmer

PROGRAMMER reads and writes the settings of an ESC (electronic speed controller) or a servo, and programs an ESC through its throttle-stick menu. It opens from the PROGRAMMER tile on the [menu](overview.md). The sequence is: device class, protocol, CONNECT.

## The device class

![WHAT ARE YOU PROGRAMMING?](view:programmer)

| Class | Line | Page |
|---|---|---|
| ESC | `bootloader, CLI and packets`, 4 protocols | this page |
| SERVO | `servos, published protocols`, 1 protocol | this page |
| ESC STICK | `throttle menus, by beeps`, with how many profiles run | [ESC STICK](programmer-stick.md) |

The ESC and SERVO classes are a model on the panel. No transport carries their protocols: CONNECT, READ and WRITE change the panel's own copy of a device and send nothing on a wire. Every page of the two classes carries the MODELLED mark. Its dot is amber while a session is open and grey while none is; it never turns green.

ESC STICK carries no mark: a stick run drives the throttle outputs and the supply.

The servo programmer that reaches real KST and Chaservo servos is PROGRAM on the SERVO screen: [Servo programmer](servo-programmer.md).

## The protocols

![The ESC class's protocols](view:programmer-protocols)

Every protocol row names its transport. There is no autodetection.

| Protocol | Transport |
|---|---|
| BLHeli_S | one-wire bootloader, 19200 baud |
| AM32 | one-wire bootloader, 19200 baud |
| ESCape32 | text CLI (command-line interface) over the signal line |
| VESC | framed packets, 115200 baud |
| Hitec servo | D-series servo protocol |

The ESC list has no entry for BLHeli_32 firmware: the bench drives such ESCs and sends the DShot special commands, but cannot read their parameters.

## Before CONNECT

![Nothing has answered](view:programmer-idle)

Nothing is editable until CONNECT opens a modelled session. The page reads `nothing has answered` and `no device, so nothing to show`.

## A session

![A session open on the BLHeli_S model](view:programmer-params)

With a session open, the device line names the modelled firmware and chip, for example `BLHeli_S 16.7 on EFM8BB21`, and CONNECT reads DISCONNECT. The parameters are shown in groups, MOTOR, STARTUP, PROTECTION and others, with the selected row's help under the list. The line `Modelled on the panel: no device is reached.` stands under it.

- `-` and `+` step a value. Steppers stop at the ends of a list; they do not wrap.
- `^` and `v` scroll the list; the counter shows the rows in view, `1-7 of 8`.
- READ takes the model's values again and discards staged edits.
- BACK climbs one level and closes the session. The band's home tag leaves the screen.

![Degrees of advance on AM32](view:programmer-am32)

Each firmware's settings appear in its own units. BLHeli_S shows timing as named steps; the others show degrees of advance.

## Staged changes

![Two staged edits: WRITE 2](view:programmer-dirty)

A changed value is not written until WRITE is pressed. Staged changes carry a mark and their own colour, and the button counts them: `WRITE 2`. WRITE is grey with nothing staged.

## Touch

Every control takes a tap: it acts when the finger lifts inside the control it pressed, no more than 8 px from the press. One finger at a time presses a control. Three controls act on the press: HOLD TO RUN, ABORT of a stick run, and the keys of the search keyboard on [ESC STICK](programmer-stick.md).

## Related pages

- [ESC STICK](programmer-stick.md): programming an ESC through its throttle-stick menu.
- [Servo programmer](servo-programmer.md): KST and Chaservo servos, on the SERVO screen.
