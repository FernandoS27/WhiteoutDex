# Anteprima WhiteoutFlakes

**WhiteoutFlakes** mostra il tuo modello come lo mostra Warcraft III: con gli shader del gioco, il colore della squadra, l'illuminazione, le particelle e le scie. Segue 3ds Max in tempo reale: quando sposti il cursore del tempo o cambi qualcosa, l'anteprima si aggiorna.

Aprila con **WhiteoutDex > [[t:menu_renderer]]** o con il pulsante **Renderer Wc3** nella barra laterale.

![WhiteoutFlakes accanto a 3ds Max 2027, con un modello Reforged nel colore della squadra.](media_1.png)

## Barra degli strumenti

| Elemento | Cosa fa |
|---|---|
| **Animazione** | Riproduce una sequenza del modello, oppure *Intervallo libero* per seguire il cursore del tempo di Max. |
| **Camera** | *Camera libera* o una delle telecamere della scena, per esempio la telecamera del ritratto. |
| **Squadra** | Il colore del giocatore per il modello. |
| **Illuminazione** | *In gioco* (come nel gioco), *Glue* (come nei menu) o *Dinamica*. |
| **Sincronizza camera** | L'anteprima segue la viewport attiva di 3ds Max. |
| **Risincronizza** | Legge di nuovo tutta la scena, per esempio dopo che hai aggiunto oggetti. |

## Menu

| Menu | Cosa contiene |
|---|---|
| **Visualizza** | Mostra o nasconde la griglia, le particelle, le scie e gli oggetti evento. *Grafica di Warcraft III*: quali texture usare (*Segui la modalità di rendering*, *Classic*, *Reforged (HD)*, *Definitive*). *Tileset*: il terreno. |
| **Debug** | Marcatori per forme di collisione e luci, viste speciali (albedo, normali, livello di dettaglio, numero di luci …) e una scelta del livello di dettaglio. |
| **Impostazioni** | Sfondo, luminosità, volume dei suoni, ora del giorno, illuminazione basata su immagini, ombre, nebbia, il backend grafico (Direct3D 11, Direct3D 12, Vulkan) e le cartelle del gioco. |

## Buono a sapersi

- Le particelle e le scie sono simulate come nel gioco. Quando salti sul cursore del tempo, l'anteprima calcola gli effetti fino al nuovo fotogramma. Così appaiono come se l'animazione fosse arrivata lì riproducendosi.
- L'anteprima salva le sue impostazioni in `WhiteoutFlakes.ini` accanto a `3dsmax.exe`.
- Se l'anteprima non mostra niente o si blocca, guarda `WhiteoutDex_render.log` (vedi *File e impostazioni*).

!!! tip
    Scegli *Classic* o *Reforged (HD)* in **Visualizza > Grafica di Warcraft III** per vedere come appare il tuo modello in ciascuna versione del gioco.
