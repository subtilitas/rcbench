# Empfängerbusse

<sub>[English](Receivers.md) · **Deutsch**</sub>

Diese Seite beschreibt den Anschluss eines Empfängers und die Anzeige
seiner Daten auf RECEIVER.

## Unterstützung

| Bus | Leitung | Stand |
| --- | --- | --- |
| S.BUS | invertierter 8E2-UART (UART: Universal Asynchronous Receiver-Transmitter), 100kBaud, ein Pin | Decoder gebaut und getestet; das PIO-Programm (PIO: Programmable Input/Output), das die invertierte Leitung in Bytes umsetzt, ist nicht geschrieben |
| iBUS, SUMD, CRSF, SRXL2, JETI EX Bus | UART, normal oder invertiert, je ein Pin | geplant |

Jeder Bus ist eine Signalleitung plus Masse an einen Pin des IOmcu.
Externe Bauteile sind nicht nötig.

## Zustände

Keine Firmware liefert Daten an RECEIVER. Der S.BUS-Decoder ist in die
Panel-Firmware und die Host-Tests eingebunden. Auf keiner der beiden
Platinen gibt es jedoch eine Quelle für Empfangsbytes; weder
`analyser_screen_push()` noch `analyser_screen_silent()` wird aufgerufen.
Die Seite zeigt deshalb KEIN SIGNAL. Die folgenden Zustände beschreiben,
wie sie auf eingespeiste Daten reagiert. Das wird in den Host-Tests geprüft.

RECEIVER zeigt sechzehn Kanäle mit Verlauf, die zwei Digitalkanäle, die
Anzahl der Frames pro Sekunde und die Zähler für Frames, Resyncs und
fehlerhafte Endbytes (Bad tail). Das Zustandsfeld zeigt einen der folgenden
Zustände:

| Zustand | Bedeutung | Maßnahme |
| --- | --- | --- |
| LIVE | Frames kommen an, der Sender wird empfangen | die Werte sind gültig |
| FRAME LOST | dieser Frame kam nicht unversehrt an | in großer Entfernung vereinzelt; gehäuft am Prüfstand weist auf Verdrahtung oder Decoder |
| FAILSAFE | der Empfänger hat den Sender verloren und sendet seine Failsafe-Werte | als Stopp behandeln; die Werte sind formal gültig, aber keine gültigen Stellvorgaben des Senders |
| KEIN SIGNAL | nichts auf der Leitung | Empfängerversorgung, Verdrahtung, Pin |

Ein Empfänger im Failsafe sendet sechzehn plausible, gleichmäßig laufende
Kanalwerte. Schalte zum Prüfen des Failsafe-Verhaltens den Sender aus und beobachte
das Zustandsfeld. Bei den meisten Empfängern ist
das Failsafe-Verhalten einstellbar.

## S.BUS

- Keine Prüfsumme. Der Decoder erkennt Framegrenzen an der Lücke zwischen den
  Frames, nicht am Header-Byte, das auch ein gültiger Kanalwert ist.
- Frame: 25 Bytes. Header 0x0F, sechzehn 11-Bit-Kanäle lückenlos gepackt, ein
  Flags-Byte mit den zwei Digitalkanälen, Frame-lost und Failsafe, und ein
  Footer.
- Footer: laut Spezifikation 0x00; Empfänger senden auch 0x04, 0x14, 0x24 und
  0x34. Der Decoder akzeptiert ein unteres Nibble von 0x0 oder 0x4.

## Decoder-Regeln

Nutze für die Framegrenzen ein zuverlässigeres Merkmal als das Header-Byte,
wenn das Protokoll eines bietet. Decodiere bei jedem Decoder das
Failsafe-Flag vor den Kanälen. Jeder Decoder wird aus seiner Spezifikation geschrieben; die
Lizenzregel steht in
[STATUS.md](https://github.com/subtilitas/rcbench/blob/main/STATUS.md).
