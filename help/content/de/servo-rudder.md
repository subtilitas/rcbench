---
screen: none
title: Ruderausschlag
order: 23
---

# Ruderausschlag

RUDER in der Leiste von [SERVO](servo.md) steuert ein Servo und zeigt den Ruderausschlag. Die Anzeige nennt Grad ab einem Nullpunkt und bei eingegebener Ruderflächentiefe auch Millimeter an der Hinterkante. WINKELQUELLE unter [Sensoren](setup-sensors.md) wählt den AS5600-Ausgangsencoder oder die IMU. Wie bei GEMESSEN auf LIVE folgt das Vorzeichen der Richtung des Sollwinkels. Die Ausgangskarte bleibt rechts sichtbar: ZIEL nennt den angesteuerten Ausgang, und ARM entscheidet, ob er sich bewegen kann.

![RUDER bei SAFE](view:servo-rudder)

## Die Anzeige

| Teil | Anzeige |
|---|---|
| AUSSCHLAG | Winkel ab NULLPUNKT, -180,0 bis +179,9° vom AS5600, -180,00 bis +180,00° von der IMU, ab 100,00° in kleineren Ziffern. Vor dem Nullsetzen gilt SENSOR-MITTE des AS5600 oder Zählwert 0 der IMU als Bezug; ohne Messwert steht `---`. |
| HINTERKANTE | L x sin(Ausschlag) bei LÄNGE L mm: der vorzeichenbehaftete Wert eines senkrecht an die Hinterkante gehaltenen Lineals. |
| MAX | Größter Ausschlag je Seite seit NULLPUNKT oder MAX LÖSCHEN, in ° und mm. |

Der Millimeterwert ist der Abstand der Hinterkante von der Neutralebene, nicht die zurückgelegte Bogenlänge. Bei 30° und 100mm Ruderflächentiefe beträgt er 50,0mm.

## Die Seitenansicht

Die Seitenansicht steht rechts neben den Statuszeilen, mit der Nasenkante links. Sie zeigt die feste Fläche grau, das Scharnier und das Ruder als cyanfarbenen Keil im angezeigten Winkel. Positiver Ausschlag hebt die Hinterkante an. Striche markieren 0, ±30, ±60 und ±90°; gestrichelte Linien zeigen MAX je Seite. Die Zeichnung endet bei ±90°, die Zahlenanzeige läuft darüber hinaus weiter. Die gezeichnete Ruderlänge bleibt fest. Ohne Messwert liegt das Ruder gedimmt auf der Neutrallinie.

![Die Fläche bei -85,0°](view:servo-rudder-85)

## LÄNGE

LÄNGE ist der Abstand von der Scharnierlinie zur Hinterkante: 0 oder 10 bis 500mm in Schritten von 1mm. Die Einstellung bleibt nach einem Neustart erhalten. Tippe darauf, um das Tastenfeld zu öffnen. Werte von 1 bis 9mm werden mit `0 oder 10 bis 500mm` abgelehnt. Bei 0 zeigt die Anzeige nur Grad und fordert in der mm-Spalte die LÄNGE an.

![LÄNGE nicht gesetzt: nur Grad](view:servo-rudder-nolength)

## Das Servo ansteuern

![RUDER bei ARMED, auf 2000us](view:servo-rudder-armed)

