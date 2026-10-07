# Stick-Programmierung

[English](StickProgramming.md)

Die Stick-Programmierung stellt einen ESC (Electronic Speed Controller) über
sein Gasknüppel-Menü ein. Der Prüfstand versorgt den ESC aus dem PD mini,
der Motor ist durch eine Widerstandslast ersetzt. Er bewegt das Gas in die
Stellungen, die das [ESC-Profil](EscProfiles-de.md) nennt, und zählt die
Pieptöne des Menüs als Pulse im Versorgungsstrom. Auf dem Bildschirm ist es
die Klasse ESC STICK des Bildschirms PROGRAMMER.

Was sie nicht weiß:

- Kein ESC ist aufgenommen worden. Jede Zeit unten ist ein Vorgabewert, der
  für eine Messung steht, und als Einstellung gehalten, damit eine Aufnahme
  ihn ohne neues Image ersetzen kann.
- Kein Profil ist verifiziert. Punkt- und Wertnummern und Einstiegsgesten
  sind die der Handbücher.
- Die eigenen Töne des ESCs nach einer Auswahl werden nicht ausgewertet.
  DONE heißt: jede Auswahlbewegung kam auf eine gezählte Gruppe in der
  Reihenfolge des Menüs, nicht, dass der ESC sie gespeichert hat.

## Vor einem Lauf

1. Den Motor vom ESC trennen.
2. Eine Widerstandslast an die Motorleitungen des ESCs legen. Ihr Wert ist
   nicht festgelegt: kein ESC ist in eine solche gelaufen. Sie muss den
   Strom bei der Spannung des Laufs unter dessen Strombegrenzung halten.
3. Die Signalleitung des ESCs an einen Pin legen, der auf dem Bildschirm
   OUTPUTS mit der Rolle Throttle gebunden ist. Ein Lauf steuert jeden als
   Throttle gebundenen Kanal, wie der Bildschirm MOTOR.
4. Die Versorgungsleitungen des ESCs an den Ausgang des Netzteils legen.
5. Den Ausgang des Netzteils ausschalten. Ein Lauf, der ihn an oder auf dem
   Weg findet, wird abgelehnt.

## Auf dem Bildschirm

PROGRAMMER, dann ESC STICK:

![Geräteklasse](img/de/programmer.png)

Die Liste enthält jedes Profil, die ausführbaren zuerst. Ein Profil, das der
Prüfstand nicht ausführen kann, nennt den Grund in seiner Zeile und öffnet
nichts; ebenso eines, dessen Spannung über der Grenze von SUPPLY liegt.
Reihenfolge und Anzahl folgen VOLTAGE und der Grenze, wenn sie sich
ändern. Ein
Profil von der SD-Karte trägt CARD.

![Die Profile](img/de/programmer-stick.png)

Die Seite eines Profils listet seine Menüpunkte. Jeder steht anfangs auf
KEEP und bleibt dann, wie er ist; die Stepper gehen durch die Werte des
Punkts und halten an beiden Enden an. Punkte mit dem Schlüssel `reset` oder
`exit` sind Aktionen, keine Einstellungen: der ESC handelt auf die
Auswahlbewegung und gibt keine Werte aus, deshalb zeigen ihre Zeilen ACTION,
NOT SET und bieten nichts an. Ein Punkt mit einem einzigen Wert -- die
einzige Einstellung, die es gibt, oder eine Regel, die als Wert geschrieben
ist, wie die Zellenzahl von `hobbywing-flyfun-hv-9item`, "N Pieptöne = N
Zellen" -- bietet nichts zur Wahl; seine Zeile zeigt NOTHING TO CHOOSE. Die Zeile unter der Liste nennt die
Werte des gewählten Punkts und den Standardwert. RUN erscheint, sobald ein
Wert gewählt ist und der Lauf starten kann; kann er es nicht, sagt die Zeile
neben RUN, warum.

![Zwei Werte gewählt](img/de/programmer-stick-items.png)

