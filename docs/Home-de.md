# rcbench

<sub>[English](Home.md) · **Deutsch**</sub>

<sub>Diese Seiten werden aus `docs/` im Repository erzeugt und bei jedem Push
auf `main` überschrieben — bearbeite die Dateien, nicht das Wiki.</sub>

Ein **Motor-, ESC- und Servoprüfstand** in zwei Hälften: ein ESP32-S3-Bedienteil,
das entscheidet, zeichnet und speichert, und ein RP2350-Koprozessor, der misst,
treibt und alles bedient, was eine Deadline hat.

> Die Regel, die Diskussionen beendet: **dem Koprozessor gehört alles mit einer
> Deadline, dem Bedienteil alles mit einer Meinung.**

Das [README](https://github.com/subtilitas/rcbench) des Repositories ist das
laufende Protokoll — was gebaut ist, was nicht, und was noch offen ist. Diese
Seiten sind die Referenz dahinter, und sie werden von dem Commit geschrieben,
der den beschriebenen Code landet, nicht vorher. Eine fehlende Seite ist ein
Subsystem, das es noch nicht gibt.

## Seiten

| Seite | Worum es geht |
| --- | --- |
| [Worum es geht](Manifest-de.md) | Der Anspruch, was jede Zeile davon bedeutet, und wo es steht |
| [Bildschirme](Screens-de.md) | Die Hülle: das Statusband, der Splash, das Menü, und wie ein Bildschirm dazukommt |
| [Der Link](Link-de.md) | Der Frame, das Page- und Registermodell, der Decoder, und was die Leitung kostet |
| [Sicherheit](Safety-de.md) | Der Heartbeat, und die drei unabhängigen Arten, wie dieser Prüfstand anhält |
| [Performance](Performance-de.md) | Warum die Frame Rate ist, wie sie ist, und die zwei Befunde, die jeden Bildschirm prägen |
| [Bauen](Building-de.md) | Der Baum, die drei Toolchains, und was CI prüft |
| [Den Link in Betrieb nehmen](Bringup-de.md) | Die beiden Platinen verkabeln und die Leitung beweisen, bevor der Code schuld ist |
| [Servoverfahren](Servo-de.md) | Die verbaute Endlage über den Strom finden, und zwei Servos auf einer Fläche abstimmen |
| [Empfängerbusse](Receivers-de.md) | S.BUS, iBUS, SUMD, CRSF, SRXL2 und JETI EX Bus, und was jeder im Decoder kostet |
| [Das OpenYGE-Protokoll](OpenYGE-de.md) | Das ESC-Telemetrieprotokoll, von der Leitung aufwärts spezifiziert |
| [BLHeli_32-Parameter](BLHeli32-de.md) | Warum dieser Prüfstand sie erkennt und ansteuert, aber keine Einstellung benennt |

Jede Seite gibt es auch auf Englisch — der Umschalter steht oben auf jeder.
