# Servo-Programmierer für KST und Chaservo

<sub>[English](ServoProgrammer.md) · **Deutsch**</sub>

Der Programmierer liest die 32 Register eines KST- oder Chaservo-Servos über
dessen Signalleitung, zeigt sie als Einstellungen und schreibt geänderte
Einstellungen zurück. Die zwei Marken teilen ein Protokoll und einen
Registersatz. Er ist ein Overlay auf dem
[SERVO-Bildschirm](Screens-de.md#servo) und wird mit **PROGRAMM.** oben auf
der rechten Karte geöffnet. **PROGRAMM.** beendet einen laufenden
automatischen Test als `vom Bediener gestoppt`.

**Kein Frame dieses Codes ist an ein Servo gesendet worden.** Der
Programmierer ist nur in der Host-Suite gegen ein Servo- und Leitungsmodell
gelaufen. Registerlayout und Timing stammen von 1 Programmierkarte und
1 Servo unbekannten Typs. Die Wirkung eines Registers auf die Bewegung des
Servos ist nicht gemessen: Namen, Einheiten und Limits sind die, die das
Hersteller-Tool anzeigt und annimmt.
[Was offen ist](#offene-messungen) nennt, was ein Prüfstand messen muss,
bevor man sich auf ein Schreiben verlässt.

![Der Programmierer, der Tester hält die Ausgänge](img/de/servo-kst.png)

## Randbedingungen

- Der Koprozessor spricht Protokoll 4.12 oder neuer. Mit einem älteren sagt
  das Overlay `Nicht verfügbar: Koprozessor-Firmware ohne KST-Port.`, und
  jedes Bedienelement außer **SCHLIESSEN** ist aus.

  ![Ein Koprozessor ohne den Port](img/de/servo-kst-nopage.png)

- Der Kanal ist auf OUTPUTS als SERVO PWM (Pulsweitenmodulation) gebunden.
  Ein Gaskanal wird abgelehnt.
- Ein Servo zur Zeit, auf einem Kanal.
- Ein Kanal ist in PWM oder im Programmieren, nie in beidem. Solange der
  Port einen Kanal hält, sendet der Tester keinen Puls darauf.
- Die Bring-up-Hardware schaltet die Versorgung eines Servos nicht. Der Weg
  zurück zu PWM ist, das Servo abzuziehen.
- Ob ein Ausgang einer gegebenen Hardware die Antwort des Servos zum Pin
  zurückführt, weiß der Koprozessor nicht. Ein Servo an einem Ausgang, der
  es nicht tut, beendet **VERBINDEN** mit
  `Kein Servo antwortet. Stecker, Versorgung, Kanal prüfen.`

## Der Port

| Zustand | Zeile auf dem Reiter SERVO | Der Kanal führt |
| --- | --- | --- |
| PWM | `Der Tester hat die Ausgänge. VERBINDEN nimmt Kanal 1.` | die Pulse des Testers |
| Verbinden | `Verbinde auf Kanal 1: kein PWM darauf.` | 50 ms keinen Puls, dann 100 ms Low, dann die Einstiegssequenz |
| Programmieren | `Kanal 1 ist im Programmiermodus: kein PWM darauf.` | nur Frames |
| Arbeiten | `Arbeite: 12 Frames.` | die Frames des laufenden Vorgangs |
| Verlassen | `Verlasse den Programmiermodus.` | nichts, bis der Port den Kanal zurückgegeben hat |

**KANAL** öffnet das Tastatur für 1 bis 16 und ist aus, solange der Port
einen Kanal hält.

![Das Tastatur für den Kanal](img/de/servo-kst-channel.png)

**VERBINDEN** nimmt den Kanal aus PWM, fährt die Einstiegssequenz und liest
alle 32 Register. Die Taste heißt **ABBRECHEN**, solange ein Vorgang läuft,
und **VERLASSEN** im Programmiermodus.

![Ein laufender Vorgang](img/de/servo-kst-working.png)

Solange der Port einen Kanal hält, bleibt das Overlay offen und
**OPTIONEN** ist aus. **ARM**, **ZENTRIEREN**, **FREIGEBEN**, die
Bedienelemente des Netzteils und **STOP** auf der rechten Karte arbeiten.
Das Verlassen des SERVO-Bildschirms lässt den Port, wie er ist; beim
nächsten Aufruf ist das Overlay wieder offen.

## Reiter SERVO

![Ein gelesenes Servo](img/de/servo-kst-read.png)

| Zeile | Taste | Wirkung |
| --- | --- | --- |
| KANAL | die Zahl | der Ausgangskanal, 1 bis 16 |
| PORT | **VERBINDEN**, **ABBRECHEN**, **VERLASSEN** | nimmt den Kanal, beendet den laufenden Vorgang, [gibt den Kanal zurück](#verlassen) |
| SERVO, dann SERVO V6 oder SERVO V8 | **ALLES LESEN** | liest die 32 Register, jedes, bis 3 von höchstens 5 Lesungen übereinstimmen; die Zeile nennt danach [den Servotyp](#der-servotyp) |
| DATEIEN | **WÄHLEN** | [speichert die Werte des Servos auf die Karte und lädt eine Datei](#servodateien) |
| ZURÜCKSPIELEN | **WÄHLEN** | [schreibt ein Backup zurück](#zurückspielen) |
| WEITERES | **WÄHLEN** | [PRÜFEN, ÄNDERUNGEN VERWERFEN, PAIRING LÖSEN](#weiteres) |
| SCHREIBEN AUF SERVO FREIGEBEN | **AUS**, **EIN** | [der Schreibschalter](#der-schreibschalter) |
| ÄNDERUNGEN | **n WERTE SCHREIBEN** | [schreibt die geänderten Werte](#schreiben) |

Unter den Zeilen stehen der Zustand des Ports, der Servotyp, der Name der
Backup-Datei der Sitzung und die letzte Meldung. Eine Meldung in der
Warnfarbe ist eine Ablehnung oder ein Ergebnis, das einen zweiten Blick
braucht; eine in der Gefahrenfarbe sagt, dass der Inhalt des Servos unklar
ist.

### Ein Servo mit unbekanntem Layout

Der Koprozessor vergleicht das Abbild mit dem Layout des bekannten Servos,
seinem Fingerprint. Ein Abbild, das nicht passt, wird gezeigt und nie
geschrieben: die Zeilen zum Ändern sind aus, ein Tipp auf den
Schreibschalter lässt ihn aus und wiederholt die Meldung, und **PAIRING
LÖSEN** wird abgelehnt. **ALLES LESEN**, das Speichern in eine Datei und
**PRÜFEN** arbeiten. Eine Datei wird nicht geladen.

![Ein Servo, das nicht zum Fingerprint passt](img/de/servo-kst-readonly.png)

## Ändern

Ein Wert wird in einem Entwurf auf dem Panel geändert. Vor
[**n WERTE SCHREIBEN**](#schreiben) erreicht nichts das Servo. Ein Wert,
der von dem des Servos abweicht, ist doppelt markiert: durch `*` davor und
durch die Akzentfüllung.

![Geänderte Werte auf TUNING](img/de/servo-kst-tune.png)

Ein Zahlenwert öffnet das Tastatur. Sein Bereich ist der, den die harten
Limits im Entwurf, wie er steht, annehmen; ein Wert außerhalb lässt sich
nicht eingeben. STRETCH öffnet eine Liste der 6 bestätigten Faktoren. Ein
EIN/AUS-Wert wechselt beim Tipp. DEAD BAND und NEUTRAL haben Minus- und
Plustasten: ein Rohzähler je Tipp, gehalten nach 0,45 s 8 Zähler je
Sekunde und nach weiteren 1,8 s 30 je Sekunde.

![Das Tastatur mit dem Bereich, den die Limits annehmen](img/de/servo-kst-keypad.png)
![Die Stretch-Liste](img/de/servo-kst-stretch.png)

### TUNING

| Zeile | Registerfeld | Hartes Limit |
| --- | --- | --- |
| DEAD BAND | Dead Band, µs | der Bereich des Felds |
| MAX. DUTY | Max. Duty, % | 10 bis 98 % |
| BOOST | Boost, µs | höchstens 3000 µs |
| STRETCH | Stretch, Faktor | x1 bis x32; die 2 übrigen Codes werden nicht angeboten |
| SOFT START | Soft_Start, EIN oder AUS | keines |
| TEMPOLIMIT | spd_sel, EIN oder AUS | EIN braucht TEMPO ab 5 % |
| TEMPO | SPD, % | höchstens 100 %; mindestens 5 %, solange TEMPOLIMIT EIN ist |

### PULS

![Der Reiter PULS](img/de/servo-kst-pulse.png)

| Zeile | Registerfeld | Hartes Limit |
| --- | --- | --- |
| NEUTRAL | Neutral, µs | 1360 bis 1640 µs; 100 µs oder mehr von jedem Endpunkt |
| PULSE LOWER | Pulse Lower, µs | 500 µs oder mehr; 100 µs oder mehr unter NEUTRAL; 400 µs oder mehr unter PULSE UPPER |
| PULSE UPPER | Pulse Upper, µs | 2500 µs oder weniger; 100 µs oder mehr über NEUTRAL |
| LEFT ANGLE | Left Angle, die Zahl des Hersteller-Tools | nicht 0 |
| RIGHT ANGLE | Right Angle, die Zahl des Hersteller-Tools | nicht 0 |
| REVERSION | Reversion, EIN oder AUS | keines |

Die Umrechnung von NEUTRAL zwischen Register und µs ist eine Anpassung an
3 Werte und um etwa 1 µs unsicher.

### FAILSAFE

![Der Reiter FAILSAFE](img/de/servo-kst-failsafe.png)

| Zeile | Registerfeld | Hartes Limit |
| --- | --- | --- |
| BEWEGEN BEI SIGNALVERLUST | Allow Uncont, EIN oder AUS | keines |
| POSITION | Uncontrolled Pos, µs | 500 bis 2500 µs und innerhalb PULSE LOWER bis PULSE UPPER |
| NACH | Uncontrolled Time, s | 0,4 bis 13,0 s |

Die Limits sind die aus `protocols/kst/kst_limits.c`; das Panel hält keine
eigenen. Das untere Limit von MAX. DUTY und die 5 % von TEMPO sind
Festlegungen, keine Messwerte eines Servos.

### Ein Wert, den ein hartes Limit ablehnt

Ein Tipp, der ein hartes Limit verletzen würde, ändert nichts und nennt das
Limit.

![Ein abgelehnter Schalter](img/de/servo-kst-hard.png)

### Ein Wert, der einen zweiten Blick braucht

Ein Wert innerhalb der harten Limits, der ein weiches Limit verletzt, öffnet
ÄNDERUNG PRÜFEN mit dem alten und dem neuen Wert und einer Zeile je Limit.
**HALTEN ZUM ÜBERNEHMEN** übernimmt ihn nach 2 s Halten; **ABBRECHEN** oder
ein Finger, der von der Taste rutscht, lässt den Entwurf, wie er war.

![Ein weiches Limit](img/de/servo-kst-soft.png)

| Bedingung | Warnung |
| --- | --- |
| MAX. DUTY über dem des Servos | `Mehr Duty erhöht Blockierstrom und Wärme.` |
| MAX. DUTY unter 50 % | `Wenig Duty schwächt das Servo: Ruder evtl. nicht gehalten.` |
| BOOST über dem des Servos | `Mehr Boost kann das Servo brummen und heiß werden lassen.` |
| STRETCH über dem des Servos | `Mehr Stretch kann das Servo schwingen lassen.` |
| SOFT START ausgeschaltet | `Ohne Soft Start springt das Servo beim Einschalten.` |
| TEMPOLIMIT eingeschaltet | `Tempolimit aktiv: das Servo fährt mit dem gesetzten Tempo.` |
| TEMPO unter 30 % bei TEMPOLIMIT EIN | `Ein langsames Servo kann die Kontrolle am Modell kosten.` |
| DEAD BAND unter dem des Servos | `Ein schmales Dead Band kann Pendeln und Wärme verursachen.` |
| DEAD BAND unter 1,0 µs | `Sehr schmales Dead Band: Pendeln und Wärme wahrscheinlich.` |
| BEWEGEN BEI SIGNALVERLUST eingeschaltet | `Ohne Signal fährt das Servo auf die Failsafe-Position.` |
| POSITION mehr als 300 µs von NEUTRAL, BEWEGEN BEI SIGNALVERLUST EIN | `Die Failsafe-Position liegt weit von der Mitte.` |
| NACH unter 1,0 s | `Kurze Aussetzer lösen die Failsafe-Bewegung aus.` |
| ein Endpunkt weniger als 300 µs von NEUTRAL | `Endpunkt nah an Neutral: wenig Weg auf dieser Seite.` |
| ein Endpunkt mehr als 100 µs von dem des Servos | `Endpunkt geändert: der Weg je Knüppelweg ändert sich.` |
| NEUTRAL mehr als 20 µs von dem des Servos | `Die Mitte wandert; eine Seite rückt näher an den Anschlag.` |
| REVERSION geändert | `Richtung ist umgekehrt. Vor dem Flug jedes Ruder prüfen.` |
| ein Winkel über dem des Servos | `Mehr Weg kann das Servo in den Anschlag fahren: es brennt.` |
| ein Winkel unter 50 | `Kleiner Winkel: wenig Weg für den vollen Knüppelweg.` |
| DEAD BAND unter 1,0 µs bei STRETCH über x1 | `Schmales Dead Band mit mehr Gain: Schwingen droht.` |
| MAX. DUTY über 90 % bei einem Winkel über dem des Servos | `Viel Duty, mehr Weg: Blockieren kann das Servo zerstören.` |

"Der des Servos" ist der zuletzt aus dem Servo gelesene Wert.

### GESPERRT

![Der Reiter GESPERRT](img/de/servo-kst-locked.png)

| Zeile | Feld |
| --- | --- |
| TYP | 20k_sel, gezeigt als [der Servotyp](#der-servotyp), V6 oder V8 |
| LOCK | Lock |
| MODE | Servo oder Gyro Servo |
| PROT1 EN, PROT1 CANCLE, PROT1 TIME, PROT1 PWM, POT ERR, POT SAMPLES, PWM ERR | der Schutzblock, den das Hersteller-Tool verbirgt; `PROT1 CANCLE` ist die Schreibweise des Herstellers |

Diese Werte werden gezeigt, und kein Ändern erreicht sie. Ihre Funktion
oder ihre Kodierung ist nicht bestätigt. Der Reiter zeigt außerdem den
Pairing-Wert, Register 1D, und die Kartennummer, die das Hersteller-Tool
für dieses Servo braucht. Ein Servo im Modus Gyro Servo wird gelesen und
nicht geschrieben: die Limits dieses Modus sind nicht bekannt.

## Der Schreibschalter

**SCHREIBEN AUF SERVO FREIGEBEN** ist in der Vorgabe aus. Ein Tipp auf
**AUS** öffnet eine Seite, die die Risiken nennt; **HALTEN ZUM FREIGEBEN**
schaltet ihn nach 2 s ein. Ein Tipp auf **EIN** schaltet ihn sofort aus.

![Das Schreiben freigeben](img/de/servo-kst-enable.png)

Der Schalter geht aus:

- bei jedem Aufruf des SERVO-Bildschirms,
- wenn der Link zum Koprozessor wegfällt oder der Koprozessor neu startet,
- bei **STOP** und bei jedem Stopp, den der Prüfstand hält,
- mit jedem **VERBINDEN** und jedem **VERLASSEN**,
- wenn ein Lesen ein anderes Abbild findet als das gehaltene,
- wenn ein Lesen nicht zum Fingerprint passt.

Bei ausgeschaltetem Schalter sendet das Panel kein Schreiben, kein
Zurückspielen und kein Lösen des Pairings, und der Koprozessor lehnt eines
ab, das ohne den Schlüssel des Schalters ankommt. Lesen, **PRÜFEN**, das
Speichern in eine Datei und das Laden einer Datei arbeiten bei
ausgeschaltetem Schalter.

## Schreiben

**n WERTE SCHREIBEN** zählt die Felder, in denen der Entwurf vom Servo
abweicht. Die Taste ist aus, wenn nichts geändert ist oder der Schalter aus
ist.

![Drei geänderte Werte, der Schalter ein](img/de/servo-kst-changes.png)

Ein Tipp öffnet die Schreibseite. **HALTEN ZUM SCHREIBEN** startet das
Schreiben nach 2 s. Das erste Schreiben nach einem **VERBINDEN** zeigt
`Ruderhorn frei, kein Gestänge angeschlossen.` als erste Zeile. Ein
Schreiben, dessen Start die Karte, ein Stopp oder der Link verhindert hat,
ist nicht dieses erste Schreiben: die Zeile bleibt für das nächste.

![Die Schreibseite](img/de/servo-kst-write.png)

1. Das erste Schreiben einer Sitzung speichert die Werte des Servos in
   [eine neue Datei auf der Karte](#das-backup-vor-dem-ersten-schreiben).
   Eine eingehängte Karte, die die Datei nicht annimmt, beendet das
   Schreiben. Ohne eingehängte Karte sagt die Schreibseite, dass keine
   Datei gespeichert wird, und das Schreiben läuft ohne eine.
2. Das Panel vermerkt das Abbild, das das Servo hält, das Abbild, das das
   Schreiben hinterlassen soll, und den Kanal: mit Karte in `KST.TXT`, ohne
   im RAM (Arbeitsspeicher). Die Seite sagt, welches. Mit einer Karte, die
   binnen 3 s keine Datei annimmt, wird das Schreiben nicht gesendet:
   `Die Karte nahm keine Datei. Nichts ging aufs Servo.`
3. Das Panel sendet das Zielabbild. Der Koprozessor prüft es gegen dieselben
   Limits, plant die Registerschreibvorgänge und führt sie aus.
4. Jedes geschriebene Register wird zurückgelesen. Ein Schreibvorgang, der
   nicht übernommen wird, wird rückgängig gemacht.
5. `3 Werte geschrieben und geprüft.` beendet es und löscht den Vermerk.

Nur Register, deren Wert sich ändert, werden geschrieben. Register 00 wird
nie geschrieben; Register 1D schreibt nur **PAIRING LÖSEN**.

![Ein Schreiben ist fertig](img/de/servo-kst-written.png)

| Meldung | Das Servo |
| --- | --- |
| `Schreiben nicht übernommen. Nichts ist halb geschrieben.` | hält, was es hielt |
| `Schreiben nicht übernommen. Der Wert ist wieder wie zuvor.` | hält, was es hielt |
| `Schreiben nicht übernommen und nicht rückgängig zu machen.` | ist unklar: [zurückspielen](#ein-unterbrochenes-schreiben) |
| `Ein Register änderte sich, das nicht geschrieben wurde.` | ist unklar |
| `Das Servo hält nicht die erwarteten Werte.` | ist unklar |

Eine Meldung über ein fehlgeschlagenes Schreiben nennt das Register und die
Zahl der erfolgten Schreibvorgänge: `Register 03, 1 Schreibvorgänge erfolgt.`

## Backups

| Backup | Gehalten von | Lebt bis |
| --- | --- | --- |
| Der Sitzung | dem Koprozessor: das erste nach **VERBINDEN** gelesene Abbild | **VERLASSEN**, ein Neustart des Koprozessors |
| Die Datei der Sitzung | der Karte: die 32 Register des Servos, wie sie vor dem ersten Schreiben der Sitzung stehen. Keine, solange keine Karte eingehängt ist | die Datei wird von der Karte entfernt |
| Vor einem Schreiben | dem Panel, in `KST.TXT` oder im RAM | das Schreiben endet geprüft, oder der Vermerk wird verworfen |

`KST.TXT` ist der Vermerk eines laufenden Schreibens: Text von höchstens
400 Bytes im Wurzelverzeichnis der Karte mit der Zeile `RCBENCH KST 1`, den
Zeilen `journal=`, `target=` und `channel=` und `check=` mit der CRC-16
(zyklische Redundanzprüfung) der Bytes davor. Eine Datei, deren Prüfsumme
nicht stimmt, wird nicht benutzt:
`KST.TXT auf der Karte ist nicht lesbar, bleibt ungenutzt.`

Das Panel schreibt den Text nach `KST.NEW`, und die Datei bekommt den Namen
`KST.TXT`, sobald die Karte sie vollständig hält. Ein Schreiben, das die
Karte oder die Versorgung beendet, hinterlässt `KST.TXT` wie zuvor oder
eine vollständige `KST.NEW`. Beim Einschalten wird eine `KST.NEW` mit
stimmender Prüfsumme benutzt und umbenannt; eine mit falscher Prüfsumme
wird entfernt und `KST.TXT` benutzt. Eine Datei, die die Karte nicht
annimmt, wird alle 5 s erneut geschrieben, bis die Karte sie annimmt, damit
ein auf dem Panel verworfener Vermerk nicht auf der Karte bleibt. Nicht auf
einer Karte gelaufen.

## Servodateien

Eine Servodatei ist eine Textdatei auf der SD-Karte des Panels mit den
32 Registern eines Servos. **DATEIEN** öffnet die Liste. Die Zeile ist im
Programmiermodus aktiv, wenn ein Servo gelesen und nicht unklar ist; sonst
antwortet der Tipp mit `Zuerst das Servo lesen: ALLES LESEN.` Speichern und
Laden arbeiten bei ausgeschaltetem Schreibschalter: eine Datei ändert den
Entwurf auf dem Panel, nie das Servo.

![Die Dateien auf der Karte](img/de/servo-kst-files.png)

Der erste Eintrag speichert. Darunter stehen die Dateien, 5 je Seite, jede
mit ihrem Servotyp; eine Datei ohne Typzeile zeigt keinen. Bei mehr als
5 Dateien zählt der Titel sie, `DATEIEN 1 BIS 5 VON 7`, und
`Weitere Dateien zeigen` blättert, nach der letzten Seite zurück zur
ersten. Das Panel fragt die Karte für jede Seite und wartet höchstens 5 s
darauf.

![Die zweite Seite von 7 Dateien](img/de/servo-kst-files-next.png)
![Eine Karte ohne Servodateien](img/de/servo-kst-files-none.png)

### Der Servotyp

Ein Servo und eine Datei sind V6 oder V8. Das Feld 20k_sel entscheidet:
1 ist V6, Ansteuerung mit 20 kHz; 0 ist V8, Ansteuerung mit 333 Hz. Kein
anderes Register geht in den Typ ein. Sobald ein Servo gelesen ist, heißt
die Zeile **SERVO** `SERVO V6` oder `SERVO V8`, und die Zeile unter den
Tasten sagt `Servotyp V6: Ansteuerung mit 20 kHz.` Der Typ wird nie
geändert und nie geschrieben.

![Ein V8-Servo und eine V6-Datei](img/de/servo-kst-file-v6.png)

### Speichern

`Werte dieses Servos als neue Datei speichern` öffnet die Texttastatur mit
dem nächsten freien Vorgabenamen, `KST001` bis `KST999`. Der Name ist frei
änderbar. **OK** speichert; die Kartenseite prüft den Namen und ersetzt nie
eine Datei.

![Der Name einer neuen Datei](img/de/servo-kst-name.png)

| Ergebnis | Gezeigt |
| --- | --- |
| Gespeichert | `Servowerte auf der Karte gespeichert: WING-R.` |
| Ein Zeichen, eine Länge oder ein Name, den die Kartenseite nicht erlaubt | die Tastatur bleibt offen unter `NAME NICHT ERLAUBT`; `Name nicht erlaubt. Erlaubt: A-Z, 0-9, - und _.` |
| Eine Datei hat den Namen | die Tastatur bleibt offen unter `NAME VERGEBEN: ÄNDERN`; `Der Name ist vergeben. Keine Datei wird ersetzt.` |
| Keine Karte | `Keine Karte. Nichts gespeichert, nichts aufs Servo.` |
| Karte voll | `Karte voll. Nichts gespeichert, nichts aufs Servo.` |
| Die Karte nimmt die Datei nicht an oder antwortet nicht binnen 5 s | `Schreibfehler der Karte. Nichts ging aufs Servo.` |

![Ein vergebener Name](img/de/servo-kst-name-exists.png)
![Ein nicht erlaubter Name](img/de/servo-kst-name-bad.png)
![Eine gespeicherte Datei](img/de/servo-kst-name-saved.png)
![Keine Karte](img/de/servo-kst-card-none.png)
![Eine volle Karte](img/de/servo-kst-card-full.png)

Ein Speichern schreibt das Abbild wie gelesen, die gesperrten Felder
eingeschlossen, auch das eines Servos, das nicht zum Fingerprint passt.

### Das Backup vor dem ersten Schreiben

Das erste Schreiben, Zurückspielen oder Lösen des Pairings einer Sitzung
speichert das Abbild des Servos ohne Nachfrage in eine neue Datei unter dem
nächsten freien Vorgabenamen. Die Schreibseite sagt
`Die Werte des Servos gehen zuerst in eine neue Datei.` Das Servo empfängt
nichts, bevor die Karte die Datei hält. Eine eingehängte Karte, die voll
ist, beim Schreiben einen Fehler meldet oder zwischen Anfrage und Datei
entnommen wird, beendet das Schreiben mit den Zeilen der Tabelle oben, und
nichts wird auf das Servo geschrieben. Platz auf der Karte schaffen oder
die Karte entnehmen lässt das Schreiben laufen. Der Reiter SERVO nennt die
Datei danach: `Backup-Datei dieser Sitzung: KST003.` Ein Speichern von Hand
desselben Abbilds zählt als diese Datei. Ein Lesen, das andere Werte
findet, lässt das nächste Schreiben erneut speichern.

Ein unklares Servo ist die Ausnahme: sein Abbild ist keine Datei wert, und
das Zurückspielen des Backups der Sitzung läuft ohne eine.

#### Ohne Karte

Ohne eingehängte Karte sind das Schreiben, das Zurückspielen und das Lösen
des Pairings nicht gesperrt. Die Seite, deren Halten das Schreiben sendet,
trägt drei weitere Zeilen:

```
Keine Karte: es wird keine Backup-Datei gespeichert.
Das Backup dieser Sitzung liegt nur im Prüfstand.
Sitzungsende oder Stromausfall am Prüfstand löscht es.
```

Das eine Halten der Seite von 2 s bestätigt sie; ein zweites Halten wird
nicht verlangt. Ein vor 2 s losgelassenes Halten sendet nichts.

![Die Schreibseite ohne Karte](img/de/servo-kst-write-nocard.png)

Nach einem solchen Schreiben zeigt der Reiter SERVO
`Keine Backup-Datei: keine Karte. Nur Backup der Sitzung.` anstelle des
Dateinamens. Die einzige Kopie des Servos, wie es vorgefunden wurde, ist
das Backup der Sitzung im Koprozessor: **VERLASSEN**, ein Neustart des
Koprozessors und ein Stromausfall des Prüfstands beenden es, und die Werte,
die das Servo vor dem Schreiben hielt, sind dann aus dem Prüfstand nicht
wiederherstellbar.

![Der Reiter SERVO nach einem Schreiben ohne Karte](img/de/servo-kst-nofile.png)

| Fall | Ergebnis |
| --- | --- |
| Eine Karte wird später in der Sitzung eingehängt | das nächste Schreiben, Zurückspielen oder Lösen des Pairings speichert zuerst die Backup-Datei. Die Datei hält das Backup der Sitzung, das vor dem ersten Schreiben gelesene Abbild, solange der Koprozessor es hält; ohne es hält die Datei das Servo, wie es zuletzt gelesen wurde. Die Zeile im Reiter SERVO wechselt zum Dateinamen |
| Ein Speichern über **DATEIEN** nach einem Schreiben ohne Karte | speichert das Servo, wie es steht; es zählt nicht als Backup-Datei |
| Die Karte wird entnommen, während eine Seite offen ist, die die Datei zusagt | das Halten sendet nichts, und die Seite kommt mit den drei Zeilen zurück; ihr eigenes Halten von 2 s sendet das Schreiben |
| Die Karte wird entnommen, nachdem die Backup-Datei gespeichert ist | spätere Schreiben der Sitzung werden gesendet; der Vermerk des Schreibens bleibt im RAM |
| **DATEIEN**, Speichern und Laden ohne Karte | abgelehnt: `Keine Karte. Nichts gespeichert, nichts aufs Servo.` |

### Laden

Ein Tipp auf eine Datei lädt sie. Ihre änderbaren Felder werden der
Entwurf, genau wie auf den Reitern eingegebene Änderungen: geänderte Werte
sind markiert, **n WERTE SCHREIBEN** zählt sie, und nichts erreicht das
Servo vor dem Schreibschalter, der Schreibseite und deren 2 s Halten.

![Eine geladene Datei](img/de/servo-kst-file-loaded.png)

| Datei | Gezeigt |
| --- | --- |
| Vom Prüfstand gespeichert, Prüfsumme intakt | `KST001 geladen: 3 Werte weichen vom Servo ab.` |
| Bearbeitet, mit Typzeile und ohne Prüfsummenzeile | dasselbe und `Bearbeitete Datei, Werte wie Handeingabe geprüft.` |
| Bearbeitet, ohne Typzeile | dasselbe und `Bearbeitete Datei ohne Servotyp, wie Handeingabe geprüft.` |
| Enthält die änderbaren Werte des Servos | `KST001 enthält die Werte des Servos: nichts zu schreiben.` |
| Vom anderen Servotyp | `Datei ist V8, Servo ist V6. Nicht geladen.` |
| Prüfsummenzeile vorhanden und falsch | `Datei beschädigt: Prüfsumme passt nicht. Nicht geladen.` `Ohne Prüfsummenzeile lädt sie als bearbeitete Datei.` |
| Keine Servodatei | `Keine Servodatei. Nicht geladen.` |
| Eine Zeile, die sich nicht lesen lässt | `Zeile 12 der Datei ist nicht lesbar. Nicht geladen.` |
| Von einer späteren Formatversion geschrieben | `Die Datei braucht eine neuere Version. Nicht geladen.` |
| Ein Wert außerhalb seines harten Limits | `Zeile 9: MAX. DUTY außer Grenze. Nicht geladen.` |
| Werte, die zusammen ein hartes Limit verletzen | `Die Datei verletzt eine Grenze, nicht geladen:` und die Zeile des Limits |
| Die Karte lässt sich nicht lesen oder antwortet nicht binnen 5 s | `Die Karte lässt sich nicht lesen.` |
| Keine Servodatei auf der Karte | `Keine Servodateien auf der Karte.` |

![Eine Datei des anderen Typs](img/de/servo-kst-file-type.png)
![Eine beschädigte Datei](img/de/servo-kst-file-damaged.png)
![Eine bearbeitete Datei](img/de/servo-kst-file-edited.png)
![Eine bearbeitete Datei ohne Typzeile](img/de/servo-kst-file-untyped.png)
![Eine Datei mit den Werten des Servos](img/de/servo-kst-file-same.png)
![Eine Zeile, die sich nicht lesen lässt](img/de/servo-kst-file-bad.png)
![Eine Datei eines späteren Formats](img/de/servo-kst-file-newer.png)
![Ein Wert außerhalb seines harten Limits](img/de/servo-kst-file-hard.png)

Eine Datei, deren Werte ein weiches Limit überschreiten, das die Werte des
Servos einhalten, öffnet DIESE DATEI PRÜFEN, bevor etwas zum Entwurf wird.
Die Seite listet dieselben Warnzeilen wie eine Änderung über diese Limits
hinaus, höchstens 5; bei mehr zählt die letzte Zeile den Rest. **HALTEN ZUM
LADEN** für 2 s übernimmt die Werte in den Entwurf. **ABBRECHEN** lässt den
Entwurf, wie er war.

![Die Warnungen einer Datei](img/de/servo-kst-file-soft.png)

### Was eine Datei nicht zurückspielt

Ein Laden schreibt nur die änderbaren Felder. Diese bleiben, wie das Servo
sie hält, was immer die Datei sagt:

- der Servotyp, 20k_sel,
- Lock,
- der Modus, Servo oder Gyro Servo,
- der Schutzblock des Reiters GESPERRT,
- der Pairing-Wert, Register 1D,
- Register 00 und jedes Bit, das zu keinem benannten Feld gehört.

Eine Datei, die dort abweicht, wird geladen, und die Abweichung wird
gezählt: `2 gesperrte Werte weichen ab: nicht geschrieben.` Der Reiter
GESPERRT markiert jeden solchen Wert mit `!`. Eine Datei ohne Typzeile, die
das 20k_sel des anderen Typs enthält, wird genauso geladen, denn nur die
Typzeile lehnt ein Laden ab.

![Gesperrte Werte, die abweichen](img/de/servo-kst-file-locked.png)

Das Zurückspielen des Backups der Sitzung ist der einzige Weg, ein
gesperrtes Register zu schreiben, und es endet mit der Sitzung.

## Zurückspielen

**ZURÜCKSPIELEN** öffnet die Liste dessen, was sich zurückspielen lässt.
Jedes Zurückspielen braucht den Schreibschalter und 2 s Halten.

![Die Liste zum Zurückspielen](img/de/servo-kst-restore-list.png)
![Das Backup der Sitzung zurückspielen](img/de/servo-kst-restore.png)

| Quelle | Zurückgeschrieben | Nicht zurückgespielt |
| --- | --- | --- |
| `Backup dieser Sitzung, wie zuerst gelesen` | jedes abweichende Register, vom Koprozessor aus dem Abbild, das er selbst hält; ohne die Limit-Prüfungen, und die Seite sagt es | nichts |
| `Werte vor dem unterbrochenen Schreiben` | die änderbaren Felder, als Schreiben mit jeder Limit-Prüfung | die gesperrten Felder, Register 00 und der Pairing-Wert |

Ein Zurückspielen aus dem Abbild des Panels ist ein gewöhnliches
Schreiben: der Koprozessor nimmt ein ungeprüftes Zurückspielen nur für das
Backup an, das er selbst hält, und diese Prüfung wird nicht abgeschwächt.
Die Seite zählt die gesperrten Werte, die abweichen und bleiben:
`2 gesperrte Werte weichen ab und bleiben.` Eine Servodatei steht nicht in
dieser Liste: [ein Laden](#laden) macht sie zum Entwurf, und das
gewöhnliche Schreiben sendet ihn.

Ein Backup, dessen Werte ein weiches Limit überschreiten, das die Werte des
Servos einhalten, öffnet zuerst BACKUP PRÜFEN. Die Seite listet die
Warnungen dieser Limits, dieselben Zeilen wie bei einer Änderung über sie
hinaus, höchstens 5; bei mehr als 5 zählt die fünfte Zeile den Rest.
**HALTEN ZUM FORTFAHREN** für 2 s öffnet die Seite des Zurückspielens, die
ein eigenes Drücken und 2 s Halten braucht. Das Zurückspielen des Backups
der Sitzung wird genauso geprüft, solange die Werte des Servos bekannt sind.

![Die Warnungen eines Backups](img/de/servo-kst-restore-check.png)

## Weiteres

![WEITERES](img/de/servo-kst-more.png)

- **PRÜFEN** liest alle Register und vergleicht sie mit dem gehaltenen
  Abbild: `Geprüft: das Servo hält, was gelesen wurde.` oder die Zahl der
  abweichenden Register.
- **ÄNDERUNGEN VERWERFEN** setzt den Entwurf auf die Werte des Servos
  zurück.
- **PAIRING LÖSEN** setzt Register 1D auf 00, nach dem Schreibschalter und
  2 s Halten. Was das Servo mit dem Wert tut, ist nicht bekannt. Das
  Zurückspielen des Backups der Sitzung setzt ihn zurück.

![Das Pairing lösen](img/de/servo-kst-release.png)

## Ein unterbrochenes Schreiben

Ein Schreiben gilt als unterbrochen, wenn der Link wegfällt, der
Koprozessor oder das Panel neu startet, **ABBRECHEN** oder ein Stopp es
beendet oder binnen 30 s kein Ergebnis kommt. Der Vermerk von vor dem
Schreiben bleibt dann stehen, auf der Karte auch über ein Aus- und
Einschalten des Prüfstands.

![Ein unterbrochenes Schreiben](img/de/servo-kst-doubt.png)

`Schreiben unterbrochen. Servo ist evtl. halb beschrieben.` bleibt stehen,
bis das Servo gelesen ist, darunter der Kanal des Schreibens: `Nicht
benutzen. An Kanal 4 verbinden und ALLES LESEN.` Ein beim Einschalten
gefundener Vermerk stellt **KANAL** auf diesen Kanal. Jedes Bedienelement
außer **VERBINDEN**, **ALLES LESEN**, **ZURÜCKSPIELEN** und **VERLASSEN**
ist aus. Das Lesen vergleicht das Servo mit dem Vermerk:

| Das Servo hält | Meldung | Danach |
| --- | --- | --- |
| das Abbild vor dem Schreiben | `Das Servo hält, was es vor dem Schreiben hielt.` | der Vermerk ist gelöscht |
| das Abbild, das das Schreiben hinterlassen sollte | `Das Servo hält die Werte des unterbrochenen Schreibens.` | der Vermerk ist gelöscht |
| etwas anderes | `Das Servo hält eine Mischung alter und neuer Werte.` `Servo nicht benutzen. Backup zurück: ZURÜCKSPIELEN.` | **ZURÜCKSPIELEN** bietet das Backup der Sitzung an, solange die Sitzung lebt, und `Werte vor dem unterbrochenen Schreiben` |

![Das Servo hält eine Mischung](img/de/servo-kst-journal.png)

`Vermerk des unterbrochenen Schreibens verwerfen` in der Liste entfernt den
Vermerk ohne ein Schreiben.

Der Vermerk gehört zu einem Servo. Ein Servo, das an einem anderen Kanal
gelesen wird oder vom Vermerk in einem Wert abweicht, den weder das
Schreiben noch eine Änderung verändert, ist nicht dieses Servo: `Das ist
nicht das Servo des unterbrochenen Schreibens.` `Es war an Kanal 4.
ZURÜCKSPIELEN: Vermerk verwerfen.` Solange das steht, wird nichts
zurückgespielt, geändert oder geschrieben, und **ZURÜCKSPIELEN** enthält
nur den Eintrag zum Verwerfen. **VERLASSEN** und ein **VERBINDEN** des
Servos des Schreibens bringen den Vergleich von oben. Zwei Servos am selben
Kanal mit gleichen gesperrten Werten werden nicht unterschieden.

![Ein anderes Servo als das des Vermerks](img/de/servo-kst-other.png)

Max. Duty liegt in den Registern 01 und 02. Ein Schreiben, das zwischen
diesen beiden abreißt, hinterlässt ein Abbild, das ein hartes Limit
verletzt, und das Zurückspielen des Panels, ein geprüftes Schreiben,
startet von einem solchen Abbild nicht. Das Zurückspielen des Backups der
Sitzung tut es. Nach einem Neustart des Koprozessors in diesem Zustand
liest der Programmierer das Servo und schreibt nichts darauf.

## Verlassen

**VERLASSEN**, oder **SCHLIESSEN** im Programmiermodus, öffnet
PROGRAMMIEREN VERLASSEN. Ein Servo, das einen Frame erhalten hat, bleibt im
Programmiermodus, bis seine Versorgung aus war, und der Port gibt den Kanal
nicht an PWM zurück, bevor ihm das gesagt wird.

![Verlassen](img/de/servo-kst-leave.png)

Auf Hardware, die die Versorgung des Servos nicht schaltet, die
Bring-up-Hardware eingeschlossen: das Servo 10 s abziehen, dann **HALTEN:
SERVO WAR ABGEZOGEN** 2 s halten. Das Halten ist die Aussage des Bedieners;
der Prüfstand misst nicht, dass das Servo stromlos war. Auf Hardware, deren
Port eine geschaltete Versorgung meldet, heißt die Taste **HALTEN:
VERSORGUNG SCHALTEN**: der Port schaltet sie 6 s aus und wartet 3 s. Keine
solche Hardware hat diesen Code ausgeführt.

Die Seite warnt, bevor geänderte Werte verloren gehen und bevor eine
Sitzung endet, die ein unklares Servo noch zurückspielen könnte. Danach
steht `Der Tester hat den Ausgang wieder.`, und die Bedienelemente des
Testers treiben den Kanal wie zuvor.

## Stopps und Ausfälle

| Ereignis | Wirkung |
| --- | --- |
| **STOP**, ein gehaltener Stopp des Prüfstands | der laufende Vorgang endet nach dem Frame auf der Leitung, höchstens 12 ms; der Schalter geht aus; `STOP: der Vorgang wurde abgebrochen.`; der Port verlangt **ALLES LESEN** vor allem anderen |
| **ABBRECHEN** | dasselbe, ohne den gehaltenen Stopp |
| Der Link fällt weg | der Schalter geht aus, offene Seiten schließen; ein laufendes Schreiben ist [unterbrochen](#ein-unterbrochenes-schreiben) |
| Der Koprozessor startet neu | seine Sitzung und sein Backup sind weg; `Koprozessor neu gestartet. VERBINDEN und ALLES LESEN.` |
| Die Control-Task des Panels nimmt 2 s kein Kommando an | nichts wird gesendet; `Kein Ergebnis vom Koprozessor. ALLES LESEN.` |
| 3 Austausche mit dem Koprozessor in Folge bleiben unbeantwortet | `Keine Antwort vom Koprozessor. ALLES LESEN.`; nach einem Lesen oder Prüfen werden die Werte auf dem Panel vor einem **ALLES LESEN**, das endet, nicht geändert, gespeichert oder geschrieben |
| 30 s kein Ergebnis eines Vorgangs | der Vorgang wird abgebrochen; `Kein Ergebnis vom Koprozessor. ALLES LESEN.`; die Werte auf dem Panel werden vor einem **ALLES LESEN**, das endet, nicht geändert, gespeichert oder geschrieben; ein Schreiben ist [unterbrochen](#ein-unterbrochenes-schreiben) |

Jede Ablehnung des Ports und jedes Ergebnis der Sitzung hat seine eigene
Zeile. [Die Link-Page](Link-de.md#der-kst-programmierport) listet die
Codes.

## Einschränkungen

- An keinem Servo getestet. Jede Aussage über das Verhalten eines Servos
  auf dieser Seite ist die des Modells.
- Kein Speichern, Auflisten oder Laden einer Servodatei ist auf einer
  Karte gelaufen. Die Panel-Firmware dieses Zweigs beantwortet jedes
  Speichern mit einem Schreibfehler und jedes Auflisten und Laden mit
  einem Lesefehler, bis das Servodatei-Modul an die Karte angeschlossen
  ist. Mit eingehängter Karte wartet das erste Schreiben einer Sitzung
  auf seine Datei, also sendet diese Firmware nur ohne eingehängte Karte
  ein Schreiben an ein Servo.
- Ein Schreiben ohne Karte hinterlässt keine Backup-Datei. Das Backup der
  Sitzung ist dann die einzige Kopie des Servos, wie es vorgefunden wurde,
  und endet mit **VERLASSEN**, einem Neustart des Koprozessors oder einem
  Stromausfall des Prüfstands.
- Das erste Schreiben einer Sitzung mit eingehängter Karte braucht freien
  Platz auf ihr.
- Eine Servodatei trägt den Servotyp, V6 oder V8, und keine weitere
  Identität. Die Datei eines V8-Servos lädt in jedes V8-Servo.
- Eine Servodatei spielt nur die änderbaren Felder zurück:
  [was sie nicht zurückspielt](#was-eine-datei-nicht-zurückspielt).
- Die Liste zeigt 5 Dateien je Seite und hat weder Suche noch eigene
  Sortierung; die Reihenfolge ist die der Kartenseite. Dateien werden auf
  dem Panel nicht entfernt oder umbenannt.
- Der Vermerk eines unterbrochenen Schreibens nennt seinen Kanal und hält
  die Werte des Servos. Ein zweites Servo an diesem Kanal mit denselben
  gesperrten Werten wird für das erste gehalten.
- Das Ersetzen von `KST.TXT` über `KST.NEW` ist nicht auf einer Karte
  gelaufen.
- Ohne Karte geht der Vermerk eines Schreibens beim Ausschalten verloren.
- Der beim Einschalten gefundene Vermerk eines unterbrochenen Schreibens
  wird nur im Programmierer gezeigt; kein anderer Bildschirm meldet ihn.
- Ein Servo, dessen gelesenes Abbild ein hartes Limit verletzt, lässt sich
  lesen und speichern. Ein Schreiben darauf wird abgelehnt.
- LEFT ANGLE und RIGHT ANGLE sind die Zahlen des Hersteller-Tools; ihre
  Einheit ist nicht bekannt.
- Die Versorgungsspannung, die ein Servo im Programmiermodus braucht, ist
  nicht bekannt.

## Offene Messungen

Keine davon ist erfolgt.

1. Die Elektrik der Leitung auf dem Pfad des Ports: Pegel von Prüfstand und
   Servo, welche Seite die Leitung in Ruhe auf Low hält, der Pull-down, die
   Ausgangsimpedanz des Servos und die Zeit, in der beide Seiten treiben.
2. Der Zustand des Motors im Programmiermodus und ob ein geschriebener Wert
   sofort wirkt.
3. Was das Servo im Programmiermodus mit einem PWM-Puls tut und außerhalb
   davon mit einem Frame.
4. Persistenz: ob ein geschriebener Wert ein Aus- und Einschalten
   übersteht und ob es einen Commit-Schritt gibt.
5. Zuverlässigkeit des Schreibens: 1000 Schreibvorgänge mit Rücklesen, die
   Zahl der nicht übernommenen, die Zahl der ungefragt geänderten Register,
   die Streuung der Antwortverzögerung.
6. Die Lesefehlerrate an 1 m Leitung, während andere Servos an
   benachbarten Ausgängen laufen: 100.000 Lesungen.
7. Das Timing von Einstieg und Ausstieg: wie lange stromlos den
   Programmiermodus beendet.
8. Die Funktion jeder änderbaren Einstellung an einem Servo, das verloren
   gehen darf: Position, Stellzeit, Blockierstrom, Ruhestrom und Schwingen
   über dem Wert.
9. Die Toleranz der Senderate um 25,40 µs je Halbzelle und die kürzeste
   Pause zwischen Frames, die das Servo annimmt.
10. Ein zweites Servo desselben Typs und andere Generationen: Fingerprint,
    Antworttiming, Halbzelle.
11. Offene Kodierungen: die Konstanten von NEUTRAL, die 2 unbestätigten
    STRETCH-Codes, die Winkelfelder im Modus Gyro Servo und ob das
    Hersteller-Tool die Bereiche erzwingt, die es beschriftet.
12. Ein hier geschriebenes Servo wird vom Hersteller-Tool mit dessen Karte
    gelesen.
13. Der Typ des Servos, von dem das Layout stammt.
14. Ob 20k_sel die Servogenerationen so trennt, wie die Typregel annimmt:
    1 Servo ist aufgezeichnet, und kein V8-Servo ist gelesen worden.
15. Ein Speichern, ein Auflisten und ein Laden auf einer Karte: die Dauer
    jedes Vorgangs gegen die Grenze von 5 s und eine Karte, die während
    eines Speicherns entfernt wird.

## Tests

`test_kst_prog` fährt das Modell und `test_kst_panel` das Overlay, beide
gegen den Port des Koprozessors und das Servomodell durch den Dispatcher
des Links: jedes Limit an seiner Kante, das Halten von 2 s auf beiden
Seiten der Schwelle, jede Ablehnung und jedes Ergebnis mit seiner Zeile in
beiden Sprachen, ein Schreiben, das ein Link-Ausfall, ein Neustart,
**ABBRECHEN** und **STOP** abschneiden, eine Karte, die versagt, und die
Timeouts über den Überlauf der Uhr bei 2^32 ms. Die Servodateien laufen
gegen ein Kartenmodell: jedes Ergebnis eines Namens, jede Art von Datei,
beide Servotypen und ihre Abweichung in beiden Richtungen, jede
Ablehnung, die Grenze von 5 s, ein erstes Schreiben mit einer Karte, die
voll ist oder versagt, und die Schreiben ohne Karte: das erste Schreiben,
ein Zurückspielen und ein Lösen des Pairings, ein früh losgelassenes
Halten, eine zwischen zwei Schreiben eingehängte Karte und eine unter der
Schreibseite entnommene Karte.
