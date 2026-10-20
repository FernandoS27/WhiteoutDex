# Popcorn FX

**Popcorn FX** es el sistema de partículas de Warcraft III: Reforged. El efecto en sí es un archivo `.pkfx` (o `.pkb`) creado con el editor de PopcornFX. El objeto Popcorn de 3ds Max coloca este efecto en el modelo y controla cuándo se reproduce.

Créalo con **Create > Helpers > Warcraft III Plugins > Wc3 Popcorn**. Popcorn solo funciona en modelos de **Reforged**.

## Archivo de Popcorn FX

[[shot:obj_popcorn_r2|Archivo de Popcorn FX.]]

| N.º | Qué hace |
|---|---|
| [[ctl:9]] | Elige un efecto `.pkb` o `.pkfx` de los archivos del juego. |
| [[ctl:10]] | Elige un archivo de efecto de tu disco. |
| [[ctl:11]] | Muestra una explicación corta. |

Los dos campos sobre los botones contienen la ruta del efecto y la carpeta que va delante.

## Guía de visibilidad de animaciones

[[shot:obj_popcorn_r3|Guía de visibilidad de animaciones.]]

| N.º | Qué hace |
|---|---|
| [[ctl:4]] | **Activado:** el efecto se reproduce en todas las animaciones. **Desactivado:** no se reproduce en ninguna. |
| [[ctl:6]] | Excepciones: en estas animaciones el efecto hace lo contrario. |
| [[ctl:8]] | Quita la excepción seleccionada. |
| [[ctl:10]] | Elige una animación de la escena … |
| [[ctl:12]] | … y añádela como excepción. |

## Ajustes de emisión

[[shot:obj_popcorn_r4|Ajustes de emisión.]]

| N.º | Qué hace |
|---|---|
| [[ctl:6]] | Transparencia de las partículas. |
| [[ctl:10]] | Partículas por segundo. |
| [[ctl:14]] | Tiempo de vida de una partícula, en segundos. |
| [[ctl:18]] | Velocidad de las partículas. |

## Ajustes de color

[[shot:obj_popcorn_r5|Ajustes de color.]]

| N.º | Qué hace |
|---|---|
| [[ctl:4]] | El color base del efecto. |
| [[ctl:12]] | Multiplica el color, así el efecto se vuelve más claro o más oscuro. |

## Marcadores de renderizado

[[shot:obj_popcorn_r6|Marcadores de renderizado.]]

| N.º | Qué hace |
|---|---|
| [[ctl:4]] | Las partículas ignoran la luz de la escena. |
| [[ctl:6]] | Las partículas ignoran la niebla. |
| [[ctl:8]] | Las partículas crecen y se encogen con el modelo. |
