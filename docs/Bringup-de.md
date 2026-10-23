# Den Link in Betrieb nehmen

<sub>[English](Bringup.md) · **Deutsch**</sub>

Wie man prüft, dass Frames den CAN-Bus (Controller Area Network) zwischen den
beiden Platinen überqueren, und wie der Bericht zu lesen ist. Die Verkabelung
steht unter [Der Link](Link-de.md).

## Echo-Selbsttest

Der Selbsttest beantwortet eine Frage: kommen Frames unversehrt über den Bus?
Er benutzt kein Page-Protokoll. Das Panel sendet einen Testframe, der
IOmcu schickt ihn zurück, und das Panel vergleicht Byte für Byte.
Besteht der Test und der Link funktioniert nicht, liegt der Fehler oberhalb
der Leitung.

### IOmcu

Nichts zu konfigurieren. Der IOmcu startet CAN beim Boot, gibt aus, ob
der Controller geantwortet hat, und beantwortet Probes dauerhaft. Das Echo
kostet einen Registerzugriff je Schleifendurchlauf und beantwortet nur Frames
an eine Page, die die Page Map nicht benutzt.

Boot-Ausgabe auf der USB-Konsole (Universal Serial Bus) des IOmcu,
alle 3 s wiederholt:

    rcbench-iomcu: CAN up, 1000000 bit/s, 0 requests served, 0 self-test echoes, tx_err 0 rx_err 0 eflg 0x00

`requests served` zählt die Link-Page-Anfragen, die das Panel geschickt und
dieses Ende beantwortet hat; `self-test echoes` zählt Testframes und steht
auf 0, solange der CAN-Selbsttest des Panels nicht läuft. Zusammen sagen die
beiden, welches Ende aufgehört hat:

| Requests | Panel zeigt | Lesart |
| --- | --- | --- |
| steigend | `VERBINDUNG` | funktioniert |
| unverändert | `KEINE VERBINDUNG` | es kommt nichts an — das Panel sendet nicht mehr, oder die Verkabelung ist offen |
| steigend | `KEINE VERBINDUNG` | die Anfragen kommen an, die Antworten nicht zurück |

`CAN did not answer on SPI` heißt, dass der Controller nach dem Reset nicht
den Configuration Mode gemeldet hat. Der Fehler liegt an SPI (Serial
Peripheral Interface; Modul nicht bestückt, Verdrahtung an GP8 bis GP12),
nicht am CAN-Bus.

### Panel

Der Test läuft bei jedem Start. Er dauert 1200 ms, innerhalb des Splash und
vor dem Identity-Poll: ein kaputter Bus wird so als kaputter Bus diagnostiziert
und nicht als eine Identity, die nie geantwortet hat.

