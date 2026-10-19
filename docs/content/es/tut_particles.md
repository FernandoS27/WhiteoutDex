# Tutorial: un efecto de fuego

En esta lección pones fuego en una antorcha con un **emisor de partículas 2**.

## Paso 1: crea el emisor

1. Elige *Create > Geometry > Particle Systems > Wc3Particles2*.
2. Haz clic en el visor para colocarlo. Muévelo a la parte superior de la antorcha.
3. Vincúlalo a la antorcha (o al hueso que sostiene la antorcha), así se mueve con ella.

## Paso 2: la textura

1. Abre la persiana **Texture Options**.
2. Haz clic en **Explorar MPQ / CASC** y elige una textura de fuego del juego (busca `fire` o `flare`).
3. Pon **Modo de mezcla de la textura** en **Additive**. Las partículas aditivas brillan.

## Paso 3: el movimiento

En **Emitter Options** y **Timing Options**:

1. **Velocidad:** un valor pequeño, así las llamas suben despacio.
2. **Ángulo del cono:** pequeño, así las llamas se quedan juntas.
3. **Vida útil:** alrededor de 1 segundo.
4. **Tasa de emisión:** alrededor de 20 partículas por segundo.

Ves las partículas en el visor enseguida. Cambia los valores hasta que te guste el fuego.

## Paso 4: color y fundido

En **Particle Options**:

1. **Color:** amarillo al inicio, naranja en el medio, rojo oscuro al final.
2. **Alfa:** 0 al inicio, 255 en el medio, 0 al final. Las llamas aparecen y desaparecen poco a poco.
3. **Escala:** pequeña al inicio, más grande en el medio, pequeña al final.

![La persiana Particle Options de un emisor.](obj_particles2_r6.png)

## Paso 5: compruébalo

Abre el previsualizador (**WhiteoutDex > [[t:menu_renderer]]**). Muestra el fuego como lo mostrará el juego.

!!! tip
    Guarda un fuego que te guste con **Exportar** en *Particle Configuration*. Cárgalo en otros modelos con **Importar**.
