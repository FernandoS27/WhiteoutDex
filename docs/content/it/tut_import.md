# Tutorial: importare un modello

In questa lezione carichi un modello di Warcraft III in 3ds Max e guardi cosa ne fa WhiteoutDex.

**Ti serve:** un file di modello. WhiteoutDex ha dei modelli di esempio nella cartella `samples` del codice sorgente, per esempio `HeroPaladin.mdx`. Puoi anche usare un file `.mdx` o `.mdl` da internet o dalle tue mappe.

## Passo 1: avvia l'importazione

1. Scegli **WhiteoutDex > [[t:menu_importer]]**.
2. Scegli il file `HeroPaladin.mdx` e clicca **Apri**.

Si apre la finestra di importazione.

![La finestra di importazione. Mostra cosa contiene il modello.](importer.png)

## Passo 2: scegli le opzioni

1. In *Impostazioni rapide* scegli **Tutto**.
2. In *Modalità* scegli **Nuova scena**.
3. Clicca **Importa**.

Dopo pochi secondi il modello è nella tua scena.

![Il Paladino importato con i suoi punti di attacco (gialli) e oggetti evento (verdi).](tut_model_helpers.png)

## Passo 3: guardati intorno

Ora la scena contiene:

- **Le mesh** del modello con i *Wc3Material* e le texture.
- **Le ossa** che muovono le mesh.
- **I punti di attacco** con i nomi gialli, per esempio *Hand Right Ref*.
- **Gli oggetti evento** con i nomi verdi, per esempio le impronte *FPT…* e i suoni *SND…*.
- **Forme di collisione**, **emettitori di particelle** e **scie**, se il modello li ha.

## Passo 4: riproduci le animazioni

1. Apri **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Fai doppio clic su una sequenza, per esempio *Walk*.

3ds Max riproduce l'animazione della camminata.

!!! tip
    Troppi helper sullo schermo? Apri il [[t:menu_node_manager]], seleziona *Helper / Dummy*, *Punti di attacco* e *Oggetti evento* e clicca **Nascondi**.

**Prossima lezione:** guarda il modello nell'anteprima WhiteoutFlakes.