RUN öffnet eine Warnung über den ganzen Bildschirm. HOLD TO RUN startet den
Lauf nach 2 s Halten, wie ARM. Ein Finger, der den Knopf verlässt, ein
verlorenes Touch-Ereignis oder ein STOP während des Haltens bricht es ab;
CANCEL schließt die Warnung. Das ARM, das das Halten anfordert, verlässt den
Bildschirm mit den Befehlen des nächsten Frames; ein STOP oder ein
verlorenes Touch-Ereignis davor nimmt es zurück und beendet den Lauf, damit
es keinen Stopp aufheben kann, der danach kam.

![Die Warnung](img/de/programmer-stick-warning.png)

Während des Laufs zeigt die Seite die Phase, die Pieptöne der laufenden
Gruppe, die letzte Gruppe und ob sie in der Reihenfolge war, und den Strom
neben seinem Grundwert. ABORT beendet den Lauf, ebenso STOP im Band und das
Verlassen des Bildschirms. BACK und TIMING sind nicht verfügbar.

![Ein Lauf zählt Punktgruppen](img/de/programmer-stick-run.png)

Das Ergebnis bleibt bis OK: welche Auswahlen getroffen wurden, und bei einem
abgebrochenen Lauf der Grund. Bei mehr als fünf Änderungen zählt die letzte
Zeile den Rest.

![Fertig](img/de/programmer-stick-done.png)

![Gestoppt](img/de/programmer-stick-aborted.png)

TIMING öffnet die Einstellungen unten. CLOSE fordert das Speichern an;
gespeichert wird, während der Prüfstand entschärft und der Ausgang des
Netzteils aus ist.

![Die Zeiteinstellungen](img/de/programmer-stick-timing.png)

## Was ein Lauf tut

| Phase | Gas | Netzteil | Endet |
| --- | --- | --- | --- |
| ARMING | MIN | aus | wenn der Prüfstand scharf meldet; nach 3000 ms: NOT ARMED |
| SIGNAL | Einstiegsstellung | aus | nach 1000 ms, damit der ESC das Signal beim Start sieht |
| POWER ON | Einstiegsstellung | an | wenn ein Messwert den Ausgang an meldet; nach 3000 ms: NO POWER |
| ENTRY | Einstiegsstellung | an | ENTRY nach dem Einschalten: das `hold_ms` des Profils, wo es eines nennt |
| ITEMS | Ruhestellung | an | eine Punktgruppe in Reihenfolge nennt einen gewünschten Punkt: die Auswahlbewegung |
| VALUES | wo die letzte Bewegung es ließ | an | eine Wertgruppe in Reihenfolge nennt den gewünschten Wert: die Wertbewegung |
| STORING | die Wertbewegung, dann die Speicherbewegung | an | nach STORE, und nach STORE noch einmal, wo das Profil eine Speicherbewegung hat |
| POWER CYCLE | wo es speicherte, dann Einstiegsstellung | aus | das Netzteil meldet den Ausgang aus und den Strom 200 ms unten, dann OFF TIME (mindestens 1000 ms) in der Einstiegsstellung, dann wieder POWER ON; nicht innerhalb von 3000 ms aus: SUPPLY STAYS ON |
| POWER OFF | wo es speicherte | aus | das Netzteil meldet den Ausgang aus und den Strom 200 ms unten; nicht innerhalb von 3000 ms: SUPPLY STAYS ON |
| DONE | MIN | aus | entschärft |
| ABORTED | MIN | aus | entschärft, alles in einem Schritt |

MIN, MID und MAX sind 0, 50 und 100 % des Wegs des Throttle-Ausgangs. Die
Ruhestellung ist `scheme.listen` des Profils, oder die Einstiegsstellung, wo
es keines hat. Ein zweistufiges Menü (`value_select` gesetzt) wählt den
Punkt mit `select` und speichert den Wert mit `value_select`, dann zählt es
wieder Punkte; ein einstufiges Menü zählt Werte und speichert mit `select`,
gefolgt von der Bewegung `scheme.store` des Profils, wo es eine nennt. Ein
Profil mit `"changes_per_entry": "one"` nimmt eine Änderung je Einschalten,
also schaltet ein Lauf mit mehreren Änderungen das Netzteil zwischen ihnen
für OFF TIME aus.

