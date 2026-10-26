# Wc3 Vertex Color

El modificador **Wc3 Vertex Color** colorea una malla entera. Puedes animar el color, por ejemplo para que un cristal palpite o para que una parte brille en rojo cuando la unidad recibe un golpe. El exportador lo escribe como el color de *animación de geoset* de la malla.

Añádelo desde la lista de modificadores: selecciona una malla y elige **Wc3 Vertex Color**.

[[shot:obj_vertexcolor_r1|Parámetros básicos del modificador.]]

| N.º | Qué hace |
|---|---|
| [[ctl:4]] | El color con el que se tiñe la malla. El blanco deja la textura como está. Anímalo con **Auto Key**. |
| [[ctl:7]] | Activa el marcador *Drop Shadow* de la malla. |
| [[ctl:9]] | Usa el color. Si está desactivado, solo se escribe el marcador. |

!!! tip
    Para que una parte aparezca y desaparezca poco a poco, usa claves de visibilidad (*Generador de claves de visibilidad*) o la *Opacidad* del Wc3Material.
