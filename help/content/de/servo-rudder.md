---
screen: none
title: Ruderausschlag
order: 23
---

# Ruderausschlag

RUDER, eine Aufgabe in der Leiste von [SERVO](servo.md), steuert ein Servo und zeigt, wie weit die Ruderfläche ausschlägt, die es bewegt: in Grad ab einem Nullpunkt und, mit eingegebener Länge der Fläche, in mm an der Hinterkante. Der Winkel ist der des AS5600-Ausgangsencoders, so wie ihn GEMESSEN auf LIVE nimmt, mit dem Vorzeichen in Richtung des befohlenen Winkels. Die Ausgangskarte rechts bleibt wie auf jeder Aufgabe: die Zeile ZIEL nennt, was die Aufgabe ansteuert, und ARM entscheidet, ob sich etwas bewegt.

![RUDER bei SAFE](view:servo-rudder)

## Die Anzeige

| Teil | Zeigt |
|---|---|
| AUSSCHLAG | den Winkel ab NULLPUNKT, -180.0 bis +179.9°; vor NULLPUNKT den Winkel ab ENC CENTRE des AS5600; `---` ohne Messwert |
| HINTERKANTE | L x sin(Ausschlag) bei einer LÄNGE von L mm: was ein senkrecht an die Hinterkante gehaltenes Lineal anzeigt, mit Vorzeichen |
| MAX | den größten Ausschlag je Seite seit NULLPUNKT oder MAX ZURÜCKSETZEN, in ° und in mm |

Die mm sind der Abstand der Hinterkante von der Neutralebene, nicht der Bogen, den sie zurücklegt. Bei 30° zeigt eine Fläche von 100mm 50.0mm.

## LÄNGE

LÄNGE ist die Fläche von der Scharnierlinie bis zur Hinterkante: 0 oder 10 bis 500mm in Schritten von 1mm, über einen Neustart gespeichert. Ein Tippen öffnet die Zifferntastatur. 1 bis 9mm werden abgelehnt, und die Tastatur zeigt `0 oder 10 bis 500mm`. Mit LÄNGE auf 0 zeigt die Anzeige nur Grad und fragt in der mm-Spalte nach der LÄNGE.

![LÄNGE nicht gesetzt: nur Grad](view:servo-rudder-nolength)

## Das Servo ansteuern

![RUDER bei ARMED, auf 2000us](view:servo-rudder-armed)

| Bedienelement | SAFE | ARMED |
|---|---|---|
| 1000us, 2000us | abgelehnt; gedimmt | befiehlt diesen Puls, begrenzt auf WEG und den Pulsbereich des eingestellten Typs |
| ZENTRIEREN | abgelehnt; gedimmt | befiehlt PULS MITTE plus TRIMMUNG |
| Drehknopf | abgelehnt; die Drehung wird nicht gemerkt | bewegt das Horn vom befohlenen Wert aus, wie auf LIVE |
| NULLPUNKT | nimmt den aktuellen Winkel als Neutrallage und löscht MAX; grau ohne Messwert | dasselbe |
| MAX ZURÜCKSETZEN | löscht MAX; der Nullpunkt bleibt | dasselbe |

1000us, ZENTRIEREN und 2000us wirken beim Drücken. LÄNGE, NULLPUNKT und MAX ZURÜCKSETZEN wirken bei einem Tippen, das auf ihnen loslässt. Die Zeile PULS unter den Tasten nennt den befohlenen Puls. Ein Wechsel des Reiters lässt das Servo, wo es steht; DISARM, STOP und das Verlassen von SERVO schalten den Prüfstand auf SAFE.

## Die Statuszeilen

| Zeile | Bedeutet |
|---|---|
| `WINKEL: AS5600` | der Encoder liefert Messwerte |
| `WINKEL: AS5600 AUS IN SETUP` | der AS5600 ist unter SETUP > SENSORS aus |
| `WINKEL: AS5600 ANTWORTET NICHT` | an, und kein Messwert kommt |
| `WINKEL: AS5600 SIEHT KEINEN MAGNETEN` | der Sensor meldet keinen Magneten |
| `WINKEL: KEINE VERBINDUNG` | keine Verbindung zur IOmcu, der RP2350-Platine |
| `KEIN NULLPUNKT: AB SENSORMITTE` | NULLPUNKT noch nicht genommen |
| `ZUERST ARM` | der Prüfstand ist SAFE |

## Was der Winkel ist

Der AS5600 sitzt auf der Servoabtriebswelle, der Winkel ist also der der Welle. Ein Gestänge zwischen Horn und Fläche lässt beide auseinanderlaufen: die Anzeige zeigt den Ausschlag der Welle, und der Unterschied zu dem der Fläche ist nicht gemessen.
