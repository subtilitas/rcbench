# Worum es geht

<sub>[English](Manifest.md) · **Deutsch**</sub>

Den Prüfstand gibt es, um Fragen über einen Antrieb zu beantworten, die man
sonst durch Raten beantwortet, durch den Kauf von drei getrennten Kisten, oder
indem man den Motor irgendwohin schickt. Eine Platine, ein Bildschirm, eine
Karte.

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

Eine Funktion steht nur implizit im Anspruch, und alles andere braucht sie: der
ESC-Tester, der die Zahlen erzeugt. Sein Gegenstück, der Logger, der sie auf die
Karte schreibt, damit der Viewer etwas zu öffnen hat, ist nicht gebaut — und
nichts blockiert ihn, was eine andere und unbequemere Lage ist als blockiert zu
sein.

## Was das über Prioritäten sagt

**Nichts hier ist ein Consumer-Gerät.** Jede Zeile ist eine Werkstattaufgabe —
messen, programmieren, auswuchten, das Log zurücklesen. Deshalb ist die UI dicht
und kontraststark statt freundlich, und deshalb sagt ein Bildschirm, der etwas
nicht kann, welche Entscheidung fehlt, statt die Funktion zu verstecken.

**Die Protokolle sind der lange Pol, nicht die UI.** SBUS, BLHeli und AM32
seriell, DShot-Telemetrie: der meiste verbleibende Aufwand besteht darin, mit
fremder Firmware korrekt zu sprechen. Deshalb sind die Teile, die sich in reinem
C festnageln *lassen* — der Rasterisierer, das Parsen, das Settings-Modell,
[der Link](Link-de.md) — genau das, und bis auf wenige Prozent auf einem Laptop
getestet. Wenn die Protokollarbeit anfängt, soll der Boden darunter nicht
wackeln.

**„Soweit es geht" ist eine echte Einschränkung, keine Bescheidenheit.** Ein
Servo-Programmiergerät muss geliehen werden. Reverse Engineering kann
scheitern. Und eine Tür, auf die sich der Anspruch gestützt hat, ist zu: Remote
Configuration über JETIs EX Bus ist laut eigener Spezifikation „available only
for the products of JETI model", und ihre Beschreibung ist nicht Teil dieses
Dokuments. Was EX Bus liefert, sind Kanalwerte und Telemetrie. Zum
Programmieren bleiben die Wege, die es immer gab — den Hersteller fragen, ein
Programmiergerät leihen und mithören, oder reverse-engineeren.

## Eine Lizenzbedingung, die jeden Protokoll-Commit prägt

Nahezu jede offene Umsetzung dieser Protokolle steht unter GPL oder AGPL, gegen
das MIT dieses Repositories. Die permissiven Ausnahmen sind PX4s
Empfänger-Decoder (BSD) und MIT-Referenzcode für SRXL2, JETI EX Bus, DShot und
DroneCAN. Alles andere wird aus der Spezifikation geschrieben, nicht aus
fremdem Code.
