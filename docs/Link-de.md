# Der Link

<sub>[English](Link.md) · **Deutsch**</sub>

**CAN mit 1 Mbit/s** zwischen Bedienteil und Koprozessor. Differenzielle
Signalübertragung neben 100–300 A Schaltstrom würde man ohnehin wählen;
Arbitrierung und Fehlerbehandlung in Hardware sind das, was es gegenüber dem
RS485, mit dem das anfing, zur richtigen Wahl gemacht hat.

Das Protokoll kopiert ArduPilots **IOMCU**, das genau dieses Problem seit einem
Jahrzehnt fliegt — ein kleiner Prozessor besitzt RC-Eingang und PWM-Ausgang,
während der große alles andere tut.

## Watchdogs

**Zwei, und der engere sitzt auf dem Koprozessor.** Das Verhältnis von IOMCU
lohnt sich zu kopieren: der Koprozessor füllt nach 200 ms Stille aus eigener
Befugnis Failsafe-Werte ein, während der Host nach einer Sekunde eskaliert — das
Ende, das die Ausgänge hält, ist also immer das misstrauischere.

**Zurückkehrender Verkehr hebt das Failsafe nicht auf.** Eine ankommende
Anfrage beweist, dass der Host lebt, und stoppt den Stillezähler, aber das
Failsafe ist gelatcht, und es zu verlassen erfordert das bewusste Schreiben
eines bekannten Wertes auf die Control-Page. Das sind verschiedene Tatsachen,
und sie zu vermengen ist der Weg, auf dem ein Prüfstand sich selbst wieder
scharf schaltet, während niemand hinsieht.

**Beide sind wrap-sicher**, und das wird an der Grenze getestet statt behauptet.
Der Fehlerfall ist nicht der naheliegende: `now >= then + timeout` wird in
32-Bit-Arithmetik ausgewertet, die Deadline wickelt also zusammen mit der Uhr um,
und die beiden stimmen *nach* dem Überlauf perfekt überein. Sie unterscheiden
sich **davor** — solange die Uhr noch groß ist und die Deadline schon klein
umgewickelt, meldet die naive Form den Timeout in dem Moment abgelaufen, in dem
die Deadline umwickelt: bis zu ein ganzes Intervall zu früh, bei scharfen
Ausgängen.

**Der Koprozessor schützt Hardware, ohne zu fragen** — Überstrom,
Übertemperatur, Stall-Timeout, verlorener Link — und meldet beim nächsten Poll,
was er getan hat. Er wartet nie auf die Erlaubnis, sicher zu versagen. Siehe
[Sicherheit](Safety-de.md).

## Die Leitung

**CAN mit 1 Mbit/s.** Der TWAI-Controller des Bedienteils gegen einen **XL2515**
(MCP2515-kompatibel, über SPI) hinter einem **SIT65HVD230**-Transceiver auf dem
Koprozessor.

Es begann als Half-Duplex-RS485, weil das Bedienteil das herausführt. Die
Platine hatte, wie sich zeigte, schon einen CAN-Pfad — ein FSUSB42UMX
multiplext ihn gegen USB — und das Koprozessormodul kam mit einem Controller
darauf an. Der Byte-Transport ist inzwischen **gelöscht**: sein Framing, sein
Decoder, beide UART-Treiber, die Richtungsbehandlung und ihre Suiten sind weg.
Was folgt, ist, was diese Änderung gebracht und was sie gekostet hat.

**Was sie löscht.** CAN arbitriert, statt sich abzuwechseln, es gibt also keine
Richtungsleitung. Alles, was daran hing, geht mit: das RC-Monoflop,
`LINK_TURNAROUND_US`, die Untergrenze von ~125 kBaud, die ungemessene Schwelle
von Q1 und die 5-V-Einschaltgefahr des Transceivers. Drei der sechs Fehler, die
das [Inbetriebnahmeverfahren](Bringup-de.md) auseinanderhalten sollte, sind nicht
mehr möglich, statt bloß leichter zu diagnostizieren. Der Controller bringt
außerdem eine 15-Bit-CRC, einen Acknowledge-Slot, automatische
Wiederholungssendung und Bus-off-Confinement mit, von denen nichts geschrieben
werden musste.

