# Partikelemitter 1

Der Partikelemitter 1 sendet keine Bilder aus, sondern **kleine Modelle**. Nutze ihn für fallende Blätter, Steine oder Pfeile, die selbst Modelle sind.

Erstelle ihn über **Create > Geometry > Particle Systems > Wc3Particles1**.

## Particle Configuration

[[shot:obj_particles1_r1|Particle Configuration.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:2020]] | Lädt alle Einstellungen aus einer `.ini`-Datei. |
| [[ctl:2021]] | Speichert alle Einstellungen in eine `.ini`-Datei. |
| [[ctl:2022]] | Ist es an, lädt [[t:popcorn_import_btn]] auch die animierten Keys der Werte. |

## Emitter Options

[[shot:obj_particles1_r2|Emitter Options.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:5002]] | Wie schnell die Modelle wegfliegen. |
| [[ctl:5004]] | Winkel der Richtung nach oben und unten. |
| [[ctl:5006]] | Winkel der Richtung zur Seite. |
| [[ctl:5008]] | Zieht die Modelle nach unten. |
| [[ctl:2030]] | **Steuerung:** wie sich ein animierter Wert zwischen den Keys ändert. Jeder Wert hat seine eigene Steuerung. |

## Timing Options

[[shot:obj_particles1_r3|Timing Options.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:5010]] | Wie viele Sekunden ein Modell lebt. |
| [[ctl:5012]] | Wie viele Modelle pro Sekunde starten. |

## Model Options

[[shot:obj_particles1_r4|Model Options.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:5017]] | Der Ordner des Modells im Spiel. |
| [[ctl:5016]] | Der Dateiname des Modells, zum Beispiel `Abilities\Weapons\Arrow\ArrowMissile.mdl`. |
| [[ctl:2024]] | Wähle ein Modell aus den Spielarchiven. |
| [[ctl:2023]] | Wähle eine Modelldatei auf deiner Festplatte. |
| [[ctl:5014]] | Größe der Modelle in der Viewport-Vorschau. |

!!! note
    Klassische Modelle schreiben in diesem Pfad oft die Endung `.mdl`, auch wenn die Datei im Spiel eine `.mdx` ist. Das Spiel findet beide.
