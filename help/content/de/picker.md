---
screen: PICKER
title: Pin-Auswahl
order: 63
---

# Pin-Auswahl

PICK A PIN zeigt die Platine des IOmcu, wie sie auf dem Prüfstand liegt, und bindet einen Pin über eine Taste an einer Leiterbahn zu seinem Pad. Der Bildschirm hält dieselbe Bindung wie [Ausgänge](outputs.md): Die Liste beantwortet, welcher GPIO (General-Purpose Input/Output) gebunden ist, die Pin-Auswahl, wo das Kabel hin muss. Er öffnet über PICK A PIN auf [SETUP](setup.md). Jeder Besuch öffnet mit OFF gewählt: Keine Taste bindet oder löst einen Pin, bis ein Protokoll in der Liste gewählt ist, und die Zeile unter der Liste lautet `PROTOKOLL WÄHLEN, DANN PIN TIPPEN`.

## Die Platine und ihre Tasten

![Die Pin-Auswahl mit drei als SERVO PWM gebundenen Pins](view:picker)

Ein Pad misst unter 40 px, weniger als eine Fingerkuppe. Deshalb hat jeder Pin eine Taste, 43 px breit, an einer geraden Leiterbahn zu seinem Pad: zwei versetzte Reihen über der Platine und zwei darunter.

| Taste | Pin | Ein Tipp |
|---|---|---|
| elfenbein oder ebenholz | frei | bindet ihn an das gewählte Protokoll |
| Akzentfarbe | im gewählten Protokoll gebunden | gibt ihn frei |
| grau | von einem anderen Protokoll oder dem Programmierport gehalten, das Protokoll ist voll, oder die Bindung ist nicht bestätigt | ändert nichts |
| keine Taste, rotes Kreuz auf dem Pad | reserviert: GP3 Heartbeat, GP8 bis GP12 CAN (Controller Area Network) | keiner |

Ein Tipp auf eine Taste schreibt die Bindung sofort, mit denselben Ergebnissen wie ein Haken unter Ausgänge.

Die Platine markiert auch die anderen Adern eines Servokabels:

| Marke | Pad |
|---|---|
| weiße runde Marke mit **G** | Masse |
| amberfarbene runde Marke mit **5V0** oder **3V3** | eine Schiene mit dieser Spannung |
| amberfarbene runde Marke mit **PWR** | VSYS, eine Schiene, die ein Eingang ist |
| blasses Kreuz auf dem Pad | keins von beiden, etwa RUN und 3V3_EN |

## Die Protokollliste und die Zähler

![Die Protokollliste über der Platine](view:picker-protocol)

Oben links steht die Protokollliste, dasselbe Bedienelement wie unter Ausgänge. Eine Wahl bestimmt das Protokoll, zu dem ein Tipp auf eine Taste kommt, und schreibt nichts. Ein Tipp außerhalb der offenen Liste schließt sie.

Unter der Liste zählt `4 VON 8 PLÄTZEN BELEGT` die Pins über alle Protokolle. Darunter steht in Amber der Grund, warum keine Taste hinzukommen kann:

| Zeile | Wann |
|---|---|
| `ALLE 8 PLÄTZE BELEGT` | 8 Pins sind über alle Protokolle gebunden |
| `BENÖTIGT 8 KANÄLE, 4 FREI` | PPM, mit 4 gebundenen Servo-Pins |
| `SERVO PWM BELEGT 8 PINS` | das gewählte Protokoll hält so viele Pins, wie es nimmt |
| `PROTOKOLL WÄHLEN, DANN PIN TIPPEN` | OFF ist gewählt |
| `NICHT AUSGELESEN` | die Bindung ist nicht bestätigt; jede Taste ist grau |
| `GP28: TEILT PWM-SLICE MIT PROG-PORT GP13` | ein Tipp auf GP28 unter SERVO PWM oder MOTOR PWM, solange der Programmierport GP13 hält: ein PWM-Slice; die Taste ist grau |
| `GP16: GLEICHER PWM-KANAL WIE GP0` | ein Tipp auf GP16 unter SERVO PWM oder MOTOR PWM bei GP0 als PWM gebunden: ein PWM-Compare-Register; die Taste ist grau |

![Acht von acht Slots belegt](view:picker-full)

## PINS und BELEGT

PINS, links abwärts, listet die Pins des gewählten Protokolls in Kanalreihenfolge mit der auf der Platine gedruckten Pad-Nummer. BELEGT, rechts abwärts, listet die Pins, die andere Protokolle halten, jede Gruppe unter dem Namen ihres Protokolls. Mit OFF gewählt fehlt PINS, denn OFF hält keinen Pin, und BELEGT führt jeden gebundenen Pin.

![GP13 vom Programmierport gehalten](view:picker-prog)

Der Pin des Programmierports, ab Werk GP13 (Pad 17), ist grau, solange kein Protokoll ihn hält, und BELEGT listet ihn zuerst unter `PROG-PORT`.

![OFF gewählt: jeder gebundene Pin unter BELEGT](view:picker-off)

## Die Platinenzeichnung

Der IOmcu trägt eine Zeichnung von sich selbst: Umriss, Bauteile und jedes Pad, maßstäblich in 0,01 mm. Das Panel lädt sie über den Link, hält sie im Flash und zeigt sie nur, wenn sie zu Form und Pin-Katalog der Platine passt.

![Die Pin-Auswahl, allein aus der Form gezeichnet](view:picker-drawn)

Ohne Zeichnung, oder solange die Übertragung nicht fertig ist, werden Umriss und Pads aus der Form der Platine gezeichnet, und die Tasten liegen an denselben Stellen. Eine Platine, die ihre Form nicht meldet, wird nicht gezeichnet: Der Bildschirm zeigt `KEIN PLATINENBILD VERFÜGBAR`, und ihre Pins stehen unter Ausgänge.

Die Zeichnung des RP2350-CAN hat 1.560 Bytes, geladen in 26 Blöcken zu 62 Bytes. Die Dauer ist nicht gemessen. Das Schreiben in den Flash wartet auf einen Prüfstand, der SAFE ist und dessen Netzteil aus ist.

## Verwandte Seiten

- [Ausgänge](outputs.md): Protokolle, LETZTER SCHREIBVORGANG und eine Bindung, die nicht bestätigt ist.
- [Setup](setup.md)
