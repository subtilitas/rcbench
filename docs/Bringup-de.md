# Den Link auf Silizium in Betrieb nehmen

<sub>[English](Bringup.md) · **Deutsch**</sub>

Beide Enden sind geschrieben und auf dem Host gegeneinander getestet. Was bleibt,
ist, sie auf einer Leitung gegeneinander laufen zu lassen — der eine Teil, den
kein Test ersetzen kann.

Diese Seite gibt es, weil der erste Hardwarestart des Bedienteils drei Symptome
und zwei Ursachen gekostet hat, hergeleitet aus einer Beschreibung statt von der
Platine. Der Link hat mehr Arten, halb kaputt zu sein, als das Display, und die
meisten davon zeigen sich als „geht nicht". Also sagt die Firmware jetzt, welche
es ist.

## Hier anfangen: kommt überhaupt etwas über den Bus?

Bevor das Page-Protokoll irgendetwas bedeutet, muss eine Frage für sich
beantwortet sein: **kommen Frames heil über diesen Bus?** Sie zusammen mit „geht
der Link" zu beantworten, ist der Weg, aus einer Inbetriebnahme einen Nachmittag
zu machen — ein leicht falsches Bit Timing, ein fehlender Abschlusswiderstand,
ein Transceiver im falschen Modus und ein Bug im Dispatcher zeigen sich alle als
„das Bedienteil zeigt keine Zahlen".

Es gibt also einen Echotest, der nur das beantwortet. Das Bedienteil sendet einen
Frame, der Koprozessor schickt ihn direkt zurück, das Bedienteil prüft, dass er
Byte für Byte zurückkam. Keine Page Map, keine Register, keine State Machine.
**Wenn das durchläuft und der Link trotzdem nicht geht, liegt der Fehler
oberhalb der Leitung** — was zu wissen sich lohnt, bevor jemand etwas absteckt.

### Ausführen

Der Koprozessor braucht nichts: er versucht CAN beim Boot, sagt, ob der
Controller geantwortet hat, und echot von da an Probes. Das kostet einen
Registerlesezugriff je Schleifendurchlauf und beantwortet nur eine Page, die die
Map nicht benutzt, also bleibt es dauerhaft drin — das Inbetriebnahme-Werkzeug,
das schon geflasht ist, ist das, das benutzt wird.

Die Seite des Bedienteils ist opt-in, weil das Starten von CAN das native USB
wegnimmt:

```bash
cd firmware/panel
idf.py -DRCBENCH_CAN_SELFTEST=1 build flash
```

**Auf den UART-Anschluss schauen, nicht auf den USB.** GPIO19 und GPIO20 führen
sowohl das native USB als auch den CAN-Transceiver, und der Multiplexer muss
sich entscheiden — siehe [der Link](Link-de.md). Die Konsole liegt genau
deswegen auf UART0 mit USB-Serial-JTAG als zweitem Weg, damit diese Sitzung
irgendwo reden kann.

Er läuft fünf Sekunden beim Boot und gibt aus:

    can: 1000000 bit/s: brp 4, tseg1 14, tseg2 5, sjw 4, sample point 75.0%
    panel: CAN self-test: every probe came back intact
      sent 2024 echoed 2024 corrupt 0 lost 0 stale 0  (transmit queue full 0 times)
      round trip min 334 max 1356 us
      panel  tx_err 0 rx_err 0 bus_err 0
      copro  CAN up, 2024 echoes, 0 overflow(s), tx_err 0 rx_err 0 flags 0x00

**Beide Enden stehen in diesem Bericht**, und die Hälfte des Koprozessors kam
über den Bus und nicht über ein zweites USB-Kabel. Das wiegt schwerer als die
Bequemlichkeit: mehrere Fehler sind nur im Vergleich sichtbar. Frames, die der
Koprozessor beantwortet hat und die das Bedienteil nie gehört hat, sind ein
Fehler auf dem Rückweg; Frames, die er nie beantwortet hat, einer auf dem
Hinweg; und ein Koprozessor, der Frames mangels freiem Puffer verworfen hat, ist
keines von beidem — was kein Buszähler irgendwo festhält.

