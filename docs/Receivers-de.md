# Empfängerbusse

<sub>[English](Receivers.md) · **Deutsch**</sub>

Einen Empfänger an den Prüfstand anschließen und seine Ausgabe auf dem
Analyser-Bildschirm lesen.

## Unterstützung

| Bus | Leitung | Stand |
| --- | --- | --- |
| S.BUS | invertierter 8E2-UART (UART: Universal Asynchronous Receiver-Transmitter), 100 kBaud, ein Pin | Decoder gebaut und getestet; das PIO-Programm (PIO: Programmable Input/Output), das die invertierte Leitung in Bytes umsetzt, ist nicht geschrieben |
| iBUS, SUMD, CRSF, SRXL2, JETI EX Bus | UART, normal oder invertiert, je ein Pin | geplant |

Jeder Bus ist eine Signalleitung plus Masse an einen Pin des Koprozessors.
Externe Bauteile sind nicht nötig.

## Zustände

Der Analyser zeigt sechzehn Kanäle mit Verlauf, die zwei Digitalkanäle, die
Frame Rate und die Zähler (Frames, Resyncs, Bad Footers). Der Zustandsblock
zeigt einen von:

| Zustand | Bedeutung | Maßnahme |
| --- | --- | --- |
| LIVE | Frames kommen an, der Sender wird empfangen | die Werte sind gültig |
| FRAME LOST | dieser Frame kam nicht unversehrt an | in großer Entfernung vereinzelt; gehäuft am Prüfstand weist auf Verdrahtung oder Decoder |
| FAILSAFE | der Empfänger hat den Sender verloren und sendet seine Failsafe-Werte | als Stopp behandeln; die Werte sind wohlgeformt und bedeutungslos |
| SILENT | nichts auf der Leitung | Empfängerversorgung, Verdrahtung, Pin |

Ein Empfänger im Failsafe sendet sechzehn plausible, gleichmäßig laufende
Kanalwerte. Zum Prüfen des Failsafe-Verhaltens eines Empfängers den Sender
ausschalten und den Zustandsblock beobachten. Bei den meisten Empfängern ist
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

Für jeden Decoder: Framegrenzen an etwas Stärkerem als einem Header-Byte
erkennen, wo das Protokoll es hergibt, und das Failsafe-Flag vor den Kanälen
decodieren. Jeder Decoder wird aus seiner Spezifikation geschrieben; die
Lizenzregel steht in
[STATUS.md](https://github.com/subtilitas/rcbench/blob/main/STATUS.md).
