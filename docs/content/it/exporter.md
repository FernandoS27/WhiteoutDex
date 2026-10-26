# L'esportatore

L'esportatore salva la tua scena come modello di Warcraft III (`.mdx` o `.mdl`). Può anche convertire le tue texture nei formati del gioco.

## Aprire l'esportatore

- Menu **WhiteoutDex > [[t:menu_exporter]]**
- Il pulsante **Esporta** nella barra laterale di WhiteoutDex
- **File > Export** e scegli *Warcraft III MDX/MDL Model*

Scegli un nome di file. Poi si apre la finestra di esportazione.

## La finestra di esportazione

[[shot:exporter|La finestra di esportazione con la scheda Geometria.]]

In alto la finestra mostra il nome del file. Sotto vedi se il file è nuovo o se esiste già un file con questo nome.

| N. | Cosa fa |
|---|---|
| [[ctl:1001]] | **Nome del modello.** Il nome dentro il file. Il gioco non lo mostra, ma altri strumenti sì. |
| [[ctl:1011]] | Esporta per il gioco classico (MDX versione 800). Le texture diventano file `.blp`. |
| [[ctl:1012]] | Esporta per Reforged (MDX versione 1800). Le texture diventano file `.dds`. |
| [[ctl:1015]] | Mostra le opzioni della mesh (questa immagine). |
| [[ctl:1016]] | Mostra le opzioni delle texture. |
| [[ctl:1021]] | Unisce in una sola mesh le mesh che usano lo stesso materiale. Il modello diventa più piccolo e più veloce. |
| [[ctl:1027]] | Rende uguali le normali dove due mesh si toccano. Così nasconde le giunture. Non usarlo se usi gli smoothing group per gli spigoli netti. |
| [[ctl:1028]] | Esporta anche ossa e helper che niente usa. Di solito l'esportatore li tralascia. |
| [[ctl:1032]] | Cambia i pesi dello skin perché funzionino nel gioco classico. Il gioco classico può usare solo pesi semplici. Solo per [[t:exp_format_classic_title]]. |
| [[ctl:1030]] | Quando il file esiste già, l'esportatore aggiunge `_1`, `_2` e così via al nome. Il tuo vecchio file resta. |
| [[ctl:1031]] | Apre la cartella del file dopo l'esportazione. |
| [[ctl:1]] | Avvia l'esportazione. |
| [[ctl:2]] | Chiude la finestra senza esportare. |

Il riquadro verde sotto le opzioni ti dice che WhiteoutDex non ha trovato problemi nella tua scena. Quando trova problemi, il riquadro è giallo. Vedi *Problemi della scena e Correggi tutto*.

### Scheda Texture

[[shot:exporter_textures|La scheda Texture, qui per un'esportazione classica.]]

| N. | Cosa fa |
|---|---|
| [[ctl:1060]] | Converte le texture dei tuoi materiali in `.blp` (Classico) o `.dds` (Reforged) e le salva accanto al modello. |
| [[ctl:1071]] | **Solo Classico.** [[t:exp_blp_paletted_item]] o *JPEG*. JPEG ha più colori. I file con palette sono più piccoli. Con JPEG imposti anche la qualità (1–100). |
| [[ctl:1075]] | Mescola un poco i colori. Così nasconde i passaggi netti tra i colori nelle texture con palette. |
| [[ctl:1061]] | Crea le versioni più piccole della texture che il gioco usa per i modelli lontani. Lascialo attivo. |
| [[ctl:1062]] | Sostituisce i file di texture che esistono già. Quando è disattivo, l'esportatore tiene i file esistenti. |

Per **Reforged** scegli invece il formato DDS: *BC3 (DXT5)* o *BC7*. BC7 ha un aspetto migliore. BC3 funziona sulle schede grafiche più vecchie.

## Classico o Reforged?

| | Classico | Reforged |
|---|---|---|
| Gioco | Warcraft III 1.x e la grafica classica di Reforged | Grafica di Reforged (HD) |
| Versione MDX | 800 | 1800 |
| Texture | `.blp` | `.dds` |
| Skin | Pesi semplici (usa [[t:exp_quantize_skin_weights_chk]]) | Pesi completi |

!!! tip
    L'esportatore ricorda tutte le opzioni per l'esportazione successiva.
