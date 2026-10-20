# Wc3Material

Cada malla que exportas necesita un **Wc3Material**. Contiene las texturas e indica al juego cómo dibujarlas. El importador crea los Wc3Material por ti. Para tus propias mallas, crea uno en el Material Editor o convierte tus materiales con *Corrección de materiales*.

Las imágenes muestran un material del modelo de ejemplo en el Compact Material Editor.

## Parámetros básicos del material

[[shot:mat_wc3material_r1|Parámetros básicos del material.]]

| N.º | Qué hace |
|---|---|
| [[ctl:3]] | **Shader.** *SD* para modelos clásicos, *HD* para Reforged, *SD on HD* para texturas clásicas en un modelo de Reforged, *Crystal* para el shader de cristal de Reforged. |
| [[ctl:5]] | **Modo de filtro**: cómo se dibuja el material (consulta la tabla de abajo). |
| [[ctl:8]] | Se dibujan los dos lados de las caras. |
| [[ctl:9]] | Ignora la luz. Sirve para las partes que brillan. |
| [[ctl:10]] | Ignora la niebla. |
| [[ctl:11]] | Un solo color para toda la malla. |
| [[ctl:12]] | Activa el marcador *Full Resolution* del material. |
| [[ctl:13]] | Se dibuja encima de otras cosas. |
| [[ctl:14]] | No oculta lo que hay detrás. |
| [[ctl:15]] | El jugador no puede hacer clic en esta parte. |
| [[ctl:16]] | Usa oclusión ambiental (Reforged). |
| [[ctl:17]] | Las caras traseras proyectan sombras (Reforged). |
| [[ctl:18]] | Sin ninguna iluminación (Reforged). |
| [[ctl:20]] | Orden de dibujo de las capas transparentes: de cerca a lejos o de lejos a cerca. |
| [[ctl:24]] | Orden de dibujo entre materiales (−20 a 20). Un valor más alto se dibuja después. |
| [[ctl:26]] | Usa una textura que pone el juego: color de equipo, brillo de equipo, acantilado o una de las texturas de árboles. |

| Modo de filtro | Aspecto | Úsalo para |
|---|---|---|
| Ninguno | Sólido. | Partes normales. |
| Transparente | Agujeros donde el alfa es bajo, bordes duros. | Hojas, pelo, vallas. |
| Mezcla | Transparencia suave. | Cristal, bordes suaves, sombras. |
| Aditivo | Añade luz, brilla. El negro es invisible. | Fuego, magia, rayos de luz. |
| AddAlpha | Añade luz, usa el alfa. | Brillos con una forma. |
| Modulación | Oscurece lo que hay detrás. | Marcas oscuras, sombras. |
| Modulación 2x | Oscurece o aclara. | Efectos especiales. |

## Animación de la capa

[[shot:mat_wc3material_r2|Animación de la capa.]]

| N.º | Qué hace |
|---|---|
| [[ctl:6]] | Opacidad de la capa (0 a 1). Anímala para que el material aparezca y desaparezca poco a poco. |
| [[ctl:7]] | Cómo cambia la opacidad entre las claves. |
| [[ctl:9]] | Escribe la animación de opacidad en el modelo. |
| [[ctl:11]] | La textura cambia por pasos con el tiempo, como un flipbook. |
| [[ctl:14]] | Qué canal UV usa la textura. |
| [[ctl:15]] | Escribe la animación de cambio de textura en el modelo. |

Las texturas que se mueven (animación UV) se definen en el *Wc3Bitmap*, no aquí.

## Mapas de textura del material

[[shot:mat_wc3material_r3|Mapas de textura del material.]]

| N.º | Qué hace |
|---|---|
| [[ctl:4]] | **Difusa:** la textura de color. Los modelos clásicos solo usan esta. |
| [[ctl:5]] | Abre el explorador de texturas para esta ranura (texturas del juego). |
| [[ctl:10]] | **Normal:** los detalles de la superficie (Reforged). |
| [[ctl:16]] | **ORM:** oclusión, rugosidad y metal en una sola textura (Reforged). |
| [[ctl:22]] | **Emisiva:** partes que brillan por sí mismas (Reforged). |
| [[ctl:28]] | **Color de equipo:** una máscara para el color de equipo (Reforged, opcional). |
| [[ctl:34]] | **Entorno:** la textura de reflejo (Reforged). |

Cada ranura tiene un **Prefijo de ruta**: la carpeta que el exportador escribe delante del nombre del archivo, por ejemplo `Textures\`.

## Ajustes de Reforged

[[shot:mat_wc3material_r4|Ajustes de Reforged.]]

| N.º | Qué hace |
|---|---|
| [[ctl:5]] | Con qué fuerza brilla la textura emisiva. |
| [[ctl:9]] | Color de Fresnel (luz de borde): rojo … |
| [[ctl:12]] | … verde … |
| [[ctl:15]] | … y azul. |
| [[ctl:18]] | Intensidad de la luz de borde. |
| [[ctl:21]] | Cuánto color de equipo usa la luz de borde. |
