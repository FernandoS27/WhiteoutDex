# Popcorn FX

**Popcorn FX** ist das Partikelsystem von Warcraft III: Reforged. Der Effekt selbst ist eine `.pkfx`-Datei (oder `.pkb`-Datei), gemacht mit dem PopcornFX-Editor. Das Popcorn-Objekt in 3ds Max setzt diesen Effekt auf das Modell und steuert, wann er läuft.

Erstelle es über **Create > Helpers > Warcraft III Plugins > Wc3 Popcorn**. Popcorn funktioniert nur in **Reforged**-Modellen.

## Popcorn FX Datei

[[shot:obj_popcorn_r2|Popcorn FX Datei.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:9]] | Wähle einen `.pkb`- oder `.pkfx`-Effekt aus den Spielarchiven. |
| [[ctl:10]] | Wähle eine Effektdatei auf deiner Festplatte. |
| [[ctl:11]] | Zeigt eine kurze Erklärung. |

Die zwei Felder über den Knöpfen enthalten den Pfad des Effekts und den Ordner davor.

## Animations-Sichtbarkeitsleitfaden

[[shot:obj_popcorn_r3|Animations-Sichtbarkeitsleitfaden.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:4]] | **An:** Der Effekt läuft in allen Animationen. **Aus:** Er läuft in keiner. |
| [[ctl:6]] | Ausnahmen: In diesen Animationen macht der Effekt das Gegenteil. |
| [[ctl:8]] | Entfernt die ausgewählte Ausnahme. |
| [[ctl:10]] | Wähle eine Animation der Szene … |
| [[ctl:12]] | … und füge sie als Ausnahme hinzu. |

## Emissionseinstellungen

[[shot:obj_popcorn_r4|Emissionseinstellungen.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:6]] | Transparenz der Partikel. |
| [[ctl:10]] | Partikel pro Sekunde. |
| [[ctl:14]] | Lebensdauer eines Partikels in Sekunden. |
| [[ctl:18]] | Geschwindigkeit der Partikel. |

## Farbeinstellungen

[[shot:obj_popcorn_r5|Farbeinstellungen.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:4]] | Die Grundfarbe des Effekts. |
| [[ctl:12]] | Multipliziert die Farbe. So wird der Effekt heller oder dunkler. |

## Render-Flags

[[shot:obj_popcorn_r6|Render-Flags.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:4]] | Die Partikel ignorieren das Licht der Szene. |
| [[ctl:6]] | Die Partikel ignorieren den Nebel. |
| [[ctl:8]] | Die Partikel wachsen und schrumpfen mit dem Modell. |
