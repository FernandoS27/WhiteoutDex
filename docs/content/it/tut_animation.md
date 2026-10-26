# Tutorial: animare

In questa lezione dai alla cassa della lezione precedente due animazioni: *Stand* (non fa niente) e *Death* (si rompe).

## Passo 1: pianifica la timeline

Tutte le animazioni di un modello condividono una sola timeline. Ogni animazione ha la sua parte:

| Sequenza | Fotogrammi |
|---|---|
| Stand | 0 – 80 |
| Death | 100 – 164 |

Lascia liberi alcuni fotogrammi tra le parti.

## Passo 2: crea le sequenze

1. Apri **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Clicca **Nuova...**, scegli *Stand*, imposta l'inizio a 0 e la fine a 80, clicca **OK**.
3. Clicca **Nuova...**, scegli *Death*, imposta l'inizio a 100 e la fine a 164, attiva **Senza loop**, clicca **OK**.

![Il Gestore sequenze con una sequenza selezionata.](sequence_manager_selected.png)

## Passo 3: imposta le chiavi

1. Attiva **Auto Key** in 3ds Max.
2. Imposta le chiavi all'inizio e alla fine di ogni sequenza: apri **WhiteoutDex > [[t:menu_anim_general]]**, seleziona la cassa e clicca **Aggiungi chiavi iniziali e finali agli oggetti selezionati**.
3. Vai al fotogramma 130. Ruota la cassa e spostala in basso, come se cadesse.
4. Vai al fotogramma 164 e spostala sotto il terreno.
5. Disattiva **Auto Key**.

## Passo 4: prova

1. Nel Gestore sequenze, fai doppio clic su *Death*. 3ds Max la riproduce.
2. Apri l'anteprima (**WhiteoutDex > [[t:menu_renderer]]**) e scegli *Death* nell'elenco **Animazione**.

!!! tip
    Le ossa si animano meglio delle mesh: collega le parti di un modello alle ossa e anima le ossa. Poi usa un modificatore **Skin** sulle mesh.

!!! note
    Il gioco riproduce *Stand* di continuo. *Death* si riproduce una volta, poi il corpo resta fino alle animazioni *Decay* (se il modello le ha).
