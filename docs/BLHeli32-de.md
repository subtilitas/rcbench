# Parameter von ESCs mit BLHeli_32-Firmware

<sub>[English](BLHeli32.md) · **Deutsch**</sub>

Die ESC-Liste (ESC: Electronic Speed Controller, Motorregler) des
Programmierers hat keinen Eintrag für BLHeli_32-Firmware. Diese Seite legt
fest, was der Prüfstand mit einem ESC mit BLHeli_32-Firmware macht und was
nicht.

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

Ein auf AM32-Firmware geflashter ESC kann die [Sitzung auf der Signalleitung](EscWire-de.md) nutzen.
