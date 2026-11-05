# Worum es geht

<sub>[English](Manifest.md) · **Deutsch**</sub>

Der Prüfstand steuert Komponenten eines RC-Systems (Radio Control,
Funkfernsteuerung) an und misst sie: Motor und ESC (Electronic Speed
Controller, Motorregler), Servos und Empfängerbusse. Weitere Aufgaben sind
das Auswuchten von Propellern und die Beurteilung des Akkuzustands. Diese
Seite beschreibt die ursprünglichen Anforderungen und ihre Umsetzung.

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
| Externe Stromsensoren über I²C (Inter-Integrated Circuit) | An den I²C-Bussen des Mess-IOmcu, dem zweiten der beiden RP2354B der IO-Platine (hardware/docs/IOBoard.md). Am Touch-Bus des Panels (GPIO7, GPIO8) hängt der Touch-Controller. Der Drehknopf hat einen eigenen I²C-Bus. |
| ESC-Programmierer (ESCs mit AM32- und BLHeli_S-Firmware gefordert) | One-Wire-Bootloader-Protokoll, Half Duplex, 19 200 Baud, auf einer PIO-State-Machine (PIO: Programmable Input/Output) |
| Auswuchten mit Beschleunigungs- und Indexsensor | Beide Sensoren am IOmcu, auf einer gemeinsamen Zeitbasis abgetastet |
| Servotester mit S.BUS und weiteren Protokollen | PWM-Ausgänge (PWM: Pulsweitenmodulation) in Hardware; ein PIO-Programm je serielles Protokoll |
| Servoprogrammierung | Servos von KST und Chaservo: eine Servoprogrammierung auf der Seite SERVO mit Backup, Grenzprüfungen und Schreibfreigabe, über den Programmierport des IOmcu ([Servoprogrammierung](ServoProgrammer-de.md)). Servos von Hitec, klassisch HS-5xxx und HS-7xxx und die D-Serie: eine Servoprogrammierung auf der Seite SERVO, die Einstellungen ausliest, sichert und schreibt ([Servoprogrammierung für Hitec-Servos](HitecProgrammer-de.md), [Der Programmierport für Servos von Hitec](LinkHitec-de.md)) |
| Logviewer für mehrere Formate | `shared/logfile`: ein CSV-Leser (Comma-Separated Values), der Dezimalkomma oder Dezimalpunkt, eine Einheitenzeile und Zeilen unterschiedlicher Länge akzeptiert |

Der Stand jeder Anforderung steht in [STATUS.md](https://github.com/subtilitas/rcbench/blob/main/STATUS.md).

## Nicht geplant

- **Konfiguration über JETI EX Bus.** Die EX-Bus-Spezifikation beschränkt
  Remote Configuration auf JETI-Produkte und dokumentiert sie nicht. Die
  EX-Bus-Unterstützung umfasst Kanalwerte und Telemetrie.
- **Parameter von ESCs mit BLHeli_32-Firmware.** Die zum Lesen nötigen Informationen sind nicht
  veröffentlicht; eine Anfrage an den Rechteinhaber wurde im August 2026
  abgelehnt. [Details](BLHeli32-de.md).

## Lizenzregel

Jedes Protokoll wird anhand seiner veröffentlichten Spezifikation
implementiert. Fehlt eine solche Spezifikation, dienen Mitschnitte der
Signalleitung an eigener Hardware des Projekts als Grundlage. Das betrifft
das Programmierprotokoll von KST- und Chaservo-Servos.
Die meisten offenen Implementierungen dieser Protokolle stehen unter GPL (GNU
General Public License) oder AGPL (GNU Affero General Public License), was mit
der MIT-Lizenz (MIT: Massachusetts Institute of Technology) dieses
Repositories unvereinbar ist. Ein Protokoll ohne veröffentlichte Spezifikation
und ohne Mitschnitt seiner Leitung wird nicht implementiert.
