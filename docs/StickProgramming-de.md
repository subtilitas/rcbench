# Stick-Programmierung

[English](StickProgramming.md)

Die Stick-Programmierung stellt einen ESC (Electronic Speed Controller) über
sein Gasknüppel-Menü ein. Der Prüfstand versorgt den ESC aus dem PD mini,
mit einem Lastwiderstand anstelle des Motors oder einem fest montierten
Motor ohne Propeller. Er bewegt das Gas in die
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
  FERTIG heißt: jede Auswahlbewegung kam auf eine gezählte Gruppe in der
  Reihenfolge des Menüs, nicht, dass der ESC sie gespeichert hat.

## Vor einem Lauf

1. Den Propeller abnehmen. Ein Lauf fährt das Gas auf MAX und zurück,
   während der ESC versorgt ist. Der ESC ist dann in seinem
   Programmiermenü und treibt nicht, aber in seltenen Fällen kann ein Motor
   anlaufen und drehen.
2. Entweder den Motor am ESC lassen, fest montiert, oder an seiner Stelle
   einen Lastwiderstand an die Motorleitungen des ESCs legen. Die Pieptöne
   werden in beiden Fällen aus dem Strom gezählt. Der Wert des Widerstands
   ist nicht festgelegt: kein ESC ist in einen solchen gelaufen. Er muss
   den Strom bei der Spannung des Laufs unter dessen Strombegrenzung
   halten. Wie gut Pieptöne durch die Wicklungen eines Motors im Strom zu
   lesen sind, ist nicht gemessen.
3. Die Signalleitung des ESCs an einen Pin legen, der auf dem Bildschirm
   OUTPUTS mit der Rolle Throttle gebunden ist. Ein Lauf steuert jeden als
   Throttle gebundenen Kanal, wie der Bildschirm MOTOR.
4. Die Versorgungsleitungen des ESCs an den Ausgang des Netzteils legen.
5. Den Ausgang des Netzteils ausschalten. Ein Lauf, der ihn an oder auf dem
   Weg findet, wird abgelehnt, ebenso einer, dessen Netzteil spannungsführend
   meldet: sein eigener Zustand an, oder mehr als 20 mA (`ESC_STICK_OFF_MA`)
   durch den Ausgang, im neuesten Messwert. Die Zeile neben START sagt, es
   zuerst auszuschalten. START braucht außerdem ein Netzteil, das bekannt
   aus ist: einen Messwert, nicht älter als 1000 ms (`ESC_STICK_STALE_MS`),
   in dem das Netzteil selbst seinen Ausgang aus meldet, mit dem Strom
   200 ms lang bei höchstens 20 mA. Kein Messwert, ein älterer oder ein
   Netzteil, das nicht antwortet, lehnt START ab mit `kein frischer
   Messwert meldet das Netzteil aus`.

## Auf dem Bildschirm

PROGRAMMER, dann ESC STICK:

![Geräteklasse](img/de/programmer.png)

Die Liste hat zwei Ebenen.