**Was sie bringt.** Arbitriert wird über den Identifier, der niedrigste gewinnt
— Priorität ist damit eine Eigenschaft der Adresse und nicht eines Schedulers.
Ein Schreiben auf die Control-Page schlägt jedes Telemetrie-Lesen *auf der
Leitung*, gegen Verkehr, der schon unterwegs ist, ohne Software an einem der
Enden. RS485 kann das bei keiner Baudrate versprechen, und das ist hier das
stärkste Argument.

**Was sie kostet.** Das TWAI des ESP32-S3 ist reines Classic CAN — kein FD,
1 Mbit/s als Obergrenze — und acht Datenbytes je Frame:

| | Nutzdaten |
| --- | ---: |
| Classic CAN, 1 Mbit/s, 29-Bit-IDs, ungünstigstes Stuffing | **52 kB/s** |
| Dasselbe mit 11-Bit-IDs | 62 kB/s |
| RS485 auf den angestrebten 1,5 MBaud | 133 kB/s |

Gegen die 12–30 kB/s, die tatsächlich fließen, fällt die Reserve vom Fünf- bis
Zwölffachen auf etwa das Doppelte. Das ist dünner und es reicht immer noch: ein
`bench_state`-Poll sind dreizehn Register, fünf Frames und **1,55 % des Busses
bei 20 Hz**, und ein 60-kB-Koprozessor-Image braucht 1,2 s.

### Das Mapping

`link_msg_t` wusste nie, was es trägt, der Dispatcher ändert sich also nicht.
Was sich ändert, ist das Framing — und der 29-Bit-Identifier fasst genau die
Felder, die die Nachricht schon hatte:

| Bits | Feld | |
| --- | --- | --- |
| 28..26 | Priorität | 3 Bit, niedriger gewinnt |
| 25..22 | op | ein `link_op_t` |
| 21..14 | Page | die Page Map, unverändert |
| 13..6 | Offset | erstes Register, das dieser Frame trägt |
| 5..0 | Count | Register, um die es in diesem Frame geht |

Daraus fallen drei Konsequenzen, und jede wiegt mehr, als sie aussieht:

**Ein Read hat keine Nutzdaten.** Die ganze Frage ist seine Adresse, Pollen
kostet also einen Frame mit null Bytes. Der Identifier wird arbitriert,
gleichgültig ob er etwas trägt.

**Nichts wird zusammengesetzt.** Jeder Frame trägt seinen eigenen Offset und
seinen eigenen Count, eine Antwort mit dreizehn Registern sind also vier
unabhängige Nachrichten und keine Folge. Ein verlorener Frame kostet einen
Registerbereich statt eines ganzen Transfers, es wartet kein Timer auf eine
Fortsetzung, die nicht kommt, und die Reihenfolge spielt keine Rolle — was
`test_link_can` prüft, indem es eine aufgeteilte Antwort rückwärts decodiert.

**Die Frame-CRC ist weg.** CAN hat eine in Silizium, mit Acknowledge-Slot und
Wiederholung dahinter. Zwei weitere Bytes mitzuführen hieße, ein Viertel einer
Acht-Byte-Nutzlast für eine Dopplung auszugeben. Ende-zu-Ende-Integrität über
etwas Größeres als eine Page — ein Firmware-Image — gehört zu diesem Transfer
und nicht zum Transport.

### Bit Timing, und die Zahl, die es entscheidet

Beide Controller teilen einen Takt in Time Quanta und zerlegen jedes Bit in ein
festes Sync-Quantum plus zwei programmierbare Segmente. Das falsch zu machen
scheitert nicht sauber: ein Knoten, der um Bruchteile eines Prozents daneben
liegt, funktioniert an einem kurzen Prüfstandskabel mit einem zweiten Knoten und
fängt an, Fehler zu protokollieren, sobald der Bus länger, kälter oder voller
wird. Die Arithmetik steht deshalb in `shared/can/can_timing.c` mit Tests statt
in einer Tabelle von Registerwerten aus einer Application Note.

