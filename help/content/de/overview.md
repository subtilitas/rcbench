---
screen: OVERVIEW
title: Menü
order: 1
---

# Menü

Das Menü ist der Startbildschirm des Panels. Es öffnet nach dem Splash und hat eine Kachel je Prüfstandsbildschirm. Ein Tipp auf eine Kachel öffnet ihren Bildschirm. Das Home-Tag oben links auf jedem anderen Bildschirm führt hierher zurück.

## Die Kacheln

![Das Menü bei stehendem Link](view:overview)

Neun Kacheln zu je 150 x 204 px, in zwei Reihen:

| Kachel | Zeile unter dem Namen | Seite |
|---|---|---|
| MOTOR & ESC | Antrieb V/A rpm | [Motor und ESC](motor.md) |
| SERVO | Weg, Strom | [Servo](servo.md) |
| SUPPLY | Setzen, Log V/A | [Netzteil](supply.md) |
| ANALYSER | Busse, Frames | [Analyser](analyser.md) |
| LOGS | Schreiben, Lesen | [Logs](logs.md) |
| SETUP | Akku und Ausgang | [Setup](setup.md) |
| BATTERY | Zellen, iR, mAh | [Akku](battery.md) |
| BALANCE | Vibration, Phase | [Wuchten](balance.md) |
| PROGRAMMER | ESC und Servo | [Programmer](programmer.md) |

Ein Tipp öffnet eine Kachel, wenn der Finger auf derselben Kachel abhebt, auf der er gedrückt hat. Ein Druck, der von der Kachel rutscht, öffnet nichts.

## Die Marke SIMULIERT

Eine Kachel mit der Marke SIMULIERT öffnet einen Bildschirm, dessen Hardware nicht eingebaut ist. Jeder Wert darauf ist simuliert, und der Bildschirm sagt es. Eine Kachel ohne Marke zeigt gemessene Werte.

Die Marke folgt den Fähigkeits-Bits, die der Koprozessor beim Link-Aufbau meldet:

| Kachel | Ohne Marke, wenn |
|---|---|
| MOTOR & ESC | der Koprozessor ESCs ansteuert und eine INA228-Einstellung hält, die unter SETUP, ANSCHLÜSSE eingeschaltet ist |
| SERVO | der Koprozessor Servo-PWM-Ausgänge (Pulsweitenmodulation) ansteuert |
| SUPPLY | der PD mini unter SETUP, ANSCHLÜSSE eingeschaltet ist |
| ANALYSER, BATTERY, BALANCE, PROGRAMMER | in diesem Release nie: kein Koprozessor meldet diese Fähigkeiten |
| LOGS, SETUP | nie markiert: sie enthalten keine Messung |

Ohne antwortenden Koprozessor tragen MOTOR & ESC und SERVO die Marke ebenfalls. SUPPLY folgt allein der Einstellung PD mini, mit oder ohne Link.

## Das helle Design

![Das Menü im hellen Design](view:overview-light)

SETUP, ANWENDUNG, Design schaltet zwischen DUNKEL und HELL. Die Änderung gilt ab dem nächsten Frame.

## Ein Alert am unteren Rand

![Ein Alert über den unteren Rand des Menüs](view:overview-alert)

Ein Fehler, den der offene Bildschirm nicht zeigt, erscheint in einem roten Band, 34 px hoch, über den unteren Rand jedes Bildschirms mit Statusband. Er verschwindet 30 s nach seinem Eintreffen oder bei einem Tipp auf das Band. Die Seite [Statusband](band.md#das-alert-band) beschreibt sein Verhalten.

## Beim Start: der Splash

![Der Splash, während die Teilsysteme sich melden](view:splash)

Beim Einschalten listet der Splash jedes Teilsystem, sobald es bereit ist: PLATINE, DISPLAY, TOUCH, SPEICHER, EINSTELLUNGEN, LINK und KOPROZESSOR. Die Zeile PLATINE trägt die Firmware-Version des Panels. Die Zeile KOPROZESSOR trägt die Protokollversion und die Firmware-Version, die der Koprozessor meldet.

- Eine fehlende SD-Karte (Secure Digital) oder ein unbrauchbarer NVS (nichtflüchtiger Speicher) ist eine Warnung.
- Ein Touch-Controller, der nicht antwortet, oder ein Koprozessor mit anderer Hauptversion des Protokolls ist ein Fehler. Der Prüfstand schaltet dann nicht auf ARMED.
- Haben alle Schritte geantwortet, folgt das Menü nach 1,6 s. Ein Tipp überspringt die Wartezeit.
- Bei jedem Start führt das Panel 1200 ms lang einen CAN-Selbsttest (Controller Area Network) aus. Ein Bus, der ihn nicht besteht, öffnet statt des Menüs den [Busfehler-Bildschirm](busfault.md).

## Verwandte Seiten

- [Statusband und STOP](band.md)
- [Koprozessor-Update](coproc_update.md): die Frage, die auf dem Menü öffnet, wenn der Koprozessor einen anderen Build hat.
