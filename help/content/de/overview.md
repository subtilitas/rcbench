---
screen: OVERVIEW
title: Menü
order: 1
---

# Menü

Das Menü ist der Startbildschirm des Panels. Es öffnet nach dem Splash und hat eine Kachel je Prüfstandsbildschirm. Ein Tipp auf eine Kachel öffnet ihren Bildschirm. Das Home-Tag oben links auf jedem anderen Bildschirm führt eine Ebene zurück: von einem Bildschirm, den das Menü öffnet, hierher, von einer Liste oder einem Bildschirm, den die Kacheln von SETUP öffnen, zu den Kacheln.

## Die Kacheln

![Das Menü bei stehendem Link](view:overview)

Acht Kacheln zu je 243 x 255px, in 4 Spalten und 2 Reihen: die Werkbänke in der ersten Reihe, die Instrumente in der zweiten, LOGS über SETUP in der letzten Spalte.

| Kachel | Zeile unter dem Namen | Seite |
|---|---|---|
| MOTOR & ESC | Antrieb, V/A, Drehzahl | [Motor und ESC](motor.md) |
| SERVO | Weg, Strom, Programm | [Servo](servo.md) |
| SUPPLY | V/A einstellen, aufzeichnen | [Netzteil](supply.md) |
| LOGS | Läufe und Dateien | [Logs](logs.md) |
| RECEIVER | S.BUS, PPM, Frames | [Receiver](analyser.md) |
| BATTERY | Zellen, iR, mAh | [Akku](battery.md) |
| BALANCE | Vibration, Phase | [Wuchten](balance.md) |
| SETUP | Pins, Sensoren, Panel | [Setup](setup.md) |

Das Programmieren hat keine Kachel: ESCs werden auf [PROGRAMMIEREN](motor.md#programmieren) von MOTOR & ESC programmiert, Servos auf PROGRAMMIEREN von SERVO.

Ein Tipp öffnet eine Kachel, wenn der Finger auf derselben Kachel abhebt, auf der er gedrückt hat. Ein Druck, der von der Kachel rutscht, öffnet nichts.

## Die Marke SIMULIERT

Die Marke SIMULIERT auf einer Kachel sagt, dass der IOmcu für diesen Bildschirm keine Fähigkeit meldet oder dass sein Sensor nicht aktiviert ist. Sie ordnet nicht jeden Wert auf dem Bildschirm ein: RECEIVER und BATTERY tragen die Marke und zeigen gar keine Werte, und SERVO ohne Marke zeigt das Versorgungsmodell des Panels als Messung der Servoschiene, wenn weder ein PD mini noch ein INA3221 aktiviert ist. Jeder Bildschirm nennt die Quelle seiner Werte selbst.

Die Marke folgt den Fähigkeits-Bits, die der IOmcu beim Link-Aufbau meldet:

| Kachel | Ohne Marke, wenn |
|---|---|
| MOTOR & ESC | der IOmcu ESCs ansteuert und eine INA228-Einstellung hält, die unter SETUP, INTERFACES eingeschaltet ist |
| SERVO | der IOmcu Servo-PWM-Ausgänge (Pulsweitenmodulation) ansteuert |
| SUPPLY | der PD mini unter SETUP, INTERFACES eingeschaltet ist |
| RECEIVER, BATTERY, BALANCE | in diesem Release nie: kein IOmcu meldet diese Fähigkeiten |
| LOGS, SETUP | nie markiert: sie enthalten keine Messung |

Ohne antwortenden IOmcu tragen MOTOR & ESC und SERVO die Marke ebenfalls. SUPPLY folgt allein der Einstellung PD mini, mit oder ohne Link.

## Das helle Design

![Das Menü im hellen Design](view:overview-light)

SETUP, PANEL, Design schaltet zwischen DUNKEL und HELL. Die Änderung gilt ab dem nächsten Frame.

## Ein Alert am unteren Rand

![Ein Alert über den unteren Rand des Menüs](view:overview-alert)

Ein Fehler, den der offene Bildschirm nicht zeigt, erscheint in einem roten Band, 44px hoch, über den unteren Rand jedes Bildschirms mit Statusband. Er verschwindet 30s nach seinem Eintreffen oder bei einem Tipp auf das Band. Die Seite [Statusband](band.md#das-alert-band) beschreibt sein Verhalten.

## Beim Start: der Splash

![Der Splash, während die Teilsysteme sich melden](view:splash)

Beim Einschalten listet der Splash jedes Teilsystem, sobald es bereit ist: PLATINE, DISPLAY, TOUCH, SPEICHER, EINSTELLUNGEN, VERBINDUNG und IOMCU. Die Zeile PLATINE trägt die Firmware-Version des Panels. Die Zeile IOMCU trägt die Protokollversion und die Firmware-Version, die der IOmcu meldet.

- Eine fehlende SD-Karte (Secure Digital) oder ein unbrauchbarer NVS (nichtflüchtiger Speicher) ist eine Warnung.
- Ein Touch-Controller, der nicht antwortet, oder ein IOmcu mit anderer Hauptversion des Protokolls ist ein Fehler. Der Prüfstand schaltet dann nicht auf ARMED.
- Haben alle Schritte geantwortet, folgt das Menü nach 1,6s. Ein Tipp überspringt die Wartezeit.
- Bei jedem Start führt das Panel 1200ms lang einen CAN-Selbsttest (Controller Area Network) aus. Ein Bus, der ihn nicht besteht, öffnet statt des Menüs den [Busfehler-Bildschirm](busfault.md).

## Verwandte Seiten

- [Statusband und STOP](band.md)
- [IOmcu-Update](coproc_update.md): die Frage, die auf dem Menü öffnet, wenn der IOmcu einen anderen Build hat.