Sie besteht darauf, dass die Bitrate **exakt** aufgeht — keine Näherung —, und
diese Regel ist es, die den Quarz vor allem anderen prüfenswert gemacht hat. Der
XL2515 teilt seinen Quarz durch zwei, bevor der Prescaler anfängt, und ein Bit
braucht mindestens acht Quanta — der Quarz setzt also eine harte Obergrenze, die
kein Registerwert verschiebt:

| Quarz | Obergrenze | Nutzdaten bei dieser Rate | gegen die fließenden 12–30 kB/s |
| --- | ---: | ---: | --- |
| **16 MHz — was das Modul hat** | **1 Mbit/s** | **51,6 kB/s** | bequem |
| 8 MHz | 500 kbit/s | 25,8 kB/s | wäre oben knapp geworden |

**Das Modul hat 16 MHz**, das Budget steht also, und 1 Mbit/s ist exakt
erreichbar — beim kleinsten Teiler und den wenigsten Quanta, die das Bauteil
zulässt.

Das wurde nicht geglaubt. Der Treiber des Herstellers trägt CNF-Tripel für zehn
gängige Bitraten; jedes zurück in Teiler und Quantazahl zu decodieren ergibt die
angegebene Rate **bei 16 MHz und bei keinem anderen Quarz** — zehn unabhängige
Bestätigungen einer Zahl. `test_can_timing` nagelt das fest, ein künftiges Modul
mit einem anderen Quarz bringt also einen Test zu Fall und keinen Bus.

Zwei durchgerechnete Beispiele sind von Hand gegen das Datenblatt geprüft:
500 kbit/s ergibt sechzehn Quanta, einen Sample Point bei 87,5 % und
CNF1/2/3 = `0x40`, `0xB5`, `0x01`; 1 Mbit/s ergibt acht Quanta und einen Sample
Point bei 75 %. Fünfundsiebzig und nicht die geforderten 87,5, denn acht Quanta
sind das Wenigste, das ein Bit haben darf, und ein Quantum ist damit ein Achtel
des Bits — näher landet nichts. Die eigene Tabelle des Herstellers setzt dieses
Bit auf 62,5 %, ein ganzes Quantum früher als nötig.

### Die Pins

Aus dem eigenen Treiber des Herstellers statt geraten, festgehalten in
`copro_pins.h`: **spi1, SCK auf GP10, MOSI GP11, MISO GP12, CS GP9, INT GP8**,
mit dem Bus auf 10 MHz. Das TWAI des Bedienteils liegt auf **GPIO19/20** — den
nativen USB-Pins, weshalb USB und CAN einander ausschließen und weshalb die
Konsole auf UART liegt.

### Zusammensetzen, und wo es nicht passiert

Ein CAN-Frame trägt vier Register, eine Antwort auf ein Lesen von dreizehn
Registern kommt also als vier Nachrichten an. Es gibt kein Zusammensetzen *im
Transport*: jeder Frame sagt, welche Register er hält, ein Frame ist also für
sich eine vollständige Nachricht.

Zusammengefügt wird im Poller, der als Einziger weiß, wonach gefragt wurde. Er
hält ein Bit je Register des angefragten Fensters und antwortet dem Aufrufer,
wenn das Fenster voll ist. Ein Teil, der ankommt, ohne es zu vervollständigen,
ist kein Fehler und wird nicht als einer gezählt; ein Teil, der außerhalb des
Fensters landet, schon — denn das ist die Antwort auf eine Frage, die niemand
mehr stellt. `test_link_loopback` lässt den echten Host gegen den echten
Dispatcher laufen, über einen Bus, der verwirft, verzögert und umsortiert,
einschließlich einer verkehrt herum zugestellten Antwort — Reihenfolge ist keine
Information, wenn jeder Frame seinen eigenen Offset trägt.

### Eine Konsole behalten, während CAN gewählt ist