- **Hersteller.** Eine Zeile je Hersteller, alphabetisch ohne Rücksicht
  auf Groß- und Kleinschreibung: sein Name, wie viele seiner Modelle laufen
  von allen, die er führt (`10 VON 82 AUSFÜHRBAR`), und das rote Schild
  HAND, wo ein Modell [Handgriffe](#handgriffe) hat. Ein Hersteller, von
  dem kein Modell läuft, ist dunkel gezeichnet. Ein Tippen öffnet seine
  Modelle.
- **Modelle.** Eine Zeile je Modell dieses Herstellers, nach Strom, dann
  nach höchster Eingangsspannung, dann nach Name; ein Modell ohne Angabe
  von Strom oder Spannung kommt in diesem Schlüssel zuletzt. Eine Zeile
  zeigt das Modell, seinen Strom und seine Spannung, seine Familie, das
  Schild HAND und was das Profil seiner Familie ist (Punkte, ein- oder
  zweistufig, KARTE für ein Profil von der SD-Karte (Secure Digital)) oder
  warum es nicht läuft. Der Pfad lautet `ESC STICK > Kontronik`. ZURÜCK
  führt zu den Herstellern auf der Seite, die es verließ.

Ein Tippen auf ein Modell öffnet das Profil seiner Familie, mit dem Modell
benannt: `ESC STICK > Kontronik > JAZZ 55 LV (JAZZ / MINIJAZZ)`. Das
Netzteil nimmt die eigene niedrigste Zellenzahl des Modells, wo es eine
nennt, sonst die niedrigste der Familie. Ein Modell, dessen Profil der
Prüfstand nicht ausführen kann, nennt den Grund in seiner Zeile und öffnet
nichts; ebenso eines, dessen Spannung über der Grenze von SUPPLY liegt.
Eines, dessen Profil Handgriffe hat, öffnet stattdessen diese, mit dem Grund für dieses Modell. Die Handgriffe, die Netzteilzeile der Seite, START und die Warnung beurteilen alle das geöffnete Modell, nicht das niedrigste der Familie. Zeilen und
Zählungen folgen SPANNUNG und der Grenze, wenn sie sich ändern. Ein Profil
von der Karte reiht sich bei den Modellen seines Herstellers ein; ein
Hersteller von der Karte, den der Satz nicht hat, reiht sich alphabetisch
bei den Herstellern ein. Jedes eingebaute Profil nennt seinen Hersteller;
der Kartenleser lehnt eine Datei ohne ihn ab. Er lehnt auch eine Datei ab,
die ihren Hersteller über 512 Modelle brächte, also führt ein Hersteller
höchstens 512 Modelle.

Die Zeile unter den Zeilen sagt, dass kein Profil geprüft ist, und die
Zählung rechts davon zählt die Ebene: `1-9/20 Hersteller, 6 ausführbar` --
die Hersteller mit einem laufenden Modell -- oder `1-9 von 82, 40
ausführbar` für die Modelle eines Herstellers.

![Die Hersteller](img/de/programmer-stick.png)

![Die Modelle von Kontronik mit dem Schild HAND](img/de/programmer-stick-hand-list.png)

### Suche

SUCHE in der Pfadzeile filtert die gezeigte Ebene. Ein Tippen darauf dockt
die Texttastatur rechts an, und die Zeilen werden links daneben schmaler:
der Hersteller oder das Modell, die gefundenen Modelle des Herstellers oder
der Strom eines Modells, und ein Zeichen, ob es läuft (ein gefüllter Punkt)
oder nicht (ein Ring); die Zählung steht unter den schmalen Zeilen. Jede
Taste filtert sofort, und die Ebene geht zu ihrer ersten Zeile zurück,
sobald sich die Suche ändert.

- Ein Modell wird gefunden, wenn der Suchtext irgendwo in seinem
  Hersteller und dem Namen seiner Familie steht, als ein Text gelesen,
  "Hobbywing Skywalker V2 15A-100A, 11-item menu", oder in seinem
  Hersteller und seinem eigenen Namen, "Kontronik JAZZ 55 LV".
- Ein Hersteller erscheint, wenn die Suche eines seiner Modelle findet;
  seine Zeile zählt sie dann: `1 VON 82 GEFUNDEN, 1 AUSFÜHRBAR`. Kein
  Hersteller öffnet sich von selbst, auch nicht als einziger gefundener.
- Groß- und Kleinschreibung spielen keine Rolle: `KONTR*Jazz` findet
  Kontronik und darin die Modelle von JAZZ und MINIJAZZ.
- `*` steht für eine beliebige Folge von Zeichen, auch keine. Die Taste
  `*` sitzt dort, wo die Namenstastatur `_` hat. `sky*v2` findet Hobbywing
  und darin die 14 Skywalker-V2-Modelle; `*kontr*jazz*55*` findet
  Kontronik und darin nur JAZZ 55 LV.
- Eine leere Suche zeigt jeden Hersteller und jedes Modell. Die Suche
  fasst bis zu 16 Zeichen und gilt auf beiden Ebenen.
- Die Zählung sagt, was gefunden wurde: `1-1/1 Hersteller, 1 ausführbar`.
  Ohne Treffer sagt die Liste `Kein Profil passt zur Suche.`

OK schließt die Tastatur und behält die Suche, ABBRECHEN geht zur Suche
zurück, mit der die Tastatur geöffnet wurde, und CLR, dann OK leert sie.
Eine Zeile, die bei offener Tastatur getippt wird, öffnet sich, die Suche
bleibt. Bei geschlossener Tastatur leert X im Feld die Suche. Die Suche
bleibt über beide Ebenen, die Seite eines Profils und zurück und beim
Verlassen des Bildschirms erhalten und ist nach einem Neustart leer.

![Eine Suche auf den Herstellern wird getippt](img/de/programmer-stick-find.png)

![Eine Suche angewendet, die Modelle von Hobbywing](img/de/programmer-stick-found.png)

Die Groß- und Kleinschreibung wird nur für die Buchstaben A bis Z
ausgeglichen. Ä, Ö und Ü in einem Profil von der Karte passen nur auf
denselben Buchstaben in derselben Schreibung, und die Tastatur hat keine
Taste für sie; `*` steht für einen davon. Kein eingebautes Profil hat einen
Buchstaben außerhalb von ASCII (American Standard Code for Information
Interchange) im Namen.

### Ein Profil und sein Lauf

Die Seite eines Profils listet seine Menüpunkte. Jeder steht anfangs auf
BEHALTEN und bleibt dann, wie er ist; die Stepper gehen durch die Werte des
Punkts und halten an beiden Enden an. Punkte mit dem Schlüssel `reset` oder
`exit` sind Aktionen, keine Einstellungen: der ESC handelt auf die
Auswahlbewegung und gibt keine Werte aus, deshalb zeigen ihre Zeilen AKTION,
NICHT SETZBAR und bieten nichts an. Ein Punkt mit einem einzigen Wert -- die
einzige Einstellung, die es gibt, oder eine Regel, die als Wert geschrieben
ist, wie die Zellenzahl von `hobbywing-flyfun-hv-9item`, "N Pieptöne = N
Zellen" -- bietet nichts zur Wahl; seine Zeile zeigt NICHTS ZU WÄHLEN. Die Zeile unter der Liste nennt die
Werte des gewählten Punkts und den Standardwert. START erscheint, sobald ein
Wert gewählt ist und der Lauf starten kann; kann er es nicht, sagt die Zeile
neben START, warum.

![Zwei Werte gewählt](img/de/programmer-stick-items.png)

### Die Einschaltstellung

Ein Lauf schaltet den ESC mit dem Knüppel dort ein, von wo das Handbuch den
Wert programmiert: beim `entry_throttle` des Werts, wo das Profil eines
nennt, sonst beim Einstieg des Profils. Ein Kontronik-Car-Modus wird mit
dem Knüppel in der Motor-Aus-Stellung in der Mitte programmiert, der
Neutralstellung, die der Modus lernt; der Lauf schaltet ihn bei MID ein.
Der Knüppel bewegt sich nur bei ausgeschaltetem Netzteil: vor dem ersten
Einschalten, und zwischen zwei Einschaltungen erst, wenn das Netzteil
selbst seinen Ausgang aus meldet und der Strom unten ist
(`ESC_STICK_OFF_MA`, `ESC_STICK_OFF_SETTLE_MS`). Er bleibt dort 1000 ms
(`ESC_STICK_SIGNAL_MS`), bevor das Netzteil einschaltet, wie bei jedem
Einstieg.

- Ein einstufiges Menü nimmt eine Änderung je Einschalten, und jedes
  Einschalten nimmt die Stellung des Werts, den es speichern wird.
- Ein zweistufiges Menü, oder eines mit mehreren Änderungen je Einschalten,
  macht seine Änderungen in der Reihenfolge, in der der ESC sie ausgibt;
  alle Änderungen eines Laufs teilen sich deshalb ein Einschalten. Ein Lauf,
  dessen Änderungen verschiedene Stellungen brauchen, wird abgelehnt, mit
  `Änderungen brauchen verschiedene Einschaltstellungen`, und einer, dessen
  Änderungen verschiedene Einstiegszeiten brauchen -- was der Lauf wartet:
  das `entry_hold_ms` des Werts oder das des Einstiegs, und nicht kürzer
  als das längste Halten `at_power_up` --, mit
  `Änderungen brauchen verschiedene Einstiegszeiten`; keiner wird
  umsortiert. Kein Profil im Satz hat eine solche Mischung.
- Wo das Profil keine Ruhestellung nennt, ruht der Knüppel in der
  Einschaltstellung, und die Auswahlbewegung muss sich von ihr
  unterscheiden.

Die Warnung und die Karte des Laufs zeigen EINSCHALTEN BEI und die
Stellung, wenn sie nicht MIN ist; die Warnung jede Stellung, die die
Einschaltungen des Laufs nehmen.

START öffnet eine Warnung über den ganzen Bildschirm mit dem Titel KEIN
PROPELLER, MOTOR GESICHERT?. Sie sagt, was der Lauf tut: er versorgt den
ESC aus dem Netzteil und fährt Throttle auf MAX und zurück, um durch das
Programmiermenü zu schalten. In seltenen Fällen kann ein Motor anlaufen und
drehen, deshalb muss ein angeschlossener Motor fest montiert sein und darf
keinen Propeller tragen; ein Lastwiderstand anstelle des Motors geht
ebenso. Die Pieptöne werden aus dem Strom gezählt. Darunter stehen das
Profil, die Sollwerte des Netzteils mit der Zahl der Änderungen und die
Zeile, dass das Profil ungeprüft und jede Pieptonzeit ein Vorgabewert ist.

HALTEN ZUM STARTEN startet den
Lauf nach 2 s Halten, wie ARM. Ein Finger, der den Knopf verlässt, ein
verlorenes Touch-Ereignis oder ein STOP während des Haltens bricht es ab;
ABBRECHEN schließt die Warnung. Das ARM, das das Halten anfordert, verlässt den
Bildschirm mit den Befehlen des nächsten Frames; ein STOP oder ein
verlorenes Touch-Ereignis davor nimmt es zurück und beendet den Lauf, damit
es keinen Stopp aufheben kann, der danach kam.

![Die Warnung](img/de/programmer-stick-warning.png)

Während des Laufs zeigt die Seite die Phase, die Pieptöne der laufenden
Gruppe, die letzte Gruppe und ob sie in der Reihenfolge war, und den Strom
neben seinem Grundwert. ABBRECHEN beendet den Lauf, ebenso STOP im Band und das
Verlassen des Bildschirms. ZURÜCK und TIMING sind nicht verfügbar. Die
Signalsäule neben der Pieptonzahl ist [unten](#die-signalsäule)
beschrieben.

![Ein Lauf zählt Punktgruppen, ein Piepton läuft](img/de/programmer-stick-run.png)

Das Ergebnis bleibt bis OK: welche Auswahlen getroffen wurden, und bei einem
abgebrochenen Lauf der Grund. Bei mehr als fünf Änderungen zählt die letzte
Zeile den Rest.

![Fertig](img/de/programmer-stick-done.png)

![Gestoppt: STOP, Rot aus](img/de/programmer-stick-aborted.png)

![Vom Netzteil beendet: NETZTEIL AUS, Rot an](img/de/programmer-stick-failed.png)

TIMING öffnet die Einstellungen unten. SCHLIESSEN fordert das Speichern an;
gespeichert wird, während der Prüfstand entschärft und der Ausgang des
Netzteils aus ist.

![Die Zeiteinstellungen](img/de/programmer-stick-timing.png)

### Handgriffe

Manche ESCs gehen nur in ihren Programmiermodus, wenn ein Mensch außer Gas
und Versorgung etwas am ESC tut: vor dem Einschalten einen Jumper auf zwei
Kontakte stecken und ihn nach den ersten Tönen abziehen, oder danach einen
Taster am ESC drücken. Jeder Kontronik-ESC im Satz gehört dazu. Das Profil
listet diese Schritte in `manual` ([ESC-Profile](EscProfiles-de.md)), jeden
mit dem Zeitpunkt, an dem er fällig ist:

| Fällig | Der Lauf |
| --- | --- |
| `before_power` | nennt ihn auf der Warnung: HALTEN ZUM STARTEN sagt, dass er erledigt ist. Ab dem zweiten Einschalten eines Laufs hält er vor jedem Einschalten an und fragt erneut |
| `at_power_up` | hält vor jedem Einschalten mit ausgeschaltetem Netzteil an und fragt; ERLEDIGT schaltet das Netzteil ein, und der Lauf zählt das Halten herunter, das das Profil nennt |
| `before_menu` | fragt, sobald der Einstieg seine Zeit hatte, mit versorgtem ESC und dem Knüppel in der Einschaltstellung. Ein Schritt mit `starts_menu` ist die Handlung, die das Menü startet, und der Lauf zählt die Pieptöne ab dem Moment, in dem er fragt (siehe unten); jeder andere wartet auf ERLEDIGT |
| `during_menu` | kann den Zeitpunkt nicht kennen: das Profil läuft nicht, `Handgriff` |
| `before_power_off` | hält nach der letzten Bewegung des Speicherns an, mit versorgtem ESC und dem Knüppel, wo das Speichern ihn ließ, und schaltet das Netzteil erst auf ERLEDIGT aus. Niemand berührt den ESC: der Bediener beobachtet, wie er den Wert bestätigt (Töne, LED) |
| `after_programming` | zeigt ihn auf dem Ergebnis |

Ein Profil mit Handgriffen zeigt im Kopf seiner Punktliste rot MANUELLER
EINGRIFF NÖTIG. Ein Tippen öffnet die Schritte über dem ganzen Bildschirm:
wann jeder fällig ist, was er ist und ob der Lauf danach fragt. Beim ersten
Öffnen des Profils nach einem Start erscheinen sie von selbst; OK schließt
sie.

![Ein Profil mit Handgriffen](img/de/programmer-stick-hand.png)

![Seine Handgriffe](img/de/programmer-stick-hand-info.png)

Die Warnung vor einem Lauf nennt die Schritte vor dem Einschalten und sagt,
wann der Lauf für weitere anhält. Sie nennt sie, und HALTEN ZUM STARTEN
zählt, nur solange das Netzteil aus meldet: ein Messwert, nicht älter als
1000 ms (`ESC_STICK_STALE_MS`), in dem das Netzteil selbst seinen Ausgang
aus meldet, mit dem Strom 200 ms lang bei höchstens 20 mA. Bis dahin sagt
sie, das Netzteil auszuschalten, und ein laufendes Halten endet, sobald das
Netzteil nicht mehr aus meldet. Der Lauf fordert dann selbst das
Ausschalten an, schaltet nichts ein und fragt nach keinem Schritt an einem
stromlosen ESC, bis seine eigenen Messwerte dasselbe sagen; das Ergebnis
und die Schritte nach einem Lauf sagen, den ESC nicht zu berühren, solange
das Netzteil nicht aus meldet.

![Die Warnung mit einem Schritt vor dem Einschalten](img/de/programmer-stick-hand-warning.png)

Schritte, die nicht alle ganz auf die Warnung passen -- jeder auf zwei
Zeilen umbrochen, über HALTEN ZUM STARTEN --, erscheinen nicht
abgeschnitten: die Warnung zählt sie und bietet ALLE HANDGRIFFE neben
HALTEN ZUM STARTEN an. HALTEN ZUM STARTEN zählt erst, wenn ALLE HANDGRIFFE
über dieser Warnung geöffnet und mit OK geschlossen wurde; das Halten
bestätigt so jeden Schritt, und eine neue Warnung fragt wieder danach.

![Vier Schritte vor dem Einschalten, unter ALLE HANDGRIFFE zu lesen (ein Beispielprofil)](img/de/programmer-stick-hand-steps.png)

Ein Schritt, nach dem der Lauf fragt, deckt die Seite ab: der Schritt, wo
Netzteil und Knüppel stehen und wie lange der Lauf wartet. ERLEDIGT geht
weiter; es ist die ersten 1000 ms dunkel (`ESC_STICK_HAND_MIN_MS`), damit
ein Tippen für den Schritt davor nicht den nächsten bestätigt, und es wirkt
im nächsten Frame, nachdem dieser STOP, das Scharfschalten, den Link und
das Netzteil beurteilt hat. ABBRECHEN beendet den Lauf, ebenso STOP im Band.
Kein ERLEDIGT innerhalb von 60 s (`ESC_STICK_HAND_WAIT_MS`) beendet den
Lauf mit NICHT BESTÄTIGT. Während er wartet, hält der Lauf jede Regel, die er
sonst hält: ein versorgtes Warten beobachtet das Netzteil und seine
Messwerte, und jedes Ende setzt Throttle auf MIN, schaltet das Netzteil aus
und entschärft.

![Der Lauf wartet auf den Jumper](img/de/programmer-stick-hand-prompt.png)

**Bevor das Netzteil ausschaltet.** Ein Kontronik-ESC bestätigt einen
gespeicherten Modus, indem er ihn als Töne wiederholt (die Kontrollausgabe
der Handbücher), Modus 7 als sieben, und die Handbücher stecken den Akku
erst danach ab. SPEICHERN (vorgegeben 2000 ms) kann vor einer langen
Wiederholung enden. Jedes Kontronik-Profil, das läuft, und die fünf
unten tragen einen Schritt `before_power_off`: nach der letzten Bewegung
des Speicherns hält der Lauf den ESC versorgt, den Knüppel, wo das
Speichern ihn ließ, und bittet den Bediener, den ESC zu beobachten und
ERLEDIGT zu tippen, sobald die Bestätigung zu Ende ist; ERLEDIGT schaltet
das Netzteil aus, und der Lauf endet oder schaltet aus und ein wie jeder
andere. Die Abfrage sagt, dass der ESC versorgt ist und nicht berührt
werden darf. Kein ERLEDIGT innerhalb von 60 s beendet den Lauf mit NICHT
BESTÄTIGT, ebenso jedes andere Ende, während der Schritt gefragt ist;
jedes davon schaltet das Netzteil während der Bestätigung aus, und das
Ergebnis sagt, dass der Modus womöglich nicht gespeichert ist.

Ein KOBY, JIVE Pro, KOLIBRI, KONTROL-X / KOLIBRI-X oder KOSMIK, der vor
dem Ende dieser Bestätigung seine Versorgung verliert, wertet die
Programmierung als abgebrochen und sperrt sich: 8-fach Blinken an einem
KONTROL-X (Kontronik_Kontrol-X_Kolibri-X.pdf S. 4, S. 11), 9-fach an KOBY,
JIVE Pro oder KOLIBRI, 10-fach an KOSMIK. Ihr Schritt kennzeichnet das
(`"locks": true`), und dort sagen Abfrage und Ergebnis, dass der ESC
gesperrt sein kann und geprüft werden muss. Von den fünf läuft
KONTROL-X; die anderen zeigen den Schritt in ihrer Liste der Handgriffe.
Die übrigen Kontronik-Handbücher nennen keine Sperre für ein Ausschalten
während der Wiederholung.

![Der Lauf wartet auf die Bestätigung des ESCs](img/de/programmer-stick-hand-end.png)

![Kein ERLEDIGT: der ESC kann gesperrt sein](img/de/programmer-stick-hand-locked.png)

**Die Handlung, die das Menü startet.** Ein Profil kennzeichnet den
Schritt, dessen Handlung die Modusfolge startet, mit `"starts_menu": true`;
ein Schritt ohne das wartet auf ERLEDIGT, wo er auch steht. 16
Kontronik-Profile kennzeichnen ihren letzten Schritt `before_menu`: der ESC
beantwortet den
abgezogenen Jumper oder den gedrückten Taster mit einem Dreiklang und gibt
danach Modus 1, 2, 3 ... aus (zum Beispiel Kontronik_Jazz.pdf S. 6,
"Jumper abziehen", gefolgt von der Tonfolge und der Folge;
Kontronik_Pix1000_3000.pdf S. 4 Schritte 5 und 6, "Taster drücken",
gefolgt von den absteigenden Tönen und der Folge; KOSMIK S. 8 Schritt 6).
CYBER-Line und HELI-Line, deren Jumper beim Anstecken des Akkus abgezogen
wird und die etwa 5 s auf ein Signal warten, kennzeichnen ihn nicht. Die
Hand des Bedieners ist dann am ESC, nicht am Bildschirm. Deshalb zählt
der Lauf die Pieptöne ab dem Moment, in dem er nach diesem Schritt fragt,
mit dem Knüppel dort, wo das Einschalten ihn ließ:

- Der Schritt gilt als erledigt mit der ersten Gruppe, die die
  Reihenfolgeregel in Folge mit der davor findet -- das Menü läuft --,
  oder mit ERLEDIGT, was zuerst kommt. Der Dreiklang des ESC ist eine
  Gruppe von 3 und nicht in Folge mit Modus 1, er zählt also nicht.
- Die Regeln der ruhigen Leitung und der Reihenfolge gelten ab der Frage:
  keine Gruppe zählt vor GROUP GAP Ruhe, und auf einen Wert wird erst mit
  der dritten Gruppe in Folge reagiert. Auf eine angeschnittene erste
  Gruppe wird nie reagiert.
- SILENCE und ZEITLIMIT laufen ab dem Moment, in dem der Schritt erledigt
  ist; bis dahin schweigt der ESC planmäßig. Kein erledigter Schritt
  innerhalb von 60 s beendet den Lauf mit NICHT BESTÄTIGT.
- Der Knüppel bewegt sich nicht, solange nach dem Schritt gefragt wird. Ein
  Profil, dessen Menü in einer anderen Stellung als der Einschaltstellung
  ruht, würde ihn bewegen; dort fragt der Lauf nach ERLEDIGT und hört erst
  danach zu, wie bei jedem früheren Schritt.

Um einen Schritt an einem versorgten ESC wird nur mit dem Knüppel in der
Motor-Aus-Stellung gebeten: MIN, oder MID, wo das `entry_throttle` eines
Werts sie nennt, die Motor-Aus-Stellung des Handbuchs in der Mitte. Ein
Profil, dessen Schritt `at_power_up` oder `before_menu` mit einem Einstieg
auf MID oder MAX kommt, läuft nicht, `Handgriff`, und ein Wert, den es bei
MAX einschalten würde, wird ebenso abgelehnt. Die 60 s sind die Zeit des
Bedieners, den ESC zu erreichen, nicht die des ESCs. Wie lange ein ESC auf
seinen Einstiegs-Jumper oder -Taster wartet, nennen nur die Handbücher von
HELI JIVE und JIVE Pro: 10 s nach dem Einschalten, und keines der beiden
Profile läuft. Spätere Schritte eines Ablaufs haben eigene Fenster, und
auch von diesen Profilen läuft keines: OPTO, BEC und OPTOMAX nehmen den
Starttaster innerhalb von ca. 5 s nach dem Doppel-Signal
(Kontronik_Opto.pdf S. 3, Kontronik_BEC.pdf, Kontronik_Optomax.pdf), 3P
den Jumper abgezogen und wieder gesteckt innerhalb von ca. 5 s nach dem
Ende der zweiten Phase (Kontronik_3P_de.pdf), und CYBER-Line den Jumper
wieder gesteckt während ca. 30 s Signalen.

Das Ergebnis eines Laufs nennt die Schritte `after_programming` des
Profils in zwei Zeilen unter den Änderungen; brauchen sie mehr, sagt es
stattdessen, wie viele es sind. Endet ein Lauf eines Profils mit solchen
Schritten, öffnen sich seine Schritte von selbst über dem Ergebnis, alle,
und MANUELLER EINGRIFF NÖTIG im Kopf des Ergebnisses öffnet sie wieder.

![Vier Handgriffe nach dem Programmieren, über dem Ergebnis (ein Beispielprofil)](img/de/programmer-stick-hand-after.png)

Nach einem abgebrochenen Lauf eines Profils mit einem Schritt vor
oder beim Einschalten sagt es, den ESC zu prüfen: ein für den Lauf
gesteckter Jumper kann noch stecken.

24 Profile haben Handgriffe: die 22 Kontronik-Familien, `turnigy-aquastar`
und `greatplanes-electrifly-c-series`. 10 laufen, jedes auch mit seinem
Schritt, bevor das Netzteil ausschaltet:

| Profil | Schritte | Werte, bei MID eingeschaltet |
| --- | --- | --- |
| `kontronik-3sl` | Jumper vor dem Einschalten auf, nach 2 s oder dem Dreiklang ab | Modus 6 |
| `kontronik-beat` | Jumper auf 2 beliebige der 3 Kontakte, ab nach 2 s oder den Tönen | Modi 6, 8 |
| `kontronik-beat-car` | wie BEAT | Modi 2 bis 6, 8 |
| `kontronik-beat-fai` | wie BEAT | keine |
| `kontronik-jazz` | JAZZ: Jumper wie BEAT; MINIJAZZ: Taster nach 2 s oder den Tönen | Modi 6, 8 |
| `kontronik-kontrol-x` | Taster unter dem Schrumpfschlauch nach 2 s oder den Tönen; der Schritt vor dem Ausschalten kennzeichnet die Sperre | Modus 3 |
| `kontronik-pix` | Taster mit der Aufschrift Taster nach 2 s oder den Tönen | keine |
| `kontronik-smile` | Taster drücken und loslassen nach 2 s oder den Tönen | Modus 6 |
| `kontronik-star-line` | Jumper vor dem Einschalten auf, nach 5 s oder den Tönen ab | Modus 6 |
| `kontronik-sun-plus` | Taster nach den Tönen: 2 s, 5 s bei Modi 4 bis 6 | Modus 6 |

Die anderen 14 zeigen ihre Schritte und nennen den Grund in ihrer Zeile:
`Handgriff` bei `kontronik-3p`, `kontronik-cyber-line`,
`kontronik-heli-line`, `kontronik-mini20` und `kontronik-optomax`, deren
Jumper oder Brücke während des Menüs bewegt wird, und bei
`turnigy-aquastar`, dessen Schalter bei Vollgas eingeschaltet wird; bei den
übrigen der Grund ihres Menüs.

### Die Signalsäule

Eine Signalsäule rechts auf der Karte des Laufs und des Ergebnisses zeigt
zwei Dinge, gezeichnet wie eine Signalsäule an einer Maschine: Rot über
Grün auf einem hellgrauen Fuß.

- **Grün** leuchtet, solange der Pieptondetektor einen Puls hält: vom
  Messwert, der über Grundwert plus SCHWELLE stieg, bis zu dem, der unter
  Grundwert plus SCHWELLE minus HYSTERESE fiel. Jeder Puls schaltet es ein, ob auf
  seine Gruppe später reagiert wird oder nicht. Ein Puls, kürzer als ein
  Frame, ist trotzdem zu sehen: Grün bleibt mindestens 150 ms
  (`ESC_STICK_BEEP_LIGHT_MS`) an, ab dem Frame, der den Puls zuerst sieht.
  Auf dem Ergebnis ist Grün aus.
- **Rot** leuchtet auf einem Ergebnis, dessen Lauf endete, weil etwas nicht
  wie erwartet war, und bleibt bis OK an. Die Gründe stehen unter
  [Wie ein Lauf endet](#wie-ein-lauf-endet). Bei FERTIG und bei den Enden,
  die ein Bediener wählt, bleibt es aus: gedrücktes STOP, ABBRECHEN und das
  Verlassen des Bildschirms. Ein Stopp, den der Prüfstand selbst auslöst,
  PRÜFSTAND GESTOPPT, schaltet es ein.

Ändert sich eine der beiden Leuchten, werden beide Bildpuffer neu
gezeichnet.

## Was ein Lauf tut

| Phase | Gas | Netzteil | Endet |
| --- | --- | --- | --- |
| ARMING | MIN | aus, angefordert aus | wenn der Prüfstand scharf meldet; nach 3000 ms: NICHT ARMED |
| SIGNAL | MIN, dann Einschaltstellung | aus | der Knüppel bleibt auf MIN, bis das Netzteil in Messwerten seit der Ausschaltanforderung des Laufs aus meldet (sein eigener Zustand aus, der Strom 200 ms lang bei höchstens 20 mA), geht dann in die Einschaltstellung und bleibt dort 1000 ms, damit der ESC das Signal beim Start sieht; nicht innerhalb von 3000 ms aus: NETZTEIL BLEIBT EIN. Hat sich der Knüppel bewegt, beendet ein Messwert mit Ausgang an oder Strom oben den Lauf sofort mit NETZTEIL BLEIBT EIN, 1000 ms ohne Messwert mit KEINE MESSWERTE |
| HANDGRIFF | Einschaltstellung | aus, gemeldet aus | vor einem Einschalten mit fälligem Schritt, erst gefragt, wenn das Netzteil aus meldet: ERLEDIGT, dann EINSCHALTEN; kein ERLEDIGT in 60 s: NICHT BESTÄTIGT; ein Messwert mit Ausgang an oder Strom oben: sofort NETZTEIL BLEIBT EIN; 1000 ms kein Messwert: KEINE MESSWERTE |
| EINSCHALTEN | Einstiegsstellung | an | wenn ein Messwert den Ausgang an meldet; nach 3000 ms: AUSGANG NICHT GEMELDET |
| EINSTIEG | Einstiegsstellung | an | EINSTIEG nach dem Einschalten: das `entry_hold_ms` des Werts, sonst das `hold_ms` des Profils, wo es eines nennt, und nicht kürzer als das längste `hold_ms` eines Schritts `at_power_up` |
| HANDGRIFF, VERSORGT | Einschaltstellung | an | nach EINSTIEG mit einem fälligen Schritt `before_menu`, der nicht der Start des Menüs ist: ERLEDIGT, dann der nächste Schritt oder das Menü; kein ERLEDIGT in 60 s: NICHT BESTÄTIGT. Nach dem letzten Schritt wird aus PUNKTE oder WERTE gefragt, schon zählend |
| WARTEN AUF DEN ESC | wo es speicherte | an | nach SPEICHERN mit einem fälligen Schritt `before_power_off`: ERLEDIGT, dann der nächste solche Schritt oder das Netzteil aus (AUS UND EIN oder AUSSCHALTEN); kein ERLEDIGT in 60 s: NICHT BESTÄTIGT, und das Ergebnis sagt, dass der ESC gesperrt sein kann |
| PUNKTE | Ruhestellung | an | eine Punktgruppe in Reihenfolge nennt einen gewünschten Punkt: die Auswahlbewegung |
| WERTE | wo die letzte Bewegung es ließ | an | eine Wertgruppe in Reihenfolge nennt den gewünschten Wert: die Wertbewegung |
| SPEICHERN | die Wertbewegung, dann die Speicherbewegung, dann die eigenen Bewegungen des Werts (`after_select`) | an | nach SPEICHERN, und nach SPEICHERN noch einmal für jede Bewegung: die Speicherbewegung des Profils, dann jede des Werts |
| AUS UND EIN | wo es speicherte, dann Einstiegsstellung | aus | das Netzteil meldet den Ausgang aus und den Strom 200 ms unten, dann AUSSCHALTZEIT (mindestens 1000 ms) in der Einstiegsstellung, dann wieder EINSCHALTEN; nicht innerhalb von 3000 ms aus: NETZTEIL BLEIBT EIN. Hat sich der Knüppel bewegt, beendet ein Messwert mit Ausgang an oder Strom oben den Lauf sofort mit NETZTEIL BLEIBT EIN, 1000 ms ohne Messwert mit KEINE MESSWERTE; das nächste Einschalten oder ein Schritt davor braucht einen Messwert, nicht älter als 1000 ms |
| AUSSCHALTEN | wo es speicherte | aus | das Netzteil meldet den Ausgang aus und den Strom 200 ms unten; nicht innerhalb von 3000 ms: NETZTEIL BLEIBT EIN |
| FERTIG | MIN | aus | entschärft |
| ABGEBROCHEN | MIN | aus | entschärft, alles in einem Schritt |

MIN, MID und MAX sind 0, 50 und 100 % des Wegs des Throttle-Ausgangs. Die
Ruhestellung ist `scheme.listen` des Profils, oder die Einstiegsstellung, wo
es keines hat. Ein zweistufiges Menü (`value_select` gesetzt) wählt den
Punkt mit `select` und speichert den Wert mit `value_select`, dann zählt es
wieder Punkte; ein einstufiges Menü zählt Werte und speichert mit `select`,
gefolgt von der Bewegung `scheme.store` des Profils, wo es eine nennt. Ein
Profil mit `"changes_per_entry": "one"` nimmt eine Änderung je Einschalten,
also schaltet ein Lauf mit mehreren Änderungen das Netzteil zwischen ihnen
für AUSSCHALTZEIT aus.

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
  Lauf mit ABGEBROCHEN und NETZTEIL BLEIBT EIN, und ein Lauf mit einer Änderung je
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

- **Grundwert.** Während EINSTIEG, ab 500 ms nach dem Einschalten, ist der
  Grundwert der niedrigste gelesene Strom. Ein Piepton kann ihn nicht
  anheben. Während das Menü gezählt wird, folgt der Grundwert ruhigen
  Messwerten um 1/16 des Unterschieds je Messwert.
- **Piepton.** Ein Messwert über Grundwert plus SCHWELLE beginnt einen
  Puls; einer unter Grundwert plus SCHWELLE minus HYSTERESE beendet ihn.
- **Längen in Messwerten.** Ein Puls mit weniger Messwerten, als PIEPTON MIN
  beim längsten gesehenen Abstand halten muss, oder eine Lücke zwischen zwei
  Pulsen mit weniger, als PAUSE MIN halten muss, verdirbt seine Gruppe. Ein
  Puls, dessen erster und letzter Messwert weiter als LANG MAX auseinander
  liegen, verdirbt seine Gruppe. Ein Puls ab LANG ist ein langer Piepton; in
  einem Profil ohne lange Pieptöne verdirbt er seine Gruppe, ebenso ein
  langer nach einem kurzen.
- **Gruppe.** Stille von GRUPPENPAUSE nach dem letzten Puls beendet eine
  Gruppe. Ihre Zahl sind die kurzen Pieptöne plus `long_equals_short` für
  jeden langen.
- **Erst Stille.** Wenn eine Phase zu hören beginnt, wird keine Gruppe
  gezählt, bevor GRUPPENPAUSE Stille gehört ist; die erste Gruppe ist also
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
- **Späte Messwerte.** Ein Messwert, der mehr als das Kürzere von PIEPTON MIN
  und PAUSE MIN nach dem vorigen kommt, ist spät, ebenso einer, dessen Zähler
  einen übersprungenen Messwert zeigt: er verdirbt die Gruppe, in die er
  fällt, und bricht die Reihenfolge. 3 späte Messwerte hintereinander
  beenden den Lauf mit MESSRATE.

## Die Einstellungen

TIMING auf der Seite eines Profils. Gespeichert mit den anderen
Einstellungen. Kein Wert hier ist gemessen.

| Einstellung | Vorgabe | Bereich | Was sie ist |
| --- | --- | --- | --- |
| Spannung | 0 V | 0 bis 20 V | die Spannung des Netzteils; 0 nimmt sie aus dem Profil |
| Strombegrenzung | 1,00 A | 0,10 bis 3,00 A | die Strombegrenzung des Netzteils während des Laufs |
| Piepton min | 200 ms | 20 bis 2000 ms | kürzester Piepton, den der ESC gibt |
| Pause min | 200 ms | 20 bis 2000 ms | kürzeste Stille zwischen zwei Pieptönen |
| Lang | 500 ms | 50 bis 5000 ms | ein Piepton ab dieser Länge ist ein langer |
| Lang max | 1500 ms | 100 bis 10000 ms | ein längerer Puls ist kein Piepton |
| Gruppenpause | 700 ms | 50 bis 10000 ms | Stille, die eine Gruppe beendet |
| Einstieg | 5000 ms | 1000 bis 60000 ms | Einschalten bis zum Menü, wo das Profil kein `hold_ms` nennt |
| Speichern | 2000 ms | 0 bis 10000 ms | gehalten an der letzten Auswahl vor dem Ausschalten |
| Ausschaltzeit | 3000 ms | 500 bis 20000 ms | Netzteil aus zwischen zwei Einstiegen |
| Stille | 10000 ms | 1000 bis 60000 ms | so lange kein Piepton beendet den Lauf |
| Zeitlimit | 180000 ms | 5000 bis 600000 ms | so lange keine Reaktion auf eine gewünschte Gruppe beendet den Lauf |
| Schwelle | 100 mA | 10 bis 2000 mA | über dem Grundwert: ein Piepton |
| Hysterese | 40 mA | 0 bis 1000 mA | unter der Schwelle minus diesem Wert: Stille |

Ein Lauf wird abgelehnt, nicht angepasst, wenn die Einstellungen sich
widersprechen: LANG höchstens PIEPTON MIN, LANG MAX höchstens LANG, GRUPPENPAUSE
höchstens PAUSE MIN, SCHWELLE höchstens HYSTERESE, EINSTIEG höchstens 500 ms,
oder GRUPPENPAUSE plus das Kürzere von PIEPTON MIN und PAUSE MIN mindestens das
`within_ms` des Profils für seine Auswahl- oder Wertbewegung.

## Das Netzteil

SPANNUNG 0 nimmt das `cells_min` des aus der Liste geöffneten Modells, wo
es eines nennt, sonst das niedrigste `cells_min` unter den Modellen des
Profils,
mit 3,8 V je LiPo-Zelle (Lithium-Polymer) oder 1,2 V je NiMH-Zelle
(Nickel-Metallhydrid): über der Abschaltung dieses Modells und unter dem
Maximum jedes Modells. Ein Profil, dessen Modelle keine Zellenzahl nennen,
braucht SPANNUNG. Eine Spannung über der Grenze des Bildschirms SUPPLY oder
unter dem Minimum des Netzteils, und eine Strombegrenzung über der Grenze
von SUPPLY, werden abgelehnt. Die Sollwerte des Laufs werden die Sollwerte
des Bildschirms SUPPLY.

## Welche Profile laufen

24 der 72 Profile sind von einer Art, die der Ablauf ausführt: 13
zweistufige und 11 einstufige. Mit den 20 V des PD mini und den
vorgegebenen Grenzen öffnet die Liste 23 davon:
hobbywing-skywalker-v2-hv-opto braucht 22,8 V. Auch 4 Modellzeilen von
Familien, die sich öffnen, werden bei 20 V abgelehnt, jede mit 22,8 V:
FLYFUN 130A und 160A HV OPTO V5 sowie Gecko 120A und 150A OPTO HV. Ein
Profil läuft, wenn es
`"automatable": "full"` ist, oder `"assisted"` mit Handgriffen, auf die der
Lauf warten kann (siehe [Handgriffe](#handgriffe)), vor dem Einschalten
betreten wird, mit `count`
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
| braucht eine Person am ESC | `automatable` ist `assisted`, und das Profil nennt keinen Handgriff, nach dem der Lauf fragen könnte: ein Mensch liest eine LED oder steckt eine JetiBox an |
| Handgriff | ein Handgriff während des Menüs, oder einer an einem versorgten ESC mit dem Einstieg auf MID oder MAX |
| kein nutzbares Verfahren | `automatable` ist `none` |
| Einstieg nach Einschalten | `scheme.entry.when` ist `after_power_on` |
| Melodie-Menü, Ja/Nein-Menü, Stickpositions-Menü, Menü eigener Art | `scheme.type` |
| Töne nach Tonhöhe getrennt | `scheme.announce.encoding` ist `melody` oder `yes_no` |
| Punkt und Wert: 1 Bewegung | `item_then_value` ohne `value_select`: welche Gruppe die Bewegung beantwortet, steht nicht fest |
| 2 Bewegungen, ohne Werttöne | `value_select` ohne `item_then_value` |
| viele Änderungen, einstufig | einstufig mit `"changes_per_entry": "many"` |
| Punkte gezählt, einstufig | einstufig, `item` angesagt, mehrere Punkte |
| Werte wiederholen sich | einstufig, eine Wertnummer in zwei Punkten |
| Auswahl = Ruhestellung | die Auswahlbewegung ist die Ruhestellung: keine Bewegung zu machen |
| Wertbewegung = Auswahl | zweistufig, `value_select` gleich `select` |
| Ruhe ohne Einstiegszeit | `scheme.listen` weicht von der Einschaltstellung ab und keine Zeit ist genannt: `hold_ms` ist null, der Wert hat kein `entry_hold_ms` und kein Schritt `at_power_up` hat ein `hold_ms`: die YGE-Profile |
| Speichern, zweistufig | zweistufig mit `scheme.store` |
| Speichern = Auswahl | einstufig, `scheme.store` gleich `select` |
| 22.8 V, Obergrenze 21.0 V | die Spannung (SPANNUNG, oder die Zellenzahl des Profils) liegt über der Grenze von SUPPLY |

Ein auf der SD-Karte korrigiertes Profil steht mit seiner Korrektur in der
Liste.

## Wie ein Lauf endet

| Ergebnis | Ursache | Rote Leuchte |
| --- | --- | --- |
| FERTIG | jede Auswahl getroffen | aus |
| STOP | STOP gedrückt, im Band oder auf einer Seite, während des Laufs | aus |
| PRÜFSTAND GESTOPPT | ein Stopp, den der Prüfstand während des Laufs selbst auslöste: Touch 500 ms ohne Antwort, Touch-Ereignisse verloren unter einem STOP-Druck oder unter dem Scharfschalten, oder die Ablehnung oder der Failsafe des Koprozessors; das Band nennt, welcher | an |
| DISARMED | der Prüfstand wurde entschärft | an |
| LINK VERLOREN | der Koprozessor antwortete irgendwann während des Laufs und hörte auf | an |
| NETZTEIL AUS | der Ausgang ging aus: eine Abschaltung, oder ein ON, das das Netzteil fallen ließ | an |
| NETZTEIL ANTWORTET NICHT | das Netzteil antwortet nicht mehr | an |
| KEINE MESSWERTE | der Messwertzähler 1000 ms unverändert | an |
| MESSRATE | 3 späte Messwerte hintereinander | an |
| NICHT ARMED | nicht scharf innerhalb von 3000 ms | an |
| AUSGANG NICHT GEMELDET | der Ausgang nicht innerhalb von 3000 ms als an gemeldet | an |
| NETZTEIL BLEIBT EIN | das Netzteil meldet den Ausgang nicht innerhalb von 3000 ms nach der Anforderung des Laufs aus, mit dem Strom unten -- beim Start, vor einem Einschalten oder am Ende --, oder ein Messwert mit Ausgang an oder Strom oben, während nach einem Schritt an einem stromlosen ESC gefragt wird | an |
| TOUCH VERLOREN | Touch-Ereignisse verloren, solange das ARM des Laufs noch nicht genommen oder der Prüfstand noch nicht scharf war | an |
| KEINE PIEPTÖNE | STILLE lang kein Piepton | an |
| STROM BLEIBT HOCH | ein Puls länger als zweimal LANG MAX | an |
| ZEITLIMIT | innerhalb von ZEITLIMIT auf keine gewünschte Gruppe reagiert | an |
| NICHT BESTÄTIGT | kein ERLEDIGT für einen Handgriff innerhalb von 60 s | an |
| ABGEBROCHEN | ABBRECHEN | aus |
| SEITE VERLASSEN | der Bildschirm wurde verlassen | aus |

Die Spalte der roten Leuchte ist `esc_stick_reason_is_fault()` in
`shared/esc/esc_stick.c`, ein Fall je Grund. `tools/check_docs.py` liest
diese Funktion und schlägt fehl, wenn diese Tabelle oder ihr englisches
Gegenstück etwas anderes sagt. TOUCH VERLOREN ist an: Ereignisse gingen
verloren, sie wurden nicht gewählt.

STOP und PRÜFSTAND GESTOPPT kommen aus zwei Zählern, die das Panel führt:
jeder Stopp, und davon die gedrückten (`arming_stop_pressed()`, im selben
Aufruf wie der Stopp gezählt). Ein Lauf, der seit seinem Beginn mehr Stopps
als Drücke sieht, endet mit PRÜFSTAND GESTOPPT, damit ein Stopp des
Prüfstands nicht hinter einem Druck verschwindet, der mit ihm kam.

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
bleibt, sind Messwerte, die ausbleiben: KEINE MESSWERTE nach 1000 ms, wie frisch
die Page selbst auch ist. Ein Piepton oder eine Lücke kürzer als etwa
200 ms kann zwischen zwei Messwerte des Moduls fallen und ungesehen
bleiben; die Reihenfolgeregel übergeht dann die Gruppe, statt die Nummer
darunter zu wählen. PIEPTON MIN und PAUSE MIN stehen deshalb auf 200 ms. Ob der
Strom des Moduls ein Augenblickswert oder ein Mittel über sein Intervall
ist, ist nicht bekannt.

Pieptöne aus der Zeit zu zählen, in der der Strom erhöht bleibt, würde auch
Pieptöne kürzer als ein Messwert erfassen, braucht aber die Periode der
Pieptöne, die nicht gemessen ist. Es wird nicht gemacht.

## Simulation

Ist das PD mini in SETUP ANSCHLÜSSE aus, betreibt der Bildschirm SUPPLY das
Modell eines Netzteils im Panel. Während eines Stick-Laufs ist der Strom
dieses Modells ein simulierter ESC (`shared/esc/esc_sim.c`), der dem Profil
folgt: Einschalten in der Einstiegsstellung betritt das Menü nach dem
`hold_ms` des Profils (3000 ms ohne), Gruppen laufen mit 250 ms Pieptönen,
250 ms Lücken, 800 ms langen Pieptönen und 1500 ms zwischen Gruppen, bei
150 mA Ruhestrom und 600 mA mehr je Piepton. Jede dieser Zahlen ist
ausgedacht. Wo das Profil eine Speicherbewegung nennt, wird eine Auswahl
erst behalten, wenn der Knüppel sie macht. Ein Punkt mit dem Schlüssel
`exit` verlässt das Menü, wenn er gewählt wird, und einer mit `reset` löscht,
was gespeichert war. Die Messwerte des Modells kommen alle 50 ms. Das Modell
des Panels bildet keinen Jumper und keinen Taster nach: es betritt sein
Menü nach `hold_ms`. Das der Host-Suite tut es (`wait_hand`): das Menü
wartet auf die Handlung, beantwortet sie mit drei Pieptönen und beginnt die
Folge 1500 ms später.

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
  Lauf dann als Werte zählt: er kann FERTIG melden, ohne dass etwas
  gespeichert ist.
- Bei einem Menü, das länger oder kürzer als sein Profil ist, wird auf die
  niedrigste Nummer nie reagiert, weil die höchste der Schleife nicht die
  davor gehörte ist; der Lauf endet mit ZEITLIMIT.
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
- Die Handgriffe sind die der Handbücher und unerprobt. Ob ein Kontronik-ESC
  ohne Grenze auf seinen Einstiegs-Jumper oder -Taster wartet und was er
  tut, wenn das Abziehen spät kommt, steht nicht fest, außer bei HELI JIVE
  und JIVE Pro (10 s), die nicht laufen. Die späteren Fenster von OPTO,
  BEC, OPTOMAX, 3P (ca. 5 s) und CYBER-Line (ca. 30 s) gehören zu
  Profilen, die nicht laufen.
- Ein KOBY, JIVE Pro, KOLIBRI, KONTROL-X / KOLIBRI-X oder KOSMIK, dessen
  Netzteil vor dem Ende seiner Modusbestätigung ausschaltet, sperrt sich
  (8- bis 10-fach Blinken, je nach Familie). Der Lauf schaltet das Netzteil
  vor ERLEDIGT aus bei NICHT BESTÄTIGT (kein ERLEDIGT innerhalb von 60 s),
  STOP, ABBRECHEN, einem Entschärfen, einem verlorenen Link und jeder
  Netzteilregel, und sagt dann, dass der ESC gesperrt sein kann. Der
  Prüfstand sieht weder die Bestätigung noch die Sperre; ERLEDIGT ist das
  Wort des Bedieners, dass die Bestätigung zu Ende ist.
- Welche Werte ein Kontronik-ESC aus der Mitte programmiert, ist den
  Handbüchern entnommen. Für JAZZ-Modi 6 und 8, KONTROL-X-Modus 3 und
  SUN-PLUS-Modus 6 nennt das Handbuch die Stellung nicht, und die Werte
  werden bei MID eingeschaltet, als wäre es die Mitte. SUN-PLUS-Modi 2, 3,
  5 und 9 beginnen in der Neutralstellung des Handbuchs, die sein
  englischer Text für Modus 4 hinten verortet ("neutral position (back
  position)", Kontronik_Sun_Plus.pdf S. 12) und die Modus 5 an einem
  Zwei-Stellungs-Schalter einstellt (S. 13); sie werden nach dieser Lesart
  bei MIN eingeschaltet. Die Zusatzmodi (7, 9)
  werden von hinten programmiert; ob sie an einem ESC im Car-Modus den
  Knüppelweg neu speichern, steht nicht fest.
- Bei einer Modusliste mit Lücke wird auf die Nummern nach der Lücke und
  auf die niedrigste nie reagiert, weil die Reihenfolgeregel eine
  übersprungene Nummer als verpasste Gruppe liest: PIX-Modi 7, 9 und 1 (die
  Liste ist 1, 2, 3, 7, 9), Smile-Modi 9 und 1, SUN-PLUS-Modi 9 und 1. Der
  Lauf endet mit ZEITLIMIT. Ob diese ESCs die nicht genutzten Zahlen
  ausgeben, steht nicht fest.
- Die Bewegungen eines Werts nach seiner Auswahl (`after_select`: PIX
  Modus 2, die Kontronik-Car-Modi, KONTROL-X Modus 3) folgen jeweils
  SPEICHERN nach der vorigen. Die Handbücher takten sie mit den
  Antworttönen des ESC, die der Lauf nicht dekodiert; SPEICHERN (vorgegeben
  2000 ms) steht für sie, nicht gemessen. Die wahlfreie eigene
  Motor-Aus-Stellung der Segelflugmodi wird nicht angefahren.
- Ein Schalter am ESC in der Einschaltfolge -- SeaKing V3 mit Schalter,
  Trackstar 60A V2, der BEC-Schalter der Hacker-X- und Master-Reihe, der
  Empfängerschalter des Jeti Spin -- bleibt eingeschaltet, und das Netzteil
  steht für ihn. Die Handbücher schalten ihn nach dem Anstecken des Akkus;
  ob das Einschalten des Netzteils das Menü betritt wie der Schalter, ist
  nicht gemessen.
- Kein Lauf mit einem Motor am ESC anstelle des Lastwiderstands. Ob ein
  Motor während des Menüs anläuft und wie seine Wicklungen die Pieptöne im
  Strom formen, ist nicht gemessen.