Ein Lauf, der wie geplant endet oder die Versorgung aus- und einschaltet,
schaltet zuerst das Netzteil aus und lässt den Knüppel, wo er gespeichert
hat. Er bewegt ihn erst, wenn das Netzteil selbst den Ausgang aus meldet,
in Messwerten, die nach der Anforderung genommen sind, mit dem Strom
200 ms lang bei höchstens 20 mA (`ESC_STICK_OFF_MA`,
`ESC_STICK_OFF_SETTLE_MS`). Das OFF des Panels ist eine Anforderung: das
PD mini schaltet einen Link-Austausch und eine Modultransaktion später ab,
und bis dahin ist der ESC versorgt und in seinem Menü.

Zwei Teile dieser Regel sind nicht gemessen:
- Der Strom, den das PD mini bei ausgeschaltetem Ausgang meldet. Die Regel
  nimmt unter 20 mA an. Ein Modul, das mehr meldet, beendet jeden geplanten
  Lauf mit ABORTED und SUPPLY STAYS ON, und ein Lauf mit einer Änderung je
  Einschalten kommt nie zur zweiten Änderung.
- Wie lange der ESC aus seinen Eingangskondensatoren weiterläuft, nachdem
  der Schalter des PD mini geöffnet hat. Der Strom zeigt, dass der Schalter
  offen ist, nicht, dass der ESC aus ist. Die 200 ms decken eine kleine
  Kapazität ab, etwa 2000 µF, die bei 30 mA in rund 270 ms um 4 V fallen;
  eine große decken sie nicht ab.

Jede Bewegung geht hinaus wie die Befehle des Bildschirms MOTOR: ARM über
die Arming-Policy, THROTTLE auf die Throttle-Kanäle, und das Netzteil über
das eigene ON und OFF von SUPPLY. Nichts umgeht STOP oder die Regeln zum
Scharfschalten.

## Wie die Pieptöne gezählt werden

- **Grundwert.** Während ENTRY, ab 500 ms nach dem Einschalten, ist der
  Grundwert der niedrigste gelesene Strom. Ein Piepton kann ihn nicht
  anheben. Während das Menü gezählt wird, folgt der Grundwert ruhigen
  Messwerten um 1/16 des Unterschieds je Messwert.
- **Piepton.** Ein Messwert über Grundwert plus THRESHOLD beginnt einen
  Puls; einer unter Grundwert plus THRESHOLD minus HYSTERESIS beendet ihn.
- **Längen in Messwerten.** Ein Puls mit weniger Messwerten, als BEEP MIN
  beim längsten gesehenen Abstand halten muss, oder eine Lücke zwischen zwei
  Pulsen mit weniger, als GAP MIN halten muss, verdirbt seine Gruppe. Ein
  Puls, dessen erster und letzter Messwert weiter als LONG MAX auseinander
  liegen, verdirbt seine Gruppe. Ein Puls ab LONG ist ein langer Piepton; in
  einem Profil ohne lange Pieptöne verdirbt er seine Gruppe, ebenso ein
  langer nach einem kurzen.
- **Gruppe.** Stille von GROUP GAP nach dem letzten Puls beendet eine
  Gruppe. Ihre Zahl sind die kurzen Pieptöne plus `long_equals_short` für
  jeden langen.
- **Erst Stille.** Wenn eine Phase zu hören beginnt, wird keine Gruppe
  gezählt, bevor GROUP GAP Stille gehört ist; die erste Gruppe ist also
  ganz und nicht das Ende einer schon laufenden.
