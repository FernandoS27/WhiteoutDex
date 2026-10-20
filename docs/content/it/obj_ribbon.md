# Emettitore di scie

Un emettitore di scie disegna una **scia** dietro un punto che si muove: la scia luminosa di un colpo di spada, la coda di una cometa, il fumo di un razzo. Collegalo all'osso che si muove, per esempio l'arma.

Crealo con **Create > Geometry > Particle Systems > Wc3Ribbon**.

## Ribbon Material Parameters

[[shot:obj_ribbon_r1|Ribbon Material Parameters.]]

| N. | Cosa fa |
|---|---|
| [[ctl:1079]] | Il materiale della scia (un *Wc3Material*). Clicca per metterlo nel Material Editor. |
| [[ctl:1073]] | La texture è una griglia di immagini: numero di righe (1, 2, 4, 8 …) … |
| [[ctl:1075]] | … e di colonne (1, 2, 4, 8 …). |
| [[ctl:1077]] | Quale immagine della griglia mostra la scia. Animalo per far tremolare la scia. |

## Ribbon Properties

[[shot:obj_ribbon_r2|Ribbon Properties.]]

| N. | Cosa fa |
|---|---|
| [[ctl:1066]] | Il colore della scia. |
| [[ctl:1059]] | Quanto arriva in alto la scia sopra l'emettitore. |
| [[ctl:1067]] | Quanto arriva in basso sotto l'emettitore. Sopra + sotto = larghezza della scia. |
| [[ctl:1080]] | Trasparenza: 0 = invisibile, 1 = solida. Animala a 0 per nascondere la scia. |
| [[ctl:1069]] | Quanti pezzi di scia partono ogni secondo. Più pezzi rendono la scia più morbida. |
| [[ctl:1071]] | Quanto resta un pezzo, in secondi. Una vita più lunga rende la scia più lunga. |
| [[ctl:1082]] | Tira la scia verso il basso. |
| [[ctl:1090]] | **Controller:** come cambia un valore animato tra le chiavi. Ogni valore ha il suo controller. |

!!! tip
    Per la scia di una spada: emettitore sulla lama, materiale *Additivo*, alfa a 0 tranne durante il colpo, durata di vita di circa 0.2 secondi.
