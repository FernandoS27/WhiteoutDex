# Emisor de partículas 2

El emisor de partículas 2 crea la mayoría de los efectos de Warcraft III: fuego, humo, chispas, polvo, magia. Emite muchas imágenes planas pequeñas (partículas). Cada partícula vive poco tiempo y cambia su color, tamaño y transparencia mientras vive.

Créalo con **Create > Geometry > Particle Systems > Wc3Particles2**. WhiteoutDex muestra las partículas directamente en el visor.

Las imágenes de abajo muestran el emisor del modelo de ejemplo *HeroPaladin*.

## Configuración de partículas (*Particle Configuration*)

[[shot:obj_particles2_r1|La persiana Particle Configuration.]]

| N.º | Qué hace |
|---|---|
| [[ctl:1030]] | Carga todos los ajustes desde un archivo `.ini`. |
| [[ctl:1031]] | Guarda todos los ajustes en un archivo `.ini`. Úsalo para copiar un efecto a otro modelo. |
| [[ctl:1032]] | Si está activado, *Importar* carga también las claves animadas de los valores, no solo los valores fijos. |

## Opciones de textura (*Texture Options*)

[[shot:obj_particles2_r2|La persiana Texture Options.]]

| N.º | Qué hace |
|---|---|
| [[ctl:1047]] | La carpeta de la textura dentro del juego, por ejemplo `Textures\`. |
| [[ctl:3075]] | El nombre del archivo de la textura. |
| [[ctl:1033]] | Elige un archivo de textura de tu disco. |
| [[ctl:1034]] | Elige una textura de los archivos del juego. |
| [[ctl:1035]] | Cómo se dibujan las partículas: *Blend* (transparencia normal), *Additive* (brilla, bueno para fuego y magia), *Modulate*, *Modulate 2X* (oscurece) o *Alpha Key* (bordes duros). |
| [[ctl:4113]] | Las partículas ignoran la luz. |
| [[ctl:4115]] | Las partículas ignoran la niebla. |
| [[ctl:4112]] | Ordena las partículas por distancia. Úsalo cuando las partículas transparentes se superponen mal. |
| [[ctl:4114]] | Usa el color de equipo o el brillo de equipo como textura. |
| [[ctl:3094]] | La textura es una cuadrícula de imágenes: número de filas … |
| [[ctl:3096]] | … y de columnas. La partícula muestra las imágenes una tras otra. |

## Opciones del emisor (*Emitter Options*)

[[shot:obj_particles2_r3|La persiana Emitter Options.]]

| N.º | Qué hace |
|---|---|
| [[ctl:3048]] | Con qué rapidez salen volando las partículas. |
| [[ctl:3050]] | Cuánto cambia la velocidad de una partícula a otra (0 = todas iguales). |
| [[ctl:3052]] | El ángulo del cono en el que vuelan las partículas. Un ángulo pequeño crea un haz estrecho, un ángulo grande un chorro ancho. |
| [[ctl:3057]] | Tira de las partículas hacia abajo (o hacia arriba con un valor negativo). |
| [[ctl:1040]] | **Control:** cómo cambia un valor animado entre las claves (consulta *Glosario*). Cada valor tiene su propio control. |

## Opciones de tiempo (*Timing Options*)

[[shot:obj_particles2_r4|La persiana Timing Options.]]

| N.º | Qué hace |
|---|---|
| [[ctl:3042]] | Cuántos segundos vive una partícula. |
| [[ctl:3053]] | Cuántas partículas salen por segundo. Anímalo a 0 para detener el efecto. |
| [[ctl:1020]] | Emite todas las partículas a la vez en una clave, como una ráfaga. |

## Opciones de tamaño (*Size Options*)

[[shot:obj_particles2_r5|La persiana Size Options.]]

| N.º | Qué hace |
|---|---|
| [[ctl:3044]] | Anchura de la zona donde salen las partículas. |
| [[ctl:3046]] | Longitud de esta zona. |
| [[ctl:1028]] | Las partículas salen a lo largo de una línea en lugar de una zona. |

## Opciones de partícula (*Particle Options*)

[[shot:obj_particles2_r6|La persiana Particle Options.]]

| N.º | Qué hace |
|---|---|
| [[ctl:1023]] | La partícula es una imagen plana (cabeza). |
| [[ctl:1024]] | La partícula es una estela estirada detrás de su movimiento (cola). |
| [[ctl:1025]] | Cabeza y cola a la vez. |
| [[ctl:3073]] | Longitud de la cola. |
| [[ctl:6027]] | En qué momento de su vida la partícula alcanza los valores *intermedios* (0.5 = la mitad de su vida). |
| [[ctl:6033]] | El color al inicio, en el medio y al final de la vida. Haz clic en un cuadro para elegir un color. |
| [[ctl:6037]] | Transparencia al inicio, en el medio y al final: 0 = invisible, 255 = sólido. |
| [[ctl:6041]] | Tamaño al inicio, en el medio y al final. |
| [[ctl:6045]] | Qué imágenes de la cuadrícula de textura muestra la cabeza mientras vive … |
| [[ctl:6049]] | … y mientras se desvanece. |
| [[ctl:6053]] | Lo mismo para la cola mientras vive … |
| [[ctl:6057]] | … y mientras se desvanece. |

## Otras opciones (*Other Options*)

[[shot:obj_particles2_r7|La persiana Other Options.]]

| N.º | Qué hace |
|---|---|
| [[ctl:3059]] | Orden de dibujo. Los valores más altos se dibujan después (encima). |
| [[ctl:4116]] | Las partículas se mueven con el modelo. Sin esta opción, se quedan donde salieron. |
| [[ctl:4117]] | Las partículas quedan planas en el suelo en lugar de mirar a la cámara. Sirve para anillos y ondas. |

!!! tip
    Para fuego y magia usa *Additive*, un ángulo de cono pequeño y una vida corta. Haz que el alfa vaya de 0 a 255 y otra vez a 0. Así las partículas aparecen y desaparecen con suavidad.