- **Reihenfolge.** Eine Gruppe ist in Reihenfolge, wenn sie eins mehr als
  die Gruppe davor ist, oder die niedrigste Nummer der Schleife nach ihrer
  höchsten. Wo das Profil jede Gruppe wiederholt (`repeat` über 0), ist
  dieselbe Zahl noch einmal nur nach einer Gruppe in Reihenfolge in
  Reihenfolge, und höchstens `repeat`-mal. Auf eine Gruppe wird nur
  reagiert, wenn sie in Reihenfolge ist und die davor auch: drei Gruppen
  hintereinander stimmen überein. Eine Zahl, die ein verlorener Piepton
  falsch macht, ist kleiner als die wahre, also ist sie nur in Reihenfolge,
  wenn die Gruppen davor auch falsch waren. Ein verlorener Piepton übergeht
  deshalb seine Gruppe, und die nächste Schleife wird genutzt; eine falsche
  Auswahl braucht einen verlorenen Piepton in jeder von drei Gruppen
  hintereinander, oder, in einem Menü mit wiederholten Gruppen, eine ganz
  verlorene Gruppe und einen verlorenen Piepton in der nächsten.
- **Späte Messwerte.** Ein Messwert, der mehr als das Kürzere von BEEP MIN
  und GAP MIN nach dem vorigen kommt, ist spät, ebenso einer, dessen Zähler
  einen übersprungenen Messwert zeigt: er verdirbt die Gruppe, in die er
  fällt, und bricht die Reihenfolge. 3 späte Messwerte hintereinander
  beenden den Lauf mit READ RATE.

## Die Einstellungen

TIMING auf der Seite eines Profils. Gespeichert mit den anderen
Einstellungen. Kein Wert hier ist gemessen.

| Einstellung | Vorgabe | Bereich | Was sie ist |
| --- | --- | --- | --- |
| Voltage | 0 V | 0 bis 20 V | die Spannung des Netzteils; 0 nimmt sie aus dem Profil |
| Current limit | 1,00 A | 0,10 bis 3,00 A | die Strombegrenzung des Netzteils während des Laufs |
| Beep min | 200 ms | 20 bis 2000 ms | kürzester Piepton, den der ESC gibt |
| Gap min | 200 ms | 20 bis 2000 ms | kürzeste Stille zwischen zwei Pieptönen |
| Long | 500 ms | 50 bis 5000 ms | ein Piepton ab dieser Länge ist ein langer |
| Long max | 1500 ms | 100 bis 10000 ms | ein längerer Puls ist kein Piepton |
| Group gap | 700 ms | 50 bis 10000 ms | Stille, die eine Gruppe beendet |
| Entry | 5000 ms | 1000 bis 60000 ms | Einschalten bis zum Menü, wo das Profil kein `hold_ms` nennt |
| Store | 2000 ms | 0 bis 10000 ms | gehalten an der letzten Auswahl vor dem Ausschalten |
| Off time | 3000 ms | 500 bis 20000 ms | Netzteil aus zwischen zwei Einstiegen |
| Silence | 10000 ms | 1000 bis 60000 ms | so lange kein Piepton beendet den Lauf |
| Timeout | 180000 ms | 5000 bis 600000 ms | so lange keine Reaktion auf eine gewünschte Gruppe beendet den Lauf |
| Threshold | 100 mA | 10 bis 2000 mA | über dem Grundwert: ein Piepton |
| Hysteresis | 40 mA | 0 bis 1000 mA | unter der Schwelle minus diesem Wert: Stille |

Ein Lauf wird abgelehnt, nicht angepasst, wenn die Einstellungen sich
widersprechen: LONG höchstens BEEP MIN, LONG MAX höchstens LONG, GROUP GAP
höchstens GAP MIN, THRESHOLD höchstens HYSTERESIS, ENTRY höchstens 500 ms,
oder GROUP GAP plus das Kürzere von BEEP MIN und GAP MIN mindestens das
`within_ms` des Profils für seine Auswahl- oder Wertbewegung.

