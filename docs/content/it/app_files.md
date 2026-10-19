# File e impostazioni

Questa pagina elenca dove WhiteoutDex tiene i suoi file. Di solito non devi aprirli. Ti aiutano quando qualcosa va storto o quando passi a un nuovo computer.

## La cartella di installazione

`C:\ProgramData\Autodesk\ApplicationPlugins\WhiteoutDex`

| Cartella o file | Cosa contiene |
|---|---|
| `native plugins\Max<year>\` | I plugin per ogni versione di 3ds Max (importatore, esportatore, particelle, scie, anteprima, lettore BLP). |
| `scripts\` | Gli strumenti di WhiteoutDex (MAXScript). |
| `lang\` | Le traduzioni dell'anteprima. |
| `whiteoutdex_icons\` | Le icone della barra laterale e degli strumenti. |
| `maps\WhiteoutDex\TeamGlow\` | Le texture del bagliore della squadra. |
| `version.txt` | La versione installata. |

Non modificare i file in questa cartella. Un aggiornamento li sostituisce.

## Le tue impostazioni

Le tue impostazioni si trovano nella cartella delle impostazioni di 3ds Max **plugcfg**. Per trovarla, apri il MAXScript Listener ([[key:F11]]) e scrivi `getDir #plugcfg`.

| File | Cosa contiene |
|---|---|
| `WhiteoutDex_Settings.ini` | Lingua, cartella di Reforged, ordine MPQ, barra laterale, controllo degli aggiornamenti. |
| `WhiteoutDexMPQ.ini` | Cartella del gioco classico. |
| `WhiteoutDexImporter.ini` | Le tue ultime opzioni di importazione. |
| `MDLXExporter.ini` | Le tue ultime opzioni di esportazione. |
| `WhiteoutDex_MaterialFix.ini` | Le impostazioni di *Correzione materiali* per SD e HD. |

!!! tip
    Per riportare WhiteoutDex allo stato del primo avvio, chiudi 3ds Max ed elimina questi file. WhiteoutDex ne crea di nuovi con i valori predefiniti.

## L'anteprima

| File | Cosa contiene |
|---|---|
| `WhiteoutFlakes.ini` (accanto a `3dsmax.exe`) | Le impostazioni dell'anteprima WhiteoutFlakes. |

## File di log

Quando qualcosa va storto, questi file dicono cosa è successo. Si trovano nella tua cartella temporanea. Per aprirla, premi [[key:Win+R]], scrivi `%TEMP%` e premi [[key:Enter]].

| File | Scritto da |
|---|---|
| `mdlx_import_debug.log` | L'importatore |
| `mdlx_export_debug.log` | L'esportatore, con l'elenco completo degli avvisi |
| `WhiteoutDex_render.log` | L'anteprima WhiteoutFlakes |

Invia questi file quando segnali un problema.
