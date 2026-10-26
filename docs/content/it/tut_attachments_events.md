# Tutorial: punti di attacco ed eventi

Nel gioco un'unità ha bisogno di posti per gli effetti (punti di attacco) e di momenti per suoni e impronte (oggetti evento). In questa lezione aggiungi entrambi a un'unità.

**Ti serve:** un'unità con ossa e un'animazione *Walk*, per esempio il Paladino importato.

## Parte 1: punti di attacco

### Il modo veloce

Se le tue ossa hanno nomi comuni (*Head*, *Chest*, *Hand*, *Foot* …):

1. Apri **WhiteoutDex > [[t:menu_anim_general]]**.
2. Clicca **Punti di aggancio con un clic**. WhiteoutDex crea i punti e li collega alle ossa.

### A mano

1. Crea un **Wc3 AttachPoint** (*Create > Helpers > Warcraft III Plugins*).
2. Clicca **Seleziona oggetto (assorbi)** e clicca l'osso, per esempio la mano destra. Il punto salta lì e si collega all'osso.
3. In **Parametri del punto di attacco** scegli **Tipo** *Hand* e **Lato** *Right*. Ora il punto si chiama *Hand Right Ref*.

In quel punto il gioco mostra gli effetti degli incantesimi sulla mano destra.

![I Parametri del punto di attacco di un punto di attacco.](obj_attachpoint_r3.png)

**Punti che ogni unità dovrebbe avere:** *Origin*, *Head*, *Chest*, *Overhead*. Le armi usano *Hand Left/Right* o *Weapon*.

## Parte 2: impronte

1. Crea un **Wc3 Event Object** e mettilo sul piede sinistro. Collegalo all'osso del piede.
2. In **Tipo di evento** scegli il tipo *FootPrint* e in **Dati** un'impronta, per esempio una piccola.
3. Clicca **Conferma**.
4. Apri la sequenza *Walk* nel Gestore sequenze. Sposta il cursore del tempo sul fotogramma in cui il piede sinistro tocca il terreno.
5. Clicca **Aggiungi**. Ripeti per ogni passo del piede sinistro.
6. Fai lo stesso per il piede destro con un secondo oggetto evento.

## Parte 3: un suono

1. Crea un altro oggetto evento vicino all'arma.
2. Scegli il tipo *Sound* e un suono di colpo in **Dati**, poi clicca **Conferma**.
3. Vai al fotogramma dell'animazione *Attack* in cui l'arma colpisce e clicca **Aggiungi**.

## Controlla il risultato

Apri l'anteprima. Con **Visualizza > Oggetti evento** attivo, l'anteprima mostra le impronte e riproduce i suoni.

!!! tip
    Guarda un modello del gioco (*Importare dal gioco*): copia come Blizzard posiziona i punti di attacco e gli eventi.
