# Worum es geht

<sub>[English](Manifest.md) · **Deutsch**</sub>

Der Prüfstand beantwortet Fragen über einen Antrieb, die man sonst durch Raten
beantwortet, durch den Kauf von drei getrennten Kisten, oder indem man den
Motor wegschickt. Eine Platine, ein Bildschirm, eine Karte. Diese Seite ist,
was er heute kann, was jeder Funktion noch fehlt, und was nicht gebaut wird —
damit du weißt, was dich erwartet und worauf du nicht warten musst.

## Der Anspruch

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

## Was jede Zeile bedeutet, und wo sie steht

| Der Anspruch | Wo es landet | Stand |
| --- | --- | --- |
| **Externe Stromsensoren über I²C** | am eigenen Bus des Koprozessors, nicht am Bedienteil — dessen Bus gehört dem Touch-Controller und darf nie verzögert werden | Schnittstelle benannt, Treiber nicht geschrieben |
| **ESC-Programmierer für das, was geht** — AM32 und BLHeli sicher | One-Wire Half Duplex mit 19 200 und einem Handshake, auf einer PIO State Machine | nicht begonnen; braucht niemandes Erlaubnis |
| **Ein ganzes System auswuchten** mit Beschleunigungs- und Positionssensor | der Koprozessor hält beide, **auf einer Zeitbasis** — worin die ganze Schwierigkeit einer Phasenmessung besteht | nicht begonnen |
| **Servotester, scharf und mit allem**: SBUS und die anderen Protokolle | PWM in Hardware nach außen, ein PIO-Programm je Summensignalprotokoll | nicht begonnen |
| **Servoprogrammierung**, wo sie sich reverse-engineeren lässt | Hitecs D-Serie ist die einzige vollständig veröffentlichte; der Rest wartet auf ein geliehenes Programmiergerät | auf Wunsch des Eigentümers zurückgestellt |
| **Logviewer für diverse Formate** | `shared/logfile` — der locale-tolerante CSV-Reader ist portiert und getestet | Reader gebaut, Bildschirm noch nicht neu geschnitten |

## Worauf nicht zu warten ist

Drei Türen sind zu, und das zu wissen erspart dir, sie zu beobachten:

- **Konfiguration über JETIs EX Bus.** Die Spezifikation sagt, Remote
  Configuration sei „available only for the products of JETI model", und hält
  ihre Beschreibung aus dem Dokument heraus. EX Bus liefert Kanalwerte und
  Telemetrie — ein Gerät hindurch zu programmieren kommt nicht.
- **BLHeli_32-Parameter.** Der Prüfstand erkennt und treibt diese Regler, und
  Drehrichtung, 3D-Modus, Beacon und Save-Settings funktionieren; die
  Parametertabelle nicht, und der Grund ist ein Key, kein Aufwand —
  [die ganze Antwort](BLHeli32-de.md).
- **Servoprogrammierung für KST** ist auf Wunsch des Eigentümers
  zurückgestellt.

Und eine Regel, die prägt, was ankommt: jedes Protokoll hier wird aus seiner
Spezifikation geschrieben, denn nahezu jede offene Umsetzung steht unter GPL
oder AGPL, gegen das MIT dieses Repositories. Ein Protokoll ohne
veröffentlichte Spezifikation kommt spät oder gar nicht — das ist der ehrliche
Preis der Regel.
