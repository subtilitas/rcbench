# Worum es geht

<sub>[English](Manifest.md) · **Deutsch**</sub>

Der Prüfstand beantwortet Fragen über einen Antrieb, die man sonst nur durch
Raten beantwortet, mit drei getrennten Messgeräten, oder indem man den Motor
einschickt. Eine Platine, ein Bildschirm, eine Speicherkarte. Auf dieser Seite
steht, was heute funktioniert, was den einzelnen Funktionen noch fehlt — und
was nicht gebaut wird, damit niemand darauf wartet.

## Die Vorgabe

Julians, in seinen Worten:

> - Eingang für externe Stromsensoren (I²C)
> - ESC-Programmierer für das, was geht — im Moment AM32 und BLHeli garantiert,
>   YGE mal anfragen oder über EX-Bus einfach machen
> - Wuchten von Systemen über Beschleunigungssensor und Positionssensor
> - Unterstützung Reglerprogrammierung, soweit es geht
> - Servo-Tester mit allem und scharf: SBUS und andere Protokolle
> - Servo-Programmierung, wenn mir jemand Programmiergeräte ausleiht und das
>   Reverse Engineering klappt, respektive über EX-Bus
> - Logviewer für diverse Formate

## Was jede Zeile bedeutet und wo sie steht

| Die Vorgabe | Wo es umgesetzt wird | Stand |
| --- | --- | --- |
| **Externe Stromsensoren über I²C** | am I²C des Koprozessors, nicht am Panel — dessen Bus gehört dem Touch-Controller und darf nicht warten | Schnittstelle festgelegt, Treiber fehlt |
| **ESC-Programmierer für das, was geht** — AM32 und BLHeli sicher | One-Wire Half Duplex mit 19 200 Baud und Handshake, auf einer PIO State Machine | Bildschirm gebaut und datengetrieben; auf der Leitung spricht noch kein Protokoll |
| **Wuchten eines ganzen Systems** über Beschleunigungs- und Positionssensor | beide Sensoren am Koprozessor, **auf einer gemeinsamen Zeitbasis** — genau darin steckt die Schwierigkeit einer Phasenmessung | Bildschirm und Platzierungsanleitungen gebaut; die Messung wartet auf die Sensoren |
| **Servotester, scharf und mit allem**: SBUS und weitere Protokolle | PWM-Ausgänge in Hardware, je Summensignal ein PIO-Programm | Bildschirm gebaut und über den Link angebunden; die Ausgangsstufe selbst kommt als Nächstes |
| **Servoprogrammierung**, soweit sie sich reverse-engineeren lässt | Hitecs D-Serie ist als einzige vollständig veröffentlicht; der Rest braucht ein geliehenes Programmiergerät | auf Wunsch des Eigentümers zurückgestellt |
| **Logviewer für diverse Formate** | `shared/logfile` — der CSV-Reader verkraftet auch Dezimalkommas und andere Dialekte | gebaut: Durchsehen, Importansicht, Plot — und scharfe Läufe werden auf die Karte geschrieben |

## Worauf nicht zu warten ist

Drei Türen sind zu, und das zu wissen erspart es, sie im Auge zu behalten:

- **Konfiguration über JETIs EX Bus.** Die Spezifikation erklärt Remote
  Configuration für „available only for the products of JETI model" und lässt
  die Beschreibung aus dem Dokument heraus. EX Bus liefert Kanalwerte und
  Telemetrie — Geräte darüber zu programmieren wird nicht kommen.
- **BLHeli_32-Parameter.** Der Prüfstand erkennt diese Regler und steuert sie
  an; Drehrichtung, 3D-Modus, Beacon und Save-Settings funktionieren. Die
  Parametertabelle nicht. Es liegt an einem Schlüssel und nicht am Aufwand, es
  wurde danach gefragt, und die Antwort war nein — [die ganze
  Antwort](BLHeli32-de.md).
- **Servoprogrammierung für KST** ist auf Wunsch des Eigentümers
  zurückgestellt.

Und eine Regel prägt, was ankommt: jedes Protokoll hier wird aus seiner
Spezifikation geschrieben, denn fast jede offene Implementierung steht unter
GPL oder AGPL — unvereinbar mit dem MIT dieses Repositories. Ein Protokoll
ohne veröffentlichte Spezifikation kommt deshalb spät oder gar nicht. Das ist
der ehrliche Preis der Regel.
