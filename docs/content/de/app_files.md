# Dateien und Einstellungen

Diese Seite zeigt, wo WhiteoutDex seine Dateien ablegt. Normalerweise musst du sie nicht öffnen. Sie helfen, wenn etwas schiefgeht oder wenn du auf einen neuen Computer umziehst.

## Der Installationsordner

`C:\ProgramData\Autodesk\ApplicationPlugins\WhiteoutDex`

| Ordner oder Datei | Was darin ist |
|---|---|
| `native plugins\Max<year>\` | Die Plugins für jede 3ds-Max-Version (Importer, Exporter, Partikel, Ribbons, Vorschau, BLP-Leser). |
| `scripts\` | Die WhiteoutDex-Werkzeuge (MAXScript). |
| `lang\` | Die Übersetzungen der Vorschau. |
| `whiteoutdex_icons\` | Die Symbole der Seitenleiste und der Werkzeuge. |
| `maps\WhiteoutDex\TeamGlow\` | Die Texturen für das Team-Glühen. |
| `version.txt` | Die installierte Version. |

Ändere hier keine Dateien. Ein Update ersetzt sie.

## Deine Einstellungen

Deine Einstellungen liegen im 3ds-Max-Einstellungsordner **plugcfg**. So findest du ihn: Öffne den MAXScript Listener ([[key:F11]]) und gib `getDir #plugcfg` ein.

| Datei | Was sie speichert |
|---|---|
| `WhiteoutDex_Settings.ini` | Sprache, Reforged-Ordner, MPQ-Reihenfolge, Seitenleiste, Update-Suche. |
| `WhiteoutDexMPQ.ini` | Ordner des klassischen Spiels. |
| `WhiteoutDexImporter.ini` | Deine letzten Import-Optionen. |
| `MDLXExporter.ini` | Deine letzten Export-Optionen. |
| `WhiteoutDex_MaterialFix.ini` | Die Einstellungen von *Material Fix* für SD und HD. |

!!! tip
    So setzt du WhiteoutDex auf den Zustand beim ersten Start zurück: Schließe 3ds Max und lösche diese Dateien. WhiteoutDex legt neue mit den Standardwerten an.

## Die Vorschau

| Datei | Was sie speichert |
|---|---|
| `WhiteoutFlakes.ini` (neben `3dsmax.exe`) | Einstellungen der Vorschau WhiteoutFlakes. |

## Log-Dateien

Wenn etwas schiefgeht, sagen dir diese Dateien, was passiert ist. Sie liegen in deinem temporären Ordner. Um ihn zu öffnen, drücke [[key:Win+R]], gib `%TEMP%` ein und drücke [[key:Enter]].

| Datei | Geschrieben von |
|---|---|
| `mdlx_import_debug.log` | dem Importer |
| `mdlx_export_debug.log` | dem Exporter, mit der vollen Liste der Warnungen |
| `WhiteoutDex_render.log` | der Vorschau WhiteoutFlakes |

Schicke diese Dateien mit, wenn du ein Problem meldest.
