# Parameter von ESCs mit BLHeli_32-Firmware

<sub>[English](BLHeli32.md) · **Deutsch**</sub>

Die ESC-Liste (Electronic Speed Controller, Motorregler) der
Programmierfunktion enthält keinen Eintrag für BLHeli_32-Firmware. Diese
Seite beschreibt, welche Funktionen der Prüfstand für einen solchen ESC
unterstützt und welche nicht.

## Unterstützt

- Erkennung: MCU (Microcontroller Unit) und Bootloader-Revision.
- Drehrichtung, Drehrichtungsumkehr, 3D-Modus, Beacon und Save-Settings, als
  DShot Special Commands auf der Signalleitung.
- Telemetrie.

## Nicht unterstützt

Auslesen oder Schreiben der Parameterliste: Timing, PWM-Frequenz
(Pulsweitenmodulation), Anlaufleistung, Strombegrenzung und die übrigen
Parameter. Die
Einstellungen liegen in einer Form vor, die der Prüfstand nicht lesen kann,
und die zum Lesen nötigen Informationen sind nicht veröffentlicht.

## Stand

Der Rechteinhaber wurde im August 2026 um die Informationen gebeten, die
zum Auslesen und Schreiben der Parameter nötig sind. Die Anfrage wurde
abgelehnt. Der Punkt ist abgeschlossen.

## Alternative

Ein auf AM32-Firmware geflashter ESC kann die [Sitzung auf der Signalleitung](EscWire-de.md) nutzen.
