---
screen: OUTPUTS
title: Ausgänge
order: 62
---

# Ausgänge

OUTPUTS bindet die Pins des Koprozessors an Ausgabeprotokolle: welche Pins Servos treiben, welche ESCs (Electronic Speed Controller), und mit welchem Signal. Der Bildschirm öffnet über OUTPUTS auf [SETUP](setup.md) und über ZIEL auf [MOTOR & ESC](motor.md#ziel), auf der [Übersicht](#die-übersicht-off): OFF gewählt und jeder gebundene Pin mit seinem Protokoll benannt. Das Home-Tag führt zurück zu dem Bildschirm, aus dem er geöffnet wurde. Die Protokollliste und LETZTER SCHREIBVORGANG belegen die linke Spalte; die Pins des Koprozessors die rechte. [PICK A PIN](picker.md) hält dieselbe Bindung auf einer Zeichnung der Platine.

## Pins binden

![Vier Pins als SERVO PWM gebunden](view:outputs)

1. Ein Protokoll in der Liste PROTOKOLL wählen. Bis dahin tut ein Tipp auf einen Pin nichts.
2. Einen Pin antippen, um ihn abzuhaken. Ein Haken bindet den Pin sofort an das Protokoll; es gibt keine Taste zum Übernehmen.
3. Einen abgehakten Pin antippen, um ihn freizugeben.
4. OFF wählen, um zur Übersicht zurückzukehren.

Die Zahl unter der Liste, `4 VON 8 PINS`, sagt, wie viele Pins das Protokoll hält und wie viele es nimmt. Ein Pin gehört höchstens einem Protokoll. Mehrere Protokolle können gleichzeitig gebunden sein.

Jeder abgehakte Pin ist ein Slot der OUTPUTS-Seite des Koprozessors, in Pin-Reihenfolge über alle Protokolle. Der niedrigste abgehakte Pin ist Kanal 0. Es gibt 8 Slots und 8 Kanäle, geteilt von allen Protokollen.

## Die Protokolle

![Die Protokollliste](view:outputs-protocol)

| Protokoll | Signal | Pins |
|---|---|---|
| OFF | bindet nichts; zeigt die ganze Bindung | 0 |
| SERVO PWM | PWM (Pulsweitenmodulation) mit 50 Hz, für ein Servo oder ein Ruder | bis 8 |
| MOTOR PWM | derselbe Puls mit 50 Hz, für einen ESC; von Leerlaufpuls bis Vollpuls | bis 8 |
| PPM | PPM (Pulspositionsmodulation): 8 Kanäle auf einem Pin mit 40 Hz | 1 |
| DSHOT300, DSHOT600 | digitaler DShot-Throttle mit 300 oder 600 kbit/s | bis 8 |
| DSHOT300 BIDIR, DSHOT600 BIDIR | DShot mit Drehzahl-Telemetrie vom ESC auf derselben Leitung | bis 8 |

Ein Servokanal zentriert, wenn nichts ihn ansteuert; ein Motorkanal stoppt. Der Puls kann nicht sagen, was am Pin hängt, also sagt es das Protokoll. Der Throttle von MOTOR & ESC treibt jeden Kanal, der als MOTOR PWM oder mit einem DShot-Eintrag gebunden ist, und lässt die Servokanäle in Ruhe. PPM braucht 8 Kanäle; ein Prüfstand mit gebundenem PPM hat für nichts anderes Platz.

![Die längsten Protokollnamen](view:outputs-bidir)

Das Wählen eines Protokolls schreibt nichts. Es bestimmt, welche Pins abgehakt erscheinen und zu welchem Protokoll der nächste Haken kommt. Die Pins der anderen Protokolle bleiben gebunden.

## Leerlaufpuls und Vollpuls

Ein MOTOR-PWM-Kanal sendet 0 % Throttle als Leerlaufpuls und 100 % als Vollpuls, ab Werk 1000 us und 2000 us. Beide stehen unter [MOTOR & ESC, OPTIONEN](motor-settings.md), in Schritten von 10 us: Leerlaufpuls 800 bis 1600 us, Vollpuls 1400 bis 2400 us. Ein SERVO-PWM-Kanal behält 1000 bis 2000 us, oder den Bereich, den SERVO sendet. Eine Änderung erreicht den Koprozessor 300 ms nach der letzten Änderung, solange der Prüfstand SAFE ist. Ein Leerlaufpuls, der nicht unter dem Vollpuls liegt, wird nicht gesendet, und das Band meldet `Leerlaufpuls muss kürzer als Vollgaspuls sein. Nicht gesendet.`.

Ein ESC, der an einem Sender kalibriert wurde, nimmt den kürzesten Puls dieses Senders als null. Leerlaufpuls auf oder unter diesen Puls setzen und Vollpuls auf oder über den längsten: Für einen Sender, der 985 bis 2012 us sendet, 980 us und 2020 us.

## Was eine Pin-Zelle zeigt

![DSHOT300 gewählt, vier Pins von SERVO PWM gehalten](view:outputs-held)

| Zelle | Bedeutung | Ein Tipp |
|---|---|---|
| abgehakt, `PAD 6` | im gewählten Protokoll gebunden; die auf der Platine gedruckte Pad-Nummer | gibt ihn frei |
| ausgegraut, der Name des anderen Protokolls | von einem anderen Protokoll gehalten | tut nichts |
| rot, durchgestrichen | reserviert: GP3 trägt den Sicherheits-Heartbeat, GP8 bis GP12 den CAN-Controller (Controller Area Network) | tut nichts |
| `PROG-PORT` | der Pin des Programmierports des Koprozessors, ab Werk GP13 | tut nichts |
| ausgegraut, `SLICE VON GP13` | unter SERVO PWM und MOTOR PWM: der Pin teilt sich einen PWM-Slice mit dem Pin des Programmierports; GP28 und GP13 sind Slice 6 | tut nichts; `GP28: TEILT PWM-SLICE MIT PROG-PORT GP13` unter der Zahl |
| ausgegraut, `KANAL VON GP0` | unter SERVO PWM und MOTOR PWM: der Pin teilt sich ein PWM-Compare-Register mit einem als PWM gebundenen Pin (GP0 und GP16, GP1 und GP17, GP2 und GP18, GP4 und GP20, GP5 und GP21, GP6 und GP22) | tut nichts; `GP16: GLEICHER PWM-KANAL WIE GP0` unter der Zahl |
| frei, `PAD 7` | nicht gebunden | bindet ihn |

![PPM mit bereits gebundenen Servo-Pins](view:outputs-full)

Kann das gewählte Protokoll keine Pins mehr nehmen, steht der Grund in Amber unter der Zahl: `BENÖTIGT 8 KANÄLE, 4 FREI`, `ALLE 8 PLÄTZE BELEGT` oder `SERVO PWM BELEGT 8 PINS`.

![GP13 vom Programmierport gehalten](view:outputs-prog)

Eine gespeicherte Bindung, die GP13 hält, behält ihn: Der Programmierport hat dann keinen Pin, und der [Servo-Programmierer](servo-programmer.md) sagt das bei VERBINDEN.

## Die Übersicht: OFF

![OFF zeigt das Protokoll jedes gebundenen Pins](view:outputs-off)

Jeder Besuch von OUTPUTS öffnet mit OFF gewählt. OFF bindet nichts und löst nichts. Jeder gebundene Pin zeigt den Namen seines Protokolls; die ganze Bindung und jeder Pin, den sie treibt, stehen auf einem Bildschirm. Ein Tipp auf einen Pin tut nichts, und die Zeile unter der Liste lautet `PROTOKOLL WÄHLEN, DANN PIN TIPPEN`. Das Öffnen des Bildschirms schreibt nichts an den Koprozessor. OFF bleibt gewählt, bis ein Protokoll gewählt wird, auch über das Verlassen des Bildschirms. Ein Wechsel der Platine, etwa ein Koprozessor, der während des Besuchs antwortet, oder einer, der gegen einen anderen getauscht wird, wählt OFF, was auch gewählt war; ein zuvor gewähltes Protokoll geht also nicht auf ihn über. Ein Protokoll wählen, um seine Pins zu ändern; OFF wählen, um hierher zurückzukehren. [PICK A PIN](picker.md) öffnet ebenfalls mit OFF gewählt.

## LETZTER SCHREIBVORGANG

Jeder Haken wird sofort geschrieben. Danach liest das Panel die Seiten des Koprozessors zurück und zeigt, was er hält.

| LETZTER SCHREIBVORGANG | Bedeutung |
|---|---|
| `NICHT GESCHRIEBEN` | seit dem Start des Panels nichts geschrieben |
| `GESCHRIEBEN` | der Koprozessor hat das Schreiben bestätigt |
| `KEINE VERBINDUNG` | ein Austausch bekam binnen 1000 ms keine Antwort, oder der Link steht nicht |
| `ABGELEHNT` | der Koprozessor hat abgelehnt: ein Pin, den er nicht binden kann, eine Änderung bei ARMED oder eine Frame Rate, die er nicht zurücksetzen konnte |

Ein Koprozessor mit Protokoll 4.10 oder neuer übernimmt die Bindung als Ganzes: `KEINE VERBINDUNG` und `ABGELEHNT` lassen die gültige Bindung, wie sie war.

## Eine Bindung, die nicht bestätigt ist

![Die zuletzt gelesene Bindung nach einem gescheiterten Lesen](view:outputs-unread)

Der Bildschirm ändert eine Bindung erst, nachdem er sie gelesen hat. Solange eine Seite nicht gelesen wurde, behält der Bildschirm die zuletzt gelesene Bindung, gedimmt, mit `ZUORDNUNG NICHT AUSGELESEN. ÄNDERN GESPERRT.`. Ein Tipp auf einen Pin tut nichts, hier und auf PICK A PIN. Das Panel liest bei jedem Link-Aufbau und alle 500 ms erneut.

![Seiten, die keine Bindung beschreiben](view:outputs-odd)

Werden die Seiten gelesen, beschreiben aber keine Bindung, die der Bildschirm kennt, zum Beispiel ein Pin in zwei Slots, lautet er `KEINE GÜLTIGE ZUORDNUNG IN DEN PAGES` und bietet `HALTEN: ALLE PIN-ZUORDNUNGEN LÖSCHEN` an. 2 s gehalten schreibt es eine Bindung ohne Pin, und Änderungen werden wieder angenommen. Ein Finger, der die Taste verlässt, ein Abheben vor 2 s oder das Verlassen des Bildschirms bricht das Halten ab.

## Der Koprozessor behält die Bindung

Der Koprozessor speichert die Bindung in seinem eigenen Flash und stellt sie beim Start wieder her; das Panel speichert nie eine. Eine wiederhergestellte Bindung belegt ihre Pins und hält sie in Ruhe, bis der Prüfstand auf ARMED geschaltet wird. Ein Stromausfall während des Speicherns lässt die Bindung von davor.

## Verwandte Seiten

- [Pin-Auswahl](picker.md): dieselbe Bindung auf der Platine.
- [Motor und ESC](motor.md#ziel): die Pins, die der Throttle treibt.
- [Servo-Optionen](servo-settings.md#die-zeile-ziel): die Pins, die der Servotester treibt.
