# Der Link

<sub>[English](Link.md) · **Deutsch**</sub>

Die beiden Platinen sprechen **CAN mit 1 Mbit/s**. Zuerst die Verkabelung, die
du brauchst; die Protokollreferenz für alle, die an der Firmware arbeiten,
folgt.

## Verkabelung

| | |
| --- | --- |
| Bus | Classic CAN (kein FD), **1 Mbit/s** |
| Bedienteilseite | der TWAI-Controller auf **GPIO19/20** |
| Koprozessorseite | **XL2515** (MCP2515-kompatibel) an **spi1**: SCK GP10, MOSI GP11, MISO GP12, CS GP9, INT GP8 — SPI mit 10 MHz |
| Transceiver | SIT65HVD230 |
| Abschluss | an **beiden** Enden |
| Quarz | das XL2515-Modul muss **16 MHz** tragen — ein 8-MHz-Modul deckelt den Bus bei 500 kbit/s und kann an diesem Link nicht teilnehmen |

**CAN zu wählen nimmt dem Bedienteil das native USB.** GPIO19/20 führen sowohl
das native USB als auch den CAN-Transceiver, und der Multiplexer der Platine
muss sich entscheiden. Die Konsole liegt deshalb auf **UART0** — der zweiten
USB-C-Buchse der Platine, hinter deren USB-UART-Bridge — mit USB-Serial-JTAG
als zweitem Weg. Wenn CAN läuft: auf die UART-Buchse schauen.

Wenn keine Frames durchkommen, arbeite
[Den Link in Betrieb nehmen](Bringup-de.md) durch — es hält die Fehlerbilder
auseinander, bevor irgendetwas abgesteckt wird.

## Was passiert, wenn der Link ausfällt

Der Koprozessor füllt nach **200 ms** Stille aus eigener Befugnis
Failsafe-Werte ein; das Bedienteil eskaliert nach einer Sekunde. Das Ende, das
die Ausgänge hält, ist immer das misstrauischere.

Das Failsafe **rastet ein**. Zurückkehrender Verkehr beweist, dass der Link
lebt, und stoppt den Stillezähler — aber er hebt das Failsafe nicht auf; es zu
verlassen braucht eine bewusste Handlung vom Bedienteil. Ein Prüfstand darf
sich nicht selbst wieder scharf schalten, während niemand hinsieht.

---

## Protokollreferenz

Alles ab hier ist für die Arbeit an der Firmware, nicht für die Benutzung.
Das Modell: bis zu 32 Sechzehn-Bit-Register je **Page**, in Fenstern gelesen
und geschrieben; eine Fähigkeit hinzuzufügen fügt eine Page hinzu, nie einen
Nachrichtentyp.

### Der Identifier trägt die ganze Adresse

Ein 29-Bit-CAN-Identifier fasst jedes Feld der Nachricht — ein Read ist damit
ein Frame ohne Nutzdaten, und Priorität ist eine Eigenschaft der Adresse:

| Bits | Feld | |
| --- | --- | --- |
| 28..26 | Priorität | 3 Bit, niedriger gewinnt die Arbitrierung |
| 25..22 | op | read / write / data / ack / nack |
| 21..14 | Page | die Page Map |
| 13..6 | Offset | erstes Register, das dieser Frame trägt |
| 5..0 | Count | Register, um die es in diesem Frame geht |

Ein Schreiben auf die Control-Page schlägt jedes Telemetrie-Lesen **auf der
Leitung**, gegen Verkehr, der schon unterwegs ist, ohne Software an einem der
Enden.

### Nichts wird zusammengesetzt

Ein Frame trägt bis zu vier Register, jeder mit eigenem Offset und Count —
eine Antwort mit dreizehn Registern sind also vier unabhängige Nachrichten,
Reihenfolge egal, und ein verlorener Frame kostet einen Registerbereich statt
eines ganzen Transfers. Zusammengefügt wird im Poller, der das angefragte
Fenster verfolgt und antwortet, wenn es voll ist.

Es gibt keine CRC in den Nutzdaten: CAN trägt eine 15-Bit-CRC, einen
Acknowledge-Slot und Wiederholung in Silizium. Ende-zu-Ende-Integrität über
etwas Größeres als eine Page — ein Firmware-Image — gehört zu diesem Transfer,
nicht zum Transport.

### Bit Timing

Gelöst in `shared/can/can_timing.c` statt aus einer Tabelle kopiert, und es
besteht darauf, dass die Bitrate exakt aufgeht. Bei 1 Mbit/s landen beide Enden
auf **acht Time Quanta mit dem Sample Point bei 75 %** — und ein Test nagelt
fest, dass sie auf *demselben* Punkt landen, denn das dürfen sie nicht getrennt
entscheiden. Der XL2515 teilt seinen Quarz durch zwei, bevor der Prescaler
anfängt — deshalb ist der 16-MHz-Quarz eine harte Anforderung und keine
Vorliebe.

### Budget

Ein voller `bench_state`-Poll sind dreizehn Register — fünf Frames, **1,55 %
des Busses bei 20 Hz**. Die ungünstigste Classic-CAN-Nutzlast bei dieser Rate
sind etwa 52 kB/s gegen die 12–30 kB/s, die tatsächlich fließen.

### Wie es getestet wird

Alles auf einem Laptop, ohne Leitung. Das Identifier-Layout wird Bit für Bit
gegen das Datenblatt über den ganzen 29-Bit-Raum geprüft; der Timing-Solver
ist auf von Hand nachgerechnete Beispiele festgenagelt. `test_link_loopback`
lässt den echten Host-Poller gegen den echten Device-Dispatcher laufen, über
einen Bus, der verwirft, verzögert und umsortiert — abgedeckt sind geteilte
Antworten, die rückwärts ankommen, Ablehnungen, die als Antworten reisen,
verlorene Teile, die eine Anfrage unbeantwortet statt halb beantwortet lassen,
und der Watchdog des Geräts, der auf einem still gewordenen Bus weiterhin
feuert.
