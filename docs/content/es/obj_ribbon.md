# Emisor de estelas

Un emisor de estelas dibuja una **estela** detrás de un punto que se mueve: la estela de luz de un golpe de espada, la cola de un cometa, el humo de un cohete. Vincúlalo al hueso que se mueve, por ejemplo el arma.

Créalo con **Create > Geometry > Particle Systems > Wc3Ribbon**.

## Parámetros del material de la estela (*Ribbon Material Parameters*)

[[shot:obj_ribbon_r1|La persiana Ribbon Material Parameters.]]

| N.º | Qué hace |
|---|---|
| [[ctl:1079]] | El material de la estela (un *Wc3Material*). Haz clic para ponerlo en el Material Editor. |
| [[ctl:1073]] | La textura es una cuadrícula de imágenes: número de filas (1, 2, 4, 8 …) … |
| [[ctl:1075]] | … y de columnas (1, 2, 4, 8 …). |
| [[ctl:1077]] | Qué imagen de la cuadrícula muestra la estela. Anímalo para que la estela parpadee. |

## Propiedades de la estela (*Ribbon Properties*)

[[shot:obj_ribbon_r2|La persiana Ribbon Properties.]]

| N.º | Qué hace |
|---|---|
| [[ctl:1066]] | El color de la estela. |
| [[ctl:1059]] | Hasta dónde llega la estela por encima del emisor. |
| [[ctl:1067]] | Hasta dónde llega por debajo del emisor. Arriba + abajo = anchura de la estela. |
| [[ctl:1080]] | Transparencia: 0 = invisible, 1 = sólido. Anímala a 0 para ocultar la estela. |
| [[ctl:1069]] | Cuántas piezas de estela salen por segundo. Más piezas crean una estela más suave. |
| [[ctl:1071]] | Cuánto tiempo dura una pieza, en segundos. Una vida más larga crea una estela más larga. |
| [[ctl:1082]] | Tira de la estela hacia abajo. |
| [[ctl:1090]] | **Controlador:** cómo cambia un valor animado entre las claves. Cada valor tiene su propio controlador. |

!!! tip
    Para la estela de una espada: emisor en la hoja, material *Aditivo*, alfa 0 excepto durante el golpe, vida de unos 0.2 segundos.