Der Statusaustausch wird gepollt, teilt sich die Page mit dem Echotest und läuft
mit derselben niedrigsten Priorität. Er passiert **auf beiden Seiten** der
Echophase, nicht nur danach: der Zähler des anderen Endes läuft seit dessen
eigenem Boot, also bedeutet nur die Differenz über die Messung hinweg etwas —
ein Wert, der einmal am Ende genommen wird, ist eine Lebenssumme und sagt
nichts.

| Was er sagt | Was es bedeutet | Wo zu suchen |
| --- | --- | --- |
| `no probe came back` | Es kommt gar nichts durch | CANH/CANL vertauscht? Gegenseite mit Strom? Beide auf derselben Bitrate? Abschlusswiderstände an **beiden** Enden? |
| `probes come back altered` | Frames kommen durch und kommen falsch an | Sample Point oder Bit Timing; ein fehlender Abschluss reflektiert |
| `probes cross, and not all of them` | Grenzwertig | Timing, ein Abschlusswiderstand, oder ein Bus, der für die Rate zu lang ist |
| `probes go missing without a bus error` | Sie kamen heil an, und niemand hat sie gelesen | Ein Empfangspuffer ist übergelaufen. **Kein** Verdrahtungsfehler — im selben Bericht den Overflow-Zähler des Koprozessors prüfen |
| `every probe came back intact` | Die Leitung ist in Ordnung | Was noch falsch ist, liegt darüber |

Korruption schlägt in dieser Tabelle Verlust, und die Reihenfolge ist Absicht:
ein grenzwertiger Bus tut beides, Verlust ist die lautere Zahl, und sie zeigt auf
die Kabellänge, während die Korruption sagt, dass das Bit Timing falsch ist.

Der Verlust selbst teilt sich noch einmal, am eigenen Fehlerzähler des
Controllers. Frames, die *mit* Busfehlern verloren gingen, wurden auf der
Leitung beschädigt — Abschluss, Timing, Länge. Frames, die mit **null**
Busfehlern verloren gingen, kamen einwandfrei an und wurden von etwas verworfen,
das nicht rechtzeitig weitergelesen hat, und dafür jemanden Abschlusswiderstände
prüfen zu schicken, verbrennt einen Nachmittag an funktionierender Hardware.

Auch die Nutzdaten sind nicht beliebig. CAN stopft nach fünf gleichen Pegeln ein
komplementäres Bit ein, die Muster, die einen grenzwertigen Bus fordern, sind
also lange Läufe eines Pegels — die Probes gehen alle durch: durchgehend
dominant, durchgehend rezessiv und beide alternierenden Muster. Ein Test, der
nur zählende Ganzzahlen sendet, würde auf einem Bus bestehen, der echten Verkehr
verliert.

Wenn der Koprozessor beim Boot `CAN DID NOT ANSWER` ausgibt, liegt der Fehler
auf **SPI und nicht auf CAN**: das Datenblatt garantiert, dass der Controller im
Configuration Mode aufwacht, ein CANSTAT, der etwas anderes sagt, bedeutet also,
dass auf dem SPI-Bus, den der Treiber zu haben glaubt, niemand zuhört. Das
auseinanderzuhalten lohnt sich, bevor ein Oszilloskop herauskommt.

---

## Was zurückzumelden ist

Die Umlaufzeit, die Fehlerzähler beider Enden, und ob der Koprozessor
Empfangs-Overflows gemeldet hat. Diese drei will das Protokoll: das erste legt
fest, was eine Pollperiode schaffen muss, die anderen beiden sagen, ob der Bus
oder die Software die Grenze war.

Erwähnenswert ist außerdem, was der *Heartbeat* währenddessen getan hat. Sein
Monoflop ist nicht bestückt, die Flanken erreichen also einen Header-Pin und ein
Oszilloskop und sonst nichts — aber dies ist eine Sitzung, in der beide Platinen
gleichzeitig unter Spannung stehen, und es ist die billigste Gelegenheit zu
bestätigen, dass die Leitung mit der Rate flankt, die [Sicherheit](Safety-de.md)
verlangt.