| Bedienelement | SAFE | ARMED |
|---|---|---|
| 1000us, 2000us | gesperrt und gedimmt | gibt diesen Puls vor, begrenzt durch WEG und den Pulsbereich des eingestellten Typs |
| ZENTRIEREN | gesperrt und gedimmt | gibt PULS MITTE plus TRIMMUNG vor |
| Drehknopf | gesperrt; Drehungen werden verworfen | bewegt das Horn ausgehend vom Sollwert, wie auf LIVE; bei laufendem AUTO gesperrt |
| NULLPUNKT | setzt den aktuellen Winkel als Neutrallage und löscht MAX; ohne Messwert grau. Änderungen an WINKELQUELLE, IMU-Achse oder IMU-Vorzeichen sowie NULLPUNKT oder NULLPUNKT LÖSCHEN auf der IMU-Seite verwerfen Nullpunkt und MAX. | ebenso; bei laufendem AUTO gesperrt |
| MAX LÖSCHEN | löscht MAX und behält den Nullpunkt | ebenso; bei laufendem AUTO gesperrt |
| AUTO | öffnet den [AUTO-Bereich](#auto) | ebenso |

1000us, ZENTRIEREN und 2000us wirken beim Drücken. LÄNGE, NULLPUNKT, MAX LÖSCHEN und AUTO wirken, wenn du einen Tipp darauf loslässt. PULS unter den Tasten zeigt den Sollpuls. Beim Reiterwechsel bleibt das Servo an seiner Position. DISARM, STOP oder das Verlassen von SERVO schaltet den Prüfstand auf SAFE.

## Die Statuszeilen

| Zeile | Bedeutung |
|---|---|
| `WINKEL: AS5600`, `WINKEL: IMU X+` | Quelle liefert Messwerte; bei der IMU stehen Achse und Vorzeichen dabei. |
| `WINKEL: AS5600 AUS IN SETUP` | AS5600 unter SETUP > SENSORS ausgeschaltet. |
| `WINKEL: AS5600 ANTWORTET NICHT`, `WINKEL: IMU X+ ANTWORTET NICHT` | Quelle eingeschaltet, aber ohne Messwert; bei der IMU auch ausgeschaltet oder nicht verbunden. |
| `WINKEL: AS5600 SIEHT KEINEN MAGNETEN` | Sensor meldet keinen Magneten. |
| `WINKEL: KEINE VERBINDUNG` | Keine Verbindung zum IOmcu, der RP2350-Platine. |
| `KEIN NULLPUNKT: AB SENSORMITTE` | NULLPUNKT noch nicht gesetzt. |
| `ZUERST ARM` | Prüfstand ist SAFE. |

## AUTO

AUTO programmiert ein KST-, Chaservo- oder Hitec-Servo am Programmierport so, dass 1000us und 2000us den je Seite vorgegebenen Ausschlag erreichen. Die [Option SERVO des Endlagenlaufs](servo-settings.md#die-optionen-des-endlagenlaufs) übernimmt die zuletzt unter PROGRAMMIEREN geöffnete Servoprogrammierung. Die dritte Zeile nennt sie, etwa `SERVO HITEC D-SERIE`.

Der AUTO-Bereich ersetzt LÄNGE, Ansteuertasten und Statuszeilen; MANUELL stellt sie wieder her. Die Seitenansicht bleibt sichtbar und ergänzt beide Sollwerte als bernsteinfarbene gestrichelte Linien. 1000us liegt auf der negativen, 2000us auf der positiven Seite, oder jeweils auf der vom letzten Lauf gemessenen Seite.

![Der Kasten von AUTO, ARMED](view:servo-rudder-auto)

| Zeile | Einstellung |
|---|---|
| 1000us, 2000us | Soll-Ausschlag je Seite, 0,5 bis 90,0°, Vorgabe 20,0°. Bei gesetzter LÄNGE stehen daneben die mm an der Hinterkante. |
| RESERVE | Anteil über dem Soll-Ausschlag, 0 bis 50%, Vorgabe 10%: 20,0° bei 10% ergeben einen Zielwert von 22,0°. |
| TOLERANZ | Zulässige Abweichung vom Zielwert, 0,1 bis 5,0°, Vorgabe 0,5°. |

Setze zuerst NULLPUNKT bei neutraler Ruderfläche. Während des Laufs sind NULLPUNKT, MAX LÖSCHEN und MANUELL gesperrt. 2s HALTEN: START beginnt den Lauf. Danach heißt die Taste LAUF BEENDEN; ein Druck beendet ihn sofort.

Der Lauf liest das Servo aus und setzt bei KST den Pulsbereich auf 1000 bis 2000us. Je Seite fährt er den Endpuls an, wartet auf einen 500ms lang ruhigen Winkel und schreibt einen neuen Endpunkt. Mit der IMU und einem [IMU-Filter](setup-sensors.md#die-imu) über 1 wartet er zusätzlich, bis der gefilterte Winkel eingeschwungen ist, bis zu 3000ms plus das längste Einschwingen des Filters, das der Bericht nennt. Er schreibt höchstens 8-mal je Seite, jeweils in einer eigenen Programmiersitzung mit Aus- und Einschalten des Servos. Der Stromschutz des Endlagenlaufs bleibt durchgehend aktiv. Blockiert das Ruder vor dem Zielwert, endet der Lauf mit `Ruder klemmt bei x°`. Bei jedem Ende schaltet der Prüfstand auf SAFE und der Netzteilausgang aus.

Die erste Zeile zeigt den laufenden Schritt, die Zielwerte oder den Grund für einen abgelehnten START. Die zweite nennt das Ergebnis, die dritte den Bericht RUDDRnnn.TXT. Hat ein nicht bestandener Lauf Einstellungen geschrieben oder einen Schreibvorgang abgebrochen, steht dort stattdessen `ZURÜCKSPIELEN IN PROGRAMMIEREN`, bei Hitec `HITEC001 IN DATEIEN LADEN`.

## Was der Winkel ist

Der AS5600 misst den Winkel der Servoabtriebswelle. Durch ein Gestänge kann der Ruderausschlag vom Wellenwinkel abweichen; diese Differenz wird nicht gemessen. Die IMU misst den Winkel des Teils, an dem sie befestigt ist. Auf der Ruderfläche misst sie deren eigenen Winkel.
