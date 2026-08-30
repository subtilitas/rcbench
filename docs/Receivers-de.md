# Empfängerbusse

<sub>[English](Receivers.md) · **Deutsch**</sub>

Einen Empfänger an den Prüfstand anschließen und sehen, was er wirklich sendet
— auch das, was ein Servo vor dir versteckt.

## Was unterstützt wird

| Bus | Leitung | Stand |
| --- | --- | --- |
| **S.BUS** | invertierter 8E2-UART, 100 kBaud, ein Pin | Decoder gebaut und getestet; das PIO-Programm, das die invertierte Leitung in Bytes verwandelt, ist noch nicht geschrieben |
| iBUS, SUMD, CRSF, SRXL2, JETI EX Bus | gewöhnlicher oder invertierter UART, je ein Pin | geplant — jeder ist etwa ein Tag Decoderarbeit, sobald Hardware zum Gegentesten auf dem Tisch liegt |

Jeder Bus ist eine Signalleitung plus Masse an einen freien Koprozessorpin.
Nichts braucht zusätzliche Bauteile.

## Was der Analyser dir zeigt

Sechzehn Kanäle mit ihrer Bewegung, die zwei digitalen Kanäle, die Frame Rate
und die Zähler — Frames, Resyncs, Bad Tails. Wie die Anzeige selbst zu lesen
ist, steht unter [Bildschirme](Screens-de.md). Hier zählt, was die Zustände
bedeuten:

| Zustand | Bedeutung | Was tun |
| --- | --- | --- |
| **LIVE** | Frames kommen an, Sender gehört | den Zahlen trauen |
| **FRAME LOST** | dieser Frame kam nicht heil an | vereinzelt auf Reichweite normal; häufig am Prüfstand ist es ein Verdrahtungs- oder Decoderproblem |
| **FAILSAFE** | der Empfänger hat **den Sender verloren** und erfindet alle sechzehn Werte | als *Stopp* behandeln — die Zahlen sind einwandfrei geformt und bedeuten nichts |
| **SILENT** | nichts auf der Leitung | Empfängerversorgung, Verdrahtung, oder der falsche Pin |

Der Failsafe-Zustand ist der Grund, warum es diesen Bildschirm gibt. Sechzehn
plausible, sich weich bewegende Kanalwerte sind genau das, was ein Empfänger im
Failsafe sendet, und nichts dahinter kann es erkennen. Benutze den Prüfstand,
um zu prüfen, was **dein** Empfänger im Failsafe wirklich tut — Sender
ausschalten und zusehen —, denn dieses Verhalten ist das, was dein Modell im
schlechtestmöglichen Moment tun wird, und bei den meisten Empfängern ist es
einstellbar.

## S.BUS-Wissenswertes

- Das Protokoll hat **keine Prüfsumme**. Der Decoder framt auf der stillen
  Lücke zwischen Frames, statt dem Header-Byte zu trauen — er kann also nicht
  mitten in einem Frame einrasten und sechzehn plausible, aber falsche Kanäle
  melden.
- Futabas Spezifikation sagt, der Footer sei `0x00`; Empfänger im Feld senden
  auch `0x04`, `0x14`, `0x24` und `0x34`. Der Prüfstand akzeptiert sie — sie
  abzulehnen hieße Hardware abzulehnen, die funktioniert.
- Die Kanäle sind sechzehn 11-Bit-Werte, lückenlos gepackt; die zwei digitalen
  Kanäle und die Failsafe-/Frame-lost-Flags reisen im Flags-Byte.

## Für Implementierer

Zwei Regeln, die von S.BUS in jeden folgenden Decoder übergehen: **framen auf
etwas Besserem als einem magischen Byte**, wo das Protokoll eines hergibt, und
**das Failsafe-Flag vor den Kanälen decodieren**, denn ein Protokoll, das
überzeugend lügen kann, wird es tun. Jeder Decoder wird aus seiner
Spezifikation geschrieben, nicht aus fremdem Code — die Lizenzbegründung steht
in [dem Protokoll](https://github.com/subtilitas/rcbench#readme).
