# Servo-Programmierer für KST- und Chaservo-Servos

<sub>[English](ServoProgrammer.md) · **Deutsch**</sub>

Der Programmierer liest die 32 Register eines KST- oder Chaservo-Servos über
dessen Signalleitung, zeigt sie als Einstellungen und schreibt geänderte
Einstellungen zurück. Die Servos der zwei Marken teilen ein Protokoll und
einen Registersatz. Er öffnet sich mit seiner Zeile `KST- und
Chaservo-Servos` auf **PROGRAMMIEREN** des
[SERVO-Bildschirms](Screens-de.md#servo), eine Ebene tiefer, und bedeckt die
linke Spalte samt den Reitern der Leiste. **SCHLIESSEN** oder das Home-Tag
führt zurück zur Liste von PROGRAMMIEREN. Das Öffnen beendet einen laufenden
automatischen Test als `vom Bediener gestoppt`. Die Zeile ist mit
`IOMCU OHNE SERVO PWM` gedimmt und öffnet nichts, solange der IOmcu, die
RP2350-Platine, kein Servo-PWM meldet, auch wenn kein IOmcu antwortet.

KST und Chaservo sind Namen oder Marken ihrer jeweiligen Inhaber. rcbench
ist unabhängig: es ist mit ihnen nicht verbunden und wird von ihnen weder
unterstützt noch gesponsert. Die Namen sagen, mit welchen Servos der
Programmierer arbeitet.
[Rechtliche Hinweise und Haftungsausschluss](../README-de.md#rechtliche-hinweise-und-haftungsausschluss)
enthält den Hinweis des Projekts.

Registerlayout und Timing stammen von 1 Tool Card #2, der
Programmierkarte von KST, und 1 Chaservo-Servo unbekannten Typs. Die Wirkung eines Registers auf die
Bewegung des Servos ist nicht gemessen: Namen, Einheiten und Limits sind
die, die das Hersteller-Tool anzeigt und annimmt.
[Was offen ist](#offene-messungen) nennt, was ein Prüfstand messen muss,
bevor man sich auf ein Schreiben verlässt.

![Der Programmierer, der Tester hält den Port](img/de/servo-kst.png)

## Randbedingungen

- Der IOmcu spricht Protokoll 4.13 oder neuer. Mit einem älteren sagt
  das Overlay `Nicht verfügbar: Der IOmcu hat keinen Programmierport.` und
  `IOmcu auf Version 4.13 oder neuer aktualisieren.`, und jedes
  Bedienelement außer **SCHLIESSEN** ist aus.
- **VERLASSEN** mit seinem Warten braucht Protokoll 4.15 auf dem
  IOmcu. Ein älterer wird
  [benannt und ohne es behandelt](#ein-iomcu-vor-link-version-415).

  ![Ein IOmcu ohne den Port](img/de/servo-kst-nopage.png)

- Das Servo hängt am Programmierport des IOmcu, Port 1: GP13, Pad 17
  des Bring-up-Moduls, solange kein anderer Pin dafür gespeichert ist. Der
  Port ist kein Ausgangskanal und kein Slot von OUTPUTS.
- **ZIEL** des SERVO-Bildschirms steht auf **PROG-PORT**
  ([das Ziel](#das-ziel)). Mit den Standardausgängen als Ziel ist der
  Programmierer gesperrt.
- Ein Servo zur Zeit.
- Der Port ist in PWM (Pulsweitenmodulation) oder im Programmieren, nie in
  beidem. Solange der Port im Programmieren ist, sendet der Tester keinen
  Puls darauf, und seine Zeile **SOLL-PULSBREITE** zeigt `---`.
- Programmieren und ein Prüfstand in ARMED schließen sich aus.
  **VERBINDEN** wird abgelehnt mit
  `Zuerst DISARM tippen. Bei ARMED kann der Port nicht übernommen werden.`, wenn ARMED für
  irgendeine Rolle gilt, angefragt ist oder ein Finger auf **ARM** liegt. Solange der Port
  gehalten wird, von **VERBINDEN** bis er PWM meldet, setzt kein Bildschirm
  den Prüfstand auf ARMED: **ARM** auf SERVO und auf MOTOR & ESC ist
  gesperrt gezeichnet und nimmt keinen Druck an, und ein Lauf des
  ESC-Stick-Programmierers startet nicht und meldet
  `Servoprogrammierung belegt einen Port. Zuerst auf SERVO auf VERLASSEN tippen.` Ein ARM, das den
  Prüfstand auf anderem Weg erreicht, wird mit dem Hinweis
  `ARMED gesperrt: Die Servoprogrammierung belegt einen Port.` abgelehnt. **DISARM**
  und **STOP** wirken in jedem Zustand. In ARMED werden das Lesen, das
  Prüfen, das Schreiben, das Zurückspielen und das Lösen der Kopplung mit
  derselben Zeile wie **VERBINDEN** abgelehnt; **VERLASSEN** wird
  angenommen.
- Antwortet ein PD mini, stellt der Port die Versorgung des Servos bei
  **VERBINDEN** ([die Versorgung](#die-versorgung)). Ohne einen stellt der
  Prüfstand die Versorgung nicht, und der Programmierer nimmt sie, wie sie
  ist.
- Der Programmierer nimmt an, dass ein Servo kein Aus- und Einschalten
  braucht: weder, damit ein geschriebener Wert wirkt, noch, um nach dem
  Programmieren PWM zu folgen. Keines von beidem ist gemessen
  ([Einschränkungen](#einschränkungen)).
- Ob eine gegebene Hardware die Antwort des Servos zum Pin zurückführt,
  weiß der IOmcu nicht. Ein Servo an einem Port, der es nicht tut,
  beendet **VERBINDEN** mit
  `Kein Servo antwortet. Stecker, Versorgung und Port prüfen.`

## Das Ziel

Der Tester des SERVO-Bildschirms treibt die Standard-Servoausgänge oder den
Programmierport. Zwei Bedienelemente stellen es:

| Bedienelement | Werte | Wirkung |
| --- | --- | --- |
| ZIEL, die Zeile auf der rechten Karte | **AUSGÄNGE**, **PROG-PORT** | was der Sweep, **ZENTRIEREN**, **FREIGEBEN**, das Halten einer Stellung und der automatische Test treiben; ein Tippen öffnet die Auswahl |
| PORT-PWM, auf **OPTIONEN** > PRÜFLING | **EIN**, **AUS** | ob der Programmierport den Puls des Testers ausgibt, solange er das Ziel ist |

Beide werden mit den SERVO-Einstellungen gehalten. Die Zeile ZIEL nennt das
aktive Ziel mit Protokoll und Pins: `PROG-PORT GP13` in der Warnfarbe oder
`SERVO PWM GP4` für die als SERVO PWM gebundenen Pins. **ZIEL** wechselt nur bei einem Prüfstand in SAFE, ohne laufenden Sweep
und ohne laufenden Test, und wenn der Port nicht im Programmieren ist; das
Panel gibt eine gehaltene Stellung frei, bevor es den IOmcu um das
andere Ziel bittet. **PORT-PWM** geht nach derselben Regel ein. Es geht
aus, wenn der Port nicht im Programmieren ist und weder Sweep noch Test
läuft, in ARMED wie in SAFE: **STOP** und
**DISARM** beenden einen Sweep oder einen Test. Mit **AUSGÄNGE** als Ziel
ist **PORT-PWM** gesperrt.

Solange der Programmierer den Port hat, ändert sich keine der beiden
Zeilen: von **VERBINDEN** bis der Port nach **VERLASSEN** wieder in PWM
ist, während jedes Vorgangs und solange der IOmcu den Port außerhalb
von PWM meldet. Beide Zeilen sind gesperrt gezeichnet, ein Zurücksetzen der
Einstellungen lässt die zwei Werte stehen, das Panel schreibt nichts in die
Seite des Ports im IOmcu, und der Tester sendet dem Port kein
Kommando und keine Freigabe. Eine dem Port geschuldete Freigabe geht
hinaus, sobald der Port wieder in PWM ist.

Ein Port ohne Pin kann nicht das Ziel sein: ein Slot von OUTPUTS, der GP13
hält, lässt dem Port keinen. Der Tester behält dann die Standardausgänge,
was immer die Zeile sagt.

Mit den Standardausgängen als Ziel zeigt der Programmierer
`Zum Programmieren muss das Servo am Programmierport angeschlossen sein.` und
`Rechts auf der Karte das ZIEL auf PROG-PORT stellen.`, und jedes
Bedienelement außer **SCHLIESSEN** ist aus.

![Die Standardausgänge sind das Ziel](img/de/servo-kst-target.png)

## Der Port

| Zustand | Zeile auf dem Reiter SERVO | Der Port führt |
| --- | --- | --- |
| PWM, ein PD mini antwortet | `Tester nutzt Port 1. VERBINDEN stellt 5.0 V und 1000 mA ein.` | die Pulse des Testers |
| PWM, kein PD mini | `Tester nutzt Port 1. Kein PD mini: Versorgung bleibt unverändert.` | die Pulse des Testers |
| Verbinden | `Verbindung an Port 1 wird aufgebaut. PWM ist aus.` | 50 ms keinen Puls, dann 100 ms Low, dann die Einstiegssequenz |
| Programmieren | `Port 1 ist im Programmiermodus. PWM ist aus.` | nur Frames |
| Programmieren, die Versorgung gelesen | `Port 1 im Programmiermodus. PWM aus. 5.00 V, 37 mA.` | nur Frames |
| Arbeiten | `Vorgang läuft: 12 Frames.` | die Frames des laufenden Vorgangs |
| Arbeiten, ein Schreiben von Werten | `3 Register werden geschrieben. Gesendete Frames: 94.` oder `1 Register wird geschrieben. Gesendete Frames: 31.` | die Frames des Schreibens; die Zahl nennt die Register, die das Schreiben setzt |
| Arbeiten, ein Zurückspielen des Backups der Sitzung oder ein Lösen des Pairings | `Schreibvorgang läuft. Gesendete Frames: 12.` | die Frames des Schreibens; der IOmcu plant es allein, und das Panel hat keine Zahl |
| Arbeiten, der IOmcu plant | `Schreibreihenfolge wird vorbereitet. Noch kein Frame gesendet.` | nichts: ein IOmcu der Link-Version 4.14 plant ein Schreiben oder ein Zurückspielen vor seinem ersten Frame |
| Verlassen | `Programmiermodus wird verlassen. PWM ist noch aus.` | die Leitung auf Low für **WARTEZEIT VOR PWM**; kein Puls, bis der Port PWM meldet |

**VERBINDEN** nimmt den Port aus PWM, stellt die Versorgung, wo ein PD mini
antwortet, fährt die Einstiegssequenz und liest alle 32 Register. Die Taste
heißt **ABBRECHEN**, solange ein Vorgang läuft oder ein Verlassen wartet,
und **VERLASSEN** im Programmiermodus.

![Ein laufender Vorgang](img/de/servo-kst-working.png)

Solange ein Schreiben, ein Zurückspielen oder ein Lösen des Pairings läuft,
zeigt der Reiter SERVO `Schreibvorgang läuft. Nicht trennen.` in der Warnfarbe, und
**ZURÜCKSPIELEN** nimmt keinen Tipp an. Der Vermerk dieses Schreibens liegt
auf der Karte oder im Speicher und wird nicht als
[unterbrochenes Schreiben](#ein-unterbrochenes-schreiben) gezeigt, bevor
das Schreiben ohne Ergebnis endet.

![Ein laufendes Schreiben](img/de/servo-kst-writing.png)

Solange der Port im Programmieren ist, bleibt das Overlay offen, die
Reiter der Leiste darunter nehmen keinen Druck, und die Bedienelemente des Testers senden nichts an
den Port. **SCHLIESSEN** und das Home-Tag öffnen PROGRAMMIERMODUS
VERLASSEN; über einem offenen Tastenfeld, einer Liste, einer Warnung oder
der Buchstabentastatur schließt das Home-Tag zuerst dieses Feld. **STOP**
auf der rechten Karte arbeitet. Das Verlassen des SERVO-Bildschirms lässt
den Port, wie er ist; beim nächsten Aufruf ist das Overlay auf
PROGRAMMIEREN wieder offen.

| **VERBINDEN** endet mit | Bedeutung |
| --- | --- |
| `Dem Programmierport ist kein Pin zugeordnet. Nichts gesendet.` | ein Slot von OUTPUTS hält den Pin des Ports |
| `Diesen Programmierport gibt es auf dem IOmcu nicht.` | der IOmcu hat weniger Ports als den angefragten |
| `Programmierspannung oder -strom überschreitet die SUPPLY-Grenzen.` und `Wert unter WEITERES senken oder Grenze erhöhen. Nichts übernommen.` | PROGRAMMIERSPANNUNG liegt über der Spannungsgrenze des SUPPLY-Bildschirms oder STROMBEGRENZUNG über seiner Stromgrenze; nichts wird gesendet |
| `Zuerst DISARM tippen. Bei ARMED kann der Port nicht übernommen werden.` | ARMED gilt oder ist angefragt; der IOmcu lehnt dasselbe von sich aus mit `Port belegt. Erneut versuchen.` ab |
| `Das Netzteil unterstützt diese Spannung oder Strombegrenzung nicht.` | die Spannung liegt über 8,4 V, die Stromgrenze außerhalb 50 bis 3000 mA, oder die Spannung, mit der die Session läuft, liegt weniger als 0,7 V unter der Eingangsspannung des PD mini; die Versorgung wird nicht geändert |
| `Versorgung unter 4.8 V. Programmieren benötigt mindestens 4.8 V.` | die Programmierspannung oder die Spannung eines Ausgangs, der ein ist, liegt unter 4,8 V; die Versorgung wird nicht geändert. Bei einem Ausgang, der ein ist und unter 4,8 V gesetzt ist, sendet das Panel nichts an den IOmcu |
| `Versorgung hat nicht innerhalb von 3000 ms geschaltet. Versorgung prüfen.` | der PD mini las nicht wie angefragt; der Port ist wieder in PWM, und ein Ausgang, den dieses **VERBINDEN** eingeschaltet hat, ist wieder aus |
| `IOmcu hat die Verbindung noch nicht freigegeben. Erneut versuchen.` | der IOmcu vertraut dem Heartbeat nicht, oder der Link war still |

![Ein Port ohne Pin](img/de/servo-kst-nopin.png)

### Der Signalpegel

Der Signalpin des Servos ist ein GPIO (General-Purpose Input/Output) eines
Mikrocontrollers. Der High-Pegel seiner Antwort ist höchstens 5 V und
typisch 3,3 V. Der Pegel ist nicht gemessen. GP13 ist für 5,5 V
spezifiziert, solange IOVDD (die Ein-/Ausgangsversorgung) des IOmcu
auf 3,3 V liegt (`hardware/docs/IOBoard.md`, Pin ratings); der Port hat
deshalb keinen Pegelwandler und keine Klemmung.

Ohne Versorgung des IOmcu ist GP13 für 3,63 V spezifiziert. Ein
Servo, das die Leitung mit 5 V treibt, während der IOmcu aus ist,
überschreitet diesen Wert. Was der Pin des Servos im Leerlauf bei
ausgeschaltetem Prüfstand tut, ist nicht gemessen.

Den IOmcu vor der Versorgung des Servos einschalten, oder das Servo
nicht aus einer externen Versorgung speisen, während der Prüfstand aus
ist.

## Die Versorgung

Solange der Tester den Port hat, öffnet **WEITERES**, was **VERBINDEN**
stellt.

![Was VERBINDEN stellt](img/de/servo-kst-options.png)

| Eintrag | Bereich | Vorgabe | Wirkung |
| --- | --- | --- | --- |
| PROGRAMMIERSPANNUNG | 4,8 bis 6,0 V in 0,1 V; bis 8,4 V mit HV-SERVO ein | 5,0 V | die Versorgungsspannung des Servos im Programmiermodus |
| STROMBEGRENZUNG | 50 bis 3000 mA | 1000 mA | die Stromgrenze der Versorgung während des Programmierens |
| WARTEZEIT VOR PWM | 0 bis 10 000 ms | 100 ms | die Zeit, die der Port seine Leitung zwischen **VERLASSEN** und dem ersten Puls auf Low hält |

Jeder Eintrag öffnet die Zahlentastatur mit seinem Bereich. Ein Wert
außerhalb des Bereichs wird nicht übernommen. Alle drei werden mit den
SERVO-Einstellungen gehalten.

**VERBINDEN** schaltet einen Ausgang, der aus ist, mit den zwei Werten ein,
und die Sitzung beginnt 1000 ms, nachdem der PD mini ein liest. Bei einem
Ausgang, der ein ist, läuft die Sitzung mit der niedrigeren der zwei
Spannungen und der niedrigeren der zwei Stromgrenzen: eine Spannung über
der Programmierspannung wird auf sie gesenkt, eine Stromgrenze über
STROMBEGRENZUNG auf diese, und keine wird angehoben. Das Verlassen des
Programmierens stellt einen Wert, den **VERBINDEN** gesenkt hat, nicht
wieder her.

Der Port programmiert bei 4,8 V oder mehr. Die Versorgungsspannung, die
ein Servo zum Speichern eines Registers braucht, ist nicht gemessen. An einem Ausgang, der unter 4,8 V ein
ist, wird nicht programmiert, und er bleibt, wie er ist; der Bediener hebt
ihn zuerst an. Ein PD mini, dessen Eingangsspannung unter 5,5 V liegt, gibt keine
4,8 V, und an ihm beginnt keine Sitzung.

Solange der Port nicht in PWM ist, gehören die Sollwerte der Versorgung dem
Port. Der IOmcu lehnt jede Änderung der Spannung und der Stromgrenze
und jedes Einschalten ab; ein Ausschalten nimmt er an. Der
SUPPLY-Bildschirm und die Zeile SOLL der rechten Karte zeigen dasselbe: die
Sollwerte, die Schieber, die Schritte und **AUSGANG EIN** sind gedimmt und
nehmen keinen Druck, **AUSGANG AUS** und **SPITZENWERTE ZURÜCKSETZEN** arbeiten,
und die Karte MODUS des SUPPLY-Bildschirms zeigt `Servoprogrammierung.`. Die
Grenzen in den Einstellungen des SUPPLY-Bildschirms bleiben editierbar;
eine Grenze, die unter einen Sollwert gesenkt wird, erreicht den Sollwert,
wenn der Port wieder in PWM ist.

Ein Ausschalten, das eintrifft, während ein Frame auf der Leitung ist, von
**AUSGANG AUS**, **STOP** oder einem verlorenen Heartbeat, liest sofort aus
und erreicht den PD mini, wenn der Port den Frame abgebrochen hat,
spätestens 12 ms später. Der eigene Schutz des PD mini wird nicht
verzögert. Wie lange der Ausgang des PD mini nach seinem Aus-Befehl zum
Abfallen braucht, ist nicht gemessen.

![Die Tastatur für die Spannung](img/de/servo-kst-voltage.png)
![Die Tastatur für das Warten](img/de/servo-kst-wait.png)

Eine Spannung über 6,0 V braucht **HV-SERVO** in den Optionen der Karte
AUTOMATISCHER TEST auf TEST; ohne es endet die Tastatur bei 6,0 V. Mit ihm öffnet ein Wert
über 6,0 V die Warnseite der Hochspannungs-Einstellung des
SERVO-Bildschirms, und **HALTEN ZUM ÜBERNEHMEN** übernimmt ihn nach 2 s.
Das Ausschalten von **HV-SERVO** setzt eine Programmierspannung über 6,0 V
auf 5,0 V zurück.

Eine Spannung über 6,0 V wird nicht gespeichert. Die SERVO-Einstellungen
halten für sie 6,0 V, und ein gespeicherter Wert über 6,0 V, von der
Einstellungsseite oder aus einer Einstellungsdatei, wird als 6,0 V geladen
und so gespeichert, mit **HV-SERVO** ein oder aus. Der Programmierer liest
die Einstellungen beim Öffnen und bei jedem Betreten des SERVO-Bildschirms,
solange kein Port gehalten wird, und wenn das Panel unter einem gehaltenen
Port startet: jedes davon beginnt bei 6,0 V oder darunter, und eine Spannung
darüber braucht erneut die Warnseite und ihr Halten von 2 s. Eine Sitzung,
die ihren Port hält, behält die Spannung, mit der sie begonnen hat.

![Eine Programmierspannung über 6,0 V](img/de/servo-kst-hv.png)

Antwortet ein PD mini, nennt die Zustandszeile vor **VERBINDEN** die Werte
und im Programmiermodus die gemessene Ausgangsspannung und den gemessenen
Strom. Der SUPPLY-Bildschirm und die Netzteil-Zeilen der rechten Karte
folgen dem, was der Port gestellt hat.

![Ein PD mini antwortet](img/de/servo-kst-supply.png)
![Die Versorgung im Programmiermodus](img/de/servo-kst-supplied.png)

Ohne PD mini werden die Spannung und die Stromgrenze gespeichert und nicht
angewendet. **WARTEZEIT VOR PWM** wendet ein IOmcu ab Link-Version 4.15
beim [Verlassen](#verlassen) an, mit und ohne PD mini.

## Reiter SERVO

![Ein gelesenes Servo](img/de/servo-kst-read.png)

| Zeile | Taste | Wirkung |
| --- | --- | --- |
| PORT | **VERBINDEN**, **ABBRECHEN**, **VERLASSEN** | nimmt den Port, beendet den laufenden Vorgang, [gibt den Port zurück](#verlassen) |
| SERVO, dann SERVO V6 oder SERVO V8 | **ALLES AUSLESEN** | liest die 32 Register, jedes, bis 3 von höchstens 5 Lesungen übereinstimmen; die Zeile nennt danach [den Servotyp](#der-servotyp) |
| DATEIEN | **WÄHLEN** | [speichert die Werte des Servos auf die Karte und lädt eine Datei](#servodateien) |
| ZURÜCKSPIELEN | **WÄHLEN** | [schreibt ein Backup zurück](#zurückspielen) |
| WEITERES | **WÄHLEN** | in PWM [was VERBINDEN stellt](#die-versorgung); im Programmiermodus [PRÜFEN, ÄNDERUNGEN VERWERFEN, PAIRING LÖSEN](#weiteres) |
| SCHREIBEN INS SERVO FREIGEBEN | **AUS**, **EIN** | [der Schreibschalter](#der-schreibschalter) |
| ÄNDERUNGEN | **n WERTE SCHREIBEN**, **1 WERT SCHREIBEN** | [schreibt die geänderten Werte](#schreiben) |

In PWM mit einem antwortenden PD mini lautet die Zeile PORT
`PORT  5.0 V  1000 mA`: die Spannung und die Stromgrenze, die **VERBINDEN**
stellt. Eine Zeile, deren Taste abgedunkelt ist, nimmt keinen Tipp an:
**DATEIEN** außerhalb des Programmiermodus und solange das Servo unklar
ist, **ZURÜCKSPIELEN**, solange ein Vorgang läuft, der Schreibschalter, wo
[er abgelehnt wird](#der-schreibschalter).

Die Zahlen auf dem Bildschirm haben in beiden Sprachen einen Dezimalpunkt:
`5.0 V`. Diese Seite schreibt Zahlen im Text mit Komma.

Unter den Zeilen stehen der Zustand des Ports, der Servotyp, der Name der
Backup-Datei der Sitzung und die letzte Meldung. Eine Meldung in der
Warnfarbe ist eine Ablehnung oder ein Ergebnis, das einen zweiten Blick
braucht; eine in der Gefahrenfarbe sagt, dass der Inhalt des Servos unklar
ist.

### Ein Servo mit unbekanntem Layout

Der IOmcu vergleicht das Abbild mit dem Layout des bekannten Servos,
seinem Fingerprint. Ein Abbild, das nicht passt, wird gezeigt und nie
geschrieben: die Zeilen zum Ändern und der Schreibschalter sind aus und
nehmen keinen Tipp an, der Reiter behält `Nur Auslesen möglich. Dieser Servotyp ist unbekannt.`, und **PAIRING LÖSEN** wird abgelehnt. **ALLES AUSLESEN**, das Speichern in eine Datei und
**PRÜFEN** arbeiten. Eine Datei wird nicht geladen.

![Ein Servo, das nicht zum Fingerprint passt](img/de/servo-kst-readonly.png)

## Ändern

Ein Wert wird in einem Entwurf auf dem Panel geändert. Vor
[**n WERTE SCHREIBEN**](#schreiben) erreicht nichts das Servo. Ein Wert,
der von dem des Servos abweicht, ist doppelt markiert: durch `*` davor und
durch die Akzentfüllung.

![Geänderte Werte auf ABSTIMMEN](img/de/servo-kst-tune.png)

Ein Zahlenwert öffnet die Tastatur. Sein Bereich ist der, den die harten
Limits im Entwurf, wie er steht, annehmen; ein Wert außerhalb lässt sich
nicht eingeben. STRETCH öffnet eine Liste der 6 bestätigten Faktoren. Ein
EIN/AUS-Wert wechselt beim Tipp. DEAD BAND und NEUTRAL haben Minus- und
Plustasten: ein Rohzähler je Tipp, gehalten nach 0,45 s 8 Zähler je
Sekunde und nach weiteren 1,8 s 30 je Sekunde.

![Die Tastatur mit dem Bereich, den die Limits annehmen](img/de/servo-kst-keypad.png)
![Die Stretch-Liste](img/de/servo-kst-stretch.png)

### ABSTIMMEN

| Zeile | Registerfeld | Hartes Limit |
| --- | --- | --- |
| DEAD BAND | Dead Band, µs | der Bereich des Felds |
| MAX. DUTY | Max. Duty, % | 10 bis 98 % |
| BOOST | Boost, µs | höchstens 3000 µs |
| STRETCH | Stretch, Faktor | x1 bis x32; die 2 übrigen Codes werden nicht angeboten |
| SOFT START | Soft_Start, EIN oder AUS | keines |
| TEMPOGRENZE | spd_sel, EIN oder AUS | EIN braucht TEMPO ab 5 % |
| TEMPO | SPD, % | höchstens 100 %; mindestens 5 %, solange TEMPOGRENZE EIN ist |

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
3 Werte und um etwa 1 µs unsicher. Der Reiter sagt zu den beiden Winkeln
`LEFT ANGLE, RIGHT ANGLE: Werte aus dem Servo. Die Einheit ist unbekannt.` Die Bildschirme
schreiben `us` für µs: ihre Schrift hat kein Mikro-Zeichen.

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
ÄNDERUNG PRÜFEN mit der Zeile des Felds und einer Zeile je Limit. Die Zeile
des Felds zählt vom Wert des Servos, nicht von einer Änderung davor: `MAX.
DUTY: 96 -> 97 %` ist der Wert, den das Servo hält, der neue Wert und die
Einheit. **HALTEN ZUM ÜBERNEHMEN** übernimmt ihn nach 2 s Halten;
**ABBRECHEN** oder ein Finger, der von der Taste rutscht, lässt den
Entwurf, wie er war.

Ein Limit, das für einen Wert bestätigt ist, ist für diesen Wert bestätigt.
Eine spätere Änderung des Felds öffnet die Seite erneut, wenn sie den Wert
weiter vom Wert des Servos entfernt als den bestätigten oder auf dessen
andere Seite legt: 97 % bestätigt, 98 % fragt erneut und zeigt `MAX. DUTY:
96 -> 98 %`, und 97 % danach nicht. Ein Schreiben, ein Lesen und
**ÄNDERUNGEN VERWERFEN** beenden jede Bestätigung.

![Ein weiches Limit](img/de/servo-kst-soft.png)

| Bedingung | Warnung |
| --- | --- |
| MAX. DUTY über dem des Servos | `Höheres Duty erhöht den Blockierstrom und die Erwärmung.` |
| MAX. DUTY unter 50 % | `Niedriges Duty schwächt das Servo. Es hält das Ruder möglicherweise nicht.` |
| BOOST über dem des Servos | `Mehr Boost kann das Servo brummen und heiß werden lassen.` |
| STRETCH über dem des Servos | `Mehr Stretch kann das Servo zum Schwingen bringen.` |
| SOFT START ausgeschaltet | `Ohne Soft Start bewegt sich das Servo beim Einschalten ruckartig.` |
| TEMPOGRENZE eingeschaltet | `Tempogrenze aktiv. Das Servo bewegt sich mit dem eingestellten Tempo.` |
| TEMPO unter 30 % bei TEMPOGRENZE EIN | `Ein zu langsames Servo kann zum Kontrollverlust über das Modell führen.` |
| DEAD BAND unter dem des Servos | `Ein schmales Dead Band kann Pendeln und Erwärmung verursachen.` |
| DEAD BAND unter 1,0 µs | `Sehr schmales Dead Band. Pendeln und Erwärmung sind wahrscheinlich.` |
| BEWEGEN BEI SIGNALVERLUST eingeschaltet | `Ohne Signal fährt das Servo auf die Failsafe-Position.` |
| POSITION mehr als 300 µs von NEUTRAL, BEWEGEN BEI SIGNALVERLUST EIN | `Die Failsafe-Position liegt weit von der Mitte entfernt.` |
| NACH unter 1,0 s | `Kurze Aussetzer lösen die Failsafe-Bewegung aus.` |
| ein Endpunkt weniger als 300 µs von NEUTRAL | `Endpunkt nah an Neutral. Auf dieser Seite bleibt wenig Weg.` |
| ein Endpunkt mehr als 100 µs von dem des Servos | `Endpunkt geändert. Dadurch ändert sich auch der Servoweg je Knüppelbewegung.` |
| NEUTRAL mehr als 20 µs von dem des Servos | `Die Mitte verschiebt sich. Eine Seite kommt näher an den mechanischen Anschlag.` |
| REVERSION geändert | `Drehrichtung umgekehrt. Prüfe vor dem Flug alle Ruder.` |
| ein Winkel über dem des Servos | `Mehr Weg kann das Servo gegen den Anschlag fahren und durchbrennen lassen.` |
| ein Winkel unter 50 | `Kleiner Winkel. Voller Knüppelweg ergibt nur wenig Servoweg.` |
| DEAD BAND unter 1,0 µs bei STRETCH über x1 | `Schmales Dead Band und mehr Gain können Schwingungen verursachen.` |
| MAX. DUTY über 90 % bei einem Winkel über dem des Servos | `Hohes Duty und mehr Weg: Blockieren kann das Servo zerstören.` |

"Der des Servos" ist in dieser Tabelle der Wert des Backups der Sitzung,
das Servo, wie die Sitzung es zuerst gelesen hat. Ein Schreiben der Sitzung
verschiebt ihn nicht: ein in zwei Schreiben erhöhter Wert wird mit dem Wert
verglichen, mit dem das Servo kam. Wo das Panel dieses Backup nicht hält
oder das Backup zu einem anderen Servo gehört, ist es der zuletzt gelesene
Wert. Die Warnungen [eines Zurückspielens](#zurückspielen) vergleichen mit
dem Servo, wie es zuletzt gelesen wurde.

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

**SCHREIBEN INS SERVO FREIGEBEN** ist in der Vorgabe aus. Ein Tipp auf
**AUS** öffnet eine Seite, die die Risiken nennt; **HALTEN ZUM FREIGEBEN**
schaltet ihn nach 2 s ein. Ein Tipp auf **EIN** schaltet ihn sofort aus.

![Das Schreiben freigeben](img/de/servo-kst-enable.png)

Der Schalter geht aus:

- bei jedem Aufruf des SERVO-Bildschirms,
- wenn der Link zum IOmcu wegfällt oder der IOmcu neu startet,
- bei **STOP** und bei jedem Stopp, den der Prüfstand hält,
- mit jedem **VERBINDEN** und jedem **VERLASSEN**,
- wenn ein Lesen ein anderes Abbild findet als das gehaltene,
- wenn ein Lesen nicht zum Fingerprint passt,
- wenn ein Schreiben, ein Zurückspielen, ein Lesen oder **PRÜFEN** mit
  einem unklaren Servo endet: das Lesen danach schaltet ihn nicht wieder
  ein,
- wenn ein Lesen ein Servo findet, das zum Vermerk eines unterbrochenen
  Schreibens passt.

Bei ausgeschaltetem Schalter sendet das Panel kein Schreiben, kein
Zurückspielen und kein Lösen des Pairings, und der IOmcu lehnt eines
ab, das ohne den Schlüssel des Schalters ankommt. Lesen, **PRÜFEN**, das
Speichern in eine Datei und das Laden einer Datei arbeiten bei
ausgeschaltetem Schalter.

Der Schalter nimmt seinen Tipp mit einem gelesenen Servo an. Wo er ihn
nicht annimmt, ist **AUS** abgedunkelt: ohne gelesenes Servo, mit einem
Servo, das nicht zum Fingerprint passt, solange ein Vorgang läuft und
solange das Panel fragt, ob das Servo das eines unterbrochenen Schreibens
ist. Eine Sitzung,
die der IOmcu nach einem unterbrochenen Schreiben gesperrt hat, nimmt
ihn auch ohne gelesenes Servo an, sobald das Panel das Backup der Sitzung
hält: das ist der Zustand eines Panels, das unter einer solchen Sitzung
startet. Der Schalter öffnet dann ein Schreiben, das Zurückspielen des
Backups der Sitzung. Ein Ändern, ein Schreiben von Werten, ein Lösen des
Pairings und das Laden einer Datei bleiben abgelehnt bis zu einem Lesen und
dem Ende der Sperre. **ALLES AUSLESEN** liest in diesem Zustand das Servo und
zeigt seine Werte; die Sperre bleibt, und kein Wert wird geändert.

## Schreiben

**n WERTE SCHREIBEN** zählt die Felder, in denen der Entwurf vom Servo
abweicht; bei einem solchen Feld heißt die Taste **1 WERT SCHREIBEN**. Sie
ist aus, wenn nichts geändert ist. Bei ausgeschaltetem Schalter öffnet ihr
Tipp [die Seite des Schalters](#der-schreibschalter).

![Drei geänderte Werte, der Schalter ein](img/de/servo-kst-changes.png)

Ein Tipp öffnet die Schreibseite. **HALTEN ZUM SCHREIBEN** startet das
Schreiben nach 2 s. Die Seite hat 12 Zeilen.

Zuerst stehen die geänderten Werte in der Reihenfolge der Reiter, je eine
Zeile: `DEAD BAND: 2.0 -> 1.6 us` ist der Wert des Servos, der neue Wert
und die Einheit. Ein Wert, der
[ein weiches Limit](#ein-wert-der-einen-zweiten-blick-braucht) verletzt,
steht in der Warnfarbe. Bei mehr Werten als Zeilen zählt die letzte dieser
Zeilen den Rest: `und 4 weitere`. Ein erstes Schreiben mit Karte zeigt 7
Werte oder 6 und die Zahl; eines ohne Karte 6 oder 5 und die Zahl.

`Halte das Ruderhorn frei. Löse das Gestänge.` und `Teste das Servo am Prüfstand, bevor du es ins Modell einbaust.` folgen beim ersten Schreiben nach einem
**VERBINDEN** und bei jedem Schreiben, das REVERSION, NEUTRAL, PULSE LOWER,
PULSE UPPER, LEFT ANGLE oder RIGHT ANGLE ändert. Ein Schreiben, dessen
Start die Karte, ein Stopp oder der Link verhindert hat, ist nicht dieses
erste Schreiben: die Zeilen bleiben für das nächste.

![Die Schreibseite](img/de/servo-kst-write.png)

1. Das erste Schreiben einer Sitzung speichert die Werte des Servos in
   [eine neue Datei auf der Karte](#das-backup-vor-dem-ersten-schreiben).
   Eine eingehängte Karte, die die Datei nicht annimmt, beendet das
   Schreiben. Ohne eingehängte Karte sagt die Schreibseite, dass keine
   Datei gespeichert wird, und das Schreiben läuft ohne eine. Solange der
   Vermerk eines unterbrochenen Schreibens gehalten wird, wird keine Datei
   gespeichert: das Servo kann halb geschrieben sein, und der Vermerk hält
   seine Werte, wie sie vorgefunden wurden.
2. Das Panel vermerkt das Abbild, das das Servo hält, das Abbild, das das
   Schreiben hinterlassen soll, und den Port: mit Karte in `KST/KST.TXT`,
   ohne im RAM (Arbeitsspeicher). Die Seite sagt, welches. Mit einer Karte,
   die binnen 3 s keine Datei annimmt, wird das Schreiben nicht gesendet:
   `Datei nicht auf Karte gespeichert. Keine Werte ins Servo geschrieben.`
3. Das Panel sendet das Zielabbild. Der IOmcu prüft es gegen dieselben
   Limits, plant die Registerschreibvorgänge und führt sie aus.
4. Jedes geschriebene Register wird zurückgelesen. Ein Schreibvorgang, der
   nicht übernommen wird, wird rückgängig gemacht.
5. `3 Werte geschrieben und geprüft.` oder `1 Wert geschrieben und geprüft.`
   beendet es und löscht den Vermerk.

Ein geschriebener und zurückgelesener Wert gilt als wirksam. Der
Programmierer verlangt nach einem Schreiben kein Aus- und Einschalten, und
sein Prüfen ist das Rücklesen derselben Sitzung. Ob ein Servo vor
seinem nächsten Einschalten nach einem geschriebenen Wert handelt, ist
nicht gemessen.

Nur Register, deren Wert sich ändert, werden geschrieben. Register 00 wird
nie geschrieben; Register 1D schreibt nur **PAIRING LÖSEN**.

![Ein Schreiben ist fertig](img/de/servo-kst-written.png)

| Meldung | Das Servo |
| --- | --- |
| `Für diese Werte gibt es keine sichere Schreibreihenfolge.` | hält, was es hielt; nichts wurde gesendet |
| `Zu viele Änderungen für einen gemeinsamen Plan. Nichts geschrieben.` `Teile die Änderungen auf. Schreibe weniger Werte auf einmal.` | hält, was es hielt; nichts wurde gesendet. Der Planer hört nach 216 000 Arbeitseinheiten ohne Ergebnis auf. Die Sitzung läuft weiter, und die offenen Werte bleiben |
| `Schreiben fehlgeschlagen. Keine Werte sind unvollständig geschrieben.` | hält, was es hielt |
| `Schreiben fehlgeschlagen. Der vorherige Wert wurde wiederhergestellt.` | hält, was es hielt |
| `Schreiben fehlgeschlagen. Zuvor geschriebene Werte bleiben. ALLES AUSLESEN tippen.` | hält die Register, die vor dem fehlgeschlagenen geschrieben wurden, und in den übrigen den alten Wert. Die zweite Zeile nennt das Register und die Zahl der erledigten Schreibvorgänge. Der Schreibschalter geht aus, und **ALLES AUSLESEN** kommt vor dem nächsten Schreiben |
| `Schreiben fehlgeschlagen. Rückgängig machen war nicht möglich.` | ist unklar: [zurückspielen](#ein-unterbrochenes-schreiben) |
| `Ein Register hat sich ohne Schreibzugriff geändert.` | ist unklar |
| `Das Servo enthält nicht die erwarteten Werte.` | ist unklar |

Eine Meldung über ein fehlgeschlagenes Schreiben nennt das Register und die
Zahl der erfolgten Schreibvorgänge: `Register 03. 1 Schreibvorgang abgeschlossen.`

## Backups

| Backup | Gehalten von | Lebt bis |
| --- | --- | --- |
| Der Sitzung | dem IOmcu: das erste nach **VERBINDEN** gelesene Abbild | **VERLASSEN**, ein Aus- und Einschalten des Servos, ein Neustart des IOmcu |
| Die Datei der Sitzung | der Karte: die 32 Register des Servos, wie sie vor dem ersten Schreiben der Sitzung stehen. Keine, solange keine Karte eingehängt ist | die Datei wird von der Karte entfernt |
| Vor einem Schreiben | dem Panel, in `KST/KST.TXT` oder im RAM | das Schreiben endet geprüft, oder der Vermerk wird verworfen |

`KST.TXT` ist der Vermerk eines laufenden Schreibens: Text von höchstens
400 Bytes im Ordner `KST` der Karte mit der Zeile `RCBENCH KST 2`, den
Zeilen `journal=`, `target=`, `port=` und `pin=` und `check=` mit der
CRC-16 (zyklische Redundanzprüfung) der Bytes davor. `pin=` hält den GPIO
(General-Purpose Input/Output), den der Port beim Schreiben trieb, in zwei
Hexadezimalziffern: `0D` für GP13, `FF`, wenn das Panel für den Port keinen
Pin kannte. Ein Vermerk der Version 1 (`RCBENCH KST 1`, keine Zeile
`pin=`) wird gelesen und nennt nur den Port. Ein Pin über GP47 wird ebenso
gelesen: der Vermerk bleibt, und seine Zeilen nennen den Port. Eine Datei, deren Prüfsumme
nicht stimmt, wird nicht benutzt:
`KST.TXT auf der Karte ist nicht lesbar. Die Datei wird nicht verwendet.` Der Name `KST`
gehört dem Vermerk: keine Servodatei bekommt ihn, und die Liste der Dateien
lässt ihn aus.

Das Panel schreibt den Text nach `KST.NEW`, und die Datei bekommt den Namen
`KST.TXT`, sobald die Karte sie vollständig hält. Ein Schreiben, das die
Karte oder die Versorgung beendet, hinterlässt `KST.TXT` wie zuvor oder
eine vollständige `KST.NEW`. Beim Einschalten wird eine `KST.NEW` mit
stimmender Prüfsumme benutzt und umbenannt; eine mit falscher Prüfsumme
wird entfernt und `KST.TXT` benutzt. Eine Datei, die die Karte nicht
annimmt, wird alle 5 s erneut geschrieben, bis die Karte sie annimmt, damit
ein auf dem Panel verworfener Vermerk nicht auf der Karte bleibt.

## Servodateien

Eine Servodatei ist eine Textdatei im Ordner `KST` der SD-Karte des Panels
mit den 32 Registern eines Servos, benannt `<name>.TXT`. Das Panel legt den
Ordner mit dem ersten Speichern an. **DATEIEN** öffnet die Liste. Die Zeile
ist im Programmiermodus aktiv, wenn ein Servo gelesen und nicht unklar ist;
sonst antwortet der Tipp mit `Lies zuerst das Servo aus. Tippe auf ALLES AUSLESEN.`
Speichern und Laden arbeiten bei ausgeschaltetem Schreibschalter: eine
Datei ändert den Entwurf auf dem Panel, nie das Servo.

![Die Dateien auf der Karte](img/de/servo-kst-files.png)

Der erste Eintrag speichert. Darunter stehen die Dateien nach Namen
sortiert, 5 je Seite, jede mit ihrem Servotyp; eine Datei ohne Typzeile
zeigt keinen. Die Dateien mit einem vorgeschlagenen Namen, `KST` und
3 Ziffern, stehen zusammen an der Stelle von `KST000`, die höchste Nummer
zuerst. Die Liste erreicht jede Servodatei der Karte. Bei mehr als
5 Dateien zählt der Titel sie, `DATEIEN 1 BIS 5 VON 7`, und
`Weitere Dateien anzeigen.` blättert, nach der letzten Seite zurück zur
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

`Werte dieses Servos in einer neuen Datei speichern.` öffnet die Texttastatur mit
dem nächsten freien Vorgabenamen, `KST001` bis `KST999`. Die Tastatur
nimmt bis zu 16 Zeichen: A bis Z, 0 bis 9, `-`, `_` und den Punkt. **OK**
speichert; die Kartenseite prüft den Namen und ersetzt nie eine Datei.
Namen werden ohne Rücksicht auf Groß- und Kleinschreibung verglichen. Eine
Datei mit einem längeren Namen, bis zu 32 Zeichen, wird gelistet und
geladen; eine Zeile, die sie nennt, zeigt ihre ersten 14 Zeichen und `..`.

![Der Name einer neuen Datei](img/de/servo-kst-name.png)

| Ergebnis | Gezeigt |
| --- | --- |
| Gespeichert | `Servowerte auf der Karte gespeichert: WING-R.` |
| Ein Punkt am Anfang oder am Ende, oder ein reservierter Name: `KST`, `CON`, `PRN`, `AUX`, `NUL`, `COM1` bis `COM9`, `LPT1` bis `LPT9`, allein oder vor einem Punkt | die Tastatur bleibt offen unter `UNGÜLTIGER NAME`; `Ungültiger Name: reserviert oder mit einem Punkt am Anfang oder Ende.` |
| Eine Datei hat den Namen | die Tastatur bleibt offen unter `NAME VERGEBEN. ANDEREN WÄHLEN.`; `Dieser Dateiname ist bereits vergeben. Keine Datei wurde ersetzt.` |
| Keine Karte | `Keine Karte. Nichts gespeichert oder ins Servo geschrieben.` |
| Karte voll, oder `KST001` bis `KST999` sind beim Backup vor einem Schreiben alle vergeben | `Karte voll. Nichts gespeichert oder ins Servo geschrieben.` |
| Die Karte ist schreibgeschützt, oder `KST` auf der Karte ist eine Datei und kein Ordner | `Speichern nicht möglich: Karte schreibgeschützt oder KST kein Ordner.` |
| Die Karte bricht das Schreiben der Datei ab oder antwortet nicht binnen 5 s | `Schreibfehler auf der Karte. Keine Werte ins Servo geschrieben.` |

![Ein vergebener Name](img/de/servo-kst-name-exists.png)
![Ein nicht erlaubter Name](img/de/servo-kst-name-bad.png)
![Eine gespeicherte Datei](img/de/servo-kst-name-saved.png)
![Keine Karte](img/de/servo-kst-card-none.png)
![Eine volle Karte](img/de/servo-kst-card-full.png)

Ein Speichern, das die Karte nach den 5 s beendet, ist die Datei, die es
angelegt hat. Läuft kein anderer Dateivorgang, wechselt die Zeile zu
`Servowerte auf der Karte gespeichert:` und dem Namen. Sie ist die
Backup-Datei der Sitzung, wenn das Servo die gespeicherten Werte noch hält
und kein Schreiben folgte.

Ein Speichern schreibt das Abbild wie gelesen, die gesperrten Felder
eingeschlossen, auch das eines Servos, das nicht zum Fingerprint passt.

### Das Backup vor dem ersten Schreiben

Das erste Schreiben, Zurückspielen oder Lösen des Pairings einer Sitzung
speichert das Abbild des Servos ohne Nachfrage in eine neue Datei unter dem
nächsten freien Vorgabenamen. Die Schreibseite sagt
`Die bisherigen Servowerte werden vor dem Schreiben in einer neuen Kartendatei gesichert.` Das Servo empfängt
nichts, bevor die Karte die Datei hält. Eine eingehängte Karte, die voll
ist, beim Schreiben einen Fehler meldet oder zwischen Anfrage und Datei
entnommen wird, beendet das Schreiben mit den Zeilen der Tabelle oben, und
nichts wird auf das Servo geschrieben. Platz auf der Karte schaffen oder
die Karte entnehmen lässt das Schreiben laufen. Der Reiter SERVO nennt die
Datei danach: `Backup-Datei dieser Sitzung: KST003.` Ein Speichern von Hand
vor dem ersten Schreiben der Sitzung zählt als diese Datei. Ein Speichern
von Hand nach einem Schreiben enthält die geschriebenen Werte und lässt die
Zeile unverändert. Ein Lesen, das andere Werte findet, lässt das nächste
Schreiben erneut speichern.

Ein unklares Servo ist die Ausnahme: sein Abbild ist keine Datei wert, und
das Zurückspielen des Backups der Sitzung läuft ohne eine.

#### Ohne Karte

Ohne eingehängte Karte sind das Schreiben, das Zurückspielen und das Lösen
des Pairings nicht gesperrt. Die Seite, deren Halten das Schreiben sendet,
trägt drei weitere Zeilen:

```
Keine Karte. Es wird keine Backup-Datei gespeichert.
Das Backup dieser Sitzung liegt nur im Speicher des Prüfstands. Es geht beim Sitzungsende oder bei Stromausfall am Prüfstand verloren.
```

Das eine Halten der Seite von 2 s bestätigt sie; ein zweites Halten wird
nicht verlangt. Ein vor 2 s losgelassenes Halten sendet nichts.

![Die Schreibseite ohne Karte](img/de/servo-kst-write-nocard.png)

Nach einem solchen Schreiben zeigt der Reiter SERVO
`Keine Karte, keine Backup-Datei. Zurückspielen nur innerhalb dieser Sitzung möglich.` anstelle des
Dateinamens. Die einzige Kopie des Servos, wie es vorgefunden wurde, ist
das Backup der Sitzung im IOmcu: **VERLASSEN**, ein Neustart des
IOmcu und ein Stromausfall des Prüfstands beenden es, und die Werte,
die das Servo vor dem Schreiben hielt, sind dann aus dem Prüfstand nicht
wiederherstellbar.

![Der Reiter SERVO nach einem Schreiben ohne Karte](img/de/servo-kst-nofile.png)

| Fall | Ergebnis |
| --- | --- |
| Eine Karte wird später in der Sitzung eingehängt | das nächste Schreiben, Zurückspielen oder Lösen des Pairings speichert zuerst die Backup-Datei. Die Datei hält das Backup der Sitzung, das vor dem ersten Schreiben gelesene Abbild. Die Zeile im Reiter SERVO wechselt zum Dateinamen |
| Der Link fällt weg und kommt wieder, oder das Panel startet, unter einer Sitzung, die der IOmcu hält | die Schreiben der Sitzung sind dem Panel nicht bekannt. Das nächste Schreiben, Zurückspielen oder Lösen des Pairings speichert zuerst die Backup-Datei, und die Datei hält das Backup der Sitzung, nicht das Servo, wie es zuletzt gelesen wurde. Ein Speichern über **DATEIEN** zählt nur als Backup-Datei, solange das Servo dem Backup der Sitzung gleicht |
| Das Servo kann geschriebene Werte halten, und das Panel hält das Backup der Sitzung nicht | das Schreiben geht ohne Datei, und seine Seite trägt die drei Zeilen oben: das Servo, wie es zuletzt gelesen wurde, wird nicht als Backup-Datei gespeichert |
| Der IOmcu startet neu | seine Sitzung und das Backup der Sitzung sind weg. **VERBINDEN** beginnt eine Sitzung, und ihr erstes Schreiben speichert das Servo, wie diese Sitzung es vorfindet |
| Ein Speichern über **DATEIEN** nach einem Schreiben ohne Karte | speichert das Servo, wie es steht; es zählt nicht als Backup-Datei |
| Die Karte wird entnommen, während eine Seite offen ist, die die Datei zusagt | das Halten sendet nichts, und die Seite kommt mit den drei Zeilen zurück; ihr eigenes Halten von 2 s sendet das Schreiben |
| Die Karte wird entnommen, nachdem die Backup-Datei gespeichert ist | spätere Schreiben der Sitzung werden gesendet; der Vermerk des Schreibens bleibt im RAM |
| **DATEIEN**, Speichern und Laden ohne Karte | abgelehnt: `Keine Karte. Nichts gespeichert oder ins Servo geschrieben.` |
| Eine Karte, die nach dem Start des Panels eingesteckt wird | das Panel versucht, eine Karte einzubinden, wenn der Programmierer öffnet und bei jedem Tippen auf **VERBINDEN**, **ALLES AUSLESEN**, **DATEIEN**, **ZURÜCKSPIELEN**, **WEITERES**, **SCHREIBEN INS SERVO FREIGEBEN** oder die Schreibzeile, solange keine eingebunden ist. Das Tippen, das die Karte findet, liest noch keine Karte; das nächste hat sie, und eine offene Schreibseite wechselt zur Seite mit der Datei |

### Laden

Ein Tipp auf eine Datei lädt sie. Ihre änderbaren Felder werden der
Entwurf, genau wie auf den Reitern eingegebene Änderungen: geänderte Werte
sind markiert, **n WERTE SCHREIBEN** zählt sie, und nichts erreicht das
Servo vor dem Schreibschalter, der Schreibseite und deren 2 s Halten.

![Eine geladene Datei](img/de/servo-kst-file-loaded.png)

| Datei | Gezeigt |
| --- | --- |
| Vom Prüfstand gespeichert, Prüfsumme intakt | `KST001 geladen. 3 Werte weichen von den Servowerten ab.` oder `KST001 geladen. 1 Wert weicht von den Servowerten ab.` |
| Bearbeitet, mit Typzeile und ohne Prüfsummenzeile | dasselbe und `Bearbeitete Datei: 27 von 27 Werten, wie manuelle Eingaben geprüft.` |
| Bearbeitet, ohne Typzeile | dasselbe und `Bearbeitete Datei ohne Servotyp: 27 von 27 Werten, wie manuelle Eingaben geprüft.` |
| Enthält die änderbaren Werte des Servos | `KST001 stimmt mit den Servowerten überein. Nichts zu schreiben.` |
| Vom anderen Servotyp | `Datei ist V8, Servo ist V6. Nicht geladen.` |
| Prüfsummenzeile vorhanden und falsch | `Datei beschädigt: Prüfsumme stimmt nicht. Nicht geladen.` `Ohne Prüfsummenzeile wird sie als bearbeitete Datei geladen.` |
| Keine Servodatei | `Keine Servodatei. Nicht geladen.` |
| Eine Zeile, die sich nicht lesen lässt | `Zeile 12 ist nicht lesbar. Datei nicht geladen.` |
| Von einer späteren Formatversion geschrieben | `Diese Datei braucht eine neuere Prüfstand-Version. Nicht geladen.` |
| Ein Wert außerhalb seines harten Limits | `Zeile 9: MAX. DUTY liegt außerhalb des erlaubten Bereichs. Nicht geladen.` |
| Werte, die zusammen ein hartes Limit verletzen | `Datei nicht geladen. Grenzwertprüfung fehlgeschlagen:` und die Zeile des Limits |
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

Die Zeile einer bearbeiteten Datei zählt die Werte, die ihre Zeilen
angeben, von den 27 einer Servodatei. Ein Wert, den die Datei nicht
angibt, bleibt, wie ihn das Servo hat.

Eine Datei, deren Werte ein weiches Limit überschreiten, das die Werte des
Servos einhalten, öffnet DIESE DATEI PRÜFEN, bevor etwas zum Entwurf wird.
Die Seite hat 12 Zeilen: die Zeile der Datei, die Zeile einer bearbeiteten
Datei, die abweichenden Werte, wie [die Schreibseite](#schreiben) sie
listet, 4 oder 3 und `und 2 weitere`, und dieselben Warnzeilen wie eine
Änderung über diese Limits hinaus; bei mehr Warnungen als Zeilen zählt die
letzte Zeile den Rest. **HALTEN ZUM
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
gezählt: `2 gesperrte Werte weichen ab. Sie werden nicht geschrieben.` oder
`1 gesperrter Wert weicht ab. Er wird nicht geschrieben.` Der Reiter GESPERRT
markiert jeden solchen Wert mit `!`. Eine Datei ohne Typzeile, die das
20k_sel des anderen Typs enthält, wird genauso geladen, denn nur die
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
| `Backup der zuerst in dieser Sitzung ausgelesenen Werte.` | jedes abweichende Register, vom IOmcu aus dem Abbild, das er selbst hält; ohne die Limit-Prüfungen, und die Seite sagt es. Ein abweichendes Register, das kein Schreiben der Sitzung geändert hat, beendet das Zurückspielen vor seinem ersten Schreiben | nichts |
| `Werte vor dem unterbrochenen Schreibvorgang.` | die änderbaren Felder, als Schreiben mit jeder Limit-Prüfung | die gesperrten Felder, Register 00 und der Pairing-Wert |
| `Werte vor dem Endlagenlauf.` | LEFT ANGLE, RIGHT ANGLE, PULSE LOWER und PULSE UPPER, wie die erste Sitzung des [Endlagenlaufs](#der-endlagenlauf) sie las, als Schreiben mit jeder Limit-Prüfung. Angeboten, solange das gelesene Servo sich in diesen vier Feldern und in keinem anderen davon unterscheidet | jedes andere Feld: es ist so, wie der Lauf es vorfand |

Ein Zurückspielen aus dem Abbild des Panels ist ein gewöhnliches
Schreiben: der IOmcu nimmt ein ungeprüftes Zurückspielen nur für das
Backup an, das er selbst hält, und diese Prüfung wird nicht abgeschwächt.
Die Seite zählt die gesperrten Werte, die abweichen und bleiben:
`2 gesperrte Werte weichen ab. Sie bleiben unverändert.` oder
`1 gesperrter Wert weicht ab. Er bleibt unverändert.` Eine Servodatei steht nicht in
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

Das Backup der Sitzung gehört zu dem Servo, von dem es gelesen wurde. Eine
Lesung, die vom Backup in einem Feld abweicht, das kein Schreiben der
Sitzung ändern sollte, ist ein anderes Servo: `Das ist ein anderes Servo. Das Backup dieser Sitzung gehört nicht dazu.` `VERLASSEN und dann VERBINDEN startet eine neue Sitzung mit eigenem Backup.` Der Schreibschalter geht aus, die Liste zum Zurückspielen
enthält das Backup der Sitzung nicht, und kein Zurückspielen davon wird
gesendet. **VERLASSEN** und **VERBINDEN** beginnen eine Sitzung, deren
Backup das dieses Servos ist. Eine Lesung des ersten Servos bringt sein
Backup zurück. Ein Schreiben, das die Karte, ein Stopp oder der IOmcu
nicht starten lässt oder das der IOmcu beendet, ohne dass ein Register
geschrieben ist, zählt nicht als Schreiben: seine Felder unterscheiden
die Servos weiterhin. Dasselbe gilt für ein Schreiben, das rückgängig
gemacht und zurückgelesen ist, ohne dass ein Schreiben als erledigt
gezählt ist.

Der IOmcu vergleicht noch einmal, Register für Register, bei der
Lesung vor dem ersten Schreiben eines Zurückspielens des Backups der
Sitzung. Er hält die Menge der Register, die die Schreibvorgänge der
Sitzung geändert haben. Ein Servo, das in einem anderen Register vom Backup
abweicht, ist nicht das Servo des Backups: das Zurückspielen endet mit
`Servowerte weichen von den ausgelesenen Werten ab. ALLES AUSLESEN tippen.`, kein Register wird
geschrieben, der Schreibschalter geht aus, und der Port nimmt nur
**ALLES AUSLESEN** an, bis das Servo gelesen ist. Das Zurückspielen repariert
deshalb kein Register, das etwas anderes als ein Schreiben der Sitzung
geändert hat. Nach einem Schreiben, das ohne vollständige Lesung des Servos
endete, zählen die Register, die bei der nächsten vollständigen Lesung
abweichen, als von diesem Schreiben geändert.

Solange nach einem unklaren Schreiben eine Lesung aussteht und die Sitzung
nicht gesperrt ist, enthält die Liste zum Zurückspielen das Backup der
Sitzung nicht: `Lies zuerst das Servo aus. Tippe auf ALLES AUSLESEN.`

Ein Zurückspielen, ein Lösen des Pairings oder ein Schreiben, das die Karte
oder die Control-Task des Panels nicht starten lässt, lässt die offenen
Werte, den Schreibschalter und den Vermerk eines unterbrochenen Schreibens
so, wie sie vor dem Halten waren.

Zwei Servos, die in jedem Feld gleich sind, das die Sitzung
nicht geschrieben hat, werden nicht unterschieden. Nach einem
Verbindungsverlust oder einem Start des Panels unter einer Sitzung sind die
Schreibvorgänge der Sitzung nicht bekannt, und der Vergleich findet nicht
statt.

Ein Aus- und Einschalten des Servos beendet das Backup der Sitzung und den
Vermerk ihrer Schreibvorgänge. Danach kann ein anderes Servo am Port
stecken. Ein solches Aus- und Einschalten ist die Versorgung eines PD mini,
die unter der Sitzung ausgeschaltet wird, ein Versorgungszyklus, den der
Port ausführt, oder der Vorgang POWER_CYCLED, den das Panel statt LEAVE
sendet. Der Port ist dann in PWM, und die Liste zum Zurückspielen enthält
kein Backup der Sitzung. **VERBINDEN** beginnt eine Sitzung, deren Backup
das Servo ist, wie die erste **ALLES AUSLESEN** es vorfindet. Eine
Backup-Datei auf der Karte bleibt dort.

![Ein anderes Servo als das des Backups](img/de/servo-kst-swapped.png)

## Der Endlagenlauf

Der Endlagenlauf des SERVO-Bildschirms führt den Programmierer selbst,
sobald ZIEL auf PROG-PORT steht
([Servoverfahren](Servo-de.md#die-weggrenzen-des-servos-programmieren)).
Seine erste Sitzung liest das Servo, auch bei NUR SUCHE: VERBINDEN mit
seinem Lesen, VERLASSEN. Mit SERVOPULSE auf 1000-2000 schreibt diese
Sitzung zudem PULSE LOWER 1000,0 µs und PULSE UPPER 2000,0 µs. Mit
SUCHEN + PROGRAMMIEREN ist jedes weitere Schreiben eine Sitzung: VERBINDEN mit seinem
Lesen, SCHREIBEN INS SERVO FREIGEBEN an, LEFT ANGLE und RIGHT ANGLE
geschrieben und zurückgelesen, der Schreibschalter aus, VERLASSEN. Was das Overlay offen
hat, schließt sich bei jedem Schritt. Ein Wert unter 50 passiert seine
weiche Grenze ohne die Seite: das Halten von 2 s beim Start des Laufs ist
die Zustimmung. Eine harte Grenze lehnt einen Wert ab wie eine Änderung,
und der Lauf endet. Ein Schreiben, das vor dem Senden abgelehnt wird,
schaltet SCHREIBEN INS SERVO FREIGEBEN wieder aus, wie das Ende eines
gesendeten Schreibens.

Das erste Schreiben des Laufs sichert die Backup-Datei, wie jedes erste
Schreiben. Eine spätere Sitzung des Laufs, deren Servo sich vom ersten Lesen
des Laufs höchstens in LEFT ANGLE, RIGHT ANGLE, PULSE LOWER und PULSE
UPPER unterscheidet, sichert
keine Datei: die erste Datei hält das Servo, wie der Lauf es vorfand. Eine
Sitzung nach dem Ende des Laufs gehört dem Bediener: ihr erstes Schreiben
sichert eine eigene Backup-Datei, wie das erste Schreiben jeder Sitzung. Der
Lauf hält dieses Lesen im Speicher des Panels bis zum ersten Schreiben des nächsten Laufs oder
einem Start des Panels, und ZURÜCKSPIELEN bietet es als `Werte vor dem Endlagenlauf.` an. Solange der Lauf den Port hält, nennen die Zeile ZIEL
und das Feld des Laufs ihn: `PROG-PORT GP13`.

Ein Lauf, der in einer Sitzung abgebrochen wird, lässt den Port im
Programmiermodus mit den zuletzt geschriebenen Werten. VERLASSEN gibt ihn
an PWM zurück; VERBINDEN und ALLES AUSLESEN bieten das Zurückspielen an. Ein
Lauf, der in einer Sitzung nicht besteht, gibt den Port selbst mit
VERLASSEN zurück, und sein Feld bleibt auf der linken Karte. Der Teil des
Laufs im Programmierer, mit den Netzteilwerten, die er hält, endet, sobald
dieses VERLASSEN vorbei ist. Ein VERLASSEN, das scheitert, lässt den Port
im Programmiermodus, und der Programmierer öffnet sich auf der linken
Karte.

## Weiteres

![WEITERES](img/de/servo-kst-more.png)

- **PRÜFEN** liest alle Register und vergleicht sie mit dem gehaltenen
  Abbild: `Prüfung erfolgreich. Das Servo enthält die ausgelesenen Werte.` oder die Zahl der
  abweichenden Register.
- **ÄNDERUNGEN VERWERFEN** setzt den Entwurf auf die Werte des Servos
  zurück.
- **PAIRING LÖSEN** setzt Register 1D auf 00, nach dem Schreibschalter und
  2 s Halten. Was das Servo mit dem Wert tut, ist nicht bekannt. Das
  Zurückspielen des Backups der Sitzung setzt ihn zurück.

![Das Pairing lösen](img/de/servo-kst-release.png)

## Ein unterbrochenes Schreiben

Ein Schreiben gilt als unterbrochen, wenn der Link wegfällt, der
IOmcu oder das Panel neu startet, **ABBRECHEN** oder ein Stopp es
beendet oder kein Ergebnis [rechtzeitig](#stopps-und-ausfälle) kommt. Der
Vermerk von vor dem
Schreiben bleibt dann stehen, auf der Karte auch über ein Aus- und
Einschalten des Prüfstands.

![Ein unterbrochenes Schreiben](img/de/servo-kst-doubt.png)

`Schreiben wurde unterbrochen. Die Einstellungen sind vielleicht unvollständig.` bleibt stehen,
bis das Servo gelesen ist, darunter der Pin des Schreibens: `Benutze das Servo nicht. An GP13 anschließen und ALLES AUSLESEN tippen.` Ein Vermerk ohne Pin nennt
stattdessen den Port: `Benutze das Servo nicht. An Port 1 anschließen und ALLES AUSLESEN tippen.` Jedes Bedienelement außer
**VERBINDEN**, **ALLES AUSLESEN**, **ZURÜCKSPIELEN**, **VERLASSEN** und, wo
das Zurückspielen der Sitzung ihn braucht, dem Schreibschalter ist aus. Das Lesen vergleicht das Servo mit dem Vermerk:

| Das Servo hält | Meldung | Danach |
| --- | --- | --- |
| das Abbild vor dem Schreiben | `Ist dies das Servo, dessen Schreibvorgang an GP13 unterbrochen wurde?` `Es enthält die alten Werte. Ist es dasselbe Servo, den Vermerk unter ZURÜCKSPIELEN verwerfen.` | der Vermerk bleibt, bis er verworfen wird |
| das Abbild, das das Schreiben hinterlassen sollte | `Ist dies das Servo, dessen Schreibvorgang an GP13 unterbrochen wurde?` `Es enthält die neuen Werte. Ist es dasselbe Servo, den Vermerk unter ZURÜCKSPIELEN verwerfen.` | der Vermerk bleibt, bis er verworfen wird |
| etwas anderes | `Das Servo enthält alte und neue Werte gemischt.` `Benutze dieses Servo nicht. Stelle das Backup mit ZURÜCKSPIELEN wieder her.` | **ZURÜCKSPIELEN** bietet das Backup der Sitzung an, solange die Sitzung lebt, und `Werte vor dem unterbrochenen Schreibvorgang.` |

![Das Servo hält eine Mischung](img/de/servo-kst-journal.png)

`Vermerk zum unterbrochenen Schreibvorgang verwerfen.` in der Liste entfernt den
Vermerk ohne ein Schreiben.

Ein Servo, das Register für Register das Abbild vor dem Schreiben oder das
Abbild hält, das das Schreiben hinterlassen sollte, ist entweder das Servo
des Schreibens oder ein zweites Servo mit denselben Einstellungen. Das
Panel kann die beiden nicht unterscheiden und fragt. Die zwei Zeilen
bleiben auf dem Tab SERVO stehen, der Schreibschalter geht nicht an, und
kein Schreiben, kein Zurückspielen und keine Pairing-Freigabe wird
gesendet. Ist es das Servo des Schreibens, beendet `Vermerk zum unterbrochenen Schreibvorgang verwerfen.` in der Liste die Frage. Ist es das
nicht, behält **VERLASSEN** den Vermerk für das Servo des Schreibens.

Der Vermerk gehört zu einem Servo. Ein Servo, das an einem anderen Port
gelesen wird, ist nicht dieses Servo, und ebenso wenig eines mit einem
Register, das weder den Wert des Abbilds vor dem Schreiben noch den Wert
hält, den das Schreiben hinterlassen sollte: `Der unterbrochene Schreibvorgang betraf ein anderes Servo.` `Es war an GP13 angeschlossen. Vermerk unter ZURÜCKSPIELEN verwerfen.` Bei einem Vermerk ohne Pin nennt die Zeile den Port: `Es war an
Port 1.` Solange das steht, wird nichts zurückgespielt, geändert oder
geschrieben, und **ZURÜCKSPIELEN** enthält nur den Eintrag zum Verwerfen.
**VERLASSEN** und ein **VERBINDEN** des Servos des Schreibens bringen den
Vergleich von oben. Ein Register ist ausgenommen: das niedrige Register
eines Felds, das in zwei Registern liegt und das das Schreiben ändert, weil
der Plan eines Schreibens es unterwegs auf einen dritten Wert setzen kann.

Der Vergleich findet statt, wenn das Servo gelesen wird und wenn der
Vermerk geladen wird: ein Vermerk von einer Karte, die mit einem schon
gelesenen Servo eingebunden wird, wird sofort mit diesem Servo verglichen.
Ein Zurückspielen des Backups der Sitzung ohne gelesenes Servo gehört nicht
zu einem gehaltenen Vermerk: der Vermerk bleibt, wie er ist, auf dem Panel
und auf der Karte. Ohne gehaltenen Vermerk legt ein solches Zurückspielen
seinen eigenen an, mit dem Backup als dem Abbild, das das Servo halten
soll. Ein Vermerk, der ohne eingehängte Karte entsteht, wird auf eine
später eingehängte Karte geschrieben.

![Ein anderes Servo als das des Vermerks](img/de/servo-kst-other.png)

Max. Duty liegt in den Registern 01 und 02. Ein Schreiben, das zwischen
diesen beiden abreißt, hinterlässt ein Abbild, das ein hartes Limit
verletzt, und das Zurückspielen des Panels, ein geprüftes Schreiben,
startet von einem solchen Abbild nicht. Das Zurückspielen des Backups der
Sitzung tut es. Nach einem Neustart des IOmcu in diesem Zustand
liest der Programmierer das Servo und schreibt nichts darauf.

## Verlassen

**VERLASSEN**, oder **SCHLIESSEN** im Programmiermodus, verlangt, die
Sitzung zu beenden und den Port an Servo-PWM zurückzugeben. Der
Programmierer tut das ohne Aus- und Einschalten der Versorgung: das Servo
bleibt versorgt, der Ausgang eines PD mini bleibt mit seiner Spannung und
Stromgrenze an, und das Signal des Testers folgt auf demselben Port. Ob ein
Servo, das einen Frame erhalten hat, PWM folgt, bevor seine Versorgung
aus war, ist nicht gemessen.

Das Verlassen des Bildschirms SERVO ist kein VERLASSEN: Sitzung und Port
bleiben. Ein Ausgang des PD mini, den SERVO eingeschaltet hat -- mit
AUSGANG EIN, mit einem Test oder mit **VERBINDEN** --, gehört dem
Bildschirm SERVO, und solange der Port ihn hält, bleibt der Ausgang nach
dem Verlassen von SERVO an: ein OFF würde die Sitzung beenden und ein
Schreiben auf der Leitung abschneiden. Ein ON, das SERVO verlangt hat und
das auf dem Weg ist, wenn der Port das Netzteil übernimmt, zählt als
eingeschaltet, auch wenn es erst nach dem Verlassen als an gemeldet wird.
Der Ausgang geht aus, wenn der Port loslässt -- ein **VERBINDEN**, das nach
dem Verlassen von SERVO ohne Sitzung endet, ein STOP, der das Netzteil
ausschaltet --, außer SERVO wird vorher wieder gezeigt. Nach
**VERLASSEN** auf SERVO bleibt der Ausgang an, bis SERVO verlassen wird.

PROGRAMMIERMODUS VERLASSEN öffnet mit
`Der Port wechselt zurück zu Servo-PWM. Die Versorgung bleibt unverändert.` und
`Das Servo erhält wieder das Testsignal und kann sich bewegen.` Die Seite
warnt, bevor geänderte Werte verloren gehen und bevor eine Sitzung endet,
die ein unklares Servo noch zurückspielen könnte. **HALTEN ZUM VERLASSEN**
für 2 s sendet den Vorgang LEAVE des Ports aus der Link-Version 4.15. Kein
Frame geht an das Servo und kein Versorgungsbefehl an den PD mini. Der Port
hält seine Leitung für [**WARTEZEIT VOR PWM**](#die-versorgung) auf Low und
gibt den Pin dann an den PWM-Generator.

![Verlassen](img/de/servo-kst-leave.png)

Die Seite ist dieselbe, wenn ein PD mini antwortet.

![VERLASSEN mit einem PD mini](img/de/servo-kst-leave-supply.png)

Die Zustandszeile zeigt
`Programmiermodus wird verlassen. PWM ist noch aus.`, und die
Bedienelemente des Testers bleiben aus, bis der Port PWM meldet. Die Taste
heißt **ABBRECHEN**, solange das Warten läuft. Danach steht
`Der Port ist wieder im Testermodus.`, und der Tester treibt den Port sofort,
mit **ZIEL** auf **PROG-PORT** und **PORT-PWM** ein. Das nächste
**VERBINDEN** beginnt eine neue Sitzung mit eigenem Lesen.

### Das Warten vor PWM

**WARTEZEIT VOR PWM** ist 0 bis 10 000 ms, ab Werk 100 ms. Der Ziffernblock
nimmt keinen Wert über 10 000 ms an, und mit einem solchen wird kein
Verlassen gesendet. Ein Warten von 0 ms endet beim nächsten Durchlauf des
IOmcu. Die Restzeit eines laufenden Wartens wird nicht angezeigt.
Das Warten, das ein Servo braucht, ist nicht gemessen.

| Während des Wartens | Wirkung |
| --- | --- |
| **ABBRECHEN** | der Port bleibt im Programmiermodus, die Sitzung wie zuvor; `Verlassen wurde vor dem Ende der Wartezeit unterbrochen.`; **VERLASSEN** wird sofort wieder angenommen |
| **STOP** oder ein Wechsel des Prüfstands auf SAFE, kein PD mini antwortet | dieselbe Zeile; der Port bleibt im Programmiermodus und verlangt **ALLES AUSLESEN** |
| Der Ausgang der Versorgung liest aus oder wird ausgeschaltet: **STOP** mit antwortendem PD mini, der eigene Schutz des PD mini | dieselbe Zeile; die Versorgung bleibt 2000 ms aus, dann ist der Port in PWM |
| Der Link fällt aus | wie [ein Ausfall des Links](#stopps-und-ausfälle) |

### Ein Port, der nicht verlassen wird

Der Port lehnt ein Verlassen ab, solange er ein Lesen verlangt oder ein
Servo hält, das ein Schreiben unklar hinterlassen hat.

| Der Port | Ein PD mini antwortet | Zeile | Ausweg |
| --- | --- | --- | --- |
| verlangt nach einem Stopp oder Abbruch ein Lesen | ja | `Zuerst das Servo auslesen. ALLES AUSLESEN tippen.` | **ALLES AUSLESEN**, dann **VERLASSEN**; oder **STOP**, das die Versorgung ausschaltet |
| hält ein unklares Servo | ja | `Servozustand unklar. Sitzung bleibt offen. Zuerst ZURÜCKSPIELEN.` und `STOP beendet die Sitzung und schaltet die Versorgung aus.` | **ZURÜCKSPIELEN**, dann **VERLASSEN**; oder **STOP** |
| eines von beiden | nein | `Der Port ist wieder im Testermodus.` und `Sitzungszustand war unklar. PWM startet ohne Wartezeit.` | keiner nötig: der Port ist in PWM |

Ohne PD mini schaltet **STOP** keine Versorgung und lässt den Port im
Programmiermodus, und ein Servo, das nicht mehr antwortet, nimmt kein Lesen
an. Das Panel sendet dann den Vorgang POWER_CYCLED des Ports statt LEAVE.
Ohne PD mini ändert er nur den Zustand des Ports: kein Frame, kein Warten,
und die Versorgung des Servos ist die, die es speist. Der Datensatz eines
unterbrochenen Schreibens bleibt für die nächste Sitzung.

### Ein IOmcu vor Link-Version 4.15

Die Link-Versionen 4.13 und 4.14 haben kein LEAVE. Das Panel sendet ihnen
keines.

| Ein PD mini antwortet | Das Panel | Zeilen |
| --- | --- | --- |
| nein | sendet POWER_CYCLED, das nur den Zustand des Ports ändert; der Port ist ohne Warten in PWM | `Der Port ist wieder im Testermodus.` und `IOmcu-Firmware älter als 4.15. PWM startet ohne Wartezeit.` |
| ja | sendet nichts: beide Vorgänge dieser Versionen für den Schritt schalten die Versorgung aus und wieder ein. Die Sitzung bleibt | `Verlassen mit PD mini erfordert IOmcu-Firmware ab 4.15.` und `STOP beendet die Sitzung und schaltet die Versorgung aus.` |

Mit einem PD mini an einem solchen IOmcu beendet **STOP** oder der
ausgeschaltete Ausgang der Versorgung die Sitzung: die Versorgung bleibt
2000 ms aus, und der Port ist wieder in PWM.

## Stopps und Ausfälle

| Ereignis | Wirkung |
| --- | --- |
| **STOP**, ein gehaltener Stopp des Prüfstands | der laufende Vorgang endet nach dem Frame auf der Leitung, höchstens 12 ms; der Schalter geht aus; `STOP: der Vorgang wurde abgebrochen.`; der Port verlangt **ALLES AUSLESEN** vor allem anderen. Der Ausgang eines PD mini wird ausgeschaltet, und der Port ist 2000 ms, nachdem er aus liest, wieder in PWM |
| **ABBRECHEN** | dasselbe, ohne den gehaltenen Stopp |
| Der Link fällt weg | der Schalter geht aus, offene Seiten schließen; ein laufendes Schreiben ist [unterbrochen](#ein-unterbrochenes-schreiben) |
| Der IOmcu startet neu | seine Sitzung und sein Backup sind weg; `IOmcu neu gestartet. VERBINDEN, dann ALLES AUSLESEN tippen.` und `Schalte zuerst die Servoversorgung aus und wieder ein.`: der Port ist in PWM, und das Servo wurde nicht aus dem Programmiermodus geführt |
| Der Port liest PWM bei gelesenem Servo, eingeschaltetem Schalter, erfolgtem Schreiben oder verlangtem Lesen, und kein Kommando des Programmierers hat ihn dorthin gebracht | die Werte des Servos, das Backup und der Schalter sind auf dem Panel weg; `Der Port hat die Sitzung beendet: Versorgung aus oder Neustart.` und `Servoversorgung aus- und einschalten, dann VERBINDEN tippen.` Ein Vorgang, der in derselben Ablesung endete, behält seine eigene Zeile |
| Die Control-Task des Panels nimmt 2 s kein Kommando an | nichts wird gesendet; `Kein Ergebnis vom IOmcu. ALLES AUSLESEN tippen.` Das Kommando ist zurückgezogen: die Control-Task verwirft es, wenn es die Warteschlange verlässt, wie spät auch immer |
| Der IOmcu lehnt einen Austausch eines Vorgangs ab, den er angenommen hat | die Zeile der Ablehnung; die Werte auf dem Panel werden vor einem **ALLES AUSLESEN**, das endet, nicht geändert, gespeichert oder geschrieben |
| 3 Austausche mit dem IOmcu in Folge bleiben unbeantwortet | `Keine Antwort vom IOmcu. ALLES AUSLESEN tippen.`; nach einem Lesen oder Prüfen werden die Werte auf dem Panel vor einem **ALLES AUSLESEN**, das endet, nicht geändert, gespeichert oder geschrieben |
| 30 s kein Ergebnis eines Vorgangs, gezählt ab der letzten Meldung des Ports, dass der Vorgang läuft oder plant, oder 120 s ab seinem Start, wie lange der Port ihn auch als laufend meldet | der Vorgang wird abgebrochen; `Kein Ergebnis vom IOmcu. ALLES AUSLESEN tippen.`; die Werte auf dem Panel werden vor einem **ALLES AUSLESEN**, das endet, nicht geändert, gespeichert oder geschrieben; ein Schreiben ist [unterbrochen](#ein-unterbrochenes-schreiben) |
| Das Ergebnis eines Vorgangs kommt, auf den der Programmierer nicht mehr wartet | es beendet keinen anderen Vorgang: ein Ergebnis zählt für die Anfrage, deren Nummer es trägt. **ABBRECHEN** erreicht einen Vorgang, den der Port noch als laufend meldet |
| Der IOmcu meldet ein Ergebnis, eine Ablehnung oder einen Ausgang, für den dieses Panel keine Zeile hat | `IOmcu meldet Code 255. Dieses Panel kennt ihn nicht.`, mit der Nummer des Codes; ein Ergebnis wird als fehlgeschlagener Vorgang behandelt |

Jede Ablehnung des Ports und jedes Ergebnis der Sitzung hat seine eigene
Zeile. [Die Link-Page](LinkKST-de.md) listet die
Codes.

## Einschränkungen

- Ein laufendes Schreiben zeigt die Zahl der Register, die es setzt, und
  die gesendeten Frames. Das Register, das gerade geschrieben wird, steht
  nicht auf der Link-Page und wird nicht gezeigt. Ein Zurückspielen des
  Backups der Sitzung und ein Lösen des Pairings zeigen nur die Frames.
- Die Bildschirme schreiben `us` für µs: die Schrift hat kein
  Mikro-Zeichen.
- Die Zeile BEWEGEN BEI SIGNALVERLUST ist übersetzt; die anderen Felder
  tragen die Namen des Hersteller-Tools.
- Der Datensatz eines unterbrochenen Schreibens wird beim Start des Panels
  von der Karte gelesen, und von einer später eingesteckten Karte, wenn der
  Programmierer sie einbindet. Ein Datensatz, den das Panel schon hält,
  bleibt, vor und nach dem Zurücklesen des Servos: der Datensatz auf einer
  solchen Karte wird dann nicht gelesen, und der nächste geschriebene
  Datensatz ersetzt ihn.
- Eine Karte, die im Betrieb entnommen und wieder eingesteckt wird, wird
  nicht erneut eingebunden.
- Ist der IOmcu ohne Versorgung und das Servo aus einer anderen
  Versorgung gespeist, überschreitet ein Servo, das die Signalleitung über
  3,63 V treibt, den für GP13 spezifizierten Wert
  ([der Signalpegel](#der-signalpegel)).
- Wie lange ein Einbindeversuch ohne Karte den Bildschirm anhält, ist nicht
  gemessen.
- Die Grenzen und Abschalt-Einstellungen des SUPPLY-Bildschirms sind nicht
  gehalten, solange der Programmierer den Port hat; die Sollwerte und
  **AUSGANG EIN** sind es.
- Mit dem simulierten Netzteil (kein PD mini verdrahtet) dimmt das Panel
  dieselben Bedienelemente, und die Versorgung des Servos ist, was es
  speist.
- Die Zeilen **ZIEL** und **PORT-PWM** folgen dem Zustand des Ports
  einen Frame des Bildschirms nach der Control-Task. Die Control-Task hält
  die Seite des Ports und die Kommandos des Testers nach ihrer eigenen
  Lesung des Ports.
- Der Code des Panels in `firmware/panel/main/` ist nicht in der Host-Suite: die Warteschlange der Kommandos zwischen Bildschirm und
  Control-Task, die Stelle, an der die Control-Task ein zurückgezogenes
  Kommando verwirft, die Übergabe des Portzustands an die Seite und den
  Tester, und die Dateiaufrufe der Karte.
- Ob ein Servo PWM folgt, während es im Programmiermodus ist, ist nicht
  gemessen. Der Programmierer verlässt das Programmieren ohne Aus- und
  Einschalten der Versorgung und treibt den Port danach mit PWM.
- Ob ein geschriebener Wert vor dem nächsten Einschalten des Servos wirkt,
  ist nicht gemessen. Der Programmierer behandelt einen Wert, der sich
  zurücklesen lässt, als wirksam.
- Antwortet ein PD mini an einem IOmcu vor Link-Version 4.15, wird
  der Port nicht aus dem Programmierer verlassen: **STOP** oder der
  ausgeschaltete Ausgang der Versorgung beendet die Sitzung, mit
  ausgeschalteter Versorgung.
- Antwortet ein PD mini, wird ein Port mit einem unklaren Servo durch
  **ZURÜCKSPIELEN** und **VERLASSEN** verlassen, oder durch **STOP** mit
  ausgeschalteter Versorgung.
- **WARTEZEIT VOR PWM** wird von einem IOmcu vor Link-Version 4.15 nicht
  angewendet, und nicht beim Verlassen eines unklaren Ports ohne PD mini.
  Das Warten, das ein Servo braucht, ist nicht gemessen. Die Restzeit
  eines laufenden Wartens wird nicht angezeigt.
- Ohne PD mini werden die Programmierspannung und die Stromgrenze nicht
  angewendet.
- Der Fehlercode, den das Dateisystem der Karte bei einer
  schreibgeschützten Karte liefert, ist nicht bekannt. Ein anderer Code
  als schreibgeschütztes Dateisystem, Zugriff verweigert und Operation
  nicht erlaubt zeigt `Schreibfehler auf der Karte.` statt
  `Speichern nicht möglich: Karte schreibgeschützt oder KST kein Ordner.`
- Die Vorgabenamen enden bei `KST999`. Ist dieser Name vergeben, wird die
  nächste freie Nummer vorgeschlagen, und die höchste Nummer ist dann nicht
  die neueste Datei.
- Eine Seite hinter den ersten 64 Dateien liest das Verzeichnis je
  64 Dateien davor ein weiteres Mal. Die Zeit dafür ist nicht gemessen;
  das Panel wartet 5 s auf eine Seite.
- Ein Schreiben ohne Karte hinterlässt keine Backup-Datei. Das Backup der
  Sitzung ist dann die einzige Kopie des Servos, wie es vorgefunden wurde,
  und endet mit **VERLASSEN**, einem Neustart des IOmcu oder einem
  Stromausfall des Prüfstands.
- Das erste Schreiben einer Sitzung mit eingehängter Karte braucht freien
  Platz auf ihr.
- Eine Servodatei trägt den Servotyp, V6 oder V8, und keine weitere
  Identität. Die Datei eines V8-Servos lädt in jedes V8-Servo.
- Eine Servodatei spielt nur die änderbaren Felder zurück:
  [was sie nicht zurückspielt](#was-eine-datei-nicht-zurückspielt).
- Die Liste zeigt 5 Dateien je Seite, nach Namen sortiert, und hat keine
  Suche. Dateien werden auf dem Panel nicht entfernt oder umbenannt.
- Der Vermerk eines unterbrochenen Schreibens nennt seinen Port und hält
  die Werte des Servos. Ein zweites Servo an diesem Port, dessen 32 Register
  dem Abbild vor dem Schreiben oder dem Abbild gleichen, das das Schreiben
  hinterlassen sollte, wird vom ersten nicht unterschieden: das Panel
  fragt, und die Antwort gibt der Bediener.
- Ohne Karte geht der Vermerk eines Schreibens beim Ausschalten verloren.
- Eine eingehängte Karte, die den Vermerk nicht annimmt, verhindert jedes
  Schreiben: eine nach dem Einhängen gezogene Karte, eine schreibgeschützte
  Karte oder eine Karte, auf der `KST/KST.TXT` oder `KST/KST.NEW` ein
  Ordner ist. Jeder Versuch endet mit `Datei nicht auf Karte gespeichert. Keine Werte ins Servo geschrieben.` Das Panel hängt eine solche Karte nicht aus. Ein ohne
  Karte gestartetes Panel hält den Vermerk im Speicher und schreibt.
- Der Vermerk eines beendeten Schreibens wird auf der Karte nach dem
  Schreiben gelöscht, durch ein eigenes Schreiben der Datei, das alle 5 s
  wiederholt wird, bis die Karte es annimmt. Ein Stromausfall davor lässt
  den Vermerk eines beendeten Schreibens stehen: beim nächsten Lesen fragt
  das Panel, ob das Servo das des Vermerks ist.
- Der beim Einschalten gefundene Vermerk eines unterbrochenen Schreibens
  wird nur im Programmierer gezeigt; kein anderer Bildschirm meldet ihn.
- Ein Servo, dessen gelesenes Abbild ein hartes Limit verletzt, lässt sich
  lesen und speichern. Ein Schreiben darauf wird abgelehnt.
- LEFT ANGLE und RIGHT ANGLE sind die Zahlen des Hersteller-Tools; ihre
  Einheit ist nicht bekannt.
- Die Versorgungsspannung, die ein Servo im Programmiermodus braucht, ist
  nicht gemessen. Die Untergrenze ist 4,8 V.
- Eine Sitzung hat kein Zeitlimit im Leerlauf. Von **VERBINDEN** bis
  **VERLASSEN** ist der Port gehalten, das Servo im Programmiermodus und
  der Ausgang eines PD mini ein, auf jedem Bildschirm und solange der
  Prüfstand versorgt ist. **STOP**, ein Link-Ausfall und das Ausschalten
  des Versorgungsausgangs beenden sie.
- Eine Karte, die im Betrieb eingesteckt wird, liest die Task, die den
  Bildschirm zeichnet. Der Bildschirm ändert sich für die Dauer dieses
  Lesens nicht und nimmt keine Berührung an. Die Dauer ist nicht gemessen.
- Die 120 s, die ein Vorgang ab seinem Start hat, sind gewählt. Wie lange
  das längste Schreiben an einem Servo dauert, ist nicht gemessen.

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
   Programmiermodus beendet, ob ein Servo nach dem Programmieren ohne Aus-
   und Einschalten PWM folgt, und das Warten, das es vor dem ersten Puls
   braucht.
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

`test_prog_link` fährt die Panelseite des Ziels und des Pulsschalters des
Ports: einen Port ohne Pin, ein Ziel, das auf einen Prüfstand in Ruhe
wartet, einen IOmcu, der neu startet, und einen Programmierer, der
den Port hat, für jedes der 16 Paare aus Seite und gewünschten Werten.
`test_settings_screen` fährt eine gehaltene Einstellung gegen jeden Aufruf, der
eine ändert, und `test_servo` die zwei Zeilen des Reiters PRÜFLING.
`test_kst_card` fährt die Kartenseite der Dateien auf einer Karte im Arbeitsspeicher. `test_kst_prog` und `test_kst_prog_card`
fahren das Modell und `test_kst_panel` das Overlay, beide gegen den Port des
IOmcu, ein Versorgungsmodell und das Servomodell durch den
Dispatcher des Links: die Bereiche der Versorgung an ihren Enden und einen
Schritt außerhalb, das Warten bei 0 ms, 100 ms, 10 000 ms und 10 001 ms als
Warten des Verlassens, ein Abbruch, ein Wechsel auf SAFE und eine Versorgung, die im
Warten ausgeht, ein Verlassen an den Link-Versionen 4.13 und 4.14 mit und
ohne PD mini, **STOP** während eines Verlassens, jedes Limit an seiner Kante, das Halten von 2 s
auf beiden
Seiten der Schwelle, jede Ablehnung und jedes Ergebnis mit seiner Zeile in
beiden Sprachen, die Zeilen der Schreibseite für 1 Wert, für mehr Werte als
Zeilen und für einen Wert über einem weichen Limit, ein weiches Limit, das
für einen Wert weiter vom Wert des Servos erneut gefragt wird, die Zeilen
zum Ruderhorn bei einem späteren Schreiben, das das Horn bewegt, die
Zustandszeile und die Zeile des Reiters bei einem laufenden Schreiben, eine
Meldung, die geht, wenn ihre Bedingung endet, die Zeilen, die keinen Tipp
annehmen, die Beschriftung der Zeile PORT, kein Komma in einer Zahl der
deutschen Tabelle, ein Schreiben, das ein Link-Ausfall, ein Neustart,
**ABBRECHEN** und **STOP** abschneiden, eine Karte, die versagt, und die
Timeouts über den Überlauf der Uhr bei 2^32 ms. `test_kst_panel_files` fährt die
Servodateien gegen ein Kartenmodell: jedes Ergebnis eines Namens, jede Art von Datei,
beide Servotypen und ihre Abweichung in beiden Richtungen, jede
Ablehnung, die Grenze von 5 s, ein erstes Schreiben mit einer Karte, die
voll ist oder versagt.
`test_kst_panel_nocard` fährt die Schreiben ohne Karte: das erste Schreiben,
ein Zurückspielen und ein Lösen des Pairings, ein früh losgelassenes
Halten, eine zwischen zwei Schreiben eingehängte Karte und eine unter der
Schreibseite entnommene Karte. Ein Kommando, das die Control-Task nach
Ablauf seiner 2 s annimmt, erreicht keine Leitung, für jede Art von
Vorgang; eine Ablehnung bei jedem späteren Austausch jeder Art von Vorgang
verlangt **ALLES AUSLESEN**; jeder Ergebniscode der Sitzung hat eine Klasse,
und ein Code ohne Klasse lässt den Test fehlschlagen; ein Zurückspielen,
das die Karte bei jedem Dateischritt nicht starten lässt, lässt die offenen
Werte stehen; ein Vermerk von einer Karte, die vor und nach dem Lesen
eingebunden wird, desselben und eines anderen Servos.
