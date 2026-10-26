# Der Programmierport für Servos von Hitec

<sub>[English](LinkHitec.md) · **Deutsch**</sub>

Diese Seite ist ein Abschnitt von [Der Link](Link-de.md), der die Verkabelung, das
Frame-Format, die Page Map und die übrigen Pages enthält. Pins heißen nach ihrer
GPIO-Nummer (General-Purpose Input/Output). PWM steht für
Pulsweitenmodulation.

Hitec ist ein Name oder eine Marke seines Inhabers. Er steht hier nur, um zu
sagen, mit welchen Servos die Page arbeitet.

Die Hitec-Page (`HITEC`, 0x34) ist neu in 4.23; das Schreibregister 23, der
Empfangsmodus einer Leitung der D-Serie, ist neu in 4.24. Ab 4.29 hält ein
gehaltener Port die SUPPLY-Page von OPEN bis CLOSE
([Die Versorgung, solange ein Port gehalten ist](#die-versorgung-solange-ein-port-gehalten-ist)).
Die Page fährt rohe Transaktionen
der Hitec-Servoleitung aus [`protocols/hitec`](../protocols/hitec/README.md)
auf einem Port von [PROG](Link-de.md#die-programmierports): classic (HS-5xxx,
HS-7xxx, 19.200bit/s) oder D-Serie (115.200bit/s). Die Page hält keine
Hitec-Session. Der Schreiber stellt eine Transaktion bereit, die Page fährt
sie, und der Mitschnitt und seine Dekodierung bleiben bis zur nächsten auf
der Page. Welche Bytes ein Servo nimmt und in welcher Reihenfolge, ist Sache
des Schreibers. Der Session-Kern
([`hitec_session.h`](../protocols/hitec/README.md#session)) und die
Link-Hälfte des Panels ([Die Link-Hälfte des Panels](#die-link-hälfte-des-panels))
führen die Programmierung auf der Seite SERVO aus, die ein Servo liest und
nichts hineinschreibt ([Servoprogrammierung für Hitec-Servos](HitecProgrammer-de.md)).
`shared/outputs/hitec_port.c` hält die Regeln.

Ein Port ist frei, von dieser Page gehalten oder von der KST-Page gehalten
([Der Programmierport für Servos von KST und Chaservo](LinkKST-de.md)), nie
zwei davon. Die Page hält einen Port zur Zeit; die KST-Page darf einen
anderen halten. Nichts wird im Flash gehalten: nach einem Neustart des IOmcu
ist der Port frei.

Jeder Port des Builds kann geöffnet werden, 4 ab Protokoll 4.26: der Pin des
Ports wird übergeben, gleich welche FUNKTION er hat, der geparkte Pin eines
Ports mit KEINE wie der eines SERVO-PWM-Ports, und bei CLOSE dieser Funktion
zurückgegeben. Der Bedarf an PIO und DMA ist der der Leitung, auf welchem
Port sie auch liegt: eine Leitung, also höchstens 2 State Machines und 3
DMA-Kanäle mit PRECHARGE.

Das Panel liest `STATE` mit der PROG-Page alle 500ms, ab 4.23. Solange es
einen gehaltenen Port nennt (STOPPING bis POWER_OFF), graut SERVO >
PROGRAMMIEREN eine Zeile auf diesem Port mit `PORT BELEGT` aus
([Bildschirme](Screens-de.md#programmierer)).

## Hinein und hinaus

OPEN nimmt den Port:

1. STOPPING, 50ms: der Port gibt keinen Puls aus, ein laufender Puls endet.
2. Der PWM-Generator gibt den Pin ab. Der Leitungstreiber öffnet die Leitung
   darauf als losgelassenen Eingang: classic mit dem Pull-down des Pads, die
   D-Serie mit seinem Pull-up und invertiertem Eingang. Die internen Pulls
   haben 50 bis 80kΩ; die D-Serie empfängt mit einem Pull-up auf der
   Platine oder per PRECHARGE ([Empfangsmodus](#empfangsmodus)). Getestet
   mit einem D485HW: mit dem Pull-up des Pads allein bleibt die Leitung
   unter 1,4V und der Servo beantwortet kein Lesen; mit 2,2kΩ von der
   Signalleitung zur Servoversorgung beantwortet er jedes Lesen. Die
   Spannungen auf der Leitung, und ob der High-Pegel des Servos in der
   Belastbarkeit des Pins bleibt, sind nicht gemessen
   ([Electrical](../protocols/hitec/README.md#electrical)).
3. OPEN. Ein Treiber, der keine PIO-State-Machine (PIO: Programmable
   Input/Output), keine 30 Worte Programmspeicher oder keine 2 DMA-Kanäle
   (Direct Memory Access) bekommt, lässt den Port mit Ergebnis 5 (`NO_LINE`)
   frei. PRECHARGE braucht eine zweite State-Machine, bei 150MHz 7 Worte
   Programmspeicher in einem anderen PIO-Block und einen dritten DMA-Kanal;
   ohne sie bleibt der Port ebenfalls mit `NO_LINE` frei.

In OPEN startet ein Kommando eine Transaktion (BUSY). Der Port steht wieder
in OPEN, wenn der Treiber den Mitschnitt zurückgibt. Der Treiber beendet eine
Transaktion selbst 1ms nach der längsten Zeit, die sie dauern kann: der
Mitschnitt behält jedes Byte, das vor diesem Ende empfangen wurde, und ist
als Überlauf markiert (`CAP` Bit 10). Eine Transaktion, die nach 10.000ms
noch läuft, bricht zusätzlich die Page ab.

CLOSE gibt den Port aus jedem Zustand außer frei zurück: eine laufende
Transaktion wird abgeschnitten, die Leitung geschlossen und der Pin gehört
dem Generator. Die Versorgung bleibt, wie sie ist. ABORT schneidet ab, was
läuft, und behält den Port.

## Die Versorgung, solange ein Port gehalten ist

Ab 4.29 hält die Page die SUPPLY-Page, solange sie einen Port hält: vom
Frame des OPEN bis zum Frame des CLOSE, in STOPPING, OPEN, BUSY und
POWER_OFF, ein gestoppter Port eingeschlossen. Die KST-Page hält sie ebenso,
solange ihr Port nicht in PWM steht
([Der Programmierport für Servos von KST und Chaservo](LinkKST-de.md)).
Solange sie gehalten ist:

- ist Bit 11 der Flags von SUPPLY gesetzt;
- weist SUPPLY mit BAD_VALUE jedes Schreiben seiner Register 4 bis 6 ab,
  das kein Ausschalten ist: ein Einschalten eines ausgeschalteten Ausgangs
  und jede Änderung der Spannung oder der Stromgrenze;
- wird ein Schreiben mit OUTPUT 0 als Ausschalten genommen, gleich welche
  Sollwerte mitkommen, und die Page behält ihre Sollwerte;
- wird ein Schreiben genommen, das einen eingeschalteten Ausgang mit den
  geltenden Sollwerten wiederholt;
- schaltet POWER die Versorgung wie bereitgestellt: das Halten hält die
  Schreibzugriffe des Panels, nicht die des Ports selbst.

Zwischen zwei POWER sind die Sollwerte die des letzten POWER oder die beim
OPEN geltenden. Das Halten endet mit dem Port: bei CLOSE, bei einem ABORT
oder einem Stopp, während der Port in STOPPING steht, bei einem OPEN, das
mit Ergebnis 5 (`NO_LINE`) endet, und bei einem Neustart des IOmcu. Ein
Stopp, eine unsichere Bank, ein ABORT in OPEN, BUSY oder POWER_OFF und der
Abbruch nach 10.000ms behalten den Port und das Halten. Ein verlorener
Heartbeat oder ein stiller Link schaltet die Versorgung trotzdem aus: das
tut SUPPLY selbst, gehalten oder nicht ([SUPPLY](Link-de.md)).

Ein IOmcu mit 4.23 bis 4.28 hält SUPPLY nur vom Ausschalten eines POWER bis
zum Ende seines LISTEN und nimmt zwischen zwei POWER die Sollwerte des
Panels. Das Panel liest Bit 11 von beiden.

## Empfangsmodus

Das Schreibregister 23 (`RX_MODE`) sagt, wie eine Leitung der D-Serie die
Antwort des Servos empfängt, ab dem nächsten OPEN. Das OPEN nimmt den Wert,
den das Register hält, wenn das OPEN geschrieben wird; eine Änderung bei
offener Leitung wartet auf das nächste OPEN. Ein OPEN einer classic-Leitung
übergeht das Register: ein classic-Servo treibt seinen High-Pegel selbst,
über etwa 1kΩ, und antwortet an einem Open-Drain-Port ohne Pull-up und
ohne Precharge (gemessen an einem HS-7955TG, einem HS-5955TG und einem
HS-5645MG bei 5,0V Versorgung; die Leitungsspannungen sind nicht gemessen).

| Wert | Name | Wie die Antwort empfangen wird |
| ---: | --- | --- |
| 0 | `PULL-UP` | die Leitung losgelassen; ein Pull-up von 2,2kΩ von der Signalleitung zur Servoversorgung hebt sie. Die Voreinstellung und der Wert nach einem Neustart des IOmcu |
| 1 | `PRECHARGE` | kein Pull-up. Alle 1086,7ns treibt der IOmcu das Pad 100,0ns auf High, lässt es los und tastet es 113,3ns nach dem Loslassen ab |

Auf einer PRECHARGE-Leitung:

- Ein SEND mit Empfangsfenster geht ohne Empfänger hinaus. Der erste
  Durchlauf der Hauptschleife des IOmcu, der die Anfrage beendet findet,
  gibt das Pad an eine zweite State-Machine: 2mA Treiberstärke, langsame
  Flanke, Eingang nicht invertiert, der Pull-up des Pads wie zuvor. Ein
  DMA-Kanal schreibt die Abtastungen in den Speicher, und jeder Durchlauf
  dekodiert die seit dem letzten geschriebenen als invertiertes 8N1
  (`protocols/hitec/include/hitec_pre.h`).
- Ein LISTEN und das LISTEN eines POWER tasten ab ihrem Start ab.
- Der Mitschnitt ist der einer PULL-UP-Leitung: dieselben Register,
  dieselben Dekoder, dieselben Ergebniscodes. Die Zeit eines Bytes trägt
  den Start des Abtasters, abgelesen am Mikrosekunden-Timer des IOmcu,
  stimmt also auf 1us; die Flanke eines Startbits liegt höchstens eine
  halbe Abtastung, 543ns, neben der angegebenen Zeit.
- Mit `RANGE` abgewiesen: ein LISTEN oder POWER, das antwortet, und ein
  Empfangsfenster über 30.000us.
- Ein SEND ohne Fenster (ein Schreiben der D-Serie) und ein LEVEL laufen wie
  auf einer PULL-UP-Leitung: ohne Abtaster.
- Am Ende des Empfangs, bei ABORT, CLOSE und einem Stopp hält der Abtaster
  mit losgelassenem Pad an, und Steuer- und Pad-Register des Pins werden
  zurückgeschrieben, wie sie waren.
- Ein Empfang, der bei eingeschalteter Versorgung startete, wird im
  Durchlauf abgeschnitten (Ergebnis 6, `CUT`), der die Versorgung
  ausgeschaltet, keinen antwortenden PD mini oder ihren Ausgang als aus
  gelesen findet, nachdem er ihn als an gelesen hatte (der Schutz des PD
  mini lässt OUTPUT eingeschaltet). Ein SEND oder LISTEN gilt ab seinem
  Start als an gelesen. Das LISTEN eines POWER startet im Durchlauf des
  ON, während der Ausgang noch als aus gelesen wird; es gilt ab dem ersten
  Durchlauf als an gelesen, der den Ausgang nicht als aus liest. Eines,
  dessen Ausgang das ganze Fenster lang, höchstens 30.000us, als aus
  gelesen wird, läuft bis zu seinem Ende, mit Ergebnis 2. Ein
  Empfang, der bei ausgeschalteter Versorgung oder ohne PD mini startete,
  läuft bis zu seinem Ende.

Bit 7 von `STATE` liest das Register; Register 25 von `VIEW` 2, Bit 8, sagt,
dass die offene Leitung per PRECHARGE empfängt. Ein IOmcu mit 4.23 weist
ein Schreiben, das Register 23 erreicht, mit READ_ONLY ab und empfängt mit
dem Pull-up; das Panel markiert dann die Einstellung unter SETUP
([INTERFACES: der Programmierport](Screens-de.md#interfaces-der-programmierport)).
Was gemessen ist und was nicht:
[`protocols/hitec`](../protocols/hitec/README.md#receive-by-precharge-and-sample).

## Transaktionen

| Operation | Was auf die Leitung geht | Schlüssel |
| --- | --- | --- |
| SEND | der Vorlauf auf dem Ruhepegel (D-Serie: auf Low getrieben), 1 bis 8 Bytes mit 0 bis 6 Ruhe-Bitzeiten dazwischen, dann der Empfänger für das Fenster | immer |
| LISTEN | der Empfänger für das Fenster; mit Antwort ein Byte am Ende des Stoppbits des ersten empfangenen Bytes, und der Empfänger wieder ab dem Ende des Stoppbits der Antwort | mit Antwort |
| LEVEL | die Leitung 1us bis 4s auf einem Pegel gehalten, dann losgelassen. Open Drain treibt nur Low | nie |
| POWER | das Aus- und Einschalten der Versorgung unten, dann ein LISTEN ab dem Einschalten | mit Antwort |

Die Antwort dauert 10 Bitzeiten: Startbit, 8 Datenbits, Stoppbit. Ihr
Startbit beginnt 10,0 bis 10,13 Bitzeiten nach dem Startbit des ersten
empfangenen Bytes. Der Empfänger wartet ab dem Ende des Stoppbits der
Antwort auf das nächste Startbit, ein Byte, das der Servo gleich nach der
Antwort beginnt, wird also empfangen.

Vorlauf, Fenster und Pegel reichen bis 4.000.000us. Ein Mitschnitt hält bis
zu 16 Bytes. Das Fenster zählt nur die Zeit, die auf ein Startbit gewartet
wird. Jedes empfangene Byte hat die Zeit seines Startbits in ns ab dem Start
des Empfängers und sein Stoppbit, wie es abgetastet wurde.

Ein SEND, ein LISTEN oder ein POWER nennt einen Dekoder für den Mitschnitt:

| Dekoder | Argument | Dekodiert |
| ---: | --- | --- |
| 0 | keins | nichts; `DEC` liest 0, `VALUE` 0 |
| 1 | das classic-Kommandobyte | die Antwort, die dieses Kommando hat (`hitec_c_decode()`); nur classic |
| 2 | das gelesene Register der D-Serie | die Antwort dieses Registers (`hitec_d_decode()`); nur D-Serie |
| 3 | das erwartete Byte | ein Byte und nichts danach (`hitec_listen_decode()`) |

Die Ergebniscodes der Dekoder sind `hitec_rx_t`: 0 OK, 1 ARG, 2 ABORTED,
3 OVERFLOW, 4 SILENT, 5 EARLY, 6 LATE, 7 NAK, 8 LENGTH, 9 FRAMING, 10 GAP,
11 ACK, 12 HEADER, 13 ADDRESS, 14 SUM.

## POWER

POWER braucht einen PD mini, der antwortet ([SUPPLY](Link-de.md)). Es:

1. verlangt die Versorgung aus;
2. wartet, bis sie aus gelesen wird, höchstens 3000ms, sonst Ergebnis 3
   (`SUPPLY`);
3. hält sie für die bereitgestellten 0 bis 10.000ms aus;
4. verlangt sie an mit der bereitgestellten Spannung und Stromgrenze;
5. startet im selben Durchlauf das bereitgestellte LISTEN.

Die Spannung ist 4800 bis 6000mV, bis eine Session das Servomodell kennt.
Die Stromgrenze ist 50 bis 3000mA. Die Spannung plus 700mV muss unter dem
Eingang des PD mini liegen, wenn der Eingang bekannt ist. Ein Sollwert
außerhalb davon wird mit `SUPPLY_RANGE` abgewiesen. Eine Versorgung, die das
Einschalten abweist, gibt Ergebnis 3; ein LISTEN, das der Treiber abweist,
gibt Ergebnis 4 (`START`) bei eingeschalteter Versorgung. Die SUPPLY-Page
ist währenddessen und danach gehalten, bis CLOSE
([Die Versorgung, solange ein Port gehalten ist](#die-versorgung-solange-ein-port-gehalten-ist)).

Ein Ausschalten der SUPPLY-Page wartet auf ein Byte, das auf der Leitung
sein kann, höchstens 12ms: Vorlauf und Bytes eines SEND, und ein LISTEN
oder das LISTEN eines POWER, das antwortet, ganz, weil die Antwort geht,
sobald das erste Byte gekommen ist.

## STOP, Schalten auf SAFE, Heartbeat-Verlust und Link-Verlust

Ein Stopp ist der verlorene Heartbeat, der stille Link oder das Schalten der
Bank auf SAFE. Bei einem Stopp:

- wird eine laufende Transaktion abgebrochen: das Pad wird sofort
  losgelassen, und der Mitschnitt hält die Bytes bis dahin, als abgebrochen
  markiert (Ergebnis 6, `CUT`);
- endet ein POWER in seiner Auszeit dort, und die Versorgung bleibt aus;
  ein POWER, dessen LISTEN läuft, wird wie eine Transaktion abgeschnitten,
  und die Versorgung ist, wie die SUPPLY-Page sie hat: aus nach verlorenem
  Heartbeat oder stillem Link, an nach einem Schalten auf SAFE allein;
- bleibt der Port mit dem Pin als losgelassenem Eingang gehalten, so wie ein
  KST-Port in PROGRAMMING bleibt, und mit ihm das Halten der SUPPLY-Page;
- ist ein Port, der noch in STOPPING steht, wieder frei;
- wird Bit 3 von `STATE` (`STOPPED`) gesetzt.

Solange `STOPPED` gesetzt ist, werden SEND, LISTEN, LEVEL und POWER mit
`STOPPED` abgewiesen. CLEAR löscht es, solange dem Heartbeat vertraut wird
und der Link nicht still ist; CLOSE und ein neues OPEN löschen es auch.
Solange dem Heartbeat nicht vertraut wird oder der Link still ist, zählt
jeder Durchlauf als Stopp, und jedes Kommando außer ABORT und CLOSE wird mit
`UNSAFE` abgewiesen. Die Bank schaltet nicht auf ARMED, solange die Page
einen Port hält, und OPEN wird abgewiesen, solange sie ARMED ist.

## Register

Ein Lesen gibt diese Register:

| Register | Name | Liest |
| ---: | --- | --- |
| 0 | `STATE` | Bits 0 bis 2 der Zustand: 0 FREE, 1 STOPPING, 2 OPEN, 3 BUSY, 4 POWER_OFF. Bit 3 `STOPPED`. Bit 4 ein PD mini antwortet. Bit 5 die Page hält einen Mitschnitt. Bit 6 die letzte Transaktion hat ein Byte auf die Leitung gelegt. Bit 7 `RX_MODE` ist PRECHARGE (4.24). Bits 12 bis 15 der Port |
| 1 | `SEQ` | die `SEQ` des letzten angenommenen Kommandos; 0 beim Start |
| 2 | `OP` | Bits 12 bis 15 die letzte gestartete Operation, Bits 0 bis 11 die seit OPEN gestarteten Transaktionen, bei 4095 gehalten |
| 3 | `RESULT` | niederwertiges Byte das Ergebnis dieser Operation: 0 keins, 1 läuft, 2 fertig, 3 `SUPPLY`, 4 `START`, 5 `NO_LINE`, 6 `CUT`. Höherwertiges Byte, warum das letzte angenommene Kommando nichts gestartet hat, 0 wenn es gestartet hat |
| 4 | `CAP` | Bits 0 bis 4 die gehaltenen Bytes. Bit 8 der Empfang endete bei seiner Anzahl, Bit 9 die Antwort ging hinaus, Bit 10 Überlauf, Bit 11 abgebrochen. Bits 12 und 13 der Dekoder |
| 5 | `DEC` | niederwertiges Byte das Ergebnis des Dekoders, höherwertiges Byte die ID der Antwort der D-Serie |
| 6 | `VALUE` | der dekodierte Wert |
| 7 | `VIEW` | was Register 16 bis 31 zeigen |
| 8 bis 15 | `BYTES` | Byte 2 k im niederwertigen Byte von Register 8 + k, 2 k + 1 im höherwertigen; 0 hinter den gehaltenen Bytes |
| 16 bis 31 | `DATA` | `VIEW` 0: die Startbits der Bytes 0 bis 7, ns, je 32 Bit, niederwertige Hälfte zuerst. `VIEW` 1: Bytes 8 bis 15. `VIEW` 2: der Lauf, unten |

`VIEW` 2:

| Register | Liest |
| ---: | --- |
| 16, 17 | die Verzögerung des Dekoders, ns |
| 18, 19 | von der ersten Flanke des Vorlaufs bis zum Loslassen der Leitung, ns: der Vorlauf, wie das PIO-Programm ihn gefahren hat, und jede gesendete Bitzeit; ein Lesen der D-Serie 559.886 |
| 20, 21 | vom Start bis zum Mitschnitt, us |
| 22, 23 | die längste Zeit, die die Transaktion dauern konnte, us |
| 24 | die Bitzeit der offenen Leitung, ns; 0, solange keine offen ist |
| 25 | die Leitung: Bit 0 die Familie (0 classic, 1 D-Serie), Bit 4 der Treiber (0 Open Drain, 1 Push-Pull), Bit 8 sie empfängt per PRECHARGE (4.24) |
| 26 | der Pin des Ports; 0xFFFF, solange der Treiber keinen hält |
| 27, 28 | das Fenster, das der Mitschnitt verlangt hat, us |
| 29 | Bit k: das Stoppbit von Byte k wurde auf dem Startpegel abgetastet |

Ein Schreiben sieht andere Register:

| Register | Name | Nimmt |
| ---: | --- | --- |
| 0 | `CMD` | Bits 0 bis 3 die Operation: 1 OPEN, 2 CLOSE, 3 SEND, 4 LISTEN, 5 LEVEL, 6 ABORT, 7 POWER, 8 CLEAR. Bits 8 bis 11 der Port. Jedes andere Bit 0 |
| 1 | `SEQ` | die `SEQ` der Page plus 1, modulo 65536 |
| 2 | `KEY` | 0x48A5 für eine Operation, die ein Byte auf die Leitung legt, sonst 0 |
| 3 | `CRC` | die Prüfsumme über Register 4 bis 21, für jede Operation außer ABORT, CLOSE und CLEAR |
| 4 | `LINE` | OPEN: Bit 0 die Familie, Bit 4 der Treiber. Classic ist nur Open Drain |
| 5 | `NTX` | SEND: niederwertiges Byte die zu sendenden Bytes, 1 bis 8; höherwertiges Byte die Ruhe-Bitzeiten zwischen zweien, 0 bis 6 |
| 6 bis 9 | `TX` | Byte 2 k im niederwertigen Byte von Register 6 + k, 2 k + 1 im höherwertigen |
| 10, 11 | `LEAD` | SEND: der Vorlauf, us, niederwertige Hälfte zuerst |
| 12, 13 | `WINDOW` | SEND, LISTEN, POWER: das Fenster, us |
| 14 | `RX` | niederwertiges Byte die zu empfangenden Bytes, 0 bis 16, 0 nur ohne Fenster; Bit 8 das erste Byte beantworten (LISTEN, POWER) |
| 15 | `ANSWER` | niederwertiges Byte die Antwort; Bit 8 der Pegel eines LEVEL |
| 16, 17 | `LEVEL` | LEVEL: wie lange, us |
| 18 | `DECODE` | Bits 0 und 1 der Dekoder, Bits 8 bis 15 sein Argument; Bits 2 bis 7 0 |
| 19 | `SUPPLY_MV` | POWER: 4800 bis 6000mV |
| 20 | `SUPPLY_MA` | POWER: 50 bis 3000mA |
| 21 | `OFF_MS` | POWER: 0 bis 10.000ms |
| 22 | `VIEW` | 0 bis 2. Gilt, sobald geschrieben, und fällt nicht unter die Prüfsumme |
| 23 | `RX_MODE` | 0 PULL-UP, 1 PRECHARGE: wie eine Leitung der D-Serie empfängt, ab dem nächsten OPEN (4.24). Fällt nicht unter die Prüfsumme; 0 nach einem Neustart |

Die Prüfsumme ist die CRC-16 des Links (Cyclic Redundancy Check), mit
0xFFFF begonnen, über das niederwertige und dann das höherwertige Byte jedes
Registers (`link_kst_crc()`), wie auf der KST-Page.

Register 0 bis 3 sind das Kommando: ein Frame, geschrieben als genau diese 4
Register und ganz oder gar nicht angenommen. Register 4 bis 23 werden vor dem
Kommando in beliebigen Fenstern geschrieben. Ein Schreiben, das Register 24
oder höher erreicht, wird mit READ_ONLY abgewiesen, ein `VIEW` über 2 oder
ein `RX_MODE` über 1 mit BAD_VALUE, und das Fenster ändert nichts.

Ein Frame mit einer anderen `SEQ` als der der Page plus 1, einer Operation 0
oder über 8, einem gesetzten Bit außerhalb von Operation und Port oder einem
`KEY`, der weder 0 noch 0x48A5 ist, wird mit BAD_VALUE abgewiesen und ändert
nichts. Der eben angenommene Frame, noch einmal gesendet, wird quittiert und
tut nichts. Jeder andere Frame wird quittiert; was aus ihm wurde, steht in
`RESULT`.

## Abweisungen

Das höherwertige Byte von `RESULT`. Ein Port, den der Build nicht hat, wird
vor jeder anderen Prüfung mit `PORT` abgewiesen, ABORT ausgenommen. Dann, in
dieser Reihenfolge für eine Transaktion:

| Code | Name | Warum |
| ---: | --- | --- |
| 7 | `NO_KEY` | ein SEND ohne Schlüssel, vor jeder anderen Prüfung; ein LISTEN oder POWER mit Antwort, nach der Bereichsprüfung |
| 6 | `NOT_OPEN` | der Port ist frei |
| 1 | `BUSY` | eine Transaktion oder ein POWER läuft, der Port steht in STOPPING, oder OPEN auf einem gehaltenen Port |
| 2 | `PORT` | kein solcher Programmierport, oder nicht der gehaltene |
| 9 | `STOPPED` | seither ein Stopp; erst CLEAR |
| 5 | `UNSAFE` | dem Heartbeat wird nicht vertraut, oder der Link ist still |
| 8 | `STAGED` | Register 4 bis 21 haben nicht die Prüfsumme, die `CRC` nennt |
| 11 | `RANGE` | die bereitgestellte Transaktion ist keine, die die Leitung fährt, der Dekoder passt nicht zur Familie, oder ein reserviertes Bit ist gesetzt; auf einer PRECHARGE-Leitung auch ein LISTEN oder POWER, das antwortet, oder ein Empfangsfenster über 30.000us |
| 13 | `NO_SUPPLY` | POWER ohne einen PD mini, der antwortet |
| 12 | `SUPPLY_RANGE` | POWER: ein Sollwert oder die Auszeit außerhalb des Bereichs |
| 3 | `HELD` | OPEN: die KST-Page hält den Port |
| 4 | `ARMED` | OPEN: die Bank ist ARMED |
| 10 | `NO_PIN` | OPEN: der Port hat keinen Pin, oder seine FUNC ist DShot (seit 4.28): an dieser Leitung hängt ein ESC |

Die KST-Page weist ENTER und ein REBOOT aus PWM mit ihrem `BUSY` (1) ab,
solange diese Page den Port hält.

## Eine Transaktion von Hand

Ein classic-Lesen der EEPROM-Adresse 0x12 auf Port 0, roh geschrieben, wie
die Prüfstandskonsole ([Die Prüfstandskonsole](BenchConsole-de.md)) es
nimmt. Das Kommando ist 0x61 0x12 0x00, sein Summenbyte 0x8D; der
classic-Dekoder (1) ist für Kommando 0x61 genannt. Die Page steht auf `SEQ`
0.

```text
w 0x34 4 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
w 0x34 0 0x0001 1 0 0x8135
r 0x34 0 4
w 0x34 4 0 0x0104 0x1261 0x8D00 0 0 0 0 0x05DC 0 2 0 0 0 0x6101 0 0 0
w 0x34 0 0x0003 2 0x48A5 0xE56A
r 0x34 0 16
```

Die ersten beiden Zeilen öffnen die Leitung, classic und Open Drain; die
dritte liest nach 50ms `STATE` 2 (OPEN). Die vierte stellt 4 Bytes mit 1
Ruhebit dazwischen, ein Fenster von 1500us für 2 Bytes und den Dekoder
bereit; die fünfte sendet sie mit dem Schlüssel. 0x8135 und 0xE56A sind die
Prüfsummen der beiden bereitgestellten Blöcke. `hitec open 0 c` und
`hitec c 0 0x61 0x12 0` der Konsole tun dasselbe und rechnen `SEQ` und die
Prüfsummen selbst.

## Die Link-Hälfte des Panels

`shared/bench/hitec_link.c` macht aus einem Schritt einer Session Frames
dieser Page und aus der Antwort der Page wieder das Ergebnis des Schritts
(`hitec_ses_result_t`). Es nimmt eine Anfrage zur Zeit: OPEN, einen Schritt
(SEND, LISTEN oder LEVEL, oder POWER mit den Sollwerten des Aufrufers),
CLEAR, oder CLOSE eines beliebigen Ports, den die Page hält, auch eines
Ports, den ein anderer Schreiber geöffnet hat. Jeden Austausch macht der
Aufrufer; ein Durchlauf macht höchstens 4.

Eine Anfrage geht in dieser Reihenfolge hinaus:

1. OPEN und ein Schritt: die Register 4 bis 22 in 5 Frames zu 4, 4, 4, 4
   und 3 Registern. `VIEW` (22) ist immer 2.
2. Das Kommando, Register 0 bis 3: `SEQ` eins über dem der Page, die
   Prüfsumme der Register 4 bis 21 und der Schlüssel bei jedem SEND und bei
   einem LISTEN oder POWER, das antwortet. Ein Kommando ohne Antwort geht
   unverändert noch einmal, 3 Versuche; die Page nimmt es einmal.
3. Register 0 bis 15: das Kommando hat die Operation gestartet, oder das
   hohe Byte von `RESULT` sagt, warum nicht. Danach alle 5ms, bis der Port
   aus STOPPING, BUSY und POWER_OFF heraus ist. Das letzte Lesen enthält
   den Mitschnitt.
4. Register 0 bis 31, einmal. Die Register 16 bis 31 gelten als der Lauf,
   wenn dasselbe Lesen `VIEW` 2 und `SEQ`, `OP` und `CAP` des letzten
   Lesens aus Schritt 3 zeigt: `VIEW` wird ohne Kommando geschrieben, ein
   anderer Schreiber kann es also ändern, ohne dass `SEQ` es zeigt. Sonst
   trägt das Ergebnis keinen Lauf, und Pin und Leitung von vorher
   bleiben.

| Die Page | Das Ergebnis des Schritts |
| --- | --- |
| `RESULT` 2, DONE | RAN, mit dem niedrigen Byte von `DEC` und `VALUE` |
| `RESULT` 6, CUT | CUT |
| eine Abweisung, ein abgewiesener Frame, `SUPPLY`, `START`, keine Antwort, ein Neustart | FAILED |

| Fall | Austausche | Durchläufe zu 5ms |
| --- | ---: | ---: |
| ein classic-Lesen | 9 | 3 |
| ein Lesen der D-Serie | 10 | 4 |
| ein volles Lesen der D-Serie, 21 Register, 20ms Abstand | - | 840ms insgesamt |

Die Zahlen stammen aus dem Host-Modell (`test_hitec_link`,
`test_hitec_link_run`); auf dem Prüfstand sind sie nicht gemessen.

Wenn der Link nicht antwortet:

- 3 Austausche hintereinander ohne Antwort geben die Anfrage auf.
- Ein Kommando, dessen 3 Versuche ohne Antwort bleiben, geht nicht noch
  einmal. Das nächste Lesen der Register 0 bis 15 entscheidet: eine Page
  auf der `SEQ` des Kommandos in einem Zustand, den das Kommando
  hinterlässt, hat es genommen, eine Page auf der `SEQ` davor nicht. Der
  passende Zustand: eine Abweisung; bei CLOSE der Port frei oder ein
  anderer Port gehalten; bei CLEAR der Port gehalten mit `STOPPED`
  gelöscht; bei den übrigen die Operation in `OP` auf dem Port der
  Anfrage, mit geändertem Zähler in `OP`, Zustand oder Ergebnis gegenüber
  dem Lesen davor. Die `SEQ` des Kommandos in einem Zustand, der nicht
  passt, ist das Kommando eines anderen Schreibers auf dieser `SEQ`. Eine Page auf `SEQ` 0 mit Operation NONE und ohne Abweisung
  ist neu gestartet, auch wenn die `SEQ` des Kommandos 0 ist, die nach
  65535.
- Eine andere `SEQ`, oder `OP` zurück auf NONE, ist ein IOmcu, der neu
  gestartet ist, oder das Kommando eines anderen Schreibers. Eine Anfrage
  unterwegs endet dort, FAILED.
- Eine Anfrage, die 25.000ms, nachdem ihr Kommando genommen wurde, noch
  läuft, bricht das Panel ab. Die Page beendet jede Transaktion innerhalb
  von 10.000ms und ein POWER innerhalb von 23.000ms; die Grenze ist für
  eine Page, die das nicht tut.

ABORT verwirft eine Anfrage, deren Kommando noch nicht hinaus ist. Ein
Kommando, das ohne Antwort hinausging, geht nicht noch einmal; die `SEQ`
entscheidet wie oben. Das Kommando ABORT geht, sobald die Page bekannt ist;
eines, das mit BAD_VALUE abgewiesen wird, geht nach einem Lesen noch einmal,
mit der `SEQ` der Page. Eine Anfrage, die verworfen wird, während einer
ihrer Austausche unterwegs ist, hält die nächste Anfrage zurück, bis die
Antwort dieses Austauschs da ist.

Zwischen den Anfragen werden die Register 0 bis 15 alle 100ms gelesen,
solange die Page einen Port hält, und alle 500ms, solange sie keinen hält.
Einem IOmcu unter 4.23 wird nichts gesendet. Die Kopie für den Bildschirm
nennt den Zustand, den Port, die Flags (`STOPPED`, ein antwortendes PD
mini, `PRECHARGE`), den Pin und die Leitung, und ob dieses Modul den
gehaltenen Port geöffnet hat, seit der Link kam. Ein Port, der über einen
Link-Verlust, einen Neustart des Panels oder nach dem Kommando eines
anderen Schreibers gehalten ist, ist nicht der des Moduls. CLOSE gibt ihn
frei, aus jedem Zustand. CLEAR wird mit `UNSAFE` abgewiesen, solange der
IOmcu dem Heartbeat nicht traut. Das Halten von SUPPLY ab 4.29 ist Bit 11
der FLAGS von SUPPLY, das das SUPPLY-Modul des Panels liest
(`shared/bench/supply_link.c`).
