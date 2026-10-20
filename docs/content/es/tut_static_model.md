# Tutorial: tu primer modelo propio

En esta lección creas un objeto sencillo desde cero: una caja de madera, por ejemplo un doodad para un mapa.

## Paso 1: modela la caja

1. Crea un **Box**: *Create > Geometry > Box*. Hazlo de unas 60 × 60 × 60 unidades. Una unidad en 3ds Max es una unidad en el juego.
2. Colócalo de forma que esté sobre el suelo (la base a la altura 0) y su centro esté en 0, 0.

## Paso 2: dale un material

1. Abre el Material Editor ([[key:M]]).
2. Elige una ranura libre, haz clic en el botón del tipo de material y elige **Wc3Material**.
3. En *Mapas de textura del material*, haz clic en el botón **Difusa** y elige **Wc3Bitmap**.
4. Haz clic en **Elegir bitmap...** y elige una textura. O haz clic en **Abrir el Explorador de texturas** y toma una del juego (busca `crate` o `wood`).
5. Arrastra el material sobre la caja.

![El Wc3Material con sus ranuras de textura.](mat_wc3material_r3.png)

!!! tip
    ¿Ya tienes un material Standard o Physical? *Corrección de materiales* lo convierte por ti en un Wc3Material.

## Paso 3: colisión

El jugador debe poder hacer clic en la caja.

1. Selecciona la caja.
2. Abre **WhiteoutDex > [[t:menu_anim_general]]** y haz clic en **Caja de colisión de un clic**.

## Paso 4: una animación Stand

Todo modelo necesita al menos una secuencia, aunque nada se mueva.

1. Abre **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Haz clic en **Nueva...**, elige el preajuste *Stand* y haz clic en **Aceptar**.

## Paso 5: exporta y prueba

1. Exporta la caja como `Crate.mdx` (consulta la lección *Exportar un modelo*).
2. Mira el cuadro de la ventana de exportación: si es amarillo, haz clic en [[t:exp_fix_all_btn]].
3. Mira el resultado en el previsualizador: **WhiteoutDex > [[t:menu_renderer]]**.

Has creado tu primer modelo de Warcraft III.
