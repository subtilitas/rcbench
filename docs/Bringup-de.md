# Den Link auf echter Hardware in Betrieb nehmen

<sub>[English](Bringup.md) · **Deutsch**</sub>

Der Link hat mehr Möglichkeiten, halb kaputt zu sein, als das Display — und
fast alle sehen gleich aus: „geht nicht". Die Firmware kann sagen, welche es
ist. Diese Seite zeigt, wie man sie dazu bringt, und was die einzelnen
Antworten bedeuten.

## Hier anfangen: kommt überhaupt etwas über den Bus?

Bevor das Protokoll eine Rolle spielt, muss eine Frage für sich beantwortet
sein: **kommen Frames unversehrt über diesen Bus?** Wer sie mit „funktioniert
der Link?" vermischt, macht aus der Inbetriebnahme einen langen Nachmittag —
ein leicht falsches Bit Timing, ein fehlender Abschlusswiderstand, ein
Transceiver im falschen Modus und ein Softwarefehler sehen alle gleich aus:
das Panel zeigt keine Zahlen.

Dafür gibt es den Echotest. Das Panel sendet einen Frame, der Koprozessor
schickt ihn unverändert zurück, das Panel vergleicht Byte für Byte. Keine
Register, kein Protokoll. **Läuft der Echotest durch und der Link geht
trotzdem nicht, liegt der Fehler oberhalb der Leitung** — und das weiß man
dann, bevor irgendjemand ein Kabel zieht.

### Ausführen

Der Koprozessor braucht keine Vorbereitung: er startet CAN beim Boot, meldet,
ob der Controller geantwortet hat, und beantwortet Echo-Probes von da an
dauerhaft. Das kostet einen Registerzugriff pro Schleifendurchlauf — deshalb
ist es fest eingebaut, und das Diagnosewerkzeug ist immer schon geflasht, wenn
man es braucht.

Auf der Panelseite ist der Test opt-in, weil das Starten von CAN das native
USB wegnimmt:

```bash
cd firmware/panel
idf.py -DRCBENCH_CAN_SELFTEST=1 build flash
```

**Auf die UART-Buchse schauen, nicht auf die USB-Buchse.** GPIO19 und GPIO20
führen sowohl das native USB als auch den CAN-Transceiver; der Multiplexer
muss sich für eines entscheiden — siehe [der Link](Link-de.md). Die Konsole
liegt genau deshalb auf UART0.

Der Test läuft fünf Sekunden beim Boot und gibt aus:

    can: 1000000 bit/s: brp 4, tseg1 14, tseg2 5, sjw 4, sample point 75.0%
    panel: CAN self-test: every probe came back intact
      sent 2024 echoed 2024 corrupt 0 lost 0 stale 0  (transmit queue full 0 times)
      round trip min 334 max 1356 us
      panel  tx_err 0 rx_err 0 bus_err 0
      copro  CAN up, 2024 echoes, 0 overflow(s), tx_err 0 rx_err 0 flags 0x00

**Beide Enden stehen in diesem Bericht**, und die Zeilen des Koprozessors sind
über den Bus gekommen, nicht über ein zweites USB-Kabel. Das ist mehr als
Bequemlichkeit: mehrere Fehler zeigen sich nur im Vergleich. Frames, die der
Koprozessor beantwortet hat und das Panel nie gehört hat, sind ein Fehler auf
dem Rückweg; Frames, die er nie beantwortet hat, einer auf dem Hinweg. Und ein
Koprozessor, der Frames mangels freiem Puffer verworfen hat, ist keins von
beidem — das steht in keinem Buszähler.

Der Statuszähler des anderen Endes läuft seit dessen Boot. Aussagekräftig ist
deshalb nur die Differenz über die Messung hinweg — darum wird der Status
**vor und nach** der Echophase abgefragt. Eine einzelne Ablesung am Ende wäre
eine Summe seit dem Einschalten und sagt nichts.

| Meldung | Bedeutung | Wo suchen |
| --- | --- | --- |
| `no probe came back` | Es kommt gar nichts durch | CANH/CANL vertauscht? Gegenseite versorgt? Beide auf derselben Bitrate? Abschlusswiderstände an **beiden** Enden? |
| `probes come back altered` | Frames kommen an — verfälscht | Sample Point oder Bit Timing; ein fehlender Abschluss reflektiert |
| `probes cross, and not all of them` | Grenzwertig | Timing, ein fehlender Abschluss, oder ein zu langer Bus für die Rate |
| `probes go missing without a bus error` | Sie kamen unversehrt an, und niemand hat sie abgeholt | Ein Empfangspuffer ist übergelaufen. **Kein** Verdrahtungsfehler — im selben Bericht den Overflow-Zähler des Koprozessors ansehen |
| `every probe came back intact` | Die Leitung ist in Ordnung | Was noch klemmt, liegt darüber |

Dass verfälschte Frames in der Tabelle vor verlorenen stehen, ist Absicht: ein
grenzwertiger Bus erzeugt beides, und der Verlust ist die auffälligere Zahl —
aber die Verfälschung benennt die Ursache (Bit Timing), während der Verlust
nur auf die Kabellänge zeigt.

Der Verlust selbst teilt sich noch einmal, am Fehlerzähler des Controllers.
Frames, die **mit** Busfehlern verloren gingen, wurden auf der Leitung
beschädigt — Abschluss, Timing, Länge. Frames, die mit **null** Busfehlern
verloren gingen, kamen einwandfrei an und wurden von Software verworfen, die
nicht rechtzeitig gelesen hat. Wer dafür Abschlusswiderstände prüfen geht,
verliert einen Nachmittag an Hardware, die funktioniert.

Auch die Testmuster sind nicht beliebig. CAN fügt nach fünf gleichen Pegeln
ein Stopfbit ein — was einen grenzwertigen Bus wirklich fordert, sind lange
Folgen desselben Pegels. Die Probes durchlaufen deshalb nacheinander: nur
dominant, nur rezessiv, und beide alternierenden Muster. Ein Test, der bloß
hochzählende Zahlen sendet, würde auf einem Bus bestehen, der echten Verkehr
verliert.

Meldet der Koprozessor beim Boot `CAN DID NOT ANSWER`, liegt der Fehler an
**SPI, nicht an CAN**: das Datenblatt garantiert, dass der Controller im
Configuration Mode aufwacht. Ein CANSTAT, der etwas anderes sagt, heißt, dass
auf dem SPI-Bus niemand zuhört. Das lohnt sich zu wissen, bevor das
Oszilloskop auf den Tisch kommt.

---

## Was zurückzumelden ist

Die Umlaufzeit, die Fehlerzähler beider Enden, und ob der Koprozessor
Empfangs-Overflows gemeldet hat. Die Umlaufzeit legt fest, was eine
Pollperiode schaffen muss; die anderen beiden sagen, ob der Bus oder die
Software die Grenze war.

Und ein Blick auf den *Heartbeat* lohnt sich in derselben Sitzung: sein
Monoflop ist noch nicht bestückt, die Flanken erreichen also nur einen
Header-Pin und ein Oszilloskop — aber beide Platinen sind gerade gleichzeitig
unter Spannung, und das ist die billigste Gelegenheit zu prüfen, dass die
Flanken mit der Rate kommen, die [Sicherheit](Safety-de.md) verlangt.
