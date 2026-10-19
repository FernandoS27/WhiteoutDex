# Tutorial: anclajes y eventos

Una unidad del juego necesita lugares para los efectos (puntos de anclaje) y momentos para los sonidos y las huellas (objetos de evento). En esta lección añades los dos a una unidad.

**Necesitas:** una unidad con huesos y una animación *Walk*, por ejemplo el Paladin importado.

## Parte 1: puntos de anclaje

### La forma rápida

Si tus huesos tienen nombres habituales (*Head*, *Chest*, *Hand*, *Foot* …):

1. Abre **WhiteoutDex > [[t:menu_anim_general]]**.
2. Haz clic en **Puntos de anclaje de un clic**. WhiteoutDex crea los puntos y los vincula a los huesos.

### A mano

1. Crea un **Wc3 AttachPoint** (*Create > Helpers > Warcraft III Plugins*).
2. Haz clic en **Seleccionar objeto (absorber)** y haz clic en el hueso, por ejemplo la mano derecha. El punto salta allí y se vincula a él.
3. En **Parámetros del anclaje**, elige en **Tipo** el valor *Hand* y en **Lado** el valor *Right*. Ahora el punto se llama *Hand Right Ref*.

El juego muestra allí los efectos de hechizos de la mano derecha.

![Parámetros del anclaje de un punto de anclaje.](obj_attachpoint_r3.png)

**Puntos que toda unidad debe tener:** *Origin*, *Head*, *Chest*, *Overhead*. Las armas usan *Hand Left/Right* o *Weapon*.

## Parte 2: huellas

1. Crea un **Wc3 Event Object** y colócalo en el pie izquierdo. Vincúlalo al hueso del pie.
2. En **Tipo de evento**, elige el tipo *FootPrint*, y en **Datos** una huella, por ejemplo una pequeña.
3. Haz clic en **Confirmar**.
4. Abre la secuencia *Walk* en el Gestor de secuencias. Mueve el control deslizante de tiempo al fotograma en que el pie izquierdo toca el suelo.
5. Haz clic en **Añadir**. Repítelo para cada paso del pie izquierdo.
6. Haz lo mismo para el pie derecho con un segundo objeto de evento.

## Parte 3: un sonido

1. Crea otro objeto de evento cerca del arma.
2. Elige el tipo *Sound* y un sonido de golpe en **Datos**. Después haz clic en **Confirmar**.
3. Ve al fotograma de la animación *Attack* en que el arma golpea y haz clic en **Añadir**.

## Compruébalo

Abre el previsualizador. Con **View > Event Objects** activado, muestra las huellas y reproduce los sonidos.

!!! tip
    Mira un modelo del juego (*Importar desde el juego*): copia cómo coloca Blizzard los puntos de anclaje y los eventos.
