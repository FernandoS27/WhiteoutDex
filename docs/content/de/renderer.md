# WhiteoutFlakes-Vorschau

**WhiteoutFlakes** zeigt dein Modell so, wie Warcraft III es zeigt: mit den Shadern des Spiels, Teamfarbe, Licht, Partikeln und Ribbons. Die Vorschau folgt 3ds Max live. Wenn du den Zeitschieber bewegst oder etwas änderst, aktualisiert sie sich.

Öffne sie über **WhiteoutDex > [[t:menu_renderer]]** oder mit dem Knopf **Renderer** in der Seitenleiste.

![WhiteoutFlakes neben 3ds Max 2027, mit einem Reforged-Modell in Teamfarbe.](media_1.png)

## Werkzeugleiste

| Element | Was es macht |
|---|---|
| **Animation** | Spielt eine Sequenz des Modells ab. Mit **Freier Bereich** folgt die Vorschau dem Zeitschieber von Max. |
| **Kamera** | **Freie Kamera** oder eine der Kameras der Szene, zum Beispiel die Porträt-Kamera. |
| **Team** | Die Spielerfarbe des Modells. |
| **Beleuchtung** | **Im Spiel** (wie im Spiel), **Glue** (wie in den Menüs) oder **Dynamisch**. |
| **Kamera synchronisieren** | Die Vorschau folgt dem aktiven Viewport von 3ds Max. |
| **Neu einlesen** | Liest die ganze Szene neu, zum Beispiel nachdem du Objekte hinzugefügt hast. |

## Menü

| Menü | Was darin ist |
|---|---|
| **Ansicht** | Raster, Partikel, Ribbons und Event-Objekte zeigen oder verstecken. **Warcraft-III-Grafiken**: welche Texturen die Vorschau nutzt (**Rendermodus folgen**, **Classic**, **Reforged (HD)**, **Definitive (DE)**). **Tileset**: der Boden. |
| **Debug** | Markierungen für Kollisionsformen und Lichter, besondere Ansichten (Albedo, Normalen, Detailstufe, Zahl der Lichter …) und die Wahl der Detailstufe. |
| **Einstellungen** | Hintergrund, Helligkeit, Lautstärke, Tageszeit, bildbasierte Beleuchtung, Schatten, Nebel, das Grafik-Backend (Direct3D 11, Direct3D 12, Vulkan) und die Spielordner. |

## Gut zu wissen

- Partikel und Ribbons werden wie im Spiel simuliert. Springst du auf dem Zeitschieber, berechnet die Vorschau die Effekte bis zum neuen Frame. Sie sehen dann so aus, als wäre die Animation bis dorthin gelaufen.
- Die Vorschau speichert ihre Einstellungen in `WhiteoutFlakes.ini` neben `3dsmax.exe`.
- Zeigt die Vorschau nichts oder stürzt sie ab, sieh in `WhiteoutDex_render.log` nach (siehe *Dateien und Einstellungen*).

!!! tip
    Wähle unter **Ansicht > Warcraft-III-Grafiken** den Eintrag **Classic** oder **Reforged (HD)**. So siehst du, wie dein Modell in jeder Version des Spiels aussieht.