## Das Netzteil

VOLTAGE 0 nimmt das niedrigste `cells_min` unter den Modellen des Profils,
mit 3,8 V je LiPo-Zelle (Lithium-Polymer) oder 1,2 V je NiMH-Zelle
(Nickel-Metallhydrid): über der Abschaltung dieses Modells und unter dem
Maximum jedes Modells. Ein Profil, dessen Modelle keine Zellenzahl nennen,
braucht VOLTAGE. Eine Spannung über der Grenze des Bildschirms SUPPLY oder
unter dem Minimum des Netzteils, und eine Strombegrenzung über der Grenze
von SUPPLY, werden abgelehnt. Die Sollwerte des Laufs werden die Sollwerte
des Bildschirms SUPPLY.

## Welche Profile laufen

14 der 72 Profile sind von einer Art, die der Ablauf ausführt: 13
zweistufige und 1 einstufiges. Mit den 20 V des PD mini und den
vorgegebenen Grenzen öffnet die Liste 13 davon:
hobbywing-skywalker-v2-hv-opto braucht 22,8 V. Ein Profil läuft, wenn es
`"automatable": "full"` ist, vor dem Einschalten betreten wird, mit `count`
oder `short_long` zählt und eine Auswahlbewegung hat. Eine Ruhestellung
außer der Einstiegsstellung braucht das `hold_ms` des Profils: die Bewegung
am Ende eines Einstiegs unbekannter Länge kann in einer anderen Stufe davon
landen. Ein zweistufiges Profil sagt zudem `item_then_value` an, seine
Bewegungen unterscheiden sich voneinander und von der Ruhestellung, und es
hat keine Speicherbewegung. Ein einstufiges Profil sagt `value` an, oder
`item` mit einem Punkt; nimmt eine Änderung je Einschalten; hat keine
Wertnummer in zwei Punkten; seine Auswahlbewegung unterscheidet sich von der
Ruhestellung, und seine Speicherbewegung, wo es eine hat, von der
Auswahlbewegung. Die Speicherbewegung darf die Ruhestellung sein: in den
YGE-Mode-Setups geht der Knüppel zum Speichern zurück auf Minimum, wo er
ruhte.

| Die Zeile sagt | Warum |
| --- | --- |
| needs a person at the ESC | `automatable` ist `assisted` |
| no usable procedure | `automatable` ist `none` |
| entered after power-on | `scheme.entry.when` ist `after_power_on` |
| melody menu, yes/no menu, stick-position menu, menu of its own kind | `scheme.type` |
| tones told apart by pitch | `scheme.announce.encoding` ist `melody` oder `yes_no` |
| item and value, one move | `item_then_value` ohne `value_select`: welche Gruppe die Bewegung beantwortet, steht nicht fest |
| two moves, no value tones | `value_select` ohne `item_then_value` |
| many changes, one stage | einstufig mit `"changes_per_entry": "many"` |
| items counted, one stage | einstufig, `item` angesagt, mehrere Punkte |
| values repeat across items | einstufig, eine Wertnummer in zwei Punkten |
| select move is the rest | die Auswahlbewegung ist die Ruhestellung: keine Bewegung zu machen |
| value move = select move | zweistufig, `value_select` gleich `select` |
| rest move, no entry time | `scheme.listen` weicht von der Einstiegsstellung ab und `hold_ms` ist null: die YGE-Profile |
| store move, two stages | zweistufig mit `scheme.store` |
| store move = select move | einstufig, `scheme.store` gleich `select` |
| needs 22.8 V, cap 21.0 V | die Spannung (VOLTAGE, oder die Zellenzahl des Profils) liegt über der Grenze von SUPPLY |

Ein auf der SD-Karte korrigiertes Profil steht mit seiner Korrektur in der
Liste.

## Wie ein Lauf endet

