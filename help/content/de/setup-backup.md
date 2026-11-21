---
screen: none
title: Sicherung der Einstellungen
order: 61
---

# Sicherung der Einstellungen

Das Panel hält seine Einstellungen im NVS (non-volatile storage) seines eigenen Flash. Ein vollständiges Flashen löscht sie und mit ihnen die Bindungen von Dial, Knoten und IMU. Die Sicherung legt Kopien auf die Karte, in `BENCH/SETUP`, als Textdateien, die ein Mensch liest und bearbeitet.

## Einstellungen sichern und Einstellungen laden

Die letzten beiden Zeilen von [SETUP > SD CARD](setup.md#sd-card):

![SD CARD am Ende der Liste](view:setup-card-backup)

| Zeile | Was Antippen tut |
|---|---|
| Einstellungen sichern | schreibt die gespeicherten Einstellungen als `BENCH/SETUP/SETTINGS.TXT` und ersetzt die vorhandene. Die Zeile zeigt `SCHREIBT`, `GESCHRIEBEN`, `KARTE VOLL` oder `FEHLER, CODE n`. Ohne Karte nimmt sie kein Antippen, ebenso nicht, solange SAVE angeboten ist: Die Datei enthält, was das Panel gespeichert hat, keine Änderung. |
| Einstellungen laden | öffnet BACKUP. Bei ARMED nimmt sie kein Antippen. |

## Die automatischen Kopien

Jedes Mal, wenn das Panel seine Einstellungen mit eingesteckter Karte ins NVS schreibt, schreibt es auch eine Kopie: `AUTO1.TXT` ist die neueste, `AUTO2.TXT` und `AUTO3.TXT` die beiden davor. Eine Kopie wird zuerst ganz als `AUTO.TMP` geschrieben, erst dann rücken die anderen weiter; eine volle Karte lässt die Kopien, wie sie waren. Eine Kopie, die die Karte nicht erreicht, steht einmal im Meldeband, `Einstellungskopie nicht geschrieben: Die Karte ist voll.` oder `Einstellungskopie nicht geschrieben: Karten-Code n.`; der nächste Fehler steht erst nach einer gelungenen Kopie wieder dort. Eine fehlgeschlagene Kopie ist kein Fehler des Prüfstands: Ausgänge, Netzteil und ARMED bleiben, wie sie sind.

## BACKUP

![BACKUP mit der neuesten Kopie gewählt](view:setup-backup)

- Jede Datei zeigt ihr Datum und die Panel-Version, die sie schrieb. `Datum unbekannt`: Die Uhr des Panels war nicht gestellt; das Panel stellt sie nur über das Netz.
- Tippe auf eine Datei, um sie zu wählen. 2s HALTEN ZUM LADEN lädt sie; Loslassen vor 2s zeigt `Halte 2s, um die Einstellungsdatei zu laden.` Bei ARMED nimmt die Taste keinen Druck.
- ZURÜCK oder das Home-Tag führt zurück zu SD CARD.

Laden ersetzt alle Einstellungen: durch den Wert der Datei oder den Standardwert, wo die Datei keinen hat. Die Werte gelten sofort und werden ins NVS geschrieben, sobald der Prüfstand ruht. Die Antwort nennt jede Zeile, die das Panel abgelehnt hat, mit Zeilennummer, Schlüssel, Code und Grund:

![Eine von Hand bearbeitete Datei geladen, drei Zeilen abgelehnt](view:setup-backup-loaded)

| Code | Grund |
|---|---|
| 1 | nicht Schlüssel = Wert |
| 2 | unbekannter Schlüssel |
| 3 | Schlüssel doppelt: Die erste Zeile zählt |
| 4 | Wert nicht lesbar: keine Zahl, keine Option, nicht ON oder OFF, ein Text ohne Anführungszeichen |
| 5 | außerhalb des Bereichs, oder ein Pin, den die Einstellung nicht nimmt |
| 6 | Text zu lang: über 23 Bytes |
| 7 | folgt einer anderen: `sns_scl` folgt Sensor SDA |
| 8 | belegt, bleibt |

Eine Datei ohne `end` als letzte Zeile ist abgeschnitten und lädt nichts: `abgelehnt: Code 6`. Eine Datei eines neueren Formats zeigt Code 5.

## Das Angebot beim Start

Ein Panel, das nur mit den Standardwerten startet, liest die neueste Datei der Karte. Hält sie andere Einstellungen, öffnet BACKUP über dem [Menü](overview.md), sobald der Prüfstand SAFE ist, mit dieser Datei gewählt. 2s HALTEN ZUM LADEN lädt sie; ÜBERSPRINGEN oder das Home-Tag führt zurück ins Menü. Bevor das Angebot beantwortet ist, wird keine automatische Kopie geschrieben.

![Das Angebot beim Start](view:setup-backup-offer)

## Nicht in der Datei

Das WLAN-Heimnetz und sein Passwort, HOTSPOT-PASSWORT und ZUGANGSCODE bleiben draußen: Eine Karte wird von jedem Computer gelesen, in den sie kommt, und eine geladene Datei darf den Zugangscode nicht abschalten. Nach einem vollständigen Flashen setzt du sie auf [WLAN](wifi.md) neu.

## Eine Datei bearbeiten

| Zeile | Was sie ist |
|---|---|
| `rcbench settings, format 1` | die erste Zeile: das Format der Datei |
| `Panel: 0.23.15` | die Panel-Version, die sie schrieb |
| `Written: 2026-11-21 14:05:33 UTC` | ihr Datum; ohne gestellte Uhr steht dort not known |
| `pack_cells = 6  # Cells, 1 to 14S` | eine Einstellung: Schlüssel, Wert und ein Kommentar mit Name und Bereich |
| `theme = DARK  # Theme: DARK, LIGHT` | eine Auswahl, mit dem englischen Text ihrer Option |
| `srv_dut = "SERVO"` | ein Text, in doppelten Anführungszeichen |
| `end` | die letzte Zeile |

Eine Zeile je Einstellung, `<Schlüssel> = <Wert>`; `#` beginnt einen Kommentar. Eine Zahl wie geschrieben, ein Schalter und eine Auswahl mit dem englischen Wort, das die Datei schreibt, ein Text in doppelten Anführungszeichen. Behalte die letzte Zeile, `end`.

## Verwandte Seiten

- [Setup](setup.md)
- [WLAN](wifi.md)
- [Files](files.md)
