# Archivos y ajustes

Esta página indica dónde guarda WhiteoutDex sus archivos. Normalmente no necesitas abrirlos. Te ayudan cuando algo va mal o cuando pasas a un ordenador nuevo.

## La carpeta de instalación

`C:\ProgramData\Autodesk\ApplicationPlugins\WhiteoutDex`

| Carpeta o archivo | Qué contiene |
|---|---|
| `native plugins\Max<year>\` | Los plugins para cada versión de 3ds Max (importador, exportador, partículas, estelas, previsualizador, lector de BLP). |
| `scripts\` | Las herramientas de WhiteoutDex (MAXScript). |
| `lang\` | Las traducciones del previsualizador. |
| `whiteoutdex_icons\` | Los iconos de la barra lateral y de las herramientas. |
| `maps\WhiteoutDex\TeamGlow\` | Las texturas del brillo de equipo. |
| `version.txt` | La versión instalada. |

No cambies los archivos de esta carpeta. Una actualización los reemplaza.

## Tus ajustes

Tus ajustes están en la carpeta de ajustes de 3ds Max **plugcfg**. Para encontrarla, abre el MAXScript Listener ([[key:F11]]) y escribe `getDir #plugcfg`.

| Archivo | Qué guarda |
|---|---|
| `WhiteoutDex_Settings.ini` | Idioma, carpeta de Reforged, orden de los MPQ, barra lateral, búsqueda de actualizaciones. |
| `WhiteoutDexMPQ.ini` | Carpeta del juego clásico. |
| `WhiteoutDexImporter.ini` | Tus últimas opciones de importación. |
| `MDLXExporter.ini` | Tus últimas opciones de exportación. |
| `WhiteoutDex_MaterialFix.ini` | Los ajustes de *Corrección de materiales* para SD y HD. |

!!! tip
    Para devolver WhiteoutDex a su estado del primer inicio, cierra 3ds Max y borra estos archivos. WhiteoutDex crea archivos nuevos con los valores predeterminados.

## El previsualizador

| Archivo | Qué guarda |
|---|---|
| `WhiteoutFlakes.ini` (junto a `3dsmax.exe`) | Los ajustes del previsualizador WhiteoutFlakes. |

## Archivos de registro

Cuando algo va mal, estos archivos dicen qué pasó. Están en tu carpeta temporal. Para abrirla, pulsa [[key:Win+R]], escribe `%TEMP%` y pulsa [[key:Enter]].

| Archivo | Lo escribe |
|---|---|
| `mdlx_import_debug.log` | El importador |
| `mdlx_export_debug.log` | El exportador, con la lista completa de advertencias |
| `WhiteoutDex_render.log` | El previsualizador WhiteoutFlakes |

Envía estos archivos cuando informes de un problema.