Natives USB ist der falsche Ort dafür, was das Gegenteil der naheliegenden
Antwort ist. USB-Serial-JTAG und USB-OTG liegen beide auf **GPIO19 und GPIO20**
— dedizierten Analogpins, die sich nirgendwo hin routen lassen —, und das ist
das Paar, das der FSUSB42UMX gegen CAN umschaltet. CAN zu wählen kostet also die
native Konsole, egal wie herum der Multiplexer verdrahtet ist, und das ist genau
die Sitzung, die eine will.

Die zweite USB-C-Buchse der Platine sitzt hinter einer USB-UART-Bridge und teilt
sich nichts mit CAN, die Konsole ist deshalb **UART0 primär mit USB-Serial-JTAG
als zweitem Weg**. Die Ausgabe geht an beide; welche Buchse steckt, zeigt sie an.
Der zweite Weg kostet nichts, solange USB gewählt ist, und sorgt dafür, dass die
Anordnung weich versagt, falls die gebrückte Buchse doch woanders als an den
Standardpins von UART0 hängt.

## Die zwei Enden

Beide Treiber sind bewusst dumm: sie bewegen Frames und sonst nichts. Die
Page-Semantik, das Identifier-Layout, das Bit Timing und beide Watchdogs liegen
in `shared/` und werden auf einem Laptop getestet — jede Entscheidung, die ein
Treiber treffen könnte, wäre also eine, die sich nur auf Hardware testen ließe.

**Das Bedienteil** (`firmware/panel/components/can_twai`) konfiguriert die
TWAI-Peripherie aus dem Timing-Solver und schaltet den Multiplexer der Platine
um — das Einzige, was es tut und was kein Wrapper ist: CAN zu wählen kostet
natives USB, es ist also eine bewusste Entscheidung und nichts, was `board_init`
still erledigt.

**Der Koprozessor** (`firmware/copro/src/xl2515.c`) sind Chip-Select-Flanken und
eine Handvoll Registerschreibvorgänge in der Reihenfolge, die das Datenblatt
angibt. Er prüft, dass der Controller im Configuration Mode aufwacht, was das
Datenblatt garantiert — ein Fehlschlag dort ist also die **SPI**-Verdrahtung und
nicht die CAN-Verdrahtung, und das sind gegenüberliegende Enden der Platine.

Sein Sendepuffer fasst einen Frame, eine mehrteilige Antwort wartet also darauf,
dass jeder Teil die Arbitrierung gewinnt; das Warten ist begrenzt, denn ein Bus,
der nichts mehr annimmt, darf das Failsafe nicht aufhalten.

## Was getestet wird, und wo

Alles davon läuft auf einem Laptop ohne Leitung.

Das Identifier-Layout wird Bit für Bit gegen das Datenblatt geprüft statt durch
Round-trip von Werten — ein Round-trip stimmt mit sich selbst überein, auch wenn
beide Hälften denselben Fehler teilen —, und der ganze 29-Bit-Raum wird
durchlaufen. Das Bit Timing wird auf Exaktheit statt auf Näherung gelöst, mit
einem von Hand gegen das Datenblatt festgenagelten Beispiel und einem Test, dass
beide Enden bei der Bitrate des Links auf demselben Sample Point landen: das
dürfen sie nicht getrennt entscheiden, und einmal taten sie es.

`test_link_loopback` lässt die beiden Enden gegeneinander laufen — den echten
Host-Poller und den echten Device-Dispatcher — über einen Bus, der verwirft,
verzögert und umsortiert. Kein Mock an einem der Enden, denn ein Mock stimmt mit
dem überein, was der Test gerade erwartet. Abgedeckt sind: eine Page, die breiter
ist als ein Frame, kommt heil zurück; die Teile kommen verkehrt herum an; ein
Fenster, das mitten in einer Page beginnt, behält seinen Offset; ein Schreiben
wird mit dem quittiert, was *gespeichert* wurde, statt mit dem, was gesendet
wurde; eine Ablehnung reist als Antwort; ein verlorenes Teil lässt die Anfrage
unbeantwortet statt halb beantwortet; eine Antwort auf eine aufgegebene Frage
wird zurückgewiesen; und der eigene Watchdog des Geräts feuert weiterhin auf
einem Bus, der still geworden ist.
