# Secuencias globales

Una **secuencia global** es una animación que se repite siempre, sin importar qué animación reproduce la unidad. Úsala para cosas que nunca se detienen: un cristal que gira, una antorcha que arde, una bandera al viento.

Cada secuencia global tiene una duración en fotogramas. Las claves de un objeto pueden pertenecer a una secuencia global en lugar de a la línea de tiempo normal.

Abre el Gestor de secuencias globales con el botón [[t:seq_globals_btn]] del *Gestor de secuencias*.

[[shot:global_sequences|El Gestor de secuencias globales con las dos secuencias globales del modelo de ejemplo.]]

La lista de arriba muestra todas las secuencias globales: número, fotogramas, tipo y los objetos que la usan.

| N.º | Qué hace |
|---|---|
| [[ctl:7]] | El fotograma final (duración) de la secuencia global seleccionada. |
| [[ctl:8]] | Lee la escena otra vez. |
| [[ctl:9]] | Quita la secuencia global seleccionada. |
| [[ctl:15]] | La duración de una nueva secuencia global, en fotogramas. |
| [[ctl:24]] | Crea una nueva secuencia global en los objetos seleccionados. |
| [[ctl:25]] | Cambia la secuencia global seleccionada. |

## Hacer que un objeto gire siempre

1. Selecciona el objeto.
2. Define la **Duración**, por ejemplo 60 fotogramas.
3. Haz clic en **Nueva**. El control deslizante de tiempo va al fotograma 0 y aparecen las opciones de controlador, canales y giro.
4. Elige el **Controlador**, por ejemplo *Linear*, y el eje.
5. Mueve el control deslizante de tiempo al último fotograma de una vuelta, por ejemplo 60.
6. Haz clic en **> Horario** o en **Antihorario <**.
7. Haz clic en **Finalizar**.

Ahora el objeto da una vuelta en 60 fotogramas, en todas las animaciones.

También puedes animar a mano: después de **Nueva**, pon claves en el objeto como siempre, dentro de la duración. Después haz clic en **Finalizar**. WhiteoutDex mueve estas claves a la nueva secuencia global.

!!! note
    El juego cuenta las secuencias globales en milisegundos. WhiteoutDex convierte los fotogramas por ti.