Ein anderes Urteil als `Alle Testframes kamen korrekt zurück.` bringt das Panel auf den
[Bus-Fehler-Bildschirm](#der-bus-fehler-bildschirm) statt ins Menü. Es muss
nichts aktiviert werden, und es wird keine Konsole gebraucht.

Die Einzelheiten stehen auf der Konsole. Auf die UART-Buchse (Universal
Asynchronous Receiver-Transmitter) schauen, nicht auf die native USB-Buchse.
GPIO19 und GPIO20 (General-Purpose Input/Output) führen sowohl das native USB
als auch den CAN-Transceiver, und der Multiplexer wählt eines aus; die Konsole
liegt auf UART0 mit USB-Serial-JTAG (der eingebauten USB-Seriell- und
Debug-Bridge des ESP32-S3) als Zweitkonsole. Ausgegeben wird:

    I (…) can: 1000000 bit/s: brp 4, tseg1 14, tseg2 5, sjw 4, sample point 75.0%
    I (…) rcbench: CAN self-test: every probe came back intact
    I (…) rcbench:   sent 2024 echoed 2024 corrupt 0 lost 0 stale 0  (transmit queue full 0 times)
    I (…) rcbench:   round trip min 334 max 1356 us
    I (…) rcbench:   panel  tx_err 0 rx_err 0 bus_err 0
    I (…) rcbench:   iomcu  CAN up, 2024 echoes, 0 overflow(s), tx_err 0 rx_err 0 flags 0x00

Die letzte Zeile ist der eigene Status des IOmcu, vor und nach der
Echophase über den Bus abgefragt. Die Differenz der beiden Ablesungen ist die
Zahl der Echos, die er während des Tests gesendet hat; das Panel vergleicht
sie mit der Zahl, die es empfangen hat.

### Befunde

| Befund | Bedeutung | Prüfen |
| --- | --- | --- |
| `Kein Testframe kam zurück.` (Konsole: `no probe came back`) | nichts kommt durch | CANH/CANL vertauscht; Gegenseite versorgt; gleiche Bitrate an beiden Enden; Abschlusswiderstände an beiden Enden |
| `Testframes kamen mit veränderten Daten zurück.` (Konsole: `probes come back altered`) | Frames kommen durch und kommen falsch an | Sample Point oder Bit Timing; ein fehlender Abschluss reflektiert |
| `Einige Testframes gingen verloren.` (Konsole: `probes cross, and not all of them`) | grenzwertiger Bus | Timing, ein Abschluss, oder ein für die Rate zu langer Bus |
| `Testframes gingen ohne Busfehler verloren.` (Konsole: `probes go missing without a bus error`) | Frames kamen unversehrt an und wurden nicht rechtzeitig gelesen | ein Empfangspuffer ist übergelaufen; kein Verdrahtungsfehler. Mit dem Overflow-Zähler des IOmcu vergleichen |
| `Alle Testframes kamen korrekt zurück.` (Konsole: `every probe came back intact`) | die Leitung ist in Ordnung | ein verbleibender Fehler liegt oberhalb der Leitung |

Die Konsole schreibt die englischen Urteile. Startbildschirm und
Bus-Fehler-Bildschirm zeigen die deutschen aus der Tabelle; jedes passt in
29 Zellen.

Tritt beides auf, wird Verfälschung vor Verlust gemeldet, weil ein
grenzwertiger Bus beides erzeugt und die Verfälschung die Ursache benennt.

Der Verlust wird am Busfehlerzähler des Controllers aufgeteilt. Frames, die
mit Busfehlern verloren gingen, wurden auf der Leitung beschädigt (Abschluss,
Timing, Länge). Frames, die mit null Busfehlern verloren gingen, kamen
unversehrt an und wurden von einem Empfänger verworfen, der nicht rechtzeitig
gelesen hat.

Erreicht der Sendefehlerzähler während des Tests 128, quittiert kein anderer
Knoten: der IOmcu ist gar nicht auf dem Bus. Der Test meldet das
einmal, bevor der Durchlauf vorbei ist.

### Der Bus-Fehler-Bildschirm

Ein anderes Urteil als `Alle Testframes kamen korrekt zurück.` wird auf dem Panel vor
dem Menü gezeigt, denn der Fehler ist von jedem anderen Bildschirm aus
unsichtbar: ein Bus, der keine Frames trägt, sieht genauso aus wie ein
IOmcu, der nicht bestückt ist, und beides sieht aus wie ein Prüfstand,
der einfach keine Zahlen zeigt.

![Frames kommen verändert an](img/de/busfault.png)

Das Urteil ist die Überschrift, die Liste darunter ist das, was zu prüfen ist,
in der Reihenfolge, die am wenigsten kostet, und die rechte Spalte ist das,
was beide Enden gezählt haben. `zurück` ist grün, sobald überhaupt etwas
zurückkam; `BUS OFF` heißt, dass dieses Panel aufgehört hat zu senden.

![Es kam nichts zurück](img/de/busfault-silent.png)

Verlassen wird der Bildschirm mit einem zwei Sekunden langen Halten der Taste
am unteren Rand — dieselbe Geste und dieselbe Überblendung wie bei ARM. Ein
Bildschirm, der sagt, dass dem Prüfstand nicht zu trauen ist, soll sich nicht
durch eine Berührung wegwischen lassen, die auch ein Ärmel gewesen sein
könnte.

Das Quittieren repariert nichts und schaltet den Test nicht ab: der Prüfstand
läuft in Simulation, nichts treibt einen Ausgang, und der Test läuft beim
nächsten Start wieder.

Die Probe-Nutzdaten durchlaufen nur dominant, nur rezessiv und beide
alternierenden Muster, weil CAN nach fünf gleichen Bits ein komplementäres
Bit einfügt und lange Folgen eines Pegels das sind, woran ein grenzwertiger
Bus scheitert.

## Link-Bericht

Nach der Inbetriebnahme pollt das Panel jede Sekunde die Identity-Page des
IOmcu, bis er antwortet, dann die Bench-Page mit 20 Hz und die
Status-Page alle 500 ms. Alle 5 s, solange der Link ausgefallen ist, und einmal
pro Minute, solange er läuft, gibt das Panel eine Diagnose aus:

    I (…) rcbench: LINK works, and not every time
    W (…) rcbench:   check: marginal timing, or a poll period tighter than the round trip
    I (…) rcbench:   panel  polls 1200 replies 1187 timeouts 13 stale 0 nack 0 crc 0 resync 0
    I (…) rcbench:   iomcu  frames 1187 crc 0 resync 0 refused 0
    I (…) rcbench:   round trip min 620 avg 700 max 1400 us

| Diagnose | Bedeutung |
| --- | --- |
| `no reply to any poll` | IOmcu versorgt; CANH/CANL; Bitrate; Abschlusswiderstände |
| `answering, wrong protocol` | beide Enden aus demselben Baum flashen |
| `requests land, answers do not` | die Gegenseite hört das Panel, das Panel hört sie nicht: ihr Sendepfad |
| `frames arrive corrupt` | Bit Timing oder Sample Point zwischen den beiden Enden uneinig |
| `answers arrive too late` | eine Antwort langsamer als der Poll-Timeout, oder eine hängende Gegenseite |
| `works, and not every time` | grenzwertiges Timing, oder eine Pollperiode enger als die Umlaufzeit |

`nack` in der Panel-Zeile zählt Anfragen, die der IOmcu mit einem NACK
(Negative Acknowledge) beantwortet hat. Ein NACK ist eine Antwort: der Link
arbeitet, und die Page behielt, was sie hielt ([Frames](Link-de.md#frames)).
Eine Einstellung aus SETUP ANSCHLÜSSE, die eine Page ablehnt, zählt je
einmal: ein Schreiben je abgelehntem Wert, das erst wieder geschrieben wird,
wenn sich der Wert ändert. Wer auf SETUP durch Zeilen schaltet, zählt also
ein NACK für jeden Wert, den der IOmcu ablehnt -- Abgriff-Pause 2 ms mit
Tiefster Ton 400 Hz, oder PD mini RX auf dem SCL des Sensorbusses --, und
keines für die Werte, die er übernimmt. Ein Pin, der abgelehnt wurde, weil
etwas anderes ihn hält, wird alle 5 s erneut angeboten und zählt jedes Mal
einmal. `timeouts`, `stale` und `crc` sind die Spalten, die einen Linkfehler
zählen.

Die Spalten `crc` und `resync` der IOmcu-Zeile tragen den Empfangs- und
den Sendefehlerzähler des XL2515 (Statusregister `LINK_ST_CRC_ERRORS` und
`LINK_ST_RESYNCS`). Die letzte Zahl der Zeile ist die der CAN-Frames, die der
IOmcu seit dem Einschalten empfangen und nicht dekodiert hat
(`LINK_ST_REFUSED_LO` und `LINK_ST_REFUSED_HI`, Protokoll 4.21; 0 von einem
älteren IOmcu). Siehe [Abgelehnte Frames](Link-de.md#abgelehnte-frames).

## Warum der IOmcu zuletzt gestartet ist

Beim Start und danach alle 3 s nennt die Konsole des IOmcu, warum der IOmcu
zuletzt gestartet ist, mit jedem Rohwert:

    rcbench-iomcu: reset FLASH_UPDATE (chip_reset 0x00010000 reason 0x1 boot 0x010104FF params 0x101FD000 0x00000000 scratch 0x101FD000 0x00000000)

Die Zeile beim Start geht meist hinaus, bevor ein Host den USB-Port
geöffnet hat; die Zeile alle 3 s wiederholt sie. `chip_reset` ist POWMAN
CHIP_RESET, `reason` WATCHDOG REASON, `boot` das Boot-Wort des Boot-ROM
(0xttppbbdd: TBYB-Flags, Partition, Boot-Typ, Diagnose-Partition; `boot
none`, wenn das Boot-ROM keines lieferte), `params` seine Reboot-Parameter,
`scratch` WATCHDOG SCRATCH2 und SCRATCH3. Die Firmware liest sie einmal
beim Boot und setzt SCRATCH2 und SCRATCH3 danach auf 0. Die längste Zeile
hat 153 Byte mit ihrem LF.

CHIP_RESET ändert sich nur bei einem Reset auf Chip-Ebene, und seine Bits
sind nur lesbar (RP2350-Datenblatt, Abschnitte 7.3 und 7.4, Tabelle 489).
Der Reboot des Boot-ROM setzt das System über die Power-on State Machine
zurück, nicht den Chip: nach einem Update über CAN liest CHIP_RESET noch
HAD_POR vom Einschalten, wie in der Zeile oben. Ein Reset auf Chip-Ebene
setzt den Watchdog zurück, also heißt `reason 0x0`, dass CHIP_RESET zu
diesem Start gehört. `reset_cause_decode()` in `shared/safety/reset_cause.c`
wählt das Wort; die erste passende Zeile gilt:

| Wort | Register | Ursache |
| --- | --- | --- |
| `FLASH_UPDATE` | Boot-Typ 4 | das Update über CAN, eine auf das BOOTSEL-Laufwerk kopierte UF2, `picotool load -x` |
| `RAM_IMAGE` | Boot-Typ 3 | ein in den SRAM geladenes Image |
| `BOOTSEL_REBOOT` | Boot-Typ 2; oder reason ungleich 0, Boot-Typ 0, scratch 0xFFFFFFFF 0x00000000 | zurück aus BOOTSEL über `picotool reboot`, das picotool auch nach jedem mit `-f` ausgeführten Befehl sendet. Der Weg in BOOTSEL ist die 1200-Baud-Anfrage oder die Reset-Anfrage von picotool über USB |
| `SW_RESET` | reason 0x2 | der Watchdog sofort von Software ausgelöst |
| `WATCHDOG` | reason 0x1 | der Watchdog ist abgelaufen: der 16,7-s-TBYB-Timer (Try Before You Buy) des Boot-ROM nach einem Update, das das Panel nicht gekauft hat, oder ein Reboot mit anderen Parametern |
| `POR` | reason 0, HAD_POR (Bit 16) | Spannung angelegt |
| `BOR` | reason 0, HAD_BOR (Bit 17) | Brown-out-Detektor |
| `RUN_PIN` | reason 0, HAD_RUN_LOW (Bit 18) | RUN-Pin auf Low: der Reset-Taster oder ein Host, der RUN zieht |
| `GLITCH` | reason 0, HAD_GLITCH_DETECT (Bit 26) | Glitch-Detektor der Versorgung |
| `DEBUGGER` | reason 0, HAD_DP_RESET_REQ, HAD_RESCUE, HAD_HZD_SYS_RESET_REQ (Bits 19, 21, 27) | ein Reset durch einen Debugger |
| `POWER_DOWN` | reason 0, HAD_SWCORE_PD (Bit 25) | Power-down des Switched Core |
| `WATCHDOG` | reason 0, ein HAD_WATCHDOG_*-Bit (22, 23, 24, 28) | ein Watchdog, den POWMAN WDSEL den Chip zurücksetzen lässt; diese Firmware setzt keines |
| `UNKNOWN` | nichts davon | keine Ursache aufgezeichnet |

Der Reboot des Boot-ROM lässt den Watchdog im Timer-Modus laufen, also liest
jeder Reboot durch ihn `reason 0x1`. Ein Warm-Reset eines Kerns durch einen
Debugger löscht REASON ohne Reset auf Chip-Ebene, und der Start danach liest
sich als der Reset auf Chip-Ebene davor. Die Zeilen `BOOTSEL_REBOOT` und
`WATCHDOG` folgen dem Quelltext des RP2350-Boot-ROM und von picotool 2.x.

## Was festzuhalten ist

Die Umlaufzeit, die Fehlerzähler beider Enden und der Overflow-Zähler des
IOmcu. Die Umlaufzeit legt fest, was eine Pollperiode schaffen muss;
die Zähler sagen, ob der Bus oder die Software die Grenze setzt.

Bei beiden Platinen unter Spannung außerdem am Oszilloskop bestätigen, dass
GPIO28 (`PANEL_HEARTBEAT_PIN`) mit der Rate flankt, die [Sicherheit](Safety-de.md) vorgibt. GPIO28 ist IO28 des 12-poligen Headers P3.
