---
screen: OUTPUTS
title: Ausgänge
order: 62
---

# Ausgänge

OUTPUTS ordnet IOmcu-Pins Ausgangsprotokollen zu. Damit legst du fest, welche Pins Servos oder ESCs (Electronic Speed Controller) ansteuern und welches Signal sie verwenden. Von [SETUP](setup.md) aus öffnet sich die [Übersicht](#die-übersicht-aus) mit AUS. Jeder zugeordnete Pin trägt seinen Protokollnamen.

Über ZIEL auf [MOTOR & ESC](motor.md#ziel) öffnet sich das Gasprotokoll, bei gemischten oder unbekannten Protokollen die Übersicht. Die [Taste PORT](servo-settings.md#die-taste-port) auf SERVO wählt den Port des Testers in einer eigenen Liste; ohne als SERVO PWM gebundenen Pin öffnet deren `OUTPUTS: KEIN PIN, OUTPUTS ÖFFNEN` hier das Protokoll SERVO PWM. Das Home-Feld führt zur aufrufenden Seite zurück. Eine offene Protokollliste schließt es zuerst ohne Auswahl.

Für Platinen mit Zeichnung oder Formbeschreibung erscheinen [Pin-Tasten](#die-platine-und-ihre-tasten). Die Liste nennt den GPIO (General-Purpose Input/Output), die Zeichnung zeigt den Anschlussort. Ohne beide Beschreibungen und auf dem rbmod-Carrier erscheint ein [Pin-Raster](#die-rasteransicht).

## Die Platine und ihre Tasten

![Drei als SERVO PWM gebundene Pins auf der Platine](view:outputs)

Pads sind weniger als 40px breit und damit kleiner als eine Fingerkuppe. Jedes erhält deshalb eine Taste von 50 x 56px, verbunden durch eine gerade Linie. Je zwei versetzte Tastenreihen liegen oberhalb und unterhalb der Platine.

| Taste | Pin | Wirkung eines Tipps |
|---|---|---|
| elfenbein- oder ebenholzfarben | frei | ordnet ihn dem gewählten Protokoll zu |
| Akzentfarbe | dem gewählten Protokoll zugeordnet | gibt ihn frei |
| grau, die Nummer in 16 x 28 über einem Tag | belegt: der Tag nennt, was den Pin hält, in höchstens 5 Zellen zu 8 x 16 (Tabelle unten) | Pin eines anderen Protokolls: fragt nach dem [Verschieben](#einen-pin-zwischen-protokollen-verschieben); Pin eines Ports oder einer Schnittstelle: nennt den Grund unter der Anzahl |
| grau, nur die Nummer | der Pin teilt einen PWM-Slice oder ein Compare-Register mit einem zugeordneten Pin oder dem Pin eines Ports, das Protokoll ist voll, oder die Zuordnung ist unbestätigt | keine Änderung; ein Tipp auf einen geteilten Slice oder ein geteiltes Register nennt den Grund unter der Anzahl |
| keine Taste, rotes Kreuz auf dem Pad | reserviert: GPIO 3 für Heartbeat, GPIO 8 bis GPIO 12 für CAN (Controller Area Network) | keine |

| Tag | Gehalten von |
|---|---|
| `ESC` | MOTOR PWM, DSHOT300, DSHOT600, DSHOT300 BIDIR oder DSHOT600 BIDIR |
| `SERVO` | SERVO PWM |
| `PPM` | PPM |
| `PORT` | dem Programmierport, bei einem Prüfstand mit 1 Port |
| `PORT1` bis `PORT4` | diesem Programmierport, bei einem Prüfstand mit 2 bis 4 Ports |
| `BUS` | SENSORBUS |
| `PD` | PD MINI |
| `PHASE` | PHASENABGRIFF |

Der Tag nennt die Rolle; BELEGT nennt das genaue Protokoll. Eine Taste mit Tag behält ihn bei AUS, in einem PROG-PORT-Eintrag und bei unbestätigter Zuordnung. Das Dial (M5Stack-Drehknopf) nennt eine Taste mit Tag samt Tag, `GPIO 7 ESC`.

Ein Tipp schreibt die Zuordnung sofort. [LETZTER SCHREIBVORGANG](#letzter-schreibvorgang) zeigt das Ergebnis.

Bei Platinen mit einem Pad nahe der linken oberen Umrissecke können obere Tasten mit Protokollliste, Anzahl und Begründung überlappen. Ein Druck im überlappenden Bereich öffnet die Liste statt den Pin auszuwählen. Der RP2350-CAN hat dort kein Pad.

Die Platine markiert auch die anderen Adern eines Servokabels:

| Markierung | Pad |
|---|---|
| weißer Kreis mit **G** | Masse |
| bernsteinfarbener Kreis mit **5V0** oder **3V3** | Schiene mit dieser Spannung |
| bernsteinfarbener Kreis mit **PWR** | VSYS, Versorgungseingang |
| blasses Kreuz | weder Masse noch Versorgungsschiene, etwa RUN und 3V3_EN |

## Pins binden

1. Wähle ein Protokoll in der Liste. Vorher binden Pin-Tasten nichts.
2. Tippe auf eine Taste. Der Pin wird sofort zugeordnet; eine zusätzliche Übernahmetaste gibt es nicht.
3. Tippe auf eine hervorgehobene Taste, um den Pin freizugeben.
4. Tippe auf eine graue Taste mit Tag, die ein anderes Protokoll hält, um ihren Pin zum gewählten Protokoll zu [verschieben](#einen-pin-zwischen-protokollen-verschieben). Eine Karte fragt vorher.
5. Wähle AUS für die Übersicht.

Ein Pin kann höchstens einem Protokoll zugeordnet sein. Mehrere Protokolle können gleichzeitig Pins belegen.

Jeder zugeordnete Pin belegt einen Slot der OUTPUTS-Page des IOmcu. Die Kanalnummern folgen den Pin-Nummern über alle Protokolle hinweg; der niedrigste Pin ist Kanal 0. Alle Protokolle teilen sich 8 Slots und 8 Kanäle.

## Die Protokolle

![Die Protokollliste](view:outputs-protocol)

| Protokoll | Signal | Pins |
|---|---|---|
| AUS | ändert nichts und zeigt die gesamte Zuordnung | 0 |
| SERVO PWM | PWM (Pulsweitenmodulation) mit 50Hz für Servo oder Ruder | bis 8 |
| MOTOR PWM | Puls mit 50Hz für einen ESC, von Leerlaufpuls bis Vollgaspuls | bis 8 |
| PPM | PPM (Pulspositionsmodulation): 8 Kanäle auf einem Pin mit 40Hz | 1 |
| DSHOT300, DSHOT600 | digitale Gassteuerung mit DShot bei 300 oder 600kbit/s | bis 8 |
| DSHOT300 BIDIR, DSHOT600 BIDIR | DShot mit ESC-Drehzahltelemetrie auf derselben Leitung | bis 8 |

Ohne Befehl zentriert ein Servokanal, während ein Motorkanal stoppt. Das gewählte Protokoll bestimmt dieses Verhalten, da der Puls allein das angeschlossene Gerät nicht erkennen lässt. MOTOR & ESC steuert alle als MOTOR PWM oder DShot zugeordneten Gaskanäle; Servokanäle bleiben unverändert. PPM benötigt alle 8 Kanäle und lässt keinen Platz für andere Protokolle.

![Die längsten Protokollnamen](view:outputs-bidir)

Die Protokollauswahl schreibt nichts. Sie hebt die zugehörigen Pins hervor und bestimmt das Protokoll für den nächsten angetippten Pin. Andere Zuordnungen bleiben bestehen.

## Leerlaufpuls und Vollgaspuls

Ein MOTOR-PWM-Kanal bildet 0% Gas auf Leerlaufpuls und 100% auf Vollgaspuls ab, mit den Vorgaben 1000us und 2000us. Beide stehen unter [MOTOR & ESC, OPTIONEN](motor-settings.md): Leerlaufpuls von 800 bis 1600us, Vollgaspuls von 1400 bis 2400us, jeweils in Schritten von 10us. SERVO PWM verwendet weiterhin 1000 bis 2000us oder den von SERVO gesendeten Bereich. Änderungen erreichen den IOmcu bei SAFE 300ms nach der letzten Eingabe. Liegt Leerlaufpuls nicht mindestens 100us unter Vollgaspuls, wird nichts gesendet. Das Band zeigt `Leerlaufpuls muss mindestens 100us unter Vollgaspuls liegen. Nicht gesendet.`.

Ein am Sender kalibrierter ESC verwendet dessen kürzesten Puls als Nullgas. Stelle Leerlaufpuls höchstens auf diesen Wert und Vollgaspuls mindestens auf den längsten Puls. Für einen Sender mit 985 bis 2012us sind das beispielsweise 980us und 2020us.

## Die Protokollliste und die Zähler

![Die Protokollliste über der Platine](view:outputs-protocol)

Oben links wählst du das Protokoll für den nächsten Pin-Tipp. Die Auswahl selbst schreibt nichts. Ein Tipp außerhalb schließt die offene Liste.

Unter der Liste zählt `4 VON 8 SLOTS BELEGT` die belegten Slots aller Protokolle. Darunter steht in Bernsteinfarbe, warum kein Pin hinzugefügt werden kann:

| Zeile | Wann |
|---|---|
| `ALLE 8 SLOTS BELEGT` | 8 Pins sind über alle Protokolle zugeordnet |
| `BENÖTIGT 8 KANÄLE, 4 FREI` | PPM, mit 4 zugeordneten Servo-Pins |
| `SERVO PWM BELEGT 8 PINS` | das gewählte Protokoll hält so viele Pins, wie es nimmt |
| `WÄHLE EIN PROTOKOLL, DANN TIPPE AUF PINS` | AUS ist gewählt |
| `NICHT AUSGELESEN` | die Zuordnung ist nicht bestätigt; jede Taste ist grau |
| `KEINE GÜLTIGE ZUORDNUNG IN DEN PAGES` | die Pages enthalten keine Zuordnung; jede Taste ist grau |
| `GPIO 28: TEILT PWM-SLICE MIT PROG-PORT GPIO 13` | ein Tipp auf GPIO 28 unter SERVO PWM oder MOTOR PWM, solange der Programmierport GPIO 13 hält: ein PWM-Slice; die Taste ist grau |
| `GPIO 16: GLEICHER PWM-KANAL WIE GPIO 0` | ein Tipp auf GPIO 16 unter SERVO PWM oder MOTOR PWM bei GPIO 0 als PWM zugeordnet: ein PWM-Compare-Register; die Taste ist grau |
| `GPIO 7: DSHOT600. WÄHLE ERST EIN PROTOKOLL` | ein Tipp auf einen zugeordneten Pin bei AUS |
| `GPIO 13: PROG-PORT 1. GIB IHN IM EINTRAG FREI` | ein Tipp auf den Pin eines Programmierports |
| `GPIO 16: SENSORBUS, IN SENSORS` | ein Tipp auf einen Pin des Sensorbusses; ebenso `GPIO 18: PD MINI, IN SUPPLY` und `GPIO 22: PHASENABGRIFF, IN SENSORS` |

Eine Zeile aus einem Tipp bleibt bis zur nächsten Wahl oder zum nächsten Tipp.

![Acht von acht Slots belegt](view:outputs-full)

## PINS und BELEGT

PINS links listet die Pins des gewählten Protokolls nach Kanalnummer, ergänzt um die aufgedruckte Pad-Nummer. BELEGT rechts zeigt andere zugeordnete Pins, nach Protokoll gruppiert. Bei AUS entfällt PINS; BELEGT zeigt dann alle zugeordneten Pins.

![GPIO 13 vom Programmierport gehalten](view:outputs-prog)

Der Programmierport belegt standardmäßig GPIO 13 (Pad 17). Ist der Pin keinem Protokoll zugeordnet, erscheint er grau mit dem Tag `PORT` und zuerst unter `PROG-PORT` in BELEGT. Ein Tipp darauf verschiebt nichts: sein Pin wird im [Eintrag PROG-PORT](#der-eintrag-prog-port) gesetzt. Weitere vom IOmcu gemeldete Ports belegen ihre Pins ebenso und stehen unter derselben Überschrift; bei 2 bis 4 Ports lauten die Tags `PORT1` bis `PORT4`.

![Die Pins der Anschlüsse gehalten und unter BELEGT genannt](view:outputs-iface)

Auch durch [SETUP > SENSORS](setup-sensors.md) oder [SUPPLY > PD MINI](supply.md#zeilen-des-pd-mini) belegte Pins sind grau, mit dem Tag `BUS`, `PD` oder `PHASE`. BELEGT listet sie nach dem Programmierport unter `SENSORBUS`, `PD MINI` oder `PHASENABGRIFF`. Passt eine Tabelle nicht vollständig, nennt `+N` die Anzahl ausgelassener Zeilen.

![AUS gewählt: jeder gebundene Pin unter BELEGT](view:outputs-off)

## Einen Pin zwischen Protokollen verschieben

![Die Frage-Karte über der Platine](view:outputs-take)

Ein Tipp auf eine Taste, die ein anderes Protokoll hält, fragt, ob der Pin zum gewählten Protokoll wechselt. Jedes Protokoll nimmt von jedem anderen, auch PPM. Die Karte deckt die Platine ab oder rückt auf einer niedrigen Platine über oder unter die angetippte Taste; diese behält einen Rand in Akzentfarbe und bleibt frei von der Karte.

| Zeile | Beispiel |
|---|---|
| der Pin und sein Halter | `GPIO 7: DSHOT600` |
| die Frage | `GPIO 7 AUS DSHOT600 ENTFERNEN UND SERVO PWM ZUWEISEN?` |
| was der Halter behält | `DSHOT600 BEHÄLT 1 PIN.`, `... BEHÄLT KEINEN PIN.` |
| die neue Rolle des Kanals, wenn sie wechselt | `CH4 WIRD ZUM SERVOKANAL: IN RUHE MITTE.` |
| die Leitung, in Bernstein | `DIE LEITUNG AN PAD 10 BEKOMMT AB DEM NÄCHSTEN ARM SERVO PWM.` |

1. Tippe auf PIN VERSCHIEBEN. Ein Tipp genügt: das Verschieben schaltet nichts ein.
2. Die Zuordnung wird einmal geschrieben, mit dem verschobenen Pin. [LETZTER SCHREIBVORGANG](#letzter-schreibvorgang) zeigt das Ergebnis.
3. Zum Rückgängigmachen wähle das alte Protokoll und verschiebe den Pin zurück. Kanäle folgen den Pins, also ist die Zuordnung wieder dieselbe.

ABBRECHEN, das Home-Feld oder ein Druck irgendwo neben der Karte schließt sie und schreibt nichts. Die Karte schließt auch ohne Schreiben, wenn ein Lesen die Zuordnung unbestätigt lässt, ein Zurücklesen einen Pin ändert, ein anderes Protokoll gewählt wird, die Platine wechselt oder die Seite verlassen wird. PIN VERSCHIEBEN prüft das Verschieben vorher noch einmal und schreibt nichts, wenn der Pin inzwischen gewechselt hat. 400ms lang, nachdem PIN VERSCHIEBEN oder ABBRECHEN die Karte geschlossen hat, wird ein Druck in der Fläche der Karte ignoriert; der zweite Tipp eines Doppeltipps wählt so keinen Pin darunter. Ein Druck auf PORT-PWM AUS wird nie ignoriert.

![Der letzte Pin von SERVO PWM unter DSHOT600 BIDIR](view:outputs-take-last)

Das Verschieben wird im Ruhezustand geschrieben. An einer ARMED-Bank lehnt der IOmcu es ab: LETZTER SCHREIBVORGANG zeigt `ABGELEHNT`, und der Pin bleibt bei seinem Halter. Ab dem nächsten ARM bekommt die Leitung am Pin das neue Signal: ein dort gelassener ESC bekommt beim ARM auf SERVO die 1500us-Mitte von SERVO PWM. Was ein ESC mit diesem Puls beim ARM macht, ist nicht gemessen; stecke die Leitung deshalb um, wie die Zeile in Bernstein sagt.

Ein Tipp, der nichts fragt, nennt stattdessen den Grund unter der Anzahl: der Pin eines Programmierports, gesetzt in dessen Eintrag; ein Pin von SENSORS oder des PD mini, gesetzt auf dieser Seite; AUS gewählt; ein volles Protokoll; eine PWM-Regel, die das Verschieben bricht.

![GPIO 16 des Sensorbusses angetippt](view:outputs-take-why)

Die [Rasteransicht](#die-rasteransicht) und der [rbmod-Carrier](#auf-dem-rbmod-carrier) fragen auf einer Karte neben der angetippten Zelle.

![Die Karte neben dem Raster](view:outputs-grid-take)

## Der Eintrag PROG-PORT

![Der Eintrag PROG-PORT](view:outputs-port)

Die offene Protokollliste enthält eine zweite Spalte mit einem PROG-PORT-Eintrag je gemeldetem Programmierport: 4 ab IOmcu 0.22.0, 1 bei älteren Builds oder vorläufig Port 1 bei unbekannter Anzahl. Vor IOmcu 0.17.0 gibt es weder Programmierport noch Eintrag. Programmierports sind eine eigene Ausgangsart und belegen weder Protokolleintrag noch Slot. Jeder Eintrag zeigt links neben der Platine folgende Zeilen:

| Zeile | Anzeige und Wirkung |
|---|---|
| PIN | `PIN GPIO 13` mit Pad, `PIN WÄHLEN`, `PIN GPIO 14 ABGELEHNT` oder `KEIN PIN BEKANNT`. Eine freie Taste fordert den Pin beim IOmcu an; die hervorgehobene Port-Taste fordert dessen Freigabe an. |
| FUNKTION | Signal außerhalb einer Programmiersitzung. Ein Tipp öffnet die Auswahl. SERVO PWM gibt Servopulse an einem PWM-Pin aus. KEINE setzt einen beliebigen freien Pin als Eingang mit Pull-down. DSHOT150, DSHOT300 und DSHOT600 ab IOmcu 0.22.0 senden Gas von MOTOR & ESC: alle 1ms ein Frame bei ARMED für Gas, an einem beliebigen freien Pin. |
| ÜBERWACHT VON | INA3221-Kanal aus SETUP > SENSORS, etwa `ÜBERWACHT VON INA3221 0x41 CH2`, oder `VON KEINEM STROMKANAL ÜBERWACHT`. |
| SERVO-TESTER | OUTPUTS oder PORT n als Ziel von [SERVO](servo.md). Die Taste PORT auf SERVO setzt denselben Wert. |
| PORT-PWM | EIN oder AUS für den Puls am Testerziel-Port. EIN nimmt ein Tippen; AUS wirkt beim Aufsetzen. |

Der IOmcu speichert Pin und Funktion jedes Ports zusammen mit den Ausgangszuordnungen im Flash. Sie gehören damit zur Platine. Port 1 beginnt mit SERVO PWM auf GPIO 13, Ports 2 bis 4 mit KEINE ohne Pin. Die Wahl einer FUNKTION oder Pin-Taste fordert eine Änderung an. Das Panel schreibt sie bei ruhendem Prüfstand; die bernsteinfarbene Zeile zeigt ihren Status.

![PROG-PORT 2 mit ausstehender Anfrage](view:outputs-port2)

| Zeile unter den Zeilen | Bedingung |
|---|---|
| `GPIO 5: TEILT PWM-SLICE MIT AUSGANG GPIO 4` | eine Taste getippt auf dem PWM-Slice eines zugeordneten PWM-Ausgangs, bei einem SERVO-PWM-Port; die Taste ist grau |
| `GPIO 28: GLEICHER PWM-KANAL WIE GPIO 12` | eine Taste getippt auf dem Compare-Register eines anderen SERVO-PWM-Ports; die Taste ist grau |
| `GESPERRT: SERVOPROGRAMMIERUNG BELEGT DEN PORT` | die [Servoprogrammierung](servo-programmer.md) hat einen Port |
| `WARTET AUF SAFE OHNE TEST, SWEEP ODER SERVOPROGRAMMIERUNG` | eine Anfrage für diesen Port wartet auf einen Prüfstand in Ruhe |
| `VOM IOMCU ABGELEHNT` | der IOmcu hat die letzte Anfrage abgelehnt oder nicht beantwortet |
| `VERBINDUNG VOR DEM SCHREIBEN VERLOREN` | die Verbindung brach ab, bevor die Anfrage geschrieben war |
| `PORTS NOCH NICHT AUSGELESEN` | die Ports des IOmcu seit dem Linkaufbau nicht gelesen |
| `GESPERRT, SOLANGE ARMED ODER EIN TEST ODER SWEEP LÄUFT` | der Prüfstand ist ARMED, oder ein Sweep oder ein Test läuft; PORT-PWM geht trotzdem aus |
| `SERVO-TESTER STEUERT PROG-PORT 3` | der Tester steuert einen anderen Port |
| `DER SERVO-TESTER BRAUCHT DIE FUNKTION SERVO PWM` | der Port ist KEINE oder DShot |
| `IOMCU ÄLTER ALS 0.22.0: NUR SERVO PWM` | der IOmcu hat keine FUNKTION |
| `PORT-PWM BRAUCHT PROG-PORT ALS ZIEL DES TESTERS` | PORT-PWM auf einem Port, der nicht das Ziel des Testers ist |
| `PORT-PWM EIN: ARM AUF SERVO SCHALTET PROG 5V UND PULSE EIN` | der rbmod-Carrier, PORT-PWM ein und PROG 5V aus: der Puls wartet auf PROG 5V, das ARM auf SERVO einschaltet |

Das Öffnen eines Port-Eintrags schreibt nichts und zeigt keinen LETZTEN SCHREIBVORGANG. Die beiden Tester-Einstellungen bleiben im Panel gespeichert und werden bei ruhendem Prüfstand an den IOmcu gesendet. Bei mehreren Ports heißen die Einträge `PROG-PORT 1` bis `PROG-PORT 4`.

Pins anderer Ports, Protokolle und Schnittstellen bleiben grau, mit ihrem Tag. Mit KEINE kann ein Port jeden freien Pin verwenden. Bis zum Auslesen der IOmcu-Ports sind alle Pin-Tasten grau.

![PROG-PORT 3, ein Port ohne Pin](view:outputs-port3)

## Die Rasteransicht

Ohne Platinenzeichnung oder Formbeschreibung zeigt OUTPUTS ein Raster aus 4 Spalten und 7 Zeilen. So werden keine Pad-Positionen geraten. Links stehen Protokollliste, Anzahl wie `3 VON 8 PINS` und LETZTER SCHREIBVORGANG. Tippe auf eine freie Zelle, um sie zuzuordnen. Der [rbmod-Carrier](#auf-dem-rbmod-carrier) verwendet dasselbe Raster ohne Katzenzeichnung. Die PIN-Zeile eines PROG-PORT-Eintrags ist hier nicht antippbar; Port-Pins lassen sich nur mit Tasten auf einer gezeichneten Platine ändern.

![Drei Pins auf einer Platine ohne Form](view:outputs-grid)

| Zelle | Bedeutung | Ein Tipp |
|---|---|---|
| abgehakt, `PAD 6` | im gewählten Protokoll zugeordnet; die auf der Platine gedruckte Pad-Nummer | gibt ihn frei |
| der Name des anderen Protokolls in voller Schrift, Kästchen grau gefüllt | von einem anderen Protokoll gehalten; gedimmt bei unbestätigter Zuordnung | fragt nach dem [Verschieben](#einen-pin-zwischen-protokollen-verschieben) |
| rot, durchgestrichen | reserviert: GPIO 3 trägt den Sicherheits-Heartbeat, GPIO 8 bis GPIO 12 den CAN-Controller (Controller Area Network) | tut nichts |
| `PROG-PORT` | der Pin des Programmierports des IOmcu, ab Werk GPIO 13 | nennt `GPIO 13: PROG-PORT. GIB IHN IM EINTRAG FREI` unter der Zahl |
| rot, `SENSORBUS`, `PD MINI` oder `PHASENABGRIFF` | ein Pin, den [SETUP > SENSORS](setup-sensors.md) oder der [Abschnitt PD MINI der SUPPLY-Optionen](supply.md#zeilen-des-pd-mini) hält: Sensor-SDA und -SCL, solange INA228 oder AS5600 EIN ist oder ein INA3221 unter [SETUP > SENSORS](setup-sensors.md) bestückt ist; PD mini TX und RX, solange der PD mini EIN ist und beide Pins gesetzt sind; der Abgriff-Pin, solange der Phasenabgriff EIN ist. Die Zelle folgt diesen Einstellungen | nennt unter der Zahl die Seite, die ihn setzt, `GPIO 16: SENSORBUS, IN SENSORS` |
| ausgegraut, `SLICE VON GPIO 13` | unter SERVO PWM und MOTOR PWM: der Pin teilt sich einen PWM-Slice mit dem Pin eines Programmierports, jedes Ports, den der IOmcu meldet; GPIO 28 und GPIO 13 sind Slice 6 | tut nichts; `GPIO 28: TEILT PWM-SLICE MIT PROG-PORT GPIO 13` unter der Zahl |
| ausgegraut, `KANAL VON GPIO 0` | unter SERVO PWM und MOTOR PWM: der Pin teilt sich ein PWM-Compare-Register mit einem als PWM zugeordneten Pin (GPIO 0 und GPIO 16, GPIO 1 und GPIO 17, GPIO 2 und GPIO 18, GPIO 4 und GPIO 20, GPIO 5 und GPIO 21, GPIO 6 und GPIO 22) | tut nichts; `GPIO 16: GLEICHER PWM-KANAL WIE GPIO 0` unter der Zahl |
| frei, `PAD 7` | nicht zugeordnet | ordnet ihn zu |

Sind keine weiteren Pins für das Protokoll verfügbar, steht der Grund in Bernsteinfarbe unter der Anzahl: `BENÖTIGT 8 KANÄLE, 4 FREI`, `ALLE 8 SLOTS BELEGT` oder `SERVO PWM BELEGT 8 PINS`.

Eine gespeicherte Ausgangszuordnung für GPIO 13 behält den Pin. Der Programmierport bleibt dann ohne Pin; die [Servoprogrammierung](servo-programmer.md) meldet dies bei VERBINDEN.

## Auf dem rbmod-Carrier

![PWM01 bis PWM04, PWM03 wegen Slice 2 abgelehnt](view:outputs-rbmod)

Auf dem [rbmod-Carrier](https://github.com/subtilitas/rcbench/wiki/Carrier-de) sind alle Leitungen fest verdrahtet. Du wählst daher Protokolle statt Pins. Das Raster zeigt PWM01 bis PWM04 auf GPIO 2, 4, 5 und 1 mit dem jeweiligen GPIO unter dem Steckernamen. Tippe auf einen Ausgang, um das Protokoll zuzuordnen. Die zugeordneten Ausgänge belegen Slots und Kanäle in der Reihenfolge PWM01 bis PWM04, ein nicht zugeordneter Ausgang belegt keinen: PWM01 ist Kanal 0, solange er zugeordnet ist, und ist nur PWM03 zugeordnet, ist PWM03 Kanal 0. Ein Protokoll nutzt höchstens diese 4 Ausgänge: `SERVO PWM BELEGT 4 PINS`.

PWM02 und PWM03 teilen sich einen PWM-Slice und damit eine Pulsrate. SERVO PWM und MOTOR PWM lassen sich dort nicht miteinander kombinieren. Die Zelle zeigt `SLICE VON PWM02`; ein Tipp meldet `PWM03 GPIO 5: GLEICHER PWM-SLICE WIE PWM02 GPIO 4`. DShot verwendet eine eigene Zustandsmaschine und lässt sich mit beiden kombinieren.

![PWM01 unter DSHOT300 angetippt](view:outputs-rbmod-take)

Ein Tipp auf einen Ausgang, den ein anderes Protokoll hält, fragt nach dem Verschieben, wie auf der Platine. Die Karte nennt den Ausgang, `DIE LEITUNG AN PWM01 BEKOMMT AB DEM NÄCHSTEN ARM DSHOT300.`

![PROG PORT 1 auf dem Carrier](view:outputs-rbmod-port)

PROG-PORT-Einträge nennen den festen Stecker und Pin, etwa `PROGP01 AUF GPIO 13`. PIN und FUNKTION lassen sich nicht direkt ändern. FUNKTION zeigt SERVO PWM, wenn der Port den Tester bedient, sonst PROGRAMMIERUNG.

Ab IOmcu-Protokoll 4.36 sind SERVO-TESTER und PORT-PWM verfügbar. Die Portwahl fordert die Servopulsfunktion an. Ein PWM-Ausgang auf demselben Slice sperrt die Wahl. PORT-PWM EIN wird auch bei ausgeschaltetem PROG 5V angenommen; der Puls startet, sobald PROG 5V EIN zeigt. ARM auf LIVE, SWEEP und RUDER von SERVO schaltet PROG 5V und PORT-PWM selbst ein. Das Verlassen von SERVO schaltet ein dort eingeschaltetes PROG 5V aus. Ältere Protokolle blenden die Testerzeilen aus und zeigen `PIN FEST. DER TESTER BRAUCHT IOMCU-PROTOKOLL 4.36`.

## Die Übersicht: AUS

![AUS zeigt das Protokoll jedes gebundenen Pins](view:outputs-off)

Von SETUP aus öffnet OUTPUTS immer mit AUS. Diese Auswahl ändert keine Zuordnung. BELEGT gruppiert alle zugeordneten Pins nach Protokoll. Der Hinweis lautet `WÄHLE EIN PROTOKOLL, DANN TIPPE AUF PINS`. Ein Tipp auf einen freien Pin bewirkt nichts; ein Tipp auf einen zugeordneten Pin verschiebt nichts und nennt sein Protokoll, `GPIO 7: DSHOT600. WÄHLE ERST EIN PROTOKOLL`. Beim Öffnen wird nichts an den IOmcu geschrieben.

AUS bleibt bis zur Protokollwahl ausgewählt, auch nach Verlassen der Seite. Ein Platinenwechsel wählt ebenfalls AUS, etwa wenn ein IOmcu erstmals während des Besuchs antwortet oder ersetzt wird. So geht eine vorherige Protokollauswahl nicht auf eine andere Platine über. Wähle ein Protokoll zum Ändern seiner Pins und AUS für die Übersicht.

## LETZTER SCHREIBVORGANG

Pin-Änderungen werden sofort geschrieben. LETZTER SCHREIBVORGANG steht neben der Protokollliste oder im Raster unten links. PROG-PORT-Anfragen verwenden ihre eigene Statuszeile. Danach liest das Panel die IOmcu-Pages erneut aus und zeigt die aktive Zuordnung.

| LETZTER SCHREIBVORGANG | Bedeutung |
|---|---|
| `NOCH KEINER` | seit dem Start des Panels nichts geschrieben |
| `GESCHRIEBEN` | der IOmcu hat das Schreiben bestätigt |
| `KEINE VERBINDUNG` | ein Austausch bekam binnen 1000ms keine Antwort, oder die Verbindung ist unterbrochen |
| `ABGELEHNT` | der IOmcu hat abgelehnt: ein Pin, den er nicht zuordnen kann, eine Änderung bei ARMED oder eine Pulsrate, die er nicht zurücksetzen konnte |

Ein IOmcu mit Link-Protokoll 4.38 oder neuer nennt die Ursache eines abgelehnten Schreibens. Die Zeile unter dem Protokoll zeigt sie, solange LETZTER SCHREIBVORGANG `ABGELEHNT` zeigt und dort keine andere Zeile steht:

| Zeile | Ursache |
|---|---|
| `ABGELEHNT: NICHT BEI ARMED` | eine Rolle, ein Throttle-Kanal oder ein Slot ändert sich bei ARMED |
| `ABGELEHNT: KEIN PIO ODER PWM-SLICE FREI` | der IOmcu hat für einen Ausgang keine freie PIO-State-Machine, keinen PIO-Instruktionsspeicher oder keinen PWM-Slice, oder der Pin hat den Treiber abgewiesen |
| `ABGELEHNT: LÄSST DEM PD MINI KEIN PIO` | die Ausgänge würden den PIO-Block belegen, auf den der UART des PD mini wartet |
| `ABGELEHNT: PIN ODER PWM-SLICE EINES PROG-PORTS` | ein Programmierport hält den Pin, dessen Kanal oder dessen PWM-Slice |
| `ABGELEHNT: PIN DES PD MINI` | SUPPLY hält den Pin; `ABGELEHNT: PIN DES SENSORBUS` und `ABGELEHNT: PIN DES PHASENABGRIFFS` ebenso für SENSORS |
| `ABGELEHNT: PWM-SLICE MIT ANDERER PULSRATE` | der Ausgang teilt einen PWM-Slice mit einem Servoausgang anderer Pulsrate |
| `ABGELEHNT: PULS LÄNGER ALS SEIN FRAME` | der Puls-Endpunkt eines Kanals ist länger als der Frame seines PWM-Ausgangs |
| `ABGELEHNT: KEIN SERVOAUSGANG DER PLATINE` | auf einer Platine mit gelöteten Pins ein Pin, der keiner ihrer Servoausgänge ist |
| `ABGELEHNT: ESC-PROGRAMMIERUNG HÄLT AUSGANG` | eine ESC-Programmierung hält einen Ausgang |
| `ABGELEHNT: WERT AUSSERHALB DES BEREICHS` | ein Registerwert außerhalb des Bereichs seiner Page |
| `ABGELEHNT: DIE PAGES KAMEN VERÄNDERT AN` | der IOmcu hält andere Pages, als das Panel gesendet hat; das nächste Schreiben sendet sie erneut |

Ab Version 0.15.0 übernimmt der IOmcu die Zuordnung vollständig oder gar nicht. Bei `KEINE VERBINDUNG` oder `ABGELEHNT` bleibt die bisherige Zuordnung aktiv.

## Die Platinenzeichnung

Der IOmcu enthält eine Zeichnung seiner Platine mit Umriss, Bauteilen und Pads im Raster von 0,01mm. Das Panel lädt sie über die Verbindung und speichert sie im Flash. Angezeigt wird sie nur, wenn Form und Pin-Katalog zur Platine passen.

![Die Platine, allein aus der Form gezeichnet](view:outputs-shape)

Ohne Zeichnung oder während der Übertragung werden Umriss und Pads aus der Formbeschreibung dargestellt. Die Tasten bleiben an denselben Stellen. Fehlt auch die Formbeschreibung, zeigt OUTPUTS [die Rasteransicht](#die-rasteransicht).

Die RP2350-CAN-Zeichnung umfasst 1.560 Bytes und wird in 26 Blöcken zu 62 Bytes übertragen. Die Dauer wurde nicht gemessen. Das Speichern im Flash wartet auf SAFE und einen ausgeschalteten Netzteilausgang.

## Eine Bindung, die nicht bestätigt ist

![Die zuletzt gelesene Bindung nach einem gescheiterten Lesen](view:outputs-unread)

Eine Zuordnung lässt sich erst nach bestätigtem Auslesen ändern. Bei fehlgeschlagener Page-Abfrage bleibt die letzte bekannte Zuordnung sichtbar, aber alle Tasten sind grau. Die Meldung lautet `NICHT AUSGELESEN`, im Raster `ZUORDNUNG NICHT AUSGELESEN. ÄNDERN GESPERRT.` Pin-Tipps haben keine Wirkung. Das Panel liest bei jedem Verbindungsaufbau und alle 500ms erneut.

![Pages mit ungültiger Zuordnung](view:outputs-odd)

Enthalten ausgelesene Pages eine ungültige Zuordnung, etwa denselben Pin in zwei Slots, erscheint `KEINE GÜLTIGE ZUORDNUNG IN DEN PAGES`. Unter BELEGT steht `2s HALTEN ZUM LÖSCHEN ALLER PIN-ZUORDNUNGEN`. Vollständiges Halten schreibt eine leere Zuordnung und gibt die Bearbeitung frei. Abrutschen, Loslassen vor 2s oder Verlassen der Seite bricht das Halten ab.

## Der IOmcu behält die Bindung

Der IOmcu speichert die Zuordnung in seinem eigenen Flash und stellt sie beim Start wieder her. Das Panel speichert sie nicht. Wiederhergestellte Zuordnungen belegen ihre Pins und halten sie bis ARMED im Ruhezustand. Bei Stromausfall während des Speicherns bleibt die vorherige Zuordnung erhalten.

## Verwandte Seiten

- [Setup](setup.md)
- [Motor und ESC](motor.md#ziel): Pins für die Gassteuerung.
- [Servo-Optionen](servo-settings.md#die-taste-port): Pins für den Servotester.
