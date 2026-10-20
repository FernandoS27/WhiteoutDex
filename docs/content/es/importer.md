# El importador

El importador carga un modelo de Warcraft III (`.mdx` o `.mdl`) en 3ds Max. También carga las texturas del modelo desde tu juego.

## Abrir el importador

Usa una de estas formas:

- Menú **WhiteoutDex > [[t:menu_importer]]**
- El botón **Importar** de la barra lateral de WhiteoutDex
- **File > Import** y elige el tipo de archivo *Warcraft III MDX/MDL Model*

Elige un archivo. Después se abre la ventana de importación.

## La ventana de importación

[[shot:importer|La ventana de importación con la pestaña Objetos.]]

La parte superior de la ventana muestra el nombre del archivo, la versión del juego del modelo (*Clásico* o *Reforged*) y cuántos huesos, mallas (geosets) y animaciones tiene.

| N.º | Qué hace |
|---|---|
| [[ctl:1052]] | **Ajustes rápidos.** Elige un preajuste. Define por ti todos los interruptores de abajo. Elige *Todo* para importarlo todo. Los otros preajustes importan menos, por ejemplo solo la malla sin materiales. |
| [[ctl:1059]] | Borra la escena abierta e importa el modelo en una escena vacía. |
| [[ctl:1060]] | Añade el modelo a la escena abierta. Úsalo para combinar modelos. |
| [[ctl:1061]] | Muestra los objetos del modelo (esta imagen). |
| [[ctl:1062]] | Muestra las opciones de animación. |
| [[ctl:1063]] | Muestra las opciones de malla, materiales y texturas. |
| [[ctl:1021]] | Interruptor para todos los objetos. Si está desactivado, no se importa ningún objeto. |
| [[ctl:1022]] | El esqueleto. Lo necesitas para las animaciones y para las mallas con skin. |
| [[ctl:1023]] | Objetos ayudantes. Mueven otros objetos, pero no los ves en el juego. |
| [[ctl:1024]] | Luces. |
| [[ctl:1025]] | Puntos de anclaje, por ejemplo para armas y efectos de hechizos. |
| [[ctl:1028]] | Objetos de evento, por ejemplo sonidos, huellas y sangre. |
| [[ctl:1026]] | Emisor de partículas 1: emisores que crean modelos pequeños. |
| [[ctl:1027]] | Emisor de partículas 2: emisores de fuego, humo, chispas y efectos parecidos. |
| [[ctl:1029]] | Emisores de estelas: estelas, por ejemplo detrás de una espada. |
| [[ctl:1030]] | Formas de colisión (cajas y esferas). El juego las usa para saber dónde puedes hacer clic en el modelo. |
| [[ctl:1031]] | Cámaras, por ejemplo la cámara de retrato. |
| [[ctl:1]] | Inicia la importación. |
| [[ctl:2]] | Cierra la ventana sin importar. |

El número junto a cada tipo de objeto te dice cuántos tiene el modelo. Los tipos que el modelo no tiene aparecen en gris.

!!! note
    Los modelos de Reforged pueden tener dos tipos de objeto más: emisores *Popcorn FX* y *FaceFX*. Solo los ves cuando el modelo los tiene.

### Pestaña Animaciones

[[shot:importer_animations|La pestaña Animaciones.]]

| N.º | Qué hace |
|---|---|
| [[ctl:1041]] | Interruptor para todas las animaciones. Si está desactivado, el modelo llega sin movimiento. |
| [[ctl:1042]] | Movimiento (claves de posición). |
| [[ctl:1043]] | Claves de rotación. |
| [[ctl:1044]] | Claves de escala. |
| [[ctl:1048]] | Claves de mostrar y ocultar. |
| [[ctl:1045]] | Valores animados de los objetos, por ejemplo la intensidad de una luz o la tasa de emisión de las partículas. |
| [[ctl:1046]] | Coordenadas de textura que se mueven (animación UV). |
| [[ctl:1047]] | Cambios de textura en el tiempo (flipbook). |
| [[ctl:1049]] | Cambios de color en el tiempo. |

### Pestaña Modelo

[[shot:importer_model|La pestaña Modelo.]]

| N.º | Qué hace |
|---|---|
| [[ctl:1001]] | Importa el skin: qué hueso mueve cada vértice. Sin él, la malla no sigue a los huesos. |
| [[ctl:1011]] | Crea materiales de WhiteoutDex para el modelo. |
| [[ctl:1012]] | Carga las texturas. WhiteoutDex busca en las carpetas de tu juego (consulta *Ajustes*). |
| [[ctl:1071]] | Importa los ayudantes como objetos *Point*. Si está desactivado, los ayudantes llegan como huesos. |
| [[ctl:1081]] | Suelda los vértices que están en el mismo lugar y simplifica la malla. |
| [[ctl:1082]] | Quita los huesos y ayudantes que no tienen animación y no hacen falta. |

## De dónde vienen las texturas

WhiteoutDex busca cada textura en este orden:

1. En la carpeta del modelo y en sus subcarpetas.
2. En los archivos del juego. Para los modelos clásicos (versión 800) busca primero en los archivos **MPQ** y después en **CASC**. Para los modelos de Reforged es al revés.

La línea inferior de la ventana te dice qué archivos usa. Defines las carpetas del juego en [[t:menu_settings]].

!!! tip
    El importador guarda junto al archivo del modelo las texturas que toma de los archivos del juego. Así, la próxima vez las encuentra enseguida.

## Es bueno saber

- El importador recuerda tus opciones para la próxima vez.
- Para importar un modelo del juego sin un archivo en tu disco, consulta *Importar desde el juego*.
