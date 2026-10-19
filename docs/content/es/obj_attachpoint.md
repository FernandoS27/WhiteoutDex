# Punto de anclaje

Un punto de anclaje marca un lugar del modelo. El juego coloca cosas allí: el arma de un héroe, un efecto de hechizo en la mano, la flecha que golpea el pecho, la barra de vida sobre la cabeza.

Créalo con **Create > Helpers > Warcraft III Plugins > Wc3 AttachPoint**. Vincúlalo al hueso al que debe seguir.

En el visor, cada punto de anclaje muestra su nombre en amarillo.

## Funciones generales

[[shot:obj_attachpoint_r1|Funciones generales.]]

| N.º | Qué hace |
|---|---|
| [[ctl:3]] | Haz clic aquí y después haz clic en un objeto del visor. El punto de anclaje salta a ese objeto y se vincula a él. |

## Parámetros del mapa del modelo

[[shot:obj_attachpoint_r2|Parámetros del mapa del modelo.]]

Un punto de anclaje puede llevar su propio modelo, por ejemplo una espada que la unidad siempre sostiene.

| N.º | Qué hace |
|---|---|
| [[ctl:9]] | Elige el archivo de modelo de tu disco. |
| [[ctl:10]] | Elige un modelo de los archivos del juego. |

Los dos campos sobre los botones contienen la carpeta y el nombre del archivo. La ruta es relativa a la carpeta de Warcraft III, por ejemplo `Textures\Model.mdx`.

## Parámetros del anclaje

[[shot:obj_attachpoint_r3|Parámetros del anclaje.]]

El juego encuentra un punto de anclaje por su **nombre**. Construyes el nombre con cuatro partes:

| N.º | Qué hace |
|---|---|
| [[ctl:5]] | **Tipo:** el lugar principal: *Head*, *Overhead*, *Origin*, *Foot*, *Chest*, *Hand*, *Weapon* o *Sprite*. |
| [[ctl:8]] | **Montura:** para jinetes sobre una montura. |
| [[ctl:11]] | **Lado:** *Left* o *Right*, para manos y pies. |
| [[ctl:14]] | **Extra:** más palabras, por ejemplo *Rear*, *Alternate*, *RallyPoint*, *Gold*, o *First* … *Sixth* para edificios. |
| [[ctl:22]] | Usa el modelo de *Parámetros del mapa del modelo*. |
| [[ctl:26]] | Visibilidad (0 a 1). Anímala para mostrar u ocultar el modelo anclado. |
| [[ctl:27]] | Muestra respecto a qué es relativa la ruta del modelo. |
| [[ctl:34]] | Un número para el punto de anclaje. |
| [[ctl:36]] | Escribe este número en el modelo. |

Ejemplo: el tipo *Hand* y el lado *Right* dan el nombre `Hand Right Ref`. El juego pone allí un efecto de hechizo para la mano derecha.

!!! tip
    La herramienta *Puntos de anclaje de un clic* de *Anim.: herramientas generales* crea por ti los puntos de anclaje habituales, cuando tus huesos tienen nombres conocidos.
