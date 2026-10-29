---
screen: PICKER
title: Pin-Auswahl
order: 63
---

# Pin-Auswahl

PICK A PIN zeigt die Platine des IOmcu, wie sie auf dem Prüfstand liegt, und bindet einen Pin über eine Taste an einer Leiterbahn zu seinem Pad. Der Bildschirm hält dieselbe Bindung wie [Ausgänge](outputs.md): Die Liste beantwortet, welcher GPIO (General-Purpose Input/Output) gebunden ist, die Pin-Auswahl, wo das Kabel hin muss. Er öffnet über PICK A PIN auf [SETUP](setup.md), und sein Home-Tag führt dorthin zurück; über der offenen Protokollliste schließt es zuerst die Liste, ohne zu wählen. Auf dem [rbmod-Carrier](https://github.com/subtilitas/rcbench/wiki/Carrier-de) sind die Pins verlötet, und die Kachel ist nicht auf SETUP. Jeder Besuch öffnet mit OFF gewählt: Keine Taste bindet oder löst einen Pin, bis ein Protokoll in der Liste gewählt ist, und die Zeile unter der Liste lautet `PROTOKOLL WÄHLEN, DANN PIN TIPPEN`.

## Die Platine und ihre Tasten

![Die Pin-Auswahl mit drei als SERVO PWM gebundenen Pins](view:picker)

Ein Pad misst unter 40px, weniger als eine Fingerkuppe. Deshalb hat jeder Pin eine Taste, 50 x 56px, an einer geraden Leiterbahn zu seinem Pad: zwei versetzte Reihen über der Platine und zwei darunter.

| Taste | Pin | Ein Tipp |
|---|---|---|
| elfenbein oder ebenholz | frei | bindet ihn an das gewählte Protokoll |
| Akzentfarbe | im gewählten Protokoll gebunden | gibt ihn frei |
| grau | von einem anderen Protokoll oder dem Programmierport gehalten, das Protokoll ist voll, oder die Bindung ist nicht bestätigt | ändert nichts |
| keine Taste, rotes Kreuz auf dem Pad | reserviert: GPIO 3 Heartbeat, GPIO 8 bis GPIO 12 CAN (Controller Area Network) | keiner |

Ein Tipp auf eine Taste schreibt die Bindung sofort, mit denselben Ergebnissen wie ein Haken unter Ausgänge.

Aktuelle Einschränkung: Auf einer Platine mit einem Pin in der oberen linken Ecke ihres Umrisses liegen die Tasten der oberen Reihen unter der Protokoll-Liste, der Anzahl und dem Grund. Ein Tipp auf den Teil einer solchen Taste über der Protokoll-Liste öffnet die Liste, statt den Pin zu nehmen. Der RP2350-CAN hat dort keinen Pin.

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
| `GPIO 28: TEILT PWM-SLICE MIT PROG-PORT GPIO 13` | ein Tipp auf GPIO 28 unter SERVO PWM oder MOTOR PWM, solange der Programmierport GPIO 13 hält: ein PWM-Slice; die Taste ist grau |
| `GPIO 16: GLEICHER PWM-KANAL WIE GPIO 0` | ein Tipp auf GPIO 16 unter SERVO PWM oder MOTOR PWM bei GPIO 0 als PWM gebunden: ein PWM-Compare-Register; die Taste ist grau |

![Acht von acht Slots belegt](view:picker-full)

## PINS und BELEGT

PINS, links abwärts, listet die Pins des gewählten Protokolls in Kanalreihenfolge mit der auf der Platine gedruckten Pad-Nummer. BELEGT, rechts abwärts, listet die Pins, die andere Protokolle halten, jede Gruppe unter dem Namen ihres Protokolls. Mit OFF gewählt fehlt PINS, denn OFF hält keinen Pin, und BELEGT führt jeden gebundenen Pin.

![GPIO 13 vom Programmierport gehalten](view:picker-prog)

Der Pin des Programmierports, ab Werk GPIO 13 (Pad 17), ist grau, solange kein Protokoll ihn hält, und BELEGT listet ihn zuerst unter `PROG-PORT`. Jeder weitere Port, den der IOmcu meldet, hält seinen Pin ebenso, unter demselben Namen gelistet.

## Der Pin eines Programmierports

![PROG-PORT 3 gewählt](view:picker-port)

Die Protokollliste endet mit den Einträgen PROG-PORT, wie auf [Outputs](outputs.md), deren Taste PIN diesen Bildschirm auf ihrem Port öffnet. Mit einem gewählten Port setzt ein Tipp den Pin dieses Ports: eine freie Taste erbittet beim IOmcu diesen Pin, die hervorgehobene Taste des Ports erbittet keinen. Der Pin eines anderen Ports, eines Protokolls und einer Schnittstelle bleibt grau. Einem SERVO-PWM-Port wird ein Pin auf dem PWM-Slice eines gebundenen PWM-Ausgangs verweigert, `GPIO 5: TEILT PWM-SLICE MIT OUTPUT GPIO 4`, und einer auf dem Compare-Register eines anderen SERVO-PWM-Ports, `GPIO 28: GLEICHER PWM-KANAL WIE GPIO 12`. Ein Port mit KEINE nimmt jede freie Taste. Die Zählzeile nennt die Funktion des Ports, `FUNKTION: SERVO PWM`. Die Bitte wartet auf einen Prüfstand in Ruhe; die gelbe Zeile sagt, wie sie steht, wie auf Outputs. Bevor die Ports des IOmcu gelesen sind, ist jede Taste grau unter `PORTS NOCH NICHT GELESEN`.

![Die Pins der Anschlüsse gehalten und unter BELEGT genannt](view:picker-iface)

Ein Pin, den [SETUP > INTERFACES](setup-interfaces.md) hält, ist ebenso grau, und BELEGT listet ihn nach dem Programmierport unter `SENSORBUS`, `PD MINI` oder `PHASENABGRIFF`. Eine Tabelle mit mehr Zeilen, als passen, endet in `+N`, N die ausgelassenen Zeilen.

![OFF gewählt: jeder gebundene Pin unter BELEGT](view:picker-off)

## Die Platinenzeichnung

Der IOmcu trägt eine Zeichnung von sich selbst: Umriss, Bauteile und jedes Pad, maßstäblich in 0,01mm. Das Panel lädt sie über den Link, hält sie im Flash und zeigt sie nur, wenn sie zu Form und Pin-Katalog der Platine passt.

![Die Pin-Auswahl, allein aus der Form gezeichnet](view:picker-drawn)

Ohne Zeichnung, oder solange die Übertragung nicht fertig ist, werden Umriss und Pads aus der Form der Platine gezeichnet, und die Tasten liegen an denselben Stellen. Eine Platine, die ihre Form nicht meldet, wird nicht gezeichnet: Der Bildschirm zeigt `KEIN PLATINENBILD VERFÜGBAR`, und ihre Pins stehen unter Ausgänge.

Die Zeichnung des RP2350-CAN hat 1.560 Bytes, geladen in 26 Blöcken zu 62 Bytes. Die Dauer ist nicht gemessen. Das Schreiben in den Flash wartet auf einen Prüfstand, der SAFE ist und dessen Netzteil aus ist.

## Verwandte Seiten

- [Ausgänge](outputs.md): Protokolle, LETZTER SCHREIBVORGANG und eine Bindung, die nicht bestätigt ist.
- [Setup](setup.md)
