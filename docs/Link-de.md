# Der Link

<sub>[English](Link.md) · **Deutsch**</sub>

Die beiden Platinen sprechen **CAN mit 1 Mbit/s**. Zuerst die Verkabelung,
danach die Protokollreferenz für alle, die an der Firmware arbeiten.

## Verkabelung

| | |
| --- | --- |
| Bus | Classic CAN (kein FD), **1 Mbit/s** |
| Panelseite | der TWAI-Controller auf **GPIO19/20** |
| Koprozessorseite | **XL2515** (MCP2515-kompatibel) an **spi1**: SCK GP10, MOSI GP11, MISO GP12, CS GP9, INT GP8 — SPI mit 10 MHz |
| Transceiver | SIT65HVD230 |
| Abschluss | an **beiden** Enden |
| Quarz | das XL2515-Modul muss einen **16-MHz**-Quarz tragen — mit 8 MHz ist bei 500 kbit/s Schluss, und dieser Link läuft schneller |

**Wer CAN wählt, verliert das native USB des Panels.** GPIO19/20 führen beides,
und der Multiplexer der Platine muss sich entscheiden. Die Konsole liegt
deshalb auf **UART0** — das ist die zweite USB-C-Buchse der Platine, hinter
ihrer USB-UART-Bridge — mit USB-Serial-JTAG als Ersatzweg. Läuft CAN, gehört
der Blick auf die UART-Buchse.

Kommen keine Frames durch: [Den Link in Betrieb nehmen](Bringup-de.md)
durcharbeiten — dort werden die Fehlerbilder auseinandergehalten, bevor
irgendjemand Kabel zieht.

## Was bei einem Linkausfall passiert

Der Koprozessor setzt nach **200 ms** Stille aus eigener Befugnis
Failsafe-Werte; das Panel eskaliert nach einer Sekunde. Das Ende, das die
Ausgänge hält, ist immer das misstrauischere.

Das Failsafe **rastet ein**. Kehrt Verkehr zurück, ist damit bewiesen, dass
der Link lebt — nicht, dass es weitergehen soll. Aufgehoben wird das Failsafe
nur durch eine bewusste Handlung am Panel. Ein Prüfstand darf sich nicht von
selbst wieder scharf schalten, während niemand hinsieht.

---

## Protokollreferenz

Ab hier geht es um die Firmware, nicht um die Benutzung. Das Modell: je
**Page** bis zu 32 Sechzehn-Bit-Register, gelesen und geschrieben in
Fenstern. Eine neue Fähigkeit bedeutet eine neue Page — nie einen neuen
Nachrichtentyp.

### Der Identifier trägt die ganze Adresse

Ein 29-Bit-CAN-Identifier fasst alle Felder der Nachricht. Ein Read ist
deshalb ein Frame ohne Nutzdaten, und Priorität ist eine Eigenschaft der
Adresse:

| Bits | Feld | |
| --- | --- | --- |
| 28..26 | Priorität | 3 Bit, niedriger gewinnt die Arbitrierung |
| 25..22 | op | read / write / data / ack / nack |
| 21..14 | Page | die Page Map |
| 13..6 | Offset | erstes Register in diesem Frame |
| 5..0 | Count | Register, um die es geht |

Ein Schreiben auf die Control-Page setzt sich **auf der Leitung** gegen jedes
Telemetrie-Lesen durch — auch gegen Verkehr, der schon unterwegs ist, und ganz
ohne Software an den Enden.

### Nichts wird zusammengesetzt

Ein Frame trägt bis zu vier Register, jeder mit eigenem Offset und Count. Eine
Antwort über dreizehn Register besteht also aus vier unabhängigen Nachrichten:
die Reihenfolge ist egal, und ein verlorener Frame kostet einen
Registerbereich, keinen ganzen Transfer. Zusammengefügt wird erst im Poller —
der weiß, welches Fenster er angefragt hat, und antwortet, sobald es voll ist.

In den Nutzdaten steckt keine CRC: CAN bringt eine 15-Bit-CRC, einen
Acknowledge-Slot und automatische Wiederholung in Silizium mit. Integrität
über etwas Größeres als eine Page — etwa ein Firmware-Image — gehört zu diesem
Transfer, nicht zum Transport.

### Bit Timing

Wird in `shared/can/can_timing.c` berechnet statt aus einer Tabelle kopiert,
und die Rechnung besteht darauf, dass die Bitrate exakt aufgeht. Bei 1 Mbit/s
landen beide Enden auf **acht Time Quanta mit Sample Point bei 75 %** — und
ein Test stellt sicher, dass es *derselbe* Punkt ist, denn das dürfen die
beiden nicht unabhängig voneinander entscheiden. Der XL2515 halbiert seinen
Quarztakt, bevor der Prescaler greift — deshalb ist der 16-MHz-Quarz eine
harte Anforderung und keine Empfehlung.

### Budget

Ein voller `bench_state`-Poll umfasst dreizehn Register — fünf Frames,
**1,55 % Buslast bei 20 Hz**. Die Nutzlast von Classic CAN bei dieser Rate
liegt im ungünstigsten Fall bei etwa 52 kB/s; tatsächlich fließen 12–30 kB/s.

### Wie es getestet wird

Alles auf einem Laptop, ohne Leitung. Das Identifier-Layout wird Bit für Bit
gegen das Datenblatt geprüft, über den gesamten 29-Bit-Raum; der
Timing-Rechner ist an von Hand nachgerechnete Beispiele genagelt.
`test_link_loopback` lässt den echten Host-Poller gegen den echten
Device-Dispatcher laufen, über einen Bus, der Frames verwirft, verzögert und
umsortiert. Abgedeckt ist unter anderem: eine geteilte Antwort kommt rückwärts
an; eine abgelehnte Anfrage kommt als NACK zurück statt gar nicht; ein
verlorenes Teilstück lässt die Anfrage unbeantwortet statt halb beantwortet;
und der Watchdog des Geräts feuert auch auf einem Bus, der still geworden ist.
