# Wc3Bitmap

Il **Wc3Bitmap** è la mappa texture di WhiteoutDex. Mettilo negli slot di un *Wc3Material*. Legge `.blp`, `.dds`, `.tga`, `.png` e gli altri formati di immagine di 3ds Max. Contiene l'animazione della texture (animazione UV) e il percorso della texture nel gioco.

## Parametri texture WC3

[[shot:mat_wc3bitmap_r1|Parametri texture WC3.]]

| N. | Cosa fa |
|---|---|
| [[ctl:6]] | Scegli un file immagine sul tuo disco. |
| [[ctl:7]] | Scegli una texture dagli archivi del gioco. |
| [[ctl:9]] | Usa una texture che il gioco riempie (colore della squadra, bagliore della squadra, scogliera, alberi). |
| [[ctl:12]] | La texture si ripete lungo la larghezza. |
| [[ctl:13]] | La texture si ripete lungo l'altezza. |
| [[ctl:14]] | Usa la texture come riflesso (mappa d'ambiente sferica). |

## Animazione UV (TXAN)

[[shot:mat_wc3bitmap_r2|Animazione UV.]]

Anima questi valori per far muovere la texture. Il *Creatore animazione texture* può impostarli per te.

| N. | Cosa fa |
|---|---|
| [[ctl:3]] | Come cambiano i valori tra le chiavi. |
| [[ctl:8]] | Sposta la texture di lato (U). |
| [[ctl:11]] | Sposta la texture in su e in giù (V). |
| [[ctl:14]] | Offset W. |
| [[ctl:17]] | Fa girare la texture. |
| [[ctl:20]] | Ripete la texture più o meno spesso di lato. |
| [[ctl:23]] | Ripete la texture più o meno spesso in su e in giù. |

## Percorso prefisso

[[shot:mat_wc3bitmap_r3|Percorso prefisso.]]

Il prefisso è la cartella che l'esportatore scrive davanti al nome della texture, per esempio `Textures\` o `war3mapImported\`.

| N. | Cosa fa |
|---|---|
| [[ctl:5]] | Scegli un prefisso salvato. |
| [[ctl:6]] | Modifica l'elenco dei prefissi salvati. |

!!! tip
    Le texture in una mappa devono usare il prefisso che usa la mappa. Per i modelli personalizzati nelle mappe di solito è `war3mapImported\`.
