---
screen: none
title: Anschlüsse
order: 61
---

# Anschlüsse

INTERFACES, die zweite Kategorie auf [SETUP](setup.md), verdrahtet das Netzteil PD mini und sagt, wie der Programmierport ein Hitec-Servo der D-Serie empfängt. Ihre 5 Einstellungen stehen in zwei Abschnitten unter den Überschriften SUPPLY und PROG-PORT; eine Überschrift ist eine eigene Zeile ohne Tasten. Die Einstellungen werden wie jede andere Einstellung auf SETUP gehalten und gespeichert. Das Panel schreibt jede Einstellung 500ms nach der letzten Änderung an den IOmcu, und nur, was von dem abweicht, was der IOmcu hält. Die Geräte, die der IOmcu liest, stehen unter [Sensoren](setup-sensors.md).

## Der PD mini

| Einstellung | Bereich | Vorgabe | Was sie tut |
|---|---|---|---|
| PD mini | EIN, AUS | AUS | das programmierbare Netzteil auf [SUPPLY](supply.md) und SERVO; AUS rechnet das Netzteilmodell des Panels |
| PD mini TX | -1 bis 47 | -1 | der GPIO des IOmcu zum DM des Moduls |
| PD mini RX | -1 bis 47 | -1 | der GPIO des IOmcu vom DP des Moduls |
| PD mini Baud | 9600, 19200, 38400, 57600, 115200, 230400, 460800, AUTO | AUTO | die UART-Rate (Universal Asynchronous Receiver-Transmitter) des Moduls; AUTO probiert jede der 7 Raten, eine pro Sekunde |

Die Tasten `-` und `+` von PD mini TX und PD mini RX springen über den GPIO, den die andere Zeile hält: mit RX auf GPIO 18 geht TX von GPIO 19 auf GPIO 17. Am Ende des Bereichs tut die Taste dann nichts: mit RX auf GPIO 47 bleibt TX auf GPIO 46. Das Tastenfeld lehnt den GPIO der anderen Zeile ab und zeigt statt des Bereichs `Diesen GPIO hält der andere PD-mini-Pin`. Eine gespeicherte Einstellung mit beiden Zeilen auf einem GPIO wird so geladen; der PD mini bleibt aus, die SUPPLY-Kopfzeile sagt `PD MINI OHNE PINS`, und beide Zeilen sagen `TX und RX auf einem Pin. Der PD mini bleibt aus.`, bis eine von ihnen verstellt wird. Der Schalter PD mini schaltet das Modul ab. Der IOmcu lehnt einen Pin ab, der reserviert, an einen Ausgang gebunden, vom Sensorbus oder vom Phasenabgriff gehalten oder der andere Pin ist: RX auf GPIO 17 wird abgelehnt, solange Sensor-SCL GPIO 17 ist. Die Meldung verlässt das Band, sobald der Pin auf einen zurückgestellt ist, den der IOmcu nimmt. Eine Änderung an den Pins, der Baudrate oder dem Einschalter schaltet den Netzteilausgang ab.

![GPIO 9 von RX für PD mini TX abgelehnt](view:setup-pdmini-why)

## Der Programmierport

| Einstellung | Bereich | Voreinstellung | Was sie tut |
|---|---|---|---|
| Hitec-D-Empfang | PULL-UP, PRECHARGE | PULL-UP | wie der IOmcu die Antwort eines Hitec-Servos der D-Serie am Programmierport empfängt. PULL-UP: ein Widerstand von 2,2kΩ von der Signalleitung zur Servoversorgung. PRECHARGE: kein Widerstand; der IOmcu treibt das Pad alle 1,09us für 100ns auf High und tastet es 113ns später ab |

Der IOmcu nimmt den Wert beim nächsten OPEN einer Leitung der D-Serie. classic-Servos (HS-5xxx, HS-7xxx) brauchen keins davon und übergehen ihn. Ein IOmcu mit 0.21.1 oder 0.21.2 (Protokoll 4.23) empfängt mit PULL-UP; mit PRECHARGE lautet die Zeile dann `IOmcu älter als 0.22.0: empfängt mit PULL-UP.` Ein IOmcu älter als 0.21.1 hat keine Hitec-Leitung; mit PRECHARGE lautet die Zeile `IOmcu älter als 0.21.1: keine Hitec-Leitung.`

## Auf dem rbmod-Carrier

![PD mini TX und RX verlötet](view:setup-rbmod-iface)

Auf dem [rbmod-Carrier](https://github.com/subtilitas/rcbench/wiki/Carrier-de) zeigt PD mini TX GPIO 7 und PD mini RX GPIO 6. Diese Zeilen haben keine Tasten und öffnen kein Tastenfeld, und ihre Hilfezeile lautet `Auf dem Board verlötet. Keine Einstellung.`. Das Panel schreibt diese Pins zum IOmcu. Die für sie gespeicherten Werte bleiben, wie sie sind, und gelten wieder an einem von Hand verdrahteten Modul.

## Verwandte Seiten

- [Setup](setup.md): Tasten, Speichern und die anderen Kategorien.
- [Sensoren](setup-sensors.md): der Sensorbus, die Strommonitore, der Winkelsensor und der Phasenabgriff.
- [Ausgänge](outputs.md): welche Pins gebunden und damit nicht frei für die Teile hier sind.
