---
screen: LAYOUT
title: Layout
order: 63
---

# Layout

LAYOUT zeigt die Stecker eines Boards mit verlöteten Pins, des [rbmod-Carriers](https://github.com/subtilitas/rcbench/wiki/Carrier-de). Es öffnet über die Kachel PICK A PIN auf [SETUP](setup.md), solange der IOmcu dieses Board meldet; die Kachel zeigt dort `STECKER UND PINS`. Sein Home-Tag führt zurück zu SETUP. Der Bildschirm treibt nichts und nimmt keinen Tipp: die Pins sind verlötet, und [Ausgänge](outputs.md) gibt jedem Servoausgang sein Protokoll.

![LAYOUT auf dem Carrier](view:layout-rbmod)

## J1 und J2

Jede Steckerposition ist Signal, + und GND, in der Folge, in der der Stecker sie nummeriert. Eine Zeile zeigt die Position, die Rolle, ihren GPIO (General-Purpose Input/Output) und das Netz an ihrem +.

| Position | J1, Programmierports | J2, Servoausgänge |
|---|---|---|
| 1 | PROGP04, GPIO 0, VCC_PROGPORT | PWM03, GPIO 5, VCC_SERVOPORT_B |
| 2 | PROGP01, GPIO 13, VCC_PROGPORT | PWM02, GPIO 4, VCC_SERVOPORT_B |
| 3 | PROGP02, GPIO 14, VCC_PROGPORT | PWM01, GPIO 2, VCC_SERVOPORT_A |
| 4 | PROGP03, GPIO 15, VCC_PROGPORT | PWM04, GPIO 1, VCC_SERVOPORT_A |

Rechts in einer Zeile von J1 steht `PROGRAMMIEREN`: ein Programmierport trägt die Programmer von KST und Hitec. Rechts in einer Zeile von J2 steht das auf dem Ausgang gebundene Protokoll, `OFF` für keines, oder `NICHT GELESEN`, bis die Bindung dieses Boards gelesen ist. Ein Protokoll, das unter OUTPUTS gebunden oder gelöst wird, zeigt sich hier im nächsten Frame.

## Weitere Stecker

J3 HEARTBEAT auf GPIO 3, J5 SENSOR I2C auf GPIO 16 und 17, J4 I2C 2 auf GPIO 18 und 19 (`NICHT BETRIEBEN`), J6 PD MINI auf GPIO 7 und 6, J9 TAP auf GPIO 22, und PROG 5V EN auf GPIO 21, der an keinem Stecker liegt.

## Boards Rev 1

Die Zeile unter den Spalten lautet `EIN BOARD REV 1 BRAUCHT ZWEI ÄNDERUNGEN VON HAND. DIE HILFE NENNT SIE.` Die zwei Änderungen stehen in [Carrier: Nacharbeit Rev 1](https://github.com/subtilitas/rcbench/wiki/Carrier-de#nacharbeit-rev-1): PROGP03 wandert von GPIO 26 auf GPIO 15, und ein Pull-down hält die 5V der Programmierports aus, solange GPIO 21 nicht getrieben ist. Ein Board Rev 2 trägt beide im Kupfer.

## Verwandte Seiten

- [Ausgänge](outputs.md): die Protokolle auf PWM01 bis PWM04.
- [SETUP](setup.md)
