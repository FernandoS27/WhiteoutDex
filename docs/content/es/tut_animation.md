# Tutorial: animar

En esta lección das dos animaciones a la caja de la lección anterior: *Stand* (no hace nada) y *Death* (se rompe).

## Paso 1: planifica la línea de tiempo

Todas las animaciones de un modelo comparten una línea de tiempo. Cada animación recibe su propia parte:

| Secuencia | Fotogramas |
|---|---|
| Stand | 0 – 80 |
| Death | 100 – 164 |

Deja unos fotogramas libres entre las partes.

## Paso 2: crea las secuencias

1. Abre **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Haz clic en **Nueva...**, elige *Stand*, pon el inicio en 0 y el fin en 80, y haz clic en **Aceptar**.
3. Haz clic en **Nueva...**, elige *Death*, pon el inicio en 100 y el fin en 164, activa **Sin bucle** y haz clic en **Aceptar**.

![El Gestor de secuencias con una secuencia seleccionada.](sequence_manager_selected.png)

## Paso 3: pon claves

1. Activa **Auto Key** en 3ds Max.
2. Pon claves al inicio y al final de cada secuencia: abre **WhiteoutDex > [[t:menu_anim_general]]**, selecciona la caja y haz clic en **Añadir claves inicial y final a los objetos seleccionados**.
3. Ve al fotograma 130. Gira la caja y bájala, como si se cayera.
4. Ve al fotograma 164 y muévela por debajo del suelo.
5. Desactiva **Auto Key**.

## Paso 4: prueba

1. En el Gestor de secuencias, haz doble clic en *Death*. 3ds Max la reproduce.
2. Abre el previsualizador (**WhiteoutDex > [[t:menu_renderer]]**) y elige *Death* en la lista **Animation**.

!!! tip
    Los huesos se animan mejor que las mallas: vincula las partes de un modelo a huesos y anima los huesos. Después usa un modificador **Skin** en las mallas.

!!! note
    El juego reproduce *Stand* una y otra vez. *Death* se reproduce una vez. Después el cuerpo se queda hasta las animaciones *Decay* (si el modelo las tiene).
