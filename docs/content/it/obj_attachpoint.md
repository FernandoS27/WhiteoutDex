# Punto di attacco

Un punto di attacco segna un posto sul modello. Il gioco attacca delle cose in quel posto: l'arma di un eroe, un effetto di incantesimo sulla mano, la freccia che colpisce il petto, la barra della salute sopra la testa.

Crealo con **Create > Helpers > Warcraft III Plugins > Wc3 AttachPoint**. Collegalo all'osso che deve seguire.

Nella viewport ogni punto di attacco mostra il suo nome in giallo.

## Funzioni generali

[[shot:obj_attachpoint_r1|Funzioni generali.]]

| N. | Cosa fa |
|---|---|
| [[ctl:3]] | Clicca qui, poi clicca un oggetto nella viewport. Il punto di attacco salta su quell'oggetto e si collega a esso. |

## Parametri della mappa del modello

[[shot:obj_attachpoint_r2|Parametri della mappa del modello.]]

Un punto di attacco può portare un suo modello, per esempio una spada che l'unità tiene sempre in mano.

| N. | Cosa fa |
|---|---|
| [[ctl:9]] | Scegli il file del modello sul tuo disco. |
| [[ctl:10]] | Scegli un modello dagli archivi del gioco. |

I due campi sopra i pulsanti contengono la cartella e il nome del file. Il percorso è relativo alla cartella di Warcraft III, per esempio `Textures\Model.mdx`.

## Parametri del punto di attacco

[[shot:obj_attachpoint_r3|Parametri del punto di attacco.]]

Il gioco trova un punto di attacco dal suo **nome**. Costruisci il nome con quattro parti:

| N. | Cosa fa |
|---|---|
| [[ctl:5]] | **Tipo:** il posto principale: *Head*, *Overhead*, *Origin*, *Foot*, *Chest*, *Hand*, *Weapon* o *Sprite*. |
| [[ctl:8]] | **Cavalcatura:** per chi cavalca una cavalcatura. |
| [[ctl:11]] | **Lato:** *Left* o *Right*, per mani e piedi. |
| [[ctl:14]] | **Extra:** altre parole, per esempio *Rear*, *Alternate*, *RallyPoint*, *Gold*, oppure da *First* a *Sixth* per gli edifici. |
| [[ctl:22]] | Usa il modello di *Parametri della mappa del modello*. |
| [[ctl:26]] | Visibilità (da 0 a 1). Animala per mostrare o nascondere il modello attaccato. |
| [[ctl:27]] | Mostra rispetto a cosa è relativo il percorso del modello. |
| [[ctl:34]] | Un numero per il punto di attacco. |
| [[ctl:36]] | Scrive questo numero nel modello. |

Esempio: Tipo *Hand* e Lato *Right* danno il nome `Hand Right Ref`. Il gioco mette lì un effetto di incantesimo per la mano destra.

!!! tip
    Lo strumento *Punti di aggancio con un clic* in *Anim.: strumenti generali* crea per te i punti di attacco comuni, quando le tue ossa hanno nomi noti.
