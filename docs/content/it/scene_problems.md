# Problemi della scena e Correggi tutto

Prima dell'esportazione, WhiteoutDex controlla la tua scena. Cerca cose che romperebbero il modello nel gioco o che farebbero fallire l'esportazione. Vedi il risultato nella finestra di esportazione.

[[shot:exporter_problems|La finestra di esportazione ha trovato tre problemi.]]

| N. | Cosa fa |
|---|---|
| [[ctl:1092]] | Ripara tutti i problemi in una volta. Questo cambia la tua scena. Ogni tipo di riparazione è un passo di annullamento, quindi puoi annullarlo con [[key:Ctrl+Z]]. |
| [[ctl:1093]] | Apre l'elenco di tutti i problemi. |

## L'elenco dei problemi

[[shot:scene_problems|Ogni riga è un problema.]]

| N. | Cosa fa |
|---|---|
| [[ctl:2000]] | I problemi. Fai doppio clic su una riga per selezionare l'oggetto nella scena. |
| [[ctl:2004]] | Controlla di nuovo la scena, per esempio dopo che hai riparato qualcosa a mano. |
| [[ctl:2002]] | Ripara solo i problemi che hai selezionato nell'elenco. |
| [[ctl:2003]] | Ripara tutti i problemi dell'elenco. |
| [[ctl:1]] | Chiude l'elenco. |

## Cosa ripara WhiteoutDex

| Problema | Cosa fa [[t:exp_fix_all_btn]] |
|---|---|
| Due oggetti hanno lo stesso nome | Li rinomina: `_2`, `_3` e così via. Il gioco ha bisogno di nomi unici. |
| Mesh vuota | La elimina. |
| Editable Poly | Aggiunge un modificatore *Edit Mesh*, così l'esportatore riceve triangoli. |
| *Edit Mesh* sopra *Skin* | Lo comprime, così lo skin resta corretto. |
| Controller che il gioco non può leggere | Lo sostituisce con un controller lineare. |
| Mesh con un materiale Multi/Sub-Object | Divide la mesh, così ogni parte ha un solo materiale. |
| Il materiale non è un Wc3Material | Lo converte. Le impostazioni vengono da *Correzione materiali* (vedi quella pagina). |
| Due materiali sono uguali | Li unisce in uno solo. |

!!! tip
    Non sei obbligato a riparare i problemi. L'esportatore funziona anche senza. Ma il modello può avere un aspetto sbagliato nel gioco.
