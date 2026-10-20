# Correzione materiali

L'esportatore può leggere solo il **Wc3Material**. Correzione materiali converte gli altri materiali (Standard, Physical, PBR) in Wc3Material. Mantiene le texture e imposta le opzioni di Warcraft III che scegli qui.

Le stesse impostazioni le usa [[t:exp_fix_all_btn]] nella finestra di esportazione.

Aprilo con **WhiteoutDex > [[t:menu_material_fix]]**.

[[shot:material_fix|Correzione materiali con le impostazioni per i modelli classici (SD).]]

Ci sono due gruppi di impostazioni: uno per i modelli classici (**SD**) e uno per Reforged (**HD**).

| N. | Cosa fa |
|---|---|
| [[ctl:4]] | Impostazioni per i modelli classici. |
| [[ctl:5]] | Impostazioni per i modelli Reforged. |

## Rendering

| N. | Cosa fa |
|---|---|
| [[ctl:8]] | Come viene disegnato il materiale. *Automatico* rende trasparenti i materiali con un canale alfa. Vedi *Wc3Material* per tutte le modalità. |
| [[ctl:10]] | Vengono disegnati entrambi i lati delle facce. |
| [[ctl:11]] | Il materiale ignora la luce. |
| [[ctl:12]] | Il materiale ignora la nebbia. |
| [[ctl:13]] | Usa un solo colore per tutta la mesh. |
| [[ctl:14]] | Disegnato sopra tutto il resto (senza test di profondità). |
| [[ctl:15]] | Non nasconde ciò che c'è dietro. |
| [[ctl:16]] | Usa l'occlusione ambientale (solo Reforged). |
| [[ctl:17]] | Le facce posteriori proiettano ombre (solo Reforged). |
| [[ctl:18]] | Prende *doppia faccia*, *non ombreggiato* e *trasparenza* dal vecchio materiale invece che dalle caselle sopra. |

## Texture

| N. | Cosa fa |
|---|---|
| [[ctl:23]] | La cartella che va davanti ai nomi delle texture nel modello, per esempio `Textures\`. |
| [[ctl:26]] | Come si ripetono le texture: come nel vecchio materiale, in entrambe le direzioni, solo U, solo V o per niente. |
| [[ctl:28]] | Una mappa normale o di rilievo diventa la mappa *Normale* (Reforged). |
| [[ctl:29]] | Una mappa di emissione diventa la mappa *Emissiva* (Reforged). |

## Correggi tutto e converti ora

| N. | Cosa fa |
|---|---|
| [[ctl:32]] | Quali impostazioni usa [[t:exp_fix_all_btn]] nella finestra di esportazione: SD o HD. |
| [[ctl:34]] | Aggiunge `_wc3` al nome di ogni materiale convertito. |
| [[ctl:39]] | Converte subito i materiali degli oggetti selezionati. |
| [[ctl:40]] | Converte subito tutti i materiali della scena. |
| [[ctl:42]] | Riporta questa scheda ai valori predefiniti. |
| [[ctl:43]] | Chiude la finestra. Le tue impostazioni vengono salvate. |

!!! tip
    Salva la scena prima di convertire tutta la scena.
