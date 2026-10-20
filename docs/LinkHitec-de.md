# Der Programmierport für Servos von Hitec

<sub>[English](LinkHitec.md) · **Deutsch**</sub>

Diese Seite ist ein Abschnitt von [Der Link](Link-de.md), der die Verkabelung, das
Frame-Format, die Page Map und die übrigen Pages enthält. Pins heißen nach ihrer
GPIO-Nummer (General-Purpose Input/Output). PWM steht für
Pulsweitenmodulation.

Hitec ist ein Name oder eine Marke seines Inhabers. Er steht hier nur, um zu
sagen, mit welchen Servos die Page arbeitet.

Die Hitec-Page (`HITEC`, 0x34) ist neu in 4.23; das Schreibregister 23, der
Empfangsmodus einer Leitung der D-Serie, ist neu in 4.24. Die Page fährt rohe Transaktionen
der Hitec-Servoleitung aus [`protocols/hitec`](../protocols/hitec/README.md)
auf einem Port von [PROG](Link-de.md#die-programmierports): classic (HS-5xxx,
HS-7xxx, 19.200 bit/s) oder D-Serie (115.200 bit/s). Die Page hält keine
Hitec-Session. Der Schreiber stellt eine Transaktion bereit, die Page fährt
sie, und der Mitschnitt und seine Dekodierung bleiben bis zur nächsten auf
der Page. Welche Bytes ein Servo nimmt und in welcher Reihenfolge, ist Sache
des Schreibers. Eine Session auf dem Panel ist nicht gebaut.
`shared/outputs/hitec_port.c` hält die Regeln.

Ein Port ist frei, von dieser Page gehalten oder von der KST-Page gehalten
([Der Programmierport für Servos von KST und Chaservo](LinkKST-de.md)), nie
zwei davon. Nichts wird im Flash gehalten: nach einem Neustart des IOmcu ist
der Port frei.

## Hinein und hinaus

OPEN nimmt den Port:

