# Files and settings

This page lists where WhiteoutDex keeps its files. You normally do not need to open them. They help when something goes wrong or when you move to a new computer.

## The install folder

`C:\ProgramData\Autodesk\ApplicationPlugins\WhiteoutDex`

| Folder or file | What is in it |
|---|---|
| `native plugins\Max<year>\` | The plugins for each 3ds Max version (importer, exporter, particles, ribbons, previewer, BLP reader). |
| `scripts\` | The WhiteoutDex tools (MAXScript). |
| `lang\` | The translations of the previewer. |
| `whiteoutdex_icons\` | The icons of the sidebar and the tools. |
| `maps\WhiteoutDex\TeamGlow\` | The team glow textures. |
| `version.txt` | The installed version. |

Do not change files here. An update replaces them.

## Your settings

Your settings are in the 3ds Max settings folder **plugcfg**. To find it, open the MAXScript Listener ([[key:F11]]) and type `getDir #plugcfg`.

| File | What it keeps |
|---|---|
| `WhiteoutDex_Settings.ini` | Language, Reforged folder, MPQ order, sidebar, update check. |
| `WhiteoutDexMPQ.ini` | Folder of the classic game. |
| `WhiteoutDexImporter.ini` | Your last import options. |
| `MDLXExporter.ini` | Your last export options. |
| `WhiteoutDex_MaterialFix.ini` | The *Material Fix* settings for SD and HD. |

!!! tip
    To reset WhiteoutDex to its first-start state, close 3ds Max and delete these files. WhiteoutDex makes new ones with the default values.

## The previewer

| File | What it keeps |
|---|---|
| `WhiteoutFlakes.ini` (next to `3dsmax.exe`) | Settings of the WhiteoutFlakes previewer. |

## Log files

When something goes wrong, these files tell what happened. They are in your temporary folder. To open it, press [[key:Win+R]], type `%TEMP%` and press [[key:Enter]].

| File | Written by |
|---|---|
| `mdlx_import_debug.log` | The importer |
| `mdlx_export_debug.log` | The exporter, with the full list of warnings |
| `WhiteoutDex_render.log` | The WhiteoutFlakes previewer |

Send these files when you report a problem.
