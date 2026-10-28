# Der Programmierport für Servos von KST und Chaservo

<sub>[English](LinkKST.md) · **Deutsch**</sub>

Diese Seite ist ein Abschnitt von [Der Link](Link-de.md), der die Verkabelung, das
Frame-Format, die Page Map und die übrigen Pages enthält. Pins heißen nach ihrer
GPIO-Nummer (General-Purpose Input/Output). PWM steht für
Pulsweitenmodulation.

Die Servo-Programmier-Page (`KST`, 0x32) ist neu in 4.12 und nennt seit 4.13
einen Programmierport. Die
Page fährt das Programmierprotokoll aus
[`protocols/kst`](../protocols/kst/README.md) für KST- und Chaservo-Servos,
die ein Protokoll und einen Registersatz teilen, auf einem Port von
[PROG](Link-de.md#die-programmierports) zur Zeit. Ein Port gibt PWM-Pulse aus oder
fährt das Programmierprotokoll, nie beides. Ob ein KST- oder Chaservo-Servo
im Programmiermodus PWM-Pulsen folgt, ist nicht gemessen. Ein
Standard-Servoport ist kein Programmierport, und ein Kommando, das einen
nennt, wird abgewiesen.

Die Session läuft auf dem IOmcu mit dem Planer und den Grenzen des
Moduls. Das Panel schreibt eine Operation und die Images, die sie braucht.
Es liest den Zustand, das Ergebnis und die 32 Register des Servos. Es sendet
keinen Registerschreibzugriff. `shared/outputs/kst_port.c` hält die Regeln.
Nichts wird im Flash gehalten: nach einem Neustart des IOmcu steht
der Port in PWM ohne Session und mit gelöschtem Bit 4 in Register 0, gleich
in welchem Modus das Servo ist, und die Versorgung ist aus angefordert.

Die Session plant ein WRITE und ein RESTORE selbst (seit 4.14). Der Frame
des Kommandos prüft den Auftrag und bereitet den Job des Planers vor, 64us
Planerarbeit. Ein Ziel, das der Planer ohne Suche abweist, wird dort
abgewiesen (PLAN_RULES, PLAN_R00, PLAN_PAIRING), und keine Operation
startet. Jeder andere Auftrag startet die Operation, und jeder Durchlauf
der Hauptschleife von Kern 0 fährt dann einen Slice des Planers: 100 bis
109 Einheiten Arbeit, höchstens 892us. Solange der Port plant, ist Bit 3
von Register 0 gesetzt, das Low-Byte von `LINK_KS_RESULT` liest 35, der
Frame-Zähler liest 0, und kein Frame geht an das Servo. Das Planen endet
nach höchstens 216 000 Einheiten, 2 161 Durchläufen:

- mit einem Plan: `LINK_KS_RESULT` liest 1, und die Session liest das Servo
  und schreibt;
- ohne eine Reihenfolge von Schreibzugriffen, die jedes Image dazwischen
  gültig hält: Ergebnis 33;
- mit verbrauchten 216 000 Einheiten, bevor eines von beiden feststeht:
  Ergebnis 34. Ob der Auftrag einen Plan hat, ist dann nicht bekannt;
- ein RESTORE, dessen Ergebnis sich nicht prüfen lässt, ohne die
  Bestätigung: Ergebnis 11.

Bei den Ergebnissen 33, 34 und 11 wird nichts an das Servo gesendet, die
Session ist nicht gesperrt, und das nächste WRITE wird ohne READ_ALL
angenommen. Der teuerste gefundene Auftrag braucht 120 136 Einheiten,
höchstens 1 202 Durchläufe und 397 bis 647ms Planerzeit. Die Zeiten sind in
einem Emulator aus den ausgeführten Instruktionen für 150MHz berechnet
([Performance](Performance-de.md#der-iomcu)) und nicht auf Hardware
gemessen. Die Zeit eines Durchlaufs mit dem Rest der Hauptschleife ist
nicht gemessen; die Planzeit an einem Prüfstand ist also nicht bekannt. Ein
4.13-IOmcu fährt die ganze Suche im Frame des Kommandos und
beantwortet einen Auftrag ohne Reihenfolge von Schreibzugriffen mit der
Abweisung PLAN_NO_PATH.

Der Port, die Page und das Modul des Panels laufen in der Host-Suite gegen ein Servo-, Leitungs- und
Versorgungsmodell.

| Zustand | Wert | Der Pin des Ports | Die Versorgung | Endet |
| --- | ---: | --- | --- | --- |
| PWM | 0 | der PWM-Generator | die des Panels, auf SUPPLY | wenn ENTER oder REBOOT angenommen ist |
| STOPPING | 1 | der PWM-Generator, ohne Puls: ein laufender Puls endet | unverändert | nach 50ms |
| LOW | 2 | der Leitungstreiber, auf Low getrieben | für das Programmieren gesetzt, siehe unten | nach 100ms und wenn die Versorgung wie angefordert liest |
| PROGRAMMING | 3 | der Leitungstreiber: Low, ein Frame oder Eingang für ein Antwortfenster | an, vom Port gehalten | mit POWER_CYCLED, REBOOT oder LEAVE, und wenn die Versorgung ausgeht; vor dem ersten Frame auch mit ABORT, einem Stopp oder einem fehlgeschlagenen Einstieg |
| RAIL_OFF | 4 | der Leitungstreiber, oder nach einem REBOOT in PWM der PWM-Generator ohne Puls | aus | 1000ms nachdem die Versorgung aus liest |
| RAIL_WAIT | 5 | wie RAIL_OFF | an, mit den Sollwerten, die sie hatte | 1000ms nachdem die Versorgung an liest, nach PWM; nach einem REBOOT mit `CMD`-Bit 5 und einer Wartezeit über 0 nach LEAVING (seit 4.30) |
| LEAVING | 6 | der Leitungstreiber, auf Low getrieben | wie sie ist: nicht geschaltet, kein Sollwert geschrieben | nach der Wartezeit aus Schreibregister 28, nach PWM (seit 4.15) |

Die Einstiegssequenz der Session hält die Leitung nach LOW weitere 100ms
auf Low. Ab LOW hat der Generator den Pin nicht. Jede Quelle eines
PWM-Kommandos (das Panel, der Sweep, der automatische Test) endet am
Bankkanal des Ports, keine erreicht also den Pin; PROG und SERVO nehmen ein
solches Kommando weiterhin an. Das `PWM`-Bit des Ports und `TARGET` bleiben
über eine Session wie geschrieben, und der Puls ist zurück, wenn der Port
wieder in PWM steht. Der IOmcu schreibt kein Flash, solange der Port
nicht in PWM steht.

## Die Versorgung

Das Servo des Ports wird vom PD mini von SUPPLY (0x2A) versorgt. Bit 10 von
Register 0 liest 1, solange ein PD mini antwortet; der Port schaltet ihn
dann.

ENTER nimmt die Programmierspannung und die Stromgrenze aus den
Schreibregistern 26 und 27. Antwortet ein PD mini, wird ENTER mit
SUPPLY_RANGE abgewiesen, außer die Spannung ist höchstens 8400mV, der Strom
50 bis 3000mA und die Spannung der Session mindestens 700mV unter der Eingangsspannung des PD
mini. 8400mV ist die höchste Servoschiene des IO-Boards. Die Spannung der
Session ist die bereitgestellte oder die niedrigere von ihr und der
Spannung eines Ausgangs, der an ist; unter 4800mV wird ENTER mit
SUPPLY_LOW abgewiesen (seit 4.15), und die Versorgung bleibt unberührt.
Das Panel hält dieselbe Untergrenze auf seiner Seite: es sendet kein ENTER,
solange es einen Ausgang des PD mini liest, der an ist und unter 4800mV
gesetzt ist, was auch immer der IOmcu mit diesem ENTER tut.
4800mV ist `KST_PORT_SUPPLY_MIN_MV`; die
Versorgungsspannung, die ein Servo zum Speichern eines Registers braucht,
ist nicht gemessen. In LOW tut der Port dann eines von zwei Dingen:

- der Ausgang ist aus angefordert: die Sollwerte werden geschrieben und der
  Ausgang wird eingeschaltet. Die Session beginnt 1000ms nachdem der PD
  mini an liest;
- der Ausgang ist an: die Spannung wird auf die niedrigere der zwei
  Spannungen gestellt und die Stromgrenze auf die niedrigere der zwei
  Grenzen (die Grenze seit 4.15). Nichts wird geschrieben, wo beide schon
  die des Ausgangs sind.

Kein Schritt des Ports hebt die Spannung oder die Stromgrenze eines
Ausgangs, der an ist, und das Verlassen des Programmierens stellt keinen
höheren Wert wieder her, den ENTER gesenkt hat. Ein PD mini, der nicht innerhalb von 3000ms wie
angefordert liest, beendet ENTER mit Ergebnis 32: der Port steht in PWM, und
ein Ausgang, den dieses ENTER eingeschaltet hat, ist wieder aus.

POWER_CYCLED und REBOOT in PROGRAMMING, bei an angefordertem PD mini,
schalten die Versorgung aus und ein: aus, RAIL_OFF für 1000ms ab der
Lesung, die sie aus zeigt, an mit den Sollwerten, die sie hatte, RAIL_WAIT
für 1000ms ab der Lesung, die sie an zeigt, dann PWM mit gelöschtem Bit 4.
Ein REBOOT mit `CMD`-Bit 5 wartet vor PWM in LEAVING (seit 4.30,
[die Wartezeit nach einem Versorgungszyklus](#die-wartezeit-nach-einem-versorgungszyklus)).
REBOOT wird auch in PWM angenommen, für ein Servo, das neu starten soll:
derselbe Ablauf, ohne Puls am Port währenddessen. Beide Zeiten, je
1000ms, sind die Werte des Eigentümers. Ein Chaservo verließ den
Programmiermodus nach 4000ms und mehr ohne Spannung und trat aus einem
Einschalten 1000ms vor dem Sync in ihn ein (Prüfstand, 2026-10-16). Die
kürzeste Zeit ohne Spannung, die Zeit, die ein Servo zum Starten braucht,
und die Versorgungsspannung, die es im Programmiermodus annimmt, sind nicht
gemessen. Ebenso wenig die Zeit, die der PD mini zum Schalten braucht. Das
Panel nutzt REBOOT für VERBINDEN bei eingeschaltetem Ausgang des PD mini und
für VERLASSEN nach einem Schreiben
([ServoProgrammer-de.md](ServoProgrammer-de.md#der-versorgungszyklus)).

Antwortet kein PD mini, fasst der Port keine Versorgung an. Die Session
läuft, Bit 10 liest 0, POWER_CYCLED ist das Wort des Bedieners, dass das
Servo ohne Spannung war, und bringt den Port sofort nach PWM, und REBOOT
wird mit NO_SUPPLY abgewiesen. REBOOT wird ebenso abgewiesen, solange der PD
mini aus angefordert ist.

Solange der Port nicht in PWM steht, ist Bit 11 der Flags von SUPPLY gesetzt
(seit 4.13), und SUPPLY weist jedes Schreiben seiner Register 4 bis 6, das
kein OFF ist, mit BAD_VALUE ab: ein ON eines Ausgangs, der aus ist, und jede
Änderung der Spannung oder der Stromgrenze, nach oben oder unten (seit 4.15;
4.13 und 4.14 weisen nur ein ON und ein Heben der Spannung ab). Ein
Schreiben mit OUTPUT 0 wird als OFF angenommen, welche Sollwerte auch
mitkommen, und die Seite behält die Sollwerte, die sie hatte. Ein Schreiben,
das einen Ausgang, der an ist, mit den geltenden Sollwerten wiederholt, wird
angenommen. Der Port schaltet den Ausgang nicht ein, solange eine eben
geschriebene Verdrahtung der Versorgung noch nicht im Flash ist.

Ein OFF, das an SUPPLY geschrieben wird, oder ein verlorener Heartbeat,
während eine Operation des Ports läuft, liest in OUTPUT sofort 0 und
erreicht den PD mini, wenn der Port seinen Frame abgebrochen hat und keine
Operation läuft, spätestens 12ms danach (`SUPPLY_PAGE_OFF_WAIT_MS`, seit
4.15). Der Port sieht OUTPUT 0 in seinem nächsten Durchlauf und beendet die
Operation. Ein Ausgang, den der PD mini selbst abschaltet, wird nicht
verzögert. Die Zeit, die der Ausgang des PD mini nach dem Kommando zum
Abfallen braucht, ist nicht gemessen.

Der IOmcu schaltet keine Ausgangsbank auf ARMED, solange ein
Programmierport nicht in PWM steht: das Register ARM wird angenommen, und
die Bank bleibt SAFE, bis der Port wieder in PWM steht. ENTER wird mit
BUSY abgewiesen, solange die Bank ARMED ist (beides seit 4.15).

Wo die Versorgung endet, wenn etwas ausfällt:

| Ereignis | Die Versorgung | Der Port |
| --- | --- | --- |
| Der PD mini antwortet bei ENTER nicht | unberührt | die Session läuft wie an einem Prüfstand ohne einen |
| Der PD mini hört auf zu antworten oder liest in LOW nicht innerhalb von 3000ms wie angefordert | aus angefordert, wenn dieses ENTER sie eingeschaltet hat, sonst an gelassen, auf der niedrigeren Spannung, wenn dieses ENTER sie gesenkt hat | PWM, Ergebnis 32 |
| Die bereitgestellte Spannung ist außerhalb des Bereichs | unberührt | ENTER mit SUPPLY_RANGE abgewiesen |
| Die Spannung der Session liegt unter 4800mV | unberührt | ENTER mit SUPPLY_LOW abgewiesen |
| Die Versorgung liest aus oder ist aus angefordert in PROGRAMMING (das Panel, ein verlorener Heartbeat, der eigene Schutz des PD mini) | 1000ms aus gehalten, nicht wieder eingeschaltet | die Operation endet, ein Frame auf der Leitung wird beendet; PWM mit gelöschtem Bit 4, Ergebnis 19 |
| Die Versorgung liest nicht innerhalb von 3000ms nach Beginn eines Aus-Ein-Ablaufs aus | aus angefordert | aus PROGRAMMING: wieder PROGRAMMING, Bits 4 und 5 bleiben, Ergebnis 32. Aus PWM: PWM, Ergebnis 32 |
| Der Heartbeat ist verloren oder der Link still in RAIL_OFF | bleibt aus | PWM mit gelöschtem Bit 4, Ergebnis 19 |
| Die Versorgung liest in RAIL_WAIT nicht innerhalb von 3000ms an | aus angefordert | PWM, Ergebnis 32 |
| Die Versorgung ist in RAIL_WAIT aus angefordert (das Panel, ein verlorener Heartbeat) | aus | PWM, Ergebnis 19 |

Die Versorgung endet aus, wann immer kein Panel da ist, das sie ausschalten
könnte: SUPPLY schaltet den PD mini aus, wenn der Heartbeat endet, und der
Port schaltet nichts ein, solange dem Heartbeat nicht vertraut wird. Ein
Servo ohne Spannung ist in keinem Modus; ein Port, der so nach PWM
zurückkehrt, hat also Bit 4 gelöscht.

## Verlassen ohne Aus- und Einschalten der Versorgung

LEAVE (Operation 10, seit 4.15) bringt einen Port von PROGRAMMING nach PWM
zurück, mit der Versorgung, wie sie ist. Der Port sendet keinen Frame,
schaltet nichts und schreibt keinen Sollwert. Ein Ausgang, der an ist,
bleibt an, mit seiner Spannung und seiner Stromgrenze, auch einer, den ENTER
eingeschaltet oder abgesenkt hat. Antwortet kein PD mini, tut der Port
dasselbe. POWER_CYCLED und REBOOT bleiben, wie sie sind.

Ein angenommenes LEAVE setzt den Port in LEAVING. Der Leitungstreiber hält
den Pin für die Wartezeit aus Schreibregister 28 (`LEAVE_MS`) auf Low, 0 bis
10 000ms, gezählt ab dem Kommando. Die Wartezeit ist 100ms, bis ein Panel
das Register schreibt, und das Register behält das Geschriebene bis zum
Neustart des IOmcu. Das Register nimmt jeden Wert: ein LEAVE, das
mehr als 10 000 darin findet, wird mit LEAVE_MS abgewiesen, der Port bleibt
in PROGRAMMING, und der Wert wird nicht begrenzt. Ein Schreiben des
Registers, während der Port wartet, gilt für das nächste LEAVE. Nach der
Wartezeit hat der Generator den Pin, der Port steht in PWM mit gelöschten
Bits 4 und 5 und Ergebnis 0, und der Puls des Ports ist zurück, wie sein
`PWM`-Bit und `TARGET` es sagen. Eine Wartezeit von 0ms endet beim nächsten
Durchlauf der Hauptschleife. Die verbleibende Zeit steht nicht auf der
Page: alle 32 Leseregister sind belegt. Ein Panel zählt sie mit seiner
eigenen Uhr.

Nicht gemessen an einem KST- oder Chaservo-Servo: die Zeit, die die Leitung
vor dem ersten PWM-Puls auf Low sein muss, ob das Servo im Programmiermodus
PWM folgt, und ob ein geschriebener Wert vor dem nächsten Einschalten des
Servos wirkt. Ein gelöschtes Bit 4 nach einem LEAVE sagt, dass der Port
keine Session hält. Es sagt nicht, dass das Servo den Programmiermodus
verlassen hat.

| Der Port beim Kommando | LEAVE |
| --- | --- |
| dem Heartbeat wird nicht vertraut oder der Link schweigt, in jedem Zustand | abgewiesen mit UNSAFE |
| PWM | abgewiesen mit NOT_PROGRAMMING |
| STOPPING, LOW, RAIL_OFF, RAIL_WAIT, LEAVING | abgewiesen mit BUSY; eine laufende Wartezeit beginnt nicht neu |
| PROGRAMMING, eine Operation läuft oder plant | abgewiesen mit BUSY. Die Operation läuft weiter; ABORT beendet sie |
| PROGRAMMING, ein anderer Port genannt | abgewiesen mit PORT |
| PROGRAMMING, Bit 5 gesetzt | abgewiesen mit MUST_READ: eine unterbrochene Session wird nicht verlassen, bevor ein READ_ALL gelungen ist |
| PROGRAMMING, Bit 9 gesetzt | abgewiesen mit LOCKED: ein Servo, das ein Schreiben im Zweifel gelassen hat, wird mit RESTORE und dann LEAVE verlassen, oder durch Aus- und Einschalten der Versorgung |
| PROGRAMMING, Register 28 über 10 000 | abgewiesen mit LEAVE_MS |
| PROGRAMMING, nichts davon | angenommen: LEAVING, Bit 3 gesetzt, Ergebnis 1 |

Ein abgewiesenes LEAVE ändert nichts: kein Frame, kein Kommando an die
Versorgung, der Pin und der Zustand wie zuvor.

| In LEAVING | Die Versorgung | Der Port |
| --- | --- | --- |
| Die Wartezeit endet | wie sie ist | PWM mit gelöschten Bits 4 und 5, Ergebnis 0 |
| ABORT | wie sie ist | PROGRAMMING mit Ergebnis 19, die Session, wie LEAVE sie vorfand, Bit 5 nicht gesetzt |
| Ein Schalten auf SAFE | wie sie ist | PROGRAMMING mit Ergebnis 19 und gesetztem Bit 5 |
| Der Heartbeat verloren oder der Link schweigt, kein PD mini antwortet | unberührt | PROGRAMMING mit Ergebnis 19 und gesetztem Bit 5 |
| Die Versorgung liest aus oder ist aus angefordert (das STOP des Panels, ein verlorener Heartbeat, der eigene Schutz des PD mini) | 1000ms aus gehalten, nicht wieder eingeschaltet | RAIL_OFF, dann PWM mit gelöschten Bits 4 und 5 und Ergebnis 19 |
| Der PD mini hört auf zu antworten | unberührt | die Wartezeit läuft weiter |
| Jedes andere Kommando | wie sie ist | abgewiesen mit BUSY |

Ein Stopp in dem Durchlauf, in dem die Wartezeit endet, kommt zuerst: der
Port steht in PROGRAMMING.

## Die Wartezeit nach einem Versorgungszyklus

Ein REBOOT aus PROGRAMMING mit gesetztem `CMD`-Bit 5 (seit 4.30) schaltet
die Versorgung aus und ein und wartet danach in LEAVING, bevor PWM folgt.
Die Wartezeit ist Schreibregister 28, wie es beim Kommando steht, 0 bis
10 000ms, gezählt ab dem Ende von RAIL_WAIT. Der Leitungstreiber hält den
Pin vom Kommando bis PWM auf Low: RAIL_OFF 1000ms, RAIL_WAIT 1000ms, dann
die Wartezeit. Eine Wartezeit von 0 beendet den Zyklus am Ende von
RAIL_WAIT in PWM, wie ein REBOOT ohne das Bit. Nach der Wartezeit steht der
Port in PWM mit gelöschten Bits 4 und 5 und Ergebnis 0; Register 2 nennt
die ganze Zeit REBOOT. Ein REBOOT ohne das Bit beachtet Register 28 nicht.

Das Panel sendet das Bit mit dem REBOOT, der eine Session nach einem
Schreiben bei antwortendem PD mini beendet, mit seiner **WARTEZEIT VOR
PWM** in Register 28, und nur an einen IOmcu mit 4.30 oder neuer. Steht
**WARTEZEIT VOR PWM** auf 0, sendet es den REBOOT ohne das Bit.

| Der Port beim Kommando | REBOOT mit `CMD`-Bit 5 |
| --- | --- |
| PWM | abgewiesen mit NOT_PROGRAMMING |
| PROGRAMMING, Register 28 über 10 000 | abgewiesen mit LEAVE_MS; nichts wird geschaltet |
| PROGRAMMING, jeder andere Fall | wie REBOOT ohne das Bit: abgewiesen oder mit dem Zyklus angenommen |
| `CMD`-Bit 5 an einer anderen Operation oder `CMD`-Bit 4 gesetzt | der Frame mit BAD_VALUE abgewiesen |

| In der Wartezeit nach dem Zyklus | Die Versorgung | Der Port |
| --- | --- | --- |
| Die Wartezeit endet | wie sie ist: an mit den Sollwerten, die sie hatte | PWM mit gelöschten Bits 4 und 5, Ergebnis 0 |
| ABORT | wie sie ist | PROGRAMMING mit Ergebnis 19, `STATE`-Bit 5 gelöscht, auch wenn ein Stopp es vor dem REBOOT gesetzt hatte; der Zyklus hat das Backup und den Programmiermodus des Servos beendet, das zuletzt gelesene Image bleibt. Ein LEAVE wird sofort angenommen |
| Ein Schalten auf SAFE | wie sie ist | PWM mit Ergebnis 19: der Pin ist freigegeben |
| Der Heartbeat verloren oder der Link schweigt | ausgeschaltet | PWM mit Ergebnis 19 |
| Die Versorgung liest aus oder ist aus angefordert (das STOP des Panels, der eigene Schutz des PD mini) | aus | PWM mit Ergebnis 19, ohne weitere 1000ms aus |
| Der PD mini hört auf zu antworten | unberührt | die Wartezeit läuft weiter |
| Jedes andere Kommando | wie sie ist | abgewiesen mit BUSY; ein Schreiben auf Register 28 gilt für die nächste Wartezeit |

Das STOP des Panels ist ein Schalten auf SAFE, der Ausgang von SUPPLY auf 0
geschrieben und ein ABORT. In der Wartezeit gibt das davon, was der Port
zuerst sieht, den Pin frei. Kommt ein ABORT zuerst, steht der Port in
PROGRAMMING, und die ausgeschaltete Versorgung beendet das dann wie in
PROGRAMMING: 1000ms aus, dann PWM. Ein ABORT in RAIL_OFF oder RAIL_WAIT
beendet den Zyklus aus, wie ohne das Bit, und keine Wartezeit folgt.

Wie lange ein Servo zwischen dem Einschalten und dem ersten Puls braucht,
ist nicht gemessen.

## Register

Ein Lesen gibt diese Register:

| Register | Name | Liest |
| ---: | --- | --- |
| 0 | `STATE` | Bits 0 bis 2 der Zustand. Bit 3 eine Operation läuft. Bit 4 ein Frame wurde gesendet, seit das Servo zuletzt ohne Spannung war oder der Port zuletzt mit LEAVE verlassen wurde. Bit 5 ein Stopp oder ein Abbruch hat die Session unterbrochen: READ_ALL kommt vor allem anderen. Bit 6 ein Image ist gelesen. Bit 7 die Session hält ein Backup. Bit 8 das Image hat das bekannte Layout. Bit 9 nur RESTORE wird angenommen. Bit 10 ein PD mini antwortet, und der Port schaltet die Versorgung des Servos. Bit 11 die Register 16 bis 31 zeigen das Backup. Bits 12 bis 15 der Programmierport |
| 1 | `SEQ` | die `SEQ` des zuletzt angenommenen Kommandos; 0 beim Start |
| 2 | `OP` | Bits 12 bis 15 die zuletzt gestartete Operation, 0 beim Start und bis eine Operation startet; ein abgewiesenes Kommando lässt sie stehen. Bits 0 bis 11 die Frames, die sie gesendet hat, gehalten bei 4095 |
| 3 | `LINK_KS_RESULT` | Low-Byte das Ergebnis dieser Operation, 1 solange sie läuft, 35 solange ein WRITE oder ein RESTORE plant (seit 4.14). High-Byte der Grund, aus dem das zuletzt angenommene Kommando nichts gestartet hat, 0 wenn es gestartet hat |
| 4 | `LINK_KS_FAIL` | Low-Byte das Servoregister, das das Ergebnis nennt, 0xFF für keines. High-Byte die erledigten Schreibzugriffe des Plans |
| 5, 6 | `DIFF` | Bit n: Servoregister n weicht von dem ab, was die Operation erwartet hat. Register 5 hält die Servoregister 0 bis 15 |
| 7, 8 | `BAD` | Bit n: Servoregister n hat keine 3 gleichen Lesungen in 5 |
| 9 | `FP_RULES` | Bit n: Regel n des Layout-Fingerprints ist verletzt |
| 10, 11 | `FP_REGS` | Bit n: Servoregister n weicht vom Fingerprint ab |
| 12, 13 | `HALF_MIN_NS`, `HALF_MAX_NS` | die kürzeste und die längste Halbzelle, gemessen an den Antworten des Servos, seit der Port den Pin genommen hat, ns; 0 vor der ersten Antwort |
| 14, 15 | `DELAY_MIN_US`, `DELAY_MAX_US` | die letzte Flanke eines Lese-Frames bis zur ersten Flanke seiner Antwort, µs; 0 vor der ersten |
| 16 bis 31 | `IMAGE` | Servoregister 2 k im Low-Byte von Register 16 + k, 2 k + 1 im High-Byte. Das zuletzt gelesene Image, oder das Backup, solange `VIEW` 1 ist; 0 solange es keines gibt |

Ein Schreiben sieht andere Register:

| Register | Name | Nimmt |
| ---: | --- | --- |
| 0 | `CMD` | Bits 0 bis 3 die Operation, Bit 5 die Wartezeit nach dem Zyklus (nur REBOOT, seit 4.30), Bit 6 gesetzt: das Kommando nennt einen Programmierport (seit 4.13), Bit 7 die Bestätigung eines ungeprüften Restore (nur RESTORE), Bits 8 bis 11 der Port; jedes andere Bit 0 |
| 1 | `SEQ` | die `SEQ` der Page plus 1, modulo 65536 |
| 2 | `KEY` | 0x57A5 für WRITE, RESTORE und RELEASE, sonst 0 |
| 3 | `CRC` | die Prüfsumme über die Register 4 bis 24, für WRITE, RESTORE, RELEASE und VERIFY; für die anderen nicht angesehen |
| 4 bis 19 | `IMAGE` | gepackt wie die Register 16 bis 31 des Lesens: das Ziel eines WRITE, das Backup eines RESTORE, das Image, das ein VERIFY erwartet |
| 20 bis 23 | `UNLOCK` | Low-Wort zuerst: die Felder, die ein WRITE über die editierbare Klasse hinaus ändern darf, Bit n für Feld n von `kst_field_id_t` |
| 24 | `START_CRC` | die Prüfsumme über die 16 Register des Images, von dem das Panel geplant hat, wie das Lesen sie gab |
| 25 | `VIEW` | 0: die Register 16 bis 31 des Lesens zeigen das zuletzt gelesene Image. 1: das Backup. Gilt ab dem Schreiben |
| 26 | `SUPPLY_MV` | die Versorgungsspannung eines ENTER, mV (seit 4.13). Der Standard des Panels ist 5000 |
| 27 | `SUPPLY_MA` | die Stromgrenze eines ENTER, das die Versorgung einschaltet, mA (seit 4.13) |
| 28 | `LEAVE_MS` | die Wartezeit eines LEAVE mit der Leitung auf Low, 0 bis 10 000ms (seit 4.15), und eines REBOOT mit `CMD`-Bit 5 nach seinem Zyklus (seit 4.30). 100, bis es geschrieben wird |

Die Prüfsumme ist die CRC-16 (zyklische Redundanzprüfung) des Links,
Startwert 0xFFFF, über das Low-Byte und dann das High-Byte jedes Registers
(`link_kst_crc()`).

Die Register 0 bis 3 sind das Kommando: ein Frame, geschrieben als genau
diese 4 Register und ganz oder gar nicht angenommen. Die Register 4 bis 28
werden in beliebigen Fenstern vor dem Kommando geschrieben. Ein Schreiben,
das Register 29 oder höher erreicht, wird mit READ_ONLY abgewiesen, ein
Fenster, das die Register 0 bis 3 berührt und nicht genau sie ist, mit
BAD_RANGE. Ein Kommando mit einem reservierten Bit, einer Operation
außerhalb von 1 bis 10, der Bestätigung an einer anderen Operation als
RESTORE, Bit 5 an einer anderen Operation als REBOOT (seit 4.30), einem `KEY`, der weder 0 noch 0x57A5 ist, oder einer `SEQ`, die
nicht die der Page plus 1 ist, wird mit BAD_VALUE abgewiesen und ändert
nichts. Der zuletzt angenommene Frame, noch einmal gesendet, wird quittiert
und tut nichts; ein Kommando, dessen Antwort verloren ging, wird also
unverändert noch einmal gesendet. Nach einem Abbruch am Panel wird es nicht
noch einmal gesendet: das Panel liest die Register 0 bis 3. Die `SEQ` vor
dem Kommando heißt, dass es nicht angenommen wurde; die Anfrage gilt dann
als verworfen. Die `SEQ` des Kommandos heißt nur zusammen mit seiner
Operation in `OP` oder einer Abweisung im oberen Byte von
`LINK_KS_RESULT`, dass es angenommen wurde. Jede andere `SEQ` ist ein
IOmcu, der neu gestartet ist. Ein neu gestarteter IOmcu liest
`SEQ` 0, Operation 0 und keine Abweisung. Operation 0 nach einem Lesen,
das eine Operation nannte, ist ein Neustart bei jeder `SEQ`, auch bei der
des Kommandos und der davor: die `SEQ` 0 einer frischen Page ist nach
`SEQ` 0xFFFF die des Kommandos und nach `SEQ` 0 die davor, und eine
frische Page nimmt ein noch einmal gesendetes Kommando mit `SEQ` 1 als ihr
eigenes nächstes an. Das Panel meldet dann einen Neustart und kein
Ergebnis. Ein IOmcu, der seit seinem Start kein Kommando angenommen
hat, liest vor und nach einem Neustart `SEQ` 0 und Operation 0: startet er
zwischen seinem ersten Kommando und diesem Lesen neu, gilt die Anfrage als
verworfen. Von solchen ersten Kommandos starten nur ENTER und REBOOT eine
Operation; jedes Schreiben braucht PROGRAMMING, das ein ENTER startet. Ein
angenommenes Kommando wird quittiert; was aus ihm wurde, steht in
`LINK_KS_RESULT`.

| Operation | Wert | Tut |
| --- | ---: | --- |
| ENTER | 1 | in PWM: nimmt den Pin des Ports, setzt die Versorgung und fährt die Einstiegssequenz. In PROGRAMMING: fährt die Einstiegssequenz noch einmal |
| READ_ALL | 2 | liest die 32 Register, jedes bis 3 von höchstens 5 Lesungen übereinstimmen. Das erste Image einer Session ist ihr Backup |
| WRITE | 3 | plant vom gelesenen Image zum bereitgestellten Image, ein Slice je Durchlauf (seit 4.14), und fährt den Plan: jeder Schreibzugriff wird zurückgelesen, und einer, der nicht greift, wird rückgängig gemacht |
| RESTORE | 4 | plant zurück zum Backup, das die Session hält, und fährt den Plan. Ein bereitgestelltes Image, das nicht dieses Backup ist, beendet es mit Ergebnis 10 |
| RELEASE | 5 | löst das Pairing |
| VERIFY | 6 | liest alle Register und vergleicht sie mit dem bereitgestellten Image |
| ABORT | 7 | beendet die laufende Operation. In STOPPING und LOW kehrt der Port nach PWM zurück. In RAIL_OFF wird die Versorgung nicht wieder eingeschaltet, in RAIL_WAIT wird sie ausgeschaltet, und der Ablauf endet mit Ergebnis 19. In jedem Zustand angenommen; Register 2 nennt es nicht |
| POWER_CYCLED | 8 | in PROGRAMMING: schaltet die Versorgung aus und ein und bringt den Port nach PWM. Antwortet kein PD mini oder ist er aus angefordert: das Wort des Bedieners, dass das Servo ohne Spannung war, sofort nach PWM |
| REBOOT | 9 | schaltet die Versorgung aus und ein, in PROGRAMMING und in PWM (seit 4.13). Mit `CMD`-Bit 5, aus PROGRAMMING: die Wartezeit aus Register 28 nach dem Zyklus (seit 4.30) |
| LEAVE | 10 | in PROGRAMMING ohne laufende Operation: hält die Leitung für die Wartezeit aus Register 28 auf Low und bringt den Port nach PWM zurück. Die Versorgung wird nicht geschaltet (seit 4.15) |

Das Ergebnis im Low-Byte von `LINK_KS_RESULT` ist das `kst_ses_t` der Session: 0
fertig, 1 läuft, 4 kein Servo hat geantwortet, 5 nicht im Programmiermodus,
6 ein Register ohne 3 gleiche Lesungen, 7 kein Backup, 8 das Layout ist
nicht das bekannte, 9 auf einen Restore gesperrt, 10 ein Plan, den die
Session nicht annimmt, 11 ein Restore, der die Bestätigung braucht, 12 das
Image des Servos hat sich seit dem Lesen geändert, 13 ein Schreibzugriff
hat nicht gegriffen und nichts ist geändert, 14 ein Plan rückgängig
gemacht, 15 ein Plan zum Teil rückgängig gemacht, 16 ein Register hat sich
geändert, das der Plan nicht geschrieben hat, 17 das Image weicht nach dem
Plan ab, 18 der Leitungstreiber ist ausgefallen oder ließ sich nicht
öffnen, 19 abgebrochen. 32 bis 35 sind die eigenen des Ports. 32 (seit
4.13): die Versorgung hat nicht rechtzeitig geschaltet. 33 (seit 4.14):
keine Reihenfolge von Schreibzugriffen hält jedes Image dazwischen gültig.
34 (seit 4.14): der Planer hat 216 000 Einheiten Arbeit verbraucht, bevor er
einen Plan hatte oder wusste, dass es keinen gibt. 35 (seit 4.14): die
Operation plant; es ist nie das Ergebnis einer beendeten Operation.

| Abweisung | Wert | Das Kommando hat nichts gestartet, weil |
| --- | ---: | --- |
| BUSY | 1 | eine Operation läuft oder der Port zwischen zwei Zuständen ist |
| PORT | 2 | der Build keinen solchen Programmierport hat, dem Kommando Bit 6 fehlt oder der Port nicht der in PROGRAMMING ist |
| UNSAFE | 5 | dem Heartbeat nicht vertraut wird oder der Link still ist |
| NOT_PROGRAMMING | 6 | der Port in PWM steht; auch für einen REBOOT mit `CMD`-Bit 5 (seit 4.30) |
| NO_ENABLE | 7 | WRITE, RESTORE oder RELEASE ohne den Key |
| STAGED | 8 | die Register 4 bis 24 nicht die Prüfsumme haben, die das Kommando nennt |
| START | 9 | das gelesene Image nicht das ist, von dem das Panel geplant hat |
| MUST_READ | 10 | ein Stopp oder ein Abbruch die Session unterbrochen hat und seitdem kein READ_ALL gelungen ist |
| NO_IMAGE | 11 | kein Image gelesen ist, von dem geplant werden kann |
| PLAN_RULES | 12 | das Ziel eine harte Regel verletzt |
| PLAN_R00 | 13 | das Ziel in Servoregister 0x00 abweicht |
| PLAN_PAIRING | 14 | ein WRITE in Servoregister 0x1D abweicht |
| PLAN_NO_PATH | 15 | keine Reihenfolge von Schreibzugriffen jedes Image dazwischen gültig hält (nur 4.12 und 4.13; seit 4.14 Ergebnis 33) |
| NO_PIN | 16 | der Port keinen Pin hält oder seine FUNC DShot ist (seit 4.28), bei einem ENTER und bei einem REBOOT in PWM (seit 4.13) |
| SUPPLY_RANGE | 17 | die bereitgestellte Versorgungsspannung oder der Strom außerhalb dessen liegt, was ENTER nimmt (seit 4.13) |
| NO_SUPPLY | 18 | ein REBOOT, bei dem kein PD mini antwortet oder der PD mini aus angefordert ist (seit 4.13) |
| LEAVE_MS | 19 | ein LEAVE mit mehr als 10 000ms in Register 28 (seit 4.15), und ein REBOOT mit `CMD`-Bit 5 (seit 4.30) |
| LOCKED | 20 | ein LEAVE, während nur RESTORE angenommen wird (seit 4.15) |
| SUPPLY_LOW | 21 | ein ENTER, dessen Versorgungsspannung, die bereitgestellte oder die niedrigere eines Ausgangs, der an ist, unter 4800mV liegt (seit 4.15) |

Die Werte 3 und 4 werden nicht gegeben. Ein Ergebnis, das die Session ohne
Plan kennt (5, 7, 8, 9 und 10 bei einem RESTORE, dessen bereitgestelltes
Image nicht das Backup ist), wird vor PLAN_RULES, PLAN_R00 und PLAN_PAIRING
gegeben (seit 4.14): eine gesperrte Session beantwortet ein WRITE mit
Ergebnis 9, gleich welches Ziel es hat. NO_ENABLE wird vor jedem anderen
Grund geprüft. Das Panel sendet den Key nur, solange sein Schalter ENABLE
WRITE TO SERVO an ist; ein Kommando, das den IOmcu ohne ihn erreicht,
schreibt also in keinem Zustand des Ports. Kein Bildschirm dieses Builds
hält diesen Schalter.

Ein Programmierport ist per Definition bidirektional: sein Pin ist ein
blanker GPIO, den der Leitungstreiber zwischen Ausgang und Eingang
umschaltet. Ein Servo hinter einem Puffer, der nur ausgibt, beendet ENTER
mit Ergebnis 4.

## STOP, Schalten auf SAFE, Heartbeat-Verlust und Link-Verlust

Der Port sieht drei Dinge. Ein Stopp ist die Flanke des Failsafe (der
Heartbeat hat geendet oder der Link ist still geworden) oder die Flanke des
Schaltens der Bank auf SAFE. Unsicher ist die Zeit, in der dem Heartbeat nicht
vertraut wird oder der Link still ist. Das dritte ist die Versorgung: SUPPLY
schaltet den PD mini aus, wenn der Heartbeat endet, und das Panel schreibt
den Ausgang von SUPPLY bei jedem STOP aus.

Das STOP des Panels ist am IOmcu deshalb drei Dinge: ein Schalten
einer Bank von ARMED auf SAFE, der Ausgang von SUPPLY auf 0 geschrieben und das
Kommando ABORT aus `kst_link_abort()`.

| Zustand | Ein Schalten auf SAFE allein | Das STOP des Panels | Heartbeat verloren, Link still |
| --- | --- | --- | --- |
| PWM | der Puls endet, wie an jedem Ausgang. Die Versorgung bleibt, wie sie ist | der Puls endet, und die Versorgung ist aus | der Puls endet. SUPPLY schaltet den PD mini aus |
| STOPPING, LOW | der Port kehrt mit Ergebnis 19 nach PWM zurück. Eine Versorgung, die dieses ENTER eingeschaltet hat, wird ausgeschaltet; eine an vorgefundene bleibt an | dasselbe, und die Versorgung ist aus | dasselbe, und SUPPLY schaltet den PD mini aus |
| PROGRAMMING | die laufende Operation endet mit Ergebnis 19: ein Frame auf der Leitung wird beendet, was höchstens 12ms dauert, und kein weiterer beginnt. Der Pin ist danach auf Low getrieben oder Eingang. Nach einem ersten Frame wird Bit 5 gesetzt, und der Port bleibt PROGRAMMING; davor kehrt der Port nach PWM zurück. Die Versorgung bleibt an | die Operation endet ebenso. Die Versorgung ist aus: der Port hält sie 1000ms aus und kehrt mit gelöschten Bits 4 und 5 und Ergebnis 19 nach PWM zurück. Antwortet kein PD mini, bleibt der Port PROGRAMMING mit gesetztem Bit 5 | wie das STOP des Panels |
| RAIL_OFF | nichts: der Ablauf geht weiter | die Versorgung bleibt aus; nach den 1000ms steht der Port mit Ergebnis 19 in PWM | wie das STOP des Panels |
| RAIL_WAIT | nichts: der Ablauf geht weiter | die Versorgung wird ausgeschaltet; der Port steht mit Ergebnis 19 in PWM | wie das STOP des Panels |
| LEAVING | die Wartezeit endet mit Ergebnis 19, und der Port steht in PROGRAMMING mit gesetztem Bit 5. Die Versorgung bleibt an | die Wartezeit endet genauso. Die Versorgung ist aus: der Port hält sie 1000ms aus und kehrt mit gelöschten Bits 4 und 5 und Ergebnis 19 nach PWM zurück. Antwortet kein PD mini, bleibt der Port in PROGRAMMING | wie das STOP des Panels |
| LEAVING nach dem Zyklus eines REBOOT (seit 4.30) | der Pin ist freigegeben: PWM mit Ergebnis 19. Die Versorgung bleibt an | PWM mit Ergebnis 19 und die Versorgung aus | PWM mit Ergebnis 19; der Port schaltet die Versorgung aus |

Solange Bit 5 gesetzt ist, nimmt der Port nur ENTER, READ_ALL, ABORT,
POWER_CYCLED und REBOOT, bis ein READ_ALL gelungen ist. ABORT allein hat in
STOPPING, LOW und PROGRAMMING die Wirkung eines Schaltens auf SAFE. In LEAVING
bringt es den Port ohne Bit 5 nach PROGRAMMING zurück.

Eine Operation, die plant, ist eine Operation in PROGRAMMING, und die Zeile
oben gilt für sie: ein Schalten auf SAFE, das STOP des Panels, ein verlorener
Heartbeat, ein stiller Link, ein ABORT und eine aus vorgefundene Versorgung
beenden das Planen im nächsten Durchlauf mit Ergebnis 19. Kein Frame wurde
gesendet und kein Register geschrieben, und die Session ist nicht gesperrt.
Bit 5 ist gesetzt wie nach einem abgeschnittenen Frame, weil die Session
Frames gesendet hat, bevor sie plante. Die Versorgung bleibt nach einem
Schalten auf SAFE oder einem ABORT allein an. Nach dem STOP des Panels, einem
verlorenen Heartbeat oder einem stillen Link ist sie aus, und wenn ein PD
mini antwortet, hält der Port sie 1000ms aus und kehrt mit gelöschten Bits
4 und 5 nach PWM zurück. POWER_CYCLED und REBOOT beenden das Planen und
laufen dann. Jedes andere Kommando wird mit BUSY abgewiesen, und das Planen
geht weiter. Ein PD mini, der aufhört zu antworten, beendet es nicht.

Ein
Schalten auf SAFE wird als Flanke gesehen: der Port stoppt einmal und nimmt wieder
Kommandos, während die Bank SAFE bleibt; ein Servo wird also an einem
Prüfstand in SAFE gelesen und geschrieben. Solange dem Heartbeat nicht
vertraut wird oder der Link still ist, beginnt kein Frame, und jede
Operation außer ABORT und POWER_CYCLED wird mit UNSAFE abgewiesen.

Ein Link, der in RAIL_OFF verloren geht, lässt die Versorgung aus und den
Port in PWM: ohne Panel am Link könnte niemand die Versorgung wieder
ausschalten, und ein Servo, das 1000ms ohne Spannung war, hat den
Programmiermodus verlassen, wenn 1000ms genügen, was nicht gemessen ist.
Das Panel schaltet die Versorgung auf SUPPLY wieder ein.

| Panel | IOmcu | Servo-Programmier-Page (`KST`, 0x32) |
| --- | --- | --- |
| 4.30 oder neuer | 4.30 oder neuer | bedient |
| 4.30 oder neuer | 4.15 bis 4.29 | bedient ohne die Wartezeit nach einem Zyklus. Ein solcher IOmcu weist `CMD`-Bit 5 mit BAD_VALUE ab: das Modul des Panels sendet für einen REBOOT weder das Bit noch Register 28 und beantwortet die Anfrage mit `KST_LINK_START_NO_OP`; das Panel sendet den REBOOT ohne das Bit, der Zyklus endet in PWM, und die Meldung sagt, dass **WARTEZEIT VOR PWM** nicht angewendet wurde |
| 4.15 bis 4.29 | 4.30 oder neuer | bedient. Das Panel sendet kein `CMD`-Bit 5; jeder REBOOT endet am Ende von RAIL_WAIT in PWM |
| 4.15 oder neuer | 4.15 oder neuer | bedient, ab 4.30 wie oben |
| 4.15 oder neuer | 4.14 | bedient ohne LEAVE. Ein 4.14-IOmcu weist die Operation 10 mit BAD_VALUE und ein Schreiben auf Register 28 mit READ_ONLY ab: das Modul des Panels sendet keines von beiden und beantwortet die Anfrage mit `KST_LINK_START_NO_OP`. POWER_CYCLED und REBOOT sind die Wege hinaus |
| 4.14 | 4.15 oder neuer | bedient. Das Panel sendet keine Operation 10 und kein Register 28, und der Port steht nie in LEAVING |
| 4.14 oder neuer | 4.13 | bedient, ab 4.15 ohne LEAVE wie oben. Der IOmcu plant im Frame des Kommandos: das Modul des Panels meldet nie PLANNING, und ein Auftrag ohne Reihenfolge von Schreibzugriffen ist die Abweisung PLAN_NO_PATH |
| 4.13 | 4.14 oder neuer | bedient. Das Modul des Panels liest eine Operation, die plant, als eine, die läuft, und die Ergebnisse 33 und 34 als Ergebnisse, für die es keinen Namen hat |
| 4.13 oder neuer | 4.12 | die Page nennt dort einen Ausgangskanal; das Modul des Panels liest die Minor beim Link-up und sendet unter 13 nichts, kein Schreiben und kein Lesen |
| 4.13 oder neuer | 4.11 oder älter | keine Page: der IOmcu beantwortet eine Anfrage an 0x32 mit BAD_PAGE, und das Panel sendet keine |
| 4.12 | 4.13 oder neuer | ein Kommando von 4.12 hat Bit 6 gelöscht und wird mit PORT abgewiesen; ein Schreiben auf Register 26 oder 27 kommt nie. Der Port bleibt in PWM |
| 4.11 oder älter | 4.13 oder neuer | das Panel liest und schreibt 0x32 nie; der Port bleibt in PWM |

`shared/bench/kst_link.c` ist die Hälfte des Panels. Eine Anfrage nennt den
Port, für ENTER Spannung und Strom der Versorgung und für LEAVE und einen
REBOOT mit dem Bit (`then_leave`) die Wartezeit, die das Modul über
10 000ms abweist, bevor etwas gesendet wird.
Es stellt die
Register bereit, sendet das Kommando mit der nächsten `SEQ`, liest die
Register 0 bis 3 alle 50ms, solange eine Operation läuft oder der Port
nicht in PWM steht, und sonst alle 500ms (eine Anfrage und ein Daten-Frame,
0,31ms Buszeit), und liest nach einer Operation die Register 4 bis 15, das
Backup und das Image. Seine Phase ist PLANNING, solange die zuletzt
gelesene Page Bit 3 gesetzt und Ergebnis 35 hat, und RUNNING vor dem ersten
Lesen und ab dem Plan. Es sendet ein Kommando bis zu 3-mal noch einmal, wenn
keine Antwort kommt, nach `kst_link_abort()` kein Mal, und liest `SEQ` nach einer Abweisung mit BAD_VALUE neu.
Nach einem abgewiesenen ABORT behält dieses Lesen die Prüfungen von oben:
eine Page abseits der `SEQ` des Panels oder mit Operation 0 nach einem
Lesen, das eine nannte, beendet die laufende Operation als neu gestartet,
nie als erledigt. Ein mit BAD_VALUE abgewiesenes ABORT geht nach dem Lesen
noch einmal, mit der `SEQ` der Page.
Ein mit BAD_PAGE abgewiesenes Lesen der Register 0 bis 3 heißt: ein
IOmcu ohne die Page; mit jedem anderen Code abgewiesen zählt es als
unbeantwortet und wird erneut gesendet. Bei einem IOmcu ohne die Page
behält das Modul für den Aufrufer, ob der Port beim letzten beantworteten
Lesen außerhalb von PWM war. Jede Anfrage hat eine Nummer, und ihr Ergebnis
trägt diese Nummer.
Es weist WRITE, RESTORE und RELEASE ohne den Schalter des Aufrufers ab,
bevor etwas gesendet wird. `shared/bench/supply_link.c` nimmt ein Kommando,
das der Port auf SUPPLY hinterlassen hat, als das angeforderte und schreibt
sein eigenes nicht darüber (`supply_link_adopted()`). Der Programmierer des
SERVO-Bildschirms ist der Aufrufer
([Servo-Programmierer](ServoProgrammer-de.md)).

`test_kst_port` fährt den Port und die Page gegen das Servo- und das
Versorgungsmodell: jeden Übergang und jede Abweisung, einen Stopp in jedem
Zustand und mitten in einem Frame, ein Schreiben, das 864 Durchläufe lang
mit je einem Slice plant, ein ABORT, ein Schalten auf SAFE, einen verlorenen
Heartbeat, eine abgeschaltete Versorgung, ein POWER_CYCLED, ein REBOOT und
ein zweites Kommando während des Planens, einen Auftrag ohne Reihenfolge
von Schreibzugriffen, einen über der Arbeitsgrenze, ein Schreiben ohne den
Key, einen PD
mini, der fehlt, langsam ist, schweigt oder ablehnt, ein ABORT in RAIL_OFF
und in RAIL_WAIT, die Versorgung an vorgefunden über, auf und unter der
Programmierspannung, einen Link, der in RAIL_OFF und in RAIL_WAIT verloren
geht, die Zähler an ihren Enden und jeden Timer über den Überlauf bei 2^32ms.
Für LEAVE fahren sie das Kommando in jedem Zustand, mit einem PD mini, der
an ist, vom Einstieg eingeschaltet ist, schweigt und fehlt, während eines
Frames und während der Port plant, mit gesetztem Bit 5 und gesetztem Bit 9,
Wartezeiten von 0, 1, 100 und 10 000ms, die Abweisung bei 10 001ms, und
ein ABORT, ein Schalten auf SAFE, einen verlorenen Heartbeat und eine
abgeschaltete Versorgung in der Wartezeit. `test_kst_cycle_wait` fährt
die Wartezeit nach dem Zyklus eines REBOOT: Wartezeiten von 0, 1, 300 und
10 000ms, die Abweisung bei 10 001ms, das Bit an jeder anderen Operation
und aus PWM, Bit 4, einen REBOOT ohne das Bit nach einer geschriebenen
Wartezeit, ABORT, ein Schalten auf SAFE, eine nicht sichere Bench, die
ausgeschaltete Versorgung und einen schweigenden PD mini in der Wartezeit,
ABORT in RAIL_OFF und RAIL_WAIT, jedes andere Kommando in der Wartezeit,
die Wartezeit über den Überlauf bei 2^32ms und die Frames des Moduls für
einen IOmcu mit 4.30 und mit 4.29. `test_kst_link` fährt das
Modul des Panels durch den Dispatcher gegen den Port: ganze Sessions, ein
Schreiben von PLANNING bis zum Ende, die Ergebnisse 33 und 34, einen
Abbruch und einen Stopp während des Planens, ein LEAVE mit seiner
Wartezeit und eines, das in der Wartezeit abgebrochen wird,
verlorene Antworten, eine veraltete `SEQ`, einen Neustart des IOmcu
und einen 4.14-, einen 4.13-, einen 4.12- und einen 4.11-IOmcu.
