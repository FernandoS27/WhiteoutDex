# Generador de claves de visibilidad

En Warcraft III muestras y ocultas partes de un modelo con **claves de visibilidad**. Por ejemplo, un arma que solo aparece en la animación de ataque, o un brillo que solo aparece mientras la unidad lanza un hechizo. El Generador de claves de visibilidad pone estas claves por ti.

Ábrelo con **WhiteoutDex > [[t:menu_visibility_keyer]]**. Antes, selecciona los objetos.

[[shot:visibility_keyer|El Generador de claves de visibilidad.]]

| N.º | Qué hace |
|---|---|
| [[ctl:3]] | Muestra u oculta los objetos seleccionados en el fotograma actual. |
| [[ctl:5]] | Añade un controlador de visibilidad a todos los objetos seleccionados. |
| [[ctl:6]] | Quita los controladores de visibilidad de los objetos seleccionados. |
| [[ctl:7]] | Copia las claves de visibilidad del objeto seleccionado. |
| [[ctl:8]] | Las pega en los objetos seleccionados. |
| [[ctl:9]] | Cómo cambia el juego entre las claves: *None* salta de golpe, los otros tipos hacen un fundido. |
| [[ctl:11]] | Da a los controladores existentes el tipo que elegiste. |
| [[ctl:13]] | Pone una clave de *mostrar* en el fotograma actual. |
| [[ctl:14]] | Pone una clave de *ocultar* en el fotograma actual. |
| [[ctl:16]] | Hace visible el objeto en todo el rango de animación. |
| [[ctl:17]] | Oculta el objeto en todo el rango de animación. |
| [[ctl:19]] | Muestra el objeto en las secuencias que seleccionaste en el *Gestor de secuencias*. |
| [[ctl:20]] | Oculta el objeto en las secuencias seleccionadas. |

## Ejemplo: un arma solo para el ataque

1. Selecciona el arma.
2. Haz clic en [[ctl:17]] para ocultarla en todas partes.
3. En el *Gestor de secuencias*, selecciona las secuencias *Attack*.
4. Haz clic en [[ctl:19]].

!!! tip
    Usa la interpolación *None* para la mayoría de las claves de visibilidad. Con otros tipos el juego hace un fundido del objeto, y eso se ve mal en la mayoría de las partes.
