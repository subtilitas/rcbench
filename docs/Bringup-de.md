# Den Link in Betrieb nehmen

<sub>[English](Bringup.md) · **Deutsch**</sub>

Wie man prüft, dass Frames den CAN-Bus (Controller Area Network) zwischen den
beiden Platinen überqueren, und wie der Bericht zu lesen ist. Die Verkabelung
steht unter [Der Link](Link-de.md).

## Echo-Selbsttest

Der Selbsttest beantwortet eine Frage: kommen Frames unversehrt über den Bus?
Er benutzt kein Page-Protokoll. Das Panel sendet einen Probe-Frame, der
Koprozessor schickt ihn zurück, und das Panel vergleicht Byte für Byte.
Besteht der Test und der Link funktioniert nicht, liegt der Fehler oberhalb
der Leitung.

### Koprozessor

Nichts zu konfigurieren. Der Koprozessor startet CAN beim Boot, gibt aus, ob
der Controller geantwortet hat, und beantwortet Probes dauerhaft. Das Echo
kostet einen Registerzugriff je Schleifendurchlauf und beantwortet nur Frames
an eine Page, die die Page Map nicht benutzt.

Boot-Ausgabe auf der USB-Konsole (Universal Serial Bus) des Koprozessors,
alle 3 s wiederholt:

    rcbench-iomcu: CAN up, 1000000 bit/s, 0 echoes served, tx_err 0 rx_err 0 eflg 0x00

`CAN did not answer on SPI` heißt, dass der Controller nach dem Reset nicht
den Configuration Mode gemeldet hat. Der Fehler liegt an SPI (Serial
Peripheral Interface; Modul nicht bestückt, Verdrahtung an GP8 bis GP12),
nicht am CAN-Bus.

### Panel

Der Test ist opt-in, weil das Starten von CAN das native USB des Panels
wegnimmt:

```bash
cd firmware/panel
idf.py -DRCBENCH_CAN_SELFTEST=1 build flash
```

Auf die UART-Buchse (Universal Asynchronous Receiver-Transmitter) schauen,
nicht auf die native USB-Buchse. GPIO19 und GPIO20 (General-Purpose
Input/Output) führen sowohl das native USB als auch den CAN-Transceiver, und
der Multiplexer wählt eines aus; die Konsole liegt auf UART0 mit
USB-Serial-JTAG (der eingebauten USB-Seriell- und Debug-Bridge des ESP32-S3)
als Zweitkonsole.

Der Test läuft 5 s lang beim Boot, vor dem Identity-Poll, und gibt aus:

    I (…) can: 1000000 bit/s: brp 4, tseg1 14, tseg2 5, sjw 4, sample point 75.0%
    I (…) rcbench: CAN self-test: every probe came back intact
    I (…) rcbench:   sent 2024 echoed 2024 corrupt 0 lost 0 stale 0  (transmit queue full 0 times)
    I (…) rcbench:   round trip min 334 max 1356 us
    I (…) rcbench:   panel  tx_err 0 rx_err 0 bus_err 0
    I (…) rcbench:   iomcu  CAN up, 2024 echoes, 0 overflow(s), tx_err 0 rx_err 0 flags 0x00

Die letzte Zeile ist der eigene Status des Koprozessors, vor und nach der
Echophase über den Bus abgefragt. Die Differenz der beiden Ablesungen ist die
Zahl der Echos, die er während des Tests gesendet hat; das Panel vergleicht
sie mit der Zahl, die es empfangen hat.

### Befunde