| Ergebnis | Ursache |
| --- | --- |
| DONE | jede Auswahl getroffen |
| STOP | STOP, aus jeder Quelle, während des Laufs gezählt |
| DISARMED | der Prüfstand wurde entschärft |
| LINK LOST | der Koprozessor antwortete irgendwann während des Laufs und hörte auf |
| SUPPLY OFF | der Ausgang ging aus: eine Auslösung, oder ein ON, das das Netzteil fallen ließ |
| SUPPLY NOT ANSWERING | das Netzteil antwortet nicht mehr |
| NO READINGS | der Messwertzähler 1000 ms unverändert |
| READ RATE | 3 späte Messwerte hintereinander |
| NOT ARMED | nicht scharf innerhalb von 3000 ms |
| NO POWER | der Ausgang nicht innerhalb von 3000 ms als an gemeldet |
| SUPPLY STAYS ON | das Netzteil meldet den Ausgang nicht innerhalb von 3000 ms nach der Anforderung des Laufs aus, mit dem Strom unten |
| TOUCH LOST | Touch-Ereignisse verloren, solange das ARM des Laufs noch nicht genommen oder der Prüfstand noch nicht scharf war |
| NO BEEPS | SILENCE lang kein Piepton |
| CURRENT STAYS HIGH | ein Puls länger als zweimal LONG MAX |
| TIMEOUT | innerhalb von TIMEOUT auf keine gewünschte Gruppe reagiert |
| ABORTED | ABORT |
| SCREEN LEFT | der Bildschirm wurde verlassen |

Jedes Ende setzt das Gas auf MIN, schaltet das Netzteil aus und entschärft;
ein Abbruch tut alle drei in einem Schritt.
Ein Stopp rastet ein wie jeder Stopp: der nächste Lauf schaltet erst über
seine eigene Warnung wieder scharf.

## Die Leserate

Ein Messwert ist neu, wenn der Messwertzähler des Netzteils weiterläuft:
beim PD mini SAMPLES auf der SUPPLY-Page, der Zähler des Koprozessors für
die Ausgangsmessungen, die das Modul beantwortet hat. Seine Zeit ist die
Page-Lesung, die den Zähler zuerst zeigte. Das PD mini liest seinen Ausgang
alle 100 ms (`PDMINI_DISPLAY_MS`), und das Panel liest die Page alle 100 ms
(`SUPPLY_LINK_READ_MS`) in seinem 50-ms-Poll; neue Messwerte kommen also
100 bis 150 ms auseinander, und der Zähler kann zwischen zwei Page-Lesungen
um 2 weiterlaufen. Ein Zähler, der um mehr als 1 weiterläuft, ist ein
Messwert, den das Panel nie sah, und ist spät. Ein Zähler, der stehen
bleibt, sind Messwerte, die ausbleiben: NO READINGS nach 1000 ms, wie frisch
die Page selbst auch ist. Ein Piepton oder eine Lücke kürzer als etwa
200 ms kann zwischen zwei Messwerte des Moduls fallen und ungesehen
bleiben; die Reihenfolgeregel übergeht dann die Gruppe, statt die Nummer
darunter zu wählen. BEEP MIN und GAP MIN stehen deshalb auf 200 ms. Ob der
Strom des Moduls ein Augenblickswert oder ein Mittel über sein Intervall
ist, ist nicht bekannt.

Pieptöne aus der Zeit zu zählen, in der der Strom erhöht bleibt, würde auch
Pieptöne kürzer als ein Messwert erfassen, braucht aber die Periode der
Pieptöne, die nicht gemessen ist. Es wird nicht gemacht.

## Simulation

