# Gestor de secuencias

Un modelo de Warcraft III guarda todas sus animaciones en una sola línea de tiempo. Cada animación es una **secuencia**: un nombre, un fotograma inicial y un fotograma final. El Gestor de secuencias crea y cambia estas secuencias.

Ábrelo con **WhiteoutDex > [[t:menu_sequence_manager]]** o con el botón **Secuencias** de la barra lateral.

[[shot:sequence_manager_selected|El Gestor de secuencias con la secuencia "Stand - 1" seleccionada.]]

## La lista

| N.º | Qué hace |
|---|---|
| [[ctl:4]] | Escribe una palabra para mostrar solo las secuencias que tienen esa palabra en el nombre. |
| [[ctl:6]] | Vacía el campo de búsqueda y muestra otra vez todas las secuencias. |
| [[ctl:7]] | Todas las secuencias del modelo. Haz clic para seleccionar una. Mantén pulsado [[key:Ctrl]] para seleccionar más. **Haz doble clic** en una secuencia para reproducirla en el visor. Las líneas como `[GLOBAL 1]` son secuencias globales. |
| [[ctl:10]] | Selecciona todas las secuencias. |
| [[ctl:11]] | No selecciona ninguna. |
| [[ctl:12]] | Selecciona las que no están seleccionadas, y al revés. |

## Crear y quitar

| N.º | Qué hace |
|---|---|
| [[ctl:14]] | Crea una secuencia nueva (consulta más abajo). |
| [[ctl:15]] | Borra las secuencias seleccionadas. Las claves de animación se quedan en la línea de tiempo. |
| [[ctl:16]] | Copia las secuencias seleccionadas. |
| [[ctl:17]] | Abre el *Gestor de secuencias globales*. |

## Cambiar la secuencia seleccionada

| N.º | Qué hace |
|---|---|
| [[ctl:20]] | El nombre. El nombre indica al juego cuándo reproducir la animación. Consulta *Nombres de animación*. |
| [[ctl:23]] | Usa el fotograma actual del control deslizante de tiempo como fotograma inicial. |
| [[ctl:26]] | El primer fotograma de la secuencia. |
| [[ctl:28]] | Usa el fotograma actual como fotograma final. |
| [[ctl:31]] | El último fotograma de la secuencia. |
| [[ctl:32]] | **Rareza** (0–40). Para animaciones con el mismo nombre, un valor más alto significa que el juego reproduce esta menos veces. |
| [[ctl:36]] | **Velocidad de movimiento** (0–700). Con qué rapidez se mueve la unidad cuando reproduce esta animación de caminar. La mayoría de las unidades tienen de 250 a 400. |
| [[ctl:40]] | La animación se reproduce una vez y no se repite. Úsalo para *Death*, *Attack* y *Spell*. |
| [[ctl:41]] | El exportador deja fuera esta secuencia. |
| [[ctl:44]] | Mueve el fotograma inicial un fotograma antes. El botón [[ctl:45]] lo mueve un fotograma después. |
| [[ctl:47]] | Mueve el fotograma final un fotograma antes. El botón [[ctl:48]] lo mueve un fotograma después. |

## Guardar y cargar

| N.º | Qué hace |
|---|---|
| [[ctl:50]] | Carga secuencias desde un archivo de texto. |
| [[ctl:51]] | Guarda las secuencias en un archivo de texto. Úsalo para copiarlas a otra escena. |

## Crear una secuencia

[[shot:sequence_new|La ventana para una secuencia nueva.]]

1. Haz clic en [[t:seq_new_btn]].
2. Elige un preajuste, por ejemplo *Walk*. El preajuste define un buen nombre y una buena duración.
3. Comprueba el nombre y los fotogramas. La secuencia nueva empieza después de la última.
4. Haz clic en **Aceptar**.

| N.º | Qué hace |
|---|---|
| [[ctl:4]] | Preajustes para las animaciones comunes, o *Custom* (personalizado). |
| [[ctl:7]] | El nombre de la secuencia nueva. |
| [[ctl:12]] | Primer fotograma. |
| [[ctl:16]] | Último fotograma. |
| [[ctl:17]] | Activa el valor de rareza. |
| [[ctl:21]] | Activa la velocidad de movimiento. |
| [[ctl:25]] | La animación se reproduce solo una vez. |

!!! tip
    Deja unos fotogramas vacíos entre dos secuencias. Así las claves de una animación no cambian la siguiente.

!!! note
    WhiteoutDex guarda las secuencias en tu archivo `.max`. Las escenas antiguas con secuencias en *note tracks* siguen funcionando: WhiteoutDex también las lee.
