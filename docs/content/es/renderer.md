# Previsualizador WhiteoutFlakes

**WhiteoutFlakes** muestra tu modelo como lo muestra Warcraft III: con los shaders del juego, el color de equipo, la iluminación, las partículas y las estelas. Sigue a 3ds Max en directo: cuando mueves el control deslizante de tiempo o cambias algo, el previsualizador se actualiza.

Ábrelo con **WhiteoutDex > [[t:menu_renderer]]** o con el botón **Renderizador** de la barra lateral.

![WhiteoutFlakes junto a 3ds Max 2027, con un modelo de Reforged en color de equipo.](media_1.png)

## Barra de herramientas

| Elemento | Qué hace |
|---|---|
| **Animation** | Reproduce una secuencia del modelo, o *Rango libre* para seguir el control deslizante de tiempo de Max. |
| **Camera** | *Free Camera* o una de las cámaras de la escena, por ejemplo la cámara de retrato. |
| **Team color** | El color de jugador del modelo. |
| **Lighting** | *InGame* (como el juego), *Glue* (como los menús) o *Dynamic*. |
| **Sincronizar cámara** | El previsualizador sigue el visor activo de 3ds Max. |
| **Resincronizar** | Lee otra vez toda la escena, por ejemplo después de añadir objetos. |

## Menú

| Menú | Qué contiene |
|---|---|
| **View** | Muestra u oculta la rejilla, las partículas, las estelas y los objetos de evento. *Warcraft III Art*: qué texturas usar (*Follow Render Mode*, *Classic*, *Reforged (HD)*, *Definitive*). *Tileset*: el suelo. |
| **Debug** | Marcadores para las formas de colisión y las luces, vistas especiales (albedo, normales, nivel de detalle, número de luces …) y una elección del nivel de detalle. |
| **Settings** | Fondo, brillo, volumen del sonido, hora del día, iluminación basada en imagen, sombras, niebla, el backend gráfico (Direct3D 11, Direct3D 12, Vulkan) y las carpetas del juego. |

## Es bueno saber

- Las partículas y las estelas se simulan como en el juego. Cuando saltas en el control deslizante de tiempo, el previsualizador calcula los efectos hasta el nuevo fotograma. Así se ven como si la animación se hubiera reproducido hasta allí.
- El previsualizador guarda sus ajustes en `WhiteoutFlakes.ini`, junto a `3dsmax.exe`.
- Si el previsualizador no muestra nada o se cierra con un error, mira `WhiteoutDex_render.log` (consulta *Archivos y ajustes*).

!!! tip
    Elige *Classic* o *Reforged (HD)* en **View > Warcraft III Art** para ver cómo se ve tu modelo en cada versión del juego.
