# Probleme und Lösungen

## Das WhiteoutDex-Menü fehlt

- Prüfe, ob deine 3ds-Max-Version zwischen 2016 und 2027 liegt.
- Installiere WhiteoutDex noch einmal, während 3ds Max geschlossen ist.
- 3ds Max 2026 und neuer brauchen **.NET 8**. Installiere es von Microsoft und starte 3ds Max neu.

## Das Modell hat keine Texturen (es ist weiß oder pink)

- Öffne [[t:menu_settings]] und prüfe die Spielordner. Die Statuszeile muss sagen, dass Archive gefunden wurden.
- Klassische Modelle suchen in den MPQ-Archiven, Reforged-Modelle in CASC. Stelle den Ordner des Spiels ein, aus dem das Modell kommt.
- Texturen eigener Modelle müssen neben der Modelldatei liegen. Oder sie liegen in einem Unterordner mit dem Pfad, den das Modell nutzt (zum Beispiel `Textures\`).

## Das Modell sieht im Spiel von innen nach außen gedreht aus

Die Dreiecke zeigen in die falsche Richtung. Der Exporter dreht Meshes um, die gegen ihre eigenen Normalen zeigen. Sieht ein Teil trotzdem falsch aus, wähle es aus und nutze **Flip** auf seinen Normalen in 3ds Max.

## Das Modell ist unsichtbar oder flackert am Bildschirmrand

Die **Extents** (die Größe jeder Animation) sind zu klein. Der Exporter misst sie in jedem Frame. Wenn du das Modell nach dem Import verschoben hast, exportiere noch einmal. Der Exporter ersetzt Extents, die das Modell nicht umfassen.

## Teile des Modells bewegen sich nicht mit den Knochen

- Das Mesh braucht einen **Skin**-Modifikator mit den Knochen.
- Im Import-Fenster muss die Option [[t:imp_skinned_chk]] an sein.
- Schalte für klassische Modelle [[t:exp_quantize_skin_weights_chk]] im Export-Fenster ein.

## Der Export zeigt Warnungen

Der Export-Bericht listet, was deine Aufmerksamkeit braucht. Jede Zeile nennt das Objekt. Die volle Liste steht in `mdlx_export_debug.log` (siehe *Dateien und Einstellungen*).

## 3ds Max zeigt „Startup Failure Detection“

3ds Max wurde beim letzten Mal nicht richtig geschlossen. Klicke auf **Continue Without Restore**. Wähle *Restore Factory Settings* nur, wenn du ganz 3ds Max zurücksetzen willst.

## Wo bekomme ich Hilfe?

- **Discord:** der WhiteoutDex-Server (Link unter [[t:menu_about]]).
- **Hive Workshop:** der WhiteoutDex-Thread.
- **GitHub:** Melde Fehler auf der Seite *Issues*. Hänge die Log-Dateien aus *Dateien und Einstellungen* an.
