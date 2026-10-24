---
screen: UPD_REMIND
title: Release-Erinnerung
order: 65
---

# Release-Erinnerung

Die Erinnerung meldet, dass ein neueres Release der Panel-Firmware auf GitHub liegt. Sie öffnet sich auf dem [Menü](overview.md), wenn die Suche beim Start eines gefunden hat. ERINNERUNG BEIM START unter [Updates](updates.md) schaltet sie; ab Werk ist sie an.

![Die Erinnerung](view:upd-remind)

| Bedienelement | Was es tut |
|---|---|
| JETZT NICHT | schließt die Erinnerung bis zum nächsten Start des Panels; Verlassen über den HOME-Tag wirkt genauso |
| NICHT MEHR ERINNERN | schaltet ERINNERUNG BEIM START aus und fordert das Speichern an; SETUP > UPDATES schaltet sie wieder ein |
| 2s HALTEN: LADEN & INSTALLIEREN | dasselbe Halten wie unter [Updates](updates.md#der-download) |

## Wann die Suche läuft

Mit eingeschalteter Erinnerung und einem gespeicherten Heimnetz sucht das Panel einmal nach jedem Start. Die Suche wartet, bis der Splash vorbei ist, der Prüfstand SAFE, der Netzteilausgang aus, kein Log-Lauf offen, WI-FI geschlossen und kein Update des IOmcu angeboten oder im Gang ist. Findet sie 10min nach dem Start keinen solchen Moment, läuft sie bei diesem Start nicht.

Die Erinnerung öffnet sich nicht, solange der Prüfstand ARMED ist oder ein Schalten auf ARMED wartet, solange der Netzteilausgang an ist und solange die Frage oder der Fortschritt des IOmcu-Updates zu sehen ist. Sie öffnet sich, sobald diese vorbei sind und das Menü zu sehen ist.

Findet die Suche kein neueres Release oder scheitert sie, zeigt sie nichts; ihr Ergebnis steht unter [Updates](updates.md).

## Verwandte Seiten

- [Updates](updates.md)
- [Panel-Update](update.md)
