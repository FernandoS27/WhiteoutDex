# Der Exporter

Der Exporter speichert deine Szene als Warcraft-III-Modell (`.mdx` oder `.mdl`). Er kann auch deine Texturen in die Formate des Spiels umwandeln.

## Exporter öffnen

- Menü **WhiteoutDex > [[t:menu_exporter]]**
- Der Knopf **Export** in der WhiteoutDex-Seitenleiste
- **Datei > Exportieren** und *Warcraft III MDX/MDL Model* wählen

Wähle einen Dateinamen. Dann öffnet sich das Export-Fenster.

## Das Export-Fenster

[[shot:exporter|Das Export-Fenster mit dem Reiter Geometrie.]]

Oben im Fenster steht der Dateiname. Darunter siehst du, ob die Datei neu ist oder ob es schon eine Datei mit diesem Namen gibt.

| Nr. | Was es macht |
|---|---|
| [[ctl:1001]] | **Modellname.** Der Name in der Datei. Das Spiel zeigt ihn nicht, andere Werkzeuge schon. |
| [[ctl:1011]] | Export für das klassische Spiel (MDX-Version 800). Texturen werden zu `.blp`-Dateien. |
| [[ctl:1012]] | Export für Reforged (MDX-Version 1800). Texturen werden zu `.dds`-Dateien. |
| [[ctl:1015]] | Zeigt die Mesh-Optionen (dieses Bild). |
| [[ctl:1016]] | Zeigt die Textur-Optionen. |
| [[ctl:1021]] | Fasst Meshes mit demselben Material zu einem zusammen. Das Modell wird kleiner und schneller. |
| [[ctl:1027]] | Gleicht die Normalen an, wo sich zwei Meshes berühren. Das versteckt Nähte. Nicht verwenden, wenn du Glättungsgruppen für harte Kanten nutzt. |
| [[ctl:1028]] | Exportiert auch Knochen und Helfer, die nichts benutzt. Normalerweise lässt der Exporter sie weg. |
| [[ctl:1032]] | Passt die Skin-Gewichte so an, dass sie im klassischen Spiel funktionieren. Das klassische Spiel kann nur einfache Gewichte. Nur für [[t:exp_format_classic_title]]. |
| [[ctl:1030]] | Wenn es die Datei schon gibt, hängt der Exporter `_1`, `_2` und so weiter an den Namen. Deine alte Datei bleibt. |
| [[ctl:1031]] | Öffnet nach dem Export den Ordner der Datei. |
| [[ctl:1]] | Startet den Export. |
| [[ctl:2]] | Schließt das Fenster ohne Export. |

Der grüne Kasten unter den Optionen sagt dir, dass WhiteoutDex keine Probleme in deiner Szene gefunden hat. Findet es Probleme, ist der Kasten gelb. Siehe *Szenenprobleme und [[t:exp_fix_all_btn]]*.

### Reiter Texturen

[[shot:exporter_textures|Der Reiter Texturen, hier für einen klassischen Export.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:1060]] | Wandelt die Texturen deiner Materialien in `.blp` (Klassisch) oder `.dds` (Reforged) um und speichert sie neben dem Modell. |
| [[ctl:1071]] | **Nur Klassisch.** [[t:exp_blp_paletted_item]] oder *JPEG*. JPEG hat mehr Farben, palettierte Dateien sind kleiner. Bei JPEG stellst du auch die Qualität ein (1–100). |
| [[ctl:1075]] | Mischt die Farben ein wenig. Das versteckt harte Farbstufen in palettierten Texturen. |
| [[ctl:1061]] | Erstellt die kleineren Versionen der Textur, die das Spiel für weit entfernte Modelle nutzt. Lass es an. |
| [[ctl:1062]] | Ersetzt Texturdateien, die schon da sind. Ist es aus, behält der Exporter vorhandene Dateien. |

Für **Reforged** wählst du stattdessen das DDS-Format: *BC3 (DXT5)* oder *BC7*. BC7 sieht besser aus, BC3 läuft auch auf älteren Grafikkarten.

## Klassisch oder Reforged?

| | Klassisch | Reforged |
|---|---|---|
| Spiel | Warcraft III 1.x und die klassische Grafik von Reforged | Reforged-Grafik (HD) |
| MDX-Version | 800 | 1800 |
| Texturen | `.blp` | `.dds` |
| Skin | Einfache Gewichte ([[t:exp_quantize_skin_weights_chk]] nutzen) | Volle Gewichte |

!!! tip
    Der Exporter merkt sich alle Optionen für den nächsten Export.
