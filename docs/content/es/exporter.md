# El exportador

El exportador guarda tu escena como un modelo de Warcraft III (`.mdx` o `.mdl`). También puede convertir tus texturas a los formatos del juego.

## Abrir el exportador

- Menú **WhiteoutDex > [[t:menu_exporter]]**
- El botón **Exportar** de la barra lateral de WhiteoutDex
- **File > Export** y elige *Warcraft III MDX/MDL Model*

Elige un nombre de archivo. Después se abre la ventana de exportación.

## La ventana de exportación

[[shot:exporter|La ventana de exportación con la pestaña Geometría.]]

La parte superior de la ventana muestra el nombre del archivo. Debajo ves si el archivo es nuevo o si ya existe un archivo con ese nombre.

| N.º | Qué hace |
|---|---|
| [[ctl:1001]] | **Nombre del modelo.** El nombre dentro del archivo. El juego no lo muestra, pero otras herramientas sí. |
| [[ctl:1011]] | Exporta para el juego clásico (MDX versión 800). Las texturas se convierten en archivos `.blp`. |
| [[ctl:1012]] | Exporta para Reforged (MDX versión 1800). Las texturas se convierten en archivos `.dds`. |
| [[ctl:1015]] | Muestra las opciones de malla (esta imagen). |
| [[ctl:1016]] | Muestra las opciones de textura. |
| [[ctl:1021]] | Une en una sola malla las mallas que usan el mismo material. El modelo es más pequeño y más rápido. |
| [[ctl:1027]] | Iguala las normales donde dos mallas se tocan. Esto oculta las costuras. No lo uses si usas grupos de suavizado para bordes duros. |
| [[ctl:1028]] | Exporta también los huesos y ayudantes que nada usa. Normalmente el exportador los deja fuera. |
| [[ctl:1032]] | Cambia los pesos de skin para que funcionen en el juego clásico. El juego clásico solo puede usar pesos simples. Solo para [[t:exp_format_classic_title]]. |
| [[ctl:1030]] | Si el archivo ya existe, el exportador añade `_1`, `_2`, etc. al nombre. Tu archivo antiguo se queda. |
| [[ctl:1031]] | Abre la carpeta del archivo después de exportar. |
| [[ctl:1]] | Inicia la exportación. |
| [[ctl:2]] | Cierra la ventana sin exportar. |

El cuadro verde debajo de las opciones te dice que WhiteoutDex no encontró problemas en tu escena. Cuando encuentra problemas, el cuadro es amarillo. Consulta *Problemas de la escena y Corregir todo*.

### Pestaña Texturas

[[shot:exporter_textures|La pestaña Texturas, aquí para una exportación clásica.]]

| N.º | Qué hace |
|---|---|
| [[ctl:1060]] | Convierte las texturas de tus materiales a `.blp` (Clásico) o `.dds` (Reforged) y las guarda junto al modelo. |
| [[ctl:1071]] | **Solo Clásico.** [[t:exp_blp_paletted_item]] o *JPEG*. JPEG tiene más colores. Los archivos con paleta son más pequeños. Con JPEG defines también la calidad (1–100). |
| [[ctl:1075]] | Mezcla un poco los colores. Esto oculta los saltos bruscos entre colores en las texturas con paleta. |
| [[ctl:1061]] | Crea las versiones más pequeñas de la textura que el juego usa para los modelos lejanos. Déjalo activado. |
| [[ctl:1062]] | Reemplaza los archivos de textura que ya existen. Si está desactivado, el exportador conserva los archivos existentes. |

Para **Reforged** eliges en su lugar el formato DDS: *BC3 (DXT5)* o *BC7*. BC7 se ve mejor. BC3 funciona en tarjetas gráficas más antiguas.

## ¿Clásico o Reforged?

| | Clásico | Reforged |
|---|---|---|
| Juego | Warcraft III 1.x y los gráficos clásicos de Reforged | Gráficos de Reforged (HD) |
| Versión de MDX | 800 | 1800 |
| Texturas | `.blp` | `.dds` |
| Skin | Pesos simples (usa [[t:exp_quantize_skin_weights_chk]]) | Pesos completos |

!!! tip
    El exportador recuerda todas las opciones para la próxima exportación.
