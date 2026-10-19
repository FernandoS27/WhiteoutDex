# Wc3Bitmap

El **Wc3Bitmap** es el mapa de textura de WhiteoutDex. Ponlo en las ranuras de un *Wc3Material*. Lee `.blp`, `.dds`, `.tga`, `.png` y los demás formatos de imagen de 3ds Max. Además guarda la animación de la textura (animación UV) y la ruta de la textura en el juego.

## Parámetros de textura WC3

[[shot:mat_wc3bitmap_r1|Parámetros de textura WC3.]]

| N.º | Qué hace |
|---|---|
| [[ctl:6]] | Elige un archivo de imagen de tu disco. |
| [[ctl:7]] | Elige una textura de los archivos del juego. |
| [[ctl:9]] | Usa una textura que pone el juego (color de equipo, brillo de equipo, acantilado, árboles). |
| [[ctl:12]] | La textura se repite a lo ancho. |
| [[ctl:13]] | La textura se repite a lo alto. |
| [[ctl:14]] | Usa la textura como reflejo (mapa de entorno esférico). |

## Animación UV (TXAN)

[[shot:mat_wc3bitmap_r2|Animación UV.]]

Anima estos valores para que la textura se mueva. El *Creador de animación de textura* puede definirlos por ti.

| N.º | Qué hace |
|---|---|
| [[ctl:3]] | Cómo cambian los valores entre las claves. |
| [[ctl:8]] | Mueve la textura hacia los lados (U). |
| [[ctl:11]] | Mueve la textura hacia arriba y hacia abajo (V). |
| [[ctl:14]] | Desplazamiento W. |
| [[ctl:17]] | Gira la textura. |
| [[ctl:20]] | Repite la textura más o menos veces hacia los lados. |
| [[ctl:23]] | Repite la textura más o menos veces hacia arriba y hacia abajo. |

## Ruta de prefijo

[[shot:mat_wc3bitmap_r3|Ruta de prefijo.]]

El prefijo es la carpeta que el exportador escribe delante del nombre de la textura, por ejemplo `Textures\` o `war3mapImported\`.

| N.º | Qué hace |
|---|---|
| [[ctl:5]] | Elige un prefijo guardado. |
| [[ctl:6]] | Edita la lista de prefijos guardados. |

!!! tip
    Las texturas de un mapa deben usar el prefijo que usa el mapa. Para los modelos propios en los mapas, suele ser `war3mapImported\`.
