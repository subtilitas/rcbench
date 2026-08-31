# BLHeli_32-Parameter

<sub>[English](BLHeli32.md) · **Deutsch**</sub>

Warum BLHeli_32 nicht in der ESC-Liste des Programmierers steht, und was der
Prüfstand mit so einem Regler stattdessen macht.

## Was funktioniert

Ein BLHeli_32-Regler ist an diesem Prüfstand ein erkannter, ansteuerbarer,
messbarer Regler:

- er wird **erkannt** — der Prüfstand nennt MCU und Bootloader-Revision;
- **Drehrichtung, Drehrichtungsumkehr, 3D-Modus, Beacon und Save-Settings**
  funktionieren, denn das sind DShot Special Commands auf der Signalleitung;
- die **Telemetrie** ist nicht betroffen.

## Was nicht funktioniert

Die Parameterliste — Timing, PWM-Frequenz, Anlaufleistung, Strombegrenzung und
der Rest. Diese Einstellungen liegen in einer Form vor, die dieser Prüfstand
nicht lesen kann, und die dafür nötigen Informationen sind nicht
veröffentlicht.

## Wir haben gefragt

Im August 2026 wurde der Rechteinhaber gefragt, ob dieses Projekt bekommen
kann, was es zum Lesen und Schreiben braucht. **Er hat abgelehnt** — damit ist
die Frage entschieden und nicht offen, und sie steht hier, damit niemand einen
Abend damit verbringt, sie neu aufzumachen.

## Was stattdessen geht

Ein auf **AM32** geflashter Regler ist offen, und dieser Prüfstand programmiert
ihn vollständig.
