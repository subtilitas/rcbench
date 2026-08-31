# Empfängerbusse

<sub>[English](Receivers.md) · **Deutsch**</sub>

Einen Empfänger an den Prüfstand anschließen und sehen, was er wirklich sendet
— auch das, was an einem angeschlossenen Servo nie sichtbar würde.

## Was unterstützt wird

| Bus | Leitung | Stand |
| --- | --- | --- |
| **S.BUS** | invertierter 8E2-UART, 100 kBaud, ein Pin | Decoder fertig und getestet; das PIO-Programm, das die invertierte Leitung in Bytes verwandelt, fehlt noch |
| iBUS, SUMD, CRSF, SRXL2, JETI EX Bus | normaler oder invertierter UART, je ein Pin | geplant — jeder etwa ein Tag Decoderarbeit, sobald Hardware zum Testen auf dem Tisch liegt |

Jeder Bus ist eine Signalleitung plus Masse an einen freien Pin des
Koprozessors. Zusätzliche Bauteile braucht keiner.

## Was der Analyser zeigt

Sechzehn Kanäle mit Verlauf, die zwei digitalen Kanäle, die Frame Rate und die
Zähler — Frames, Resyncs, Bad Tails. Wie die Anzeige selbst zu lesen ist,
steht unter [Bildschirme](Screens-de.md). Hier geht es darum, was die
Zustände bedeuten:

| Zustand | Bedeutung | Was tun |
| --- | --- | --- |
| **LIVE** | Frames kommen an, der Sender wird gehört | den Zahlen trauen |
| **FRAME LOST** | dieser Frame kam nicht unversehrt an | in großer Entfernung vereinzelt normal; direkt am Prüfstand deutet Häufung auf Verdrahtung oder Decoder |
| **FAILSAFE** | der Empfänger hat **den Sender verloren** und erfindet alle sechzehn Werte | als *Stopp* behandeln — die Zahlen sind tadellos geformt und bedeuten nichts |
| **SILENT** | nichts auf der Leitung | Empfängerversorgung, Verdrahtung, oder der falsche Pin |

Der Failsafe-Zustand ist der Grund für diesen Bildschirm. Sechzehn plausible,
weich laufende Kanalwerte sind genau das, was ein Empfänger im Failsafe
sendet — und nichts dahinter kann das erkennen. Prüfe am Prüfstand, was
**dein** Empfänger im Failsafe wirklich tut: Sender ausschalten und zusehen.
Genau dieses Verhalten zeigt dein Modell im dümmsten Moment, und bei den
meisten Empfängern lässt es sich einstellen.

## Wissenswertes zu S.BUS

- Das Protokoll hat **keine Prüfsumme**. Der Decoder erkennt Framegrenzen an
  der Sendepause zwischen den Frames, statt dem Header-Byte zu trauen — er
  kann also nicht mitten in einem Frame einrasten und sechzehn plausible, aber
  falsche Kanäle melden.
- Laut Futabas Spezifikation ist der Footer `0x00`; Empfänger im Feld senden
  auch `0x04`, `0x14`, `0x24` und `0x34`. Der Prüfstand akzeptiert das — sonst
  würde er Hardware ablehnen, die funktioniert.
- Die Kanäle sind sechzehn 11-Bit-Werte, lückenlos gepackt; die zwei digitalen
  Kanäle und die Failsafe-/Frame-lost-Flags stecken im Flags-Byte.

## Für Implementierer

Zwei Regeln gehen von S.BUS auf jeden weiteren Decoder über: **Framegrenzen an
etwas Besserem als einem Magic Byte erkennen**, wo das Protokoll etwas
hergibt, und **das Failsafe-Flag vor den Kanälen decodieren** — ein Protokoll,
das überzeugend lügen kann, wird es tun. Jeder Decoder wird aus seiner
Spezifikation geschrieben, nicht aus fremdem Code; die Lizenzgründe stehen in
[dem Protokoll](https://github.com/subtilitas/rcbench/blob/main/STATUS.md).
