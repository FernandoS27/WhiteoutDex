# Herramientas de manipulación de objetos

Copia y pega posiciones, vínculos y posiciones de vértices entre objetos. Sirve para modelos simétricos: construye el lado izquierdo y después cópialo al lado derecho.

Abre las herramientas con **WhiteoutDex > [[t:menu_object_tools]]**.

[[shot:object_tools|Las Herramientas de manipulación de objetos con una malla seleccionada.]]

## Objetos

| N.º | Qué hace |
|---|---|
| [[ctl:5]] | **Jerarquía:** guarda los vínculos con el padre de los objetos seleccionados y los desvincula. |
| [[ctl:6]] | Vuelve a vincular los objetos a sus padres guardados. |
| [[ctl:8]] | **Posición:** copia la posición del primer objeto seleccionado. |
| [[ctl:9]] | Mueve los objetos seleccionados a la posición copiada. |
| [[ctl:11]] | **Estructura (I↔D):** copia las posiciones de todos los objetos seleccionados con sus nombres. |
| [[ctl:12]] | Las pega en los objetos con el nombre reflejado: una posición copiada de `Arm_L` va a `Arm_R`. |
| [[ctl:14]] | Desvincula los objetos por un momento al pegar, así sus hijos no se mueven con ellos. |
| [[ctl:15]] | Refleja la posición al pegar, en los ejes que marcas: [[ctl:16]], [[ctl:17]], [[ctl:18]]. |

## Vértices (herramientas Poly)

Antes, selecciona vértices de un Editable Poly o de un Editable Mesh. La línea sobre los botones muestra el objeto y cuántos vértices seleccionaste.

| N.º | Qué hace |
|---|---|
| [[ctl:22]] | Copia las posiciones de los vértices seleccionados. |
| [[ctl:23]] | Pega las posiciones en los vértices. |
| [[ctl:26]] | Refleja los valores X al pegar. [[ctl:27]] y [[ctl:28]] hacen lo mismo para Y y Z. |
| [[ctl:29]] | Refleja alrededor del centro del mundo. |
| [[ctl:30]] | Refleja alrededor del pivote del objeto. |
| [[ctl:31]] | Refleja alrededor del centro de la selección. |
| [[ctl:35]] | Pega en los mismos números de vértice. Úsalo en la misma malla o en una copia de ella. |
| [[ctl:36]] | Pega en los vértices que seleccionaste, incluso en otra malla. |

!!! tip
    Nombra tus objetos con `_L` y `_R` (o `Left` y `Right`). Así *Estructura (I↔D)* encuentra sola la pareja de cada objeto.
