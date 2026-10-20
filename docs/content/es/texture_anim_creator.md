# Creador de animación de textura

Hace que las texturas se muevan: agua que fluye, un fuego de una hoja de sprites o un flipbook de muchas imágenes. La herramienta escribe las claves por ti.

Ábrelo con **WhiteoutDex > [[t:menu_texture_anim_creator]]**.

Hay tres tipos de animación:

| Tipo | Úsalo para |
|---|---|
| **Anim flujo** | La textura se desliza, gira o crece: agua, lava, energía. |
| **Sec. (única)** | Una imagen con muchas imágenes pequeñas en una cuadrícula (una *hoja de sprites*). La herramienta las muestra una tras otra. |
| **Sec. (varias)** | Una carpeta con muchas imágenes. La herramienta las muestra una tras otra (un *flipbook*). |

[[shot:texture_anim_creator|El Creador de animación de textura con Sec. (única).]]

## Elegir la textura

| N.º | Qué hace |
|---|---|
| [[ctl:4]] | Haz clic en una malla del visor para usar su material. |
| [[ctl:5]] | Usa la malla seleccionada. |
| [[ctl:6]] | Usa el material activo en el Material Editor. |
| [[ctl:8]] | El material. |
| [[ctl:11]] | La textura del material. La línea de debajo muestra su nombre y su tamaño. |
| [[ctl:14]] | Anim flujo. |
| [[ctl:15]] | Sec. (única). |
| [[ctl:16]] | Sec. (varias). |

## Sec. (única): una hoja de sprites

| N.º | Qué hace |
|---|---|
| [[ctl:46]] | El fotograma donde empieza la animación. |
| [[ctl:50]] | Cuántas veces se repite la animación. |
| [[ctl:54]] | Cuántas imágenes hay en una fila de la hoja. |
| [[ctl:58]] | Cuántas filas tiene la hoja. |
| [[ctl:62]] | La primera imagen que se muestra. |
| [[ctl:66]] | La última imagen que se muestra. |
| [[ctl:68]] | Salta fotogramas entre dos imágenes (0 = cada fotograma muestra una imagen nueva). |
| [[ctl:71]] | Hacia delante o hacia atrás. |
| [[ctl:74]] | Imágenes por segundo: 10, 15, 30 o 60. |
| [[ctl:77]] | **SÍ:** la animación se repite siempre como secuencia global. **NO:** se ejecuta en la línea de tiempo. |
| [[ctl:80]] | **SÍ:** conserva las claves que ya tiene la textura. **NO:** las reemplaza. |
| [[ctl:84]] | Encuentra automáticamente el número de columnas y filas de la imagen. |
| [[ctl:85]] | Ajusta las coordenadas UV de la malla a una imagen de la hoja. Lo necesitas una vez. |
| [[ctl:87]] | **SÍ:** contrae el modificador UV en la malla después de *Generar mapa UV*. |
| [[ctl:116]] | Escribe la animación. La línea de encima muestra cuántas claves tendrá y cuánto durará. |

## Anim flujo

[[shot:texture_anim_flow|Anim flujo.]]

1. Define el tiempo de inicio y de fin.
2. Haz clic en las flechas de la dirección en que se mueve la textura. También puedes girarla (izquierda, derecha) y hacer que crezca o se reduzca (+, −).
3. Elige **SÍ** en *Animación global* para una textura que se mueve siempre.
4. Haz clic en **Generar animación**.

## Sec. (varias)

[[shot:texture_anim_many|Sec. (varias).]]

1. Elige la carpeta con las imágenes (botón **...**). Las imágenes se usan en el orden de sus nombres.
2. Define el orden de reproducción, la velocidad y el fotograma inicial.
3. Define el prefijo de ruta, por ejemplo `Textures\`. El modelo usa esta ruta para cada imagen.
4. Haz clic en **Generar animación**.

!!! tip
    Las claves de la textura están en el *Wc3Bitmap* (animación UV) o en el *Wc3Material* (flipbook). Búscalas allí para cambiarlas después.
