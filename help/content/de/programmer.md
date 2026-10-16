---
screen: PROGRAMMER
title: Programmer
order: 90
---

# Programmer

PROGRAMMER liest und schreibt die Einstellungen eines ESC (Electronic Speed Controller) oder eines Servos und programmiert einen ESC über sein Throttle-Stick-Menü. Der Bildschirm öffnet über die Kachel PROGRAMMER im [Menü](overview.md). Die Folge ist: Geräteklasse, Protokoll, VERBINDEN.

## Die Geräteklasse

![WAS WIRD PROGRAMMIERT?](view:programmer)

| Klasse | Zeile | Seite |
|---|---|---|
| ESC | `Bootloader, CLI und Datenpakete`, 4 Protokolle | diese Seite |
| SERVO | `Servos mit veröffentlichten Protokollen`, 1 Protokoll | diese Seite |
| ESC STICK | `Gasmenüs und Pieptöne`, mit der Zahl ausführbarer Profile | [ESC STICK](programmer-stick.md) |

Die Klassen ESC und SERVO sind ein Modell auf dem Panel. Kein Transport trägt ihre Protokolle: VERBINDEN, AUSLESEN und SCHREIBEN ändern die eigene Kopie eines Geräts im Panel und senden nichts auf eine Leitung. Jede Seite der beiden Klassen trägt die Marke SIMULIERT. Ihr Punkt ist amber, solange eine Sitzung offen ist, und grau, solange keine offen ist; grün wird er nie.

ESC STICK trägt keine Marke: Ein Stick-Lauf treibt die Throttle-Ausgänge und das Netzteil.

Der Servo-Programmierer, der echte KST- und Chaservo-Servos erreicht, ist PROGRAMMIEREN auf dem Bildschirm SERVO: [Servo-Programmierer](servo-programmer.md).

## Die Protokolle

![Die Protokolle der Klasse ESC](view:programmer-protocols)

Jede Protokollzeile nennt ihren Transport. Es gibt keine automatische Erkennung.

| Protokoll | Transport |
|---|---|
| BLHeli_S | Eindraht-Bootloader, 19200 Baud |
| AM32 | Eindraht-Bootloader, 19200 Baud |
| ESCape32 | Text-CLI (Command-Line Interface) über die Signalleitung |
| VESC | Datenpakete mit Rahmen, 115200 Baud |
| Hitec-Servo | Servoprotokoll der D-Serie |

Die ESC-Liste hat keinen Eintrag für BLHeli_32-Firmware: Der Prüfstand treibt solche ESCs und sendet die DShot-Sonderbefehle, kann ihre Parameter aber nicht lesen.

## Vor VERBINDEN

![Nichts hat geantwortet](view:programmer-idle)

Nichts ist bearbeitbar, bis VERBINDEN eine simulierte Sitzung öffnet. Die Seite zeigt `Noch keine Antwort von einem Gerät.` und `Kein Gerät verbunden. Keine Daten zum Anzeigen.`.

## Eine Sitzung

![Eine Sitzung am Modell von BLHeli_S](view:programmer-params)

Mit offener Sitzung nennt die Gerätezeile die simulierte Firmware und den Chip, zum Beispiel `BLHeli_S 16.7 auf EFM8BB21`, und VERBINDEN lautet TRENNEN. Die Parameter stehen in Gruppen, MOTOR, ANLAUF, SCHUTZ und weitere, mit der Hilfe der gewählten Zeile unter der Liste. Darunter steht die Zeile `Auf dem Panel simuliert. Kein Gerät wird angesprochen.`

- `-` und `+` ändern einen Wert schrittweise. Sie halten an den Enden einer Liste an; sie springen nicht herum.
- `^` und `v` scrollen die Liste; der Zähler zeigt die sichtbaren Zeilen, `1-7 von 8`.
- AUSLESEN übernimmt die Werte des Modells erneut und verwirft vorgemerkte Änderungen.
- ZURÜCK geht eine Ebene hoch und schließt die Sitzung. Das Home-Tag im Band verlässt den Bildschirm.

![Grad Vorzündung bei AM32](view:programmer-am32)

Jede Firmware zeigt ihre Einstellungen in ihren eigenen Einheiten. BLHeli_S zeigt das Timing als benannte Stufen; die anderen zeigen Grad Vorzündung.

## Vorgemerkte Änderungen

![Zwei vorgemerkte Änderungen: SCHREIBEN 2](view:programmer-dirty)

Ein geänderter Wert wird erst geschrieben, wenn SCHREIBEN gedrückt wird. Vorgemerkte Änderungen tragen eine Marke und eine eigene Farbe, und die Taste zählt sie: `SCHREIBEN 2`. Ohne Vormerkung ist SCHREIBEN grau.

## Touch

Jedes Bedienelement nimmt einen Tipp an: Es wirkt, wenn der Finger innerhalb des gedrückten Elements abhebt, höchstens 8 px vom Druckpunkt. Ein Finger zur selben Zeit drückt ein Bedienelement. Drei Bedienelemente wirken beim Druck: HALTEN ZUM STARTEN, ABBRECHEN eines Stick-Laufs und die Tasten der Suchtastatur unter [ESC STICK](programmer-stick.md).

## Verwandte Seiten

- [ESC STICK](programmer-stick.md): einen ESC über sein Throttle-Stick-Menü programmieren.
- [Servo-Programmierer](servo-programmer.md): KST- und Chaservo-Servos, auf dem Bildschirm SERVO.
