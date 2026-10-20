# Tutorial: il tuo primo modello

In questa lezione crei da zero un oggetto semplice: una cassa, per esempio un doodad per una mappa.

## Passo 1: modella la cassa

1. Crea un **Box**: *Create > Geometry > Box*. Fallo di circa 60 × 60 × 60 unità. Un'unità in 3ds Max è un'unità nel gioco.
2. Posizionalo in modo che stia sul terreno (base all'altezza 0) e che il suo centro sia in 0, 0.

## Passo 2: dagli un materiale

1. Apri il Material Editor ([[key:M]]).
2. Scegli uno slot libero, clicca il pulsante del tipo di materiale e scegli **Wc3Material**.
3. In *Mappe texture del materiale* clicca il pulsante **Diffusa** e scegli **Wc3Bitmap**.
4. Clicca **Scegli bitmap...** e scegli una texture. Oppure clicca **Apri il Browser texture** e prendine una dal gioco (cerca `crate` o `wood`).
5. Trascina il materiale sul box.

![Il Wc3Material con i suoi slot per le texture.](mat_wc3material_r3.png)

!!! tip
    Hai già un materiale Standard o Physical? *Correzione materiali* lo converte in un Wc3Material per te.

## Passo 3: collisione

Il giocatore deve poter cliccare la cassa.

1. Seleziona il box.
2. Apri **WhiteoutDex > [[t:menu_anim_general]]** e clicca **Box di collisione con un clic**.

## Passo 4: un'animazione Stand

Ogni modello ha bisogno di almeno una sequenza, anche se niente si muove.

1. Apri **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Clicca **Nuova...**, scegli il preset *Stand* e clicca **OK**.

## Passo 5: esporta e prova

1. Esporta la cassa come `Crate.mdx` (vedi la lezione *Esportare un modello*).
2. Guarda il riquadro della finestra di esportazione: se è giallo, clicca [[t:exp_fix_all_btn]].
3. Guarda il risultato nell'anteprima: **WhiteoutDex > [[t:menu_renderer]]**.

Hai creato il tuo primo modello di Warcraft III.
