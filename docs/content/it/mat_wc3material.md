# Wc3Material

Ogni mesh che esporti ha bisogno di un **Wc3Material**. Contiene le texture e dice al gioco come disegnarle. L'importatore crea i Wc3Material per te. Per le tue mesh, creane uno nel Material Editor o converti i tuoi materiali con *Correzione materiali*.

Le immagini mostrano un materiale del modello di esempio nel Compact Material Editor.

## Parametri di base del materiale

[[shot:mat_wc3material_r1|Parametri di base del materiale.]]

| N. | Cosa fa |
|---|---|
| [[ctl:3]] | **Shader.** *SD* per i modelli classici, *HD* per Reforged, *SD on HD* per le texture classiche in un modello Reforged, *Crystal* per lo shader dei cristalli di Reforged. |
| [[ctl:5]] | **Modalità filtro** – come viene disegnato il materiale (vedi la tabella sotto). |
| [[ctl:8]] | Vengono disegnati entrambi i lati delle facce. |
| [[ctl:9]] | Ignora la luce. Adatto per le parti che brillano. |
| [[ctl:10]] | Ignora la nebbia. |
| [[ctl:11]] | Un solo colore per tutta la mesh. |
| [[ctl:12]] | Imposta il flag *Risoluzione piena* del materiale. |
| [[ctl:13]] | Disegnato sopra le altre cose. |
| [[ctl:14]] | Non nasconde ciò che c'è dietro. |
| [[ctl:15]] | Il giocatore non può cliccare questa parte. |
| [[ctl:16]] | Usa l'occlusione ambientale (Reforged). |
| [[ctl:17]] | Le facce posteriori proiettano ombre (Reforged). |
| [[ctl:18]] | Per niente illuminato (Reforged). |
| [[ctl:20]] | Ordine di disegno dei livelli trasparenti: da vicino a lontano o da lontano a vicino. |
| [[ctl:24]] | Ordine di disegno tra i materiali (da −20 a 20). Un valore più alto viene disegnato dopo. |
| [[ctl:26]] | Usa una texture che il gioco riempie: colore della squadra, bagliore della squadra, scogliera o una delle texture degli alberi. |

| Modalità filtro | Aspetto | Usala per |
|---|---|---|
| Nessuno | Solido. | Parti normali. |
| Trasparente | Buchi dove l'alfa è basso, bordi netti. | Foglie, capelli, recinti. |
| Fusione | Trasparenza morbida. | Vetro, bordi morbidi, ombre. |
| Additivo | Aggiunge luce, brilla. Il nero è invisibile. | Fuoco, magia, raggi di luce. |
| AddAlpha | Aggiunge luce, usa l'alfa. | Bagliori con una forma. |
| Modulazione | Scurisce ciò che c'è dietro. | Segni scuri, ombre. |
| Modulazione 2x | Scurisce o schiarisce. | Effetti speciali. |

## Animazione del livello

[[shot:mat_wc3material_r2|Animazione del livello.]]

| N. | Cosa fa |
|---|---|
| [[ctl:6]] | Opacità del livello (da 0 a 1). Animala per far comparire e scomparire il materiale. |
| [[ctl:7]] | Come cambia l'opacità tra le chiavi. |
| [[ctl:9]] | Scrive l'animazione dell'opacità nel modello. |
| [[ctl:11]] | La texture cambia a passi nel tempo, come un flipbook. |
| [[ctl:14]] | Quale canale UV usa la texture. |
| [[ctl:15]] | Scrive nel modello l'animazione del cambio di texture. |

Le texture che si muovono (animazione UV) si impostano sul *Wc3Bitmap*, non qui.

## Mappe texture del materiale

[[shot:mat_wc3material_r3|Mappe texture del materiale.]]

| N. | Cosa fa |
|---|---|
| [[ctl:4]] | **Diffusa:** la texture del colore. I modelli classici usano solo questa. |
| [[ctl:5]] | Apre il browser delle texture per questo slot (texture del gioco). |
| [[ctl:10]] | **Normale:** i dettagli della superficie (Reforged). |
| [[ctl:16]] | **ORM:** occlusione, rugosità e metallo in una sola texture (Reforged). |
| [[ctl:22]] | **Emissiva:** le parti che brillano da sole (Reforged). |
| [[ctl:28]] | **Colore squadra:** una maschera per il colore della squadra (Reforged, opzionale). |
| [[ctl:34]] | **Ambiente:** la texture dei riflessi (Reforged). |

Ogni slot ha un **Prefisso del percorso**: la cartella che l'esportatore scrive davanti al nome del file, per esempio `Textures\`.

## Impostazioni Reforged

[[shot:mat_wc3material_r4|Impostazioni Reforged.]]

| N. | Cosa fa |
|---|---|
| [[ctl:5]] | Quanto forte brilla la texture emissiva. |
| [[ctl:9]] | Colore Fresnel (luce sui bordi): rosso … |
| [[ctl:12]] | … verde … |
| [[ctl:15]] | … e blu. |
| [[ctl:18]] | Intensità della luce sui bordi. |
| [[ctl:21]] | Quanto colore della squadra usa la luce sui bordi. |
