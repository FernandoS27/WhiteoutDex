# Fichiers et paramètres

Cette page indique où WhiteoutDex range ses fichiers. En général, vous n'avez pas besoin de les ouvrir. Ils sont utiles quand quelque chose ne fonctionne pas ou quand vous changez d'ordinateur.

## Le dossier d'installation

`C:\ProgramData\Autodesk\ApplicationPlugins\WhiteoutDex`

| Dossier ou fichier | Contenu |
|---|---|
| `native plugins\Max<year>\` | Les plug-ins pour chaque version de 3ds Max (importateur, exportateur, particules, traînées, aperçu, lecteur BLP). |
| `scripts\` | Les outils WhiteoutDex (MAXScript). |
| `lang\` | Les traductions de l'aperçu. |
| `whiteoutdex_icons\` | Les icônes de la barre latérale et des outils. |
| `maps\WhiteoutDex\TeamGlow\` | Les textures de halo d'équipe. |
| `version.txt` | La version installée. |

Ne modifiez pas les fichiers de ce dossier. Une mise à jour les remplace.

## Vos réglages

Vos réglages se trouvent dans le dossier de réglages de 3ds Max **plugcfg**. Pour le trouver, ouvrez le MAXScript Listener ([[key:F11]]) et tapez `getDir #plugcfg`.

| Fichier | Ce qu'il enregistre |
|---|---|
| `WhiteoutDex_Settings.ini` | Langue, dossier de Reforged, ordre des MPQ, barre latérale, recherche de mises à jour. |
| `WhiteoutDexMPQ.ini` | Le dossier du jeu classique. |
| `WhiteoutDexImporter.ini` | Vos dernières options d'import. |
| `MDLXExporter.ini` | Vos dernières options d'export. |
| `WhiteoutDex_MaterialFix.ini` | Les réglages de la *Correction des matériaux* pour SD et HD. |

!!! tip
    Pour remettre WhiteoutDex dans l'état du premier démarrage, fermez 3ds Max et supprimez ces fichiers. WhiteoutDex en crée de nouveaux avec les valeurs par défaut.

## L'aperçu

| Fichier | Ce qu'il enregistre |
|---|---|
| `WhiteoutFlakes.ini` (à côté de `3dsmax.exe`) | Les réglages de l'aperçu WhiteoutFlakes. |

## Fichiers journaux

Quand quelque chose ne fonctionne pas, ces fichiers indiquent ce qui s'est passé. Ils se trouvent dans votre dossier temporaire. Pour l'ouvrir, appuyez sur [[key:Win+R]], tapez `%TEMP%` et appuyez sur [[key:Enter]].

| Fichier | Écrit par |
|---|---|
| `mdlx_import_debug.log` | L'importateur |
| `mdlx_export_debug.log` | L'exportateur, avec la liste complète des avertissements |
| `WhiteoutDex_render.log` | L'aperçu WhiteoutFlakes |

Joignez ces fichiers quand vous signalez un problème.
