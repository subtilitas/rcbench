---
screen: OVERVIEW
title: Menü
order: 1
---

# Menü

Das Menü ist die Startseite des Panels. Es öffnet sich nach dem Splash und enthält eine Kachel für jede Prüfstandsseite. Tippe auf eine Kachel, um die Seite zu öffnen. Das Home-Feld oben links führt eine Ebene zurück: von einer Hauptseite zu diesem Menü, von einer SETUP-Seite oder Liste zu den SETUP-Kacheln.

## Die Kacheln

![Das Menü bei bestehender Verbindung](view:overview)

Acht Kacheln zu je 243 x 255px, in 4 Spalten und 2 Reihen: die Werkbänke in der ersten Reihe, die Instrumente in der zweiten, LOGS über SETUP in der letzten Spalte.

| Kachel | Zeile unter dem Namen | Seite |
|---|---|---|
| MOTOR & ESC | Antrieb, V/A, Drehzahl | [Motor und ESC](motor.md) |
| SERVO | Weg, Strom, Programmierung | [Servo](servo.md) |
| SUPPLY | V/A einstellen, aufzeichnen | [Netzteil](supply.md) |
| LOGS | Läufe, Dateien | [Logs](logs.md) |
| RECEIVER | S.BUS, PPM, Frames | [Receiver](analyser.md) |
| BATTERY | Zellen, iR, mAh | [Akku](battery.md) |
| BALANCE | Vibration, Phase | [Wuchten](balance.md) |
| SETUP | Panel, Pins, Karte, WLAN | [Setup](setup.md) |

Die Programmierung hat keine eigene Kachel. Öffne [PROGRAMMIEREN](motor.md#programmieren) auf MOTOR & ESC für elektronische Drehzahlregler (ESCs) oder PROGRAMMIEREN auf SERVO für Servos.

Eine Kachel öffnet sich, wenn du den Finger auf derselben Kachel anhebst, auf die du gedrückt hast. Ziehst du den Finger von der Kachel weg, wird der Tipp verworfen.

## Die Marke SIMULIERT

Die Markierung SIMULIERT bedeutet, dass der IOmcu für diese Seite keine Fähigkeit meldet oder dass ihr Sensor deaktiviert ist. Sie beschreibt nicht jeden angezeigten Wert. RECEIVER und BATTERY tragen die Markierung, zeigen aber keine Werte. SERVO kann ohne Markierung erscheinen und dennoch das Netzteilmodell des Panels als Quelle für die Servoschiene verwenden, wenn weder ein PD mini noch ein INA3221 aktiviert ist. Jede Seite nennt die Quelle ihrer Messwerte.

Die Markierung richtet sich nach den Fähigkeits-Bits, die der IOmcu beim Verbindungsaufbau meldet:

| Kachel | Ohne Marke, wenn |
|---|---|
| MOTOR & ESC | der IOmcu ESCs ansteuert und eine INA228-Einstellung hält, die unter SETUP, SENSORS eingeschaltet ist |
| SERVO | der IOmcu Servo-PWM-Ausgänge (Pulsweitenmodulation) ansteuert |
| SUPPLY | der PD mini unter SUPPLY, Tab PD MINI eingeschaltet ist |
| RECEIVER, BATTERY, BALANCE | in diesem Release nie: kein IOmcu meldet diese Fähigkeiten |
| LOGS, SETUP | nie markiert: sie enthalten keine Messung |

Wenn kein IOmcu antwortet, tragen auch MOTOR & ESC und SERVO die Markierung. Bei SUPPLY hängt sie allein von der Einstellung PD mini ab, unabhängig von der Verbindung.

## Das helle Design

![Das Menü im hellen Design](view:overview-light)

SETUP, PANEL, Design schaltet zwischen DUNKEL und HELL. Die Änderung gilt ab dem nächsten Frame.

## Ein Alert am unteren Rand

![Ein Alarm am unteren Rand des Menüs](view:overview-alert)

Ein Fehler, den die geöffnete Seite nicht zeigt, erscheint in einem roten Band am unteren Rand jeder Seite mit Statusband. Das Alarmband ist 44px hoch. Ein Alarm verschwindet nach 30s oder wenn du auf das Band tippst. Die Seite [Statusband](band.md#das-alert-band) beschreibt die Einzelheiten.

## Beim Start: der Splash

![Der Splash, während die Teilsysteme sich melden](view:splash)

Beim Einschalten zeigt der Splash den Start der Teilsysteme: PLATINE, DISPLAY, TOUCH, SPEICHER, EINSTELLUNGEN, VERBINDUNG und IOMCU. Die Zeile PLATINE zeigt die Firmware-Version des Panels. Die Zeile IOMCU zeigt die vom IOmcu gemeldete Protokoll- und Firmware-Version.

- Eine fehlende SD-Karte (Secure Digital) oder ein unbrauchbarer NVS (nichtflüchtiger Speicher) führt zu einer Warnung.
- Ein Touch-Controller, der nicht antwortet, oder ein IOmcu mit einer anderen Hauptversion des Protokolls führt zu einem Fehler. Der Prüfstand schaltet dann nicht auf ARMED.
- Sobald alle Schritte abgeschlossen sind, öffnet sich nach 1,6s das Menü. Tippe auf die Anzeige, um die Wartezeit zu überspringen.
- Bei jedem Start führt das Panel 1200ms lang einen CAN-Selbsttest (Controller Area Network) aus. Besteht der Bus diesen Test nicht, öffnet sich statt des Menüs die [Busfehler-Seite](busfault.md).

## Verwandte Seiten

- [Statusband und STOP](band.md)
- [IOmcu-Update](coproc_update.md): die Rückfrage im Menü, wenn der IOmcu einen anderen Build verwendet.
