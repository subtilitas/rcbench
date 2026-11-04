---
screen: none
title: Ruderausschlag
order: 23
---

# Ruderausschlag

RUDER, eine Aufgabe in der Leiste von [SERVO](servo.md), steuert ein Servo und zeigt, wie weit die Ruderfläche ausschlägt, die es bewegt: in Grad ab einem Nullpunkt und, mit eingegebener Länge der Fläche, in mm an der Hinterkante. Der Winkel kommt von der Quelle, die ANGLE SOURCE unter [Sensoren](setup-sensors.md) nennt: vom AS5600-Ausgangsencoder oder von der IMU, so wie ihn GEMESSEN auf LIVE nimmt, mit dem Vorzeichen in Richtung des befohlenen Winkels. Die Ausgangskarte rechts bleibt wie auf jeder Aufgabe: die Zeile ZIEL nennt, was die Aufgabe ansteuert, und ARM entscheidet, ob sich etwas bewegt.

![RUDER bei SAFE](view:servo-rudder)

## Die Anzeige

| Teil | Zeigt |
|---|---|
| AUSSCHLAG | den Winkel ab NULLPUNKT, -180.0 bis +179.9°; vor NULLPUNKT den Winkel ab ENC CENTRE des AS5600 oder ab dem Zählwert 0 der IMU; `---` ohne Messwert |
| HINTERKANTE | L x sin(Ausschlag) bei einer LÄNGE von L mm: was ein senkrecht an die Hinterkante gehaltenes Lineal anzeigt, mit Vorzeichen |
| MAX | den größten Ausschlag je Seite seit NULLPUNKT oder MAX LÖSCHEN, in ° und in mm |

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
| NULLPUNKT | nimmt den aktuellen Winkel als Neutrallage und löscht MAX; grau ohne Messwert; eine andere ANGLE SOURCE, eine andere AXIS oder SIGN der IMU oder ein NULLPUNKT oder NULLPUNKT WEG auf dem IMU-Bildschirm verwirft Nullpunkt und MAX | dasselbe |
| MAX LÖSCHEN | löscht MAX; der Nullpunkt bleibt | dasselbe |
| AUTO | zeigt den Kasten von AUTO ([AUTO](#auto)) | dasselbe |

1000us, ZENTRIEREN und 2000us wirken beim Drücken. LÄNGE, NULLPUNKT, MAX LÖSCHEN und AUTO wirken bei einem Tippen, das auf ihnen loslässt. Die Zeile PULS unter den Tasten nennt den befohlenen Puls. Ein Wechsel des Reiters lässt das Servo, wo es steht; DISARM, STOP und das Verlassen von SERVO schalten den Prüfstand auf SAFE.

## Die Statuszeilen

| Zeile | Bedeutet |
|---|---|
| `WINKEL: AS5600`, `WINKEL: IMU X+` | die Quelle liefert Messwerte; die IMU steht mit AXIS und SIGN da |
| `WINKEL: AS5600 AUS IN SETUP` | der AS5600 ist unter SETUP > SENSORS aus |
| `WINKEL: AS5600 ANTWORTET NICHT`, `WINKEL: IMU X+ ANTWORTET NICHT` | an, und kein Messwert kommt; bei der IMU auch, solange sie aus oder nicht verbunden ist |
| `WINKEL: AS5600 SIEHT KEINEN MAGNETEN` | der Sensor meldet keinen Magneten |
| `WINKEL: KEINE VERBINDUNG` | keine Verbindung zur IOmcu, der RP2350-Platine |
| `KEIN NULLPUNKT: AB SENSORMITTE` | NULLPUNKT noch nicht genommen |
| `ZUERST ARM` | der Prüfstand ist SAFE |

## AUTO

AUTO programmiert ein KST- oder Chaservo-Servo am Programmierport so, dass 1000us und 2000us den je Seite eingegebenen Ausschlag erreichen. Der Kasten nimmt den Platz von LÄNGE, den Tasten zum Ansteuern und den Statuszeilen ein; MANUELL bringt sie zurück.

![Der Kasten von AUTO, ARMED](view:servo-rudder-auto)

| Zeile | Setzt |
|---|---|
| 1000us, 2000us | den Soll-Ausschlag dieser Seite, 0.5 bis 90.0°, 20.0°; mit gesetzter LÄNGE daneben die mm an der Hinterkante |
| RESERVE | so viel über das Soll hinaus programmiert, 0 bis 50%, 10%: 20.0° bei 10% ergeben einen Sollwert von 22.0° |
| TOLERANZ | wie nah am Sollwert eine Messung liegen muss, 0.1 bis 5.0°, 0.5° |

Zuerst NULLPUNKT mit der Fläche in Neutrallage. 2s HALTEN: START startet den Lauf; die Taste heißt dann LAUF BEENDEN, und ein Druck beendet den Lauf sofort. Der Lauf liest das Servo, setzt seinen Pulsbereich auf 1000 bis 2000us, fährt dann je Seite den Endpuls an, wartet, bis der Winkel 500ms ruhig ist, und schreibt einen neuen Endpoint: höchstens 8 Schreibvorgänge je Seite, jeder in einer eigenen Sitzung des Programmierers, mit dem Servo aus- und eingeschaltet. Der Stromschutz des Endlagenlaufs gilt durchgehend: ein Blockieren vor dem Sollwert beendet den Lauf mit `Ruder klemmt bei x°`. Jedes Ende schaltet den Prüfstand auf SAFE und den Ausgang der Versorgung aus.

Die erste Zeile nennt den laufenden Schritt, die Sollwerte oder warum ein START abgelehnt ist; die zweite das Ergebnis; die dritte den Bericht RUDDRnnn.TXT, oder `ZURÜCKSPIELEN IN PROGRAMMIEREN`, wenn ein Lauf, der geschrieben hat, nicht bestanden ist.

## Was der Winkel ist

Der AS5600 sitzt auf der Servoabtriebswelle, sein Winkel ist also der der Welle. Ein Gestänge zwischen Horn und Fläche lässt beide auseinanderlaufen: die Anzeige zeigt den Ausschlag der Welle, und der Unterschied zu dem der Fläche ist nicht gemessen. Die IMU misst das, woran sie befestigt ist, auf der Fläche deren eigenen Winkel.
