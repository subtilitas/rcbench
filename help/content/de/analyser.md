---
screen: ANALYSER
title: Receiver
order: 40
---

# Receiver

RECEIVER zeigt, was ein Empfänger auf einer S.BUS-Leitung sendet: sechzehn Kanäle mit ihrem Verlauf, die beiden digitalen Kanäle, die Frame Rate und die Zähler des Decoders. Der Bildschirm öffnet über die Kachel RECEIVER im [Menü](overview.md). Die Kachel trägt in diesem Release die Marke SIMULIERT.

## Am Prüfstand: KEIN SIGNAL

![Der RECEIVER-Bildschirm ohne Signal auf der Leitung](view:analyser-silent)

In diesem Release speist kein Firmware-Image diesen Bildschirm. Der S.BUS-Decoder ist gebaut und getestet, aber keine der beiden Platinen hat eine Bytequelle für ihn. Der Bildschirm zeigt daher KEIN SIGNAL und `Kein Signal auf der Leitung.`. Die Spuren sind leer, die Werte zeigen `---`, und ROHDATEN zeigt `Noch keine Daten dekodiert.`.

## Was ein gespeister Bildschirm zeigt

![Der RECEIVER-Bildschirm, im Host-Renderer mit S.BUS-Frames gespeist](view:analyser)

Die Bilder dieses Abschnitts stammen aus dem Host-Renderer, der den Bildschirm mit Frames speist. Sie zeigen, was der Bildschirm zeichnet, sobald es eine Bytequelle gibt.

| Bereich | Zeigt |
|---|---|
| CH01 bis CH16 | eine Spur je Kanal: etwa 1,7 s Verlauf, 118 Werte, ein Balken für den aktuellen Wert und den Wert in us |
| Zustandsblock | KEIN SIGNAL, FRAME LOST, FAILSAFE oder LIVE, mit einer Zeile darunter |
| BUS | `S.BUS` |
| RATE | Frames pro Sekunde, in Hz |
| FRAMES, RESYNC, BAD TAIL | dekodierte Frames, Neusynchronisierungen, Frames mit einem Abschluss, den der Decoder ablehnt |
| DIGITAL | CH17 und CH18, leuchtend, solange gesetzt |

Ein Störimpuls ist eine Spitze in einer Spur. Ein Aussetzer ist eine Kerbe quer über alle sechzehn Spuren im selben Moment.

| Zustand | Zeile | Bedeutung |
|---|---|---|
| LIVE | `Signal vom Sender empfangen.` | Frames kommen an; die Werte sind gültig |
| FRAME LOST | `Ein fehlerhafter Frame wurde empfangen.` | dieser Frame kam nicht unversehrt an; häufig am Prüfstand deutet das auf die Verdrahtung |
| FAILSAFE | `Dies sind Ersatzwerte.` | der Empfänger hat den Sender verloren und sendet seine Failsafe-Werte |
| KEIN SIGNAL | `Kein Signal auf der Leitung.` | kein Frame; Versorgung des Empfängers, Verdrahtung und Pin prüfen |

![Ein Empfänger im Failsafe: jede Spur rot](view:analyser-failsafe)

Im FAILSAFE ist jede Spur rot gezeichnet. Der Empfänger sendet sechzehn formal gültige Werte, die er selbst erzeugt hat. FAILSAFE als Stopp behandeln, nicht als sechzehn gültige Kanäle.

## ROHDATEN

ROHDATEN zeigt den letzten Frame als hexadezimale Bytes, 8 je Zeile. Kopf- und Abschlussbyte stehen in der Akzentfarbe. Darunter: die Bytezahl und die Pause, an der der Decoder Frames trennt, `25 Bytes; 2000 us Pause trennen die Frames`, dann die Zähler für Resync und Bad tail. KANÄLE und ROHDATEN wechseln die linke Ansicht.

## Verwandte Seiten

- [Menü](overview.md): die Marke SIMULIERT.
- [Empfängerbusse](https://github.com/subtilitas/rcbench/blob/main/docs/Receivers-de.md): der S.BUS-Frame und die Regeln des Decoders.
