---
screen: OUTPUTS
title: Ausgänge
order: 62
---

# Ausgänge

OUTPUTS bindet die Pins des IOmcu an Ausgabeprotokolle: welche Pins Servos treiben, welche ESCs (Electronic Speed Controller), und mit welchem Signal. Der Bildschirm öffnet über OUTPUTS auf [SETUP](setup.md) auf der [Übersicht](#die-übersicht-off): OFF gewählt und jeder gebundene Pin mit seinem Protokoll benannt. Über ZIEL auf [MOTOR & ESC](motor.md#ziel) öffnet er auf dem Protokoll, an das die Gas-Pins gebunden sind (der Übersicht, solange sie an mehrere gebunden sind oder keines bekannt ist), und über ZIEL auf [SERVO](servo-settings.md#die-zeile-ziel) auf dem, was diese Zeile zeigt: dem [Eintrag PROG-PORT](#der-eintrag-prog-port), solange der IOmcu den Port treibt, SERVO PWM, solange er die Ausgänge treibt, der Übersicht, solange das Ziel nicht bekannt ist. Das Home-Tag führt zurück zu dem Bildschirm, aus dem er geöffnet wurde; über der offenen Protokollliste schließt es zuerst die Liste, ohne zu wählen. Eine Platine mit Zeichnung oder Form zeigt [ihre Tasten](#die-platine-und-ihre-tasten): Die Liste beantwortet, welcher GPIO (General-Purpose Input/Output) gebunden ist, die Platine, wo das Kabel hin muss. Eine Platine ohne beides und der rbmod-Carrier zeigen [ein Raster der Pins](#die-rasteransicht).

## Die Platine und ihre Tasten

![Drei als SERVO PWM gebundene Pins auf der Platine](view:outputs)

Ein Pad misst unter 40px, weniger als eine Fingerkuppe. Deshalb hat jeder Pin eine Taste, 50 x 56px, an einer geraden Leiterbahn zu seinem Pad: zwei versetzte Reihen über der Platine und zwei darunter.

| Taste | Pin | Ein Tipp |
|---|---|---|
| elfenbein oder ebenholz | frei | bindet ihn an das gewählte Protokoll |
| Akzentfarbe | im gewählten Protokoll gebunden | gibt ihn frei |
| grau | von einem anderen Protokoll oder dem Programmierport gehalten, das Protokoll ist voll, oder die Bindung ist nicht bestätigt | ändert nichts |
| keine Taste, rotes Kreuz auf dem Pad | reserviert: GPIO 3 Heartbeat, GPIO 8 bis GPIO 12 CAN (Controller Area Network) | keiner |

Ein Tipp auf eine Taste schreibt die Bindung sofort; [LETZTER SCHREIBVORGANG](#letzter-schreibvorgang) sagt, was daraus wurde.

Aktuelle Einschränkung: Auf einer Platine mit einem Pin in der oberen linken Ecke ihres Umrisses liegen die Tasten der oberen Reihen unter der Protokoll-Liste, der Anzahl und dem Grund. Ein Tipp auf den Teil einer solchen Taste über der Protokoll-Liste öffnet die Liste, statt den Pin zu nehmen. Der RP2350-CAN hat dort keinen Pin.

Die Platine markiert auch die anderen Adern eines Servokabels:

| Marke | Pad |
|---|---|
| weiße runde Marke mit **G** | Masse |
| amberfarbene runde Marke mit **5V0** oder **3V3** | eine Schiene mit dieser Spannung |
| amberfarbene runde Marke mit **PWR** | VSYS, eine Schiene, die ein Eingang ist |
| blasses Kreuz auf dem Pad | keins von beiden, etwa RUN und 3V3_EN |

## Pins binden

1. Ein Protokoll in der Liste wählen. Bis dahin tut ein Tipp auf eine Taste nichts.
2. Eine Taste antippen. Der Tipp bindet den Pin sofort an das Protokoll; es gibt keine Taste zum Übernehmen.
3. Eine hervorgehobene Taste antippen, um ihren Pin freizugeben.
4. OFF wählen, um zur Übersicht zurückzukehren.

Ein Pin gehört höchstens einem Protokoll. Mehrere Protokolle können gleichzeitig gebunden sein.

Jeder gebundene Pin ist ein Slot der OUTPUTS-Seite des IOmcu, in Pin-Reihenfolge über alle Protokolle. Der niedrigste gebundene Pin ist Kanal 0. Es gibt 8 Slots und 8 Kanäle, geteilt von allen Protokollen.

## Die Protokolle

![Die Protokollliste](view:outputs-protocol)

| Protokoll | Signal | Pins |
|---|---|---|
| OFF | bindet nichts; zeigt die ganze Bindung | 0 |
| SERVO PWM | PWM (Pulsweitenmodulation) mit 50Hz, für ein Servo oder ein Ruder | bis 8 |
| MOTOR PWM | derselbe Puls mit 50Hz, für einen ESC; von Leerlaufpuls bis Vollpuls | bis 8 |
| PPM | PPM (Pulspositionsmodulation): 8 Kanäle auf einem Pin mit 40Hz | 1 |
| DSHOT300, DSHOT600 | digitaler DShot-Throttle mit 300 oder 600kbit/s | bis 8 |
| DSHOT300 BIDIR, DSHOT600 BIDIR | DShot mit Drehzahl-Telemetrie vom ESC auf derselben Leitung | bis 8 |

Ein Servokanal zentriert, wenn nichts ihn ansteuert; ein Motorkanal stoppt. Der Puls kann nicht sagen, was am Pin hängt, also sagt es das Protokoll. Der Throttle von MOTOR & ESC treibt jeden Kanal, der als MOTOR PWM oder mit einem DShot-Eintrag gebunden ist, und lässt die Servokanäle in Ruhe. PPM braucht 8 Kanäle; ein Prüfstand mit gebundenem PPM hat für nichts anderes Platz.

![Die längsten Protokollnamen](view:outputs-bidir)

Das Wählen eines Protokolls schreibt nichts. Es bestimmt, welche Tasten hervorgehoben erscheinen und zu welchem Protokoll der nächste Tipp kommt. Die Pins der anderen Protokolle bleiben gebunden.

## Leerlaufpuls und Vollpuls

Ein MOTOR-PWM-Kanal sendet 0% Throttle als Leerlaufpuls und 100% als Vollpuls, ab Werk 1000us und 2000us. Beide stehen unter [MOTOR & ESC, OPTIONEN](motor-settings.md), in Schritten von 10us: Leerlaufpuls 800 bis 1600us, Vollpuls 1400 bis 2400us. Ein SERVO-PWM-Kanal behält 1000 bis 2000us, oder den Bereich, den SERVO sendet. Eine Änderung erreicht den IOmcu 300ms nach der letzten Änderung, solange der Prüfstand SAFE ist. Ein Leerlaufpuls, der nicht mindestens 100us unter dem Vollpuls liegt, wird nicht gesendet, und das Band meldet `Leerlaufpuls muss mindestens 100us unter Vollpuls liegen. Nicht gesendet.`.

Ein ESC, der an einem Sender kalibriert wurde, nimmt den kürzesten Puls dieses Senders als null. Leerlaufpuls auf oder unter diesen Puls setzen und Vollpuls auf oder über den längsten: Für einen Sender, der 985 bis 2012us sendet, 980us und 2020us.

## Die Protokollliste und die Zähler

![Die Protokollliste über der Platine](view:outputs-protocol)

Oben links steht die Protokollliste. Eine Wahl bestimmt das Protokoll, zu dem ein Tipp auf eine Taste kommt, und schreibt nichts. Ein Tipp außerhalb der offenen Liste schließt sie.

Unter der Liste zählt `4 VON 8 PLÄTZEN BELEGT` die Pins über alle Protokolle. Darunter steht in Amber der Grund, warum keine Taste hinzukommen kann:

| Zeile | Wann |
|---|---|
| `ALLE 8 PLÄTZE BELEGT` | 8 Pins sind über alle Protokolle gebunden |
| `BENÖTIGT 8 KANÄLE, 4 FREI` | PPM, mit 4 gebundenen Servo-Pins |
| `SERVO PWM BELEGT 8 PINS` | das gewählte Protokoll hält so viele Pins, wie es nimmt |
| `PROTOKOLL WÄHLEN, DANN PIN TIPPEN` | OFF ist gewählt |
| `NICHT AUSGELESEN` | die Bindung ist nicht bestätigt; jede Taste ist grau |
| `KEINE GÜLTIGE ZUORDNUNG IN DEN PAGES` | die Seiten beschreiben keine Bindung; jede Taste ist grau |
| `GPIO 28: TEILT PWM-SLICE MIT PROG-PORT GPIO 13` | ein Tipp auf GPIO 28 unter SERVO PWM oder MOTOR PWM, solange der Programmierport GPIO 13 hält: ein PWM-Slice; die Taste ist grau |
| `GPIO 16: GLEICHER PWM-KANAL WIE GPIO 0` | ein Tipp auf GPIO 16 unter SERVO PWM oder MOTOR PWM bei GPIO 0 als PWM gebunden: ein PWM-Compare-Register; die Taste ist grau |

![Acht von acht Slots belegt](view:outputs-full)

## PINS und BELEGT

PINS, links abwärts, listet die Pins des gewählten Protokolls in Kanalreihenfolge mit der auf der Platine gedruckten Pad-Nummer. BELEGT, rechts abwärts, listet die Pins, die andere Protokolle halten, jede Gruppe unter dem Namen ihres Protokolls. Mit OFF gewählt fehlt PINS, denn OFF hält keinen Pin, und BELEGT führt jeden gebundenen Pin.

![GPIO 13 vom Programmierport gehalten](view:outputs-prog)

Der Pin des Programmierports, ab Werk GPIO 13 (Pad 17), ist grau, solange kein Protokoll ihn hält, und BELEGT listet ihn zuerst unter `PROG-PORT`. Jeder weitere Port, den der IOmcu meldet, hält seinen Pin ebenso, unter demselben Namen gelistet.

![Die Pins der Anschlüsse gehalten und unter BELEGT genannt](view:outputs-iface)

Ein Pin, den [SETUP > SENSORS](setup-sensors.md) oder der [Tab PD MINI auf SUPPLY](supply.md#tab-pd-mini) hält, ist ebenso grau, und BELEGT listet ihn nach dem Programmierport unter `SENSORBUS`, `PD MINI` oder `PHASENABGRIFF`. Eine Tabelle mit mehr Zeilen, als passen, endet in `+N`, N die ausgelassenen Zeilen.

![OFF gewählt: jeder gebundene Pin unter BELEGT](view:outputs-off)

## Der Eintrag PROG-PORT

![Der Eintrag PROG-PORT](view:outputs-port)

Die offene Protokollliste zeigt einen Eintrag PROG-PORT je Programmierport, den der IOmcu meldet, in einer zweiten Spalte neben den Protokollen: 4 ab IOmcu 0.22.0, 1 bei einem älteren Build, und einer für Port 1, solange die Anzahl nicht bekannt ist. Ein IOmcu älter als 0.17.0 hat keinen Programmierport und keinen Eintrag. Ein Programmierport ist ein Ausgang eigener Art, kein Protokoll und kein Slot. Jeder Eintrag hat dieselben Zeilen, in der Spalte links der Platine:

| Zeile | Zeigt und tut |
|---|---|
| PIN | `PIN GPIO 13` mit seinem Pad, `PIN WÄHLEN`, `PIN GPIO 14 ABGELEHNT` oder `KEIN PIN BEKANNT`. Die Tasten auf der Platine setzen ihn: eine freie Taste erbittet beim IOmcu diesen Pin, die hervorgehobene Taste des Ports erbittet keinen |
| FUNKTION | was der Pin trägt, solange keine Programmiersitzung ihn hält; ein Tipp auf die Fläche öffnet die Liste über der Spalte. SERVO PWM ist der Servopuls auf einem PWM-Pin; KEINE lässt den Pin einen Eingang mit seinem Pull-down, auf jedem freien Pin; DSHOT150, DSHOT300 und DSHOT600 (IOmcu ab 0.22.0) senden DShot mit dem Gas von MOTOR & ESC, ein Frame alle 1ms, solange für das Gas ARMED, auf jedem freien Pin |
| ÜBERWACHT VON | der INA3221-Kanal, der den Port in SETUP > SENSORS überwacht, `ÜBERWACHT VON INA3221 0x41 CH2`; `VON KEINEM STROMKANAL ÜBERWACHT`, solange keiner es tut |
| SERVO-TESTER | OUTPUTS oder PORT n: was der Tester von [SERVO](servo.md) treibt. Die Zeile ZIEL von SERVO öffnet OUTPUTS hier, solange der IOmcu den Port treibt |
| PORT-PWM | EIN oder AUS: der Puls des Ports, der das Ziel des Testers ist |

Der IOmcu hält Pin und Funktion jedes Ports in seinem Flash, mit der Bindung der Ausgänge: sie gehören zur Platine. Port 1 beginnt als SERVO PWM auf GPIO 13, die Ports 2 bis 4 als KEINE ohne Pin. Ein Tipp auf FUNKTION und ein Tipp auf eine Taste sind Bitten: das Panel schreibt sie bei einem Prüfstand in Ruhe, und die gelbe Zeile sagt, wie sie stehen.

![PROG-PORT 2, die Bitte wartet](view:outputs-port2)

| Zeile unter den Zeilen | Bedingung |
|---|---|
| `GPIO 5: TEILT PWM-SLICE MIT OUTPUT GPIO 4` | eine Taste getippt auf dem PWM-Slice eines gebundenen PWM-Ausgangs, bei einem SERVO-PWM-Port; die Taste ist grau |
| `GPIO 28: GLEICHER PWM-KANAL WIE GPIO 12` | eine Taste getippt auf dem Compare-Register eines anderen SERVO-PWM-Ports; die Taste ist grau |
| `GESPERRT: DER SERVO-PROGRAMMIERER BELEGT DEN PORT` | der [Servo-Programmierer](servo-programmer.md) hat einen Port |
| `WARTET AUF SAFE, KEINEN TEST, SWEEP ODER SERVO-PROGRAMMIERER` | eine Bitte für diesen Port wartet auf einen Prüfstand in Ruhe |
| `DER IOMCU HAT ES ABGELEHNT` | der IOmcu hat die letzte Bitte abgelehnt oder nicht beantwortet |
| `VERBINDUNG VOR DEM SCHREIBEN VERLOREN` | die Verbindung brach ab, bevor die Bitte geschrieben war |
| `PORTS NOCH NICHT GELESEN` | die Ports des IOmcu seit dem Linkaufbau nicht gelesen |
| `GESPERRT, SOLANGE ARMED ODER EIN TEST ODER SWEEP LÄUFT` | der Prüfstand ist ARMED, oder ein Sweep oder ein Test läuft; PORT-PWM geht trotzdem aus |
| `SERVO-TESTER STEUERT PROG-PORT 3` | der Tester steuert einen anderen Port |
| `DER SERVO-TESTER BRAUCHT FUNKTION SERVO PWM` | der Port ist KEINE oder DShot |
| `IOMCU ÄLTER ALS 0.22.0: NUR SERVO PWM` | der IOmcu hat keine FUNKTION |
| `PORT-PWM BRAUCHT PROG-PORT ALS ZIEL DES TESTERS` | PORT-PWM auf einem Port, der nicht das Ziel des Testers ist |

Das Wählen des Eintrags schreibt nichts an den IOmcu, und LETZTER SCHREIBVORGANG wird auf ihm nicht gezeigt. Die zwei Einstellungen des Testers hält das Panel, und sie erreichen den IOmcu bei einem Prüfstand in Ruhe. Bei mehr als einem Port heißen die Einträge `PROG-PORT 1` bis `PROG-PORT 4`.

Der Pin eines anderen Ports, eines Protokolls und einer Schnittstelle bleibt grau. Ein Port mit KEINE nimmt jede freie Taste. Bevor die Ports des IOmcu gelesen sind, ist jede Taste grau.

![PROG-PORT 3, ein Port ohne Pin](view:outputs-port3)

## Die Rasteransicht

Eine Platine, die weder Zeichnung noch Form meldet, lässt sich nicht zeichnen: Ein geratenes Bild zeigte ebenso überzeugt auf das falsche Pad wie auf das richtige. OUTPUTS zeigt ihre Pins dann als Raster, 4 Spalten zu 7 Zellen, und in der linken Spalte die Protokollliste, die Zahl `3 VON 8 PINS` und LETZTER SCHREIBVORGANG. Ein Tipp auf eine Zelle hakt sie ab. Der [rbmod-Carrier](#auf-dem-rbmod-carrier) hat dasselbe Raster. Darauf wird keine Katze gezeichnet. Die Zeile PIN eines Eintrags PROG-PORT nimmt dort keinen Tipp: Nur die Tasten einer gezeichneten Platine setzen den Pin eines Ports.

![Drei Pins auf einer Platine ohne Form](view:outputs-grid)

| Zelle | Bedeutung | Ein Tipp |
|---|---|---|
| abgehakt, `PAD 6` | im gewählten Protokoll gebunden; die auf der Platine gedruckte Pad-Nummer | gibt ihn frei |
| ausgegraut, der Name des anderen Protokolls | von einem anderen Protokoll gehalten | tut nichts |
| rot, durchgestrichen | reserviert: GPIO 3 trägt den Sicherheits-Heartbeat, GPIO 8 bis GPIO 12 den CAN-Controller (Controller Area Network) | tut nichts |
| `PROG-PORT` | der Pin des Programmierports des IOmcu, ab Werk GPIO 13 | tut nichts |
| rot, `SENSORBUS`, `PD MINI` oder `PHASENABGRIFF` | ein Pin, den [SETUP > SENSORS](setup-sensors.md) oder der [Tab PD MINI auf SUPPLY](supply.md#tab-pd-mini) hält: Sensor-SDA und -SCL, solange INA228 oder AS5600 EIN ist oder ein INA3221 unter [SETUP > SENSORS](setup-sensors.md) bestückt ist; PD mini TX und RX, solange der PD mini EIN ist und beide Pins gesetzt sind; der Abgriff-Pin, solange der Phasenabgriff EIN ist. Die Zelle folgt diesen Einstellungen | tut nichts |
| ausgegraut, `SLICE VON GPIO 13` | unter SERVO PWM und MOTOR PWM: der Pin teilt sich einen PWM-Slice mit dem Pin eines Programmierports, jedes Ports, den der IOmcu meldet; GPIO 28 und GPIO 13 sind Slice 6 | tut nichts; `GPIO 28: TEILT PWM-SLICE MIT PROG-PORT GPIO 13` unter der Zahl |
| ausgegraut, `KANAL VON GPIO 0` | unter SERVO PWM und MOTOR PWM: der Pin teilt sich ein PWM-Compare-Register mit einem als PWM gebundenen Pin (GPIO 0 und GPIO 16, GPIO 1 und GPIO 17, GPIO 2 und GPIO 18, GPIO 4 und GPIO 20, GPIO 5 und GPIO 21, GPIO 6 und GPIO 22) | tut nichts; `GPIO 16: GLEICHER PWM-KANAL WIE GPIO 0` unter der Zahl |
| frei, `PAD 7` | nicht gebunden | bindet ihn |

Kann das gewählte Protokoll keine Pins mehr nehmen, steht der Grund in Amber unter der Zahl: `BENÖTIGT 8 KANÄLE, 4 FREI`, `ALLE 8 PLÄTZE BELEGT` oder `SERVO PWM BELEGT 8 PINS`.

Eine gespeicherte Bindung, die GPIO 13 hält, behält ihn: Der Programmierport hat dann keinen Pin, und der [Servo-Programmierer](servo-programmer.md) sagt das bei VERBINDEN.

## Auf dem rbmod-Carrier

![PWM01 bis PWM04, PWM03 wegen Slice 2 abgelehnt](view:outputs-rbmod)

Der [rbmod-Carrier](https://github.com/subtilitas/rcbench/wiki/Carrier-de) verlötet jede Leitung auf ihren Pin, hier wird also kein Pin gewählt. Das Raster zeigt die vier Servoausgänge, PWM01 bis PWM04 auf GPIO 2, 4, 5 und 1, jeder mit seinem GPIO unter dem Namen. Ein Tipp gibt dem Ausgang das gewählte Protokoll, wie ein Haken einem Pin. Slot n ist PWM0(n+1): PWM01 ist Kanal 0. Ein Protokoll belegt höchstens die 4 Ausgänge, `SERVO PWM BELEGT 4 PINS`.

PWM02 und PWM03 sind ein PWM-Slice und laufen mit einer Framerate. Ein Flächenprotokoll (SERVO PWM) und ein Gasprotokoll (MOTOR PWM) werden auf beiden zusammen nicht angeboten: die Zelle zeigt `SLICE VON PWM02` und ein Tipp darauf `PWM03 GPIO 5: GLEICHER PWM-SLICE WIE PWM02 GPIO 4`. DShot läuft auf einer eigenen Zustandsmaschine und wird neben beiden angeboten.

![PROG PORT 1 auf dem Carrier](view:outputs-rbmod-port)

Die Einträge PROG PORT nennen den Pin jedes Ports, `PROGP01 AUF GPIO 13`, und die Zeile PIN nimmt keinen Tipp. FUNKTION zeigt PROGRAMMIEREN und nimmt keinen Tipp: ein Programmierport trägt die Programmer von KST und Hitec und kein Signalprotokoll. SERVO-TESTER und PORT-PWM werden nicht gezeigt. Die Zeile unter den Zeilen lautet `NUR KST- UND HITEC-PROGRAMMER. SIGNALE: PWM01 BIS PWM04`.

## Die Übersicht: OFF

![OFF zeigt das Protokoll jedes gebundenen Pins](view:outputs-off)

Jeder Besuch von SETUP aus öffnet mit OFF gewählt. OFF bindet nichts und löst nichts. BELEGT listet jeden gebundenen Pin unter seinem Protokoll; die ganze Bindung und jeder Pin, den sie treibt, stehen auf einem Bildschirm. Ein Tipp auf eine Taste tut nichts, und die Zeile unter der Liste lautet `PROTOKOLL WÄHLEN, DANN PIN TIPPEN`. Das Öffnen des Bildschirms schreibt nichts an den IOmcu. OFF bleibt gewählt, bis ein Protokoll gewählt wird, auch über das Verlassen des Bildschirms. Ein Wechsel der Platine, etwa ein IOmcu, der während des Besuchs antwortet, oder einer, der gegen einen anderen getauscht wird, wählt OFF, was auch gewählt war; ein zuvor gewähltes Protokoll geht also nicht auf ihn über. Ein Protokoll wählen, um seine Pins zu ändern; OFF wählen, um hierher zurückzukehren.

## LETZTER SCHREIBVORGANG

Jeder Tipp auf eine Taste wird sofort geschrieben. LETZTER SCHREIBVORGANG steht rechts der Protokollliste, in der Rasteransicht am Fuß der linken Spalte; die Bitten eines Eintrags PROG-PORT melden sich stattdessen in seiner eigenen Zeile. Danach liest das Panel die Seiten des IOmcu zurück und zeigt, was er hält.

| LETZTER SCHREIBVORGANG | Bedeutung |
|---|---|
| `NICHT GESCHRIEBEN` | seit dem Start des Panels nichts geschrieben |
| `GESCHRIEBEN` | der IOmcu hat das Schreiben bestätigt |
| `KEINE VERBINDUNG` | ein Austausch bekam binnen 1000ms keine Antwort, oder der Link steht nicht |
| `ABGELEHNT` | der IOmcu hat abgelehnt: ein Pin, den er nicht binden kann, eine Änderung bei ARMED oder eine Frame Rate, die er nicht zurücksetzen konnte |

Ein IOmcu mit 0.15.0 oder neuer übernimmt die Bindung als Ganzes: `KEINE VERBINDUNG` und `ABGELEHNT` lassen die gültige Bindung, wie sie war.

## Die Platinenzeichnung

Der IOmcu trägt eine Zeichnung von sich selbst: Umriss, Bauteile und jedes Pad, maßstäblich in 0,01mm. Das Panel lädt sie über den Link, hält sie im Flash und zeigt sie nur, wenn sie zu Form und Pin-Katalog der Platine passt.

![Die Platine, allein aus der Form gezeichnet](view:outputs-shape)

Ohne Zeichnung, oder solange die Übertragung nicht fertig ist, werden Umriss und Pads aus der Form der Platine gezeichnet, und die Tasten liegen an denselben Stellen. Eine Platine, die ihre Form nicht meldet, wird nicht gezeichnet: OUTPUTS zeigt [die Rasteransicht](#die-rasteransicht).

Die Zeichnung des RP2350-CAN hat 1.560 Bytes, geladen in 26 Blöcken zu 62 Bytes. Die Dauer ist nicht gemessen. Das Schreiben in den Flash wartet auf einen Prüfstand, der SAFE ist und dessen Netzteil aus ist.

## Eine Bindung, die nicht bestätigt ist

![Die zuletzt gelesene Bindung nach einem gescheiterten Lesen](view:outputs-unread)

Der Bildschirm ändert eine Bindung erst, nachdem er sie gelesen hat. Solange eine Seite nicht gelesen wurde, behält der Bildschirm die zuletzt gelesene Bindung, jede Taste grau, mit `NICHT AUSGELESEN` (`ZUORDNUNG NICHT AUSGELESEN. ÄNDERN GESPERRT.` in der Rasteransicht). Ein Tipp auf eine Taste tut nichts. Das Panel liest bei jedem Link-Aufbau und alle 500ms erneut.

![Seiten, die keine Bindung beschreiben](view:outputs-odd)

Werden die Seiten gelesen, beschreiben aber keine Bindung, die der Bildschirm kennt, zum Beispiel ein Pin in zwei Slots, lautet er `KEINE GÜLTIGE ZUORDNUNG IN DEN PAGES` und bietet unter BELEGT `2s HALTEN ZUM LÖSCHEN ALLER PIN-ZUORDNUNGEN` an. Gehalten schreibt es eine Bindung ohne Pin, und Änderungen werden wieder angenommen. Ein Finger, der die Taste verlässt, ein Abheben vor 2s oder das Verlassen des Bildschirms bricht das Halten ab.

## Der IOmcu behält die Bindung

Der IOmcu speichert die Bindung in seinem eigenen Flash und stellt sie beim Start wieder her; das Panel speichert nie eine. Eine wiederhergestellte Bindung belegt ihre Pins und hält sie in Ruhe, bis der Prüfstand auf ARMED geschaltet wird. Ein Stromausfall während des Speicherns lässt die Bindung von davor.

## Verwandte Seiten

- [Setup](setup.md)
- [Motor und ESC](motor.md#ziel): die Pins, die der Throttle treibt.
- [Servo-Optionen](servo-settings.md#die-zeile-ziel): die Pins, die der Servotester treibt.