1. STOPPING, 50 ms: der Port gibt keinen Puls aus, ein laufender Puls endet.
2. Der PWM-Generator gibt den Pin ab. Der Leitungstreiber öffnet die Leitung
   darauf als losgelassenen Eingang: classic mit dem Pull-down des Pads, die
   D-Serie mit seinem Pull-up und invertiertem Eingang. Die internen Pulls
   haben 50 bis 80 kOhm; die D-Serie empfängt mit einem Pull-up auf der
   Platine oder per PRECHARGE ([Empfangsmodus](#empfangsmodus)). Getestet
   mit einem D485HW: mit dem Pull-up des Pads allein bleibt die Leitung
   unter 1,4 V und der Servo beantwortet kein Lesen; mit 2,2 kOhm von der
   Signalleitung zur Servoversorgung beantwortet er jedes Lesen. Die
   Spannungen auf der Leitung, und ob der High-Pegel des Servos in der
   Belastbarkeit des Pins bleibt, sind nicht gemessen
   ([Electrical](../protocols/hitec/README.md#electrical)).
3. OPEN. Ein Treiber, der keine PIO-State-Machine (PIO: Programmable
   Input/Output), keine 29 Worte Programmspeicher oder keine 2 DMA-Kanäle
   (Direct Memory Access) bekommt, lässt den Port mit Ergebnis 5 (`NO_LINE`)
   frei. PRECHARGE braucht eine zweite State-Machine, bei 150 MHz 7 Worte
   Programmspeicher in einem anderen PIO-Block und einen dritten DMA-Kanal;
   ohne sie bleibt der Port ebenfalls mit `NO_LINE` frei.

In OPEN startet ein Kommando eine Transaktion (BUSY). Der Port steht wieder
in OPEN, wenn der Treiber den Mitschnitt zurückgibt. Der Treiber beendet eine
Transaktion selbst 1 ms nach der längsten Zeit, die sie dauern kann; eine
Transaktion, die nach 10.000 ms noch läuft, bricht zusätzlich die Page ab.

CLOSE gibt den Port aus jedem Zustand außer frei zurück: eine laufende
Transaktion wird abgeschnitten, die Leitung geschlossen und der Pin gehört
dem Generator. Die Versorgung bleibt, wie sie ist. ABORT schneidet ab, was
läuft, und behält den Port.

## Empfangsmodus

Das Schreibregister 23 (`RX_MODE`) sagt, wie eine Leitung der D-Serie die
Antwort des Servos empfängt, ab dem nächsten OPEN. Das OPEN nimmt den Wert,
den das Register hält, wenn das OPEN geschrieben wird; eine Änderung bei
offener Leitung wartet auf das nächste OPEN. Ein OPEN einer classic-Leitung
übergeht das Register: ein classic-Servo treibt seinen High-Pegel selbst,
über etwa 1 kOhm, und antwortet an einem Open-Drain-Port ohne Pull-up und
ohne Precharge (gemessen an einem HS-7955TG, einem HS-5955TG und einem
HS-5645MG bei 5,0 V Versorgung; die Leitungsspannungen sind nicht gemessen).

| Wert | Name | Wie die Antwort empfangen wird |
| ---: | --- | --- |
| 0 | `PULL-UP` | die Leitung losgelassen; ein Pull-up von 2,2 kOhm von der Signalleitung zur Servoversorgung hebt sie. Die Voreinstellung und der Wert nach einem Neustart des IOmcu |
| 1 | `PRECHARGE` | kein Pull-up. Alle 1086,7 ns treibt der IOmcu das Pad 100,0 ns auf High, lässt es los und tastet es 113,3 ns nach dem Loslassen ab |

Auf einer PRECHARGE-Leitung:

- Ein SEND mit Empfangsfenster geht ohne Empfänger hinaus. Der erste
  Durchlauf der Hauptschleife des IOmcu, der die Anfrage beendet findet,
  gibt das Pad an eine zweite State-Machine: 2 mA Treiberstärke, langsame
  Flanke, Eingang nicht invertiert, der Pull-up des Pads wie zuvor. Ein
  DMA-Kanal schreibt die Abtastungen in den Speicher, und jeder Durchlauf
  dekodiert die seit dem letzten geschriebenen als invertiertes 8N1
  (`protocols/hitec/include/hitec_pre.h`).
- Ein LISTEN und das LISTEN eines POWER tasten ab ihrem Start ab.
- Der Mitschnitt ist der einer PULL-UP-Leitung: dieselben Register,
  dieselben Dekoder, dieselben Ergebniscodes. Die Zeit eines Bytes trägt
  den Start des Abtasters, abgelesen am Mikrosekunden-Timer des IOmcu,
  stimmt also auf 1 us; die Flanke eines Startbits liegt höchstens eine
  halbe Abtastung, 543 ns, neben der angegebenen Zeit.
- Mit `RANGE` abgewiesen: ein LISTEN oder POWER, das antwortet, und ein
  Empfangsfenster über 30.000 us.
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
  dessen Ausgang das ganze Fenster lang, höchstens 30.000 us, als aus
  gelesen wird, läuft bis zu seinem Ende, mit Ergebnis 2. Ein
  Empfang, der bei ausgeschalteter Versorgung oder ohne PD mini startete,
  läuft bis zu seinem Ende.

Bit 7 von `STATE` liest das Register; Register 25 von `VIEW` 2, Bit 8, sagt,
dass die offene Leitung per PRECHARGE empfängt. Ein IOmcu mit 4.23 weist
ein Schreiben, das Register 23 erreicht, mit READ_ONLY ab und empfängt mit
dem Pull-up; das Panel markiert dann die Einstellung unter SETUP
([ANSCHLÜSSE: der Programmierport](Screens-de.md#anschlüsse-der-programmierport)).
Was gemessen ist und was nicht:
[`protocols/hitec`](../protocols/hitec/README.md#receive-by-precharge-and-sample).

## Transaktionen

| Operation | Was auf die Leitung geht | Schlüssel |
| --- | --- | --- |
| SEND | der Vorlauf auf dem Ruhepegel (D-Serie: auf Low getrieben), 1 bis 8 Bytes mit 0 bis 6 Ruhe-Bitzeiten dazwischen, dann der Empfänger für das Fenster | immer |
| LISTEN | der Empfänger für das Fenster; mit Antwort ein Byte am Ende des Stoppbits des ersten empfangenen Bytes | mit Antwort |
| LEVEL | die Leitung 1 us bis 4 s auf einem Pegel gehalten, dann losgelassen. Open Drain treibt nur Low | nie |
| POWER | das Aus- und Einschalten der Versorgung unten, dann ein LISTEN ab dem Einschalten | mit Antwort |

Vorlauf, Fenster und Pegel reichen bis 4.000.000 us. Ein Mitschnitt hält bis
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
2. wartet, bis sie aus gelesen wird, höchstens 3000 ms, sonst Ergebnis 3
   (`SUPPLY`);
3. hält sie für die bereitgestellten 0 bis 10.000 ms aus;
4. verlangt sie an mit der bereitgestellten Spannung und Stromgrenze;
5. startet im selben Durchlauf das bereitgestellte LISTEN.

Die Spannung ist 4800 bis 6000 mV, bis eine Session das Servomodell kennt.
Die Stromgrenze ist 50 bis 3000 mA. Die Spannung plus 700 mV muss unter dem
Eingang des PD mini liegen, wenn der Eingang bekannt ist. Ein Sollwert
außerhalb davon wird mit `SUPPLY_RANGE` abgewiesen. Eine Versorgung, die das
Einschalten abweist, gibt Ergebnis 3; ein LISTEN, das der Treiber abweist,
gibt Ergebnis 4 (`START`) bei eingeschalteter Versorgung. Vom Ausschalten
bis zum Ende des LISTEN ist die SUPPLY-Page gehalten (Flag-Bit 11): das
Panel kann die Versorgung ausschalten, aber nicht einschalten und keinen
Sollwert verschieben.

Ein Ausschalten der SUPPLY-Page wartet auf ein Byte, das auf der Leitung
sein kann, höchstens 12 ms: Vorlauf und Bytes eines SEND, und ein LISTEN
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
  KST-Port in PROGRAMMING bleibt;
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
| 19 | `SUPPLY_MV` | POWER: 4800 bis 6000 mV |
| 20 | `SUPPLY_MA` | POWER: 50 bis 3000 mA |
| 21 | `OFF_MS` | POWER: 0 bis 10.000 ms |
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
| 11 | `RANGE` | die bereitgestellte Transaktion ist keine, die die Leitung fährt, der Dekoder passt nicht zur Familie, oder ein reserviertes Bit ist gesetzt; auf einer PRECHARGE-Leitung auch ein LISTEN oder POWER, das antwortet, oder ein Empfangsfenster über 30.000 us |
| 13 | `NO_SUPPLY` | POWER ohne einen PD mini, der antwortet |
| 12 | `SUPPLY_RANGE` | POWER: ein Sollwert oder die Auszeit außerhalb des Bereichs |
| 3 | `HELD` | OPEN: die KST-Page hält den Port |
| 4 | `ARMED` | OPEN: die Bank ist ARMED |
| 10 | `NO_PIN` | OPEN: der Port hat keinen Pin |

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
dritte liest nach 50 ms `STATE` 2 (OPEN). Die vierte stellt 4 Bytes mit 1
Ruhebit dazwischen, ein Fenster von 1500 us für 2 Bytes und den Dekoder
bereit; die fünfte sendet sie mit dem Schlüssel. 0x8135 und 0xE56A sind die
Prüfsummen der beiden bereitgestellten Blöcke. `hitec open 0 c` und
`hitec c 0 0x61 0x12 0` der Konsole tun dasselbe und rechnen `SEQ` und die
Prüfsummen selbst.