Ist das PD mini in SETUP INTERFACES aus, betreibt der Bildschirm SUPPLY das
Modell eines Netzteils im Panel. Während eines Stick-Laufs ist der Strom
dieses Modells ein simulierter ESC (`shared/esc/esc_sim.c`), der dem Profil
folgt: Einschalten in der Einstiegsstellung betritt das Menü nach dem
`hold_ms` des Profils (3000 ms ohne), Gruppen laufen mit 250 ms Pieptönen,
250 ms Lücken, 800 ms langen Pieptönen und 1500 ms zwischen Gruppen, bei
150 mA Ruhestrom und 600 mA mehr je Piepton. Jede dieser Zahlen ist
ausgedacht. Wo das Profil eine Speicherbewegung nennt, wird eine Auswahl
erst behalten, wenn der Knüppel sie macht. Ein Punkt mit dem Schlüssel
`exit` verlässt das Menü, wenn er gewählt wird, und einer mit `reset` löscht,
was gespeichert war. Die Messwerte des Modells kommen alle 50 ms.

Die Host-Suite (`test_esc_stick`) lässt den Ablauf von Ende zu Ende gegen
die Simulation laufen: zwei- und einstufige Menüs, wiederholte Gruppen, eine
Ruhestellung, Ein- und Ausschalten zwischen Änderungen, Messwerte 100 bis
190 ms auseinander, 30 mA Rauschen, ein verlorener Piepton in einer Gruppe,
ein Menü mit einem Punkt mehr als das Profil, ein ESC, der nie piept, und
jeder Weg, auf dem ein Lauf endet. Ein Durchlauf mit einem verlorenen
Piepton in jeder der ersten zehn Gruppen gegen ESC-Einstiege 2000 ms vor
und nach dem des Ablaufs speichert keinen anderen Wert als den verlangten.

## Aktuelle Einschränkungen

- Kein Lauf gegen einen ESC. Die Vorgaben und die Reihenfolgeregel sind an
  echten Pieptönen nicht erprobt.
- Ein ESC, der eine Auswahl ignoriert, gibt weiter seine Punkte aus, die der
  Lauf dann als Werte zählt: er kann DONE melden, ohne dass etwas
  gespeichert ist.
- Bei einem Menü, das länger oder kürzer als sein Profil ist, wird auf die
  niedrigste Nummer nie reagiert, weil die höchste der Schleife nicht die
  davor gehörte ist; der Lauf endet mit TIMEOUT.
- Eine Auswahl braucht drei Gruppen hintereinander in Reihenfolge und
  kostet also mindestens einen Durchgang der Schleife: bis zu etwa zwei
  Schleifen, wenn die gewünschte Nummer die niedrigste ist. Ein
  zweistufiges Menü, dessen Wertschleife beim gespeicherten Wert beginnt,
  wie das YGE-Handbuch es für sein RC-Setup beschreibt, kostet mehr.
- Die Reihenfolgeregel nimmt an, dass das Menü des ESCs das des Profils
  ist. Punkte, die es nur an manchen Modellen gibt, verschieben an den
  anderen die Nummern: in `hobbywing-flyfun-v5` gibt es Punkt 6
  (BEC-Spannung) an den Modellen mit 60, 80 und 120 A, in `ztw-gecko`
  Punkt 6 an den SBEC-Modellen. An einem Modell ohne den Punkt geben die
  Punkte danach womöglich eine Nummer weniger aus, als das Profil sagt.
  Einen davon zu wählen kann dann den Punkt danach wählen, eine Nummer
  höher im Profil; die
  Reihenfolgeregel merkt das nicht, weil das Menü in Reihenfolge ist, nur
  kürzer. Ein Profil je Modellsatz wäre die Abhilfe und ist nicht angelegt.
- `sunrise-pro`: das Handbuch schaltet die Bremse mit "the first quad group"
  um, die auch der Wert für automatisches Timing ist; Timing 4 zu speichern
  schaltet womöglich auch die Bremse um. Nicht gemessen.
- Die Gasstellungen sind 0, 50 und 100 % des Wegs des Ausgangs. Ein ESC, der
  auf andere Endpunkte kalibriert ist, liest sie womöglich anders.
- Nur ein Netzteil: das PD mini, höchstens 20 V. ESCs, die mehr brauchen,
  brauchen eine externe Versorgung, die der Prüfstand nicht schaltet.
