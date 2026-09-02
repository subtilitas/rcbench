# BLHeli_32-Parameter

<sub>[English](BLHeli32.md) · **Deutsch**</sub>

BLHeli_32 steht nicht in der ESC-Liste (ESC: Electronic Speed Controller,
Motorregler) des Programmierers. Diese Seite legt fest, was der Prüfstand mit
einem BLHeli_32-ESC macht und was nicht.

## Unterstützt

- Erkennung: MCU (Microcontroller Unit) und Bootloader-Revision.
- Drehrichtung, Drehrichtungsumkehr, 3D-Modus, Beacon und Save-Settings, als
  DShot Special Commands auf der Signalleitung.
- Telemetrie.

## Nicht unterstützt

Lesen oder Schreiben der Parameterliste: Timing, PWM-Frequenz (PWM:
Pulsweitenmodulation), Anlaufleistung, Strombegrenzung und die übrigen. Die
Einstellungen liegen in einer Form vor, die der Prüfstand nicht lesen kann,
und die zum Lesen nötigen Informationen sind nicht veröffentlicht.

## Stand

Der Rechteinhaber wurde im August 2026 gefragt, ob das Projekt erhalten kann,
was Lesen und Schreiben der Parameter voraussetzt. Die Anfrage wurde
abgelehnt. Der Punkt ist abgeschlossen.

## Alternative

Ein auf AM32 geflashter ESC wird vom Programmierer vollständig unterstützt.
