# Worum es geht

<sub>[English](Manifest.md) · **Deutsch**</sub>

Der Prüfstand misst und treibt die Teile eines RC-Antriebs (RC: Radio
Control, Funkfernsteuerung): Motor und ESC (Electronic Speed Controller,
Motorregler), Servos, Empfängerbusse, Propellerwucht und Akkuzustand. Diese
Seite listet die ursprünglichen Anforderungen und ihre Umsetzung.

## Anforderungen

Die ursprüngliche Anforderungsliste:

> - Eingang für externe Stromsensoren (I²C)
> - ESC-Programmierer für das, was geht — im Moment AM32 und BLHeli garantiert,
>   YGE mal anfragen oder über EX-Bus einfach machen
> - Wuchten von Systemen über Beschleunigungssensor und Positionssensor
> - Unterstützung Reglerprogrammierung, soweit es geht
> - Servo-Tester mit allem und scharf: SBUS und andere Protokolle
> - Servo-Programmierung, wenn mir jemand Programmiergeräte ausleiht und das
>   Reverse Engineering klappt, respektive über EX-Bus
> - Logviewer für diverse Formate

## Umsetzung

| Anforderung | Umsetzung |
| --- | --- |
| Externe Stromsensoren über I²C (Inter-Integrated Circuit) | An den I²C-Bussen des Mess-Koprozessors, des zweiten der beiden RP2354B des IO-Boards (hardware/docs/IOBoard.md). Der I²C-Bus des Panels ist für den Touch-Controller und den I/O-Expander reserviert. |
| ESC-Programmierer (ESCs mit AM32- und BLHeli_S-Firmware gefordert) | One-Wire-Bootloader-Protokoll, Half Duplex, 19 200 Baud, auf einer PIO-State-Machine (PIO: Programmable Input/Output) |
| Auswuchten mit Beschleunigungs- und Indexsensor | Beide Sensoren am Koprozessor, auf einer gemeinsamen Zeitbasis abgetastet |
| Servotester mit S.BUS und weiteren Protokollen | PWM-Ausgänge (PWM: Pulsweitenmodulation) in Hardware; ein PIO-Programm je serielles Protokoll |
| Servoprogrammierung | Das Protokoll der Hitec-D-Serie ist veröffentlicht; andere Hersteller brauchen ein Programmiergerät zum Mitschneiden |
| Logviewer für mehrere Formate | `shared/logfile`: ein CSV-Reader (CSV: Comma-Separated Values), der Dezimalkomma und Dezimalpunkt, eine Einheitenzeile und unvollständige Zeilen akzeptiert |

Der Stand jeder Anforderung steht in [STATUS.md](https://github.com/subtilitas/rcbench/blob/main/STATUS.md).

## Nicht geplant

- **Konfiguration über JETI EX Bus.** Die EX-Bus-Spezifikation beschränkt
  Remote Configuration auf JETI-Produkte und dokumentiert sie nicht. Die
  EX-Bus-Unterstützung umfasst Kanalwerte und Telemetrie.
- **Parameter von ESCs mit BLHeli_32-Firmware.** Die zum Lesen nötigen Informationen sind nicht
  veröffentlicht; eine Anfrage an den Rechteinhaber wurde im August 2026
  abgelehnt. [Details](BLHeli32-de.md).

## Lizenzregel

Jedes Protokoll wird aus seiner veröffentlichten Spezifikation implementiert
oder, wo keine veröffentlicht ist, aus Mitschnitten der Leitung an eigener
Hardware des Projekts: das Programmierprotokoll von KST- und Chaservo-Servos
ist der eine Fall.
Die meisten offenen Implementierungen dieser Protokolle stehen unter GPL (GNU
General Public License) oder AGPL (GNU Affero General Public License), was mit
der MIT-Lizenz (MIT: Massachusetts Institute of Technology) dieses
Repositories unvereinbar ist. Ein Protokoll ohne veröffentlichte Spezifikation
und ohne Mitschnitt seiner Leitung wird nicht implementiert.
