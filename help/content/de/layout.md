---
screen: LAYOUT
title: Layout
order: 63
---

# Layout

LAYOUT zeigt die Stecker und verlöteten Pins des [rbmod-Carriers](https://github.com/subtilitas/rcbench/wiki/Carrier-de). Öffne die Seite über die Kachel LAYOUT auf [SETUP](setup.md). Die Kachel ist verfügbar, solange der IOmcu diese Platine meldet, und zeigt `STECKER UND PINS`. Das Home-Feld führt zurück zu SETUP. Die Ansicht steuert keine Ausgänge an und reagiert nicht auf Antippen. Die Pins sind verlötet; ordne jedem Servoausgang sein Protokoll unter [OUTPUTS](outputs.md) zu.

![LAYOUT auf dem Carrier](view:layout-rbmod)

## J1 und J2

Jede Steckerposition umfasst Signal, + und GND. Die Zeilen folgen der Nummerierung des Steckers und zeigen Position, Funktion, GPIO (General-Purpose Input/Output) und das mit + verbundene Netz.

| Position | J1, Programmierports | J2, Servoausgänge |
|---|---|---|
| 1 | PROGP04, GPIO 0, VCC_PROGPORT | PWM03, GPIO 5, VCC_SERVOPORT_B |
| 2 | PROGP01, GPIO 13, VCC_PROGPORT | PWM02, GPIO 4, VCC_SERVOPORT_B |
| 3 | PROGP02, GPIO 14, VCC_PROGPORT | PWM01, GPIO 2, VCC_SERVOPORT_A |
| 4 | PROGP03, GPIO 15, VCC_PROGPORT | PWM04, GPIO 1, VCC_SERVOPORT_A |

Rechts in jeder J1-Zeile steht `PROGRAMMIERUNG`: Diese Ports dienen der KST- und Hitec-Programmierung. Jede J2-Zeile zeigt das zugeordnete Ausgangsprotokoll, `AUS` ohne Zuordnung oder `NICHT AUSGELESEN`, bis die Zuordnung dieser Platine ausgelesen ist. Änderungen unter OUTPUTS erscheinen hier im nächsten Frame.

## Weitere Stecker

J3 HEARTBEAT liegt auf GPIO 3, J5 SENSOR I2C auf GPIO 16 und 17, J4 I2C 2 auf GPIO 18 und 19 (`NICHT ANGESTEUERT`), J6 PD MINI auf GPIO 7 und 6 und J9 TAP auf GPIO 22. PROG 5V EN liegt auf GPIO 21 und ist auf keinen Stecker geführt.

## Platinen Rev 1

Unter den Spalten steht `PLATINE REV 1: DREI ÄNDERUNGEN VON HAND. DIE HILFE NENNT SIE.` [Carrier: Nacharbeit Rev 1](https://github.com/subtilitas/rcbench/wiki/Carrier-de#nacharbeit-rev-1) beschreibt alle drei Änderungen: PROGP03 von GPIO 26 auf GPIO 15 verlegen; die 5V der Programmierports durch einen Pull-down ausgeschaltet halten, solange GPIO 21 nicht angesteuert ist; und eine Z-Diode mit 5,6V und 1W vom Ausgang von M7 nach GND ergänzen, um Ausfälle durch ein bremsendes Servo zu verhindern. Die Firmware setzt die dritte Änderung nicht voraus. Bei Rev 2 sind die ersten zwei im Platinenlayout umgesetzt; die dritte bleibt eine offene Designfrage.

## Verwandte Seiten

- [Ausgänge](outputs.md): die Protokolle auf PWM01 bis PWM04.
- [SETUP](setup.md)
