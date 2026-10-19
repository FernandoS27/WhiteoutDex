# Emettitore di particelle 2

L'Emettitore di particelle 2 crea la maggior parte degli effetti di Warcraft III: fuoco, fumo, scintille, polvere, magia. Emette molte piccole immagini piatte (particelle). Ogni particella vive per poco tempo e mentre vive cambia colore, dimensione e trasparenza.

Crealo con **Create > Geometry > Particle Systems > Wc3Particles2**. WhiteoutDex mostra le particelle direttamente nella viewport.

Le immagini qui sotto mostrano l'emettitore del modello di esempio *HeroPaladin*.

## Particle Configuration

[[shot:obj_particles2_r1|Particle Configuration.]]

| N. | Cosa fa |
|---|---|
| [[ctl:1030]] | Carica tutte le impostazioni da un file `.ini`. |
| [[ctl:1031]] | Salva tutte le impostazioni in un file `.ini`. Usalo per copiare un effetto su un altro modello. |
| [[ctl:1032]] | Quando è attivo, *Importa* carica anche le chiavi animate dei valori, non solo i valori fissi. |

## Texture Options

[[shot:obj_particles2_r2|Texture Options.]]

| N. | Cosa fa |
|---|---|
| [[ctl:1047]] | La cartella della texture dentro il gioco, per esempio `Textures\`. |
| [[ctl:3075]] | Il nome del file della texture. |
| [[ctl:1033]] | Scegli un file di texture sul tuo disco. |
| [[ctl:1034]] | Scegli una texture dagli archivi del gioco. |
| [[ctl:1035]] | Come vengono disegnate le particelle: *Blend* (trasparenza normale), *Additive* (brillano, adatto per fuoco e magia), *Modulate*, *Modulate 2X* (scuriscono) o *Alpha Key* (bordi netti). |
| [[ctl:4113]] | Le particelle ignorano la luce. |
| [[ctl:4115]] | Le particelle ignorano la nebbia. |
| [[ctl:4112]] | Ordina le particelle per distanza. Usalo quando le particelle trasparenti si sovrappongono in modo sbagliato. |
| [[ctl:4114]] | Usa come texture il colore della squadra o il bagliore della squadra. |
| [[ctl:3094]] | La texture è una griglia di immagini: numero di righe … |
| [[ctl:3096]] | … e di colonne. La particella mostra le immagini una dopo l'altra. |

## Emitter Options

[[shot:obj_particles2_r3|Emitter Options.]]

| N. | Cosa fa |
|---|---|
| [[ctl:3048]] | Quanto velocemente volano via le particelle. |
| [[ctl:3050]] | Quanto cambia la velocità da una particella all'altra (0 = tutte uguali). |
| [[ctl:3052]] | L'angolo del cono in cui volano le particelle. Un angolo piccolo crea un fascio stretto, un angolo grande uno spruzzo largo. |
| [[ctl:3057]] | Tira le particelle verso il basso (o verso l'alto con un valore negativo). |
| [[ctl:1040]] | **Controllo:** come cambia un valore animato tra le chiavi (vedi *Glossario*). Ogni valore ha il suo controllo. |

## Timing Options

[[shot:obj_particles2_r4|Timing Options.]]

| N. | Cosa fa |
|---|---|
| [[ctl:3042]] | Quanti secondi vive una particella. |
| [[ctl:3053]] | Quante particelle partono ogni secondo. Animalo a 0 per fermare l'effetto. |
| [[ctl:1020]] | Emette tutte le particelle insieme su una chiave, come un'esplosione. |

## Size Options

[[shot:obj_particles2_r5|Size Options.]]

| N. | Cosa fa |
|---|---|
| [[ctl:3044]] | Larghezza dell'area in cui partono le particelle. |
| [[ctl:3046]] | Lunghezza di quest'area. |
| [[ctl:1028]] | Le particelle partono lungo una linea invece che da un'area. |

## Particle Options

[[shot:obj_particles2_r6|Particle Options.]]

| N. | Cosa fa |
|---|---|
| [[ctl:1023]] | La particella è un'immagine piatta (testa). |
| [[ctl:1024]] | La particella è una scia allungata dietro il suo movimento (coda). |
| [[ctl:1025]] | Sia testa che coda. |
| [[ctl:3073]] | Lunghezza della coda. |
| [[ctl:6027]] | In quale momento della sua vita la particella raggiunge i valori *intermedi* (0.5 = a metà della sua vita). |
| [[ctl:6033]] | Il colore all'inizio, a metà e alla fine della vita. Clicca un riquadro per scegliere un colore. |
| [[ctl:6037]] | Trasparenza all'inizio, a metà e alla fine: 0 = invisibile, 255 = solida. |
| [[ctl:6041]] | Dimensione all'inizio, a metà e alla fine. |
| [[ctl:6045]] | Quali immagini della griglia della texture mostra la testa mentre vive … |
| [[ctl:6049]] | … e mentre svanisce. |
| [[ctl:6053]] | Lo stesso per la coda mentre vive … |
| [[ctl:6057]] | … e mentre svanisce. |

## Other Options

[[shot:obj_particles2_r7|Other Options.]]

| N. | Cosa fa |
|---|---|
| [[ctl:3059]] | Ordine di disegno. I valori più alti vengono disegnati dopo (sopra). |
| [[ctl:4116]] | Le particelle si muovono con il modello. Senza questa opzione restano dove sono partite. |
| [[ctl:4117]] | Le particelle stanno piatte sul terreno invece di guardare verso la telecamera. Adatto per anelli e onde. |

!!! tip
    Per fuoco e magia usa *Additive*, un angolo del cono piccolo e una durata di vita breve. Fai andare l'alfa da 0 a 255 e di nuovo a 0, così le particelle compaiono e scompaiono in modo morbido.
