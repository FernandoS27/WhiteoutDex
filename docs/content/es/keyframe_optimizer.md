# Optimizador de fotogramas clave

Las animaciones de otros programas o de captura de movimiento suelen tener una clave en cada fotograma. Eso hace grande el modelo. El Optimizador de fotogramas clave quita las claves que no hacen falta, y el movimiento queda casi igual.

Ábrelo con **WhiteoutDex > [[t:menu_keyframe_optimizer]]**. Antes, selecciona los objetos animados.

[[shot:keyframe_optimizer|El Optimizador de fotogramas clave.]]

## Ajustes

| N.º | Qué hace |
|---|---|
| [[ctl:7#2]] | **Umbral:** cuánto puede cambiar la curva. Un valor más alto quita más claves. Empieza con 0.5. |
| [[ctl:11#2]] | **Paso:** con qué precisión mide la herramienta, en fotogramas. Quédate por debajo de 0.49. |
| [[ctl:13#2]] | Escribe detalles en el MAXScript Listener. Déjalo desactivado, porque es más lento. |

## Optimizar

| N.º | Qué hace |
|---|---|
| [[ctl:3]] | Ordena la lista por tiempo o por nombre. |
| [[ctl:5]] | Las secuencias del modelo. Selecciona las que quieres optimizar. |
| [[ctl:7]] | Optimiza las claves de posición. |
| [[ctl:8]] | Optimiza las claves de rotación. |
| [[ctl:9]] | Optimiza las claves de escala. |
| [[ctl:14]] | Optimiza las secuencias seleccionadas. |
| [[ctl:15]] | Optimiza todas las secuencias. |
| [[ctl:16]] | Borra las claves que no están dentro de ninguna secuencia. No hacen nada en el juego. |

!!! tip
    Guarda tu escena antes de optimizar. Después comprueba la animación. Si algo se ve mal, deshaz y prueba un umbral más pequeño.
