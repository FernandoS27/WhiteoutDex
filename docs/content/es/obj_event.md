# Objeto de evento

Un objeto de evento inicia algo en un momento exacto de una animación: un sonido cuando la espada golpea, una huella cuando el pie toca el suelo, sangre cuando la unidad muere, un modelo que aparece.

Créalo con **Create > Helpers > Warcraft III Plugins > Wc3 Event Object**. Colócalo donde debe ocurrir el evento y vincúlalo a un hueso si debe moverse.

En el visor, cada objeto de evento muestra su nombre en verde.

[[shot:obj_event_r2|La persiana Objeto de evento de un sonido de muerte del modelo de ejemplo.]]

## Elige el evento

| N.º | Qué hace |
|---|---|
| [[ctl:5]] | El tipo de evento (consulta la tabla de abajo). |
| [[ctl:8]] | El evento exacto de la lista del juego, por ejemplo un sonido o un color de sangre. |
| [[ctl:13]] | Usa tu elección y da nombre al objeto. La línea bajo el botón muestra el código del evento. |

También puedes escribir tú mismo un código de evento en *Datos personalizados*.

| Tipo | El código empieza por | Qué pasa |
|---|---|---|
| FootPrint | `FPT` | Una huella en el suelo. |
| BloodSplat | `SPL` | Una salpicadura en el suelo, por ejemplo sangre. |
| UberSplat | `UBR` | Una marca grande en el suelo, por ejemplo un cráter o la base de un edificio. |
| Sound | `SND` | Un sonido. |
| SpawnObject | `SPN` | Aparece un modelo, por ejemplo una parte del cuerpo que cae. |

## Cuándo ocurre

| N.º | Qué hace |
|---|---|
| [[ctl:19]] | Los momentos en que ocurre el evento. |
| [[ctl:20]] | Mueve el control deslizante de tiempo al fotograma correcto y haz clic aquí para añadir un momento. |
| [[ctl:21]] | Quita el momento seleccionado. |
| [[ctl:22]] | Quita todos los momentos. |

!!! tip
    Usa el *Gestor de secuencias* para saltar a la animación. Después mueve el control deslizante fotograma a fotograma para encontrar el momento exacto, por ejemplo cuando el pie toca el suelo.
