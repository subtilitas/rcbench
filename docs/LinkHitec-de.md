# Der Programmierport für Servos von Hitec

<sub>[English](LinkHitec.md) · **Deutsch**</sub>

Diese Seite beschreibt den Programmierport für Hitec-Servos.
[Der Link](Link-de.md) behandelt die Verkabelung, das Frame-Format, die
Page Map und die übrigen Pages. Pins werden mit ihrer GPIO-Nummer
(General-Purpose Input/Output) bezeichnet. PWM steht für
Pulsweitenmodulation.

Hitec ist ein Name oder eine Marke seines Inhabers. Die Bezeichnung
kennzeichnet hier nur die Servos, mit denen die Page arbeitet.

Die Hitec-Page (`HITEC`, 0x34) benötigt Protokoll 4.23 oder höher.
Schreibregister 23 wählt den Empfangsmodus einer Leitung der D-Serie
und benötigt 4.24. Ab 4.29 reserviert ein belegter Port die SUPPLY-Page
von OPEN bis CLOSE
([Die Versorgung, solange ein Port gehalten ist](#die-versorgung-solange-ein-port-gehalten-ist)).
Ab 4.31 markiert ein Schreiber die Schritte eines Schreibens, denen weitere
folgen, und SUPPLY hält das OFF des Panels zurück, bis das Schreiben endet
([Ein OFF während eines Schreibens](#ein-off-während-eines-schreibens)).
Die Page führt rohe Transaktionen des Hitec-Servoprotokolls aus
[`protocols/hitec`](../protocols/hitec/README.md) auf einem
[PROG](Link-de.md#die-programmierports)-Port aus: classic (HS-5xxx,
HS-7xxx, 19.200bit/s) oder D-Serie (115.200bit/s). Sie verwaltet keine
Hitec-Session. Der Aufrufer bereitet eine Transaktion vor, die Page
führt sie aus. Mitschnitt und dekodiertes Ergebnis bleiben bis zur
nächsten Transaktion verfügbar. Der Aufrufer muss die Bytes und ihre
Reihenfolge passend zum Servo wählen. Der Session-Kern
([`hitec_session.h`](../protocols/hitec/README.md#session)) und die
Link-Hälfte des Panels ([Die Link-Hälfte des Panels](#die-link-hälfte-des-panels))
führen die Lese- und Schreibvorgänge der Programmierung auf der Seite
SERVO aus ([Servoprogrammierung für Hitec-Servos](HitecProgrammer-de.md)).
`shared/outputs/hitec_port.c` hält die Regeln.

Ein Port ist entweder frei, von dieser Page belegt oder von der KST-Page
belegt ([Der Programmierport für Servos von KST und Chaservo](LinkKST-de.md)).
Beide Pages können ihn nicht gleichzeitig belegen. Diese Page belegt
jeweils einen Port; die KST-Page kann einen anderen belegen. Die Belegung
wird nicht im Flash gespeichert. Nach einem Neustart des IOmcu ist der
Port frei.

Jeder Port des Builds kann geöffnet werden, ab Protokoll 4.26 sind es
4. Der Pin wird unabhängig von seiner FUNKTION übergeben: Das gilt für
den geparkten Pin eines Ports mit KEINE ebenso wie für einen
SERVO-PWM-Port. CLOSE gibt den Pin an diese Funktion zurück. Der
PIO- und DMA-Bedarf hängt von der Leitung ab, nicht vom gewählten Port.
Für die eine Leitung werden mit PRECHARGE höchstens 2 State Machines
und 3 DMA-Kanäle benötigt.

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

In OPEN startet ein Kommando eine Transaktion und wechselt zu BUSY.
Sobald der Treiber den Mitschnitt zurückgibt, steht der Port wieder in
OPEN. Überschreitet eine Transaktion ihre berechnete Höchstdauer um
1ms, beendet der Treiber sie. Der Mitschnitt behält alle bis dahin
empfangenen Bytes und wird als Überlauf markiert (`CAP` Bit 10).
Zusätzlich bricht die Page jede nach 10.000ms noch laufende Transaktion
ab.

CLOSE gibt den Port aus jedem Zustand außer frei zurück. Eine laufende
Transaktion wird abgebrochen, die Leitung geschlossen und der Pin an
den Generator zurückgegeben. Die Versorgung bleibt unverändert. ABORT
bricht den laufenden Vorgang ab, behält aber den Port.

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

### Ein OFF während eines Schreibens

Ab 4.31 läuft auf dieser Page ein Schreiben in den Speicher des Servos,
solange ein SEND läuft, dessen Frame schreibt (BUSY), und 150ms nach
seinem Ende (`HITEC_PORT_WRITE_TAIL_MS`), oder solange das zuletzt
angenommene Kommando seinen Schritt mit Bit 4 von `CMD` (MORE) gestartet
hat: weitere Schritte dieses Schreibens folgen. Ein Frame schreibt, wenn
er ein classic-WRITE_EEPROM (0x62) oder -WRITE_RAM (0x64) ist oder eine
Schreibanfrage der D-Serie (0x96, 7 Bytes), Speichern und Neustart
eingeschlossen; die Page erkennt es an den Bytes des Frames, mit oder ohne
Bit. Ein Lesen, ein MOVE, ein LISTEN, ein LEVEL und ein POWER schreiben
nichts. Der Schreiber setzt das Bit auf jedem Schritt eines Schreibens
außer dem letzten: SEND, LISTEN, LEVEL oder POWER. `STATE` Bit 8 zeigt,
dass das Bit steht. Jedes andere von der Page verarbeitete Kommando löscht
es, auch wenn die angeforderte Operation abgelehnt wird. Ein Stopp
und CLOSE löschen es ebenfalls.

Wie lange ein Servo nach einem schreibenden Frame Versorgung braucht, ist
nicht gemessen. Die 150ms liegen über der längsten Pause, die ein
Programmiergerät des Herstellers nach einem schreibenden Frame vor seinem
nächsten Frame lässt: 121 bis 128ms beim HPP-21 Plus zwischen zwei
classic-Einstellungen, 100ms beim HFP-30 vom Speichern der D-Serie bis zum
Neustart.

Schreibt das Panel während eines Servo-Schreibvorgangs OFF auf SUPPLY,
liefert das Ausgangsregister sofort 0. Der PD mini erhält OFF erst nach
Ende des Schreibvorgangs: 150ms nach der Transaktion des letzten
schreibenden Frames, spätestens 10.000ms nach OFF
(`SUPPLY_PAGE_WRITE_WAIT_MS`). Bis dahin sieht der Port seine Versorgung
als eingeschaltet und führt die Schreibschritte fort. Ohne laufenden
Schreibvorgang erreicht OFF den PD mini im nächsten Durchlauf. Eine
Abschaltung wegen fehlendem Heartbeat oder stillem Link wird nicht
verzögert.

Vom OFF des Panels bis CLOSE schaltet POWER die Versorgung weiterhin
aus. Sein ON wird jedoch mit RESULT 3 (SUPPLY) abgelehnt. Das Servo
bleibt deshalb ohne Versorgung (`hitec_port_writing()`, `supply_page.h`).

| Schreiben | Schritte mit Bit 4 | Letzter Schritt |
| --- | --- | --- |
| classic | jede Zelle und jede Ausgleichszelle (0x1C) außer der letzten; jeder Versuch eines Schritts außer dem dritten des letzten | der dritte Versuch der letzten Ausgleichszelle, oder der Versuch, der angenommen wurde |
| D-Serie | die Schreibvorgänge und das Speichern | der Neustart |

Ein classic-Schritt, dessen Antwort beim ersten oder zweiten Versuch kommt,
trägt das Bit; es steht deshalb nach der letzten Ausgleichszelle bis zum
nächsten Kommando: das erste Lesen des Zurücklesens, 2ms später, löscht es.
Ein Schreiben, das nach einem Schritt mit dem Bit in einer Störung endet,
lässt es stehen bis zum nächsten Kommando dieser Page oder bis zur Grenze
von 10.000ms. Ein IOmcu mit 4.30 weist ein Kommando mit Bit 4 mit BAD_VALUE
ab; das Panel setzt es erst ab 4.31.

## Empfangsmodus

Schreibregister 23 (`RX_MODE`) wählt, wie eine Leitung der D-Serie die
Servoantwort empfängt. OPEN verwendet den Registerwert zum Zeitpunkt
des Kommandos. Änderungen bei offener Leitung gelten erst beim nächsten
OPEN. Ein OPEN einer classic-Leitung
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
([Hitec-D-Empfang](HitecProgrammer-de.md#hitec-d-empfang)).
Was gemessen ist und was nicht:
[`protocols/hitec`](../protocols/hitec/README.md#receive-by-precharge-and-sample).

## Transaktionen

| Operation | Was auf die Leitung geht | Schlüssel |
| --- | --- | --- |
| SEND | der Vorlauf auf dem Ruhepegel (D-Serie: auf Low getrieben), 1 bis 8 Bytes mit 0 bis 6 Ruhe-Bitzeiten dazwischen, dann der Empfänger für das Fenster | immer |
| LISTEN | der Empfänger für das Fenster; mit Antwort ein Byte am Ende des Stoppbits des ersten empfangenen Bytes, und der Empfänger wieder ab dem Ende des Stoppbits der Antwort | mit Antwort |
| LEVEL | die Leitung 1us bis 4s auf einem Pegel gehalten, dann losgelassen. Open Drain treibt nur Low | nie |
| POWER | das Aus- und Einschalten der Versorgung unten, dann ein LISTEN ab dem Einschalten | mit Antwort |

Die Antwort dauert 10 Bitzeiten: Startbit, 8 Datenbits und Stoppbit. Ihr
Startbit beginnt 10,0 bis 10,13 Bitzeiten nach dem Startbit des ersten
empfangenen Bytes. Ab dem Ende des Antwort-Stoppbits wartet der
Empfänger wieder auf ein Startbit. Er empfängt damit auch ein Byte,
das das Servo unmittelbar nach der Antwort sendet.

Vorlauf, Fenster und Pegel können bis zu 4.000.000us dauern. Ein
Mitschnitt enthält bis zu 16 Bytes. Das Fenster zählt nur die Wartezeit
auf ein Startbit. Für jedes empfangene Byte werden der Startbit-Zeitpunkt
in ns seit Empfängerstart und der abgetastete Wert des Stoppbits
gespeichert.

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

POWER benötigt einen antwortenden PD mini ([SUPPLY](Link-de.md)) und
führt diese Schritte aus:

1. verlangt die Versorgung aus;
2. wartet, bis sie aus gelesen wird, höchstens 3000ms, sonst Ergebnis 3
   (`SUPPLY`); der Treiber des PD mini liest den Zustand 250ms nach einem
   Ausschalten, das Warten dauert also 250ms oder mehr;
3. hält sie für die bereitgestellten 0 bis 10.000ms ab dieser Lesung aus;
4. verlangt sie an mit der bereitgestellten Spannung und Stromgrenze;
5. startet im selben Durchlauf das bereitgestellte LISTEN.

Die Auszeit besteht aus der vorgegebenen Dauer, den 250ms bis zum
Auslesen und den Lesezugriffen des Treibers vor dem Einschalten. Bei
den klassischen 500ms sind das etwa 760ms; an einem Modul wurden
762ms gemessen. Die eigene Schaltzeit des Moduls ist nicht gemessen.

Die Spannung ist 4800 bis 6000mV, bis eine Session das Servomodell kennt.
Die Stromgrenze ist 50 bis 3000mA. Die Spannung plus 700mV muss unter dem
Eingang des PD mini liegen, wenn der Eingang bekannt ist. Ein Sollwert
außerhalb davon wird mit `SUPPLY_RANGE` abgewiesen. Eine Versorgung, die das
Einschalten abweist, gibt Ergebnis 3; ein LISTEN, das der Treiber abweist,
gibt Ergebnis 4 (`START`) bei eingeschalteter Versorgung. Die SUPPLY-Page
ist währenddessen und danach gehalten, bis CLOSE
([Die Versorgung, solange ein Port gehalten ist](#die-versorgung-solange-ein-port-gehalten-ist)).

Ein OFF auf der SUPPLY-Page wartet bis zu 12ms auf eine mögliche
Byte-Übertragung. Das umfasst den Vorlauf und die Bytes eines SEND
sowie das gesamte LISTEN eines LISTEN- oder POWER-Vorgangs mit
Antwort. Bei Letzteren muss gewartet werden, weil die Antwort erst
nach Empfang des ersten Bytes gesendet wird.

## STOP, Schalten auf SAFE, Heartbeat-Verlust und Link-Verlust

Ein verlorener Heartbeat, ein stiller Link oder das Schalten der Bank
auf SAFE löst einen Stopp aus. Bei einem Stopp:

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
einen Port hält: ein Schreiben von ARM ungleich 0 wird mit NOT_ARMED abgewiesen,
eine 0 wird angenommen, und bei jeder Rückgabe des Ports sind die Register ARM und
THROTTLE 0. OPEN wird abgewiesen, solange die Bank ARMED ist.

## Register

Ein Lesen gibt diese Register:

| Register | Name | Liest |
| ---: | --- | --- |
| 0 | `STATE` | Bits 0 bis 2 der Zustand: 0 FREE, 1 STOPPING, 2 OPEN, 3 BUSY, 4 POWER_OFF. Bit 3 `STOPPED`. Bit 4 ein PD mini antwortet. Bit 5 die Page hält einen Mitschnitt. Bit 6 die letzte Transaktion hat ein Byte auf die Leitung gelegt. Bit 7 `RX_MODE` ist PRECHARGE (4.24). Bit 8 MORE: das zuletzt angenommene Kommando hat seinen Schritt mit `CMD` Bit 4 gestartet (4.31). Bits 12 bis 15 der Port |
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
| 0 | `CMD` | Bits 0 bis 3 die Operation: 1 OPEN, 2 CLOSE, 3 SEND, 4 LISTEN, 5 LEVEL, 6 ABORT, 7 POWER, 8 CLEAR. Bit 4 MORE bei SEND, LISTEN, LEVEL und POWER: weitere Schritte dieses Schreibens folgen (4.31). Bits 8 bis 11 der Port. Jedes andere Bit 0 |
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

Register 0 bis 3 bilden das Kommando. Schreibe genau diese 4 Register
in einem Frame; die Page übernimmt das Kommando vollständig oder gar
nicht. Register 4 bis 23 lassen sich vorher in beliebigen Fenstern
schreiben. Ein Schreibzugriff auf Register 24 oder höher wird mit
READ_ONLY abgelehnt. Ein `VIEW` über 2 oder ein `RX_MODE` über 1 wird
mit BAD_VALUE abgelehnt. In jedem dieser Fälle lässt das gesamte
Schreibfenster die Register unverändert.

Ein Frame mit einer anderen `SEQ` als der der Page plus 1, einer Operation 0
oder über 8, einem gesetzten Bit außerhalb von Operation, Bit 4 und Port,
Bit 4 bei OPEN, CLOSE, ABORT oder CLEAR, oder einem `KEY`, der weder 0
noch 0x48A5 ist, wird mit BAD_VALUE abgewiesen und ändert nichts. Wird der zuletzt angenommene Frame erneut gesendet, wird er bestätigt,
aber nicht erneut ausgeführt. Andere gültige Frames werden bestätigt;
ihr Ergebnis steht in `RESULT`.

## Abweisungen

Das höherwertige Byte von `RESULT` nennt den Ablehnungsgrund. Außer
bei ABORT wird ein im Build nicht vorhandener Port vor allen anderen
Prüfungen mit `PORT` abgelehnt. Danach folgen die Transaktionsprüfungen
in dieser Reihenfolge:

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

`shared/bench/hitec_link.c` wandelt einen Session-Schritt in Frames
dieser Page um und deren Antwort in das Schrittergebnis
(`hitec_ses_result_t`). Das Modul verarbeitet jeweils eine Anfrage:
OPEN, einen Schritt (SEND, LISTEN, LEVEL oder POWER mit den Sollwerten
des Aufrufers), CLEAR oder CLOSE eines von der Page belegten Ports.
CLOSE ist auch für einen von einem anderen Schreiber geöffneten Port
möglich. Der Aufrufer führt die Austausche aus, höchstens 4 je Durchlauf.

Eine Anfrage geht in dieser Reihenfolge hinaus:

1. OPEN und ein Schritt: die Register 4 bis 22 in 5 Frames zu 4, 4, 4, 4
   und 3 Registern. `VIEW` (22) ist immer 2.
2. Das Kommando, Register 0 bis 3: `SEQ` eins über dem der Page, die
   Prüfsumme der Register 4 bis 21 und der Schlüssel bei jedem SEND und bei
   einem LISTEN oder POWER, das antwortet. Bit 4 geht mit einem Schritt,
   den die Session `more` markiert, nur an einen IOmcu ab 4.31
   ([Ein OFF während eines Schreibens](#ein-off-während-eines-schreibens)).
   Ein Kommando ohne Antwort geht unverändert noch einmal, 3 Versuche; die
   Page nimmt es einmal.
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

ABORT verwirft eine Anfrage, deren Kommando noch nicht gesendet wurde.
Ein bereits gesendetes Kommando ohne Antwort wird nicht erneut gesendet;
seine `SEQ` entscheidet wie oben über das Ergebnis. Das Kommando ABORT
wird gesendet, sobald der Zustand der Page bekannt ist. Nach einer
Ablehnung mit BAD_VALUE liest das Modul die Page und wiederholt ABORT
mit deren `SEQ`. Wird eine Anfrage während eines laufenden Austauschs
verworfen, wartet die nächste Anfrage auf dessen Antwort.

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
