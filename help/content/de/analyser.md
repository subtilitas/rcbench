---
screen: ANALYSER
title: Receiver
order: 40
---

# Receiver

RECEIVER zeigt die Daten eines S.BUS-Empfängers: sechzehn Kanäle mit ihrem Verlauf, zwei digitale Kanäle, die Frame-Rate und die Zähler des Decoders. Öffne die Seite über die Kachel RECEIVER im [Menü](overview.md). Die Kachel trägt in diesem Release die Markierung SIMULIERT.

## Am Prüfstand: KEIN SIGNAL

![Die Seite RECEIVER ohne Signal auf der Leitung](view:analyser-silent)

In diesem Release liefert keine der beiden Platinen Bytes an diese Seite. Der S.BUS-Decoder ist gebaut und getestet, aber kein Firmware-Image verbindet ihn mit einer Bytequelle. Die Seite zeigt daher KEIN SIGNAL und `Kein Signal auf der Leitung.`. Die Spuren sind leer, die Werte zeigen `---`, und unter ROHDATEN steht `Noch keine Daten dekodiert.`.

## Was ein gespeister Bildschirm zeigt

![Die Seite RECEIVER mit S.BUS-Frames aus dem Host-Renderer](view:analyser)

Der Host-Renderer liefert die Frames für die Bilder in diesem Abschnitt. Die Bilder zeigen, wie die Seite Daten aus einer Bytequelle darstellt.

| Bereich | Anzeige |
|---|---|
| CH01 bis CH16 | eine Spur je Kanal: etwa 2,8s Verlauf bei 71 Frames je Sekunde, 200 Werte, ein Balken für den aktuellen Wert und der Wert in us |
| Zustandsblock | KEIN SIGNAL, FRAME LOST, FAILSAFE oder LIVE, mit einer Zeile darunter |
| BUS | `S.BUS` |
| RATE | Frames pro Sekunde, in Hz |
| FRAMES, RESYNC, BAD TAIL | dekodierte Frames, Neusynchronisierungen, Frames mit einem Abschluss, den der Decoder ablehnt |
| DIGITAL | CH17 und CH18 als Leuchten: Der Punkt leuchtet, solange der Kanal gesetzt ist. Die Leuchten reagieren nicht auf Antippen. |

Ein Störimpuls ist eine Spitze in einer Spur. Ein Aussetzer ist eine Kerbe quer über alle sechzehn Spuren im selben Moment.

| Zustand | Zeile | Bedeutung |
|---|---|---|
| LIVE | `Signal vom Sender empfangen.` | Frames kommen an; die Werte sind gültig |
| FRAME LOST | `Ein fehlerhafter Frame wurde empfangen.` | Der Frame kam nicht unversehrt an. Häufige Fehler am Prüfstand deuten auf die Verdrahtung hin. |
| FAILSAFE | `Dies sind Ersatzwerte.` | Der Empfänger hat die Verbindung zum Sender verloren und sendet seine Failsafe-Werte. |
| KEIN SIGNAL | `Kein Signal auf der Leitung.` | Kein Frame kommt an. Prüfe die Versorgung des Empfängers, die Verdrahtung und den Pin. |

![Ein Empfänger im Failsafe: jede Spur rot](view:analyser-failsafe)

Im Zustand FAILSAFE ist jede Spur rot. Der Empfänger sendet sechzehn formal gültige Ersatzwerte anstelle der Senderbefehle. Behandle FAILSAFE als Stopp, nicht als sechzehn gültige Kanäle.

## ROHDATEN

ROHDATEN zeigt den letzten Frame als hexadezimale Bytes, 8 je Zeile. Kopf- und Abschlussbyte erscheinen in der Akzentfarbe. Darunter stehen die Bytezahl und die Pause zur Trennung der Frames, `25 Bytes; 2000us Pause zwischen den Frames`, gefolgt von den Zählern für Resync und Bad tail. Mit KANÄLE und ROHDATEN wechselst du die linke Ansicht.

## Verwandte Seiten

- [Menü](overview.md): die Marke SIMULIERT.
- [Empfängerbusse](https://github.com/subtilitas/rcbench/blob/main/docs/Receivers-de.md): der S.BUS-Frame und die Regeln des Decoders.