| Befund | Bedeutung | Prüfen |
| --- | --- | --- |
| `no probe came back` | nichts kommt durch | CANH/CANL vertauscht; Gegenseite versorgt; gleiche Bitrate an beiden Enden; Abschlusswiderstände an beiden Enden |
| `probes come back altered` | Frames kommen durch und kommen falsch an | Sample Point oder Bit Timing; ein fehlender Abschluss reflektiert |
| `probes cross, and not all of them` | grenzwertiger Bus | Timing, ein Abschluss, oder ein für die Rate zu langer Bus |
| `probes go missing without a bus error` | Frames kamen unversehrt an und wurden nicht rechtzeitig gelesen | ein Empfangspuffer ist übergelaufen; kein Verdrahtungsfehler. Mit dem Overflow-Zähler des Koprozessors vergleichen |
| `every probe came back intact` | die Leitung ist in Ordnung | ein verbleibender Fehler liegt oberhalb der Leitung |

Tritt beides auf, wird Verfälschung vor Verlust gemeldet, weil ein
grenzwertiger Bus beides erzeugt und die Verfälschung die Ursache benennt.

Der Verlust wird am Busfehlerzähler des Controllers aufgeteilt. Frames, die
mit Busfehlern verloren gingen, wurden auf der Leitung beschädigt (Abschluss,
Timing, Länge). Frames, die mit null Busfehlern verloren gingen, kamen
unversehrt an und wurden von einem Empfänger verworfen, der nicht rechtzeitig
gelesen hat.

Erreicht der Sendefehlerzähler während des Tests 128, quittiert kein anderer
Knoten: der Koprozessor ist gar nicht auf dem Bus. Der Test meldet das
einmal, bevor die fünf Sekunden vorbei sind.

Die Probe-Nutzdaten durchlaufen nur dominant, nur rezessiv und beide
alternierenden Muster, weil CAN nach fünf gleichen Bits ein komplementäres
Bit einfügt und lange Folgen eines Pegels das sind, woran ein grenzwertiger
Bus scheitert.

## Link-Bericht

Nach der Inbetriebnahme pollt das Panel jede Sekunde die Identity-Page des
Koprozessors, bis er antwortet, dann die Bench-Page mit 20 Hz und die
Status-Page mit 1 Hz. Alle 5 s, solange der Link ausgefallen ist, und einmal
pro Minute, solange er läuft, gibt das Panel eine Diagnose aus:

    I (…) rcbench: LINK works, and not every time
    W (…) rcbench:   check: marginal timing, or a poll period tighter than the round trip
    I (…) rcbench:   panel  polls 1200 replies 1187 timeouts 13 stale 0 nack 0 crc 0 resync 0
    I (…) rcbench:   iomcu  frames 1187 crc 0 resync 0
    I (…) rcbench:   round trip min 620 avg 700 max 1400 us

| Diagnose | Bedeutung |
| --- | --- |
| `no reply to any poll` | Koprozessor versorgt; CANH/CANL; Bitrate; Abschlusswiderstände |
| `answering, wrong protocol` | beide Enden aus demselben Baum flashen |
| `requests land, answers do not` | die Gegenseite hört das Panel, das Panel hört sie nicht: ihr Sendepfad |
| `frames arrive corrupt` | Bit Timing oder Sample Point zwischen den beiden Enden uneinig |
| `answers arrive too late` | eine Antwort langsamer als der Poll-Timeout, oder eine hängende Gegenseite |
| `works, and not every time` | grenzwertiges Timing, oder eine Pollperiode enger als die Umlaufzeit |

Die Spalten `crc` und `resync` der Koprozessorzeile tragen den Empfangs- und
den Sendefehlerzähler des XL2515 (Statusregister `LINK_ST_CRC_ERRORS` und
`LINK_ST_RESYNCS`).

## Was festzuhalten ist

Die Umlaufzeit, die Fehlerzähler beider Enden und der Overflow-Zähler des
Koprozessors. Die Umlaufzeit legt fest, was eine Pollperiode schaffen muss;
die Zähler sagen, ob der Bus oder die Software die Grenze setzt.

Bei beiden Platinen unter Spannung außerdem am Oszilloskop bestätigen, dass
GPIO6 auf J8 mit der Rate flankt, die [Sicherheit](Safety-de.md) vorgibt.
