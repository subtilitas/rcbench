# Worum es geht

<sub>[English](Manifest.md) · **Deutsch**</sub>

Der Prüfstand misst und treibt die Teile eines RC-Antriebs (RC: Radio
Control, Funkfernsteuerung): Motor und ESC (Electronic Speed Controller,
Motorregler), Servos, Empfängerbusse, Propellerwucht und Akkuzustand. Diese
Seite listet die ursprünglichen Anforderungen und den Stand jeder einzelnen.

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

## Stand jeder Anforderung

| Anforderung | Umsetzung | Stand |
| --- | --- | --- |
| Externe Stromsensoren über I²C (Inter-Integrated Circuit) | Am I²C-Bus des Koprozessors. Der I²C-Bus des Panels ist für den Touch-Controller und den I/O-Expander reserviert. | Schnittstelle festgelegt; kein Treiber. Bauteile gewählt: INA238 (Motor), INA745A (Servoschiene). |
| ESC-Programmierer (AM32 und BLHeli_S gefordert) | One-Wire-Bootloader-Protokoll, Half Duplex, 19 200 Baud, auf einer PIO-State-Machine (PIO: Programmable Input/Output) | Bildschirm gebaut und tabellengesteuert für BLHeli_S, AM32, ESCape32 und VESC; kein Protokoll wird gesendet. BLHeli_32-Parameter werden nicht unterstützt: [BLHeli_32](BLHeli32-de.md). |
| Auswuchten mit Beschleunigungs- und Indexsensor | Beide Sensoren am Koprozessor, auf einer gemeinsamen Zeitbasis abgetastet | Bildschirm und Platzierungsanleitungen gebaut; die Messung wartet auf die Sensoren |
| Servotester mit S.BUS und weiteren Protokollen | PWM-Ausgänge (PWM: Pulsweitenmodulation) in Hardware; ein PIO-Programm je serielles Protokoll | Bildschirm gebaut und steuert über den Link; kein Ausgangstreiber erzeugt Pulse |
| Servoprogrammierung | Das Protokoll der Hitec-D-Serie ist veröffentlicht; andere Hersteller brauchen ein Programmiergerät zum Mitschneiden | Hitec-Tabelle im Programmierer-Bildschirm; KST (ein Servohersteller) auf Wunsch des Eigentümers zurückgestellt |
| Logviewer für mehrere Formate | `shared/logfile`: ein CSV-Reader (CSV: Comma-Separated Values), der Dezimalkomma und Dezimalpunkt, eine Einheitenzeile und unvollständige Zeilen akzeptiert | Gebaut: Durchsehen, Importansicht, Plot. Läufe werden auf die Karte geschrieben, solange der Prüfstand scharf ist. |

## Nicht geplant

- **Konfiguration über JETI EX Bus.** Die EX-Bus-Spezifikation beschränkt
  Remote Configuration auf JETI-Produkte und dokumentiert sie nicht. Die
  EX-Bus-Unterstützung umfasst Kanalwerte und Telemetrie.
- **BLHeli_32-Parameter.** Die zum Lesen nötigen Informationen sind nicht
  veröffentlicht; eine Anfrage an den Rechteinhaber wurde im August 2026
  abgelehnt. [Details](BLHeli32-de.md).
- **KST-Servoprogrammierung.** Auf Wunsch des Eigentümers zurückgestellt.

## Lizenzregel

Jedes Protokoll wird aus seiner veröffentlichten Spezifikation implementiert.
Die meisten offenen Implementierungen dieser Protokolle stehen unter GPL (GNU
General Public License) oder AGPL (GNU Affero General Public License), was mit
der MIT-Lizenz (MIT: Massachusetts Institute of Technology) dieses
Repositories unvereinbar ist. Ein Protokoll ohne veröffentlichte Spezifikation
wird nicht implementiert.
